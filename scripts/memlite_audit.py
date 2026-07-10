#!/usr/bin/env python3
"""memlite_audit — cross-check iwd2 memlite state-shaped memories against src/.

`memlite audit` lists memories whose claims expire when the world moves
(pending/blocked/stub/...) and extracts the 0xADDRs they cite. This wrapper
resolves each address through scripts/src_find.py and reports which claims
look dead (function recovered since the memory was written), with ready-to-
paste `resolve` commands. It never modifies the store: verify, then run the
suggested command yourself.

Usage: python3 scripts/memlite_audit.py [--min-lines 15]
"""
import argparse
import json
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

MEMLITE = "/home/wills/memlite/memlite.py"
DB = str(Path("~/.memlite/iwd2.db").expanduser())
SRC_FIND = str(Path(__file__).resolve().parent / "src_find.py")
SPAN_RE = re.compile(r"\[(\d+)-(\d+)\]")
# IWD2.exe code lives above ImageBase 0x400000; smaller hex = struct offsets,
# larger = data/garbage (0xCCCCCCCC) — neither says anything about recovery
FN_RANGE = (0x400000, 0xA00000)


def src_lookup(addr):
    r = subprocess.run([sys.executable, SRC_FIND, addr], capture_output=True, text=True)
    line = r.stdout.strip().splitlines()[0] if r.stdout.strip() else ""
    if r.returncode != 0 or not line or line == "no match":
        return addr, None, 0
    m = SPAN_RE.search(line)
    span = (int(m.group(2)) - int(m.group(1)) + 1) if m else 0
    return addr, line.split()[0], span


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--min-lines", type=int, default=15,
                    help="body span at/above which a cited fn counts as recovered")
    args = ap.parse_args()

    out = subprocess.run([sys.executable, MEMLITE, "--db", DB, "audit", "--json"],
                         capture_output=True, text=True, check=True).stdout
    hits = json.loads(out)
    if not hits:
        print("no state-shaped memories")
        return

    for h in hits:
        h["addrs"] = [a for a in h["addrs"] if FN_RANGE[0] <= int(a, 16) < FN_RANGE[1]]
    addrs = sorted({a for h in hits for a in h["addrs"]})
    with ThreadPoolExecutor(max_workers=8) as pool:
        looked = {a: (name, span) for a, name, span in pool.map(src_lookup, addrs)}

    stale, unsure, manual = [], [], []
    for h in hits:
        # prefs are durable working rules: cited fns being recovered since
        # doesn't expire them — never suggest resolution mechanically
        if not h["addrs"] or h["kind"] == "pref":
            manual.append(h)
            continue
        verdicts = [(a, *looked[a]) for a in h["addrs"]]
        recovered = [v for v in verdicts if v[2] >= args.min_lines]
        h["verdicts"] = verdicts
        # every cited fn now has a real body -> the "stub/blocked" claim is dead
        (stale if len(recovered) == len(verdicts) else unsure).append(h)

    def show(h):
        print(f"  {h['id']}  {h['kind']:<8}{h['age_days']:>4}d  {h['title']}")
        for a, name, span in h.get("verdicts", []):
            state = f"{name} ({span} lines)" if name else "NOT IN SRC"
            print(f"      {a}: {state}")

    if stale:
        print(f"== {len(stale)} likely-dead claims (all cited fns recovered) ==")
        for h in stale:
            show(h)
            print(f"      -> {MEMLITE} --db {DB} resolve {h['id']} \"<how it ended>\"")
    if unsure:
        print(f"== {len(unsure)} cited addrs not (fully) recovered — probably still live ==")
        for h in unsure:
            show(h)
    if manual:
        print(f"== {len(manual)} state-shaped, no addr cited — manual review ==")
        for h in manual:
            show(h)


if __name__ == "__main__":
    main()
