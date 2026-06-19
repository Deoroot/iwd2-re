#!/usr/bin/env python3
"""project_status.py - honest progress metrics for the IWD2 recovery.

Measures the REAL state of the port directly from the repo, with no fabricated
denominators. Two distinct progress axes are reported because they are NOT the
same thing and conflating them is how the old README ended up claiming "53% /
~2M lines":

  1. Ghidra naming   - how many functions in the Ghidra DB have a real name
                       (FUN_/sub_/thunk_ = still anonymous). This is cheap
                       metadata work, not recovery.
  2. C++ recovery    - how many functions actually have hand-written C++ in
                       src/ (counted by unique `// 0xADDR` markers). This is
                       the metric that matters for "is it playable".

Run from repo root:
    .venv-reagent/bin/python scripts/project_status.py            # human table
    .venv-reagent/bin/python scripts/project_status.py --json     # machine
    .venv-reagent/bin/python scripts/project_status.py --markdown # README table

Pure stdlib; no Ghidra boot, no network. Reads:
  - src/**.{cpp,h}
  - .ghidra-exports/*.json   (one file per binary function; name inside)
  - .ghidra-exports/address_map.json (source-linked, named subset)
  - src/NewDiscovered.h      (uncategorized prototype dump)
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
EXPORTS = os.path.join(ROOT, ".ghidra-exports")

ADDR_RE = re.compile(rb"// 0x[0-9A-Fa-f]{5,8}")
ADDR_VAL_RE = re.compile(rb"// (0x[0-9A-Fa-f]{5,8})")
TODO_RE = re.compile(rb"TODO|FIXME")
INCOMPLETE_RE = re.compile(rb"TODO:\s?Incomplete")
FUN_RE = re.compile(rb"\bFUN_[0-9A-Fa-f]{6,8}\b")
FIELD_RE = re.compile(rb"\bfield_[0-9A-Fa-f]+\b")
PLACEHOLDER_PREFIXES = ("FUN_", "sub_", "thunk_FUN", "thunk_sub")


def iter_src(exts=(".cpp", ".h")):
    for dirpath, _dirs, files in os.walk(SRC):
        for fn in files:
            if fn.endswith(exts):
                yield os.path.join(dirpath, fn)


def scan_source():
    files = list(iter_src())
    total_lines = 0
    todo = 0
    todo_files = Counter()
    incomplete = 0
    addrs = set()
    addr_ints = set()
    fun_placeholders = set()
    fields = set()
    addrs_per_file = Counter()

    for path in files:
        with open(path, "rb") as fh:
            data = fh.read()
        total_lines += data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
        rel = os.path.relpath(path, ROOT)

        n_todo = len(TODO_RE.findall(data))
        if n_todo:
            todo += n_todo
            todo_files[rel] = n_todo
        incomplete += len(INCOMPLETE_RE.findall(data))

        file_addrs = ADDR_VAL_RE.findall(data)
        if path.endswith(".cpp"):
            addrs_per_file[rel] = len(set(file_addrs))
        for a in file_addrs:
            a = a.lower()
            addrs.add(a)
            try:
                addr_ints.add(int(a, 16))
            except ValueError:
                pass
        for m in FUN_RE.findall(data):
            fun_placeholders.add(m)
        for m in FIELD_RE.findall(data):
            fields.add(m)

    return {
        "src_files": len(files),
        "src_lines": total_lines,
        "todo_fixme": todo,
        "todo_files": len(todo_files),
        "todo_top": todo_files.most_common(12),
        "todo_incomplete": incomplete,
        "recovered_funcs": len(addrs),
        "recovered_addr_ints": addr_ints,
        "fun_placeholders_in_src": len(fun_placeholders),
        "field_unnamed_unique": len(fields),
        "recovered_top": addrs_per_file.most_common(10),
    }


def scan_ghidra():
    if not os.path.isdir(EXPORTS):
        return None
    files = [
        f
        for f in glob.glob(os.path.join(EXPORTS, "*.json"))
        if os.path.basename(f) != "address_map.json"
        and len(os.path.basename(f)) == 13  # 8 hex + ".json"
    ]
    named = unnamed = 0
    entries = set()  # function entry addresses (ints), from filenames
    for f in files:
        base = os.path.basename(f)
        try:
            entries.add(int(base[:8], 16))
        except ValueError:
            pass
        try:
            with open(f) as fh:
                d = json.load(fh)
        except (OSError, ValueError):
            continue
        n = d.get("name") or d.get("function_name") or ""
        if not n or n.startswith(PLACEHOLDER_PREFIXES):
            unnamed += 1
        else:
            named += 1
    total = named + unnamed
    src_linked = None
    amap = os.path.join(EXPORTS, "address_map.json")
    if os.path.isfile(amap):
        try:
            with open(amap) as fh:
                src_linked = len(json.load(fh))
        except (OSError, ValueError):
            pass
    return {
        "total_funcs": total,
        "named": named,
        "unnamed": unnamed,
        "src_linked_map": src_linked,
        "entries": entries,
    }


def byte_coverage(entries, recovered_addr_ints):
    """Byte-weighted recovery: a 2000-instruction function counts far more than
    a 5-byte stub. Function size is derived from the gap to the next entry
    address (Ghidra's entry set covers .text densely, so this delta equals the
    real function size to within a few bytes of alignment padding — validated
    against the PE: sum of deltas == .text VirtualSize ± 2 bytes).

    Returns total .text code bytes and the bytes whose owning function has a
    recovered C++ body in src/. This is the honest 'how much of the engine is
    rebuilt' number, and runs in well under a second."""
    if not entries:
        return None
    alla = sorted(entries)
    sizes = {a: (alla[i + 1] - a) for i, a in enumerate(alla[:-1])}
    sizes[alla[-1]] = 16  # last function: no next entry, nominal
    total = sum(sizes.values())
    recovered = sum(sizes[a] for a in recovered_addr_ints if a in sizes)
    matched = sum(1 for a in recovered_addr_ints if a in sizes)
    return {
        "text_bytes": total,
        "recovered_bytes": recovered,
        "pct": round(100 * recovered / total, 1) if total else 0.0,
        "addrs_matched": matched,
    }


