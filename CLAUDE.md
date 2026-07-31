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
| **Verify a recovered fn (the whole sequence, one verdict)** | `scripts/arc.py verify 0xADDR` — see "arc" below. Don't run the 9 steps by hand |
| **What to work on next** | `scripts/arc.py targets` (= `scripts/next_targets.py`); tracked top-40 in `docs/next-targets.md` |
| **Drive/inspect the UI yourself (menus, action bar, ground)** | `scripts/vm.sh smoke <slot> <secs> --ui scripts/scenarios/<s>.txt` — see "AutoUI" below. Don't hand-drive or ask the user first |
| Find fn/class/global in src (file:line, 0xADDR, exact body) | `python3 scripts/src_find.py NAME` / `Class::Method --body` / `0xADDR` / `Class:: -l` / `--file f.cpp` |
| Quick look at a binary fn (sig, callees, callers, strings) | `python3 scripts/fn_digest.py 0xADDR\|Name` (`--full` → tmp file path) |
| BG2 name carry-over (class layout, methods+vft slots, members, globals) | `python3 scripts/bg2_find.py NAME` / `CClass::` / `CClass::sub` — never grep the PDB dumps |
| PE bytes / dwords / strings / disasm / vtable / ptr-scan / crash dump | `.venv-reagent/bin/python scripts/sym.py bytes\|u32\|str\|disasm\|findptr\|vtable\|addr2fn\|crash` |
| Who touches field +0xNNN (byte-pattern → containing fns) | `scripts/sym.py scan <hexbytes>` (little-endian displacement, e.g. `38560000` = `[reg+0x5638]`) |
| Callers/callees graph, blast radius, exec flows, review diff | graph MCP: `query_graph` (callers_of/callees_of/file_summary), `get_impact_radius`, `get_affected_flows`, `detect_changes` |
| GemRB lookup / cross-repo | same graph tools + `repo_root="/home/wills/iwd2-re/refs/gemrb"`; `cross_repo_search_tool` |
| Anything VM (build/run/logs/processes/files/frida) | `scripts/vm.sh` (below) |
| Simple Frida probe (log args + fields at fn entries) | `scripts/frida_probe.py --hooks h.json` then `--summary` (its docstring lists the 6 VBS/stdin/fsync payload gotchas — read it before writing a bespoke probe) |
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
- Kill policy: `build` never kills; `run` kills our exe only. Original `IWD2.exe` is fair to kill once a trace/investigation is done — don't leave it running idle (wastes CPU/power); relaunch fresh for the next trace rather than keeping it up. Frida sessions survive builds.
- GUI over SSH = session 0 (never renders); session 1 via `vm_s1.cmd` required for render/input/Frida. Smoke timeout there = no desktop, not a crash.
- Commits must compile VS2019 Win32. Rename → update `.h` + ALL `.cpp` in one commit. Build fail → stop. `git push` periodically — commits pile up unpushed on `main`.

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

**Parity** = faithfulness lint (14 signals + objective verifier). Run from repo root:

```bash
.venv-reagent/bin/re-agent --config re-agent.host.yaml parity --address 0xADDR
.venv-reagent/bin/re-agent --config re-agent.host.yaml parity --filter "CClass::" --output tmp_parity.json
```

GREEN/YELLOW/RED. asm signals boot PyGhidra (~1 min/call). STL-inline call-count YELLOW/RED = classic false positives.
⚠️ `re-agent reverse` = lint/oracle at best, NEVER author (regresses clean code). Don't commit its output unverified; gate behavioral claims with a Frida diff.

PE facts: host IWD2.exe = same bytes as VM, ImageBase `0x400000`, no ASLR → `scripts/sym.py`.

## Frida tracing

