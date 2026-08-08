#!/usr/bin/env python3
"""Whole-codebase parity WITHOUT booting PyGhidra.

Live `re-agent parity` (no filter) boots PyGhidra per function for the asm
signals (~1 min each x 7000+ hooks = days). This sweep instead pre-loads each
function's GhidraData from the cached headless exports in `.ghidra-exports/`
(decompile only) and runs run_parity with no backend, so:

  - the asm-based signals self-skip (asm_ok stays False), and
  - the decompile-only signals run codebase-wide in seconds.

The headline beneficiary is `check_param_swap` (the "wrong argument fed to an
expression" signal), which needs only the decompiled body + the source
signature -- both available offline.

Run:  .venv-reagent/bin/python scripts/parity_cache_sweep.py [--filter REGEX] [--signal NAME]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "vendor" / "auto-re-agent" / "src"))

from re_agent.config.loader import load_config
from re_agent.core.models import GhidraData
from re_agent.parity.engine import read_hooks, run_parity
from re_agent.utils.address import normalize_address

EXPORTS = REPO / ".ghidra-exports"


def _export_path(address: str) -> Path:
    key = address.lower().removeprefix("0x").lstrip("0").rjust(8, "0")
    return EXPORTS / f"{key}.json"


def _ghidra_from_cache(address: str) -> GhidraData | None:
    p = _export_path(address)
    if not p.exists():
        return None
    try:
        d = json.loads(p.read_text(errors="replace"))
    except (ValueError, OSError):
        return None
    dec = d.get("decompiled") or ""

    def _count(v: object) -> int | None:
        return len(v) if isinstance(v, list) else (v if isinstance(v, int) else None)

    return GhidraData(
        decompile_ok=bool(dec),
        callers=_count(d.get("callers")),
        callees=_count(d.get("callees")),
        decompiled=dec,
        signature=d.get("signature") or "",
        resolved_address=address,
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=str(REPO / "re-agent.host.yaml"))
    ap.add_argument("--filter", default=None, help="regex on symbol/class")
    ap.add_argument("--signal", default=None, help="only show findings whose reason matches this regex")
    args = ap.parse_args()

    config = load_config(Path(args.config))
    source_root = Path(config.project_profile.source_root)
    hooks = read_hooks(Path(config.project_profile.hooks_csv))
    if args.filter:
        rx = re.compile(args.filter)
        hooks = [h for h in hooks if rx.search(h.symbol) or rx.search(h.class_path)]

    gmap = {}
    for h in hooks:
        g = _ghidra_from_cache(h.address)
        if g is not None:
            gmap[normalize_address(h.address)] = g

    results = run_parity(hooks, source_root, config, backend=None, ghidra_data_map=gmap)

    sig_rx = re.compile(args.signal, re.I) if args.signal else None
    n_yellow = n_red = 0
    hits: list[tuple[str, str, str]] = []
    for r in results:
        for f in r["findings"]:
            if f.level == "red":
                n_red += 1
            elif f.level == "yellow":
                n_yellow += 1
            else:
                continue
            if sig_rx and not sig_rx.search(f.reason):
                continue
            hits.append((f.level, r["hook"].symbol, f.reason))

    for level, sym, reason in sorted(hits):
        print(f"  [{level.upper():6}] {sym}\n           {reason}")
    print(
        f"\nSwept {len(hooks)} hooks ({len(gmap)} with cached decompile): "
        f"RED={n_red} YELLOW={n_yellow}"
        + (f"  (shown: signal~/{args.signal}/)" if args.signal else "")
    )
    # Gate semantics: RED is a real faithfulness break, YELLOW is advisory
    # (STL-inline call-count noise is the classic false positive).
    return 1 if n_red else 0


if __name__ == "__main__":
    raise SystemExit(main())
