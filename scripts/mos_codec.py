#!/usr/bin/env python3
"""MOSV1/MOSC <-> image codec for IWD2 UI mosaics. Paletted, 64px tiles, per-tile
256-colour palette (BGRX). Used to build HD (2x) UI MOS for the de-double path."""
import struct, zlib
from PIL import Image

def parse_mos(data):
    if data[:4] == b"MOSC":
        data = zlib.decompress(data[12:])
    assert data[:8] == b"MOS V1  ", data[:8]
    w, h, cols, rows = struct.unpack_from("<HHHH", data, 8)
    tilesize, paloff = struct.unpack_from("<II", data, 0x10)
    n = cols * rows
    palettes = [[struct.unpack_from("<BBBB", data, paloff + t*1024 + i*4) for i in range(256)]
                for t in range(n)]
    offtab = paloff + n*1024
    offsets = list(struct.unpack_from(f"<{n}I", data, offtab))
    pixoff = offtab + n*4
    return dict(w=w, h=h, cols=cols, rows=rows, tilesize=tilesize,
                palettes=palettes, offsets=offsets, pixoff=pixoff, raw=data)

def mos_to_image(m):
    img = Image.new("RGB", (m["w"], m["h"]))
    px = img.load(); ts = m["tilesize"]
    for row in range(m["rows"]):
        for col in range(m["cols"]):
            t = row*m["cols"] + col
            tw = min(ts, m["w"]-col*ts); th = min(ts, m["h"]-row*ts)
            pal = m["palettes"][t]; off = m["pixoff"] + m["offsets"][t]
            d = m["raw"][off:off+tw*th]
            for y in range(th):
                for x in range(tw):
                    b, g, r, _ = pal[d[y*tw+x]]
                    px[col*ts+x, row*ts+y] = (r, g, b)
    return img

def image_to_mos(img, tilesize=64, compress=True, global_palette=True, dither=False):
    """Tile the RGB image -> MOSV1/MOSC. Transparency = (0,255,0) colour key at INDEX 0
    (the stock convention). By default ALL tiles share ONE global 255-colour palette,
    which eliminates the inter-tile palette seams a per-tile median-cut leaves on smooth
    (AI-upscaled) gradients. global_palette=False = per-tile palettes (finer colour, seams)."""
    img = img.convert("RGB")
    w, h = img.size
    cols = (w + tilesize - 1)//tilesize
    rows = (h + tilesize - 1)//tilesize
    data = list(img.getdata())
    gs = set(i for i, p in enumerate(data) if p == (0, 255, 0))
    dmode = Image.FLOYDSTEINBERG if dither else Image.NONE

    def pal255_bgrx(getpal):                      # idx0 = green key, 1..255 from getpalette()
        pal = list(getpal[:255*3]); pal += [0]*(255*3 - len(pal))
        blob = bytearray(struct.pack("<BBBB", 0, 255, 0, 0))
        for i in range(255):
            r, g, bb = pal[i*3], pal[i*3+1], pal[i*3+2]
            blob += struct.pack("<BBBB", bb, g, r, 0)
        return bytes(blob)

    pal_blob = bytearray(); pix_blob = bytearray(); offsets = []
    if global_palette:
        opaque = Image.new("RGB", img.size)
        opaque.putdata([(0, 0, 0) if i in gs else p for i, p in enumerate(data)])
        gq = opaque.quantize(colors=255, method=Image.MEDIANCUT, dither=dmode)
        gbgrx = pal255_bgrx(gq.getpalette())
        gidx = list(gq.getdata())
        full = bytes(0 if i in gs else gidx[i] + 1 for i in range(len(data)))
        for row in range(rows):
            for col in range(cols):
                tw = min(tilesize, w-col*tilesize); th = min(tilesize, h-row*tilesize)
                pal_blob += gbgrx                 # same palette in every tile slot (MOSC dedups)
                tilepx = bytearray()
                for yy in range(th):
                    base = (row*tilesize+yy)*w + col*tilesize
                    tilepx += full[base:base+tw]
                offsets.append(len(pix_blob)); pix_blob += tilepx
    else:
        for row in range(rows):
            for col in range(cols):
                tw = min(tilesize, w-col*tilesize); th = min(tilesize, h-row*tilesize)
                tile = img.crop((col*tilesize, row*tilesize, col*tilesize+tw, row*tilesize+th))
                td = list(tile.getdata())
                tgs = set(i for i, p in enumerate(td) if p == (0, 255, 0))
                if tgs:
                    op = Image.new("RGB", tile.size)
                    op.putdata([(0, 0, 0) if i in tgs else p for i, p in enumerate(td)])
                    q = op.quantize(colors=255, method=Image.MAXCOVERAGE, dither=dmode)  # MAXCOVERAGE preserves saturated accents (gold) vs MEDIANCUT starving them in grey-dominated tiles
                    pal_blob += pal255_bgrx(q.getpalette())
                    qi = list(q.getdata())
                    pix = bytes(0 if i in tgs else qi[i] + 1 for i in range(len(td)))
                else:
                    q = tile.quantize(colors=256, method=Image.MAXCOVERAGE, dither=dmode)  # preserve saturated accents
                    pal = q.getpalette()[:256*3]; pal += [0]*(256*3 - len(pal))
                    for i in range(256):
                        r, g, bb = pal[i*3], pal[i*3+1], pal[i*3+2]
                        pal_blob += struct.pack("<BBBB", bb, g, r, 0)
                    pix = q.tobytes()
                offsets.append(len(pix_blob)); pix_blob += pix
    header = bytearray(0x18)
    header[0:8] = b"MOS V1  "
    struct.pack_into("<HHHH", header, 8, w, h, cols, rows)
    struct.pack_into("<II", header, 0x10, tilesize, 0x18)        # palettes right after header
    off_blob = b"".join(struct.pack("<I", o) for o in offsets)
    raw = bytes(header) + bytes(pal_blob) + off_blob + bytes(pix_blob)
    if compress:
        return b"MOSC" + b"V1  " + struct.pack("<I", len(raw)) + zlib.compress(raw, 9)
    return raw

if __name__ == "__main__":
    import sys
    m = parse_mos(open(sys.argv[1], "rb").read())
    mos_to_image(m).save(sys.argv[2])
    print(f"{sys.argv[1]}: {m['w']}x{m['h']} {m['cols']}x{m['rows']} tiles -> {sys.argv[2]}")