Throwaway traces to confirm field semantics live BEFORE a recover — ground truth beats guessing. Simple probes (args+fields at entries) = `frida_probe.py` hook-table JSON; complex logic (call-origin filters, list walks) = bespoke script, template `scripts/frida_formation_trace.py`, docs `docs/frida-differential-tracing.md`. **Bespoke probes go in `scripts/probes/`** — `scripts/` keeps only reusable tooling (`frida_probe`, `frida_crash_guard`, `frida_formation_trace`, `frida_hang_bt`, `frida_diff_*`).
- **OUR build → `DebugLog.h` (`Iwd2DebugLog(fmt,...)`), NOT Frida**: typed members, zero offset/symbol/ecx fragility (the debug-build +0xA base shift breaks Frida member reads — see `53fe6dccc766`). Enable = touch `.\iwd2-re-debug.enabled` in the game CWD (`C:\GOG Games\Icewind Dale 2`) — assume it is ALREADY present by default (don't recreate); writes `.\iwd2-re-debug.log` (append-only — `Clear-Content` it before a fresh capture or stale lines mislead). Frida ONLY for the ORIGINAL (no source) + the our-vs-original differential.
- **Commit clean BEFORE adding `Iwd2DebugLog`.** Keeps debug isolated → strip via one `git checkout`/`revert`, no token-burning hunt for scattered debug lines.
- **Self-closing crash (no dialog): WER dumps + `frida_crash_guard` both miss it. Don't chase a dump** — bisect with `Iwd2DebugLog`, self-launch `vm.sh run <slot>`, read the debug log (no user needed); last line = fault. If `CInfGame.cpp:787` on relaunch: `(Get-Process iwd2-re).Kill()` (not `taskkill`) + recreate `tempsave`.
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
| Spell SPxxx? / button slot? / qslot layout? | `SPL/SPxxx.SPL` (dump: `scripts/spell_info.py RESREF`) / `CHU/<panel>.CHU` / `2DA/QSLOTS.2DA` |
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

## arc — run the validation sequence, don't retype it

```bash
scripts/arc.py verify 0xADDR   # resolve → lint → binary oracles → parity → layout → build → smoke
scripts/arc.py check           # host-only source lints (~1.5s); this is the pre-commit gate
scripts/arc.py sweep [--deep]  # whole-codebase auditors (~9 min; parity_cache_sweep alone is ~8)
scripts/arc.py status          # metrics + freshness nags
scripts/arc.py explain <step>  # the tool prose arc deliberately did NOT print
scripts/arc.py targets         # what to work on next
```

`verify` runs everything the bullets below describe, in cheap→expensive order, and prints ~10 lines. **Read those 10 lines, not 9 tool outputs** — every step's full stdout is captured under `.arc/<run>/` and is one `arc explain` away. Flags: `--static` (no VM), `--no-parity` (skip the ~60s PyGhidra boot), `--slot N --hold S --hit SYM`, `--json`, `--strict` (warn→fail), `--require exe,vm` (turn skips into failures).

- Exit **0** = no failure · **1** = a failure · **2** = harness error (or address not recovered).
- A missing capability (VM down, no `.ghidra-exports/`, **Ghidra GUI open**) is a `skip`, never a `fail`.
- `scripts/arc-baseline.txt` = accepted pre-existing findings. Add a line only with a reason; arc flags entries that stop matching as stale.
- Pre-commit gate is wired (`scripts/hooks/pre-commit-arc`). Bypass with `ARC_SKIP=1 git commit` — prefer it over `--no-verify`, which also skips the graph update. Re-install with `arc hooks --install`.

Run a bullet below directly only when you need that one tool in isolation.

## Code changes

- Recover → context bundle first (above). Verify `// 0xADDR` against Ghidra before touching.
- Prefer named constants over magic numbers when defined in file.
- `python scripts/vtable_audit.py ClassName` — missing virtual overrides (1-vtable anchor → conflation-blind).
- `python scripts/ctor_vtable_check.py [ClassName]` — class CONFLATION: ctors install diff vtables = 2 binary classes merged → wrong virtual recovered (the Fireball green bug). Run on classes w/ >1 ctor.
- After a recover: `re-agent parity --address 0xADDR` should be GREEN/YELLOW.
- Member-heavy fn (cell/render/anim): `.venv-reagent/bin/python scripts/parity_offsets.py 0xADDR` — "right callee, wrong member" (diffs binary's per-thiscall `this`-offset→member vs source; parity call-counts blind to it; found the 3-yr corpse-tint bug). `scripts/lint_extend_cells.py` = source-only twin for extend/`*Base` copy-paste.
- `scripts/arg_provenance.py 0xADDR|--sweep` — binary operand-ORDER audit at `operator+` sites (the swap class parity is order-blind to; found FindFileInDirectoryList dir/file).
- `scripts/lint_twin_symmetry.py [--aggressive]` — source-only antonym-pair operand swap (LHS `<side>`-named, RHS pulls the opposite side whose same-side twin exists; left/right, top/bottom, src/dst, ... default — x/y/z, r/g/b, min/max FP-prone → `--aggressive`). Parity- AND arg_provenance-blind (locals in `-`/`/`, not `operator+`). Found the FillConvexPoly `nRightAdjUp`→`nLeftAdjUp` swap (0x7C0F40); catches only ASYMMETRIC swaps (both-sides-wrong stays asm/runtime).
- **Binary-mirror class (`/* 0xNNN */` offset comments) MUST be `#pragma pack(2)`** — IE packs to 2; without it a sub-4-aligned `LONG`/`WORD` tail silently 4-aligns. Parity stays GREEN (layout-blind) — only a runtime/visual bug shows (the Cloudkill ring over-density). `python scripts/struct_layout_audit.py ClassName [--header f.h --source f.cpp]` flags ACTIONABLE pack(2) drift vs benign STL/`CVidCell`/out-of-order breaks; offsetof can't (MSVC C2618 on polymorphic classes).
- **Static parity ≠ runtime proof — arc not done until OUR build runs the path.** `scripts/vm.sh smoke [slot] [secs]` = our exe + save (default slot 3, auto-loaded) + crash oracle `frida_crash_guard.py` (symbolized EBP bt). Interactive TTY: holds until ENTER→`RESULT: CLEAN`. **From CC (non-TTY/backgrounded): run `scripts/vm.sh smoke 3 90` — it auto-ends `RESULT: CLEAN` after `[secs]` of no fault (default 90s when stdin not a TTY); do NOT pipe ENTER (ignored, non-TTY).** Verdict + bt land in `tmp_smoke_guard.log` (`EXCEPTION` line = crash); load status in `vm_s1_out.txt` (`loaded:` = save up, PEL ticking). Original-only Frida never exercises our exe (shipped both Fireball cast crashes past GREEN: abort 11ef54f6, UAF 1ac84b92). Behavioral claims → diff our-vs-original, same hooks.
- **Drive the UI yourself before asking the user.** `src/AutoUI.{h,cpp}` (test scaffolding, patterned on `AutoLoad.cpp`, inert unless `IWD2_RE_UI_SCRIPT` is set) replays a script from the tail of `CChitin::AsynchronousUpdate`. IWD2 handles no `WM_LBUTTONDOWN` — it polls `GetAsyncKeyState` and calls `pActiveEngine->OnLButtonDown/Up` with a screen point, so a synthetic click is those same two virtual calls. One primitive covers menus, action bar, portraits and ground; a control is addressed as `(panel, control)` and resolved to its centre, never as a pixel pair.
  - Run: `scripts/vm.sh smoke 3 60 --ui scripts/scenarios/save-menu.txt`. Composes with `--hit`: "the scenario clicked X" + "symbol Y fired" is what proves a UI action reached code. Output → `tmp_ui_result.jsonl` (one JSON per step). Exit 2 adds `UI-FAIL` / `UI-NO-RESULT` / `UI-INCOMPLETE`.
  - Ops: `wait <screen> [ticks]` · **`waitgo`** · `dump` · `screen` · `click <panel> <ctrl>` · `clickxy <x> <y>` · `key <vk>` · `goto <screen>` · `expect screen|controls|control` · `sleep <ticks>`.
  - **Always `waitgo` before the first action** — the crash guard only attaches after the save reports `loaded:`, so an earlier click is invisible to `--hit` (this cost three runs; measured 616 ticks of wait).
  - `dump` is the diagnostic: it answers "does this screen actually have controls, and are they active" without reading pixels. That is how the empty save menu was traced to `CScreenSave::RefreshGameSlots`.
- **A bare `RESULT: CLEAN` only means "no fault while idle" — it does NOT prove the recovered code ran.** Gate on it: `vm.sh smoke 3 90 --hit Class::Method` counts entries via the guard's PDB symbols (no source change) and exits **2** on `NOT-EXERCISED` (never entered) or `NOT-INSTRUMENTED` (symbol resolved to nothing — inlined / `/OPT:ICF`). `--expect REGEX` greps the debug log instead, for the deliberate-`Iwd2DebugLog` flow. `arc verify` passes `--hit` automatically from the symbol it resolved. Exit 2 = "ran clean, proved nothing" → arc reports WARN, never a plain PASS.
- End every recover session by recommending the next function(s): `scripts/arc.py targets` (ranked; `--near CClass` for one class), then refresh `docs/next-targets.md` with `scripts/next_targets.py --write`. **Real remaining scope is ~1.6k functions, not ~18k** — `_index.json`'s 29,664 entries include ~19.6k `Unwind@`/`Catch@` SEH funclets, so the ~37% in README/`project_status.py` is measured against a denominator that is two thirds compiler plumbing. `arc status` prints both.

## No hacks

Code must match `IWD2.exe`. No invented behavior.
- Can't recover → leave unimplemented (return early / no-op). Missing better than wrong.
- Unavoidable hack → `// HACK: <what> — <why> — replaces 0x<addr>`. Not `// TODO`.
