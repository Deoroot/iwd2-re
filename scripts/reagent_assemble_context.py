#!/usr/bin/env python3
"""Assemble the full first-pass reverse-engineering context for ONE function into a
single prompt-ready bundle -- the "awareness layer" the LLM (deepseek, no tools)
cannot gather itself.

deepseek does the bulk first pass but is a raw chat endpoint: it cannot decompile,
cannot open the BG2 PDB, cannot read the IDS tables or the vtables. So everything it
needs must be INJECTED. This script is the mechanical (no-LLM, deterministic) front
half of the integration driver: it gathers, from the offline Ghidra export and our
recovered sources, one structured document the model can answer from directly.

Fully OFFLINE -- reads ``.ghidra-exports`` (the per-address ``<addr>.json`` already
carries ``decompiled`` + ``callees``) and ``src/``; no PyGhidra boot. Sections:

  1. the Ghidra decompile, with FUN_/DAT_/LAB_ rewritten to our recovered names
     (brick a) and ``(*this+0xNN)`` virtual calls annotated by vtable slot (brick b);
  2. the REQUIRED CALL SET -- the exact functions the binary calls (ground truth from
     the export call graph): the model's output must reproduce these;
  3. the class layout from the BG2EE PDB (brick c) -- field NAMES to reuse (offsets
     differ, it's an earlier game);
  4. IDS candidates for any switch/case engine constants (brick d);
  5. the existing recovered header for the class, for style + member names.

The bundle ends with the recovery instructions (idiomatic C++ matching ``src/``,
injected names only, ``#guess`` for uncertain renames, leave unknown tokens as-is,
reproduce the call set, invent nothing -- missing > wrong).

Usage::

    python scripts/reagent_assemble_context.py --address 0x402b70
    python scripts/reagent_assemble_context.py --address 0x402b70 --out ctx.md
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reagent_resolve_symbols import (  # noqa: E402
    load_names, resolve, annotate_vcalls, load_vtable_map, lookup_class,
)
from reagent_ids_constants import load_ids, annotate as ids_annotate  # noqa: E402
from reagent_bg2_pdb import format_block as bg2_block  # noqa: E402

REPO = Path(r"C:\iwd2-re")
EXPORTS = REPO / ".ghidra-exports"
DEFAULT_MAP = EXPORTS / "address_map.json"
DEFAULT_INDEX = EXPORTS / "_index.json"
DEFAULT_GLOBALS = EXPORTS / "_globals.json"
DEFAULT_VTABLE = EXPORTS / "vtable_map.json"
DEFAULT_BG2 = EXPORTS / "bg2_pdb_layout.json"
DEFAULT_IDS = REPO / "data" / "near_infinity_export" / "IDS"
SRC = REPO / "src"

INSTRUCTIONS = """\
## Your task

Recover the C++ body of this function as it appears in IWD2.exe. Rules (in order):

1. Match the existing `src/` style: real `this`, `CClass::Method`, member names, RAII.
   Output buildable C++ (VS2019 Win32), not flat pseudo-C (`undefined4`/`CONCAT31`).
2. Use ONLY the names injected below (resolved decompile, class layout, IDS, header).
   Do NOT borrow field names from another game's headers beyond the BG2 PDB block,
   and remember its OFFSETS differ -- match members by role/type, never by offset.
3. Reproduce the REQUIRED CALL SET exactly -- every function the binary calls must
   appear; do not add calls it does not make. (An inlined helper may show its own
   callees here; prefer calling the helper if `src/` already has it.)
4. A rename you are unsure of: mark `/*#guess*/` if plausible, else leave the original
   token (`FUN_0089abcd`, `field_0x40`). Missing a name is better than a wrong one.
5. Invent no behaviour. If a path is unrecoverable, leave it a faithful stub (early
   return / no-op) rather than guessing. Ghidra is the truth.

