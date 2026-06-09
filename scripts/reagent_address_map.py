#!/usr/bin/env python3
"""Emit a ghidra-ai-bridge ``address_map.json`` from our ``// 0xADDR`` convention.

ghidra-ai-bridge's own ``build-map`` only understands hook-registration macros
whose regex captures ``(function_name, address)`` *in that group order* (e.g. the
plugin-sdk preset's ``RH_ScopedInstall(Func, 0xADDR)``). Our faithful source has
no such macro: each recovered function is marked with a bare ``// 0xADDR`` comment
on the line *above* its signature -- address-first, which build-map's group-order
contract cannot express with stdlib ``re`` (fixed-width lookbehind only). Adding a
hook macro to the source would violate the project's "no hacks / match IWD2.exe"
rule, so instead we produce the identical artifact ourselves and point the bridge's
``address_map_path`` at it (``ghidra_ai_bridge.query`` reads that file directly).

Output schema matches ``ghidra_ai_bridge.address_map.build_address_map`` exactly::

    { "0040abcd": {"name", "class", "full_name", "file"}, ... }

keys are 8-char zero-padded lowercase hex.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

FILE_EXTENSIONS = (".cpp", ".h", ".hpp")

# A bare `// 0xADDR` comment alone on its line (trailing text tolerated).
ADDR_RE = re.compile(r"^[ \t]*//[ \t]*0x([0-9A-Fa-f]+)\b")
# Qualified out-of-line definition: `... Class::Func(` (skips template return
# types like `ATL::CStringT<...>&` because they are not followed by `(`).
QUAL_RE = re.compile(r"(~?\w+)\s*::\s*(~?\w+)\s*\(")
# Inline member (header body): grab the identifier just before `(`.
INLINE_RE = re.compile(r"(~?\w+)\s*\(")
# How far below the address comment to look for the signature line.
SIG_LOOKAHEAD = 4

# Real IWD2 code lives in this range (matches ghidra-bridge.yaml code_range). A
# `// 0xADDR` outside it is not a function: small values are struct field offsets
# (`// 0x10`) and high values are data globals (`// 0x847f6c`). Excluding them
# kills false entries like 0x10 -> "ResolveActionTarget" or 0x847f6c -> "RGB".
CODE_MIN = 0x401000
CODE_MAX = 0x800000

# Control-flow / operator keywords the inline fallback can mis-grab as a function
# name when a `// 0xADDR` sits above a non-signature line (e.g. `if (` -> "::if").
CPP_KEYWORDS = frozenset({
    "if", "while", "for", "switch", "else", "return", "do", "case", "default",
    "goto", "break", "continue", "sizeof", "new", "delete", "catch", "throw",
})


def _signature_line(lines: list[str], start: int) -> str | None:
    """First non-blank, non-comment line within SIG_LOOKAHEAD of *start*."""
    for j in range(start + 1, min(start + 1 + SIG_LOOKAHEAD, len(lines))):
        stripped = lines[j].strip()
        if not stripped or stripped.startswith("//"):
            continue
        return lines[j]
    return None


def build_map(source_root: Path) -> tuple[dict, list[tuple[str, int, str]], list[tuple[str, int, str]]]:
    """Return ``(address_map, unparsed, filtered)`` for the source tree.

    *unparsed* lists ``(rel_path, line_no, comment)`` for ``// 0xADDR`` markers
    whose signature could not be parsed; *filtered* lists markers rejected as
    non-functions (out-of-code-range address or keyword name) -- both useful to
    gauge coverage.
    """
    address_map: dict = {}
    unparsed: list[tuple[str, int, str]] = []
    filtered: list[tuple[str, int, str]] = []

    for path in sorted(source_root.rglob("*")):
        if path.suffix.lower() not in FILE_EXTENSIONS or not path.is_file():
            continue
        rel = path.relative_to(source_root).as_posix()
        file_class = path.stem
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()

        for i, line in enumerate(lines):
            m = ADDR_RE.match(line)
            if not m:
                continue
            addr_int = int(m.group(1), 16)
            # Reject non-function annotations: struct field offsets (// 0x10) and
            # data globals (// 0x847f6c) fall outside the code range.
            if not (CODE_MIN <= addr_int <= CODE_MAX):
                filtered.append((rel, i + 1, line.strip()))
                continue
            sig = _signature_line(lines, i)
            if sig is None:
                unparsed.append((rel, i + 1, line.strip()))
                continue

            qm = QUAL_RE.search(sig)
            if qm:
                cls, func = qm.group(1), qm.group(2)
            else:
                im = INLINE_RE.search(sig)
                if not im:
                    unparsed.append((rel, i + 1, line.strip()))
                    continue
                cls, func = file_class, im.group(1)

            # Reject control-flow keywords mis-grabbed as a function name.
            if func in CPP_KEYWORDS:
                filtered.append((rel, i + 1, line.strip()))
                continue

            addr_norm = f"{addr_int:08x}"
            address_map[addr_norm] = {
                "name": func,
                "class": cls,
                "full_name": f"{cls}::{func}",
                "file": rel,
            }

    return address_map, unparsed, filtered


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default=r"C:\iwd2-re\src", help="source tree to scan")
    ap.add_argument("--out", default=r"C:\iwd2-re\.ghidra-exports\address_map.json",
                    help="output address_map.json path")
    ap.add_argument("--hooks-csv", default=r"C:\iwd2-re\.ghidra-exports\hooks.csv",
                    help="also emit a re-agent hooks CSV (address,name,reversed)")
    ap.add_argument("--show-unparsed", type=int, default=10,
                    help="print up to N unparsed // 0xADDR markers (0 to suppress)")
    args = ap.parse_args()

    source_root = Path(args.source_root)
    if not source_root.is_dir():
        print(f"ERROR: source root not found: {source_root}", file=sys.stderr)
        return 1

    address_map, unparsed, filtered = build_map(source_root)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(address_map, indent=2), encoding="utf-8")

    total_markers = len(address_map) + len(unparsed) + len(filtered)
    print(f"Scanned {source_root}")
    print(f"  // 0xADDR markers: {total_markers}")
    print(f"  mapped:            {len(address_map)}")
    print(f"  filtered (non-fn): {len(filtered)}")
    print(f"  unparsed:          {len(unparsed)}")
    print(f"Saved {len(address_map)} entries to {out}")

    # re-agent hooks CSV: cmd_parity loads this so each address carries its
    # (class, fn_name); run_parity then finds the body via the generic
    # Class::Func token scan (no hook macro needed). HOOK_ADDR_RE wants `0x`.
    hooks_csv = Path(args.hooks_csv)
    hooks_csv.parent.mkdir(parents=True, exist_ok=True)
    with hooks_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["address", "name", "reversed"])
        for addr8, info in sorted(address_map.items()):
            w.writerow([f"0x{addr8}", info["full_name"], 1])
    print(f"Saved {len(address_map)} hooks to {hooks_csv}")

    if unparsed and args.show_unparsed:
        print(f"\nUnparsed markers (first {args.show_unparsed}):")
        for rel, line_no, comment in unparsed[:args.show_unparsed]:
            print(f"  {rel}:{line_no}  {comment}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
