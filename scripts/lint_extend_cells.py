#!/usr/bin/env python3
"""Lint: extend-guarded blocks must mutate *VidCellExtend, not *VidCellBase.

Catches the copy-paste class where an `if (m_bExtendDir && !MIRROR_BAM)` block
mirrors a base block but forgot to swap `m_*VidCellBase` -> `m_*VidCellExtend`.

This is the exact signature of the 3-year-old upstream corpse-tint bug in
CGameAnimationTypeMonsterIcewind::ClearColorEffectsAll (fixed 2026-06-13): the
extend block re-cleared the BASE cells (a no-op) so colour/acid-glow affects on
the mirrored-facing (extend) cells were never removed -> permanent green tint
on corpses. `re-agent parity` can never see it: callees + call-counts match
exactly; the only discriminating signal is the per-thiscall member offset,
which the decompile masks. A source-level twin-block check is cheap and exact.

The positive extend guard is `m_bExtendDir && !MIRROR_BAM` (the block runs when
the extend direction is active, so it MUST touch *Extend cells). The inverse
guard `!m_bExtendDir || MIRROR_BAM` legitimately touches *Base and is ignored.

  python3 scripts/lint_extend_cells.py            # scan src/, exit 1 on any hit
  python3 scripts/lint_extend_cells.py --quiet     # only print hits
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"

# Positive extend guard: extend direction ACTIVE -> block must use *VidCellExtend.
# Matches the 3 paren variants + the m_currentBamDirection-extended form; never
# matches the inverse `!m_bExtendDir || MIRROR_BAM` (which correctly uses Base).
GUARD = re.compile(r"m_bExtendDir\s*&&\s*!\s*MIRROR_BAM")

# Cell mutators whose target cell family (Base/Extend) is load-bearing. A call on
# a *Base cell inside a positive extend guard is the copy-paste bug signature.
MUTATORS = (
    "DeleteResPaletteAffect",
    "AddResPaletteAffect",
    "DeleteRangeAffects",
    "AddRangeAffect",
    "SetTintColor",
    "SuppressTint",
    "UnsuppressTint",
    "UnsuppressTintAllRanges",
    "SetColorEffect",
    "SetColorRange",
    "SetPalette",
    "SetResRef",
)
BASE_CALL = re.compile(
    r"\bm_(\w+?)VidCellBase\.(" + "|".join(MUTATORS) + r")\b"
)


def match_block(text: str, open_brace: int) -> int:
    """Return index just past the `}` that closes the block opening at open_brace."""
    depth = 0
    k = open_brace
    n = len(text)
    while k < n:
        c = text[k]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return k + 1
        k += 1
    return n


def scan_file(path: Path):
    text = path.read_text(errors="replace")
    hits = []
    for g in GUARD.finditer(text):
        brace = text.find("{", g.end())
        if brace < 0:
            continue
        end = match_block(text, brace)
        block = text[brace:end]
        for m in BASE_CALL.finditer(block):
            line = text.count("\n", 0, brace + m.start()) + 1
            hits.append((line, f"m_{m.group(1)}VidCellBase.{m.group(2)}()"))
    return hits


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("paths", nargs="*", help="files/dirs (default: src/)")
    args = ap.parse_args()

    targets = [Path(p) for p in args.paths] if args.paths else [SRC]
    files = []
    for t in targets:
        files.extend(sorted(t.rglob("*.cpp")) if t.is_dir() else [t])

    total = 0
    for f in files:
        hits = scan_file(f)
        if hits:
            for line, call in hits:
                rel = f.relative_to(SRC.parent) if SRC.parent in f.parents else f
                print(f"{rel}:{line}: extend block mutates BASE cell: {call} -> should be *VidCellExtend")
            total += len(hits)

    if not args.quiet:
        scanned = len(files)
        if total:
            print(f"\n{total} suspect call(s) across {scanned} file(s) -- verify vs Ghidra, swap Base->Extend.")
        else:
            print(f"OK: no extend-block Base-cell mutations in {scanned} file(s).")
    return 1 if total else 0


if __name__ == "__main__":
    raise SystemExit(main())
