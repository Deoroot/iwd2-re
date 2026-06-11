#!/usr/bin/env python3
"""Extract class layouts from the BG2EE PDB dump and emit them as prompt context.

deepseek (raw chat/completions, no tools) cannot *fetch* the BG2 PDB, so a class's
field names must be INJECTED into the first-pass prompt. BG2EE and IWD2 are the same
Infinity engine, an EARLIER game -- field NAMES and ROLES carry over (~70%, cf the
bg2-pdb-names memory) but OFFSETS DIFFER (different game, different struct growth).
So this injects ``{name, type, bg2_offset}`` strictly as a *naming* hint, with a loud
caveat that the offset is BG2's, not IWD2's.

Source = ``data/pdb/extracted/bg2_pdb_types.txt``: an LLVM ``llvm-pdbutil``-style
CodeView type dump (UTF-16 LE). A class's real definition is the ``LF_CLASS`` /
``LF_STRUCTURE`` record whose ``field list`` is a real index and whose ``sizeof`` is
non-zero (the other, forward-ref, record has ``field list: <no type>`` / ``sizeof 0``).
Its field list (``LF_FIELDLIST``) holds ``LF_BCLASS`` bases and ``LF_MEMBER`` data
members; member types are type-indices resolved here to a readable name (class ->
name, pointer -> ``referent*``, modifier -> ``const ...``, primitives via the inline
``0xHHHH (name)`` the dump itself prints; anything else falls back to the raw index --
honest, never invented).

Usage::

    python scripts/reagent_bg2_pdb.py                       # build bg2_pdb_layout.json
    python scripts/reagent_bg2_pdb.py --class CGameSprite   # print the inject block
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TYPES = REPO / "data" / "pdb" / "extracted" / "bg2_pdb_types.txt"
DEFAULT_OUT = REPO / ".ghidra-exports" / "bg2_pdb_layout.json"

# Record header: ``   0x1635C | LF_CLASS [size = 48] `CResText` ``
HDR_RE = re.compile(r"^\s*0x([0-9A-Fa-f]+) \| LF_(\w+)\b(.*)$")
NAME_RE = re.compile(r"`(.+)`")
MEMBER_RE = re.compile(r"- LF_MEMBER \[name = `(.+?)`, Type = 0x([0-9A-Fa-f]+), offset = (\d+)")
BCLASS_TYPE_RE = re.compile(r"type = 0x([0-9A-Fa-f]+), offset = (\d+)")
FIELDLIST_RE = re.compile(r"field list: (0x[0-9A-Fa-f]+|<no type>)")
SIZEOF_RE = re.compile(r"sizeof (\d+)")
REFERENT_RE = re.compile(r"referent = 0x([0-9A-Fa-f]+)(?:, mode = (\w+))?")
ELEMENT_RE = re.compile(r"element type: 0x([0-9A-Fa-f]+)")
INLINE_PRIM_RE = re.compile(r"0x([0-9A-Fa-f]+) \(([^)]+)\)")

NAMED_KINDS = {"CLASS", "STRUCTURE", "UNION", "ENUM", "INTERFACE"}


def build(types_path: Path) -> dict:
    """Single pass over the dump -> ``{class: {size, bases, members}}``."""
    names: dict[int, str] = {}          # ti -> displayed name (classes, enums, ...)
    prims: dict[int, str] = {}          # ti -> primitive name, harvested inline
    ptr: dict[int, tuple[int, str]] = {}  # ti -> (referent_ti, mode)
    mod: dict[int, int] = {}            # ti -> referent_ti (const/volatile modifier)
    arr: dict[int, int] = {}            # ti -> element_ti (LF_ARRAY)
    fieldlists: dict[int, dict] = {}    # fl_ti -> {members, bases}
    classes: dict[str, dict] = {}       # name -> {size, fl}

    cur_fl: int | None = None
    pending_bclass = False
    # A class record's `field list:` / `sizeof` lines follow its header; buffer them.
    pend_cls: dict | None = None

    def commit_class() -> None:
        if not pend_cls:
            return
        name, fl, size = pend_cls.get("name"), pend_cls.get("fl"), pend_cls.get("size")
        if name and fl not in (None, "<no type>") and size:
            prev = classes.get(name)
            # Prefer the record that actually has members (real def over a stub).
            if prev is None or not prev.get("fl"):
                classes[name] = {"size": size, "fl": int(fl, 16)}

    with types_path.open(encoding="utf-16") as fh:
        for raw in fh:
            for ti_hex, nm in INLINE_PRIM_RE.findall(raw):
                prims.setdefault(int(ti_hex, 16), nm)

            m = HDR_RE.match(raw)
            if m:
                commit_class()
                pend_cls = None
                cur_fl = None
                pending_bclass = False
                ti = int(m.group(1), 16)
                kind, rest = m.group(2), m.group(3)
                if kind in NAMED_KINDS:
                    nmatch = NAME_RE.search(rest)
                    if nmatch:
                        names[ti] = nmatch.group(1)
                    if kind in ("CLASS", "STRUCTURE"):
                        pend_cls = {"ti": ti, "name": names.get(ti)}
                elif kind == "FIELDLIST":
                    cur_fl = ti
                    fieldlists[ti] = {"members": [], "bases": []}
                elif kind == "POINTER":
                    pend_cls = {"_ptr": ti}  # await referent line
                elif kind == "MODIFIER":
                    pend_cls = {"_mod": ti}  # await referent line
                elif kind == "ARRAY":
                    pend_cls = {"_arr": ti}  # await element-type line
                continue

            # Continuation lines (belong to the most recent record).
            if pend_cls is not None and "_ptr" in pend_cls:
                rm = REFERENT_RE.search(raw)
                if rm:
                    ptr[pend_cls["_ptr"]] = (int(rm.group(1), 16), rm.group(2) or "ptr")
                    pend_cls = None
                continue
            if pend_cls is not None and "_mod" in pend_cls:
                rm = REFERENT_RE.search(raw)
                if rm:
                    mod[pend_cls["_mod"]] = int(rm.group(1), 16)
                    pend_cls = None
                continue
            if pend_cls is not None and "_arr" in pend_cls:
                em = ELEMENT_RE.search(raw)
                if em:
                    arr[pend_cls["_arr"]] = int(em.group(1), 16)
                    pend_cls = None
                continue
            if pend_cls is not None and "name" in pend_cls:
                fl = FIELDLIST_RE.search(raw)
                if fl:
                    pend_cls["fl"] = fl.group(1)
                sz = SIZEOF_RE.search(raw)
                if sz:
                    pend_cls["size"] = int(sz.group(1))
                continue

            if cur_fl is not None:
                if pending_bclass:
                    bm = BCLASS_TYPE_RE.search(raw)
                    if bm:
                        fieldlists[cur_fl]["bases"].append(
                            (int(bm.group(1), 16), int(bm.group(2))))
                        pending_bclass = False
                    continue
                mm = MEMBER_RE.search(raw)
                if mm:
                    fieldlists[cur_fl]["members"].append(
                        (mm.group(1), int(mm.group(2), 16), int(mm.group(3))))
                elif "- LF_BCLASS" in raw:
                    pending_bclass = True

    commit_class()

    def resolve(ti: int, depth: int = 0) -> str:
        if depth > 6:
            return f"0x{ti:x}"
        if ti in names:
            return names[ti]
        if ti in prims:
            return prims[ti]
        if ti in ptr:
            ref, _mode = ptr[ti]
            return resolve(ref, depth + 1) + "*"
        if ti in mod:
            return "const " + resolve(mod[ti], depth + 1)
        if ti in arr:
            return resolve(arr[ti], depth + 1) + "[]"
        return f"0x{ti:x}"

    out: dict = {}
    for name, info in classes.items():
        fl = fieldlists.get(info["fl"])
        if not fl:
            continue
        bases = [{"name": resolve(t), "offset": off} for t, off in fl["bases"]]
        members = [{"name": nm, "type": resolve(t), "offset": off}
                   for nm, t, off in fl["members"]]
        if not bases and not members:
            continue
        out[name] = {"size": info["size"], "bases": bases, "members": members}
    return out


def format_block(name: str, layout: dict) -> str:
    """Render one class layout as an injectable comment block (or '' if unknown)."""
    cls = layout.get(name)
    if not cls:
        return ""
    lines = [
        f"// BG2EE PDB layout for {name} (same Infinity engine, EARLIER game -- "
        "offsets DIFFER in IWD2;",
        "//   use ONLY to name members by role/type, NEVER trust the offset):",
        f"//   sizeof {cls['size']}",
    ]
    for b in cls["bases"]:
        lines.append(f"//   +{b['offset']:<5} base {b['name']}")
    for mem in cls["members"]:
        lines.append(f"//   +{mem['offset']:<5} {mem['name']} : {mem['type']}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--types", type=Path, default=DEFAULT_TYPES, help="BG2 PDB types dump (UTF-16)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output bg2_pdb_layout.json")
    ap.add_argument("--class", dest="only", help="print the inject block for one class (build if needed)")
    args = ap.parse_args()

    if args.only:
        layout = json.loads(args.out.read_text(encoding="utf-8")) if args.out.is_file() else build(args.types)
        block = format_block(args.only, layout)
        print(block or f"// (no BG2 PDB layout for {args.only})")
        return 0

    if not args.types.is_file():
        print(f"types dump not found: {args.types}")
        return 2
    layout = build(args.types)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(layout, indent=2), encoding="utf-8")
    n_mem = sum(len(c["members"]) for c in layout.values())
    print(f"Wrote {len(layout)} class layouts, {n_mem} members to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
