---
name: todo-action-bar
description: Remaining work to bring CInfButtonArray + CUIControlButtonAction to 100% Ghidra parity. Picker states (0x65-0x6B etc.) currently show a placeholder formation-button fallback because the dynamic spell/item list builder isn't ported yet.
metadata:
  type: project
---

# Action bar — remaining work

Tracker. Update after each milestone.

## Done so far (commits 1b64c398 → cdee8f24)

### Visual parity (commit e9145a7c + 68c6a5fa + 616a9e28)
- Quick-slot item icons use BAM cycle 1 (small 26×27 / 32×32) instead of cycle 0 (large inventory 53×43 / 64×64). Cases 0x3C-0x43 / 0x46-0x4E / 0x50-0x52 / 0x5A-0x62 / 0x6E-0x76 set `nIconSequence = 1` when `buttonData.m_icon` is populated. Sequence stashed in `settings.field_1C8` so `RenderButton` re-applies on frame swap.
- Formation buttons (0x10-0x14 + 0x15-0x20) set `bHasOverlay = FALSE` → GUIBTBUT stone bezel paints under FORMx icon.
- Inactive slot (`field_0 == 0`) skips every paint path in `CUIControlButtonAction::Render` → group state 0x6E slots 8-11 (type 100) stay transparent so GACTN008 BG shows through.
- GUIBTACT (full 38×38, baked bezel) paints at `pt + 0`; STON* / FORM* / item (32×32) paints at `pt + 3` centred inside the bezel. Fixed crop on right/bottom edges.
- HIGHLGHT BAM green ring drawn when `m_bActiveWeaponSet != 0 && m_bSelected == 0`.

### Coverage (commit 6bf25346)
- `UpdateButtons` covers types 2 / 3 / 4 / 5 / 7 / 8 / 9 / 0xB-0xF / 0x10-0x14 / 0x15-0x20 / 0x21 / 0x22 / 0x23-0x2A / 0x32-0x39 / 0x3C-0x43 / 0x46-0x4E / 0x50-0x52 / 0x5A-0x62 / 0x6E-0x76 / 0x77 / 100. Empty quick slots keep `bActive = TRUE` with STON* fallback.
- `SetState` covers 0x65-0x6B / 0x70-0x71 / 0x7A / 0x7B (picker fallback = formation-button placeholder), 0x6C / 0x6D (formation picker), 0x6E (group), 0x6F (empty), 0x72 (single PC), 0x73 / 0x74 (action submenu), 0x75 (customize), 0x76 / 0x77 (class picker), 0x78 (quick-item picker), 0x79 (quick-weapon picker).
- `OnLButtonPressed` default case covers 2 / 3 / 4 / 5 / 7 / 8 / 0xB / 0xC / 0xE / 0xF / 10 / 0x32-0x38 / 0x3C-0x43 / 0x46-0x4E / 0x50-0x52 / 0x5A-0x62 / 0x6E-0x76 / 0x77. State 0x6E + 0x6C / 0x6D state-specific paths.
- `OnRButtonPressed` is the full state dispatcher (0x6E → 0x6C, 0x72 weapon → 0x79, 0x72 other → 0x75, 0x75 → various, 0x77 → 0x66).
- `m_bActiveWeaponSet` set when slot pair matches `pSprite->m_nWeaponSet` → drives green HIGHLGHT ring.

### Refactor (commit bc1cc153)
- `CInfButtonSettings` field renames: `field_4` → `m_bActive`, `field_8` → `m_bHasOverlay`, `field_C` → `m_nIconNormalFrame`, `field_10` → `m_nIconSelectedFrame`, `field_1CC` → `m_bSelected`, `field_1D0` → `m_bActiveWeaponSet`, `field_1D8` → `m_nCount`.

## Remaining

### Picker dynamic list (high effort — blocks most picker submenus)

`FUN_00587c20` (at 0x587c20) builds a `CGameButtonList` from sprite spells / items / abilities / songs filtered by class + level (`m_nCurrentSelectedSpellClass`, `m_nCurrentSelectedSpellLevel`). Dispatch by `param_2`:

