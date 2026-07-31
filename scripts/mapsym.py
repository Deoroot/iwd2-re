#!/usr/bin/env python3
"""Resolve OUR build's runtime addresses to source symbols via the linker map.

sym.py answers the same question for the ORIGINAL IWD2.exe (Ghidra address_map).
This is the counterpart for `iwd2-re.exe`, whose addresses only exist at runtime:
frida_hang_bt.py / frida_crash_guard.py print frames as `iwd2-re.exe+0xRVA`, and
the /MAP file the build already emits (CMakeLists.txt target_link_options) is the
only thing that maps those back to C++.

  scripts/mapsym.py 0x495859 0x458e97 ...        # RVAs (module-base relative)
  scripts/mapsym.py --va 0x895859                # preferred-load VAs instead
  scripts/mapsym.py --map other.map 0x1234       # default: tmp_iwd2re.map

Prints the nearest preceding public symbol and the offset into it. Undecorate the
MSVC mangling yourself if needed -- the map's `Publics by Value` names are raw.
"""
import argparse
import bisect
import os
import re
import sys

DEFAULT_MAP = "tmp_iwd2re.map"
# " 0001:00095859       ?Foo@Bar@@QAEXXZ           00495859 f   CProjectile.obj"
ROW = re.compile(
    r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+\S*\s*(\S+)?\s*$"
)


def load(path):
    base = 0x400000
    syms = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            if "Preferred load address is" in line:
                base = int(line.rsplit(None, 1)[1], 16)
                continue
            m = ROW.match(line)
            if m:
                syms.append((int(m.group(2), 16), m.group(1), m.group(3) or ""))
    syms.sort(key=lambda s: s[0])
    return base, syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addrs", nargs="+")
    ap.add_argument("--map", default=DEFAULT_MAP)
    ap.add_argument("--va", action="store_true",
                    help="inputs are preferred-load VAs, not module-relative RVAs")
    args = ap.parse_args()

    if not os.path.exists(args.map):
        sys.exit("map not found: %s (pull it: scripts/vm.sh pull "
                 "'C:/iwd2-re/build/Debug/iwd2-re.map' %s)" % (args.map, DEFAULT_MAP))

    base, syms = load(args.map)
    keys = [s[0] for s in syms]
    if not syms:
        sys.exit("no 'Publics by Value' rows parsed from %s" % args.map)

    for a in args.addrs:
        v = int(a, 16)
        va = v if args.va else base + v
        i = bisect.bisect_right(keys, va) - 1
        if i < 0:
            print("0x%08x  <before first symbol>" % va)
            continue
        addr, name, obj = syms[i]
        print("0x%08x  %s+0x%x  [%s]" % (va, name, va - addr, obj))


if __name__ == "__main__":
    main()
