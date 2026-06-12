#!/usr/bin/env python3
"""Audit NewDiscovered.h against categorized C++ definitions.

By default, report addresses that still appear in NewDiscovered.h even though
they have a definition in src/*.cpp. Use --prune to remove those stale entries
and refresh the function count in the header.
"""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from pathlib import Path

import src_find


REPO = Path(__file__).resolve().parents[1]
DEFAULT_HEADER = REPO / "src" / "NewDiscovered.h"
ENTRY_RE = re.compile(r"//\s*(0x[0-9A-Fa-f]{5,8})\s*$")
COUNT_RE = re.compile(r"^//\s*\d+\s+functions\s*$")


def find_source_definitions() -> dict[int, list[dict]]:
    """Return categorized .cpp function definitions indexed by address."""
    definitions: dict[int, list[dict]] = defaultdict(list)
    cache = src_find.load_index(force=True)

    for symbol in src_find.all_syms(cache):
        if symbol["kind"] != "fn" or not symbol["addr"]:
            continue
        if not symbol["file"].endswith(".cpp"):
            continue
        definitions[int(symbol["addr"], 16)].append(symbol)

    return definitions


def find_stale_entries(
    header: Path,
    definitions: dict[int, list[dict]],
) -> list[tuple[int, int, str, list[dict]]]:
    """Return (line number, address, declaration, definitions) tuples."""
    stale = []
    for line_number, line in enumerate(
        header.read_text(encoding="utf-8").splitlines(), start=1
    ):
        match = ENTRY_RE.search(line)
        if not match:
            continue
        address = int(match.group(1), 16)
        if address in definitions:
            stale.append((line_number, address, line.strip(), definitions[address]))
    return stale


def prune_header(
    header: Path,
    definitions: dict[int, list[dict]],
) -> list[tuple[int, int, str, list[dict]]]:
    """Remove categorized entries and update the header's function count."""
    stale = find_stale_entries(header, definitions)
    if not stale:
        return []

    stale_addresses = {entry[1] for entry in stale}
    lines = header.read_text(encoding="utf-8").splitlines()
    kept = []
    entry_count = 0

    for line in lines:
        match = ENTRY_RE.search(line)
        if match and int(match.group(1), 16) in stale_addresses:
            continue
        if match:
            entry_count += 1
        kept.append(line)

    for index, line in enumerate(kept):
        if COUNT_RE.match(line):
            kept[index] = f"// {entry_count} functions"
            break

    header.write_text("\n".join(kept) + "\n", encoding="utf-8")
    return stale


def describe_symbol(symbol: dict) -> str:
    name = src_find.fullname(symbol)
    return f"{name} ({symbol['file']}:{symbol['line']})"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--prune", action="store_true")
    args = parser.parse_args()

    definitions = find_source_definitions()
    stale = (
        prune_header(args.header, definitions)
        if args.prune
        else find_stale_entries(args.header, definitions)
    )

    if not stale:
        print(f"{args.header}: no categorized addresses remain")
        return 0

    action = "Removed" if args.prune else "Found"
    print(f"{action} {len(stale)} categorized address(es):")
    for line_number, address, declaration, symbols in stale:
        targets = ", ".join(describe_symbol(symbol) for symbol in symbols)
        print(f"  0x{address:06X} line {line_number}: {declaration} -> {targets}")

    return 0 if args.prune else 1


if __name__ == "__main__":
    raise SystemExit(main())
