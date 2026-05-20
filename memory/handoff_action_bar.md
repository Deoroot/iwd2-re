---
name: handoff-action-bar
description: Session handoff — action bar + inventory UI port. Major Ghidra-faithful work done; targeted polish remaining. Read before resuming action-bar / CScreenInventory work.
metadata:
  type: project
---

# Handoff — action bar + inventory UI port (May 2026)

## TL;DR

Action bar (`CInfButtonArray`) and inventory slot rendering (`CScreenInventory`) ported to ~Ghidra parity. Pickers populated, click dispatch wired, count badge + active-set HIGHLGHT in place, STON* fallback gated to equipment slots only. Three known open items at session end:

1. Equipped-weapon icons not appearing in inventory quick-weapon slots → `InventoryInfoPersonal` returns empty `cResIcon`. Need to verify CRE load fills `m_equipment.m_items[43+set*2]`.
2. Save-load formation red border doesn't show until first click → upstream `m_curFormation` save offset / formation-AI wiring still stub.
3. STON* tint on empty action-bar quick slots differs subtly from original — cause unclear (alpha blit / palette / unported `SetTintColor` call).

## Where to start

- `memory/todo_action_bar.md` — full TODO tracker, refreshed at end of session.
- `src/CInfButtonArray.cpp` / `.h` — main file. Mostly done.
- `src/CGameSprite.cpp` lines ~7956-8108 — picker list builders (`GetSpellsAtLevelButtonList`, `GetSpellsButtonList`, `GetDomainSpellsButtonList`, `CountClassesWithSpells`).
- `src/CScreenInventory.cpp` `CUIControlButtonInventorySlot::Render` (line ~4160) — recently expanded with STON* fallback + HIGHLGHT.
- `src/CUIControlFactory.cpp` `CUIControlButtonAction::Render` (line ~4260) — the action-bar slot render wrapper.

## What landed this session (chronological commits)

```
1b64c398  fix: spellbook unmemorize compaction and flash render condition (prev)
616a9e28  action bar empty quick-slot buttons and weapon-set highlight
6bf25346  CInfButtonArray action bar icon overlays + missing button types
20f60bd2  action bar state handlers and quick-slot parity
68c6a5fa  action-bar button rendering — icon origin and bezel visibility
e9145a7c  action bar quick-slots & formation buttons visual parity
cdee8f24  left-click handlers for buttons 5, 10, 0x0E, 0x6E-0x76, 0x77
bc1cc153  rename CInfButtonSettings fields to semantic names
5f09e69a  docs: refresh action-bar TODO
20a7d3b7  formation slots show red border when selected formation matches
16526233  drop invented Unmarshal/Init formation tweaks; document as TODO
a9624b15  add dynamic spell picker list to CInfButtonArray
74c46244  spellbook picker class dispatch + level state leakage
635f46ac  count domain pool toward class total
ff0a89ce  dispatch picker click via CGameSprite::UseButtonAction
4f2c03f6  add page-up/down (0x21/0x22) when list > 12
964bfdb9  wire weapon (0x65) and item (0x68/0x69) pickers via GetItemUsages
d99efcec  quick-song UseButtonAction + weapon flip SetWeaponSet
dee82867  item picker dispatches via ReadyOffInternalList
ee7cd2dd  innate picker (0x6A/0x6B) via UseButtonItem
33d4a2bb  customize states (0x66/0x68/0x71) save buttonData to quick slot
b30380bb  draw NUMBER.BAM count badge on quick spells/abilities/songs
8d4bab3a  inventory: STONSLOT border on active weapon set slots (later replaced)
27c99b29  inventory: STON* fallback for empty slots (later scoped down)
3db3a98d  inventory: HIGHLGHT for active set + STON* only on equipment slots
```

Run `git log --oneline | head -30` for the latest list.

## Subsystem state

### Action bar (`CInfButtonArray`)

- `UpdateButtons` covers button types 2 / 3 / 4 / 5 / 7 / 8 / 9 / 0xB-0xF / 0x10-0x14 / 0x15-0x20 / 0x21 / 0x22 / 0x23-0x2A / 0x32-0x39 / 0x3C-0x43 / 0x46-0x4E / 0x50-0x52 / 0x5A-0x62 / 0x6E-0x76 / 0x77 / 100. Empty quick slots stay active with STON* fallback.
- `SetState` covers 0x65 / 0x66 / 0x67 / 0x68 / 0x69 / 0x6A / 0x6B / 0x70 / 0x71 / 0x6C / 0x6D / 0x6E / 0x6F / 0x72 / 0x73 / 0x74 / 0x75 / 0x76 / 0x77 / 0x78 / 0x79 / 0x7A / 0x7B. Picker states (0x65-0x6B, 0x70/0x71, 0x7A/0x7B) build a real `CGameButtonList*` via `RebuildPickerList`.
- `OnLButtonPressed` default case handles 2 / 3 (with single-class fast-path) / 4 / 5 / 7 / 8 / 0xB / 0xC / 0xE / 0xF / 10 / 0x32-0x39 / 0x3C-0x43 / 0x46-0x4E / 0x50-0x52 / 0x5A-0x62 / 0x6E-0x76 / 0x77. State-specific paths for 0x6C/0x6D (formation pick), 0x6E (group bar), 0x76 (class picker), 0x66-0x6B/0x70/0x71/0x7A/0x7B (picker — customize vs dispatch).
- `OnRButtonPressed` is the full state dispatcher (0x6E / 0x72 / 0x75 / 0x77).
- `RenderButton` paints HIGHLGHT ring + icon + NUMBER count badge per Ghidra `FUN_005950F0` + `CIcon::RenderIcon`.

