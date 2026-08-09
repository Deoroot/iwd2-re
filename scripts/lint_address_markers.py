#!/usr/bin/env python3
"""Flag ``// 0xADDR`` markers that are not function starts in IWD2.exe.

A marker is the only link between a recovered function and the bytes it claims
to be. Nothing checks it: the source compiles, parity looks the address up in
the map *we* generated from that same marker, and the whole toolchain agrees
with itself. `CAIGroup::GroupAction` carried ``// 0x404E80`` for as long as it
existed -- an address 0x180 bytes into the real function, disassembling
mid-instruction -- and every tool reported it as fine while two thirds of the
body sat unrecovered.

Ghidra's ``.ghidra-exports/_index.json`` is keyed by function start address, so
it is the independent answer. Two findings come out of comparing against it:

``unreferenced``
    The marker sits inside a function Ghidra defined, and nothing in the binary
    calls it or points at it. Nothing reaches that address, so it is not the
    start of anything. This is what fails the lint.

``merged``
    The marker sits inside a Ghidra function but a call site or a vtable slot
    does target it, so it is a real entry point that Ghidra swallowed into its
    neighbour. The marker is right and Ghidra's boundary is wrong; reported for
    eyes, not failed.

``undefined``
    No defined function contains the address. Usually means Ghidra has not
    defined a function there yet -- the vtable-only callee case in CLAUDE.md,
    fixed by ``reagent_address_map.py`` + ``gb export create-functions`` -- so
    it is reported but does not fail unless ``--strict``.

Addresses outside the code range, struct offsets and data globals are dropped by
the same rules ``reagent_address_map.py`` uses, so ``// 0x847F6C`` on a constant
is not a finding.
"""
from __future__ import annotations

import argparse
import bisect
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from reagent_address_map import (  # noqa: E402
    ADDR_RE,
    CODE_MAX,
    CODE_MIN,
    CPP_KEYWORDS,
    FILE_EXTENSIONS,
    INLINE_RE,
    QUAL_RE,
    _signature_line,
)

REPO = Path(__file__).resolve().parent.parent
INDEX = REPO / ".ghidra-exports" / "_index.json"


def collect_markers(source_root: Path):
    """Every ``// 0xADDR`` marker that names a function, with its position.

    Mirrors ``reagent_address_map.build_map`` -- same regexes, same rejects --
    but keeps the line number, which the map schema has no room for.
    """
    markers = []

    for path in sorted(source_root.rglob("*")):
        if path.suffix.lower() not in FILE_EXTENSIONS or not path.is_file():
            continue

        rel = path.relative_to(REPO).as_posix()
        file_class = path.stem
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()

        for i, line in enumerate(lines):
            m = ADDR_RE.match(line)
            if not m:
                continue

            addr = int(m.group(1), 16)
            if not (CODE_MIN <= addr <= CODE_MAX):
                continue

            sig = _signature_line(lines, i)
            if sig is None:
                continue

            qm = QUAL_RE.search(sig)
            if qm:
                cls, func = qm.group(1), qm.group(2)
            else:
                im = INLINE_RE.search(sig)
                if not im:
                    continue
                cls, func = file_class, im.group(1)

            if func in CPP_KEYWORDS:
                continue

            markers.append({
                "file": rel,
                "line": i + 1,
                "address": addr,
                "symbol": f"{cls}::{func}",
            })

    return markers


def is_referenced(addr: int) -> bool | None:
    """Does anything in the binary call *addr* or hold a pointer to it?

    Ghidra defining one function over two is common enough that "inside another
    function" is not a verdict on its own. Reachability is: a real entry point
    is reached by an E8 call or sits in a vtable, and an address in the middle
    of a function body is reached by neither. Trying to decode instead -- linear
    sweep from the enclosing start, asking whether the address is an instruction
    boundary -- does not work: over thousands of bytes the sweep re-synchronises
    and every one of the addresses checked here came back "clean", including the
    four that disassemble to garbage.

    Returns None when the binary is unavailable, so the caller can degrade
    instead of failing on no evidence.
    """
    try:
        import sym
    except Exception:
        return None

    try:
        if sym.find_pointers(addr, limit=1):
            return True
        return bool(sym.find_callsites(addr))
    except Exception:
        return None


