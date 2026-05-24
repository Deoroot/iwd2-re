# CLAUDE.md

## Repo

Community RE of **Icewind Dale 2** (2002). `src/` = hand-recovered C++ matching `IWD2.exe`. Ghidra = truth; source = translation. See `README.md`, `ARCHITECTURE.md`.

## code-review-graph MCP — MANDATORY for `src/` lookups

**RULE: searching `src/` → graph MCP FIRST. Grep/Glob/Read forbidden until graph returns 0 results.** Graph parsed src tree; structural + cheap. Grep over src/ wastes tokens + misses callers/callees.

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

Workflow: `semantic_search_nodes` → `query_graph callers/callees` → Read only specific lines from result.

**Query phrasing:** `semantic_search_nodes` matches against node **names** (identifiers), FTS5 hybrid. Use ONE bare identifier token (`StartDialog`), not multi-word natural-language phrases (`open dialog debug trace`) — extra words get keyword-ANDed and return 0 (`search_mode:"keyword"`). Vector/semantic ranking only kicks in after `embed_graph`; without it, it's pure keyword. 0 results ≠ absent — retry with a shorter/different identifier before falling back to Grep.

Violation flagged 2026-05-23. See `memory/feedback_graph_first.md`.

## Build & run (Win32, VS 2019)

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32
Get-Process -Name iwd2-re -ErrorAction SilentlyContinue | Stop-Process -Force
cmake --build build --config Debug
Copy-Item -Path "build/Debug/iwd2-re.exe" -Destination "C:\GOG Games\Icewind Dale 2\" -Force
& "C:\GOG Games\Icewind Dale 2\iwd2-re.exe"
python scripts/click_load_original.py   # smoke test
```

Every `src/` commit must compile clean VS2019 Win32. Rename field/fn → update `.h` + ALL `.cpp` in ONE atomic commit (`rg "oldName" src/`). Build fail → report first error, stop.

## Ghidra = truth

`// 0xADDR` comments can be stale. Ghidra wins when conflicting. Verify address before reasoning. If address not in Ghidra `funcs` table → check vtable `DATA` xref (likely virtual method). See `memory/feedback_ghidra_truth.md`.

## GhidraMCP (`http://127.0.0.1:8089`)

Install: `C:\ghidra-mcp`. Ghidra GUI opens → plugin auto-starts.

**Sanity:**
```bash
curl -s http://127.0.0.1:8089/check_connection
curl -s http://127.0.0.1:8089/get_metadata
```

**Schema:**
```bash
curl -s http://127.0.0.1:8089/mcp/schema -o ghidra_mcp_schema.json
python -c "import json; d=json.load(open('ghidra_mcp_schema.json')); print(len(d['tools']))"
```

**Decompile / disasm:**
```bash
curl -s "http://127.0.0.1:8089/decompile_function?address=0x6C9A50"
curl -s "http://127.0.0.1:8089/disassemble_function?address=0x6CA830"
curl -s -X POST http://127.0.0.1:8089/disassemble_bytes -H "Content-Type: application/json" -d '{"start_address":"0x44CBC0","length":24}'
```

**Search:**
```bash
curl -s "http://127.0.0.1:8089/search_functions?name_pattern=CanSaveGame&limit=50"
curl -s "http://127.0.0.1:8089/search_instructions?mnemonic=MOV&operand_pattern=0x4076&limit=20"
```

**Xrefs:**
```bash
curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x44CBC0&limit=20"
curl -s "http://127.0.0.1:8089/get_xrefs_from?address=0x6C90B9&limit=10"
```

**Rename / prototype:**
```bash
curl -s -X POST http://127.0.0.1:8089/rename_function_by_address -H "Content-Type: application/json" -d '{"function_address":"0x6C8390","new_name":"CGamAnimationTypeCharacter::EquipWeapon"}'
curl -s -X POST http://127.0.0.1:8089/set_function_prototype -H "Content-Type: application/json" -d '{"function_address":"0x6CA830","prototype":"void __thiscall EquipOffHWeapon(void * resRef, byte * colorRangeValues)"}'
```

Mutations are in-memory until saved. Persist with:
```bash
curl -s -X POST http://127.0.0.1:8089/save_program -H "Content-Type: application/json" -d '{"program":"IWD2.exe"}'
```

**MCP bridge:**
```bash
cd C:\ghidra-mcp && python bridge_mcp_ghidra.py   # stdio, registers 195 tools
```

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
| `refs/gemrb/` | GemRB source (CGameSprite↔Actor, CInfGame↔Game) |
| `refs/NearInfinity/` | File formats (.CRE/.ARE/.ITM) |
| `refs/iesdp/` | Effects, opcodes, STATS.IDS |
| `C:\ghidra_projects\IWD2\` | Live Ghidra project |

## Temp files

`tmp_*.txt`, `tmp_*.json`, `chunk_*.sql` = RE session noise. Not tracked. Delete freely.

## Code changes

- Verify `// 0xADDR` against Ghidra before touching.
- Minimal diffs. One bug = one change. No refactor in bugfix commits.
- Prefer named constants over magic numbers when defined in file.

