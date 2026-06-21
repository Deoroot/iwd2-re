#!/usr/bin/env python3
"""BAM V1 (+BAMC) <-> RGBA-frames codec for IWD2 UI cells (buttons/sliders/etc).
Decode every frame to RGBA (palette index 0 = transparent), and rebuild a BAM that
preserves the frame count, cycle table, frame-lookup table, and the transparent index,
while scaling every frame's metrics (w,h,cx,cy) by S. Used to ship 2x-authored HD UI
button BAMs for the CResCell de-double path (force bDoubleSize=0 -> renders native = crisp).

BAM V1 layout:
  0x00 "BAM V1  "
  0x08 nFrames(H) nCycles(B) rleColor(B)  frameEntriesOff(I)
  0x10 paletteOff(I)  frameLookupOff(I)
  frame entry  (12b): w(H) h(H) cx(h) cy(h) dataOff+rleBit(I)  [bit31 SET = uncompressed]
  cycle entry  (4b) : frameCount(H) firstLookupIndex(H)
  lookup table      : nLookup * H  (cycle entries index into this -> frame indices)
  palette           : 256 * BGRA
  frame data        : RLE (rleColor,count) or raw, index into palette; idx0 transparent
"""
import struct, zlib
from PIL import Image

def _decompress(data):
    if data[:4] == b"BAMC":
        return zlib.decompress(data[12:])     # BAMC: sig(4) "V1  "(4) uncompSize(4) then zlib
    return data

def parse_bam(data):
    data = _decompress(data)
    assert data[:8] == b"BAM V1  ", data[:8]
    nframes, ncycles, rle_color, frame_off = struct.unpack_from("<HBBI", data, 8)
    pal_off, lookup_off = struct.unpack_from("<II", data, 0x10)
    palette = [struct.unpack_from("<BBBB", data, pal_off + i*4) for i in range(256)]  # B,G,R,A
    frames = []
    for i in range(nframes):
        w, h, cx, cy, d = struct.unpack_from("<HHhhI", data, frame_off + i*12)
        frames.append(dict(w=w, h=h, cx=cx, cy=cy, off=d & 0x7FFFFFFF, raw=(d & 0x80000000) != 0))
    # cycles immediately follow the frame entries
    cyc_off = frame_off + nframes*12
    cycles = [struct.unpack_from("<HH", data, cyc_off + i*4) for i in range(ncycles)]  # (count, firstIdx)
    nlookup = max((fi + cnt for cnt, fi in cycles), default=0)
    lookup = list(struct.unpack_from(f"<{nlookup}H", data, lookup_off)) if nlookup else []
    return dict(nframes=nframes, ncycles=ncycles, rle_color=rle_color, palette=palette,
                frames=frames, cycles=cycles, lookup=lookup, raw=data)

def _decode_pixels(m, f):
    w, h = f["w"], f["h"]; src = m["raw"]; pos = f["off"]; total = w*h
    if f["raw"]:
        return bytearray(src[pos:pos+total])
    out = bytearray(total); o = 0; rc = m["rle_color"]
    while o < total:
        b = src[pos]; pos += 1
        if b == rc:
            run = src[pos] + 1; pos += 1
            for _ in range(run):
                if o >= total: break
                out[o] = rc; o += 1
        else:
            out[o] = b; o += 1
    return out

def bam_to_images(m):
    """Each frame -> RGBA (palette idx0 / rle_color -> transparent)."""
    imgs = []
    for f in m["frames"]:
        w, h = f["w"], f["h"]
        if w == 0 or h == 0:
            imgs.append(Image.new("RGBA", (max(w, 1), max(h, 1)), (0, 0, 0, 0))); continue
        px = _decode_pixels(m, f); img = Image.new("RGBA", (w, h)); data = []
        for v in px:
            if v == 0:                                   # idx0 = transparent (stock convention)
                data.append((0, 0, 0, 0))
            else:
                b, g, r, a = m["palette"][v]; data.append((r, g, b, 255))
        img.putdata(data); imgs.append(img)
    return imgs

