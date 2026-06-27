"""Repack an IWD2 BAMv1 bitmap font into a sharp HD (2x) version rendered from a
real vector font (default: TeX Gyre Pagella / Palatino), preserving the engine's
per-glyph advances, char->frame mapping, palette, and the black-ink/white-halo
look.

Why this works (see CVidFont::RenderCharacters @0x7A1660 + GetStringLength @0x792D40):
  - Advance per glyph = BAM frame WIDTH `w` (old path GetCurrentFrameSize().cx;
    new path m_aGlyphWidths[], built from the BAM at load). So scaling every
    frame metric (w,h,cx,cy) by S=2 yields exactly 2x layout, *whatever* the
    exact pen formula -- we never have to guess it.
  - Under the engine double-size (m_bDoubleSize) the texture-atlas path draws
    each glyph quad at nScale=2 -> nearest/bilinear stretch of the native-res
    texture = blocky. A 2x BAM makes the atlas 2x native; we then DISABLE
    m_bDoubleSize for this font (engine phase) so nScale=1 renders the 2x
    texture 1:1 = crisp. (A 2x BAM also sharpens the old blit path for free.)
  - Glyph encoding is simply index = round(coverage*255) into a white(1)->black(255)
    inverted ramp palette (idx0 = green-screen transparent). Full ink -> dark core,
    AA fringe -> light = the built-in white halo. We reuse the ORIGINAL palette and
    map our rasterized coverage the same way, so tinting/RealizePalette behaves
    identically.

Faithful spacing: the Pagella glyph is rendered LEFT-ALIGNED into a cell whose
width = orig_w*S (the inherited advance). Pagella is narrower than the original
Times-ish advances, so glyphs sit with a little extra room on the right -> the
slightly-looser tracking the user asked for, for free, while UI text-box layout
(which assumes the original advances) stays intact.

Usage:
  .venv-reagent/bin/python scripts/bam_font_pack.py <in.BAM> <out.BAM> \
      [--font /path/to.otf] [--scale 2] [--tracking 0] [--sheet sheet.png]

Requires Pillow (installed in .venv-reagent).
"""

import argparse
import os
import struct
import sys

from PIL import Image, ImageFont, ImageDraw

DEFAULT_FONT = "/usr/share/fonts/tex-gyre/texgyrepagella-regular.otf"


# ---------------------------------------------------------------------------
# BAMv1 parse (frames + palette + cycles + frame-lookup, raw-preserving)
# ---------------------------------------------------------------------------
def parse_bam(data):
    if data[:4] == b"BAMC":
        import zlib
        data = zlib.decompress(data[12:])
    if data[:8] != b"BAM V1  ":
        raise ValueError(f"not BAM V1: {data[:8]!r}")
    n_frames, n_cycles, rle_color, frame_off = struct.unpack_from("<HBBI", data, 8)
    pal_off = struct.unpack_from("<I", data, 0x10)[0]
    lookup_off = struct.unpack_from("<I", data, 0x14)[0]

    frames = []
    for i in range(n_frames):
        w, h, cx, cy, d = struct.unpack_from("<HHhhI", data, frame_off + i * 12)
        uncompressed = (d & 0x80000000) != 0
        frames.append({"w": w, "h": h, "cx": cx, "cy": cy,
                       "offset": d & 0x7FFFFFFF, "uncompressed": uncompressed})

    cycle_off = frame_off + n_frames * 12
    cycles = []  # (frame_count, first_lookup_index)
    for i in range(n_cycles):
        cnt, first = struct.unpack_from("<HH", data, cycle_off + i * 4)
        cycles.append((cnt, first))

    palette = [struct.unpack_from("<BBBB", data, pal_off + i * 4) for i in range(256)]

    lut_len = max((first + cnt for cnt, first in cycles), default=0)
    lookup = list(struct.unpack_from(f"<{lut_len}H", data, lookup_off)) if lut_len else []

    return {"n_frames": n_frames, "n_cycles": n_cycles, "rle_color": rle_color,
            "frames": frames, "cycles": cycles, "palette": palette,
            "lookup": lookup, "raw": data}


def decode_indices(bam, idx):
    """Return (w, h, bytearray of palette indices) for a frame."""
    f = bam["frames"][idx]
    w, h = f["w"], f["h"]
    n = w * h
    src = bam["raw"]
    pos = f["offset"]
    if f["uncompressed"]:
        return w, h, bytearray(src[pos:pos + n])
    rc = bam["rle_color"]
    px = bytearray(n)
    out = 0
    while out < n:
        b = src[pos]; pos += 1
        if b == rc:
            run = src[pos] + 1; pos += 1
            for _ in range(run):
                if out >= n:
                    break
                px[out] = b; out += 1
        else:
            px[out] = b; out += 1
    return w, h, px


