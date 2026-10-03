#!/usr/bin/env python3
"""Remove the Ghidra functions that `create-functions` made out of body notes.

Until s54, `reagent_address_map.py` took every `// 0xADDR` line as a function
marker, including the indented ones inside a body -- `// 0x45395C: IsScriptName`
on a switch arm of `CGameAIBase::EvaluateStatusTrigger`.  The map named each one
after the next identifier in sight (`CAITrigger cause(trigger);` ->
`CGameAIBase::cause`) and `gb export create-functions` then CREATED a Ghidra
function at the address.  152 such phantoms ended up in the project: in the
export, in `_index.json`, in the function totals, under `sym.py whatis` /
`addr2fn` as if they were real starts, and confirmed by `lint_address_markers`,
which checks markers against that same index.

`reagent_address_map.is_body_note` now keeps them out of the map.  This tool
takes the ones already in the project back out:

  candidate   an address whose ONLY `// 0xADDR` lines in src/ are body notes
              (an address that also has a real marker is left alone),
  and         Ghidra's function there carries a name the map gave it -- not a
              `FUN_` name, which means Ghidra found the function on its own
              and the body note merely cites it (0x48C310, 0x71C0C0, ...).

Usage:
  .venv-reagent/Scripts/python.exe scripts/ghidra_remove_body_notes.py          # dry run
  .venv-reagent/Scripts/python.exe scripts/ghidra_remove_body_notes.py --apply  # Ghidra + exports

`--apply` needs the Ghidra GUI CLOSED (it opens the project headless, like
`gb export`), removes each function, then deletes its `.ghidra-exports/<addr>.json`
and its `_index.json` entry so no full re-export is needed.  Back the project up
first: removing a function is not undone by re-running anything here.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))

from reagent_address_map import (  # noqa: E402
    ADDR_RE,
    CODE_MAX,
    CODE_MIN,
    FILE_EXTENSIONS,
    build_map,
    is_body_note,
)

EXPORTS = REPO / ".ghidra-exports"
INDEX = EXPORTS / "_index.json"
CONFIG = REPO / "ghidra-bridge.host.yaml"


def body_note_addresses(source_root: Path) -> set[int]:
    notes: set[int] = set()
    markers: set[int] = set()
    for path in sorted(source_root.rglob("*")):
        if path.suffix.lower() not in FILE_EXTENSIONS or not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            m = ADDR_RE.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            if not (CODE_MIN <= addr <= CODE_MAX):
                continue
            (notes if is_body_note(path, line) else markers).add(addr)
    mapped = {int(k, 16) for k in build_map(source_root)[0]}
    return notes - markers - mapped


def phantoms() -> list[tuple[int, str]]:
    index = json.loads(INDEX.read_text(encoding="utf-8"))
    out = []
    for addr in sorted(body_note_addresses(REPO / "src")):
        entry = index.get(f"{addr:08x}")
        if entry is None:
            continue
        name = entry.get("name") or ""
        if name.startswith("FUN_"):
            continue
        out.append((addr, name))
    return out


def ghidra_config() -> dict:
    import yaml

    cfg = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))
    return cfg["ghidra"]


def remove_in_ghidra(targets: list[tuple[int, str]]) -> list[int]:
    import pyghidra

    g = ghidra_config()
    os.environ["GHIDRA_INSTALL_DIR"] = g["install_dir"]
    pyghidra.start()
    removed = []
    with pyghidra.open_program(
        None,
        project_location=g["project_dir"],
        project_name=g["project_name"],
        program_name=g["program_name"],
        analyze=False,
        nested_project_location=False,
    ) as flat_api:
        program = flat_api.getCurrentProgram()
        fm = program.getFunctionManager()
        factory = program.getAddressFactory()
        for addr, name in targets:
            func = fm.getFunctionAt(factory.getAddress(f"0x{addr:08x}"))
            if func is None:
                print(f"  0x{addr:08X}  no function there any more")
                continue
            # createFunction was handed the map's full "Class::Func" and stores
            # it as one flat name; accept the bare last component as well.
            if func.getName() not in (name, name.split("::")[-1]):
                print(f"  0x{addr:08X}  SKIP: Ghidra calls it {func.getName()!r}, index {name!r}")
                continue
            if fm.removeFunction(func.getEntryPoint()):
                removed.append(addr)
            else:
                print(f"  0x{addr:08X}  removeFunction refused")
    return removed


def live_phantoms() -> list[tuple[int, str]]:
    """The same question asked of the PROJECT rather than the export index --
    the proof that an --apply was saved, since --apply prunes the index too."""
    import pyghidra

    g = ghidra_config()
    os.environ["GHIDRA_INSTALL_DIR"] = g["install_dir"]
    pyghidra.start()
    out = []
    with pyghidra.open_program(
        None,
        project_location=g["project_dir"],
        project_name=g["project_name"],
        program_name=g["program_name"],
        analyze=False,
        nested_project_location=False,
    ) as flat_api:
        program = flat_api.getCurrentProgram()
        fm = program.getFunctionManager()
        factory = program.getAddressFactory()
        for addr in sorted(body_note_addresses(REPO / "src")):
            func = fm.getFunctionAt(factory.getAddress(f"0x{addr:08x}"))
            if func is not None and not func.getName().startswith("FUN_"):
                out.append((addr, func.getName()))
    return out


def prune_exports(removed: list[int]) -> None:
    index = json.loads(INDEX.read_text(encoding="utf-8"))
    for addr in removed:
        key = f"{addr:08x}"
        index.pop(key, None)
        p = EXPORTS / f"{key}.json"
        if p.exists():
            p.unlink()
    INDEX.write_text(json.dumps(index, indent=2), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--apply", action="store_true",
                    help="remove them from the Ghidra project and the exports")
    ap.add_argument("--live", action="store_true",
                    help="dry run against the Ghidra project itself (boots PyGhidra), "
                         "not the export index")
    args = ap.parse_args()

    targets = live_phantoms() if args.live else phantoms()
    for addr, name in targets:
        print(f"  0x{addr:08X}  {name}")
    print(f"{len(targets)} Ghidra function(s) created from body notes")
    if not args.apply or not targets:
        return 0

    removed = remove_in_ghidra(targets)
    prune_exports(removed)
    print(f"removed {len(removed)} from Ghidra and from .ghidra-exports/")
    return 0 if len(removed) == len(targets) else 1


if __name__ == "__main__":
    sys.exit(main())
