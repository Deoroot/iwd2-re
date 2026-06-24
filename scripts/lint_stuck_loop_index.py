#!/usr/bin/env python3
"""Lint: a counter/index local that a loop uses as a shift amount or array index
but never advances -- so it is stuck at its initial value.

Exact signature of the CSpawn::Read time-of-day bug (0x769BF0, CSpawn.cpp:949):

    int timeOfDay = 0;
    int bit = 0;
    for (int pos = min(sValue.GetLength() - 1, 31); pos >= 0; pos--) {
        if (sValue[pos] != '0' && sValue[pos] != 'o') {
            timeOfDay |= 1 << bit;          // <- `bit` is the shift amount ...
        }
    }                                       // ... but nothing ever does `bit++`

The binary increments the shift counter every iteration (0x76A842 `inc ecx`,
decompiles to `bVar7 = bVar7 + 1`), so the recovered loop is unfaithful and the
mask collapses to bit 0. `re-agent parity` / call-count signals are blind (no
call is involved); cppcheck does not model "this index should advance".

Precision over recall (FP-prone class -> tight conjunction). A loop is flagged
ONLY when a candidate var:
  * is declared `T v = <integer literal>;` shortly before the loop (a fresh
    counter, not a member / parameter / computed value),
  * is NOT one of the loop's own control variables,
  * is used inside the body as a shift amount (`<< v` / `v <<`) or array index
    (`[... v ...]`),
  * is never mutated anywhere in [its declaration .. the loop body end] -- this
    span (not just the body) is what rejects an enclosing for-loop's `v++` in its
    header (nested-loop FP) and a const set just before the loop like `shift = 4;`
    (legit-invariant FP), and
  * does not appear after the loop (its only purpose is the loop).
Any bare mutation (`v++`, `v =`, `v +=`, `&v`, a call/by-ref pass) clears the
flag. Verify each hit against the binary before "fixing" (the faithful loop may
differ in more than the increment -- the CSpawn original also increments `pos`).

  python3 scripts/lint_stuck_loop_index.py            # scan src/, exit 1 on hit
  python3 scripts/lint_stuck_loop_index.py --quiet     # only print hits
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"

LOOP = re.compile(r"\b(for|while)\s*\(")
INT_DECL = re.compile(
    r"\b(?:int|INT|unsigned|UINT|size_t|long|LONG|short|WORD|DWORD|BYTE|char|CHAR)\s+(\w+)\s*=\s*(\d+)\s*;"
)
PRE_WINDOW = 400  # chars before the loop to look for the counter declaration


def _balance(text: str, open_idx: int, opc: str, clc: str) -> int:
    depth = 0
    k = open_idx
    n = len(text)
    while k < n:
        c = text[k]
        if c == opc:
            depth += 1
        elif c == clc:
            depth -= 1
            if depth == 0:
                return k + 1
        k += 1
    return n


def _mutated(region: str, var: str) -> bool:
    """True if `var` is advanced / reassigned / passed by-ref / address-taken
    anywhere in `region` (so it is not provably stuck)."""
    pat = re.compile(r"\b" + re.escape(var) + r"\b")
    for m in pat.finditer(region):
        j = m.end()
        while j < len(region) and region[j] in " \t":
            j += 1
        after = region[j : j + 2]
        p = m.start() - 1
        while p >= 0 and region[p] in " \t":
            p -= 1
        before = region[p] if p >= 0 else ""
        if (
            after[:2] in ("++", "--", "+=", "-=", "*=", "/=", "|=", "&=", "^=")
            or (after[:1] == "=" and after[:2] != "==")
            or after[:1] == ")"  # trailing arg of a call: f(.., var)
            or before in ("&", "(", ",")  # address-of / call argument
        ):
            return True
    return False


def scan_file(path: Path):
    text = path.read_text(errors="replace")
    hits = []
    for lm in LOOP.finditer(text):
        kw = lm.group(1)
        lparen = lm.end() - 1
        head_end = _balance(text, lparen, "(", ")")
        head = text[lparen + 1 : head_end - 1]
        nb = head_end
        while nb < len(text) and text[nb] in " \t\r\n":
            nb += 1
        if nb >= len(text) or text[nb] != "{":
            continue
        body_end = _balance(text, nb, "{", "}")
        body = text[nb:body_end]

        control = set(re.findall(r"[A-Za-z_]\w*", head))
        pre_start = max(0, lm.start() - PRE_WINDOW)
        pre = text[pre_start : lm.start()]
        for dm in INT_DECL.finditer(pre):
            var = dm.group(1)
            if var in control:
                continue
            shift_ctx = re.search(r"<<\s*" + re.escape(var) + r"\b", body) or re.search(
                r"\b" + re.escape(var) + r"\s*<<", body
            )
            index_ctx = re.search(r"\[[^\]]*\b" + re.escape(var) + r"\b[^\]]*\]", body)
            if not (shift_ctx or index_ctx):
                continue
            # span = [end of the var's declaration .. loop body end]
            scan_region = text[pre_start + dm.end() : body_end]
            if _mutated(scan_region, var):
                continue
            # used after the loop? then it has another purpose -> not provably the bug
            if re.search(r"\b" + re.escape(var) + r"\b", text[body_end : body_end + PRE_WINDOW]):
                continue
            line = text.count("\n", 0, lm.start()) + 1
            hits.append((line, var, kw, "shift" if shift_ctx else "index"))
    return hits


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("paths", nargs="*", help="files/dirs (default: src/)")
    args = ap.parse_args()

    targets = [Path(p) for p in args.paths] if args.paths else [SRC]
    files = []
    for t in targets:
        files.extend(sorted(t.rglob("*.cpp")) if t.is_dir() else [t])

    total = 0
    for f in files:
        for line, var, kw, ctx in scan_file(f):
            rel = f.relative_to(SRC.parent) if SRC.parent in f.parents else f
            print(
                f"{rel}:{line}: {kw}-loop {ctx} '{var}' never advances in the body "
                f"-- stuck counter? verify the binary advances it"
            )
            total += 1

    if not args.quiet:
        n = len(files)
        print(
            f"\n{total} stuck-counter loop(s) across {n} file(s) -- verify vs binary, add the missing advance."
            if total
            else f"OK: no stuck loop counters in {n} file(s)."
        )
    return 1 if total else 0


if __name__ == "__main__":
    raise SystemExit(main())
