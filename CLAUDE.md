# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A community reverse engineering of **Icewind Dale 2** (2002). `src/` contains hand-recovered C++ matching the original `IWD2.exe`. Ghidra holds the authoritative analysis of the binary; the C++ tree is a translation of that analysis. See `README.md` and `ARCHITECTURE.md` for the broader picture.

## Build & run (Windows, Win32, VS 2019/2022, MFC)

```powershell
# Configure once
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32

# Rebuild after edits
cmake --build build --config Debug

# Run (must be inside the IWD2 install dir for chitin.key + assets)
cp build/Debug/iwd2-re.exe "C:\GOG Games\Icewind Dale 2\"
& "C:\GOG Games\Icewind Dale 2\iwd2-re.exe"

# Driver that launches the game and clicks Load + first save (smoke test)
python scripts/click_load_original.py
```

Build safety is non-negotiable: every `src/` commit must compile clean on VS 2019 Win32. Rename a field/function → update header + every `.cpp` referent in **one** atomic commit (`rg "oldName" src/`).

## Ghidra is the source of truth

The `// 0xADDR` comment above each function is best-effort and **can be stale or wrong**. When source and Ghidra disagree, Ghidra wins. Always re-derive the real address from Ghidra before reasoning about binary behavior. See `memory/feedback_ghidra_truth.md` for the incident that taught us this.

If a function's `// 0xADDR` isn't found in Ghidra's `funcs` table, the function may be a virtual method that Ghidra never auto-detected — look for a `DATA` xref from a vtable pointing at that address to confirm the entry point exists, then probe instructions directly.

## GhidraMCP workflow (preferred — replaces GhidraSQL)

GhidraMCP is installed at `C:\ghidra-mcp`. It exposes ~195 REST endpoints from a Ghidra plugin (or standalone headless server) at **`http://127.0.0.1:8089`** and a thin MCP bridge for AI clients. Stable under load — no `query_worker_busy` stalls.

### Launch

GUI mode (recommended — full plugin set, lock-free reattach to the saved project):

```powershell
# One-time install + first launch
cd C:\ghidra-mcp
python -m tools.setup deploy --ghidra-path "C:\ghidra_dist\ghidra_12.0.4_PUBLIC"

# Subsequent launches: just open Ghidra and load the IWD2 project
& "C:\ghidra_dist\ghidra_12.0.4_PUBLIC\ghidraRun.bat"
```

Headless server alternative (no GUI; uses the existing saved project at `C:\ghidra_projects\IWD2`):

```bash
java -Xmx4g -Dghidra.home="C:/ghidra_dist/ghidra_12.0.4_PUBLIC" \
     -Dapplication.name=GhidraMCP \
     -classpath "C:/ghidra-mcp/target/GhidraMCP-5.10.0.jar;<Ghidra/Framework/*/lib/*.jar>;<Ghidra/Features/*/lib/*.jar>;..." \
     com.xebyte.headless.GhidraMCPHeadlessServer \
     --port 8089 --bind 127.0.0.1 --project "C:/ghidra_projects/IWD2"
```

### Sanity gate

```bash
curl -s http://127.0.0.1:8089/check_connection
# → Connected: GhidraMCP plugin running with program 'IWD2.exe'
curl -s http://127.0.0.1:8089/list_open_programs
curl -s http://127.0.0.1:8089/get_metadata
```

If the program isn't bound, open IWD2.exe in Ghidra (GUI) or pass `--project` to the headless server.

### Schema discovery

```bash
curl -s http://127.0.0.1:8089/mcp/schema -o tmp_schema.json
python -c "import json; d=json.load(open('tmp_schema.json')); print(len(d['tools']))"
# inspect a tool's params:
python -c "import json; d=json.load(open('tmp_schema.json')); print(next(t for t in d['tools'] if t['path']=='/set_function_prototype'))"
```

### Common queries

```bash
# Decompile
curl -s "http://127.0.0.1:8089/decompile_function?address=0x6C9A50"

# Disassemble whole function
curl -s "http://127.0.0.1:8089/disassemble_function?address=0x6CA830"

# Disassemble arbitrary byte range (POST + JSON body)
curl -s -X POST http://127.0.0.1:8089/disassemble_bytes \
  -H "Content-Type: application/json" \
  -d '{"start_address":"0x44CBC0","length":24}'

# Search by name pattern
curl -s "http://127.0.0.1:8089/search_functions?name_pattern=CanSaveGame&limit=50"

# Xrefs to / from
curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x44CBC0&limit=20"
curl -s "http://127.0.0.1:8089/get_xrefs_from?address=0x6C90B9&limit=10"

# Find instructions by mnemonic + operand
curl -s "http://127.0.0.1:8089/search_instructions?mnemonic=MOV&operand_pattern=0x4076&limit=20"
```

