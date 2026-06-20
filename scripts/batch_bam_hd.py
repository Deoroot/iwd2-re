#!/usr/bin/env python3
"""Batch-upscale the UI button BAMs to HD 2x with one AI model (Remacri), write HD BAM to
the game Override + mod clone, and print the resref dword pairs for the CResCell de-double
hook. Excludes the 255-frame stone fonts + the tiny status-icon / strip / caret BAMs.

  batch_bam_hd.py <model.pth>
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import upscale_ai
from bam_codec import parse_bam, bam_to_images, images_to_bam
from bam_hd_ai import hd_frame

GAME = "/home/wills/Games/Heroic/Icewind Dale 2"
CLONE = "/home/wills/IWD2EE/iwd2ee/ieex_override"

# UI button/graphic BAMs referenced by the CHU windows. Excludes: NORMAL/TOOLFONT/REALMS/
# INITIALS/STONESML/STONEBIG/STONESM3 (cell fonts -> font pipeline), STATES2 (142 status
# icons 12x13), GUIHITPT (4px strip), CAROT (3px caret).
BUTTONS = [
    'CBUT','CGEAR','CLIK2CON','CONTBACK','FLAG1','GBTNBFRM','GBTNBLNK','GBTNCA','GBTNJBTN',
    'GBTNKICK','GBTNLRG','GBTNLRG2','GBTNLRG3','GBTNMED','GBTNMED2','GBTNMINS','GBTNOPT1',
    'GBTNOPT3','GBTNPERM','GBTNPLUS','GBTNPOR','GBTNRECB','GBTNSCRL','GBTNSPB1','GBTNSPB2',
    'GBTNSPB3','GBTNSTD','GBTNUPDN','GCOMMBTN','GCOMMSB','GUIBTACT','GUIBTBUT','GUICTRL',
    'GUIMAPWC','GUIRSPOR','GUIRZPOR','GUISLDR','GUISTBBC','GUISTMSC','INVBUT2','INVBUT3',
    'ROOMDELU','ROOMMERC','ROOMNOBE','ROOMPEAS','SPLBUT','STONSLOT','STORESCR','TOGGLE',
]

def key_index():
    key = open(f"{GAME}/CHITIN.KEY", "rb").read()
    nbif, nres, bifoff, resoff = struct.unpack_from("<IIII", key, 8)
    def bifn(loc):
        bo = bifoff + (loc >> 20)*12; bl, no, nl, _ = struct.unpack_from("<IIHH", key, bo)
        return key[no:no+nl].split(b'\0')[0].decode('latin1').split('\\')[-1]
    idx = {}
    for i in range(nres):
        o = resoff + i*14; nm = key[o:o+8].split(b'\0')[0].decode('latin1', 'ignore').upper()
        rt, loc = struct.unpack_from("<HI", key, o+8)
        if rt == 0x03E8 and nm in BUTTONS:
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

def main():
    model = upscale_ai.load(sys.argv[1])
    idx = key_index()
    done = []
    for nm in BUTTONS:
        if nm not in idx:
            print(f"  {nm} NOT FOUND", flush=True); continue
        loc, bn = idx[nm]
        try:
            m = parse_bam(extract(loc, bn))
            imgs = bam_to_images(m)
            hd = [hd_frame(model, im) for im in imgs]
            out = images_to_bam(hd, m, scale=2)
            open(f"{GAME}/Override/{nm}.BAM", "wb").write(out)
            open(f"{CLONE}/{nm}.BAM", "wb").write(out)
            done.append(nm)
            print(f"  {nm}  {m['nframes']}f -> HD ({len(out)}b)", flush=True)
        except Exception as e:
            print(f"  {nm} ERR {e}", flush=True)
    print(f"=== {len(done)} button BAMs HD ===")
    print("RESREFS:")
    for nm in done:
        b = nm.encode('latin1')[:8].ljust(8, b'\0')
        d1, d2 = struct.unpack_from('<I', b, 0)[0], struct.unpack_from('<I', b, 4)[0]
        print(f"  {nm} #{d1:08X} #{d2:08X}")

if __name__ == "__main__":
    main()
