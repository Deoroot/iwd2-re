#!/usr/bin/env python3
"""Build an AA replacement IWD2 font BAM from a TTF, preserving per-glyph frame
geometry (w,h,cx,cy) so layout is identical. Frame index = charcode-1.

ENGINE COLOR MODEL (verified in iwd2-re CVidFont::RealizePalette @0x793... and
CGameText SetColor(RGB(255,255,255), RGB(0,0,0))): the BAM's own palette colours
are IGNORED at draw time -- the engine rebuilds the palette as a linear ramp where
index 1 ~= BACKGROUND and index 255 ~= FOREGROUND (text colour). index 0 = green
colour-key transparent. So ONLY the per-pixel INDEX matters: glyph ink -> high
index (255 = foreground), AA edges + drop bevel/shadow -> low index (1 = bg), and
fully outside -> index 0. We encode coverage directly: idx = 1 + round(cov*254)."""
import struct, zlib
import numpy as np
from PIL import Image, ImageFont, ImageDraw, ImageFilter

THR = 0.16  # coverage below this -> transparent (index 0)


def load_bam(p):
    d = open(p, 'rb').read()
    if d[:4] == b'BAMC':
        d = zlib.decompress(d[12:])
    assert d[:8] == b'BAM V1  ', d[:8]
    nf, nc, rle, foff = struct.unpack_from('<HBBI', d, 8)
    poff = struct.unpack_from('<I', d, 0x10)[0]
    loff = struct.unpack_from('<I', d, 0x14)[0]
    frames = [list(struct.unpack_from('<HHhhI', d, foff + i*12)) for i in range(nf)]
    cycles = [struct.unpack_from('<HH', d, foff + nf*12 + i*4) for i in range(nc)]
    pal = [struct.unpack_from('<BBBB', d, poff + i*4) for i in range(256)]
    nlook = max((c[1] + c[0]) for c in cycles) if cycles else nf
    lookup = [struct.unpack_from('<H', d, loff + i*2)[0] for i in range(nlook)]
    return d, nf, nc, rle, frames, cycles, pal, lookup


def decode_frame(d, fr, rle):
    w, h, cx, cy, dd = fr
    rl = (dd & 0x80000000) == 0
    off = dd & 0x7FFFFFFF
    out = bytearray(w*h)
    if not rl:
        out[:] = d[off:off+w*h]
    else:
        i = off; o = 0
        while o < w*h:
            b = d[i]; i += 1
            if b == rle:
                c = d[i] + 1; i += 1
                for _ in range(c):
                    if o < w*h:
                        out[o] = b; o += 1
            else:
                out[o] = b; o += 1
    return np.frombuffer(bytes(out), np.uint8).reshape(h, w)


def ramp_palette():
    # cosmetic (engine realizes its own). Match the realized default fg=white/bg=black:
    # idx0 = green key, idx1 = black (bg), idx255 = white (fg).
    pal = [(0, 255, 0, 0)]
    for i in range(1, 256):
        v = round((i-1)/254*255)
        pal.append((v, v, v, 0))
    return pal


