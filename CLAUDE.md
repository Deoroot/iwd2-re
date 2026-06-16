# CLAUDE.md

## Repo

`src/` = hand-recovered C++ matching `IWD2.exe`. Binary (asm/bytes) + Frida =
ground truth; the Ghidra *decompile* is a fallible lift (see "Truth hierarchy").

## Host / VM split

Host (CachyOS, `/home/wills/iwd2-re`) = edit/CC/graph/re-agent/Ghidra(`/opt/ghidra`)/pefile.
VM (Win11, SSH `win11vm`) = build (VS2019/MFC) + game + Frida only.
Paths: `/home/wills/iwd2-re/...` = host; `C:\iwd2-re\...`, `C:\GOG Games\...` = VM.

## Lookup routing — cheapest tool first

| Want | Tool |
|------|------|
| Find fn/class/global in src (file:line, 0xADDR, exact body) | `python3 scripts/src_find.py NAME` / `Class::Method --body` / `0xADDR` / `Class:: -l` / `--file f.cpp` |
| Quick look at a binary fn (sig, callees, callers, strings) | `python3 scripts/fn_digest.py 0xADDR\|Name` (`--full` → tmp file path) |
| BG2 name carry-over (class layout, methods+vft slots, members, globals) | `python3 scripts/bg2_find.py NAME` / `CClass::` / `CClass::sub` — never grep the PDB dumps |
| PE bytes / dwords / strings / disasm / vtable / ptr-scan / crash dump | `.venv-reagent/bin/python scripts/sym.py bytes\|u32\|str\|disasm\|findptr\|vtable\|addr2fn\|crash` |
| Who touches field +0xNNN (byte-pattern → containing fns) | `scripts/sym.py scan <hexbytes>` (little-endian displacement, e.g. `38560000` = `[reg+0x5638]`) |
| Callers/callees graph, blast radius, exec flows, review diff | graph MCP: `query_graph` (callers_of/callees_of/file_summary), `get_impact_radius`, `get_affected_flows`, `detect_changes` |
| GemRB lookup / cross-repo | same graph tools + `repo_root="/home/wills/iwd2-re/refs/gemrb"`; `cross_repo_search_tool` |
| Anything VM (build/run/logs/processes/files/frida) | `scripts/vm.sh` (below) |
| Simple Frida probe (log args + fields at fn entries) | `scripts/frida_probe.py --hooks h.json` then `--summary` |
| Mechanical batch analysis (parity triage, `#guess` sweep) | `scripts/ds_batch.py parity_triage\|rename_sweep\|free` (DeepSeek via OpenCode Go, key in `.env`) |
| Token-cost check of recent sessions | `python3 scripts/token_audit.py` |

- **No ad-hoc python heredocs** for pefile/capstone/minidump/map work the CLIs cover — extend the CLI instead.
- Run scripts via `.venv-reagent/bin/python` (pefile/capstone/ghidra deps live there; bare `python` → `ModuleNotFoundError`).
- Raw `gb decompile` only when the digest is insufficient. Recovery = full bundle (below), unchanged.
- Grep/Read on `src/` only after `src_find.py` misses. Graph query = one bare identifier token.
- Exploration estimated >5 tool calls or >3 files → spawn `caveman:cavecrew-investigator` (compressed output). Mechanical 1-2-file edit late in a long session → `caveman:cavecrew-builder`. Else inline.
- Batch independent tool calls in one message. `/clear` between recovery arcs. Memory arc files ≤4K.

## Build & run (host → VM over SSH)

```bash
scripts/vm.sh build [--run]   # remote_build.sh wrapped: prints ONLY errors/warnings + status (full log tmp_vm_build.log)
scripts/vm.sh run [slot]      # kill OUR iwd2-re.exe + launch session 1 (default combat slot 2)
scripts/vm.sh log <regex> [-n 50] [-f path]   # Select-String VM-side (UTF-16 safe); also: tail/status/ps/pull/push
scripts/vm.sh frida script.py # ship + run as session-1 payload, ready-to-log
```
- Kill policy: `build` never kills; `run` kills our exe only; original `IWD2.exe` only via explicit `vm.sh kill orig` (Frida sessions survive builds).
- GUI over SSH = session 0 (never renders); session 1 via `vm_s1.cmd` required for render/input/Frida. Smoke timeout there = no desktop, not a crash.
- Commits must compile VS2019 Win32. Rename → update `.h` + ALL `.cpp` in one commit. Build fail → stop.

