#!/usr/bin/env python3
"""List resources indexed by CHITIN.KEY.

Answers "does this resref actually ship with the game?" without unpacking BIFs --
the question that keeps coming up when a recovered function builds a resref at
runtime (sound variants, spell/item lookups) and we need to know which of the
names it can produce are real.

Usage:
    python scripts/key_resources.py --prefix BL_          # every resref starting BL_
    python scripts/key_resources.py --prefix HIT_ --type WAV
    python scripts/key_resources.py --exists PC_MS4       # exit 0 if present, 1 if not
    python scripts/key_resources.py --prefix SL_ --count  # just the tally

The key path defaults to $IWD2_GAME_DIR/CHITIN.KEY, else the GOG/local installs.
"""

import argparse
import os
import struct
import sys

DEFAULT_KEYS = [
    os.path.join(os.environ.get("IWD2_GAME_DIR", ""), "CHITIN.KEY"),
    r"C:\Juegos\Icewind Dale 2\CHITIN.KEY",
    r"C:\GOG Games\Icewind Dale 2\CHITIN.KEY",
]

# IE resource type codes (the handful worth naming; others print as 0xNNNN).
TYPES = {
    0x0001: "BMP", 0x0002: "MVE", 0x0004: "WAV", 0x0005: "WFX",
    0x0006: "PLT", 0x03E8: "BAM", 0x03E9: "WED", 0x03EA: "CHU",
    0x03EB: "TIS", 0x03EC: "MOS", 0x03ED: "ITM", 0x03EE: "SPL",
    0x03EF: "IDS", 0x03F0: "CRE", 0x03F1: "ARE", 0x03F2: "DLG",
    0x03F3: "2DA", 0x03F4: "GAM", 0x03F5: "STO", 0x03F6: "WMP",
    0x03F7: "EFF", 0x03F8: "BS", 0x03F9: "CHR", 0x03FA: "VVC",
    0x03FB: "VEF", 0x03FC: "PRO", 0x03FE: "BIO", 0x0802: "INI",
}
TYPE_BY_NAME = {v: k for k, v in TYPES.items()}


def find_key(explicit):
    if explicit:
        return explicit
    for candidate in DEFAULT_KEYS:
        if candidate and os.path.isfile(candidate):
            return candidate
    return None


def read_resources(path):
    with open(path, "rb") as handle:
        blob = handle.read()

    if blob[:4] != b"KEY ":
        raise SystemExit(f"{path}: not a KEY file (signature {blob[:4]!r})")

    res_count, res_offset = struct.unpack_from("<II", blob, 12)[0], struct.unpack_from("<I", blob, 20)[0]

    out = []
    for i in range(res_count):
        base = res_offset + i * 14
        name, type_id, locator = struct.unpack_from("<8sHI", blob, base)
        resref = name.split(b"\0", 1)[0].decode("ascii", "replace")
        out.append((resref, type_id, locator))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", help="path to CHITIN.KEY")
    parser.add_argument("--prefix", help="only resrefs starting with this (case-insensitive)")
    parser.add_argument("--type", help="filter by type name (WAV, SPL, ...) or 0xNNNN")
    parser.add_argument("--exists", help="test one exact resref; exit 0 found, 1 missing")
    parser.add_argument("--count", action="store_true", help="print only the number of matches")
    args = parser.parse_args()

    key_path = find_key(args.key)
    if not key_path:
        raise SystemExit("CHITIN.KEY not found; pass --key or set IWD2_GAME_DIR")

    resources = read_resources(key_path)

    want_type = None
    if args.type:
        want_type = TYPE_BY_NAME.get(args.type.upper())
        if want_type is None:
            want_type = int(args.type, 0)

    if args.exists:
        target = args.exists.upper()
        for resref, type_id, _ in resources:
            if resref.upper() == target and (want_type is None or type_id == want_type):
                print(f"{resref} ({TYPES.get(type_id, hex(type_id))}) present")
                return 0
        print(f"{args.exists} absent")
        return 1

    matches = []
    for resref, type_id, _ in resources:
        if want_type is not None and type_id != want_type:
            continue
        if args.prefix and not resref.upper().startswith(args.prefix.upper()):
            continue
        matches.append((resref, type_id))

    if args.count:
        print(len(matches))
        return 0

    for resref, type_id in sorted(matches):
        print(f"{resref:<10} {TYPES.get(type_id, hex(type_id))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
