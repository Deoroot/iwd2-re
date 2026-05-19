# Agent Rules

Repo: `github.com/WillScarlettOhara/iwd2-re` (fork of alexbatalov)

## Build
- `src/` commit must compile clean on VS2019 Win32.
- Rename field/function → update `.h` + ALL `.cpp` in ONE commit. `rg "oldName" src/`
- Rename function → Ghidra first, save, then sync source.
- Danger files: `FileFormat.h`, `BalDataTypes.h`, `CChitin.h`, `CGameSprite.h`.
- Never prefix `field_XXX` with type letter.
- Broken? `git diff upstream/main -- src/`

## Naming
- Functions: `sub_NNNNNN` → `ClassName::method`
- Fields: `field_XX` → `m_camelCase` (Hungarian)
- Address comment above each function: `// 0x7D14F0`
- Virtual function: `// 0x7D14F0 (virtual)`

## Rename Workflow

**Functions:**
```
curl -X POST http://127.0.0.1:8089/rename_function_by_address -H "Content-Type: application/json" -d '{"function_address":"0xNNNNNN","new_name":"Class::Method"}'
```
Save Ghidra project. Sync source in one commit.

**Fields:**
1. Match class in `bg2_pdb_types.txt` (BG2EE offsets differ, names match)
2. Rename in `.h` + all `.cpp` in ONE commit
3. Build before push

## GhidraMCP (`:8089`)

Installed at `C:\ghidra-mcp`.

**Launch:**
```powershell
cd C:\ghidra-mcp
python -m tools.setup deploy --ghidra-path "C:\ghidra_dist\ghidra_12.0.4_PUBLIC"
& "C:\ghidra_dist\ghidra_12.0.4_PUBLIC\ghidraRun.bat"
```

**Sanity:**
```bash
curl -s http://127.0.0.1:8089/check_connection
```

**Decompile / disasm:**
```bash
curl "http://127.0.0.1:8089/decompile_function?address=0x6C9A50"
curl "http://127.0.0.1:8089/disassemble_function?address=0x6CA830"
curl -X POST http://127.0.0.1:8089/disassemble_bytes -H "Content-Type: application/json" -d '{"start_address":"0x44CBC0","length":24}'
```

**Search:**
```bash
curl "http://127.0.0.1:8089/search_functions?name_pattern=CanSaveGame&limit=50"
```

**Xrefs:**
```bash
curl "http://127.0.0.1:8089/get_xrefs_to?address=0x44CBC0&limit=20"
curl "http://127.0.0.1:8089/get_xrefs_from?address=0x6C90B9&limit=10"
```

**Set prototype:**
```bash
curl -X POST http://127.0.0.1:8089/set_function_prototype -H "Content-Type: application/json" -d '{"function_address":"0x6CA830","prototype":"void __thiscall EquipOffHWeapon(void * resRef, byte * colorRangeValues)"}'
```

**Read bytes (wide range) — use pefile, not `/memory_bytes`:**
```python
import pefile; pe = pefile.PE(r'C:\GOG Games\Icewind Dale 2\IWD2.exe', fast_load=True); print(pe.get_data(0x8ABCA4 - pe.OPTIONAL_HEADER.ImageBase, 16))
```

Mutations commit immediately. Save project after rename batches.

## After Session
Save in Ghidra GUI. `bash scripts/ghidra_save.sh` for zip backup.

## Refs
| Path | Use |
|------|-----|
| `C:/projects/bg2-symbols/bg2_pdb_types.txt` | BG2EE PDB layouts |
| `C:/projects/gemrb/` | GemRB source |
| `C:/projects/NearInfinity/` | File formats |
| `C:/projects/iesdp/` | Effects, opcodes |
| `C:/ghidra-mcp` | GhidraMCP |

## Naming Rules
- Rename only when source matches Ghidra or decomp clear
- Guesses → `FUN_` + `Analysis` bookmark
- `curl "http://127.0.0.1:8089/search_functions?name_pattern=FUN_*&limit=50"`

## Milestone
Phase 2: name remaining `sub_` (~200) + `field_` (~640). Small classes first.