### Picker dispatch matrix

| State | Per Ghidra helper | Our sprite method |
|-------|-------------------|-------------------|
| 0x66 / 0x68 / 0x71 | save quick slot via FUN_00588cb0 | `SetQuickSpell` / `SetQuickItem` / `SetQuickSong` |
| 0x67 (spell) | FUN_005886a0 | `UseButtonAction` |
| 0x69 (item) | FUN_005884b0 | `ReadyOffInternalList` |
| 0x6A / 0x6B (innate) | FUN_00588760 | `UseButtonItem` |
| 0x70 / 0x7A (song) | FUN_00588820 | `UseButtonAction` (close approx; real song needs sound + AIAction) |
| 0x7B | catch-all | `UseButtonAction` |

### Inventory (`CScreenInventory::CUIControlButtonInventorySlot::Render`)

- STON* fallback per Ghidra: 11/12/13/14 → STONARM/STONGLET/STONHELM/STONAMUL. 15-17 → STONQUIV. 21-25 → STONBELT/STONRING/STONRING/STONCLOK/STONBOOT. 101-108 → STONWEAP/STONSHIL alternating. Quick items (5/6/7) + inventory grid (30-45/73-80) do NOT get fallback — Ghidra `goto LAB_0062dff4`s them directly.
- Active weapon set: HIGHLGHT.BAM (32×32 green ring) over both main + off slots of `pSprite->m_nWeaponSet`. Drawn at pt+2 scaled.

## Open items (priority order)

### High — weapon icons missing in inventory

Image #26 shows equipped Geldin with empty quick weapon slots. `InventoryInfoPersonal(slot, ...)` returns `cResIcon == ""` for those slots. Verify:

1. `pSprite->m_equipment.m_items[43 + 2*m_nWeaponSet]` is non-NULL after CRE load.
2. `CItem::GetItemIcon()` returns a valid resref for that item.
3. The CRE in the save file actually has the weapons at the expected slot offsets (the save format `pCreature + 0x?` for weapons may have shifted).

Drop into `CGameSprite::UnmarshalCreature` (likely culprit) and DBG-log `m_items[43..50]` after load.

### Med — save-load formation red border

`m_curFormation` is loaded from save at offset 0x0C (`CInfGame::Unmarshal` line 2061) but the red border doesn't appear until the user clicks a formation. Two paths:

