#!/usr/bin/env python3
"""HD UI portraits: AI-upscale every stock _L portrait (210x330 -> 420x660) and rebuild
the small _S (42x42 face crop) from the upscaled large, so both render crisp under the 2x
UI (CResBitmap de-double gates on native dims 420x660 / 84x84). _S is NOT a squish of _L --
it is a square face crop; we recover the stock crop box by NCC template-match (_S vs _L)
then apply it (x4) to the full-res AI output. 24-bit BMP out -> Override + mod clone.

  portraits_hd.py <model.pth> [--only DFF,EFF] [--pilot DFF]
"""
import sys, os, struct, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import upscale_ai
from PIL import Image

GAME = "/home/wills/Games/Heroic/Icewind Dale 2"
CLONE = "/home/wills/IWD2EE/iwd2ee/ieex_override"

def key_index():
    key = open(f"{GAME}/CHITIN.KEY", "rb").read()
    nbif, nres, bifoff, resoff = struct.unpack_from("<IIII", key, 8)
    def bifn(loc):
        bo = bifoff + (loc >> 20) * 12
        bl, no, nl, _ = struct.unpack_from("<IIHH", key, bo)
        return key[no:no+nl].split(b'\0')[0].decode('latin1').split('\\')[-1]
    idx = {}
    for i in range(nres):
        o = resoff + i*14
        nm = key[o:o+8].split(b'\0')[0].decode('latin1', 'ignore').upper()
        rt, loc = struct.unpack_from("<HI", key, o+8)
        if rt == 0x0001 and (nm.endswith("_L") or nm.endswith("_S")):
            idx[nm] = (loc, bifn(loc))
    return idx

def extract(loc, bn):
    cand = [x for x in os.listdir(f"{GAME}/Data") if x.lower() == bn.lower()]
    if not cand:
        return None
    bif = open(f"{GAME}/Data/{cand[0]}", "rb").read()
    ne, nt, eo = struct.unpack_from("<III", bif, 8); fi = loc & 0x3FFF
    for i in range(ne):
        l, do, ds, rt, _ = struct.unpack_from("<IIIHH", bif, eo+i*16)
        if (l & 0x3FFF) == fi:
            return bif[do:do+ds]

def to_img(raw):
    import io
    return Image.open(io.BytesIO(raw)).convert("RGB")

def find_crop(L, S):
    """Square box (x0,y0,R) in L (210x330) which, resized to 42, best matches S (NCC)."""
    Sa = np.asarray(S, dtype=np.float32); Sm = Sa - Sa.mean(); Sn = np.sqrt((Sm**2).sum()) + 1e-9
    best = (-1, None)
    for R in range(70, 200, 6):
        f = R/42.0; dw, dh = int(round(210/f)), int(round(330/f))
        if dw < 42 or dh < 42:
            continue
        Ld = np.asarray(L.resize((dw, dh), Image.LANCZOS), dtype=np.float32)
        win = np.lib.stride_tricks.sliding_window_view(Ld, (42, 42, 3))[:, :, 0]
        wm = win - win.mean(axis=(2, 3, 4), keepdims=True)
        num = (wm * Sm).sum(axis=(2, 3, 4)); den = np.sqrt((wm**2).sum(axis=(2, 3, 4))) * Sn + 1e-9
        ncc = num/den; yy, xx = np.unravel_index(np.argmax(ncc), ncc.shape)
        if ncc[yy, xx] > best[0]:
            best = (ncc[yy, xx], (int(round(xx*f)), int(round(yy*f)), R))
    return best

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model"); ap.add_argument("--only", default=""); ap.add_argument("--pilot", default="")
    a = ap.parse_args()
    only = set(x.strip().upper() for x in a.only.split(",") if x.strip())
    idx = key_index()
    bases = sorted(set(nm[:-2] for nm in idx if nm.endswith("_L") and f"{nm[:-2]}_S" in idx))
    if only:
        bases = [b for b in bases if b in only]
    model = upscale_ai.load(a.model)
    done = []
    for b in bases:
        lL, bL = idx[f"{b}_L"]; lS, bS = idx[f"{b}_S"]
        rawL = extract(lL, bL); rawS = extract(lS, bS)
        if rawL is None or rawS is None:
            print(f"  {b} SKIP (unresolved biff)", flush=True); continue
        L = to_img(rawL); S = to_img(rawS)
        big = upscale_ai.upscale(model, L)                  # ~840x1320 (x4)
        hdL = big.resize((420, 660), Image.LANCZOS)         # crisp 2x large
        v, (x0, y0, R) = find_crop(L, S)                    # crop box in stock-L coords
        sx, sy, sr = x0*4, y0*4, R*4                        # -> full-res AI coords
        crop = big.crop((sx, sy, min(sx+sr, big.width), min(sy+sr, big.height)))
        hdS = crop.resize((84, 84), Image.LANCZOS)          # crisp 2x small (face)
        for nm, im in ((f"{b}_L", hdL), (f"{b}_S", hdS)):
            im.save(f"{GAME}/Override/{nm}.BMP", "BMP")
            im.save(f"{CLONE}/{nm}.BMP", "BMP")
        if a.pilot and b == a.pilot.upper():
            hdL.save(f"/tmp/port/{b}_HDL.png"); hdS.resize((336, 336), Image.NEAREST).save(f"/tmp/port/{b}_HDS.png")
        done.append(b)
        print(f"  {b}  L->420x660  S(ncc={v:.2f} box={x0},{y0},{R})->84x84", flush=True)
    print(f"=== {len(done)} portraits HD ({len(done)*2} BMP) ===")

if __name__ == "__main__":
    main()
