#!/usr/bin/env python3
"""Emit ``vtable_map.json``: ``{class: {slot_off: {addr, name, recovered}}}`` from
IWD2.exe's real vtables.

The re-agent workflow has no vtable ground truth (the ghidra-bridge ``_vtables.json``
export is empty), so virtual calls -- ``(**(code **)(*this + 0x1c))()`` in the
decompiler -- resolve to nothing. This builder fills that gap by REUSING
``vtable_audit.py``'s binary anchoring (no RTTI needed): each class's vtable is
located in ``.rdata`` by matching the addresses of slots we already recovered, then
every slot dword is read and named from our recovered src (authoritative) or
Ghidra's ``_index.json`` (fallback).

The result feeds two things:
  * the symbol resolver -- name indirect virtual calls by ``(class, slot)``;
  * a verifier -- check a reversed virtual method sits at the right slot.

Usage::

    python scripts/reagent_vtable_map.py                 # write vtable_map.json
    python scripts/reagent_vtable_map.py --class CGameSprite   # print one class
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Reuse vtable_audit's PE/vtable machinery rather than duplicate it.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vtable_audit import Image, parse_headers, parse_addrs, find_vtable, vtable_len, EXE  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
DEFAULT_INDEX = REPO / ".ghidra-exports" / "_index.json"
DEFAULT_OUT = REPO / ".ghidra-exports" / "vtable_map.json"

# A Ghidra "name" that is itself a synthetic placeholder carries no information.
SYNTH_RE = re.compile(r"^(PTR_)?(FUN|LAB|DAT|UNK|SUB)_[0-9A-Fa-f]+$", re.IGNORECASE)


def _load_ghidra_index(index_path: Path | None) -> dict[int, str]:
    """Load ``{addr: name}`` from ``_index.json``, skipping synthetic names."""
    out: dict[int, str] = {}
    if not index_path or not index_path.is_file():
        return out
    raw = json.loads(index_path.read_text(encoding="utf-8"))
    for addr_hex, info in raw.items():
        nm = info.get("name") if isinstance(info, dict) else None
        if nm and SYNTH_RE.match(nm) is None:
            try:
                out[int(addr_hex, 16)] = nm
            except ValueError:
                continue
    return out


def build(index_path: Path | None) -> dict:
    """Return ``{class: {"vtable","len","slots":{off:{addr,name,recovered}}}}``."""
    img = Image(EXE)
    classes = parse_headers()
    amap, _all = parse_addrs()

    # Our recovered names: invert (class, method) -> addrs into addr -> "Class::method".
    addr2name: dict[int, str] = {}
    for (cls, meth), addrs in amap.items():
        for a in addrs:
            addr2name.setdefault(a, f"{cls}::{meth}")
    gidx = _load_ghidra_index(index_path)

    out: dict = {}
    for cls in sorted(classes):
        own_anchored = sum(1 for nm in classes[cls]["slots"].values() if amap.get((cls, nm)))
        vbase, nmatch = find_vtable(img, cls, classes, amap)
        # Same credibility gate as the audit: avoid emitting a misanchored vtable.
        if vbase is None or nmatch < max(2, own_anchored // 2):
            continue
        vlen = vtable_len(img, vbase)
        slots: dict[str, dict] = {}
        for off in range(0, vlen, 4):
            a = img.d32(vbase + off)
            slots[f"0x{off:04x}"] = {
                "addr": f"{a:08x}",
                "name": addr2name.get(a) or gidx.get(a),
                "recovered": a in addr2name,
            }
        out[cls] = {"vtable": f"{vbase:08x}", "len": vlen, "slots": slots}
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index", type=Path, default=DEFAULT_INDEX, help="Ghidra _index.json (fallback names)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output vtable_map.json path")
    ap.add_argument("--class", dest="only", help="print one class's vtable to stdout instead of writing all")
    args = ap.parse_args()

    vmap = build(args.index)
    if args.only:
        print(json.dumps(vmap.get(args.only, {}), indent=2))
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(vmap, indent=2), encoding="utf-8")
    n_slots = sum(len(v["slots"]) for v in vmap.values())
    n_named = sum(1 for v in vmap.values() for s in v["slots"].values() if s["name"])
    n_recov = sum(1 for v in vmap.values() for s in v["slots"].values() if s["recovered"])
    print(f"Wrote {len(vmap)} class vtables, {n_slots} slots "
          f"({n_named} named, {n_recov} recovered) to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
