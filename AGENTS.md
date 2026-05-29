# Agent Rules

Repo: `github.com/WillScarlettOhara/iwd2-re`. Ghidra = truth. `// 0xADDR` can be stale.

## Build & Run

```bash
# Kill + build + deploy + smoke
taskkill //f //im iwd2-re.exe 2>/dev/null || true
cmake --build build --config Debug
rm -f "C:/GOG Games/Icewind Dale 2/iwd2-re-crash.log" "C:/GOG Games/Icewind Dale 2/iwd2-re-debug.log" && cp "build/Debug/iwd2-re.exe" "C:/GOG Games/Icewind Dale 2/" -f
python scripts/auto_start_game.py
python scripts/auto_start_game.py --new-game --party "Lady's Lament"
```

Commits must compile VS2019 Win32. Rename → update `.h` + ALL `.cpp` in one commit. Build fail → stop.
Danger files: `FileFormat.h`, `BalDataTypes.h`, `CChitin.h`, `CGameSprite.h`.
Never prefix `field_XXX` with type letter.
Broken? `git diff upstream/main -- src/`.
Diffs minimal. One bug = one change. No refactor in bugfix commits.

## Naming

- Functions: `sub_NNNNNN` → `ClassName::method`
- Fields: `field_XX` → `m_camelCase` (Hungarian)
- Address comment above function: `// 0x7D14F0` / `// 0x7D14F0 (virtual)`
- Rename only when source matches Ghidra or decomp clear
- Guesses → `FUN_` + `Analysis` bookmark → `curl -s "http://127.0.0.1:8089/search_functions?name_pattern=FUN_*&limit=50"`
- Prefer named constants over magic numbers

## Rename Workflow

**Functions:**
1. `curl` rename via GhidraMCP
2. Save Ghidra project
3. Sync source in one commit

**Fields:**
1. Match class in `bg2_pdb_types.txt` (BG2EE offsets differ, names match)
2. Rename in `.h` + all `.cpp` in ONE commit
3. Build before push

## GhidraMCP (`:8089`)

Installed at `C:\ghidra-mcp`. Run Ghidra GUI → plugin auto-starts.
`curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x0084c44c&limit=5"`.
Mutations in-memory → persist with `save_program`.
`__thiscall` `this` locked → document type via `batch_set_comments` plate comment.
Virtual functions: if address not in `funcs` table, check vtable `DATA` xref → entry point.

**Read bytes from EXE (use pefile, not `/memory_bytes`):**
```python
import pefile; pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True); print(pe.get_data(0x8ABCA4 - pe.OPTIONAL_HEADER.ImageBase, 16))
```

## Frida tracing

Hook original `IWD2.exe` at runtime, diff against `Iwd2DebugLog`.
Docs: `docs/frida-differential-tracing.md`. Template: `scripts/frida_formation_trace.py`.
- `ptr(0xADDR)` absolute (no ASLR, ImageBase `0x400000`). `__thiscall`: `this` = `this.context.ecx`.
- Hook function ENTRIES only. Mid-function hooks crash.
- Runtime wins when decompiler disagrees on `__thiscall` args.
- Read PE constants with `pefile`, not memory endpoint.

## code-review-graph — use before Grep/Glob/Read

**RULE: searching `src/` or `refs/gemrb/` → graph tools first.**
**Query:** one bare identifier token. Retry shorter before Grep.
Allowed without graph: `tmp_*.txt`, `data/`, `scripts/`, ghidra curl, raw binaries.

- iwd2-re: default `repo_root`. Alias `iwd2`.
- GemRB: `repo_root="C:\iwd2-re\refs\gemrb"`. Alias `gemrb`.
- Both at once: `cross_repo_search_tool`.

| Want | Tool |
|------|------|
| Find fn/class | `semantic_search_nodes` |
| Callers of X | `query_graph pattern=callers_of target=X` |
| Callees of X | `query_graph pattern=callees_of target=X` |
| File contents | `query_graph pattern=file_summary target=path` |
| Blast radius | `get_impact_radius` |
| Exec paths hit | `get_affected_flows` |
| Free traversal | `traverse_graph` |
| Quick stats | `get_minimal_context` |
| Review diff | `detect_changes` → `get_affected_flows` |
| Test coverage | `query_graph pattern=tests_for` |

Workflow: `semantic_search_nodes` → `query_graph callers/callees` → Read only matched lines.

## Game assets

| Question | File |
|---|---|
| Action id N? | `data/near_infinity_export/IDS/ACTION.IDS` |
| Trigger id N? | `IDS/TRIGGER.IDS` |
| Stat/state name? | `IDS/STATS.IDS`, `IDS/SPLSTATE.IDS` |
| Spell SPxxx? | `SPL/SPxxx.SPL` |
| Button slot? | `CHU/<panel>.CHU` |
| QSlot layout? | `2DA/QSLOTS.2DA` |
| Area edges? | `WMP/WORLDMAP.WMP` |
| Script usage? | grep `BCS/*.BAF` + `BS/*.BAF` |

## Refs

| Path | Use |
|------|-----|
| `data/pdb/bg2_pdb_types.txt` | BG2EE PDB layouts |
| `refs/gemrb/` | GemRB source |
| `refs/NearInfinity/` | File formats (.CRE/.ARE/.ITM) |
| `refs/iesdp/` | Effects, opcodes |
| `data/near_infinity_export/` | Game assets (BAM/CHU/ITM/CRE/ARE/2DA) |
| `C:\ghidra_projects\IWD2\` | Live Ghidra project |

## Not tracked

`tmp_*.txt`, `tmp_*.json`, `chunk_*.sql` = RE session noise. Delete freely.

## No hacks

Code must match `IWD2.exe`. No invented behavior.
- Can't recover → leave unimplemented (return early / no-op). Missing better than wrong.
- Unavoidable hack → `// HACK: <what> — <why> — replaces 0x<addr>`. Not `// TODO`.

## Milestone

Phase 2: name remaining `sub_` (~200) + `field_` (~640). Small classes first.