1. Save offset wrong — verify against IESDP IWD2 GAM format.
2. `UpdateButtons` runs before Unmarshal completes — no re-render after load. (Original probably does refresh somewhere; we don't yet.)

Commit 16526233 reverted my invented `UpdateState()` call in Unmarshal because that's not Ghidra-faithful. The real fix likely lives in the formation/AI module that's still stub. Leave for now.

### Med — STON* tint subtle diff on empty quick slots

Image #18 vs Image #19 — our empty STONSPEL / STONITEM / STONSPEC slots render slightly darker than original. Confirmed not `m_bGreyOut` driven. Possible causes: STON* BAM alpha blit, RGB565 dither, missing `SetTintColor` from `FUN_005950F0`. Diff GUIBTBUT[0] RGB(78,74,58) vs GUIBTACT[0] RGB(100,83,46) — original may render GUIBTACT under STON* for some slots; we render GUIBTBUT. Skip until other items done.

### Low — case 9 (Shapeshift) + case 0xD (Trapfinding)

Both require helpers we haven't ported:
- 9 → `CGameSprite::GetShapeshifts` list
- 0xD → `FUN_00718390` (build buttonData from `DAT_008f8e60` cleric-trapfind resref) + `FUN_005886a0` dispatch

### Low — count badge offsets

`RenderButton` uses `LAST_DIGIT_OFFSET (25,25)` from `CIcon`. Mirrors Ghidra `CIcon::RenderIcon` digit loop. Verify visually that the badge sits in the bottom-right corner of the 38×38 button cleanly. If not, adjust offsets or use a digit-specific BAM (NUMBER2 / NUMBER3 are variants).

### Low — inventory yellow border

Looking at Image #25, original may use a YELLOW (not green) ring on the *main* slot of the active set and GREEN on the off-hand. Current impl uses HIGHLGHT green on both. If user complains, swap main to a tinted variant or a different BAM.

## How to test

```powershell
# Force-close, build, deploy in one shot
Get-Process -Name iwd2-re -ErrorAction SilentlyContinue | Stop-Process -Force
cmake --build build --config Debug
Copy-Item -Path "build/Debug/iwd2-re.exe" -Destination "C:\GOG Games\Icewind Dale 2\" -Force
& "C:\GOG Games\Icewind Dale 2\iwd2-re.exe"
```

Or use the `iwd2-build-deploy` agent.

Smoke test:
- `python scripts/click_load_original.py` — loads first save automatically.
- Click party portraits → action bar should refresh. Select single PC vs full group.
- Right-click a quick spell slot → state 0x75 customize menu (right click 0x24 → 0x77 class picker).
- Cast Spell click on cleric with domain spells → Cleric/Domain picker. Click Cleric → cleric spellbook. Click Domain → domain spellbook.
- Cast Spell click on sorcerer → direct spellbook (no class picker).
- Pick spell from spellbook → should cast (UseButtonAction).
- Open inventory (I) — verify empty equipment slots show STON*, quick items stay transparent, active weapon set has green ring.

## Gotchas

- Repo convention: no `Co-Authored-By: Claude` trailer on commits. Use `iwd2-build-deploy` agent for force-close + rebuild + redeploy in one step.
- `CInfButtonSettings` field names were renamed to semantic (commit bc1cc153) — old code may still reference `field_4` / `field_8` / `field_C` / `field_10` / `field_1CC` / `field_1D0` / `field_1D8`. Map to `m_bActive` / `m_bHasOverlay` / `m_nIconNormalFrame` / `m_nIconSelectedFrame` / `m_bSelected` / `m_bActiveWeaponSet` / `m_nCount`.
- `field_1C8` repurposed to stash the BAM cycle used by `RenderButton` (sequence 0 for STON*, sequence 1 for item icons). Don't reset it without reason.
- `m_pPickerList` (`CGameButtonList*`) is OWNED by the array — `ClearPickerList()` walks `RemoveHead` then deletes. `SetState` to a non-picker state auto-clears.
- Picker entry index in paging mode: `m_nPickerPage * 10 + (buttonType - 0x15)`. In ≤12 mode: `buttonType - 0x15`. Both `UpdateButtons` and `OnLButtonPressed` apply this so they stay consistent.

## Reference Ghidra addresses

| Function | Address | Notes |
|----------|---------|-------|
| CInfButtonArray::SetState | 0x589110 | Full state machine |
| CInfButtonArray::UpdateButtons | 0x58A340 | Per-button-type icon dispatch |
| CInfButtonArray::OnLButtonPressed | 0x58FF20 | Left-click handler |
| CInfButtonArray::OnRButtonPressed | 0x594720 | Right-click handler |
| CInfButtonArray::RenderButton | 0x5957C0 / 0x5950F0 | Icon overlay + BG paint |
| CUIControlButtonAction::Render | 0x77B530 | Slot-level dispatcher |
| FUN_00587c20 | 0x587c20 | Dynamic picker list builder |
| FUN_00594280 | 0x594280 | Modal dispatch (case 3 / 5 / 10 / 0xE / 0x50-0x52) |
| FUN_00588570 | 0x588570 | Equip / ReadySpell / ReadyItem |
| FUN_005886a0 | 0x5886A0 | UseButtonAction wrapper (spells) |
| FUN_005884b0 | 0x5884B0 | ReadyOffInternalList wrapper (items) |
| FUN_00588760 | 0x588760 | UseButtonItem wrapper (innate) |
| FUN_00588820 | 0x588820 | Song play wrapper |
| FUN_00588cb0 | 0x588cb0 | Customize quick slot |
| CIcon::RenderIcon | 0x4E66E0 | Generic icon + count digit render |
| CUIControlButtonInventorySlot::Render | 0x62DDE0 | Inventory slot render |
| CUIControlButtonInventoryAppearance::Render | 0x62E7D0 | Inventory char preview render (different — Ghidra mislabels) |

## Tools used

- GhidraMCP at `127.0.0.1:8089` (`curl /decompile_function?address=0xXXXX -o tmp_X.txt`).
- BAM inspection: `scripts/bam_to_png.py <path> all` then `Read` the resulting PNG to view inline.
- `pefile` for reading raw rdata strings (the HIGHLGHT resref string was found at `0x8AFB60`).
- `iwd2-build-deploy` agent for the kill+build+copy cycle.

Last commit: `3db3a98d`. `git log --oneline | head -5` for current tip.