def audit(markers, starts, names):
    """Split markers into (ok, inside, undefined) against the function starts."""
    ok, inside, undefined = [], [], []

    for marker in markers:
        addr = marker["address"]
        if addr in names:
            ok.append(marker)
            continue

        # Nearest function start at or below the marker. Its extent is taken as
        # running to the next start: Ghidra's index carries no size, so a marker
        # landing in inter-function padding is reported here too -- also not a
        # function start, also worth knowing.
        pos = bisect.bisect_right(starts, addr) - 1
        if pos < 0:
            undefined.append(dict(marker, detail="below the first function"))
            continue

        owner = starts[pos]
        end = starts[pos + 1] if pos + 1 < len(starts) else CODE_MAX
        if addr < end:
            inside.append(dict(marker,
                               owner=owner,
                               owner_name=names[owner],
                               delta=addr - owner,
                               referenced=is_referenced(addr)))
        else:
            undefined.append(dict(marker, detail="no function defined here"))

    return ok, inside, undefined


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source-root", default=str(REPO / "src"))
    ap.add_argument("--index", default=str(INDEX),
                    help="Ghidra function index (_index.json)")
    ap.add_argument("--strict", action="store_true",
                    help="also fail on markers with no function defined there")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--quiet", action="store_true",
                    help="only failures, as `path:line: message` for the arc gate")
    ap.add_argument("--show-undefined", type=int, default=10,
                    help="print up to N `undefined` findings (0 to suppress)")
    args = ap.parse_args()

    index_path = Path(args.index)
    if not index_path.is_file():
        print(f"skip: no function index at {index_path}", file=sys.stderr)
        return 0

    raw = json.loads(index_path.read_text(encoding="utf-8"))
    names = {int(a, 16): info.get("name", f"FUN_{a}") for a, info in raw.items()}
    starts = sorted(names)

    source_root = Path(args.source_root)
    if not source_root.is_dir():
        print(f"ERROR: source root not found: {source_root}", file=sys.stderr)
        return 2

    markers = collect_markers(source_root)
    ok, inside, undefined = audit(markers, starts, names)

    # referenced is False only when the scan actually ran and found nothing;
    # None means the binary was unavailable, and those stay in the reported
    # group rather than being promoted to a failure on no evidence.
    unreferenced = [f for f in inside if f["referenced"] is False]
    merged = [f for f in inside if f["referenced"] is True]
    unchecked = [f for f in inside if f["referenced"] is None]

    if args.quiet:
        for f in unreferenced:
            print(f"{f['file']}:{f['line']}: 0x{f['address']:08X} {f['symbol']} is not a"
                  f" function start -- nothing calls or points at it (inside"
                  f" {f['owner_name']}+0x{f['delta']:X})")
        for f in unchecked:
            print(f"{f['file']}:{f['line']}: 0x{f['address']:08X} {f['symbol']} unjudged --"
                  f" could not read the binary (run under .venv-reagent/bin/python)")
    elif args.json:
        print(json.dumps({
            "markers": len(markers),
            "ok": len(ok),
            "unreferenced": unreferenced,
            "merged": merged,
            "unchecked": unchecked,
            "undefined": undefined,
        }, indent=2))
    else:
        print(f"address markers: {len(markers)}  "
              f"function starts: {len(ok)}  "
              f"unreferenced: {len(unreferenced)}  "
              f"merged into a neighbour: {len(merged)}  "
              f"unchecked: {len(unchecked)}  "
              f"no function defined: {len(undefined)}")

        for f in unreferenced:
            print(f"  FAIL {f['file']}:{f['line']}  0x{f['address']:08X} {f['symbol']}"
                  f"  ->  nothing calls or points at it; inside 0x{f['owner']:08X} "
                  f"{f['owner_name']}+0x{f['delta']:X}")

        # Silence here would be the worst outcome: every finding downgrades to a
        # warning and the run still exits 0. Say it instead.
        if unchecked:
            print(f"  NOTE could not read the binary, so {len(unchecked)} finding(s)"
                  f" are unjudged -- run this under .venv-reagent/bin/python")
            for f in unchecked:
                print(f"    ?    {f['file']}:{f['line']}  0x{f['address']:08X} {f['symbol']}"
                      f"  ->  inside 0x{f['owner']:08X} {f['owner_name']}+0x{f['delta']:X}")

        for f in merged:
            print(f"  WARN {f['file']}:{f['line']}  0x{f['address']:08X} {f['symbol']}"
                  f"  ->  inside 0x{f['owner']:08X} {f['owner_name']}+0x{f['delta']:X}"
                  f" (referenced -- Ghidra merged it into its neighbour)")

        if undefined and args.show_undefined:
            print(f"\n  no function defined at these (re-export, or a real gap); "
                  f"showing {min(len(undefined), args.show_undefined)} of {len(undefined)}:")
            for f in undefined[:args.show_undefined]:
                print(f"    {f['file']}:{f['line']}  0x{f['address']:08X} {f['symbol']}")

    if unreferenced:
        return 1
    if args.strict and (merged or unchecked or undefined):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