# ---------------------------------------------------------------------------
# Palette helpers
# ---------------------------------------------------------------------------
def build_coverage_lut(palette, invert=False):
    """Map coverage 0..255 -> nearest palette index. Default: full coverage -> the
    DARK end of the ramp (ink lives at high idx; for fonts the engine SetColor-remaps,
    high idx = foreground). `invert`: full coverage -> the LIGHT end (idx1); for fonts
    rendered with the LITERAL palette whose stroke is white, e.g. REALMS.
    Returns a list[256] indexed by coverage."""
    # luminance of each palette entry (skip idx0 = transparent key colour)
    lum = []
    for i, (r, g, bl, _a) in enumerate(palette):
        lum.append(0.299 * r + 0.587 * g + 0.114 * bl)
    # coverage c -> desired luminance ; find non-zero idx closest in lum
    lut = [0] * 256
    for c in range(256):
        if c == 0:
            lut[0] = 0
            continue
        want = float(c) if invert else 255.0 - c  # invert: full cov -> light idx
        best_i, best_d = 1, 1e9
        for i in range(1, 256):
            d = abs(lum[i] - want)
            if d < best_d:
                best_d, best_i = d, i
        lut[c] = best_i
    return lut


# ---------------------------------------------------------------------------
# Glyph rasterisation
# ---------------------------------------------------------------------------
def cp_char(frame_index, encoding="cp1252"):
    """Frame i corresponds to character byte (i+1), decoded as `encoding`. The engine
    indexes the font by the codepage of the installed game language: cp1252 for the
    Western languages (English/French/Italian/Portuguese), cp1251 for Russian Cyrillic,
    cp1250 for Central European, etc. Returns the unicode char, or None if undefined /
    non-printable for that codepage."""
    b = frame_index + 1
    if b < 0x20:
        return None
    try:
        ch = bytes([b]).decode(encoding)
    except (UnicodeDecodeError, LookupError):
        return None
    if not ch.isprintable():
        return None
    return ch


def cp1252_char(frame_index):
    """Back-compat wrapper: cp_char with the default Western codepage."""
    return cp_char(frame_index, "cp1252")


