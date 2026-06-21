#!/usr/bin/env python3
"""Lint: every source file must end with balanced #pragma pack(push)/pop.

A `#pragma pack(push)` without a matching `#pragma pack(pop)` leaks the changed
alignment into every header included afterwards. The same class then compiles to
two different sizes across translation units (an ODR violation) -- one TU's
`new T` allocates fewer bytes than another TU's ctor writes -> a write past the
heap block -> STATUS_HEAP_CORRUPTION that builds perfectly clean.

That is exactly the regression fixed in 1d329bb0 (CAIAction.h shipped a push with
no pop, silently packing CScreenCharacter et al. in downstream TUs and crashing
the boot before world activation).

This is the cheap host-side net (no build needed); the compiler's /we4103
("alignment changed after including header") is the authoritative gate.

Exit non-zero on any imbalance. Run from anywhere:
    python scripts/lint_pragma_pack_balance.py
"""
import os
import re
import sys
import glob

PUSH = re.compile(r"#\s*pragma\s+pack\s*\(\s*push")
POP = re.compile(r"#\s*pragma\s+pack\s*\(\s*pop")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(path):
    """Return list of (line_or_None, message) problems for one file."""
    problems = []
    depth = 0
    for i, line in enumerate(open(path, errors="replace"), 1):
        if PUSH.search(line):
            depth += 1
        elif POP.search(line):
            depth -= 1
            if depth < 0:
                problems.append((i, "#pragma pack(pop) without a matching push"))
                depth = 0
    if depth > 0:
        problems.append((None, f"{depth} unbalanced #pragma pack(push) at EOF -- missing pack(pop) (leaks alignment downstream)"))
    return problems


def main():
    files = sorted(
        glob.glob(os.path.join(REPO, "src", "*.h"))
        + glob.glob(os.path.join(REPO, "src", "*.cpp"))
    )
    fails = 0
    for f in files:
        for line, msg in check(f):
            rel = os.path.relpath(f, REPO)
            loc = f"{rel}:{line}" if line else rel
            print(f"{loc}: {msg}")
            fails += 1
    if fails:
        print(f"\n{fails} pack-balance issue(s) -- a leaked #pragma pack causes cross-TU ODR / heap corruption.")
        sys.exit(1)
    print(f"OK: {len(files)} files, all #pragma pack push/pop balanced.")


if __name__ == "__main__":
    main()
