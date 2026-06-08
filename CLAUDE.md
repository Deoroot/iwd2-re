# CLAUDE.md

## Repo

`src/` = hand-recovered C++ matching `IWD2.exe`. Ghidra = truth.

## code-review-graph MCP — use before Grep/Glob/Read

**RULE: searching `src/` or `refs/gemrb/` → graph tools first.**

- iwd2-re graph (`src/`): default `repo_root`. Alias `iwd2`.
- GemRB graph: `repo_root="C:\iwd2-re\refs\gemrb"`. Alias `gemrb`.
- Both: `cross_repo_search_tool` searches both simultaneously.

Allowed without graph: `tmp_*.txt`, `data/`, `scripts/`, `.ghidra-exports/`, raw binaries.

| Want | Tool |
|------|------|
| Find fn/class by name | `semantic_search_nodes` |
| Callers of X | `query_graph pattern=callers_of target=X` |
| Callees of X | `query_graph pattern=callees_of target=X` |
| File contents tree | `query_graph pattern=file_summary target=path` |
| Blast radius | `get_impact_radius` |
| Exec paths hit | `get_affected_flows` |
| Free traversal | `traverse_graph` |
| Quick repo stats | `get_minimal_context` |
| Review diff | `detect_changes` → `get_affected_flows` |
| GemRB lookup | same tools + `repo_root="C:\iwd2-re\refs\gemrb"` |
| Cross-repo search | `cross_repo_search_tool` |

Workflow: `semantic_search_nodes` → `query_graph callers/callees` → Read only specific lines from result.

**Query:** one bare identifier token. Retry shorter before Grep.

## Build & run (Win32, VS 2019)

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32
taskkill //f //im iwd2-re.exe 2>/dev/null || true
cmake --build build --config Debug
rm -f "C:/GOG Games/Icewind Dale 2/iwd2-re-crash.log" "C:/GOG Games/Icewind Dale 2/iwd2-re-debug.log" && cp "C:/iwd2-re/build/Debug/iwd2-re.exe" "C:/GOG Games/Icewind Dale 2/" -f
python scripts/auto_start_game.py   # smoke: load slot 0, wait for world
python scripts/auto_start_game.py --new-game
```

Commits must compile VS2019 Win32. Rename → update `.h` + ALL `.cpp` in one commit. Build fail → stop.

## Ghidra = truth

Don't invent code. Check Ghidra first. `// 0xADDR` can be stale. Ghidra wins.
Address not in funcs table → check vtable DATA xref (virtual method).

## Ghidra access — ghidra-bridge (no GhidraMCP)

GhidraMCP (HTTP `:8089`) is **retired**. Ghidra data now comes from **ghidra-ai-bridge**:
a PyGhidra **headless export** into `.ghidra-exports/` (not a live server). Toolchain lives
in the `.venv-reagent` venv; config is `ghidra-bridge.yaml` + `re-agent.yaml` (repo root).
pip installs for this toolchain are blocked for the agent → user runs them via the `!` prefix.
Branch: `re-agent-workflow`.

