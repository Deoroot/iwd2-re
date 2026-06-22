# Icewind Dale 2 - Reverse Engineered

> **Recover the lost source code.** An open-source recreation of the Icewind Dale II engine, built via community-driven reverse engineering. Shout out to Alexander Batalov, whose Infinity Engine RE work this project builds on.

---

## What is this?

This repository contains **reverse-engineered C++ source code** for **Icewind Dale II** (2002), the final Infinity Engine game from BioWare/Black Isle Studios.

The original source code was lost, which is why [Beamdog](https://beamdog.com/) never shipped an Enhanced Edition for IWD2. This project reconstructs it by analyzing the compiled `IWD2.exe` and translating machine code back into readable, faithful C++ that compiles with the same toolchain (Visual Studio, MFC).

This repository contains **only code, not game assets**. You still need to acquire those by purchasing the game on [GOG](https://www.gog.com/game/icewind_dale_2). The original working asset files (which would have allowed smoothing animations, remastering visuals, or improving audio) are also permanently lost.

**Goal**: A fully working, open-source engine that runs Icewind Dale II, which the community can use to improve [IWD2EE](https://github.com/Bubb13/IWD2EE), feed [GemRB](https://gemrb.org/), or build a full remake — in the spirit of the [Daggerfall Unity](https://www.dfworkshop.net/) port.

---

### AI Usage Disclaimer

I use AI to recover the IWD2 source code because my programming skills are nowhere near those of the engine's original authors. I also rely on AI to translate messages like this one into English, as my English isn't very good.

Many people criticize using AI to "vibecode" buggy applications because of the hallucinations inherent in LLMs, and I understand those criticisms. But reverse engineering is a near-ideal use case: when properly guided, the AI invents nothing. The ground truth is the binary itself — assembly and pseudocode extracted from `IWD2.exe` with Ghidra, runtime traces captured with Frida, and an automated faithfulness lint that flags any drift from the original. Every recovered function is checked against what the CPU actually runs, not against a guess.

It still requires a human constantly steering the output, plus time and money. I mostly run Opus on two $20 Claude Pro subscriptions. Cheaper models create too many subtle errors to be worth the debugging time on a binary this large. With a $200/month budget the recovery would already be close to finished — the limiting factor is waiting for message limits to reset, not the technique.

---

## Project Status

> **Not playable yet** — the engine boots and large subsystems are recovered, but core gameplay paths are still being reconstructed.

All numbers below are measured directly from the repo. Regenerate them anytime:

```bash
.venv-reagent/bin/python scripts/project_status.py            # human table
.venv-reagent/bin/python scripts/project_status.py --markdown # this table
```

| Metric | Value | Notes |
|--------|-------|-------|
| **`.text` code recovered** | **~77%** (3.47 MB / 4.48 MB) | **Byte-weighted** — share of the binary's executable code that has a faithful C++ body. The honest "how much engine is rebuilt" figure. |
| **Functions recovered to C++** | ~37% (10,964 / 29,647) | By count. Much lower than the byte figure because the recovered functions are the big ones; what remains is mostly small leaves/stubs. |
| **Functions named in Ghidra** | ~89% (26,525 / 29,647) | Metadata only — 3,122 still anonymous `FUN_`/`sub_`. Naming ≠ recovery. |
| **Source code** | ~308,000 lines | 426 `.cpp`/`.h` files |
| **TODO / FIXME** | 858 | across 119 files; 280 are `TODO: Incomplete` (unimplemented stubs) |
| **Unnamed fields** | 647 unique | `field_XXX` members still awaiting names |

**Three numbers, three different things.** *Named in Ghidra* (~89%) is cheap metadata. *Functions recovered* (~37%) counts a 5-byte stub the same as a 2000-instruction monster, so it understates real progress. **The byte-weighted `.text` figure (~77%)** is the one that reflects how much of the actual engine has been rebuilt: recovery has prioritized the large, important functions first, so most of the code mass is done even though a third of the function *count* remains. (The function-size denominator is derived from Ghidra's entry addresses and validated against the PE — the sum matches `.text` VirtualSize to within 2 bytes.)

> `src/NewDiscovered.h` is a **stale manual scratch list** of uncategorized `FUN_` addresses. It is **not `#include`d anywhere** and covers only about a third of the functions still anonymous in Ghidra. Don't treat its header count as a real backlog figure — use `scripts/project_status.py`.

### Subsystem recovery (high level)

What has been *recovered into source*. Function-level numbers come from the script above; see **[ARCHITECTURE.md](ARCHITECTURE.md)** for the full breakdown.

| Subsystem | State | Notes |
|-----------|-------|-------|
| **Boot & Main Menu** | Recovered | Boots to the connection screen; cursor visible and clickable |
| **UI Screens** | Recovered | Options, keymaps, single-player, party select, character creation, inventory, store |
| **Dialogue** | Recovered | StartDialog→EndDialog flow, Continue/End, panel switching, dialogue action/break effects |
| **Pathfinding** | Recovered | A* (`CPathSearch::FindPath`), search thread, party group movement |
| **Inventory & Containers** | Recovered | Equipment/weapon drag highlight, loot panel, container slots, encumbrance/gold readout |
| **Spell projectiles & detonation** | Recovered | Projectile factory + delivery, cones, spell-hit detonation engine, dozens of spell visuals (see below) |
| **Action / Gameplay dispatch** | Partial | Most action IDs dispatched |
| **Effects** | Partial | Majority of `ApplyEffect` opcodes done, including damage/death |
| **Save / Load (marshalling)** | Partial | Party-record version upgraders + several GAM sections recovered |
| **Multiplayer / Networking** | Not working | Most message subtypes dropped; many Marshal/Unmarshal stubs |

### Recent progress — June 2026

The big arc this month was the **spell projectile and detonation system** — the factory, delivery, cone geometry, and a large batch of individual spell visuals.

| Area | Recovered |
|------|-----------|
| **Spell-hit detonation** | `IcewindCProjectileSpellHit` (Fire, OnArrival, AIUpdate, GatherTargets, Strike, detonation spawn) + the spell-hit visual/particle classes |
| **Spell visuals (leaves)** | Fireball, Delayed Blast Fireball, Skull Trap, Magic Missile, Stinking Cloud, Cloudkill, Acid Fog, Flame Strike, Ice Storm, Web, Entangle, Grease, Circle of Death, Insect Plague, Snilloc's Snowball Swarm, Call Lightning (SPPR302), and ~30 more spell-hit leaves wired into the projectile factory |
| **Cone projectiles** | `CProjectileCone` geometry, fan, hit-test, OnArrival/Pulse + 14 cone cases wired live |
| **Projectile core** | `CProjectileTravelling::Fire/AIUpdate`, `CProjectile::DeliverEffects` immunity gate, instant-delivery, trailing-VFX messages, `CMessageFireProjectile` |
| **Spell firing** | `CGameEffect::FireSpell` + `OnAdd`, `ForceMarkedSpell`/`CallLightning` action cases, projectile launch message wiring |
| **Effects** | `CGameEffectDamage`, `CGameEffectDeath`, `CGameEffectSkillUnsummon`, on-death effect-list teardown, impact/travel sounds |
| **Inventory / containers** | Loot-panel chain, container slot render/click, encumbrance + gold readout, drag highlight, quick-weapon swap |
| **Save / Load** | GAM party-record version upgraders, autosave area-marshal fidelity, save-screen BMP writes, several `CInfGame::Unmarshal` sections |
| **Tooling** | `struct_layout_audit.py` (pack(2) drift), ctor-vtable conflation guardrail, crash-guard arc gate, `spell_info.py`, `project_status.py` |

---

## Architecture

The Infinity Engine is split into several subsystems:

| Subsystem | Key classes | Description |
|-----------|-------------|-------------|
| **Core Engine** | `CChitin`, `CBaldurChitin`, `CInfinity` | App framework, message loop, renderer |
| **Game State** | `CInfGame`, `CGameSave`, `CGameArea` | Party, areas, saves, journal, world map |
| **Game Objects** | `CGameSprite`, `CGameDoor`, `CGameTrigger`, `CGameAIBase` | Interactive world entities |
| **AI / Scripting** | `CAIScript`, `CAIAction`, `CAITrigger`, `CAIObjectType` | Script parser and execution |
| **UI** | `CScreenWorld`, `CScreenCharacter`, `CScreenInventory`, … | All game screens |
| **Resources** | `CRes`, `CDimm`, `CResRef` | Asset loading (.ARE, .CRE, .ITM, …) |
| **Messages** | `CMessage` | Inter-object communication |
| **Effects / Spells** | `CGameEffect`, `CProjectile`, `CDerivedStats` | Spells, buffs, projectiles, modifiers |
| **Network** | `CNetwork` | Multiplayer support |
| **Audio** | `CSoundMixer`, `CSound` | Sound effects and music |

---

## Building

### Requirements

- **Visual Studio 2019** (or newer) with the **MFC** component
- **Windows 10/11**
- **C++17**, **Win32 (x86)** target — the binary is 32-bit
- **DirectDraw**
- Original game files from [GOG](https://www.gog.com/game/icewind_dale_2) or Steam

The build is driven by `CMakeLists.txt` (`CMAKE_MFC_FLAG 2`, `WIN32` executable) and must compile as **Win32/x86**; commits are expected to build clean.

```bash
# Clone with reference submodules
git clone --recursive git@github.com:WillScarlettOhara/iwd2-re.git
cd iwd2-re
cmake -A Win32 -B build
cmake --build build --config Debug
```

Building only needs the source tree and your own game assets — the bundled RE data (PDB extracts, Ghidra project) is for reverse-engineering work, not the build (see *Tools & Reference Data*).

Drop the resulting `iwd2-re.exe` into a legitimate IWD2 install directory to run it against your assets. Only **32-bpp windowed mode** is currently implemented.

> Full setup, dependencies, and run instructions: **[BUILD_WINDOWS.md](BUILD_WINDOWS.md)**.

---

## Reverse-engineering workflow

Ground truth is, in order: **(1) PE bytes / disassembly** → **(2) Frida runtime traces** → **(3) the Ghidra decompile** (a fallible lift, used for navigation, never trusted blindly). Recovered C++ is verified against the binary, not against a guess.

Core tooling lives in `scripts/`:

| Tool | Purpose |
|------|---------|
| `scripts/sym.py` | PE micro-CLI: bytes, dwords, strings, disasm, vtable dump, pointer scan, `addr2fn`, minidump triage |
| `scripts/src_find.py` | Find a recovered function/class/global in `src/` (file:line, body, by address) |
| `scripts/fn_digest.py` | Quick look at a binary function (signature, callees, callers, strings) |
| `scripts/spell_info.py` | Dump an IWD2 `.SPL` by resref |
| `scripts/project_status.py` | Honest progress metrics (this README's table) |
| `vendor/ghidra-ai-bridge` | Headless PyGhidra export → `gb decompile / xrefs / struct / vtable` |
| `re-agent … parity` | Faithfulness lint comparing recovered C++ to the binary |

Reverse engineering and editing happen on Linux (Ghidra, the knowledge graph, the RE agents); the Windows build, game, and Frida run on a Windows VM. See **[AGENTS.md](AGENTS.md)** and **[CLAUDE.md](CLAUDE.md)** for the full workflow, plus **[docs/](docs/)** for tracing and validation guides.

---

## Knowledge Graph (code-review-graph)

The repo uses [code-review-graph](https://github.com/tirth8205/code-review-graph) to build a structural map of the codebase for AI-assisted navigation (callers, callees, impact radius, test coverage). Independent graphs cover `src/` and `refs/gemrb/`. MCP tools (`semantic_search_nodes`, `query_graph`, `get_impact_radius`, `detect_changes`, `cross_repo_search_tool`, …) are exposed to AI agents — see `CLAUDE.md` / `AGENTS.md`.

---

## Idle CPU yield (deliberate divergence from the binary)

The original `IWD2.exe` busy-waits its idle loop: `CChitin::WinMain` (0x7926B0) spins on `PeekMessage` between the ~33 ms AI ticks with no CPU yield. There is no `Sleep` anywhere in that loop (confirmed against the binary's call set), so it pegs roughly one core **in both windowed and fullscreen** — fullscreen does *not* throttle it via vsync; the "fullscreen idles at 0%" intuition simply does not hold for IWD2. Our build instead parks the main thread with `MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT)` whenever no frame is pending, eliminating that spin.

This is safe because the main thread is only a *render-on-stale consumer*: the frame cadence is owned by the `timeSetEvent` timer thread (`TimerFunction` → `SetEvent(m_eventTimer)`) and the AI thread (`CBaldurChitin` AI loop → `AsynchronousUpdate` → sets `m_bDisplayStale`). Yielding the consumer for ≤1 ms cannot change AI timing, frame rate, or render output — only idle wall-clock — so runtime traces stay call-for-call identical to the original.

Measured on an 8-core VM, same in-world save, mouse idle:

| Build / mode                              | CPU (% of all 8 cores) |
| ----------------------------------------- | ---------------------- |
| `IWD2.exe` original, **fullscreen**       | 10.3 % (~0.8 core)     |
| our build, windowed, marker present       | 11.7 % (~0.9 core)     |
| our build, windowed, **yield (default)**  | **6.0 % (~0.5 core)**  |

The yield removes ~one core of pure spin and already idles below the original in *every* mode. The remaining ~6 % is the engine's real 30 Hz AI+render cycle, which runs unconditionally while a game is loaded (the original pays it *on top of* the spin). Driving idle CPU to literal zero would mean skipping renders when the frame is unchanged and AI ticks when paused — a frame-model rewrite IWD2 never does — so it is intentionally not attempted.

This is the one **intentional divergence** from `IWD2.exe`, kept clear of reverse-engineering: drop an empty marker file `iwd2-re-busywait.enabled` in the game directory (the process CWD) to disable the yield and restore the exact original busy-wait for faithful timing / Frida differential runs. With the marker present the added branch is dead and the loop is behaviorally identical to `0x7926B0` — so parity on that address reads RED by design.

---

## Contributing

Help is welcome — recovering functions, naming `FUN_`/`sub_` placeholders, documenting `field_XXX` members, testing against real assets. The golden rule: **code must match `IWD2.exe`; missing is better than wrong.** Full guidelines, naming conventions, and the recovery/verification workflow are in **[CONTRIBUTING.md](CONTRIBUTING.md)**.

---

## Tools & Reference Data

### Bundled data (`data/`)

| Path | Contents |
|------|----------|
| `data/pdb/` | BG2EE PDB (`Baldur.pdb`) + extracted symbol DB (`bg2_pdb.sqlite`, `bg2_pdb_extracted.zip`) — names carry over, offsets differ |
| `data/ghidra/` | BG2EE Ghidra project (`BG2EE_rep.zip`). The **IWD2** Ghidra snapshot is too large/churny for git — it ships as a rolling **GitHub Release** asset (tag `ghidra-snapshot`), produced by `scripts/ghidra_snapshot.sh` and fetched with `scripts/ghidra_restore.sh` |
| `data/near_infinity_export/` | NearInfinity asset export (2DA, ARE, CRE, DLG, SPL, ITM, IDS, …) |
| `data/runtime_reference/` | Captured original-game runtime references (e.g. prologue autosave) |

The Windows-era `data/restore.ps1` expands the bundled PDB/BG2EE archives into VM paths; on the Linux host, fetch the IWD2 Ghidra project with `scripts/ghidra_restore.sh`.

### Git submodules (`refs/`)

| Submodule | Use |
|-----------|-----|
| `refs/gemrb` | Open-source IE implementation — `CGameSprite`→`Actor`, `CInfGame`→`Game` mappings |
| `refs/NearInfinity` | IWD2 file formats (.CRE, .ARE, .ITM) |
| `refs/iesdp` | IE effects/opcodes, STATS.IDS |

### External tools

| Tool | Purpose |
|------|---------|
| **Ghidra** + headless PyGhidra export | Reverse-engineering framework, decompile/disasm/struct export |
| **Frida** | Runtime tracing / differential capture (original vs. our build) |
| **Visual Studio 2019** (Win32, C++17, MFC) | Build compiler |

---

## Related projects

- [alexbatalov/iwd2-re](https://github.com/alexbatalov/iwd2-re) — upstream Infinity Engine RE work
- [Bubb13/IWD2EE](https://github.com/Bubb13/IWD2EE) — Icewind Dale II: Enhanced Edition community project
- **GemRB** — open-source Infinity Engine (see `refs/gemrb`)
- **NearInfinity** — IE file browser (see `refs/NearInfinity`)
- **IESDP** — Infinity Engine Structures Description Project (see `refs/iesdp`)

---

## Documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — engine subsystems and how they fit together
- **[BUILD_WINDOWS.md](BUILD_WINDOWS.md)** — full build & run setup
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — how to contribute, naming conventions, faithfulness rules
- **[AGENTS.md](AGENTS.md)** / **[CLAUDE.md](CLAUDE.md)** — full RE workflow, tool routing, build-safety rules
- **[docs/](docs/)** — differential Frida tracing, recovery validation, faithfulness notes, recover tutorial

---

## License

Available under the [**Sustainable Use License**](LICENSE.md).

> This project does **not** distribute game assets. You must own a legitimate copy of Icewind Dale II.

---

## Acknowledgments

- **Alexander Batalov** — original Infinity Engine reverse-engineering work
- **Beamdog** — Enhanced Editions and keeping IE alive
- **Ghidra Team (NSA)** — the reverse-engineering framework
- **Infinity Engine community** — decades of modding and documentation

---

> *"The winds of Icewind Dale carry whispers of a lost codebase... recovered, one function at a time."*
