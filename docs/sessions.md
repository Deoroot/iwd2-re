# Session ledger

**The last session is the top row.** Several sessions land on the same day, so
the date does not identify one — the tag does.

One command, no reading required:

```bash
git tag -l 's[0-9]*' --sort=-v:refname | head -1     # -> the latest session tag
git log --oneline s28..s29                           # -> everything session 29 did
git rev-parse 's29^{commit}'                         # -> its final commit
```

Note the `^{commit}`: these are ANNOTATED tags, so a bare `git rev-parse s29`
returns the tag object's sha, not the commit's, and will not match `HEAD`.

There is deliberately no commit column below: the tag *is* the commit, and a
column holding it could only ever be one commit stale — the row naming a hash
has to be written before the commit that contains it exists.

Each session ends by tagging its final commit `sNN` and adding a row here.
Numbering matches the `UPDATE NN` blocks in the `iwd2-re-combat-gap` memory
file, which carries the full detail; this table is only the index.

| # | tag | date | pushed | headline |
|---|-----|------|--------|----------|
| 33 | `s33` | 2026-08-29 | yes | **`CScreenCharacter::UpdatePopupPanel` dispatched ONE popup of twenty, so every character-screen popup but Information went un-updated -- no Done button re-enabled, no scroll bar re-bound.** The binary's table (0x5E0DC0, index 0x5E0E14, 54 values from 4) has twenty real arms and thirty-two that fall to the default assert; the source had one `case`. Most arms tail-dispatch to an `Update*Panel` that already existed, but FOUR did not, which is what made this an arc rather than a one-liner: 0x5DAF30 `UpdateInformationPanel` was already here INLINED into `case 57` and the binary CALLS it, so it moved out; 0x5E87E0 `UpdateFeatsPanel`, 0x5F7B00 `UpdateSkillsPanel` and 0x5DB200 `UpdateRecordsPanel` are new recoveries. The feats/skills pair is NOT symmetric -- opposite HP save/restore ordering, different enable predicates (`CanSelectFeat && m_nExtraFeats > 0` vs `nCost != 0 && m_nExtraSkillPoints >= nCost`), skills calls `GetSkillId` TWICE in its label loop and once in its button loop, and only skills ends a row with a three-way colour -- so mirroring one onto the other would have shipped a twin swap. `case 20` calls `GetControl(6)` BEFORE testing the panel for NULL, which is the byte order, not a slip. `UpdateRecordsPanel` has THREE CString locals, not four: the epilogue has exactly three destructors and the time string and favourite-spell name are the same slot. Cases 9/10/50 are real empty arms that jump past the assert. RUNTIME-PROVEN: portrait -> `goto character` -> click opened panels 57 and 13 (`active:0 -> 1`) with no crash, which is what mattered because the faithful `UTIL_ASSERT(FALSE)` default is newly fatal where the old default was a bare `break;`. jumptable_audit 0x5E0B20 CLEAN (20 arms, was 1); parity GREEN on all five. Sweep 515: CLEAN 437 -> 438, MISSING 3 -> 2, everything else unchanged. TRIGGER.IDS recon for the last big arc: 182 names 0x400A..0x40F8, base 16394 = 0x400A = `Alignment`, and exactly ONE duplicated id in range (0x4039, a harmless alias). |
| 32 | `s32` | 2026-08-29 | yes | **The sweep's last three INCOMPLETE arms are gone, and so is every DEFAULT: CLEAN 423 -> 437, MISSING 34 -> 3, INCOMPLETE 3 -> 0, DEFAULT 9 -> 0, OUT-OF-RANGE 24 -> 10.** Four real recoveries. `CUIControlEdit::OnKeyDown` held all three INCOMPLETE -- the DBCS bodies of Backspace, Return's history shift and Left -- plus a missing `VK_ESCAPE` arm that restores `m_sOriginalText` and kills capture unconditionally. `CUIControlButtonInventorySlot::Render` never named an empty slot: its fused table sets a STRREF beside each STON* placeholder, and the single exit passes it to `SetToolTipStrRef` where we passed -1. Runtime-proven live -- 22 and 23 report "Left Ring" and "Right Ring", so the SPLIT was real, and the three slots holding items show the ITEM's description. `CAIObjectType::OfType` handled the eleven single classes and none of CLASS.IDS' eight groups, so `Class(x, MAGE_ALL)` matched nothing; the masks name them 8 for 8 against a rival BG2-era sequence. Plus `OnActionButton`'s `case -1` on a BYTE (dead code; the binary means 255) and a keymap arm. Three more false-positive classes (20-22) cleared 28 findings across four functions with NO source change: a reloaded index register carries no bias, a class-scope `const CClass::NAME = OTHER;` has to resolve, and the right inlined callee among namesakes is the one that contradicts the table nowhere. |
| 31 | `s31` | 2026-08-17 | yes | **The party-move bug the user reported is fixed and runtime-proven.** All three `CAIGroup` formation entry points asked for `GetArea(0)` in sixteen places where the binary asks for two other things: `GetVisibleArea()` for the search add/remove, and the SPRITE'S OWN `m_pArea` for `AdjustTarget`. `m_visibleArea` is 0 only until the party first transitions, which is exactly why the bug appeared after an area change and never with one character selected — that path calls `AdjustTarget` at all. A live trace caught both states in one run: `area0 == visible` before the transition, `area0` still the OLD area after it. Also `CResBIO` (AllocResObject's last three arms, name taken from BG2's `CResHelper<CResBIO,1022>`), three real switch arms in the inventory/create-char screens, and four more `jumptable_audit` false-positive classes (16–19): CLEAN 392 → 423, MISSING 71 → 34, twelve functions cleared, three newly visible. |
| 30 | `s30` | 2026-08-16 | yes | `jumptable_audit` now DERIVES the table↔switch pairing: AMBIGUOUS-PAIRING 181 → 59, CLEAN 268 → 392, OUT-OF-RANGE 53 → 13. Five more false-positive classes (11–15), the largest being that an inlined callee's table is diffed against the callee's own recovery via `// NOTE: Uninline.`. Two real defects out of the UI triage: `CScreenWorld::CancelPopup` called `StopDeath` where the binary calls nothing (and `CancelEngine` did it for case 15, which asserts) — both missing `case 22`; and icon index 20 had no arm in `CGameDoor::SetCursor` or `CGameTrigger::SetCursor`. All four parity GREEN. |
| 29 | `s29` | 2026-08-16 | yes | The sweep's only INCOMPLETE was the tool's own bug, not a second Power Attack. Three more false-positive classes fixed (case-group brace scoping, Ghidra's end vs *undefined* functions, C escape literals). One real finding: `CGameAnimationTypeMonster::SetSequence` sequences 8–13 hit the `default:` assert — recovered — plus an `m_currentVidCellWeaponBase` member swap it exposed. First live proofs of `CGameDoor::CompressTime` (5 of 6 doors, sub-hour delta closed none) and `CheckModal` case 0 (bard song). Sweep 522/28 → 515/24. |
| 28 | `s28` | 2026-08-16 | yes | `CheckFeatPrerequisites` COMPLETE (MERCANTILE_BACKGROUND, SNAKE_BLOOD were the last two stubs). New tools `jumptable_audit.py` (seven false-positive classes) and `save_party.py`. Both ported SUMMON blocks runtime-proven with a sprite census. |

Sessions before 28 are not numbered here — their record lives in the
`iwd2-re-combat-gap-archive` memory file (UPDATE 1–43). Do not backfill this
table by guessing which commits belonged to which session; the mapping was
never recorded and several sessions share a day.

## Ending a session

1. Add the row above, newest first, with the `pushed` column set honestly
   (`git log --oneline origin/main..HEAD`).
2. Commit it — that commit is the session's last.
3. `git tag -a sNN -m "<one-line headline>"` on it.
4. Update the `iwd2-re-combat-gap` memory's `description:` so it names the same
   session number.

Tags are local until pushed: `git push origin sNN` (or `git push --tags`).