## Truth hierarchy (the decompile is a fallible lift)

Don't invent code. Rank of ground truth:
1. **PE bytes / disassembly** (`sym.py disasm`, `gb dump-asm`) = what the CPU runs — final word on instructions, call targets, struct offsets.
2. **Frida runtime trace** = final word on dynamic facts — real `__thiscall` args, which branch runs, field/flag semantics, container contents, actual runtime types.
3. **Ghidra decompile** = best first read but FALLIBLE: guesses types, mangles `__thiscall` args, miscounts 16-vs-32-bit reads, drops/merges inlined calls, garbles SEH/reordering, invents `CONCAT`/casts. Navigate with it; verify against asm or Frida before trusting counts, args, widths, offsets, or types.

`// 0xADDR` can be stale → re-check against the binary. Address not in funcs table → check vtable DATA xref (virtual method): `scripts/sym.py findptr 0xMETHOD` → `sym.py vtable 0xBASE`.

## Ghidra access — ghidra-bridge

**ghidra-ai-bridge** (vendored `vendor/ghidra-ai-bridge`) = PyGhidra **headless export** into `.ghidra-exports/` (not a live server). Host-side: venv `.venv-reagent/bin/{re-agent,ghidra-bridge}`, project `~/ghidra_projects/IWD2/IWD2`. Config `ghidra-bridge.host.yaml` + `re-agent.host.yaml` (repo root, gitignored). re-agent's backend goes through `scripts/ghidra-bridge-host` (injects `--config`) — without it every lookup returns "Function not found".

**Query** (`gb` = `.venv-reagent/bin/ghidra-bridge --config ghidra-bridge.host.yaml`):

| Want | Cmd |
|---|---|
| Full decompile (prefer `fn_digest.py` for a look) | `gb decompile 0xADDR` |
| Callers / callees | `gb xrefs-to 0xADDR` / `gb xrefs-from 0xADDR` |
| Disassembly (boots PyGhidra ~1 min; `sym.py disasm` = instant) | `gb dump-asm 0xADDR out.asm` |
| Struct / vtable / containing fn / unrecovered list | `gb struct CClass` / `gb vtable CClass` / `gb containing 0xADDR` / `gb unimplemented CClass` |

**"Function not found" / "no export" on a VALID addr** = no fn defined there (vtable-only callee). Fix: ensure `// 0xADDR` in source (stub if new) → `python scripts/reagent_address_map.py --out .ghidra-exports/address_map.json` → `gb export create-functions` (~10 min, re-exports all).
⚠️ NEVER `gb build-map` — overwrites the map with an EMPTY one.

**Parity** = faithfulness lint (11 signals + objective verifier). Run from repo root:

```bash
.venv-reagent/bin/re-agent --config re-agent.host.yaml parity --address 0xADDR
.venv-reagent/bin/re-agent --config re-agent.host.yaml parity --filter "CClass::" --output tmp_parity.json
```

GREEN/YELLOW/RED. asm signals boot PyGhidra (~1 min/call). STL-inline call-count YELLOW/RED = classic false positives.
⚠️ `re-agent reverse` = lint/oracle at best, NEVER author (regresses clean code). Don't commit its output unverified; gate behavioral claims with a Frida diff.

PE facts: host IWD2.exe = same bytes as VM, ImageBase `0x400000`, no ASLR → `scripts/sym.py`.

## Frida tracing

