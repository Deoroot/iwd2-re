# Icewind Dale 2 - Reverse Engineered

> **Recover the lost source code.** An open-source recreation of the Icewind Dale II engine, built via community-driven reverse engineering.

---

## What is this?

This repository contains **reverse-engineered C++ source code** for **Icewind Dale II** (2002), the final Infinity Engine game from BioWare/Black Isle Studios.

Original source code was never released. This project reconstructs it by analyzing the compiled binary and translating machine code back into readable, modern C++.

**Goal**: Create a fully working, open-source engine that can run Icewind Dale II, then extend it with community improvements.

---

## Project Status

> **Not playable yet** — but getting closer.

| Metric | Progress | Details |
|--------|----------|---------|
| **Functions** | **88%** (24,883 / 28,024) | Ghidra DB sync'd with C++ source |
| **Code** | ~53% (~2M / ~3.8M lines) | Decompiled and organized |
| **Unnamed Functions** | 3,141 remaining | Stubs in `NewDiscovered.h`, not yet RE'd |
| **Unnamed Fields** | ~1,880 unique | Class-scoped rename in progress |
| **TODO / FIXME** | 889 in source | 716 synced as Ghidra bookmarks |
| **Main Menu** | **Working** | Boots to CScreenConnection; mouse cursor visible and clickable |
| **UI Screens** | Working | Options, Keymaps, Single Player, Party Select, Character Creation |
| **Load Game** | **Working** | Save list + preview thumbnails fixed |
| **World Screen** | **Partial** | Loads from menu; blocked on AI/dialogue pipeline |
| **Action Bar** | **Working** | Skills submenu, customize menu, weapon picker, all sub-menus exit correctly |
| **Inventory** | **Working** | Slots, weapon icons, active-set highlight, STON* fallback for empties |
| **Gameplay** | **Not working** | Requires `ProcessAI` / `ExecuteAction` implementation |

**Current milestone**: Reconstruct the AI and dialogue runtime (`CGameAIBase::ProcessAI`, `ExecuteAction`, `EvaluateStatusTrigger`).

### Recent Progress

| Date | Achievement |
|------|------------|
| May 2026 | **Action Bar**: Customize menu (state 0x75), skills submenu (0x73), quick-weapon picker (0x79), class pickers (0x76/0x77), and all sub-menu exit paths wired per Ghidra |
| May 2026 | **Inventory**: STON* fallback for empty equipment slots, active-set HIGHLGHT ring, quick-weapon panel rendering |
| May 2026 | **Ghidra DB**: 4,963 functions renamed (88% named), 716 TODO/FIXME bookmarks imported |
| May 2026 | **Tooling**: GhidraMCP REST API (`:8089`) for rename/annotate; batch SQL via `ghidrasql -f` |
| May 2026 | **Fields**: 26 `field_XXX` renamed across 10 classes (CRes, CChitin, CDimm, CUIManager, CNetwork, etc.) |
| May 2026 | **Load Game**: Save list + preview thumbnails fixed |
| May 2026 | **Critical sections**: Named and documented across CUIManager, CNetwork, CChitin |
| May 2026 | **Cleanup**: 204 DBG() scaffolding calls + debuglog.h removed once boot was stable |

---

## Architecture

The Infinity Engine is split into several subsystems:

| Subsystem | Files | Description |
|-----------|-------|-------------|
| **Core Engine** | `CChitin`, `CBaldurChitin`, `CInfinity` | App framework, message loop, renderer |
| **Game State** | `CInfGame`, `CGameSave`, `CGameArea` | Party, areas, saves, journal, world map |
| **Game Objects** | `CGameSprite`, `CGameDoor`, `CGameTrigger`, `CGameAIBase` | All interactive world entities |
| **AI/Scripting** | `CAIScript`, `CAIAction`, `CAITrigger`, `CAIObjectType` | Script parser and execution |
| **UI** | `CScreenWorld`, `CScreenCharacter`, `CScreenInventory`, ... | All game screens |
| **Resources** | `CRes`, `CDimm`, `CResRef` | Asset loading (.ARE, .CRE, .ITM, etc.) |
| **Messages** | `CMessage` | Inter-object communication system |
| **Effects** | `CGameEffect`, `CDerivedStats` | Spells, buffs, modifiers |
| **Network** | `CNetwork` | Multiplayer support |
| **Audio** | `CSoundMixer`, `CSound` | Sound effects and music |

