#!/usr/bin/env python3
"""Batch-upscale every UI panel MOS (GUI*, height>=MINH) to HD 2x with one AI model,
write the HD MOS to the game Override + the mod clone, and print the resref list (with
LE dword pairs) for the de-double hook. Run with .venv-upscale (torch+spandrel+Pillow).

  batch_mos_hd.py <model.pth> [--minh 90 --minw 250]
"""
import sys, os, struct, zlib, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mos_codec import parse_mos, mos_to_image, image_to_mos
import upscale_ai
from PIL import Image

GAME = "/home/wills/Games/Heroic/Icewind Dale 2"
CLONE_MOS = "/home/wills/IWD2EE/iwd2ee/ieex_override"
GREEN = (0, 255, 0)

def key_index(prefix=""):
    key = open(f"{GAME}/CHITIN.KEY", "rb").read()
    nbif, nres, bifoff, resoff = struct.unpack_from("<IIII", key, 8)
    def bifn(loc):
        bo = bifoff + (loc >> 20) * 12
        bl, no, nl, _ = struct.unpack_from("<IIHH", key, bo)
        return key[no:no+nl].split(b'\x00')[0].decode('latin1').split('\\')[-1]
    idx = {}
    for i in range(nres):
        o = resoff + i*14
        nm = key[o:o+8].split(b'\x00')[0].decode('latin1', 'ignore').upper()
        rt, loc = struct.unpack_from("<HI", key, o+8)
        if rt == 0x03EC and nm.startswith(prefix):
            idx[nm] = (loc, bifn(loc))
    return idx

def extract(loc, bn):
    p = [x for x in os.listdir(f"{GAME}/Data") if x.lower() == bn.lower()][0]
    bif = open(f"{GAME}/Data/{p}", "rb").read()
    ne, nt, eo = struct.unpack_from("<III", bif, 8); fi = loc & 0x3FFF
    for i in range(ne):
        l, do, ds, rt, _ = struct.unpack_from("<IIIHH", bif, eo+i*16)
        if (l & 0x3FFF) == fi:
            return bif[do:do+ds]

def hd_one(model, raw, tile=0):
    m = parse_mos(raw); img = mos_to_image(m); w, h = img.size; px = img.load()
    mask = Image.new("L", (w, h), 0); mp = mask.load(); has = False
    for y in range(h):
        for x in range(w):
            if px[x, y] == GREEN: mp[x, y] = 255; has = True
    if has:
        for x in range(w):                       # vertical bleed
            last = None
            for y in range(h):
                if mp[x, y] == 0: last = px[x, y]
                elif last is not None: px[x, y] = last
        for y in range(h):                       # horizontal bleed (cover top edges)
            last = None
            for x in range(w):
                if mp[x, y] == 0: last = px[x, y]
                elif last is not None: px[x, y] = last
    up = upscale_ai.upscale(model, img, tile=tile)
    hd = up.resize((w*2, h*2), Image.LANCZOS)
    if has:
        hm = mask.resize((w*2, h*2), Image.NEAREST).point(lambda v: 255 if v >= 128 else 0)
        hd.paste(Image.new("RGB", hd.size, GREEN), (0, 0), hm)
    return image_to_mos(hd, global_palette=False)  # per-64x64-tile 256 palettes (native MOS), NOT one global 256 -> no banding on paintings

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model"); ap.add_argument("--minh", type=int, default=90); ap.add_argument("--minw", type=int, default=250)
    ap.add_argument("--prefix", default="", help="resref prefix filter; '' = ALL MOS")
    ap.add_argument("--tile", type=int, default=0, help="tile size for heavy transformers (DAT) to bound VRAM; 0=whole image")
    a = ap.parse_args()
    idx = key_index(a.prefix)
    model = upscale_ai.load(a.model)
    done = []
    for nm in sorted(idx):
        loc, bn = idx[nm]
        try:
            raw = extract(loc, bn)
            dec = zlib.decompress(raw[12:]) if raw[:4] == b"MOSC" else raw
            w, h = struct.unpack_from("<HH", dec, 8)
            if h < a.minh or w < a.minw:
                continue
            out = hd_one(model, raw, tile=a.tile)
            open(f"{GAME}/Override/{nm}.MOS", "wb").write(out)
            open(f"{CLONE_MOS}/{nm}.MOS", "wb").write(out)
            done.append(nm)
            print(f"  {nm} {w}x{h} -> HD ({len(out)}b)", flush=True)
        except Exception as e:
            print(f"  {nm} SKIP/ERR {e}", flush=True)
    print(f"=== {len(done)} MOS done ===")
    # resref dword pairs for the hook
    print("RESREFS:")
    for nm in done:
        b = nm.encode('latin1')[:8].ljust(8, b'\0')
        d1 = struct.unpack_from('<I', b, 0)[0]; d2 = struct.unpack_from('<I', b, 4)[0]
        print(f"  {nm} #{d1:08X} #{d2:08X}")

if __name__ == "__main__":
    main()
