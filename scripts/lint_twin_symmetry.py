#!/usr/bin/env python3
"""Lint: antonym-pair operand swaps (copy-paste asymmetry).

Catches the class where a statement assigning to a `<side>`-named target pulls in
an operand from the OPPOSITE side of the same antonym family -- the signature of a
mirrored block copied and half-renamed. Found 1 of the 3 CVidPoly::FillConvexPoly
edge-setup swaps (fixed 9cf9c3ee):

    nLeftErrTerm = nRightAdjUp - nLeftDy;   // BUG: nRightAdjUp should be nLeftAdjUp

The LHS is Left-sided; an operand `nRightAdjUp` is Right-sided; the same-side twin
`nLeftAdjUp` exists in the file -> flag. The recurring families are not just
left/right: top/bottom, src/dst, width/height, near/far, first/last,
horizontal/vertical, read/write, push/pop by default (x/y/z, r/g/b, min/max,
begin/end, in/out under --aggressive, where the false-positive rate is higher).

This is `re-agent parity`-blind (structure + call counts match; only the operand
identity differs) and `arg_provenance`-blind (that audits operator+ call sites,
not `-`/`/` with swapped locals). It is the source-only twin of the asm operand
audit -- it only catches the ASYMMETRIC swap; a value wrong on BOTH mirrored
sides (e.g. the same FillConvexPoly's nMaxYPt-for-nMinYPt) stays asm/runtime-only.

  python3 scripts/lint_twin_symmetry.py             # scan src/, exit 1 on any hit
  python3 scripts/lint_twin_symmetry.py --aggressive # add FP-prone families
  python3 scripts/lint_twin_symmetry.py --file f.cpp  # one file
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"

# Antonym families. Default set = low false-positive. Single-letter / heavily
# idiomatic families (axis, colour, min/max, ...) gated behind --aggressive.
FAMILIES_DEFAULT = [
    ("left", "right"),
    ("top", "bottom"),
    ("src", "dst"),
    ("source", "dest"),
    ("width", "height"),
    ("near", "far"),
    ("first", "last"),
    ("horizontal", "vertical"),
    ("horz", "vert"),
    ("read", "write"),
    ("push", "pop"),
]
FAMILIES_AGGRESSIVE = [
    ("x", "y"), ("y", "z"), ("x", "z"),
    ("r", "g"), ("g", "b"), ("r", "b"),
    ("min", "max"),
    ("begin", "end"),
    ("in", "out"),
    ("lo", "hi"),
]

# camelCase / Hungarian segment splitter: ACRONYM | Titlecase/lower run | digits.
_SEG = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z][a-z]+|[A-Z]+|[a-z]+|[0-9]+")
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# Compound or plain assignment operator, excluding == <= >= != and ++/-- :
_ASSIGN = re.compile(r"(?<![=!<>+\-*/%&|^])([-+*/%&|^]|<<|>>)?=(?!=)")
_BINOP = re.compile(r"[-+*/%]")


def strip_noise(text: str) -> str:
    """Blank out // and /* */ comments and "..." / '...' literals, keep newlines."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif two == "/*":
            j = text.find("*/", i + 2)
            out.append(re.sub(r"[^\n]", " ", text[i:(n if j < 0 else j + 2)]))
            i = n if j < 0 else j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(" " * (min(j, n - 1) - i + 1))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _cased(seg: str, twin: str) -> str:
    if seg.isupper():
        return twin.upper()
    if seg[:1].isupper():
        return twin[:1].upper() + twin[1:]
    return twin.lower()


def flip(ident: str, span, twin: str) -> str:
    s, e = span
    return ident[:s] + _cased(ident[s:e], twin) + ident[e:]


