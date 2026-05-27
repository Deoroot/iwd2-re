# CLAUDE.md

## Repo

Community RE of **Icewind Dale 2** (2002). `src/` = hand-recovered C++ matching `IWD2.exe`. Ghidra = truth; source = translation. See `README.md`, `ARCHITECTURE.md`.

## code-review-graph MCP — MANDATORY for `src/` or `refs/gemrb/` lookups

**RULE: searching `src/` or `refs/gemrb/` → ALWAYS use code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore the codebase **

- iwd2-re graph (`src/`): default `repo_root`. Alias `iwd2`.
- GemRB graph: `repo_root="C:\iwd2-re\refs\gemrb"`. Alias `gemrb`.
- Both: `cross_repo_search_tool` searches both simultaneously.

Allowed without graph: `tmp_*.txt`, `data/`, `scripts/`, ghidra curl, raw binaries.

| Want | Tool |
|------|------|
| Find fn/class by name | `semantic_search_nodes` |
| Callers of X | `query_graph pattern=callers_of target=X` |
| Callees of X | `query_graph pattern=callees_of target=X` |
| File contents tree | `query_graph pattern=file_summary target=path` |
| Blast radius | `get_impact_radius` |
| Exec paths hit | `get_affected_flows` |
| Free traversal | `traverse_graph` |
| Quick repo stats | `get_minimal_context` |
| Review diff | `detect_changes` → `get_affected_flows` |
| GemRB lookup | same tools + `repo_root="C:\iwd2-re\refs\gemrb"` |
| Cross-repo search | `cross_repo_search_tool` |

Workflow: `semantic_search_nodes` → `query_graph callers/callees` → Read only specific lines from result.

**Query phrasing:** `semantic_search_nodes` FTS5 hybrid. Use ONE bare identifier token (`LevelUp`); retry shorter token before Grep.

## Build & run (Win32, VS 2019)

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32
taskkill //f //im iwd2-re.exe 2>/dev/null || true
cmake --build build --config Debug
rm -f "C:/GOG Games/Icewind Dale 2/iwd2-re-crash.log" "C:/GOG Games/Icewind Dale 2/iwd2-re-debug.log" && cp "C:/iwd2-re/build/Debug/iwd2-re.exe" "C:/GOG Games/Icewind Dale 2/" -f
python scripts/auto_start_game.py   # smoke: load slot 0, wait for world
python scripts/auto_start_game.py --new-game
```

Every `src/` commit must compile clean VS2019 Win32. Rename field/fn → update `.h` + ALL `.cpp` in ONE atomic commit (`rg "oldName" src/`). Build fail → report first error, stop.

## Ghidra = truth

**RULE: DON'T INVENT CODE. ALWAYS check Ghidra before any code edit.**
`// 0xADDR` comments can be stale. Ghidra wins when conflicting. Verify address before reasoning. If address not in Ghidra `funcs` table → check vtable `DATA` xref (likely virtual method). See `memory/feedback_ghidra_truth.md`.

## GhidraMCP

`curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x0084c44c&limit=5"`.
Mutations in-memory → persist with `save_program`.
`__thiscall` `this` locked → document type via `batch_set_comments` plate comment.

**Read PE bytes (use pefile, not `/memory_bytes`):**
```python
import pefile
pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True)
ib = pe.OPTIONAL_HEADER.ImageBase
print(pe.get_data(0x8ABCA4 - ib, 16))
```

**Schema reference:** `ghidra_mcp_schema.json`.

## Game assets (`data/near_infinity_export/`)

Extracted via NearInfinity. Use for game content questions (action IDs, spell IDs, scripts, UI layouts).

```
data/near_infinity_export/
├── 2DA/         rule tables (QSLOTS, XPLEVEL, ANIMATE, etc.)
├── ARE/         areas — actors, doors, triggers, spawns
├── BAM_DECOMP/  decompressed BAMv1 (read frames with struct.unpack)
├── BCS/         creature scripts (exported as .BAF)
├── BS/          player scripts (.BAF)
├── CHU/         UI panels (button frames, control ids)
├── CRE/         creature templates
├── DLG/         dialogue trees
├── EFF/         standalone effects
├── IDS/         symbol→id maps (ACTION, TRIGGER, OBJECT, STATS, SPLSTATE)
├── ITM/         items
├── SPL/         spells
├── STO/         stores
├── WED/         tile maps
└── WMP/         world map
```

| Question | File |
|---|---|
| Action id N? | `IDS/ACTION.IDS` |
| Trigger id N? | `IDS/TRIGGER.IDS` |
| Stat/state name? | `IDS/STATS.IDS`, `IDS/SPLSTATE.IDS` |
| Spell SPxxx? | `SPL/SPxxx.SPL` |
| Button slot? | `CHU/<panel>.CHU` |
| QSlot layout? | `2DA/QSLOTS.2DA` |
| Area edges? | `WMP/WORLDMAP.WMP` |
| Script usage? | grep `BCS/*.BAF` + `BS/*.BAF` |

## External refs

| Path | Use |
|-----|-----|
| `data/pdb/bg2_pdb_types.txt` | BG2EE PDB layouts (field names match, offsets differ) |
| `refs/gemrb/` | GemRB source  → use CRG graph (`repo_root="C:\iwd2-re\refs\gemrb"`) |
| `refs/NearInfinity/` | File formats (.CRE/.ARE/.ITM) |
| `refs/iesdp/` | Effects, opcodes, STATS.IDS |
| `C:\ghidra_projects\IWD2\` | Live Ghidra project |

## Temp files

`tmp_*.txt`, `tmp_*.json`, `chunk_*.sql` = RE session noise. Not tracked. Delete freely.

## Code changes

- Verify `// 0xADDR` against Ghidra before touching.
- Minimal diffs. One bug = one change. No refactor in bugfix commits.
- Prefer named constants over magic numbers when defined in file.
