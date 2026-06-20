#!/usr/bin/env python3
"""Build an HD 2x UI MOS via AI upscale (spandrel) + transparency-safe repack.
Run with the .venv-upscale venv (torch+spandrel+Pillow).

  mos_hd_ai.py <orig.MOS> <out.MOS> <model.pth> [--tile N] [--preview out.png]

Flow: unpack -> bleed art into the (green) transparent region so the model never
sees green -> AI upscale (model.scale x) -> downscale to 2x of the original (=
the doubled-panel size the de-double hook expects) -> re-apply the green key
(nearest mask) -> pack with green at index 0. Renders native = crisp 2x in-game.
"""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mos_codec import parse_mos, mos_to_image, image_to_mos
import upscale_ai
from PIL import Image

GREEN = (0, 255, 0)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile"); ap.add_argument("outfile"); ap.add_argument("model")
    ap.add_argument("--tile", type=int, default=0)
    ap.add_argument("--preview", default=None)
    a = ap.parse_args()

    m = parse_mos(open(a.infile, "rb").read())
    img = mos_to_image(m); w, h = img.size; px = img.load()

    # green-key mask + vertical bleed (fill each transparent pixel from the last
    # opaque pixel above it, so the AI upscale isn't contaminated by green edges)
    mask = Image.new("L", (w, h), 0); mp = mask.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] == GREEN:
                mp[x, y] = 255
    for x in range(w):
        last = None
        for y in range(h):
            if mp[x, y] == 0:
                last = px[x, y]
            elif last is not None:
                px[x, y] = last

    model = upscale_ai.load(a.model)
    up = upscale_ai.upscale(model, img, tile=a.tile)          # model.scale x
    hd = up.resize((w * 2, h * 2), Image.LANCZOS)             # -> 2x of original

    # re-apply the green key (sharp, nearest-upscaled mask)
    hdmask = mask.resize((w * 2, h * 2), Image.NEAREST).point(lambda v: 255 if v >= 128 else 0)
    hd.paste(Image.new("RGB", hd.size, GREEN), (0, 0), hdmask)

    if a.preview:
        hd.save(a.preview)
    open(a.outfile, "wb").write(image_to_mos(hd))
    print(f"AI-HD {os.path.basename(a.infile)} {w}x{h} --[{model.scale}x]--> {up.size} "
          f"--> {w*2}x{h*2} -> {a.outfile}")

if __name__ == "__main__":
    main()
