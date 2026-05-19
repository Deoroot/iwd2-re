---
name: todo-action-bar
description: Remaining work to bring CInfButtonArray + CUIControlButtonAction to 100% Ghidra parity. Picker states (0x65-0x6B etc.) currently fall back to formation buttons because the dynamic spell/item list builder isn't ported yet.
metadata:
  type: project
---

# Action bar — remaining work

Tracker file. Update after each milestone.

## Done so far (commits 1b64c398 → 20f60bd2)

- `UpdateButtons` covers button types 2/3/4/5/7/8/9/0xB-0xF/0x10-0x14/0x15-0x20/0x21/0x22/0x23-0x2A/0x32-0x39/0x3C-0x43/0x46-0x4E/0x50-0x52/0x5A-0x62/0x6E-0x76/0x77/100. Empty quick slots keep `bActive=TRUE` with STON* fallback. `field_8 = 0` for STON*-style slots so the GUIBTBUT base bezel paints under them. `field_1D0 = 1` for the sprite's active weapon-set pair → drives the HIGHLGHT green ring in RenderButton.
- `SetState` has 0x65-0x6F, 0x6C/0x6D, 0x72, 0x73/0x74, 0x75, 0x76/0x77, 0x78, 0x79, 0x7A/0x7B (picker states use the formation-button fallback layout from Ghidra LAB_00589bbb).
- `OnLButtonPressed` default case covers 2/4/0xB/0xC/3/7/8/0xF/0x3C-0x43/0x32-0x39/0x46-0x4E/0x50-0x52/0x5A-0x62. State 0x6E covers 6/7/8/0xF/0x10-0x14. State 0x6C/0x6D handles formation pick.
- `OnRButtonPressed` is the full state dispatcher (0x6E / 0x72 / 0x75 / 0x77).
- `RenderButton` paints HIGHLGHT (active weapon set) before field_14.

## Remaining

### Picker dynamic list (high effort, blocks most submenus)

`FUN_00587c20` builds a `CGameButtonList` from sprite spells / items / abilities / songs filtered by the spell-class in `m_nCurrentSelectedSpellClass`. Used by SetState cases 0x65-0x6B / 0x70-0x71 / 0x7A / 0x7B. Without it, those pickers show formation icons instead of actual castable spells. Needed inputs: `CGameSpriteGroupedSpellList::GetSpellsAtLevel`, `CSpell::Demand`, `CGameSprite::CanCast`, paging logic for >12 entries (uses buttons 0x21 / 0x15-0x1F / 0x22).

### State-specific OnLButtonPressed handlers

Currently the picker states route a left-click to the default-case button-type handler, which is wrong for the picker layouts. Per Ghidra `FUN_0058FF20`:

- 0x65 (weapon equip pick): clicking 0x15-0x20 calls `CGameSprite::EquipWeapon` then drops back to 0x72.
- 0x66 / 0x67 (spellbook pick): clicking 0x15-0x20 calls `CGameSprite::Cast` for the indexed spell, drops back to 0x72.
- 0x68 / 0x69 (item ability pick): calls `FUN_005884b0` then back to 0x72.
- 0x6A / 0x6B (innate pick): `FUN_00588760`.
- 0x70 / 0x71 / 0x7A (song pick): `FUN_00588820`.
- 0x73 (modal sub): toggle the picked modal (Stealth / Berserk / Turn / etc.).
- 0x74 (sub-modal config): writes to `m_customButtonTypes`.
- 0x77 left-click on class: stores `m_nCurrentSelectedSpellClass` then SetState(0x67) — partially done.
- 0x78 (quick-item pick): for slot 0x50-0x52, calls `CGameSprite::ReadyItem` (already supported by our default case).
- 0x79 (quick-weapon pick): for 0x3C-0x43, calls `CGameSprite::SetSelectedWeaponButton` (i.e. `FUN_00588570 case 1`).

### CUIControlButtonAction::Render polish

- field_EE count overlay: original `FUN_005950F0` (lines 88-104) paints `piVar1 + 0x30` as a memorize-count badge for quick spells (0x46-0x4E) and quick abilities (0x5A-0x62). We never assign field_EE so no count shows. Needs `CButtonData::m_count` propagated into field_EE's render call.
- Quick-item usability overlay (`STORTIN4` / `STORTINT` per `FUN_005950F0` lines 104-145) — small red badge on unusable items.

### CHU layout / panel BG

- Action bar strip BG (the wood-grain panel behind the 12 button slots) renders a slightly different MOS than the original. Likely a CHU/PANEL field we read with wrong offset or skip; needs comparison with the original `GUIWORLD.CHU` layout via NearInfinity. Also affects slot positions (a few pixels off vs Ghidra-faithful CHU loader).
- Cropped buttons (right + bottom pixel row missing): probably an off-by-one in the control-rect clip or the surrounding panel rect.

### Inventory quick-weapon path (separate file)

- `CScreenInventory::CUIControlButtonInventorySlot::Render` doesn't paint the yellow active-set highlight that the original shows around the currently-selected weapon pair (slots 43-50 in `m_items`).

### Misc

- `OnLButtonPressed` `case 9` (Shapeshift) not implemented (needs `CGameSprite::GetShapeshifts` list).
- `OnLButtonPressed` `case 5` (modal alternate) — uses `FUN_00594280 case 5` which toggles `m_nModalState`.
- `OnLButtonPressed` `case 0xD` (Trapfinding) — uses `FUN_005886a0`.
- `OnLButtonPressed` `case 0x77` (Weapon flip) — calls `CGameSprite::SwitchWeaponSet` (need to confirm name).
- Quick-song left-click (`case 0x6E-0x76` in default) — needs `FUN_00588820` song-play helper. Currently no handler.