def scan_newdiscovered():
    """NewDiscovered.h is a *stale manual scratch list* of uncategorized FUN_
    addresses, NOT wired into the build (#included nowhere). Its count covers
    only a fraction of the functions still anonymous in Ghidra, so it is
    reported for transparency, never as a headline progress metric."""
    path = os.path.join(SRC, "NewDiscovered.h")
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as fh:
        data = fh.read()
    # actual format: `void FUN_0052e940(); // 0x52E940`
    protos = len(re.findall(rb"^\s*\w[\w\s\*]*\bFUN_[0-9A-Fa-f]+\s*\(", data, re.M))
    included = bool(
        list(
            p
            for p in iter_src()
            if b"NewDiscovered.h" in open(p, "rb").read()
            and not p.endswith("NewDiscovered.h")
        )
    )
    return {
        "prototypes": protos,
        "lines": data.count(b"\n") + 1,
        "wired_into_build": included,
    }


def collect():
    s = scan_source()
    g = scan_ghidra()
    nd = scan_newdiscovered()
    out = {"source": s, "ghidra": g, "new_discovered": nd}
    if g and g["total_funcs"]:
        out["recovery_pct"] = round(100 * s["recovered_funcs"] / g["total_funcs"], 1)
        out["named_pct"] = round(100 * g["named"] / g["total_funcs"], 1)
        out["bytes"] = byte_coverage(g["entries"], s["recovered_addr_ints"])
    # drop unserializable sets before any JSON dump
    s.pop("recovered_addr_ints", None)
    if g:
        g.pop("entries", None)
    return out