Throwaway traces to confirm field semantics live BEFORE a recover — ground truth beats guessing. Simple probes (args+fields at entries) = `frida_probe.py` hook-table JSON; complex logic (call-origin filters, list walks) = bespoke script, template `scripts/frida_formation_trace.py`, docs `docs/frida-differential-tracing.md`.
- **OUR build → `DebugLog.h` (`Iwd2DebugLog(fmt,...)`), NOT Frida**: typed members, zero offset/symbol/ecx fragility (the debug-build +0xA base shift breaks Frida member reads — see `53fe6dccc766`). Enable = touch `.\iwd2-re-debug.enabled` in the game CWD (`C:\GOG Games\Icewind Dale 2`); writes `.\iwd2-re-debug.log`. Frida ONLY for the ORIGINAL (no source) + the our-vs-original differential.
- Game in VM session 1; spawn-time hooks → driver as session-1 payload (`vm.sh frida`).
- **`vm.sh frida` payload gotchas (scheduled-task + fire-and-forget VBS — bite EVERY time):**
  - NO stdin: `sys.stdin.read()`=EOF → driver exits early. Keep alive: `while True: time.sleep(0.5)`.
  - stdout `>`-redirect drops writes once VBS parent exits. Write to a DEDICATED file w/ `f.flush()`+`os.fsync(fd)`, read THAT (not `vm_s1_out.txt`).
  - Re-attach: stuck driver BLOCKS `frida.attach` (silent no-fire / locked logfile). `taskkill` UNRELIABLE; use `Get-Process python` / `Stop-Process -Name python -Force`, confirm empty before re-ship.
  - Silent no-fire? base `0x400000` no-ASLR (`ptr(0xADDR)` absolute ok) — but dump 8 bytes/hook, diff vs `sym.py bytes ADDR` before blaming the path.
- `ptr(0xADDR)` absolute. `__thiscall`: `this` = `this.context.ecx`, stack args = `args[0..]`. Hook fn ENTRIES only.
- Runtime wins when `__thiscall` args disagree with decompiler; decompiler wins for counts (high-word garbage in int32 reads of 16-bit values).
- Read PE constants with `sym.py`, not the memory endpoint.
- Input-driving order: Frida-RPC into the engine fn > keyboard > `PostMessage` > polite physical hijack (unattended only; IWD2 polls mouse from the engine tick). Hover first and prove `CGameSprite::IsOver`/`OnActionButtonDown picked=...`; screenshots alone mislead. Dialog replies: `responseMarker` + real/post click.

## Game assets (`data/near_infinity_export/`)

NearInfinity export. Use for game content (action IDs, spell IDs, scripts, UI layouts).
Dirs: `2DA/` rule tables · `ARE/` areas · `BAM_DECOMP/` decompressed BAMv1 · `BCS/`+`BS/` scripts (.BAF) · `CHU/` UI panels · `CRE/` creatures · `DLG/` dialogs · `EFF/` effects · `IDS/` symbol→id maps · `ITM/` `SPL/` `STO/` `WED/` `WMP/`.

| Question | File |
|---|---|
| Action/trigger id N? | `IDS/ACTION.IDS`, `IDS/TRIGGER.IDS` |
| Stat/state name? | `IDS/STATS.IDS`, `IDS/SPLSTATE.IDS` |
| Spell SPxxx? / button slot? / qslot layout? | `SPL/SPxxx.SPL` / `CHU/<panel>.CHU` / `2DA/QSLOTS.2DA` |
| Area edges? / script usage? | `WMP/WORLDMAP.WMP` / grep `BCS/*.BAF` + `BS/*.BAF` |

### Asset names / StringRefs

Resolve the canonical TLK name before naming an asset in comments or commit messages (never infer from resref):
`python scripts/reagent_asset_names.py SPWI304 AR1000 60SPELLS` (`--strref 6618`, `--json`). Output comment-ready: `Fireball (SPWI304.SPL, strref 6618)`. Supports SPL, ITM, CRE, STO, ARE.

## External refs

