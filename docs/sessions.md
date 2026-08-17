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
| 31 | `s31` | 2026-08-17 | no | **The party-move bug the user reported is fixed and runtime-proven.** All three `CAIGroup` formation entry points asked for `GetArea(0)` in sixteen places where the binary asks for two other things: `GetVisibleArea()` for the search add/remove, and the SPRITE'S OWN `m_pArea` for `AdjustTarget`. `m_visibleArea` is 0 only until the party first transitions, which is exactly why the bug appeared after an area change and never with one character selected — that path calls `AdjustTarget` at all. A live trace caught both states in one run: `area0 == visible` before the transition, `area0` still the OLD area after it. Also `CResBIO` (AllocResObject's last three arms, name taken from BG2's `CResHelper<CResBIO,1022>`), three real switch arms in the inventory/create-char screens, and four more `jumptable_audit` false-positive classes (16–19): CLEAN 392 → 423, MISSING 71 → 34, twelve functions cleared, three newly visible. |
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
