#!/usr/bin/env python3
"""HD UI button BAMs: AI-upscale every frame of a BAM to 2x and re-encode (metrics x2,
cycles/lookup/transparency preserved) for the CResCell de-double path. Transparent regions
are bled (so the model never sees the green key -> no halo), AI 4x, downscale to 2x native,
then the alpha mask is re-applied. Model: Remacri (crisp UI art).

  bam_hd_ai.py <model.pth> <in.bam> <out.bam> [--preview out.png]
"""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import upscale_ai
from bam_codec import parse_bam, bam_to_images, images_to_bam
from PIL import Image

def bleed(rgb, alpha):
    """Fill transparent (alpha<128) pixels with nearest opaque colour by 4 directional passes."""
    a = np.asarray(alpha) >= 128
    p = np.asarray(rgb).copy()
    if a.all() or not a.any():
        return Image.fromarray(p)
    for _ in range(2):
        for axis in (0, 1):
            for rev in (False, True):
                idx = range(p.shape[axis]-1, -1, -1) if rev else range(p.shape[axis])
                prev = None
                for i in idx:
                    line_a = a[i, :] if axis == 0 else a[:, i]
                    line_p = p[i, :, :] if axis == 0 else p[:, i, :]
                    if prev is not None:
                        fillmask = ~line_a
                        line_p[fillmask] = prev[fillmask]
                        if axis == 0: p[i, :, :] = line_p
                        else: p[:, i, :] = line_p
                        a_now = a[i, :] if axis == 0 else a[:, i]
                    prev = (p[i, :, :] if axis == 0 else p[:, i, :]).copy()
    return Image.fromarray(p)

def hd_frame(model, im):
    w, h = im.size
    if w == 0 or h == 0:
        return im.resize((max(w*2, 1), max(h*2, 1)))
    rd = im.split()
    rgb = im.convert("RGB"); alpha = rd[3] if len(rd) == 4 else Image.new("L", im.size, 255)
    bled = bleed(rgb, alpha)
    big = upscale_ai.upscale(model, bled)                  # ~4x
    hd = big.resize((w*2, h*2), Image.LANCZOS)
    am = alpha.resize((w*2, h*2), Image.NEAREST).point(lambda v: 255 if v >= 128 else 0)
    out = hd.convert("RGBA"); out.putalpha(am)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model"); ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--preview", default=""); ap.add_argument("--model_obj", default=None)
    a = ap.parse_args()
    model = a.model_obj or upscale_ai.load(a.model)
    m = parse_bam(open(a.inp, "rb").read())
    imgs = bam_to_images(m)
    hd = [hd_frame(model, im) for im in imgs]
    out = images_to_bam(hd, m, scale=2)
    open(a.out, "wb").write(out)
    print(f"  {os.path.basename(a.inp)}: {m['nframes']} frames -> HD ({len(out)}b)")
    if a.preview:
        # tile the largest few frames for a look
        big = sorted(hd, key=lambda im: -im.size[0]*im.size[1])[:6]
        W = sum(im.size[0]+4 for im in big); H = max(im.size[1] for im in big)
        sheet = Image.new("RGBA", (W, H), (40, 40, 40, 255)); x = 0
        for im in big:
            sheet.alpha_composite(im, (x, 0)); x += im.size[0]+4
        sheet.convert("RGB").save(a.preview)

if __name__ == "__main__":
    main()
