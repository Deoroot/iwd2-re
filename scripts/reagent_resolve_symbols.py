#!/usr/bin/env python3
"""Resolve bare ``FUN_<addr>`` / ``DAT_<addr>`` / ``LAB_<addr>`` tokens in Ghidra
output to our recovered names.

Ghidra leaves a function/datum as ``FUN_0078abb0`` / ``DAT_008c5cf8`` when its own
DB has no symbol for that address. But *we* have often already recovered and named
that address in ``src/`` -- captured in ``address_map.json`` (built by
``reagent_address_map.py`` from our ``// 0xADDR`` markers). This pass injects those
ground-truth names back into the decompiler/disassembler text, turning::

    iVar1 = FUN_00403320(local_2c);

into::

    iVar1 = CResText::GetText(local_2c);

It is purely mechanical -- a dictionary lookup by address, no LLM, no guessing --
so it cannot hallucinate. Addresses with no recovered name are left untouched.

NOTE on BG2 PDB: the BG2EE PDB maps names by *its own* offsets, not IWD2
addresses, so it cannot resolve a bare ``FUN_<iwd2_addr>``. PDB-assisted naming is
a different (human-in-the-loop, by-class) step and is intentionally not done here.

Usage::

    reagent_resolve_symbols.py --address 0x402b70        # decompile via bridge, resolve
    reagent_resolve_symbols.py --input dump.asm          # resolve a file
    cat dump.c | reagent_resolve_symbols.py              # resolve stdin
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_MAP = r"C:\iwd2-re\.ghidra-exports\address_map.json"
DEFAULT_INDEX = r"C:\iwd2-re\.ghidra-exports\_index.json"
DEFAULT_GLOBALS = r"C:\iwd2-re\.ghidra-exports\_globals.json"
DEFAULT_BRIDGE = r"C:\iwd2-re\.venv-reagent\Scripts\ghidra-bridge.exe"
DEFAULT_CONFIG = r"C:\iwd2-re\ghidra-bridge.yaml"

# Ghidra synthetic symbols: FUN_ (function), DAT_ (datum), LAB_ (code label).
# Addresses are zero-padded hex; this binary's range needs 6-8 significant digits.
TOKEN_RE = re.compile(r"\b(FUN|DAT|LAB)_([0-9a-fA-F]{6,8})\b")

# A Ghidra "name" that is itself just a synthetic placeholder (FUN_x, DAT_x,
# LAB_x, PTR_DAT_x...) carries no information -- skip it so we never rewrite a
# token into another placeholder (e.g. DAT_008c1758 -> PTR_DAT_008c1758).
SYNTH_RE = re.compile(r"^(PTR_)?(FUN|LAB|DAT|UNK|SUB)_[0-9A-Fa-f]+$", re.IGNORECASE)


def _is_useful(name: str) -> bool:
    """True unless *name* is a bare Ghidra synthetic (FUN_x/DAT_x/PTR_DAT_x...)."""
    return bool(name) and SYNTH_RE.match(name) is None


def _ingest(path: Path, names: dict[int, str], key: str) -> None:
    """Merge ``{addr: info[key]}`` from a Ghidra export JSON, skipping synthetics."""
    if not path or not path.is_file():
        return
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return
    for addr_hex, info in raw.items():
        nm = info.get(key) if isinstance(info, dict) else None
        if not nm or not _is_useful(nm):
            continue
        try:
            names[int(addr_hex, 16)] = nm
        except ValueError:
            continue


def load_names(map_path: Path, index_path: Path | None, globals_path: Path | None) -> dict[int, str]:
    """Build ``{address_int: name}`` from all available sources.

    Priority (lowest first, so the last write wins): Ghidra ``_index.json``
    (function names) and ``_globals.json`` (data names) provide broad coverage
    for the ASM path; our ``address_map.json`` overrides them with the names *we*
    recovered (authoritative). For the decompiler path the Ghidra layers are
    effectively redundant -- the decompiler already substitutes every name Ghidra
    knows -- but they are essential when resolving raw disassembly.
    """
    names: dict[int, str] = {}
    _ingest(index_path, names, "name")
    _ingest(globals_path, names, "name")
    if map_path and map_path.is_file():
        raw = json.loads(map_path.read_text(encoding="utf-8"))
        for addr_hex, info in raw.items():
            full = info.get("full_name") if isinstance(info, dict) else None
            if full:
                try:
                    names[int(addr_hex, 16)] = full
                except ValueError:
                    continue
    return names


def resolve(text: str, addr_map: dict[int, str]) -> tuple[str, dict[str, str], list[str]]:
    """Rewrite FUN_/DAT_/LAB_ tokens to recovered names.

    Returns ``(rewritten_text, resolved {token: name}, unresolved [tokens])``.
    """
    resolved: dict[str, str] = {}
    unresolved: set[str] = set()

    def _sub(m: re.Match[str]) -> str:
        token = m.group(0)
        addr = int(m.group(2), 16)
        name = addr_map.get(addr)
        if name is None:
            # Only flag FUN_ as a meaningful gap; DAT_/LAB_ rarely live in our
            # (function-only) map, so do not treat them as misses.
            if m.group(1) == "FUN":
                unresolved.add(token)
            return token
        resolved[token] = name
        return name

    rewritten = TOKEN_RE.sub(_sub, text)
    return rewritten, resolved, sorted(unresolved)


def fetch_decompile(address: str, bridge: str, config: str) -> str:
    """Shell the ghidra-bridge to decompile *address* and return its text."""
    proc = subprocess.run(
        [bridge, "--config", config, "decompile", address],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout)
        raise SystemExit(f"ghidra-bridge decompile {address} failed (exit {proc.returncode})")
    return proc.stdout


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--address", help="decompile this address via ghidra-bridge, then resolve")
    src.add_argument("--input", type=Path, help="resolve this file (default: stdin)")
    ap.add_argument("--map", type=Path, default=Path(DEFAULT_MAP), help="our address_map.json (authoritative names)")
    ap.add_argument("--index", type=Path, default=Path(DEFAULT_INDEX), help="Ghidra _index.json (function names; for ASM)")
    ap.add_argument("--globals", dest="globals_", type=Path, default=Path(DEFAULT_GLOBALS), help="Ghidra _globals.json (data names; for ASM)")
    ap.add_argument("--no-ghidra-names", action="store_true", help="resolve only against our address_map (skip _index/_globals)")
    ap.add_argument("--bridge", default=DEFAULT_BRIDGE, help="ghidra-bridge executable")
    ap.add_argument("--config", default=DEFAULT_CONFIG, help="ghidra-bridge.yaml path")
    ap.add_argument("--summary", action="store_true", help="print a resolved/unresolved summary to stderr")
    args = ap.parse_args()

    if not args.map.is_file():
        sys.stderr.write(f"ERROR: address map not found: {args.map}\n")
        return 1
    index_path = None if args.no_ghidra_names else args.index
    globals_path = None if args.no_ghidra_names else args.globals_
    addr_map = load_names(args.map, index_path, globals_path)

    if args.address:
        text = fetch_decompile(args.address, args.bridge, args.config)
    elif args.input:
        text = args.input.read_text(encoding="utf-8", errors="replace")
    else:
        text = sys.stdin.read()

    rewritten, resolved, unresolved = resolve(text, addr_map)
    sys.stdout.write(rewritten)

    if args.summary or args.address:
        names = sorted(set(resolved.values()))
        sys.stderr.write(
            f"\n[resolve] {len(resolved)} token(s) -> {len(names)} name(s); "
            f"{len(unresolved)} FUN_ unresolved\n"
        )
        for token, name in sorted(resolved.items()):
            sys.stderr.write(f"  {token} -> {name}\n")
        if unresolved:
            sys.stderr.write(f"  unresolved FUN_: {', '.join(unresolved)}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