---

## Building

### Requirements

- **Visual Studio 2019** (or newer)
- **Windows 10/11**
- **C++17**
- **MFC** (Microsoft Foundation Classes)
- **DirectDraw**
- Original game files from [GOG](https://www.gog.com/game/icewind_dale_2) or Steam

### Quick Start

```bash
# Clone with all reference submodules
git clone --recursive git@github.com:WillScarlettOhara/iwd2-re.git
cd iwd2-re

# Restore bundled data (PDB extracts + Ghidra projects)
powershell -File data/restore.ps1

# Build (Win32/Debug or Release)
cmake --build build --config Debug

# Smoke test: load visible save slot 0 and wait for world engine
python scripts/auto_start_game.py
python scripts/auto_start_game.py --new-game --party "Lady's Lament"

# Optional changed-line RE lint
cmake --build build --target re-lint --config Debug
```

> Only **32-bpp windowed mode** is currently implemented.

`re-lint` is deliberately narrow: it checks only touched `src/` lines by
default and reports patterns that tend to cause IntelliSense/build drift or
Ghidra sync mistakes. It does not reformat, modernize, or auto-fix code.

---

## Known Limitations & Optimization Opportunities

### CPU usage at idle (~6-7% in windowed mode)

The original `IWD2.exe` exhibits identical CPU usage on the same hardware — this is not a regression in this codebase.

**Root cause:** `CChitin::WinMain` (0x7926B0) uses a `PeekMessage` spin loop between AI ticks (~33 ms apart). In windowed mode there is no vsync throttle, so the inner loop burns CPU continuously.

**Potential improvements (not implemented — would deviate from Ghidra original):**
- Replace `PeekMessage` spin with `MsgWaitForMultipleObjects` to sleep until a message or timer fires
- Enable fullscreen mode: `CChitin::FullScreenFlip` passes `DDFLIP_WAIT` to DirectDraw, providing vsync throttle

Both changes would reduce idle CPU to near 0% but diverge from the original binary behavior.

---

## Contributing

We need help! Here's how you can contribute:

### 1. Reverse Engineering
- Decompile functions using [Ghidra](https://ghidra-sre.org/)
- Name `sub_` placeholders with meaningful names
- Document `field_` members
- Cross-reference with other Infinity Engine games (BG2EE, IWDEE)

### 2. Implement Stubs
- Find `// TODO: Incomplete` markers
- Implement missing function bodies based on decompiled pseudocode
- Focus on World Screen blockers

### 3. Testing
- Build and test the game
- Report what works/breaks
- Verify fixes against original game

### 4. Documentation
- Add class header comments
- Write architecture docs
- Help newcomers understand the code

### Naming Conventions

We use **MFC Hungarian notation**:
- `m_` — class members
- `n` — integer
- `p` — pointer
- `b` — boolean
- `w` — WORD/short
- `dw` — DWORD
- `s` — string
- `c` — class instance
- `l` — list
- `h` — handle

Address comments mark original binary locations:
```cpp
// 0x7D14F0
int CGameSprite::GetDerivedStats() {
    return m_pDerivedStats;
}
```

### Rename / Annotate Ghidra

Use the **GhidraMCP REST API** (`http://127.0.0.1:8089`) — run Ghidra GUI, plugin auto-starts.

```powershell
# Inspect before renaming
curl -s "http://127.0.0.1:8089/decompile_function?address=0x5D2DE0"
curl -s "http://127.0.0.1:8089/get_function_variables?address=0x5D2DE0"

# Rename function
curl -s -X POST http://127.0.0.1:8089/rename_function_by_address -H "Content-Type: application/json" -d '{"function_address":"0x5D2DE0","new_name":"RenderFogOfWar"}'

# Set function signature
curl -s -X POST http://127.0.0.1:8089/set_function_prototype -H "Content-Type: application/json" -d '{"function_address":"0x5D2DE0","prototype":"void RenderFogOfWar(CVidMode*)"}'

# Rename local / param
curl -s -X POST http://127.0.0.1:8089/rename_variable -H "Content-Type: application/json" -d '{"function_address":"0x5D2DE0","oldName":"local_8","newName":"pArea"}'
curl -s -X POST http://127.0.0.1:8089/set_parameter_type -H "Content-Type: application/json" -d '{"function_address":"0x5D2DE0","parameter_name":"param_1","new_type":"CVidMode *"}'

# Comments & bookmarks
curl -s -X POST http://127.0.0.1:8089/set_plate_comment -H "Content-Type: application/json" -d '{"address":"0x5D2DE0","comment":"Renders fog of war overlay."}'
curl -s -X POST http://127.0.0.1:8089/set_bookmark -H "Content-Type: application/json" -d '{"address":"0x5D2DE0","category":"review","comment":"Check blend flags."}'

# Persist Ghidra DB
curl -s "http://127.0.0.1:8089/save_program?program=IWD2.exe"
```

Full workflow: [decomp_ref/ghidra_rename_annotate.md](decomp_ref/ghidra_rename_annotate.md)

---

## Tools & Reference Data

Everything needed for RE work is bundled in this repo or linked as submodules.

### Bundled Data (`data/`)
| Archive | Contents | Uncompressed |
|---------|----------|-------------|
| `data/pdb/bg2_pdb_extracted.zip` | BG2EE PDB types, symbols, globals | 126 MB |
| `data/ghidra/IWD2_rep.zip` | IWD2 Ghidra project repository | 301 MB |
| `data/ghidra/BG2EE_rep.zip` | BG2EE Ghidra project repository | 227 MB |

Run `data/restore.ps1` after clone to extract to `C:\projects\` and `C:\ghidra_projects\`.

### Git Submodules (`refs/`)
| Submodule | Source | Use |
|-----------|--------|-----|
| `refs/gemrb` | github.com/gemrb/gemrb | CGameSprite→Actor, CInfGame→Game mappings |
| `refs/NearInfinity` | github.com/NearInfinityBrowser/NearInfinity | IWD2 file formats (.CRE, .ARE, .ITM) |
| `refs/iesdp` | github.com/Gibberlings3/iesdp | IWD2 effects (353 opcodes), STATS.IDS |

### External Tools
| Tool | Purpose | Link |
|------|---------|------|
| **Ghidra 12.1** | Reverse engineering framework | [ghidra-sre.org](https://ghidra-sre.org/) |
| **GhidraSQL** | SQL interface to Ghidra DB | bundled with LibGhidraHost |
| **Visual Studio 2019** | Build compiler (Win32, C++17, MFC) | — |

---

## Related Projects

- [alexbatalov/iwd2-re](https://github.com/alexbatalov/iwd2-re) — Upstream project by Alexander Batalov
- **GemRB** — Open-source Infinity Engine implementation (see `refs/gemrb`)
- **NearInfinity** — Infinity Engine file browser (see `refs/NearInfinity`)
- **IESDP** — Infinity Engine Structures Description Project (see `refs/iesdp`)
- **BG2EE** / **IWDEE** — Beamdog's official Enhanced Editions (PDB symbols in `data/pdb/`)

## Documentation

- **[AGENTS.md](AGENTS.md)** — Full workflow guide: GhidraMCP queries, rename strategy, build safety rules
- **[decomp_ref/ghidra_rename_annotate.md](decomp_ref/ghidra_rename_annotate.md)** — GhidraMCP rename / annotate workflow and tool usage
- **[decomp_ref/](decomp_ref/)** — Memory nodes per subsystem with field mappings and discoveries

---

## License

This source code is available under the [**Sustainable Use License**](LICENSE.md).

> **Note**: This project does NOT distribute game assets. You must own a legitimate copy of Icewind Dale II.

---

## Acknowledgments

- **Alexander Batalov** — Original reverse engineering work
- **Beamdog** — Enhanced Editions and keeping IE alive
- **Ghidra Team (NSA)** — For the incredible reverse engineering tool
- **Infinity Engine Community** — Decades of modding and documentation

---

## Community

- GitHub Discussions: [github.com/WillScarlettOhara/iwd2-re/discussions](https://github.com/WillScarlettOhara/iwd2-re/discussions)
- Infinity Engine Discord communities
- r/icewinddale — Reddit community

---

> *"The winds of Icewind Dale carry whispers of a lost codebase... recovered, one function at a time."*