| Path | Use |
|-----|-----|
| `data/pdb/Baldur.pdb` | BG2EE 2.5 PDB (names carry over, offsets differ). Query via `bg2_find.py`, not the raw dumps |
| `refs/gemrb/` | GemRB source → CRG graph (`repo_root=".../refs/gemrb"`) |
| `refs/NearInfinity/` | File formats (.CRE/.ARE/.ITM) |
| `refs/iesdp/` | Effects, opcodes, STATS.IDS |
| `~/ghidra_projects/IWD2/IWD2` | Ghidra project. CLOSE the GUI before `ghidra-bridge export`. |

## Temp files

`tmp_*.txt`, `tmp_*.json`, `chunk_*.sql` = RE session noise. Not tracked. Delete freely.
`.venv-reagent/`, `.ghidra-exports/` = toolchain + export cache. Gitignored.

## Recover a function: assemble context FIRST

Quick look = `fn_digest.py`. Recovering = full offline bundle (fast, no PyGhidra boot):

    python scripts/reagent_assemble_context.py --address 0xADDR --out tmp_ctx.md

Bundle = resolved decompile (our names, vtable-slot-annotated vcalls) + REQUIRED CALL SET (binary ground truth — reproduce exactly) + BG2 PDB layout + IDS constants + class header. Read it, then write idiomatic C++. Don't hand-recover from a bare decompile.

## Code changes

- Recover → context bundle first (above). Verify `// 0xADDR` against Ghidra before touching.
- Prefer named constants over magic numbers when defined in file.
- `python scripts/vtable_audit.py ClassName` — missing virtual overrides (1-vtable anchor → conflation-blind).
- `python scripts/ctor_vtable_check.py [ClassName]` — class CONFLATION: ctors install diff vtables = 2 binary classes merged → wrong virtual recovered (the Fireball green bug). Run on classes w/ >1 ctor.
- After a recover: `re-agent parity --address 0xADDR` should be GREEN/YELLOW.
- Member-heavy fn (cell/render/anim): `.venv-reagent/bin/python scripts/parity_offsets.py 0xADDR` — "right callee, wrong member" (diffs binary's per-thiscall `this`-offset→member vs source; parity call-counts blind to it; found the 3-yr corpse-tint bug). `scripts/lint_extend_cells.py` = source-only twin for extend/`*Base` copy-paste.
- **Binary-mirror class (members carry `/* 0xNNN */` offsets) MUST be `#pragma pack(2)`** — the IE engine packs to 2, so a sub-4-aligned `LONG`/`WORD` tail silently 4-aligns without it. Compiles + parity GREEN (parity is code-faithfulness, layout-blind); only a runtime/visual bug shows — the Cloudkill ring over-density: `m_visual2MaxSpawn` drifted +0x30 vs the packed +0x2E, so the `reinterpret_cast<IcewindCSpellHitEmission&>` slot read garbage and the moving-spawn ran uncapped (36 vs 13). `python scripts/struct_layout_audit.py ClassName [--header f.h --source f.cpp]` diffs the MSVC-compiled layout (`cl /d1reportSingleClassLayout`) vs the comments by adjacent-member spacing (base-shift-robust); a break into a `reinterpret_cast`-relied tail = the bug (upstream STL/`CVidCell`/base-size breaks are known/benign). offsetof static_asserts can't do this — MSVC rejects offsetof on polymorphic classes (C2618).
- **Static parity ≠ runtime proof — arc not done until OUR build runs the path.** `scripts/vm.sh smoke [slot]` = our exe + save (default slot 3) + crash oracle `frida_crash_guard.py` (symbolized EBP bt), HOLDS terminal (no timer): ENTER→`RESULT: CLEAN` or a fault prints its bt. Original-only Frida never exercises our exe (shipped both Fireball cast crashes past GREEN: abort 11ef54f6, UAF 1ac84b92). Behavioral claims → diff our-vs-original, same hooks.
- End every recover session by recommending the next function(s) (callee gaps, `gb unimplemented`, caller counts).

## No hacks

Code must match `IWD2.exe`. No invented behavior.
- Can't recover → leave unimplemented (return early / no-op). Missing better than wrong.
- Unavoidable hack → `// HACK: <what> — <why> — replaces 0x<addr>`. Not `// TODO`.
