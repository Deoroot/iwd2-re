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
curl -s http://127.0.0.1:8089/mcp/schema -o tmp_schema.json
python -c "import json; d=json.load(open('tmp_schema.json')); print(len(d['tools']))"
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

Mutations commit immediately. Save project after rename batches. `bash scripts/ghidra_save.sh` for zip backup.

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

# Smoke test
python scripts/click_load_original.py
```

## Refs
| Path | Use |
|------|-----|
| `C:/projects/bg2-symbols/bg2_pdb_types.txt` | BG2EE PDB layouts |
| `C:/projects/gemrb/` | GemRB source |
| `C:/projects/NearInfinity/` | File formats |
| `C:/projects/iesdp/` | Effects, opcodes |
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