def measure_cap_height(font_path, size):
    ft = ImageFont.truetype(font_path, size)
    img = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(img).text((size // 2, size // 2), "H", font=ft, fill=255, anchor="ls")
    bb = img.getbbox()
    return (bb[3] - bb[1]) if bb else size


def pick_font_size(font_path, target_cap_px):
    """Choose a px size so the font's cap height ~ target_cap_px."""
    probe = 100
    cap = measure_cap_height(font_path, probe)
    if cap <= 0:
        return target_cap_px
    return max(6, round(probe * target_cap_px / cap))


def pick_font_size_by_lineheight(font_path, target_line_px):
    """Choose a px size so the font's line height (ascent+descent) ~ target_line_px.
    This matches the *original font's* line box (×S) rather than just cap height, so a
    taller-bodied face like Pagella is scaled down to occupy the same footprint as the
    stock font instead of overflowing it ('too big')."""
    probe = 100
    asc, dsc = ImageFont.truetype(font_path, probe).getmetrics()
    line = asc + dsc
    if line <= 0:
        return target_line_px
    return max(6, round(probe * target_line_px / line))


def render_glyph_cell(ft, ch, tracking, ss=1, weight=0.0):
    """Render `ch` into a cell whose WIDTH = Pagella's own natural advance for the
    glyph (+ tracking). Using the font's natural advance -- not the original's
    narrow advance -- means every glyph fits its cell (no clipping), while the
    summed word width stays ~equal to the original (Palatino's overall metrics
    happen to match the IWD2 font), so UI text-box layout is preserved.

    The glyph is drawn at its natural left side bearing inside the cell (origin at
    x=0), so the bearing lives in the bitmap and frame cx can stay 0. Height/cy
    follow the actual ink so ascenders/descenders never clip.

    Returns (cell_w, PIL 'L' cropped image [cell_w x ink_h], cy, overflow_px).
    image is None for an empty (whitespace) glyph."""
    # `ft` is the face at (pack_size * ss). Render at ss supersampling with an optional
    # faux-bold `weight` px stroke (at pack scale), then downsample -> smooth sub-pixel weight.
    adv = ft.getlength(ch) / ss
    cell_w = max(1, int(round(adv)) + tracking)
    asc, dsc = ft.getmetrics()                       # at ss size
    baseline = asc + 2 * ss
    canvas_h = asc + dsc + 4 * ss
    cw = cell_w * ss
    # render on a canvas wider than the cell so we can MEASURE real right overflow
    img = Image.new("L", (cw + max(8 * ss, cw), canvas_h), 0)
    ImageDraw.Draw(img).text(((tracking // 2) * ss, baseline), ch, font=ft, fill=255,
                             stroke_width=round(weight * ss), stroke_fill=255, anchor="ls")
    if ss != 1:
        # BOX (area average), NOT Lanczos: Lanczos rings and leaves faint halos that
        # inflate each glyph's getbbox (height/cy/overflow). BOX gives clean coverage.
        img = img.resize((img.width // ss, img.height // ss), Image.BOX)
    ink = img.getbbox()
    if ink is None:
        return cell_w, None, 0, 0
    overflow = max(0, ink[2] - cell_w)
    crop = img.crop((0, ink[1], cell_w, ink[3]))  # cell-width slice, vertical ink only
    cy = round(baseline / ss) - ink[1]
    return cell_w, crop, cy, overflow


# ---------------------------------------------------------------------------
# BAMv1 writer (uncompressed frames)
# ---------------------------------------------------------------------------
def bamc_wrap(raw):
    """Wrap an uncompressed BAMv1 blob as BAMC V1 (zlib) -- the form the game's
    Override BAMs use."""
    import zlib
    return b"BAMCV1  " + struct.pack("<I", len(raw)) + zlib.compress(raw, 9)


def write_bam(path, n_cycles, rle_color, frames, cycles, palette, lookup, bamc=True):
    """frames = list of dicts {w,h,cx,cy,pixels(bytes)}."""
    n_frames = len(frames)
    header = bytearray(0x18)
    header[0:8] = b"BAM V1  "
    struct.pack_into("<HBBI", header, 8, n_frames, n_cycles, rle_color, 0x18)
    frame_entry_off = 0x18
    cycle_off = frame_entry_off + n_frames * 12
    pal_off = cycle_off + n_cycles * 4
    lookup_off = pal_off + 256 * 4
    data_off = lookup_off + len(lookup) * 2
    struct.pack_into("<I", header, 0x10, pal_off)
    struct.pack_into("<I", header, 0x14, lookup_off)

    frame_entries = bytearray()
    pixel_blob = bytearray()
    for f in frames:
        off = data_off + len(pixel_blob)
        frame_entries += struct.pack("<HHhhI", f["w"], f["h"], f["cx"], f["cy"],
                                     off | 0x80000000)  # bit31 set = uncompressed
        pixel_blob += f["pixels"]

    cycle_blob = b"".join(struct.pack("<HH", c, fi) for c, fi in cycles)
    pal_blob = b"".join(struct.pack("<BBBB", *p) for p in palette)
    lut_blob = struct.pack(f"<{len(lookup)}H", *lookup) if lookup else b""

    blob = bytes(header) + bytes(frame_entries) + cycle_blob + pal_blob + lut_blob + bytes(pixel_blob)
    with open(path, "wb") as fp:
        fp.write(bamc_wrap(blob) if bamc else blob)


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("outfile")
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--scale", type=float, default=2)
    ap.add_argument("--size-scale", type=float, default=1.0,
                    help="fine-tune glyph size vs the original line box (1.0=match, <1 smaller)")
    ap.add_argument("--tracking", type=int, default=0,
                    help="extra px added to each glyph's left offset (>0 = looser)")
    ap.add_argument("--weight", type=float, default=0.0,
                    help="faux-bold: stroke width in px at pack scale (0.25 = subtle, 0.5 = bold-ish)")
    ap.add_argument("--encoding", default="cp1252",
                    help="codepage for the frame->char map: cp1252 (Western, default), "
                         "cp1251 (Russian Cyrillic), cp1250 (Central European), ...")
    ap.add_argument("--caps", action="store_true",
                    help="render capitals at lowercase positions too (all-caps fonts like STONESML)")
    ap.add_argument("--invert", action="store_true",
                    help="ink at the LIGHT palette end (idx1); for literal-palette fonts whose stroke is white (REALMS)")
    ap.add_argument("--sheet", default=None, help="write a preview contact sheet PNG")
    ap.add_argument("--raw", action="store_true",
                    help="write uncompressed BAM V1 (default: BAMC zlib, as the game uses)")
    args = ap.parse_args()

    data = open(args.infile, "rb").read()
    if data[:4] == b"BAMC":
        sys.exit("compressed BAM -- decompress first (use BAM_DECOMP version)")
    bam = parse_bam(data)
    S = args.scale

    # Size to the ORIGINAL font's line box x S (so the HD face occupies the same footprint as
    # the stock font -- 'same relative size as the UI'). frame 1 is the engine's GetFontHeight
    # reference (line height); fall back to cap-height if absent. --size-scale fine-tunes.
    orig_line = bam["frames"][1]["h"] if bam["n_frames"] > 1 else 13
    H_frame = 71 if bam["n_frames"] > 71 else 0
    orig_cap = bam["frames"][H_frame]["cy"] or 9
    target_line = round(orig_line * S * args.size_scale)
    size = pick_font_size_by_lineheight(args.font, target_line)
    SS = 4  # supersample for smooth sub-pixel faux-bold (--weight)
    ft = ImageFont.truetype(args.font, size * SS)
    cov_lut = build_coverage_lut(bam["palette"], args.invert)

    print(f"{os.path.basename(args.infile)}: {bam['n_frames']} frames, "
          f"orig line~{orig_line}px cap~{orig_cap}px -> target line {target_line}px, "
          f"Pagella size={size}px (cap~{measure_cap_height(args.font, size)}px)")

    new_frames = []
    overflow_total = 0
    rendered = 0
    for i, f in enumerate(bam["frames"]):
        w = f["w"]
        ch = cp_char(i, args.encoding)
        if args.caps and ch is not None:
            up = ch.upper()
            if len(up) == 1:
                ch = up
        if ch is not None and ch != " " and w > 0:
            cell_w, cov, new_cy, overflow = render_glyph_cell(ft, ch, args.tracking, SS, args.weight)
            overflow_total += overflow
            if cov is not None:
                px = bytearray(cell_w * cov.height)
                for j, c in enumerate(list(cov.getdata())):
                    px[j] = cov_lut[c]
                new_frames.append({"w": cell_w, "h": cov.height, "cx": 0,
                                   "cy": new_cy, "pixels": bytes(px)})
                rendered += 1
                continue
        # whitespace / control / blank: keep the ORIGINAL frame's metrics x S (transparent
        # pixels). CRITICAL: the engine reads control frames 0 & 1 for the font's line metrics
        # (CVidFont::GetBaseLineHeight = GetFrameSize(seq0).cy, GetFontHeight = GetFrameSize(seq1).cy),
        # which drive CUIControlTextDisplay line spacing (journal/combat-log/dialogue). If these are
        # 1px the multi-line text collapses. Scaling the original h/cy by S preserves the real
        # line height (orig 11/13 -> 22/26) exactly as the stock font does.
        nw = max(1, round(w * S))
        nh = max(1, round(f["h"] * S))
        new_frames.append({"w": nw, "h": nh, "cx": round(f["cx"] * S), "cy": round(f["cy"] * S),
                           "pixels": bytes(nw * nh)})

    write_bam(args.outfile, bam["n_cycles"], bam["rle_color"], new_frames,
              bam["cycles"], bam["palette"], bam["lookup"], bamc=not args.raw)
    print(f"  wrote {args.outfile}: {rendered} glyphs rendered, "
          f"overflow_total={overflow_total}px"
          + ("  (consider lowering --tracking or font size)" if overflow_total else ""))

    if args.sheet:
        make_sheet(args.outfile, args.sheet)


def make_sheet(bam_path, out_png):
    bam = parse_bam(open(bam_path, "rb").read())
    pal = bam["palette"]
    cells = []  # (img, cy)
    maxw = max_above = max_below = 0
    for i in range(0x20, min(0x7F, bam["n_frames"])):
        w, h, px = decode_indices(bam, i)
        cy = bam["frames"][i]["cy"]
        im = Image.new("RGBA", (max(1, w), max(1, h)), (0, 0, 0, 0))
        im.putdata([(0, 0, 0, 0) if p == 0 else (pal[p][0], pal[p][1], pal[p][2], 255)
                    for p in px])
        cells.append((im, cy))
        maxw = max(maxw, w)
        max_above = max(max_above, cy)        # pixels above baseline
        max_below = max(max_below, h - cy)    # pixels below baseline
    cellh = max_above + max_below
    cols, pad = 16, 3
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * (maxw + pad), rows * (cellh + pad)),
                      (128, 128, 128, 255))
    for k, (im, cy) in enumerate(cells):
        r, c = divmod(k, cols)
        x = c * (maxw + pad)
        y = r * (cellh + pad) + (max_above - cy)  # baseline-align
        sheet.alpha_composite(im, (x, y))
    sheet.save(out_png)
    print(f"  sheet -> {out_png} ({sheet.size[0]}x{sheet.size[1]})")


if __name__ == "__main__":
    main()
