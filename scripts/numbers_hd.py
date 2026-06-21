#!/usr/bin/env python3
"""Build an HD NUMBER.BAM (action-bar / item count digits) from a pixel font, replacing the
stock 6x7 bevel digits with crisp glyphs at 2x (12x14) so they match the de-doubled HD UI.
NUMBER is a CVidCell (literal palette), drawn by CIcon::RenderIcon with bDoubleSize=nScale==2;
we author at final px and DE-DOUBLE NUMBER (add to the CResCell de-double set) so it renders
native = crisp. Digits keep the stock gold metallic LOOK via a vertical gradient fill (cream
top -> warm gold/shadow bottom) + 1px black outline. Each digit frame = 2x the stock frame
(12x14, cx/cy x2) so the engine's fixed 10px digit step + anchor are unchanged. Frame 10
(stock 13x7 'special') is scale2x'd verbatim.

  numbers_hd.py <orig NUMBER.bam> <out.bam> [--ttf PATH] [--size 18] [--sheet out.png]
"""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from bam_codec import parse_bam, bam_to_images, images_to_bam
from PIL import Image, ImageFont, ImageDraw

STOPS = [(0.0, (255, 248, 205)), (0.35, (240, 212, 130)), (0.6, (205, 160, 70)),
         (0.82, (170, 120, 50)), (1.0, (135, 92, 40))]

def grad(t):
    for i in range(len(STOPS)-1):
        t0, c0 = STOPS[i]; t1, c1 = STOPS[i+1]
        if t0 <= t <= t1:
            f = (t-t0)/(t1-t0); return tuple(int(c0[k]+(c1[k]-c0[k])*f) for k in range(3))
    return STOPS[-1][1]

def scale2x(im):
    a = np.asarray(im); h, w = a.shape[:2]; out = np.zeros((h*2, w*2, 4), a.dtype)
    g = lambda y, x: a[min(max(y, 0), h-1), min(max(x, 0), w-1)]
    for y in range(h):
        for x in range(w):
            P = a[y, x]; A = g(y-1, x); B = g(y, x+1); C = g(y, x-1); D = g(y+1, x)
            ca, ab, dc, bd = (np.array_equal(C, A), np.array_equal(A, B),
                              np.array_equal(D, C), np.array_equal(B, D))
            out[y*2, x*2]     = A if (ca and not dc and not ab) else P
            out[y*2, x*2+1]   = B if (ab and not ca and not bd) else P
            out[y*2+1, x*2]   = C if (dc and not bd and not ca) else P
            out[y*2+1, x*2+1] = D if (bd and not ab and not dc) else P
    return Image.fromarray(out)

def render_glyph(ft, ch, cell_w, cell_h):
    """LycheeSoda digit, gold gradient + 1px black outline, fit into a (cell_w x cell_h)
    transparent cell: centred horizontally, top-aligned (matches the stock digit's float)."""
    asc, dsc = ft.getmetrics()
    tmp = Image.new("L", (cell_w*3, asc+dsc+8), 0)
    ImageDraw.Draw(tmp).text((cell_w, asc), ch, font=ft, fill=255, anchor="ls")
    msk = tmp.point(lambda v: 255 if v >= 128 else 0); bb = msk.getbbox()
    if bb is None:
        return Image.new("RGBA", (cell_w, cell_h), (0, 0, 0, 0))
    x0, y0, x1, y1 = bb; gw, gh = x1-x0, y1-y0
    glyph = Image.new("RGBA", (gw+2, gh+2), (0, 0, 0, 0)); mm = msk.load()
    def on(cx, cy):
        ax, ay = x0+cx, y0+cy
        return 0 <= ax < msk.width and 0 <= ay < msk.height and mm[ax, ay] != 0
    for oy in range(gh+2):
        for ox in range(gw+2):
            cx, cy = ox-1, oy-1
            if on(cx, cy):
                glyph.putpixel((ox, oy), (*grad(cy/max(gh-1, 1)), 255))
            elif any(on(cx+dx, cy+dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                glyph.putpixel((ox, oy), (0, 0, 0, 255))
    cell = Image.new("RGBA", (cell_w, cell_h), (0, 0, 0, 0))
    px = (cell_w - glyph.width)//2; py = 0
    cell.alpha_composite(glyph, (max(px, 0), py))
    return cell

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile"); ap.add_argument("outfile")
    ap.add_argument("--ttf", default="/home/wills/Downloads/lycheesoda/LycheeSoda.ttf")
    ap.add_argument("--size", type=int, default=18)
    ap.add_argument("--cx", type=int, default=0, help="shift digits RIGHT by N px (negative cx anchor)")
    ap.add_argument("--cy", type=int, default=0, help="shift digits DOWN by N px (negative cy anchor)")
    ap.add_argument("--sheet", default=None)
    a = ap.parse_args()
    m = parse_bam(open(a.infile, "rb").read())
    orig = bam_to_images(m)
    ft = ImageFont.truetype(a.ttf, a.size)
    frames = []
    for i, f in enumerate(m["frames"]):
        cw, ch = f["w"]*2, f["h"]*2
        if i <= 9 and f["w"] > 0:
            frames.append(render_glyph(ft, str(i), cw, ch))
        else:
            frames.append(scale2x(orig[i]) if f["w"] > 0 else Image.new("RGBA", (max(cw, 1), max(ch, 1)), (0, 0, 0, 0)))
    out = images_to_bam(frames, m, scale=2, cx_off=-a.cx, cy_off=-a.cy)
    open(a.outfile, "wb").write(out)
    print(f"wrote {a.outfile}: {m['nframes']} frames, digit cell={m['frames'][0]['w']*2}x{m['frames'][0]['h']*2}")
    if a.sheet:
        from bam_codec import parse_bam as pb
        m2 = pb(open(a.outfile, "rb").read()); im2 = bam_to_images(m2)
        Z = 14; samples = "188", "5", "23"
        digw = im2[0].size[0]
        strip = Image.new("RGBA", (10*(digw+3)*Z + 40, im2[0].size[1]*Z+8), (40, 42, 50, 255)); x = 4
        for d in range(10):
            big = im2[d].resize((im2[d].size[0]*Z, im2[d].size[1]*Z), Image.NEAREST)
            strip.alpha_composite(big, (x, 4)); x += (digw+3)*Z
        strip.convert("RGB").save(a.sheet)
        print("sheet:", a.sheet)

if __name__ == "__main__":
    main()
