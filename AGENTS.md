# iwd2-re Agent Rules

Repo: `github.com/WillScarlettOhara/iwd2-re` (fork of alexbatalov)

## Build Safety — ZERO TOLERANCE
- Every `src/` commit must compile clean on VS2019 Win32.
- Rename field → update `.h` + ALL `.cpp` refs in ONE commit. `rg "oldName" src/`
- Rename function → Ghidra DB FIRST, save, then sync `.h` + `.cpp` + callsites atomically.
- Danger zone: `FileFormat.h`, `BalDataTypes.h`, `CChitin.h`, `CGameSprite.h`.
- Never prefix `field_XXX` with type letters (`nm_`, etc.).
- Break? `git diff upstream/main -- src/` or `git checkout upstream/main -- file`.

## Naming
- Functions: `sub_NNNNNN` placeholder → `ClassName::methodName`.
- Fields: `field_XX` placeholder → `m_typeName` (Hungarian, no type prefix on placeholder).
- Addresses: `// 0x7D14F0` above each function.
- If function is virtual, mark address comment as virtual, e.g. `// 0x7D14F0 (virtual)`, so agents know Ghidra direct decompile by concrete address may not find an owning thunk/body.

## Rename Workflow (functions)
1. Rename via GhidraMCP: `curl -X POST http://127.0.0.1:8089/rename_function_by_address ...`
2. Save Ghidra project (File → Save in GUI, or autosave on headless shutdown).
3. Sync to C++ source in one commit.

## Rename Workflow (fields)
1. Match class in `bg2_pdb_types.txt` (BG2EE PDB offsets differ, names match).
2. Rename `field_XX` in `.h` + all `.cpp` in ONE commit.
3. Build before push.

## GhidraMCP (preferred — replaces GhidraSQL)

GhidraMCP is installed at `C:\ghidra-mcp`. Exposes ~195 REST endpoints at **`http://127.0.0.1:8089`**. Stable under load — no `query_worker_busy` stalls.

### Launch (GUI mode — recommended)
```powershell
# One-time install + first launch
cd C:\ghidra-mcp
python -m tools.setup deploy --ghidra-path "C:\ghidra_dist\ghidra_12.0.4_PUBLIC"

# Subsequent launches: open Ghidra, load the IWD2 project
& "C:\ghidra_dist\ghidra_12.0.4_PUBLIC\ghidraRun.bat"
```

### Sanity gate
```bash
curl -s http://127.0.0.1:8089/check_connection
# → Connected: GhidraMCP plugin running with program 'IWD2.exe'
curl -s http://127.0.0.1:8089/get_metadata
```

### Key Commands
```bash
# Decompile
curl -s "http://127.0.0.1:8089/decompile_function?address=0x6C9A50"

# Disassemble function
curl -s "http://127.0.0.1:8089/disassemble_function?address=0x6CA830"

# Disassemble byte range
curl -s -X POST http://127.0.0.1:8089/disassemble_bytes \
  -H "Content-Type: application/json" \
  -d '{"start_address":"0x44CBC0","length":24}'

# Search by name pattern
curl -s "http://127.0.0.1:8089/search_functions?name_pattern=CanSaveGame&limit=50"

# Xrefs to / from
curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x44CBC0&limit=20"
curl -s "http://127.0.0.1:8089/get_xrefs_from?address=0x6C90B9&limit=10"

# Rename function
curl -s -X POST http://127.0.0.1:8089/rename_function_by_address \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x6C8390","new_name":"CGameAnimationTypeCharacter::EquipWeapon"}'

# Set function prototype
curl -s -X POST http://127.0.0.1:8089/set_function_prototype \
  -H "Content-Type: application/json" \
  -d '{"function_address":"0x6CA830","prototype":"void __thiscall EquipOffHWeapon(void * resRef, byte * colorRangeValues)"}'

# Read raw bytes (wide ranges) — use pefile instead of /memory_bytes
python -c "import pefile; pe = pefile.PE(r'C:\GOG Games\Icewind Dale 2\IWD2.exe', fast_load=True); print(pe.get_data(0x8ABCA4 - pe.OPTIONAL_HEADER.ImageBase, 16))"
```

Mutations commit immediately to the running Ghidra project. Save the project after batches of renames.

### Legacy: GhidraSQL (`:8081`)
The old GhidraSQL workflow is deprecated — it suffers `query_worker_busy` stalls on `memory_bytes` / wide `instructions` ranges and locks the project. Only fall back to it for SQL joins across tables that GhidraMCP doesn't expose. Same project dir, same `.lock` files — never run both at once.

### After session
- **GUI mode**: File → Save in Ghidra
- **After session backup**: `bash scripts/ghidra_save.sh` — zip backup + commit to repo

## Reference Paths
- PDB types: `C:/projects/bg2-symbols/bg2_pdb_types.txt`
- GemRB: `C:/projects/gemrb/`
- NearInfinity: `C:/projects/NearInfinity/`
- IESDP: `C:/projects/iesdp/`
- GhidraMCP: `C:/ghidra-mcp`

## Naming Rules
- Rename only: source code match OR decomp unambiguous clarity
- Speculative → `FUN_` + bookmark with `Analysis` category
- Search: `SELECT name, printf('0x%X', address) FROM funcs WHERE name LIKE 'FUN_%' AND address >= 0x... AND size >= 50;`

## Current Milestone
Phase 2: name remaining `sub_` (~200) and `field_` (~640). Priority: small classes first, `CGameSprite` last.
