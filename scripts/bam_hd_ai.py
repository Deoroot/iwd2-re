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

def defringe(rgba, rings=1):
    """Item-icon edge cleanup: erode `rings` boundary pixels whose luminance is near-black
    (the baked dark AA outline + drop-shadow fringe), keeping the item (its material is
    dark-grey/coloured, only the outline/shadow/AA is near-black). NOT de-black (which would
    gut dark item parts) -- this only touches the silhouette boundary."""
    a = np.asarray(rgba).copy(); al = a[..., 3]; rgb = a[..., :3].astype(np.float32)
    lum = 0.299*rgb[..., 0] + 0.587*rgb[..., 1] + 0.114*rgb[..., 2]
    op = al >= 128
    inner = op.copy()
    for _ in range(rings+1):
        e = inner.copy(); e[:-1] &= inner[1:]; e[1:] &= inner[:-1]; e[:, :-1] &= inner[:, 1:]; e[:, 1:] &= inner[:, :-1]; inner = e
    med = np.median(lum[inner]) if inner.any() else 128
    dark_t = max(50, med*0.5)
    for _ in range(rings):
        o = al >= 128; tadj = np.zeros_like(o)
        tadj[:-1] |= ~o[1:]; tadj[1:] |= ~o[:-1]; tadj[:, :-1] |= ~o[:, 1:]; tadj[:, 1:] |= ~o[:, :-1]
        al[(o & tadj) & (lum < dark_t)] = 0
    a[..., 3] = al
    return Image.fromarray(a)

def hd_frame(model, im, defringe_rings=0):
    w, h = im.size
    if w == 0 or h == 0:
        return im.resize((max(w*2, 1), max(h*2, 1)))
    rd = im.split()
    rgb = im.convert("RGB"); alpha = rd[3] if len(rd) == 4 else Image.new("L", im.size, 255)
    bled = bleed(rgb, alpha)
    big = upscale_ai.upscale(model, bled)                  # ~4x RGB
    # Silhouette: upscale the 1-bit alpha SMOOTHLY to the model's 4x, carry it through the same
    # Lanczos downscale as the colour, THEN threshold to 1-bit. The edge follows the true contour
    # instead of NEAREST 2x2 pixel-doubling (which stair-steps = aliasing the AI then sharpens).
    big_a = alpha.resize(big.size, Image.LANCZOS)
    rgba = big.convert("RGBA"); rgba.putalpha(big_a)
    hd = rgba.resize((w*2, h*2), Image.LANCZOS)
    am = hd.split()[3].point(lambda v: 255 if v >= 128 else 0)
    out = hd.convert("RGB").convert("RGBA"); out.putalpha(am)
    if defringe_rings > 0:                                 # item icons: strip baked dark outline/shadow fringe
        out = defringe(out, defringe_rings)
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