def images_to_bam(imgs, m, scale=2, transparent=(0, 255, 0), cx_off=0, cy_off=0):
    """Rebuild a BAM from HD RGBA frames (already scaled by `scale`), reusing the cycle/
    lookup tables + transparent index from the source meta `m`. One global 255-colour
    palette (idx0 = transparent key); frames stored raw (bit31 set)."""
    # global palette from all opaque pixels across frames
    opaque = []
    for im in imgs:
        for (r, g, bb, a) in im.getdata():
            if a >= 128:
                opaque.append((r, g, bb))
    if opaque:
        tmp = Image.new("RGB", (len(opaque), 1)); tmp.putdata(opaque)
        q = tmp.quantize(colors=255, method=Image.MEDIANCUT)
        pal = q.getpalette()[:255*3]
    else:
        pal = []
    pal += [0]*(255*3 - len(pal))
    # palette blob: idx0 = transparent, 1..255 = quantized
    pal_blob = bytearray(struct.pack("<BBBB", transparent[2], transparent[1], transparent[0], 0))
    for i in range(255):
        r, g, bb = pal[i*3], pal[i*3+1], pal[i*3+2]
        pal_blob += struct.pack("<BBBB", bb, g, r, 0)
    # map each opaque colour to nearest palette index (build via a quantize-apply)
    palimg = Image.new("P", (1, 1)); full_pal = []
    for i in range(255):
        full_pal += [pal[i*3], pal[i*3+1], pal[i*3+2]]
    full_pal = [transparent[0], transparent[1], transparent[2]] + full_pal   # idx0 first
    full_pal += [0]*(768 - len(full_pal)); palimg.putpalette(full_pal)

    frame_entries = bytearray(); frame_data = bytearray(); base = 0
    new_frames_meta = []
    for srcf, im in zip(m["frames"], imgs):
        w, h = im.size
        rgb = Image.new("RGB", (w, h)); mask = []
        rd = list(im.getdata())
        rgb.putdata([(r, g, bb) for (r, g, bb, a) in rd])
        idx = rgb.quantize(palette=palimg, dither=Image.NONE)
        ip = bytearray(idx.tobytes())
        for i, (r, g, bb, a) in enumerate(rd):
            if a < 128:
                ip[i] = 0                                  # transparent -> idx0
        new_frames_meta.append((w, h, srcf["cx"]*scale + cx_off, srcf["cy"]*scale + cy_off, base, bytes(ip)))
        base += len(ip)
    # assemble: header(0x18) + frame entries + cycles + lookup + palette + data
    nframes = len(imgs); ncycles = m["ncycles"]
    frame_off = 0x18
    cyc_off = frame_off + nframes*12
    lookup_off = cyc_off + ncycles*4
    pal_off = lookup_off + len(m["lookup"])*2
    data_off = pal_off + 256*4
    header = bytearray(0x18)
    header[0:8] = b"BAM V1  "
    struct.pack_into("<HBBI", header, 8, nframes, ncycles, 0, frame_off)
    struct.pack_into("<II", header, 0x10, pal_off, lookup_off)
    fe = bytearray()
    for (w, h, cx, cy, off, _ip) in new_frames_meta:
        fe += struct.pack("<HHhhI", w, h, cx, cy, (data_off + off) | 0x80000000)  # raw (bit31)
    ce = bytearray()
    for (cnt, fi) in m["cycles"]:
        ce += struct.pack("<HH", cnt, fi)
    lt = struct.pack(f"<{len(m['lookup'])}H", *m["lookup"]) if m["lookup"] else b""
    fd = bytearray()
    for (_w, _h, _cx, _cy, _off, ip) in new_frames_meta:
        fd += ip
    return bytes(header) + bytes(fe) + bytes(ce) + lt + bytes(pal_blob) + bytes(fd)

if __name__ == "__main__":
    import sys
    m = parse_bam(open(sys.argv[1], "rb").read())
    print(f"{sys.argv[1]}: frames={m['nframes']} cycles={m['ncycles']} rle_color={m['rle_color']} "
          f"lookup={len(m['lookup'])} pal0={m['palette'][0]}")
    for i, f in enumerate(m["frames"][:12]):
        print(f"  f{i}: {f['w']}x{f['h']} center=({f['cx']},{f['cy']}) raw={f['raw']}")
    print("  cycles:", m["cycles"][:8])
