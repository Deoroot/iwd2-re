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

Installed at `C:\ghidra-mcp`. Base: `http://127.0.0.1:8089`. Schema: `/mcp/schema`. Run Ghidra GUI → plugin auto-starts.
Mutations in-memory → persist with `save_program`.
`__thiscall` `this` locked → document type via `batch_set_comments` plate comment.
Virtual functions: if address not in `funcs` table, check vtable `DATA` xref → that's entry point.

**Read bytes from EXE (use pefile, not `/memory_bytes`):**
```python
import pefile; pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True); print(pe.get_data(0x8ABCA4 - pe.OPTIONAL_HEADER.ImageBase, 16))
```

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
```bash
# Kill running instance
taskkill //f //im iwd2-re.exe 2>/dev/null || true

# Build
cmake --build build --config Debug

# Deploy
cp "build/Debug/iwd2-re.exe" "C:/GOG Games/Icewind Dale 2/" -f

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
the codebase — this includes BOTH `src/` AND `refs/gemrb/`.** The
graph is faster, cheaper (fewer tokens), and gives you structural
context (callers, dependents, test coverage) that file scanning cannot.

- Main graph (`src/`): default `repo_root` (auto-detected). Alias `iwd2`.
- GemRB graph: `repo_root="C:\iwd2-re\refs\gemrb"`. Alias `gemrb`.
- Both: use `cross_repo_search_tool` to search both graphs simultaneously.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes` or `query_graph` instead of Grep
- **Exploring gemrb**: same tools, pass `repo_root="C:\iwd2-re\refs\gemrb"`
- **Cross-reference gemrb ↔ IWD2**: `cross_repo_search_tool`
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