OUTPUT FORMAT (strict): keep any analysis to a few lines, then end your reply with
the COMPLETE function in exactly ONE ```cpp code block, and that block MUST begin
with the `// 0xADDR` address marker. Do not put decompile snippets in code blocks --
only your final C++ goes in a fenced block.
"""


def load_export(address: int) -> dict:
    path = EXPORTS / f"{address:08x}.json"
    if not path.is_file():
        raise SystemExit(f"no export for {address:#x} at {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def call_set(callees: list, names: dict[int, str]) -> list[str]:
    """Resolve the export's callee list to our names where possible, deduped."""
    out: set[str] = set()
    for c in callees or []:
        nm = c.get("name") or ""
        addr = c.get("addr")
        if nm.startswith(("FUN_", "LAB_", "SUB_")) and addr:
            try:
                nm = names.get(int(addr, 16), nm)
            except ValueError:
                pass
        if nm:
            out.add(nm)
    return sorted(out)


def header_for(rel_file: str | None) -> str | None:
    """Return the recovered class header text (``.h`` beside the ``.cpp``)."""
    if not rel_file:
        return None
    h = SRC / (Path(rel_file).stem + ".h")
    return h.read_text(encoding="utf-8", errors="replace") if h.is_file() else None


def build(address: int, args) -> str:
    exp = load_export(address)
    decomp = exp.get("decompiled") or ""
    name = exp.get("name") or f"FUN_{address:08x}"
    sig = exp.get("signature") or ""

    names = load_names(args.map, None if args.no_ghidra else args.index,
                       None if args.no_ghidra else args.globals_)
    rewritten, _resolved, _unres = resolve(decomp, names)

    cls = lookup_class(args.map, f"{address:#x}")
    n_vcall = 0
    if cls and args.vtable.is_file():
        rewritten, n_vcall = annotate_vcalls(rewritten, cls, load_vtable_map(args.vtable))

    calls = call_set(exp.get("callees"), names)

    bg2 = ""
    if cls and args.bg2.is_file():
        bg2 = bg2_block(cls, json.loads(args.bg2.read_text(encoding="utf-8")))

    ids_blk = ""
    if args.ids_dir.is_dir():
        ids_blk, _n = ids_annotate(rewritten, load_ids(args.ids_dir), args.max_symbols)

    rel = None
    raw_map = json.loads(args.map.read_text(encoding="utf-8"))
    info = raw_map.get(f"{address:08x}")
    if isinstance(info, dict):
        rel = info.get("file")
    header = header_for(rel)

    parts: list[str] = [f"# Reverse target: {name} @ {address:#06x}", f"`{sig}`", ""]
    parts += ["## Ghidra decompile (our names resolved"
              + (f", {n_vcall} virtual call(s) annotated" if n_vcall else "") + ")",
              "```c", rewritten.strip(), "```", ""]
    if calls:
        parts += ["## Required call set (binary ground truth -- reproduce these)",
                  *[f"- {c}" for c in calls], ""]
    if bg2:
        parts += ["## Class layout (BG2EE PDB)", "```", bg2, "```", ""]
    if ids_blk:
        parts += ["## Engine constants (IDS candidates)", "```", ids_blk, "```", ""]
    if header:
        parts += [f"## Existing header ({Path(rel).stem}.h)", "```cpp", header.strip(), "```", ""]
    parts += [INSTRUCTIONS]
    return "\n".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--address", required=True, help="target function address (e.g. 0x402b70)")
    ap.add_argument("--map", type=Path, default=DEFAULT_MAP)
    ap.add_argument("--index", type=Path, default=DEFAULT_INDEX)
    ap.add_argument("--globals", dest="globals_", type=Path, default=DEFAULT_GLOBALS)
    ap.add_argument("--vtable", type=Path, default=DEFAULT_VTABLE)
    ap.add_argument("--bg2", type=Path, default=DEFAULT_BG2)
    ap.add_argument("--ids-dir", type=Path, default=DEFAULT_IDS)
    ap.add_argument("--max-symbols", type=int, default=5)
    ap.add_argument("--no-ghidra", action="store_true", help="resolve only against our address_map")
    ap.add_argument("--out", type=Path, help="write the bundle here (default: stdout)")
    args = ap.parse_args()

    try:
        addr = int(args.address, 16)
    except ValueError:
        print(f"bad address: {args.address}")
        return 2

    bundle = build(addr, args)
    if args.out:
        args.out.write_text(bundle, encoding="utf-8")
        print(f"wrote {len(bundle)} chars -> {args.out}")
    else:
        sys.stdout.write(bundle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
