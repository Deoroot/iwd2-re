#!/usr/bin/env python3
"""fn_digest.py - ~25-line digest of a Ghidra export (token-cheap `gb decompile` replacement).

  fn_digest.py 0x703700              digest: signature, vtable note, callees, strings, globals
  fn_digest.py CGameSprite::Render   same, by recovered name
  fn_digest.py 0x703700 --full       resolved decompile -> tmp_digest_<addr>.md (prints path only)

Reads .ghidra-exports/<addr>.json offline (no PyGhidra boot). Names resolved with the
same bricks as reagent_assemble_context.py. Quick look -> this; actual recovery ->
reagent_assemble_context.py (full bundle with PDB/IDS/header).
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reagent_resolve_symbols import (  # noqa: E402
    load_names, resolve, annotate_vcalls, load_vtable_map, lookup_class, virtual_placement,
)

REPO = Path(__file__).resolve().parent.parent
EXPORTS = REPO / ".ghidra-exports"
ADDR_MAP = EXPORTS / "address_map.json"
VTABLE = EXPORTS / "vtable_map.json"
SRC_INDEX = EXPORTS / "src_index.json"

STR_RE = re.compile(r'"((?:[^"\\]|\\.){2,60})"')


def name_to_addr(query: str) -> int | None:
    raw = json.loads(ADDR_MAP.read_text())
    for k, v in raw.items():
        if isinstance(v, dict) and (v.get("full_name") == query or v.get("name") == query):
            return int(k, 16)
    return None


def src_line(full_name: str) -> str | None:
    try:
        c = json.loads(SRC_INDEX.read_text())
    except Exception:
        return None
    for rel, syms in c.get("syms", {}).items():
        for s in syms:
            fn = (s["qual"] + "::" + s["name"]) if s["qual"] else s["name"]
            if s.get("kind") == "fn" and fn == full_name:
                return f"{rel}:{s['line']}"
    return None


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    full = "--full" in sys.argv
    if not args:
        print(__doc__.strip())
        return 2
    q = args[0]
    if re.fullmatch(r"0[xX][0-9A-Fa-f]+", q):
        addr = int(q, 16)
    else:
        a = name_to_addr(q)
        if a is None:
            print(f"name not in address_map: {q}")
            return 1
        addr = a

    path = EXPORTS / f"{addr:08x}.json"
    if not path.is_file():
        print(f"no export for {addr:#x} -- ensure a // 0x{addr:X} marker exists in src, then "
              f"reagent_address_map.py + `gb export create-functions` (see CLAUDE.md)")
        return 1
    exp = json.loads(path.read_text(encoding="utf-8"))
    decomp = exp.get("decompiled") or ""
    name = exp.get("name") or f"FUN_{addr:08x}"

    names = load_names(ADDR_MAP, EXPORTS / "_index.json", EXPORTS / "_globals.json")
    rewritten, _res, unres = resolve(decomp, names)
    cls = lookup_class(ADDR_MAP, f"{addr:#x}")
    vmap = load_vtable_map(VTABLE) if VTABLE.is_file() else {}
    n_vcall = 0
    if cls and vmap:
        rewritten, n_vcall = annotate_vcalls(rewritten, cls, vmap)

    if full:
        out = REPO / f"tmp_digest_{addr:08x}.md"
        out.write_text(f"# {name} @ {addr:#x}\n`{exp.get('signature', '')}`\n\n```c\n{rewritten.strip()}\n```\n",
                       encoding="utf-8")
        print(f"{out}  ({rewritten.count(chr(10)) + 1} lines)")
        return 0

    lines = rewritten.count("\n") + 1
    loc = src_line(name)
    print(f"{name} @ {addr:#x}  {exp.get('calling_convention', '?')}  {lines} decomp lines"
          + (f"  {loc}" if loc else "  (not in src)"))
    print(f"`{exp.get('signature', '')}`")
    vplace = virtual_placement(addr, vmap, cls, name) if vmap else None
    if vplace:
        print(f"VIRTUAL slot {vplace['slot']} of {vplace['class']}"
              + (f" (COMDAT-folded x{vplace['n_placements']})" if vplace.get("folded") else ""))
    if exp.get("is_thunk"):
        print("THUNK")

    callees = exp.get("callees") or []
    if callees:
        seen = set()
        out = []
        for c in callees:
            nm = c.get("name") or "?"
            if nm.startswith(("FUN_", "LAB_", "SUB_")) and c.get("addr"):
                nm = names.get(int(c["addr"], 16), nm) if isinstance(names, dict) else nm
            if nm not in seen:
                seen.add(nm)
                out.append(nm)
        print(f"callees ({len(out)}): " + ", ".join(out[:20]) + (" ..." if len(out) > 20 else ""))
    callers = exp.get("callers") or []
    if callers:
        cn = [c.get("name", "?") for c in callers]
        print(f"callers ({len(cn)}): " + ", ".join(cn[:12]) + (" ..." if len(cn) > 12 else ""))
    else:
        print("callers: none exported (virtual-only or root)")
    if n_vcall:
        print(f"virtual calls in body: {n_vcall} (annotated in --full)")

    strs = []
    for m in STR_RE.finditer(decomp):
        s = m.group(1)
        if s not in strs and not s.startswith("\\"):
            strs.append(s)
    if strs:
        print("strings: " + " | ".join(f'"{s}"' for s in strs[:10]) + (" ..." if len(strs) > 10 else ""))

    globs = sorted(set(re.findall(r"\b(?:DAT_|g_)\w+", rewritten)))
    if globs:
        print("globals: " + ", ".join(globs[:12]) + (" ..." if len(globs) > 12 else ""))
    if unres:
        print(f"unresolved tokens: {len(unres)} (FUN_/DAT_ without names)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
