# Recovery instructions

Recover playable IWD2 engine behavior in `src/`, matching the canonical binary.
Optimize for useful recovered C++, not the number of probes, cases or reports.
These are the current workflow rules. Read the linked technical references only
when the current task needs them; do not reload all documentation on each `go`.

## Non-negotiable

- Do not invent behavior. PE bytes/ASM and original runtime observations outrank
  the decompiler, other ports and names. Leave an unresolved branch explicit.
- Preserve original `// 0xADDR` markers, calling conventions, signed widths,
  ownership, RNG consumption and engine packing (`#pragma pack(2)`).
- Preserve existing user changes. Update declarations and callers with a rename.
  Never edit C++ during a build. A failed build blocks runtime validation.
- Use one agent unless the user explicitly requests delegation. No external LLM
  API calls or usage-reset redemption without explicit user authorization.
- Never overwrite the original executable or test by saving over a user's save.
  Use an existing reversible profile for loaded-world fixtures. Stop only this
  checkout's game, plus original/private processes launched for the current test.
- No new `Iwd2DebugLog` instrumentation in a dirty checkout; prefer existing typed
  AutoUI checks. Never read our build's members using original-binary offsets.

## Default recovery loop

1. Choose one playable behavior and its necessary dependencies. Recover a
   cohesive batch, usually 5-10 functions when justified, rather than stopping
   after a few lines. Do not reopen already settled binary questions.
2. Start with `src_find.py` and `fn_digest.py`. Assemble context once for the
   batch; read complete target bodies, required calls and relevant layouts.
   Reuse unchanged shared context already read. Check ambiguous ABI, offsets,
   widths, ordering and ownership in ASM; trace only what static evidence cannot
   settle. Do not dump unrelated class headers or entire large logs repeatedly.
3. Write the C++ first. Reuse existing fixtures, scenarios and comparison tools.
   A new probe needs a concrete unresolved behavior or regression to justify it;
   no new reference/compare Python pair or exhaustive input grid by default.
4. Validate with `arc batch` and an existing suitable scenario/profile. Keep its
   gates and save restoration intact. Build once and prove the changed path ran.
   Add a small targeted check when existing coverage cannot observe the change.
   Use the smallest scenario covering the changed behavior and its immediate
   dependencies. For chargen spells, use `scripts/profiles/chargen-spells.json`.
   Do not automatically append every new check to `combat-messages.txt` or rerun
   its full integration suite for a local UI change. Keep new checks in their
   focused domain; split an oversized fixture when it prevents focused coverage.
   Run the full combat suite for changes affecting shared combat behavior,
   cross-domain integration, a suspected regression or an explicit milestone.
   Use paired original/reconstruction captures for uncertain rules/RNG/ownership
   or an actual equivalence claim; otherwise say what remains unproven.
5. Inspect the compact verdict; open detailed output only to diagnose a finding.
   After failure, rerun the affected gate when possible; retain the failed record.
   Do not rerun passed expensive checks without a relevant change or concern.
6. Commit a meaningful behavior with relevant validation, push periodically,
   and leave the next action. Use the generated `.arc/.../continuation.md` or
   5-10 lines in the commit body; no new narrative document per recovery lot.
   Refresh the graph after C++ changes; refresh target rankings when choosing a
   new direction, not as a mandatory report for every batch.

## Local commands

On Windows use PowerShell, `.\.venv-reagent\Scripts\python.exe`, VS2019/v142
Win32, and `scripts/native.py` (`arc.py` selects it automatically). Assets default
to `C:\Games\Icewind Dale 2`; build to `build/Debug/iwd2-re.exe`.
On Linux use `.venv-reagent/bin/python` and the existing `scripts/vm.sh` route.

| Need | Command (prefix Python scripts with the venv executable) |
|---|---|
| Source definition/body | `scripts/src_find.py NAME --body` |
| Binary overview | `scripts/fn_digest.py 0xADDR` |
| Batch context | `scripts/reagent_assemble_context.py --address 0xA --address 0xB --out tmp_context.md` |
| ASM, bytes, vtable, crash | `scripts/sym.py --help` then its relevant subcommand |
| BG2 names/layout | `scripts/bg2_find.py NAME` |
| Focused recovery gate (chargen spells) | `scripts/arc.py batch 0xA 0xB --profile scripts/profiles/chargen-spells.json` |
| Combat integration gate | `scripts/arc.py batch 0xA 0xB --profile scripts/profiles/combat-messages.json` |
| Logs for one failed gate | `scripts/arc.py explain STEP --run RUN_ID` |
| Build / scenario / processes | `scripts/native.py build`, `smoke`, `status`, `ps`, `kill ours` |
| Next candidates | `scripts/arc.py targets --near CLASS --top 10` |

Use `src_find` before grepping `src/`. Use existing PE/ASM/map CLIs rather than
ad hoc pefile/capstone scripts. Do not run `re-agent reverse` as a code author.
Never use `gb build-map`; use `scripts/reagent_address_map.py` for source mapping.
Static warnings, missing tools, runtime hits and paired comparisons are separate
facts: CLEAN without a hit does not prove recovery; agreement with a port that
copied our code is not independent evidence.

## On-demand references

- [Cost-conscious workflow and iwd2r ideas](docs/economical-recovery.md).
- [Technical commands, binary audits, Frida and profile cleanup](docs/recovery-reference.md).
- [Windows setup](docs/windows-native-workflow.md).
- [Trace comparison limits](docs/frida-trace-comparison.md).

## This fork (Deoroot/iwd2-re) — additions kept across the upstream sync

- **Last session / where it left off**: `git tag -l 's[0-9]*' --sort=-v:refname | head -1`,
  then the top row of `docs/sessions.md`. Several sessions share a day, so the date does
  NOT identify one — the `sNN` tag does. End every session by tagging and adding its row.
- **How much is left, per subsystem**: `scripts/progress_badges.py` (table) / `--write`
  (README badges) / `--classes` (regroup via `scripts/progress_groups.json`, which also
  holds `library_start` and the zlib `library_ranges`). Fidelity needs
  `scripts/parity_cache_sweep.py --skip-concat-swap --json docs/badges/parity.json`.
- **Small mechanical questions from a disasm dump, in one call**: `scripts/sym.py whatis
  TOK...` — mixes `0xADDR`, `CClass+0xNNN`, `vtable:0xADDR`, `data:0xADDR`. Batch them.
- **A function marker is `// 0xADDR` at COLUMN 0 in a .cpp** (any indent in a header).
  An indented `// 0xADDR` inside a body is a NOTE; the map, hooks.csv, `project_status`
  and `lint_address_markers` skip it (`reagent_address_map.is_body_note`).
  `scripts/ghidra_remove_body_notes.py` finds phantom Ghidra functions made from notes.
- **Layout without a VM**: `scripts/layout_offsets.py CClass` / `--tree CBase [--sizes]`
  asks the local compiler where each `/* 0xNNN */` member sits and diffs it against the
  comment. A comment is a claim: settle a mismatch in the constructor's disassembly. A
  derived class WITH data members needs its own `#pragma pack(2)` even when its base has
  one. In a Debug build `CCriticalSection` is 0x24, not 0x20, so members after one sit +4.