### Mutations (rename / prototype / labels)

```bash
# Rename function (accepts address or current name)
curl -s -X POST http://127.0.0.1:8089/rename_function_by_address \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x6C8390","new_name":"CGameAnimationTypeCharacter::EquipWeapon"}'

# Set prototype (use `void*` for CString-typed params — Ghidra can't always parse C++ types)
curl -s -X POST http://127.0.0.1:8089/set_function_prototype \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x6CA830","prototype":"void __thiscall EquipOffHWeapon(void * resRef, byte * colorRangeValues)"}'
```

Mutations commit immediately to the running Ghidra project. After a batch of renames, save the project (File → Save in GUI, or rely on autosave + `--shutdown save`).

### MCP bridge (for MCP-aware AI tools)

```bash
cd C:\ghidra-mcp
python bridge_mcp_ghidra.py            # stdio (default — for Claude Code, Continue, etc.)
python bridge_mcp_ghidra.py --transport sse --port 9090   # SSE for web clients
```

The bridge auto-connects to `127.0.0.1:8089` and registers all 195 tools. Wire it into `.mcp.json` to call endpoints directly as MCP tools rather than `curl`.

### Reading raw bytes from `IWD2.exe`

GhidraMCP's `/memory_bytes` is slow on wide ranges. For known addresses (e.g. extracting a string at a vtable slot), read directly from the on-disk PE with `pefile`:

```python
import pefile
pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True)
ib = pe.OPTIONAL_HEADER.ImageBase
print(pe.get_data(0x8ABCA4 - ib, 16))   # bytes at .rdata 0x8ABCA4
```

### Legacy: GhidraSQL (`:8081`)

The old GhidraSQL workflow is deprecated for everyday work — it suffers `query_worker_busy` stalls on `memory_bytes` / wide `instructions` ranges and locks the project. Only fall back to it for SQL joins across tables that GhidraMCP doesn't expose. Same project dir, same `.lock` files — never run both at once. See git history for the legacy `scripts/ghidra_re.py` patterns.

## Renaming workflow

Functions:

1. `UPDATE funcs SET name = 'NewName' WHERE address = 0xNNNNNN;`
2. `SELECT save_database();`
3. Sync to the matching `.h` + `.cpp` + every call site in one commit.

Fields (`field_XX` placeholders): match the class in `C:/projects/bg2-symbols/bg2_pdb_types.txt` (BG2EE PDB — offsets differ but names match), then update `.h` + every `.cpp` referent atomically and build before push.

Never prefix a placeholder `field_XXX` with a type letter (`nm_`, etc.). Speculative names go to `FUN_<addr>` plus a `bookmark category=Analysis` until verified.

## Reference paths (outside the repo)

| Path | Purpose |
|------|---------|
| `C:/projects/bg2-symbols/bg2_pdb_types.txt` | BG2EE PDB class layouts (field names match IWD2, offsets don't) |
| `C:/projects/gemrb/` | GemRB source — useful for CGameSprite ↔ Actor, CInfGame ↔ Game mappings |
| `C:/projects/NearInfinity/` | NearInfinity — IWD2 file formats (.CRE, .ARE, .ITM) |
| `C:/projects/iesdp/` | IESDP — IWD2 effects, opcodes, STATS.IDS |
| `C:\ghidra_projects\IWD2\` | Live Ghidra project |

## Notes on temp files and noise

The repo root accumulates `tmp_*.txt`, `tmp_*.json`, and `chunk_*.sql` from prior RE sessions. They are not tracked. Do not include them in commits; if they get in the way, just delete them.

## When making code changes

Don't touch a function's `// 0xADDR` comment unless you've verified the address against Ghidra. Don't refactor surrounding code while fixing a bug — keep diffs minimal so each change is auditable against the binary. Prefer named constants (`ITEM_FLAGS_TWOHANDED`) over magic numbers when they're already defined in the file.