def ident_sides(seg_text: str, sides):
    """Yield (ident, seg_start, seg_end, token) for each side-bearing segment.

    A side that is the FIRST segment of a `.`/`->` member (a CRect `.left`/`.top`
    field, `.Width()`, ...) is skipped: rect/clip arithmetic legitimately mixes
    opposite edges (`right = left + width`). A side buried inside a member ident
    (`pEdge->nLeftX`) or a plain local (`nLeftAdjUp`) is kept -- that is where the
    copy-paste swap lives.
    """
    for m in _IDENT.finditer(seg_text):
        ident = m.group(0)
        p = m.start()
        is_member = p > 0 and (seg_text[p - 1] == "."
                               or (seg_text[p - 1] == ">" and p > 1 and seg_text[p - 2] == "-"))
        tail = seg_text[m.end():]
        is_call = tail[:1] == "(" or tail.lstrip()[:1] == "("
        for sm in _SEG.finditer(ident):
            tok = sm.group(0).lower()
            if tok not in sides:
                continue
            if is_member and sm.start() == 0:   # CRect field -> geometry, not a swap
                continue
            if is_call and sm.start() == 0:     # min()/max() clamp idiom, not a swap
                continue
            yield ident, sm.start(), sm.end(), tok


def statements(text: str):
    """Yield (lineno, lhs, rhs) for each `LHS = RHS` assignment in stripped text."""
    line_starts = [0]
    for m in re.finditer(r"\n", text):
        line_starts.append(m.end())

    def lineno(pos: int) -> int:
        import bisect
        return bisect.bisect_right(line_starts, pos)

    # Split into ;/{/} terminated chunks; find the assignment op within each.
    for chunk in re.finditer(r"[^;{}]+", text):
        seg = chunk.group(0)
        am = _ASSIGN.search(seg)
        if not am:
            continue
        lhs = seg[:am.start()]
        rhs = seg[am.end():]
        # LHS must be a lone lvalue (ident / member / index), not a condition.
        if not _IDENT.search(lhs):
            continue
        yield lineno(chunk.start() + am.start()), lhs, rhs


def scan_file(path: Path, families):
    text = strip_noise(path.read_text(errors="replace"))
    file_idents = set(_IDENT.findall(text))
    sides = {}                      # side token -> twin
    for a, b in families:
        sides[a] = b
        sides[b] = a

    hits = []
    for lineno, lhs, rhs in statements(text):
        if not _BINOP.search(rhs):  # bare `a = b` (swaps, copies) are not the bug
            continue
        # Families present on the LHS (only those are load-bearing for this stmt).
        lhs_sides = {tok for _, _, _, tok in ident_sides(lhs, sides)}
        if not lhs_sides:
            continue
        lhs_idents = _IDENT.findall(lhs)
        lhs_lvalue = lhs_idents[-1] if lhs_idents else ""
        for ri, s, e, tok in ident_sides(rhs, sides):
            twin = sides[tok]
            # opposite side of a family the LHS uses, and not a side the LHS itself
            # already carries (a stmt that intentionally spans both sides is fine):
            if twin in lhs_sides and tok not in lhs_sides:
                cand = flip(ri, (s, e), twin)
                # `nRight = nLeft + width` defines an edge from its opposite edge:
                # the correction trivially reproduces the lvalue -> not a swap.
                if cand != ri and cand != lhs_lvalue and cand in file_idents:
                    hits.append((lineno, lhs.strip(), rhs.strip(), ri, cand))
                    break
    return hits


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aggressive", action="store_true",
                    help="add FP-prone families (axis x/y/z, colour r/g/b, min/max...)")
    ap.add_argument("--file", help="scan a single file instead of all of src/")
    ap.add_argument("--quiet", action="store_true", help="print hits only")
    args = ap.parse_args()

    families = FAMILIES_DEFAULT + (FAMILIES_AGGRESSIVE if args.aggressive else [])
    paths = ([Path(args.file)] if args.file
             else sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")))

    total = 0
    for p in paths:
        for lineno, lhs, rhs, bad, cand in scan_file(p, families):
            total += 1
            rel = p if args.file else p.relative_to(SRC.parent)
            print(f"{rel}:{lineno}: {lhs} = {rhs}")
            print(f"    `{bad}` is the opposite side of `{lhs}` -- did you mean `{cand}`?")
    if not args.quiet:
        print(f"\n{total} suspected antonym swap(s) "
              f"across {len(paths)} file(s).", file=sys.stderr)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