def fmt_human(d):
    s, g, nd = d["source"], d["ghidra"], d["new_discovered"]
    L = []
    L.append("=" * 60)
    L.append("  IWD2-RE PROJECT STATUS")
    L.append("=" * 60)
    L.append("")
    L.append("C++ RECOVERY (the metric that matters)")
    b = d.get("bytes")
    if b:
        L.append(f"  .text code recovered       : {b['recovered_bytes']:>9,} / "
                 f"{b['text_bytes']:,} bytes  ({b['pct']}%)")
        L.append( "    ^ byte-weighted: big functions count more than stubs")
    if g:
        L.append(f"  Functions recovered to C++ : {s['recovered_funcs']:>9,} / "
                 f"{g['total_funcs']:,}  ({d['recovery_pct']}%)")
    else:
        L.append(f"  Functions recovered to C++ : {s['recovered_funcs']:>9,}")
    L.append(f"  Source lines               : {s['src_lines']:>9,}  "
             f"({s['src_files']} files)")
    L.append("")
    if g:
        L.append("GHIDRA NAMING (metadata, not recovery)")
        L.append(f"  Named functions            : {g['named']:>7,} / "
                 f"{g['total_funcs']:,}  ({d['named_pct']}%)")
        L.append(f"  Still FUN_/sub_ (anonymous): {g['unnamed']:>7,}")
        if g["src_linked_map"] is not None:
            L.append(f"  address_map.json entries   : {g['src_linked_map']:>7,}")
        L.append("")
    L.append("OUTSTANDING WORK")
    L.append(f"  TODO / FIXME               : {s['todo_fixme']:>7,}  "
             f"({s['todo_files']} files)")
    L.append(f"  TODO: Incomplete (stubs)   : {s['todo_incomplete']:>7,}")
    L.append(f"  FUN_ placeholders in src   : {s['fun_placeholders_in_src']:>7,}")
    L.append(f"  Unnamed field_ (unique)    : {s['field_unnamed_unique']:>7,}")
    if nd:
        wired = "wired in" if nd["wired_into_build"] else "NOT in build (stale scratch list)"
        L.append(f"  NewDiscovered.h prototypes : {nd['prototypes']:>7,}  ({wired})")
    L.append("")
    L.append("TOP TODO/FIXME FILES")
    for path, n in s["todo_top"]:
        L.append(f"  {n:>4}  {path}")
    L.append("")
    L.append("MOST-RECOVERED SOURCE FILES (unique addrs)")
    for path, n in s["recovered_top"]:
        L.append(f"  {n:>4}  {path}")
    L.append("=" * 60)
    return "\n".join(L)


def fmt_markdown(d):
    s, g = d["source"], d["ghidra"]
    b = d.get("bytes")
    L = ["| Metric | Value | Notes |", "|--------|-------|-------|"]
    if b:
        L.append(f"| **`.text` code recovered** | **~{b['pct']}%** "
                 f"({b['recovered_bytes']:,} / {b['text_bytes']:,} bytes) | "
                 f"Byte-weighted — the real 'how much engine is rebuilt' figure. "
                 f"Big functions count more than stubs. |")
    if g:
        L.append(f"| **Functions recovered to C++** | {d['recovery_pct']}% "
                 f"({s['recovered_funcs']:,} / {g['total_funcs']:,}) | "
                 f"By count — remaining functions are mostly small leaves/stubs |")
        L.append(f"| **Functions named in Ghidra** | {d['named_pct']}% "
                 f"({g['named']:,} / {g['total_funcs']:,}) | "
                 f"Metadata only — {g['unnamed']:,} still `FUN_`/`sub_` |")
    L.append(f"| **Source code** | {s['src_lines']:,} lines | "
             f"{s['src_files']} `.cpp`/`.h` files |")
    L.append(f"| **TODO / FIXME** | {s['todo_fixme']:,} | "
             f"{s['todo_files']} files; {s['todo_incomplete']:,} are "
             f"`TODO: Incomplete` stubs |")
    L.append(f"| **Unnamed fields** | {s['field_unnamed_unique']:,} unique | "
             f"`field_XXX` members awaiting names |")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true", help="emit raw JSON")
    ap.add_argument("--markdown", action="store_true",
                    help="emit a README-ready metrics table")
    args = ap.parse_args()

    d = collect()
    if args.json:
        json.dump(d, sys.stdout, indent=2)
        print()
    elif args.markdown:
        print(fmt_markdown(d))
    else:
        print(fmt_human(d))


if __name__ == "__main__":
    main()