- 1 = weapon usages (`CGameSprite::GetItemUsages` or `FUN_00717250`)
- 2 = spell list (`FUN_007155c0` per class+level, or `FUN_00714f70` for class)
- 3 = item usages (`CGameSprite::GetItemUsages` or `FUN_00716e80`)
- 4 = abilities (`FUN_00715bd0`)
- 5 = internal buttons (`CGameSprite::GetInternalButtonList`)
- 6 = songs (`CGameSprite::GetSongsButtonList`)

Returned list is iterated; when length ≤ 12 the picker buttons get types 0x15-0x20 with per-slot icon/resref written into `field_14`. When > 12 it switches to paging buttons 0x21 / 0x15-0x1F / 0x22 (DAT_008e6820 stores the paging offset).

To port: add a `CGameButtonList* m_pPickerList` plus `int m_nPickerPage` on `CInfButtonArray`. SetState 0x65-0x6B / 0x70 / 0x71 / 0x7A / 0x7B / 0x65 builds it; UpdateButtons handles the per-slot icon override; OnLButton state-specific handlers consume the click. Spell-icon lookup: `CInfGame::m_spells.Get(nID)` → resref → `CSpell::Demand` + `CSpell::GetIcon`.

### State-specific OnLButtonPressed handlers

Currently the picker states route to the default-case button-type handler, which is wrong for picker layouts.

- 0x65 (weapon equip pick) — click 0x15-0x20 calls `CGameSprite::EquipWeapon` then back to 0x72.
- 0x66 / 0x67 (spellbook pick) — `CGameSprite::Cast` for the indexed spell.
- 0x68 / 0x69 (item ability pick) — `FUN_005884b0` (item-ability use helper).
- 0x6A / 0x6B (innate pick) — `FUN_00588760`.
- 0x70 / 0x71 / 0x7A (song pick) — `FUN_00588820` (song play).
- 0x73 (modal sub) — toggle the picked modal.
- 0x74 (custom slot config) — write `m_customButtonTypes`.
- 0x77 left-click on class — partial (sets `m_nCurrentSelectedSpellClass` then SetState 0x67).
- 0x78 (quick-item pick) — for 0x50-0x52, `CGameSprite::ReadyItem` (already in default).
- 0x79 (quick-weapon pick) — for 0x3C-0x43, `CGameSprite::SetSelectedWeaponButton` (FUN_00588570 case 1).

### CUIControlButtonAction::Render polish

- field_EE count overlay — original `FUN_005950F0` paints `piVar1 + 0x30` as a memorize-count badge for quick spells (0x46-0x4E) + abilities (0x5A-0x62). Requires propagating `buttonData.m_count` into `m_nCount`, then drawing the number with a small font in the corner. We have the field but no render. Probably needs CVidFont integration.
- Quick-item usability badge — `FUN_005950F0` lines 104-145 overlay `STORTIN4` / `STORTINT` BAMs on unusable items via `CInfGame::CheckItemUsable`.

### CHU / panel BG nits

- Slight discrepancies between our action-bar strip BG colour and the original. Probably a screen-cap artifact; verify with a pixel-diff if it becomes a real bug.

### Inventory quick-weapon yellow border (separate file)

- `CScreenInventory::CUIControlButtonInventorySlot::Render` doesn't paint the yellow active-set highlight around `pSprite->m_nWeaponSet` in the quick-weapons panel.

### Misc

- `case 9` (Shapeshift) not implemented (needs `CGameSprite::GetShapeshifts` list).
- `case 0xD` (Trapfinding) not implemented (`FUN_005886a0`).
- Song-play (`case 0x6E-0x76`) currently toggles modal state 1 directly instead of calling `FUN_00588820`.
- Weapon flip (`case 0x77`) cycles `m_nWeaponSet` directly instead of using `CGameSprite::SwitchWeaponSet`.
