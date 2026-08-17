# Session ledger

**The last session is the top row.** Several sessions land on the same day, so
the date does not identify one — the tag does.

One command, no reading required:

```bash
git tag -l 's[0-9]*' --sort=-v:refname | head -1     # -> the latest session tag
git show --stat $(git tag -l 's[0-9]*' --sort=-v:refname | head -1)
```

Each session ends by tagging its final commit `sNN` and adding a row here.
Numbering matches the `UPDATE NN` blocks in the `iwd2-re-combat-gap` memory
file, which carries the full detail; this table is only the index.

| # | tag | date | HEAD at end | pushed | headline |
|---|-----|------|-------------|--------|----------|
| 29 | `s29` | 2026-08-16 | `35c1630d` | no | The sweep's only INCOMPLETE was the tool's own bug, not a second Power Attack. Three more false-positive classes fixed (case-group brace scoping, Ghidra's end vs *undefined* functions, C escape literals). One real finding: `CGameAnimationTypeMonster::SetSequence` sequences 8–13 hit the `default:` assert — recovered — plus an `m_currentVidCellWeaponBase` member swap it exposed. First live proofs of `CGameDoor::CompressTime` (5 of 6 doors, sub-hour delta closed none) and `CheckModal` case 0 (bard song). Sweep 522/28 → 515/24. |
| 28 | `s28` | 2026-08-16 | `e1144974` | yes | `CheckFeatPrerequisites` COMPLETE (MERCANTILE_BACKGROUND, SNAKE_BLOOD were the last two stubs). New tools `jumptable_audit.py` (seven false-positive classes) and `save_party.py`. Both ported SUMMON blocks runtime-proven with a sprite census. |

Sessions before 28 are not numbered here — their record lives in the
`iwd2-re-combat-gap-archive` memory file (UPDATE 1–43). Do not backfill this
table by guessing which commits belonged to which session; the mapping was
never recorded and several sessions share a day.

## Ending a session

1. `git tag -a sNN -m "<one-line headline>"` on the final commit.
2. Add the row above, newest first.
3. Set the `pushed` column honestly — `git log --oneline origin/main..HEAD`.
4. Update the `iwd2-re-combat-gap` memory's `description:` so it names the same
   session number and HEAD.

Tags are local until pushed: `git push origin sNN` (or `git push --tags`).
