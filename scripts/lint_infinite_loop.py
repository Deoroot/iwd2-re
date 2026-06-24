#!/usr/bin/env python3
"""Lint: `while (*p <op> X)` pointer-walk whose body never advances `p`.

Catches the dropped-increment class where a recovered loop walks a pointer/array
via a dereference condition but the body forgot to advance the pointer, looping
forever on the same element.

Exact signature of the ColorTintSolid all-ranges bug (CGameEffectColorTintSolid::
ApplyEffect, 0x4A7000, fixed 2026-06-24): `while (*range != -1) { ...; }` over a
static ranges[] table never did `range++`, so on area load the first creature
wearing an all-ranges colour-tint item allocated CColorEffect forever -> OOM,
the game stalled ~5s at 25% then closed. `re-agent parity` can't see it (the
increment is a `ptr+4`, not a call -> no call-count signal) and cppcheck does
not detect "condition variable never mutated". A source-level check is exact.

Precision over recall (this is a gate): only the unary-deref condition form is
considered, and a loop is flagged ONLY when the controlling pointer appears in
the body solely as `*p` reads -- never bare (`p`, `p++`, `p =`, `&p`, `f(p)`),
any of which could advance it or hand it to a callee by reference. Loops with a
`break`/`return`/`goto` (a plausible alternate exit) are skipped. do/while and
empty-body `while(...);` are skipped.

  python3 scripts/lint_infinite_loop.py            # scan src/, exit 1 on any hit
  python3 scripts/lint_infinite_loop.py --quiet     # only print hits
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"

WHILE = re.compile(r"\bwhile\s*\(")
# A unary deref `*p` is one whose `*` is not preceded by an operand (so `a * b`
# multiplication is excluded). These chars legitimately precede a unary `*`.
UNARY_PREFIX = set("(!&|=<>,+-*/%?:")


def _balance(text: str, open_idx: int, opc: str, clc: str) -> int:
    """Index just past the matching close for the bracket opening at open_idx."""
    depth = 0
    n = len(text)
    k = open_idx
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


def _prev_nonspace(text: str, i: int) -> str:
    j = i - 1
    while j >= 0 and text[j] in " \t\r\n":
        j -= 1
    return text[j] if j >= 0 else ""


def _condition_pointer(cond: str) -> str | None:
    """Return the controlling pointer of a unary-deref condition, else None.

    Targets `*p ...`; rejects `a * b` (multiply). If several distinct deref vars
    appear, bail (ambiguous -> avoid a false positive)."""
    found = set()
    for m in re.finditer(r"\*\s*([A-Za-z_]\w*)", cond):
        if _prev_nonspace(cond, m.start()) in UNARY_PREFIX or m.start() == 0:
            found.add(m.group(1))
    return next(iter(found)) if len(found) == 1 else None


def _advances(body: str, var: str) -> bool:
    """True if `var` appears bare anywhere (advance / reassign / by-ref pass /
    address-of) -- i.e. not solely as a `*var` dereference read."""
    for m in re.finditer(r"\b" + re.escape(var) + r"\b", body):
        if _prev_nonspace(body, m.start()) != "*":
            return True
    return False


def scan_file(path: Path):
    text = path.read_text(errors="replace")
    hits = []
    for w in WHILE.finditer(text):
        # do/while: the `while` is preceded by the body's closing `}`.
        if _prev_nonspace(text, w.start()) == "}":
            continue
        lparen = w.end() - 1
        cond_end = _balance(text, lparen, "(", ")")  # past the `)`
        cond = text[lparen + 1 : cond_end - 1]
        var = _condition_pointer(cond)
        if not var:
            continue
        # body must be a brace block (skip `while(...);` empty-statement loops)
        nb = cond_end
        while nb < len(text) and text[nb] in " \t\r\n":
            nb += 1
        if nb >= len(text) or text[nb] != "{":
            continue
        body = text[nb : _balance(text, nb, "{", "}")]
        if re.search(r"\b(break|return|goto)\b", body):
            continue  # plausible alternate exit -> not provably infinite
        if not _advances(body, var):
            line = text.count("\n", 0, w.start()) + 1
            hits.append((line, var))
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
        for line, var in scan_file(f):
            rel = f.relative_to(SRC.parent) if SRC.parent in f.parents else f
            print(f"{rel}:{line}: while (*{var} ...) never advances '{var}' in body -- infinite loop?")
            total += 1

    if not args.quiet:
        n = len(files)
        print(
            f"\n{total} suspect loop(s) across {n} file(s) -- verify vs binary, add the missing advance."
            if total
            else f"OK: no never-advancing deref loops in {n} file(s)."
        )
    return 1 if total else 0


if __name__ == "__main__":
    raise SystemExit(main())