def render_glyph(font, ch, cell_w, cell_h, ss=8, stretch=1.0, edge_off=0, var=None):
    """Return a cell_h x cell_w uint8 index map. Glyph body = coverage ramp toward
    255 (foreground); the bottom-right EXTRUDED bevel/shadow = index 1 (background);
    outside = 0 (transparent)."""
    big = max(cell_w, cell_h) * ss * 4 + 40
    img = Image.new('L', (big, big), 0)
    try:
        f = ImageFont.truetype(font, cell_h * ss * 2)
    except Exception:
        return None
    if var:
        try:
            f.set_variation_by_axes(var)
        except Exception:
            pass
    bb = f.getbbox(ch)
    if bb[2] <= bb[0] or bb[3] <= bb[1]:
        return None
    ImageDraw.Draw(img).text((20-bb[0], 20-bb[1]), ch, fill=255, font=f)
    a = np.array(img)
    ys, xs = np.where(a > 8)
    if len(xs) == 0:
        return None
    glyph = img.crop((xs.min(), ys.min(), xs.max()+1, ys.max()+1))
    inw = max(1, cell_w - edge_off)
    inh = max(1, cell_h - edge_off)
    gw = max(1, int(inw*ss*stretch)); gh = max(1, int(inh*ss))
    glyph = glyph.resize((gw, gh), Image.LANCZOS)
    cw, chh = cell_w*ss, cell_h*ss
    body = Image.new('L', (cw, chh), 0)
    body.paste(glyph, (max(0, (inw*ss - gw)//2), 0))
    fa = np.array(body).astype(np.float32)/255.0      # body coverage @ supersample
    # extruded bottom-right bevel/shadow (union of shifts 1..edge_off*ss)
    edge = np.zeros_like(fa)
    H0, W0 = fa.shape
    for k in range(1, edge_off*ss + 1):
        sh = np.zeros_like(fa); sh[k:, k:] = fa[:H0-k, :W0-k]
        edge = np.maximum(edge, sh)
    # downscale both to cell
    def ds(arr):
        im = Image.fromarray((np.clip(arr, 0, 1)*255).astype(np.uint8))
        return np.array(im.resize((cell_w, cell_h), Image.LANCZOS)).astype(np.float32)/255.0
    bcov = ds(fa); ecov = ds(edge)
    # index: body coverage -> 1+round(cov*254) (toward fg/255); bevel-only -> 1 (bg);
    idxf = 1 + np.round(bcov*254).astype(np.int32)
    out = np.zeros((cell_h, cell_w), np.uint8)
    m_edge = ecov >= THR
    out[m_edge] = 1
    m_body = bcov >= THR
    out[m_body] = np.clip(idxf[m_body], 1, 255)
    return out


def render_auto(font, ch, caph, edge_off=0, ss=8, var=None):
    """Render ch at a NATURAL width/height with cap-height = caph final px (used
    when the source cell geometry doesn't match the glyph, e.g. Cyrillic in a
    cp1251 slot whose Latin cell is the wrong width). Returns (idxmap, w, h, cx, cy)."""
    probe = ImageFont.truetype(font, 200)
    if var:
        try: probe.set_variation_by_axes(var)
        except Exception: pass
    cb = probe.getbbox('H'); capref = cb[3] - cb[1]
    if capref <= 0:
        return None
    size = max(6, int(round(200 * (caph*ss) / capref)))
    f = ImageFont.truetype(font, size)
    if var:
        try: f.set_variation_by_axes(var)
        except Exception: pass
    asc, desc = f.getmetrics()
    bb = f.getbbox(ch)
    if bb[2] <= bb[0] or bb[3] <= bb[1]:
        return None
    pad = caph*ss
    W = (bb[2]-bb[0]) + 2*pad
    Ht = asc + desc + 2*pad
    im = Image.new('L', (W, Ht), 0)
    baseline_y = pad + asc
    ImageDraw.Draw(im).text((pad - bb[0], pad), ch, fill=255, font=f)
    fa = np.array(im).astype(np.float32)/255.0
    eoff = edge_off*ss
    edge = np.zeros_like(fa)
    H0, W0 = fa.shape
    for k in range(1, eoff + 1):
        sh = np.zeros_like(fa); sh[k:, k:] = fa[:H0-k, :W0-k]
        edge = np.maximum(edge, sh)
    cover = np.maximum(fa, edge)
    ys, xs = np.where(cover > 0.06)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max()+1, ys.min(), ys.max()+1
    fa = fa[y0:y1, x0:x1]; edge = edge[y0:y1, x0:x1]
    # downscale to final px
    def ds(arr):
        im2 = Image.fromarray((np.clip(arr, 0, 1)*255).astype(np.uint8))
        nw = max(1, round(arr.shape[1]/ss)); nh = max(1, round(arr.shape[0]/ss))
        return np.array(im2.resize((nw, nh), Image.LANCZOS)).astype(np.float32)/255.0
    bcov = ds(fa); ecov = ds(edge)
    idxf = 1 + np.round(bcov*254).astype(np.int32)
    out = np.zeros(bcov.shape, np.uint8)
    out[ecov >= THR] = 1
    mb = bcov >= THR
    out[mb] = np.clip(idxf[mb], 1, 255)
    w = out.shape[1]; h = out.shape[0]
    cy = int(round((baseline_y - y0)/ss))    # top -> baseline distance
    cx = 0
    return out, w, h, cx, cy


def build(src_bam, ttf, chars, out_bam, stretch=1.0, edge_off=0, var=None,
          src_scale=1, codepage=None):
    """src_scale: multiply every frame's w,h,cx,cy (2 = synthesize a 2x BAM from a
    1x source). codepage: decode charcodes >=0x80 via this codepage (cp1251 = RU)."""
    def charof(cc):
        if cc < 0x80 or codepage is None:
            return chr(cc) if 0 <= cc < 0x110000 else None
        try:
            return bytes([cc]).decode(codepage)
        except Exception:
            return None
    d, nf, nc, rle, frames, cycles, pal, lookup = load_bam(src_bam)
    frames = [[w*src_scale, h*src_scale, cx*src_scale, cy*src_scale, dd]
              for (w, h, cx, cy, dd) in frames]
    # cap-height (final px) from the ASCII 'H' cell, for autocell glyphs whose
    # source cell is the wrong shape (Cyrillic in a Latin cp1251 slot).
    caph = frames[ord('H') - 1][1] or (8 * src_scale)
    pixels = []
    repl = 0
    for i, fr in enumerate(frames):
        w, h, cx, cy, dd = fr
        cc = i + 1
        ch = charof(cc)
        if ch is not None and ch in chars and w > 0 and h > 0:
            # high range under a codepage (Cyrillic): the Latin source cell is the
            # wrong width -> compute a natural cell from the glyph instead.
            if codepage is not None and cc >= 0x80:
                r = render_auto(ttf, ch, caph, edge_off=edge_off, var=var)
                if r is not None:
                    idxmap, nw, nh, ncx, ncy = r
                    frames[i] = [nw, nh, ncx, ncy, dd]
                    pixels.append(idxmap); repl += 1; continue
            r = render_glyph(ttf, ch, w, h, stretch=stretch, edge_off=edge_off, var=var)
            if r is not None:
                pixels.append(r); repl += 1; continue
        # keep original glyph: decode at native size, NN-upscale by src_scale.
        # indices kept verbatim (already in engine bg->fg ordering).
        orig = decode_frame(d, [w//src_scale, h//src_scale, 0, 0, dd], rle)
        if src_scale != 1:
            orig = np.repeat(np.repeat(orig, src_scale, 0), src_scale, 1)
        pixels.append(orig)
    newpal = ramp_palette()
    hdr = 0x18
    frame_tab = hdr
    cycle_tab = frame_tab + nf*12
    pal_off = cycle_tab + nc*4
    look_off = pal_off + 256*4
    data_off = look_off + len(lookup)*2
    buf = bytearray(data_off)
    struct.pack_into('<8sHBBI', buf, 0, b'BAM V1  ', nf, nc, 0, frame_tab)
    struct.pack_into('<I', buf, 0x10, pal_off)
    struct.pack_into('<I', buf, 0x14, look_off)
    cur = data_off
    for i, fr in enumerate(frames):
        w, h, cx, cy, dd = fr
        px = pixels[i].astype(np.uint8).tobytes()
        struct.pack_into('<HHhhI', buf, frame_tab + i*12, w, h, cx, cy, cur | 0x80000000)
        buf += px; cur += len(px)
    for i, c in enumerate(cycles):
        struct.pack_into('<HH', buf, cycle_tab + i*4, c[0], c[1])
    for i, (b, g, rr, aa) in enumerate(newpal):
        struct.pack_into('<BBBB', buf, pal_off + i*4, b, g, rr, aa)
    for i, l in enumerate(lookup):
        struct.pack_into('<H', buf, look_off + i*2, l)
    open(out_bam, 'wb').write(buf)
    print(f"{out_bam}: replaced {repl} glyphs, {nf} frames, {len(buf)} bytes")
