# Agent Rules

Repo: `github.com/WillScarlettOhara/iwd2-re` (fork of alexbatalov)
Ghidra = source of truth. `// 0xADDR` comment can be stale. Ghidra wins when source disagrees.

## Build
- `src/` commit must compile clean on VS2019 Win32.
- Rename field/function → update `.h` + ALL `.cpp` in ONE commit. `rg "oldName" src/`
- Rename function → Ghidra first, save, then sync source.
- Danger files: `FileFormat.h`, `BalDataTypes.h`, `CChitin.h`, `CGameSprite.h`.
- Never prefix `field_XXX` with type letter.
- Broken? `git diff upstream/main -- src/`
- Diffs minimal. One bug = one change. No refactor in bugfix commits.

## Naming
- Functions: `sub_NNNNNN` → `ClassName::method`
- Fields: `field_XX` → `m_camelCase` (Hungarian)
- Address comment above each function: `// 0x7D14F0`
- Virtual function: `// 0x7D14F0 (virtual)`

## GhidraMCP (`:8089`)

Installed at `C:\ghidra-mcp`. Run Ghidra GUI → plugin auto-starts.

**Sanity:**
```bash
curl -s http://127.0.0.1:8089/check_connection
curl -s "http://127.0.0.1:8089/get_metadata"
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
curl -s -X POST http://127.0.0.1:8089/rename_function_by_address -H "Content-Type: application/json" -d '{"function_address":"0xNNNNNN","new_name":"Class::Method"}'
curl -s -X POST http://127.0.0.1:8089/set_function_prototype -H "Content-Type: application/json" -d '{"function_address":"0x6CA830","prototype":"void __thiscall EquipOffHWeapon(void * resRef, byte * colorRangeValues)"}'
```

**Read bytes from EXE (use pefile, not `/memory_bytes`):**
```python
import pefile; pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True); print(pe.get_data(0x8ABCA4 - pe.OPTIONAL_HEADER.ImageBase, 16))
```

Mutations in-memory until saved. Persist:
```bash
curl -s -X POST http://127.0.0.1:8089/save_program -H "Content-Type: application/json" -d '{"program":"IWD2.exe"}'
```

Schema reference: `ghidra_mcp_schema.json` (committed copy).

**Virtual functions:** if address not in `funcs` table, check vtable `DATA` xref → that's entry point.

## Rename Workflow

**Functions:**
1. `curl` rename via GhidraMCP
2. Save Ghidra project
3. Sync source in one commit

**Fields:**
1. Match class in `bg2_pdb_types.txt` (BG2EE offsets differ, names match)
2. Rename in `.h` + all `.cpp` in ONE commit
3. Build before push

## Build & Run
```powershell
# Kill running instance (PowerShell)
Get-Process | Where-Object { $_.ProcessName -like '*iwd2*' -or $_.Path -like '*iwd2-re.exe' } | Stop-Process -Force

# Build
cmake --build build --config Debug

# Deploy
Copy-Item build/Debug/iwd2-re.exe "C:\GOG Games\Icewind Dale 2\" -Force

# Smoke test: load visible save slot 0 and wait for world engine
python scripts/auto_start_game.py

# New-game smoke test with Party.ini group
python scripts/auto_start_game.py --new-game --party "Lady's Lament"
```

## Refs
| Path | Use |
|------|-----|
| `data/pdb/bg2_pdb_types.txt` | BG2EE PDB layouts |
| `refs/gemrb/` | GemRB source |
| `refs/NearInfinity/` | File formats |
| `refs/iesdp/` | Effects, opcodes |
| `data/near_infinity_export/` | Game assets (BAM/CHU/ITM/CRE/ARE/2DA) |
| `C:/ghidra-mcp` | GhidraMCP |

## Naming Rules
- Rename only when source matches Ghidra or decomp clear
- Guesses → `FUN_` + `Analysis` bookmark
- `curl -s "http://127.0.0.1:8089/search_functions?name_pattern=FUN_*&limit=50"`
- Prefer named constants over magic numbers when defined in file

## Temp Files
`tmp_*.txt`, `tmp_*.json`, `chunk_*.sql` = RE session noise. Not tracked. Delete freely.

## Milestone
Phase 2: name remaining `sub_` (~200) + `field_` (~640). Small classes first.

<!-- code-review-graph MCP tools -->
## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes` or `query_graph` instead of Grep
- **Understanding impact**: `get_impact_radius` instead of manually tracing imports
- **Code review**: `detect_changes` + `get_review_context` instead of reading entire files
- **Finding relationships**: `query_graph` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview` + `list_communities`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context` | Need source snippets for review — token-efficient |
| `get_impact_radius` | Understanding blast radius of a change |
| `get_affected_flows` | Finding which execution paths are impacted |
| `query_graph` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes` | Find fn/class. ⚠️ 1 token max |
| `get_architecture_overview` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes` for code review.
3. Use `get_affected_flows` to understand impact.
4. Use `query_graph` pattern="tests_for" to check coverage.
