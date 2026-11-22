# Test saves made with the original's cheat console

Saves in `C:\Juegos\Icewind Dale 2\MPSave` that reach later areas. They are
written by the ORIGINAL IWD2.exe, so they are not our build's output. Every one
is a new folder; no existing save was modified.

| Folder | Area | Made from | Loads in our build |
|---|---|---|---|
| `000000014-S57 AR5200 Monastery` | AR5200 (Black Raven Monastery, exterior) | `000000007-Staircase Transition` | **no**: AV in `CVidCell::Blt8To32`, rendering a `CGameAnimationTypeMonsterQuadrant` (pre-existing, s57) |
| `000000015-S57 AR6300 Severed Hand` | AR6300 (Severed Hand) | `000000007-Staircase Transition` | yes |

## How the console works (verified in the bytes, s57)

- `[Game Options] Cheats=1` in `icewind2.ini`. `CInfGame::LoadOptions` reads it
  at 0x5A9CB1 into `CInfGame+0x446E` and never writes it back.
- CTRL+TAB in the world screen (0x687A2E, which needs that flag and the CTRL
  key) toggles `ScreenWorld+0xF37` and calls `StartCommand` (0x691140).
- The line goes through `CUIControlEditLua::OnEditReturn` (0x77BF30) to the
  parser 0x5838A0: `CtrlAltDelete:Command(args)`, case-insensitive. Its
  keywords are stored encoded as `(c+8)*2` (table 0x8AF630), which is why
  `ctrlaltdelete` is not a plain string in the exe.
- After ENTER the edit loses focus: a second ENTER closes the console
  (`StopCommand`). On reopening, the edit keeps the previous line, and a
  paste is appended to it. Use ONE command per run, starting from an ini with
  no `[Lua Edit]` history.
- `MoveToArea("ARxxxx")` sends the selected party to (0,0) of that area, which
  lands on the nearest walkable point.

## Recipe (unattended, `frida_orig.py --key`)

1. Back up `icewind2.ini` byte for byte, then change only `Cheats=0` to
   `Cheats=1`. Do it with Python on the bytes: Git Bash `sed -i` rewrites the
   file as LF.
2. Make sure no `000000002-Quick-Save` exists (Q saves there, and
   `000000001-Quick-Save-Backup` is created next to it).
3. Run:

   ```
   python scripts/frida_orig.py --hooks <any spec> --out c:/tmp/x.jsonl \
     --load-name "000000007-Staircase Transition" --post-load 200 --timeout 800 \
     --key "=" --key "^TAB" --key 'clip:CtrlAltDelete:MoveToArea("AR6300")' \
     --key wait:30 --key "^V" --key wait:30 --key ENTER --key wait:30 \
     --key ENTER --key wait:1800 --key Q --key wait:900
   ```

   `=` selects the whole party. `clip:` puts the line on the clipboard, and
   `^V` pastes it (CUIControlEdit::OnKeyDown, 0x4D7559). The `wait:30` before
   `^V` matters: without it, one run in three pasted before the edit had the
   keyboard.
4. Rename `000000002-Quick-Save` to the next free number and delete the
   (empty) `000000001-Quick-Save-Backup`. AR5200 also writes a chapter
   autosave (`000000000-Autosave - Chapter Four`) as a NEW folder; delete it.
5. Restore `icewind2.ini` from the backup and check it with `cmp`.

`SetCurrentXP(n)` works the same way (it sets, not adds, XP for the selected
members). No XP was given to the two saves above.