**(Re)build the export** — when Ghidra renames/types change or the dump is stale.
**Close the Ghidra GUI first** (headless takes the project's exclusive lock):

```bash
.venv-reagent/Scripts/python scripts/reagent_address_map.py   # // 0xADDR -> address_map.json + hooks.csv (~7944)
.venv-reagent/Scripts/ghidra-bridge --config ghidra-bridge.yaml export all   # read-only dump, ~28k fns
```

`export all` is read-only. NEVER run `create-functions`/`fix-all` without first backing up
`IWD2.rep` — they write the Ghidra DB (`createFunction`) and a `.corrupted` rep already exists.

**Query** (`gb` = `.venv-reagent/Scripts/ghidra-bridge --config ghidra-bridge.yaml`):

| Want | Cmd |
|---|---|
| Decompile + caller/callee counts | `gb decompile 0xADDR` |
| Callers / callees | `gb xrefs-to 0xADDR` / `gb xrefs-from 0xADDR` |
| Disassembly (boots PyGhidra, ~1 min) | `gb dump-asm 0xADDR out.asm` (writes a file; not `asm`) |
| Struct / vtable | `gb struct CClass` / `gb vtable CClass` |
| Crash addr → containing fn | `gb containing 0xADDR` |
| Unrecovered (by caller count) | `gb unimplemented CClass` |

**Parity** = faithfulness lint of recovered C++ vs Ghidra (11 signals + call-count/control-flow
objective verifier). Run from `C:\iwd2-re` so the bridge finds the yaml via cwd:

```bash
.venv-reagent/Scripts/re-agent --config re-agent.yaml parity --address 0xADDR
.venv-reagent/Scripts/re-agent --config re-agent.yaml parity --filter "CClass::" --output tmp_parity.json
```

GREEN/YELLOW/RED per fn. `asm`-based signals (fp_sensitivity, large-asm-tiny-source) read
`dump-asm`, which boots PyGhidra headless (~1 min/call) — heavier than the cache-read signals.

⚠️ `re-agent reverse` (LLM whole-file codegen loop) is **NOT validated here** and fights
minimal-diff + faithfulness. Never commit its output unverified. Use `parity` as a lint, not
`reverse` as an author; gate any behavioral claim with a Frida diff.

**Read PE bytes (use pefile):**
```python
import pefile
pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True)
ib = pe.OPTIONAL_HEADER.ImageBase
print(pe.get_data(0x8ABCA4 - ib, 16))
```

## Frida tracing

Don't hesitate to write a throwaway Frida trace to confirm/investigate field semantics live in-game before a recover — ground truth beats guessing.

Hook original `IWD2.exe` at runtime, diff against `Iwd2DebugLog`.
Docs: `docs/frida-differential-tracing.md`. Template: `scripts/frida_formation_trace.py`.
- `ptr(0xADDR)` absolute (no ASLR, ImageBase `0x400000`). `__thiscall`: `this` = `this.context.ecx`, stack args = `args[0..]`.
- Hook function ENTRIES only. Mid-function hooks crash.
- Runtime wins when `__thiscall` args disagree with decompiler.
- Read PE constants with `pefile`, not memory endpoint.
- Preferred input-driving order: Frida-RPC into the engine fn > keyboard > `PostMessage` > polite physical hijack (`BlockInput`+restore cursor, unattended only). IWD2 polls the mouse from the engine tick, so synthetic clicks can drop — physical is sometimes the only thing that registers. No cursor hijack while the user is at the machine.
- Hedron revisit trace: kill old `iwd2-re/IWD2` first; 800x600 client click is `[500,250]` (world around `[2513,906]`, obj `524296`).
- For mouse automation, hover first and prove `CGameSprite::IsOver` or `CGameArea::OnActionButtonDown picked=524296`; raw screenshots alone misled upward.
- RE dialog replies are flaky with marker-only RPC; use `responseMarker` plus a real/post click on the visible reply line.

## Game assets (`data/near_infinity_export/`)

Extracted via NearInfinity. Use for game content questions (action IDs, spell IDs, scripts, UI layouts).

```
data/near_infinity_export/
├── 2DA/         rule tables (QSLOTS, XPLEVEL, ANIMATE, etc.)
├── ARE/         areas — actors, doors, triggers, spawns
├── BAM_DECOMP/  decompressed BAMv1 (read frames with struct.unpack)
├── BCS/         creature scripts (exported as .BAF)
├── BS/          player scripts (.BAF)
├── CHU/         UI panels (button frames, control ids)
├── CRE/         creature templates
├── DLG/         dialogue trees
├── EFF/         standalone effects
├── IDS/         symbol→id maps (ACTION, TRIGGER, OBJECT, STATS, SPLSTATE)
├── ITM/         items
├── SPL/         spells
├── STO/         stores
├── WED/         tile maps
└── WMP/         world map
```

| Question | File |
|---|---|
| Action id N? | `IDS/ACTION.IDS` |
| Trigger id N? | `IDS/TRIGGER.IDS` |
| Stat/state name? | `IDS/STATS.IDS`, `IDS/SPLSTATE.IDS` |
| Spell SPxxx? | `SPL/SPxxx.SPL` |
| Button slot? | `CHU/<panel>.CHU` |
| QSlot layout? | `2DA/QSLOTS.2DA` |
| Area edges? | `WMP/WORLDMAP.WMP` |
| Script usage? | grep `BCS/*.BAF` + `BS/*.BAF` |

## External refs

| Path | Use |
|-----|-----|
| `data/pdb/bg2_pdb_types.txt` | BG2EE PDB layouts (field names match, offsets differ) |
| `refs/gemrb/` | GemRB source  → use CRG graph (`repo_root="C:\iwd2-re\refs\gemrb"`) |
| `refs/NearInfinity/` | File formats (.CRE/.ARE/.ITM) |
| `refs/iesdp/` | Effects, opcodes, STATS.IDS |
| `C:\ghidra_projects\IWD2\` | Ghidra project (install `C:\ghidra_dist\ghidra_12.1_PUBLIC`). CLOSE the GUI before `ghidra-bridge export`. |

## Temp files

`tmp_*.txt`, `tmp_*.json`, `chunk_*.sql` = RE session noise. Not tracked. Delete freely.
`.venv-reagent/`, `.ghidra-exports/` = re-agent toolchain + export cache. Gitignored.

## Code changes

- Verify `// 0xADDR` against Ghidra before touching.
- Minimal diffs. One bug = one change. No refactor in bugfix commits.
- Prefer named constants over magic numbers when defined in file.
- `python scripts/vtable_audit.py ClassName` — catches missing virtual overrides.
- After a recover, `re-agent parity --address 0xADDR` should be GREEN/YELLOW (RED = under-implemented vs Ghidra).

## No hacks

Code must match `IWD2.exe`. No invented behavior.
- Can't recover → leave unimplemented (return early / no-op). Missing better than wrong.
- Unavoidable hack → `// HACK: <what> — <why> — replaces 0x<addr>`. Not `// TODO`.
