# Non-faithful passages

Inventory of every place in `src/` that does **not** match `IWD2.exe` 1:1 — behavioral
divergences, stubs, approximations, intentional skips, and uncertain recoveries.

`Ghidra = truth`. This file is a map of where the recovered code knowingly departs from
the binary, so those spots can be revisited and closed.

> Line numbers drift as the tree changes. Each entry carries the source marker text and,
> where known, the binary address, so the site can be re-found by searching.

## How divergences are marked in the source

| Marker | Meaning | Faithful? |
|--------|---------|-----------|
| `// HACK:` | The CLAUDE.md convention for an unavoidable hack. | **0 in tree today.** |
| `// NOTE: Original code is different` | Behavioral or representational divergence. | depends — see entry |
| `// NOTE: ... but does the same thing` | Different shape, identical behavior. | ✅ yes |
| `// NOTE: Uninline.` | Original inlined a callee; we call it out-of-line. | ✅ yes (~hundreds) |
| `// TODO: Incomplete.` | Unimplemented or partial — behavior **missing**. | ❌ 308 across 58 files |
| `// FIXME:` | Mostly code-quality (refs, redundancy, typos). | ✅ usually behavior-preserving |
| `// #guess` | Uncertain struct offset / field type (layout recovery). | ⚠️ unverified, not behavioral |
| `// Skip...` | Intentionally omitted behavior (MP / movies / minor UI). | ❌ behavior omitted |

## Severity legend

- 🔴 **Behavioral divergence** — does something different from the binary.
- 🟠 **Stub / missing behavior** — no-op, early return, or partial.
- 🟡 **Approximation** — 1:1 not feasible; close but not exact.
- 🔵 **Intentional skip** — feature deliberately left out (MP, movies, dev/test).
- ⚪ **Behavior-preserving** — cosmetic; listed only as a non-issue.

---

## 1. Behavioral divergences 🔴

Code that runs but does something measurably different from the binary.

| File:line | What diverges | Binary ref |
|-----------|---------------|------------|
| `CInfGame.cpp:5501` | Conditions **inverted** vs original. | — |
| `CInfGame.cpp:6302` | Sets token only under a narrower condition. | — |
| `CBaldurChitin.cpp:1524` | "Looks odd, generated binary does not match." | — |
| `CChitin3d.cpp:41` | Different — reworked 3D entry point. | — |
| `CChitin3d.cpp:71` | Slightly different current-mode check. | — |
| `CGameObjectArray.cpp:317` | Copies every property one-by-one instead of bulk. | — |
| `CGameSprite.cpp:9765` | Slightly different (heavy inlining in original). | — |
| `CResCHR.cpp:50` | Deviation from the sibling `Parse` methods. | — |
| `CVidCell.cpp:2053` | Different — "not sure if the implementation is [correct]". | — |
| `CScreenCreateChar.cpp:5545` | Casts `CScreenSinglePlayer` differently. | — |
| `CScreenCreateChar.cpp:5622` | Original uses one variable for two roles. | — |
| `CAIObjectType.cpp:764` / `CAIScriptFile.cpp:2242` | "Original code is (slightly) different." | — |
| `CImmunities.cpp:207,503`, `CNetwork.cpp:343`, `CResWorldMap.cpp:37` | "Slightly different." | — |
| `CScreenCharacter.cpp:8730,8750,8771`, `CScreenInventory.cpp:5046,5315` | "Slightly different." | — |
| `CScreenMap.cpp:2205,2262`, `CScreenStore.cpp:5082,5818`, `CStore.cpp:114` | "Slightly different." | — |
| `CScreenConnection.cpp:3510`, `CScreenCreateChar.cpp:4962`, `CInfCursor.cpp:484` | "Slightly different" (loop/inlining). | — |

### Suspected-wrong (flagged by the author)
| File:line | Note |
|-----------|------|
| `CDimmKeyTable.cpp:325` | "Probably wrong — `TranslateType` returns -1 to indicate ..." |
| `CGameEffect.cpp:4395` | "Probably wrong (using live as start)." |
| `CGameEffect.cpp:4483` | "Probably wrong (using live as start)." |

### Known-incorrect but harmless (documented equivalence)
| File:line | Note |
|-----------|------|
| `CGameArea.cpp:107` | memcpy of `DEFAULT_TERRAIN_TABLE` from a different source than the binary (`0x8A8168`/`0x8A8154`); values are identical, so harmless. |
| `CGameSpawning.cpp:33` | Same terrain-table source divergence as above; same values. |

---

## 2. Stubs / not-yet-recovered behavior 🟠

Functions that return early / no-op / always-pass, so real behavior is missing.

| File:line | Stub | Binary ref | Impact |
|-----------|------|-----------|--------|
| `CGameObject.cpp:430` | `EvaluateStatusTrigger` returns `TRUE` unconditionally. | `0x799E20`; real impl `CGameSprite` override `0x731B30` not recovered | Dialog/script conditions (Global, GlobalGT, NumTimesTalkedTo, Class…) always pass → duplicate dialog replies, wrong trigger gating. **Tracked in the pathfinding/dialog arcs.** |
| `CAIGroup.cpp:1488` | Formation passability check stubbed `passable = TRUE`. | `0x46a3d0` not recovered | Leader-cell passability when target ≠ leader cell. |
| `CAIGroup.cpp:1532` | Member-destination passability stubbed `TRUE`. | `0x46a3d0` | Formation member placed at offset even when blocked → may overlap impassable. **Relevant to formation work (plan §C).** |
| `CAIGroup.cpp:1695,1751` | Two more `0x46a3d0` passability stubs in the same routine. | `0x46a3d0` | Same. |
| `CAIGroup.cpp:1546` | Original inserts `SMALLWAIT(rand()%7)` (0–6 ticks) into each member's queue **only when the MP flag `CInfGame+0x1B7C` is set**; omitted. | — | **MP-only — faithful in single-player** (the original adds nothing in SP). TODO only restores MP stagger once the flag offset is recovered. |

### Spell / action recovery gaps (`CGameAIBase.cpp`)
Partial action dispatch; cross-referenced by the ForceSpell and dialog recovery arcs.
- `:304` action dispatch incomplete (target-object & message-heavy actions).
- `:636` binary also enqueues a `NULL_ACTION` terminator after clear — omitted.
- `:760` `CInfGame::FeedBack` call omitted.
- `:1363` `0x57` action sentinel meaning unknown.
- `:2383,3113` multiplayer `CMessage` broadcasts skipped.
- `:2699,2762` projectile launch / legacy `FireSpell` fallback (`FUN_0051EAF0`, `CMessageFireProjectile`) still TODO.
- `:3061,3085` skips the `FUN_0045BDD0` immunity/distance filter.

---

## 3. Approximations 🟡

Intentional non-1:1 where exact replication was impractical.

| File:line | Approximation |
|-----------|---------------|
| `CSearchBitmap.cpp:714` | `BYTE snapshotDynamicCost[320*320]` (fixed) approximates the binary's `alloca(cx*cy)`. Safe because the search grid is capped at `GRID_ACTUALX/Y = 320`. |
| `CInfButtonArray.cpp:1768` | Weapon-set equip approximated by "always equip" instead of the real set logic. |
| `CInfButtonArray.cpp:1815` | Bard song select simplified to a toggle instead of song-select state `0x7A`. |
| `CInfButtonArray.cpp:1961` | **Placeholder** formation buttons until `FUN_00587c20` is ported. |
| `CInfButtonArray.cpp:2337` | Song availability approximated via `std::vector` emptiness on `m_songs.m_List`. |
| `CScreenInventory.cpp:4171` | Per-slot `STON*` placeholder set up ahead of the switch. |
| `CScreenCreateChar.cpp:3225,3279` | Char-creation paths "likely impossible to replicate one to one." |
| `CVidMosaic.cpp:85` | Non-3D mosaic blit "different and hard to replicate one to one." |
| `CVidMosaic.cpp:410` | 3D mosaic blit adapted from the non-3D path; not 1:1. |
| `CVidMosaic.cpp:238,277,311` | Less-optimized than the original (functionally equivalent). |
| `CVidInf.cpp:48` | Unwind-stack difference; "not sure how to replicate it one-to-one." |

---

## 4. Intentional skips 🔵

Behavior deliberately omitted — multiplayer, movies, dev/test, minor UI.

| File:line | Skipped |
|-----------|---------|
| `CBaldurChitin.cpp:245` | 3D-accelerated flag read from settings instead of forced `FALSE` (dev/test toggle). |
| `CBaldurChitin.cpp:898` | Intro movie / projector activation skipped until that path is stable. |
| `CBaldurChitin.cpp:904` | Menu-only bootstrap: async service-provider countdown/popup skipped. |
| `CDungeonMaster.cpp:31` | Recursive `SelectEngine` skipped — **it crashes** (workaround, not a fix). |
| `CChitin.cpp:491` | `m_bFullscreen = FALSE` forced for testing. |
| `CGameDialog.cpp:311` | Talker portrait color + display name (best-effort, skipped). |
| `CGameDialog.cpp:574` | Stack-local `CSound` playback skipped (minor UI). |
| `CGameDialog.cpp:614,670` | Talker-switch / counter-reset paths skipped. |
| `CGameAIBase.cpp` (many) | Multiplayer `CMessage` broadcasts throughout — single-player unaffected. |

---

## 5. Pathfinding / movement divergences (current focus)

Some lack inline markers — captured here because they are live work (see
`plans/il-y-a-forc-ment-tidy-lynx.md`).

| File:site | Divergence | Status |
|-----------|------------|--------|
| `CGameSprite.cpp` `ClearBumpPath` (`0x6FA900`) | dirTable indexed writes **guarded to array bounds**. The binary writes `abStack_ac[idx]` unconditionally; for non-adjacent spans `idx` leaves the array and the binary scribbles dead frame locals (harmless), which our standalone array can't replicate without corruption. `dirTable[0..8]` (the only slots read) are identical for every adjacent input. | Fixed `5bb98c36`; documented divergence. |
| `CSearchBitmap.cpp` `GetMobileCost`/`SnapshotGetCost` personal-space clamp | Loop clamps `maxX`/`maxY` to `cx`/`cy` (not `cx-1`/`cy-1`) with an inclusive `<=`, reading `dyn[y*cx+x]` up to index `cx`. **✅ Verified faithful** to binary `0x547D30`: offset `+0xe6`=cx (row stride + X clamp), `+0xea`=cy (Y clamp), inclusive loops, same one-past read. Do NOT change — `cx-1` would diverge. | ✅ Verified faithful. |
| `CGameSprite.cpp` `ClearBumpPath` cellY bound | Inner bound checks `cellY >= m_GridSquareDimensions.cx` (width). **✅ Verified faithful** to binary `0x6FA900`: it uses offset `+0xe6` (cx) for **both** the cellX and cellY bounds (and as the `dyn[cellY*cx+cellX]` stride). Do NOT change to `.cy` — that would diverge. | ✅ Verified faithful. |
| `CGameSprite.cpp` / `CSearchBitmap.cpp` / `CAIGroup.cpp` debug logging | Custom `Iwd2DebugLog`/`s_gcDbg`/`bLogRenderPortrait` facility (`src/DebugLog.{h,cpp}`), absent from the binary; frame-layout sensitive. **✅ Removed** from all three pathfinding files (54 sites + orphaned includes). Facility + other-arc call sites (dialog/UI) retained. | ✅ Removed. |
| `CGameObject.cpp:430` `EvaluateStatusTrigger` | (See §2.) Affects post-dialog movement gating. | Open. |

---

## 6. Layout / type guesses (`// #guess`) ⚠️

~80+ `// #guess` markers, almost all on **struct field offsets/types** in headers
(`CBaldurProjector.h`, `CChitin.h`, `CDimm.h`, `CDimmKeyTable.h`, `CGameOptions.h`,
`CChitin.cpp`, `CBaldurChitin.cpp`, …) plus a few function-shape guesses
(`C2DArray.cpp:292`, `CBaldurChitin.cpp:106/110/137/1488/1501/1508`, `CChitin.cpp:46/50/1661`,
`CBaldurProjector.cpp:341`, `CDimm.cpp:1496`).

These are **unverified recoveries**, not deliberate behavior changes. A wrong offset
here would corrupt memory silently — worth auditing against the BG2EE PDB and Ghidra DATA
xrefs, but they are not "hacks" in the behavioral sense.

---

## 7. `TODO: Incomplete` stub inventory (308 across 58 files)

Per-file counts. Each is an unimplemented or partial function (early return / no-op /
copies an unknown STL container). Files are grouped by subsystem; **bold** = gameplay-
or simulation-affecting.

### Simulation / AI / rules
| File | Count |
|------|-------|
| **`CGameSprite.cpp`** | 25 |
| **`CGameAIBase.cpp`** | 4 |
| **`CGameEffect.cpp`** | 6 |
| **`CDerivedStats.cpp`** | 10 |
| **`CGameArea.cpp`** | 3 |
| `CGameAreaNotes.cpp` | 2 |
| `CGameAnimationTypeMonsterAnkheg.cpp` | 1 |
| `CGameAnimationTypeMonsterLayeredSpell.cpp` | 1 |
| `CGameObject.cpp` | 1 |
| `CGameSpawning.cpp` | 1 |
| `CGameSpriteEquipment.cpp` | 1 |
| `CItem.cpp` | 2 |
| `CProjectile.cpp` | 1 |
| `CParticle.cpp` | 6 |
| `CSpawn.cpp` | 3 |
| `CStore.cpp` | 16 |
| `CRuleTables.cpp` | 1 |
| `CVariableHash.cpp` | 1 |
| `CWarp.cpp` | 1 |
| `CTiledObject.cpp` | 3 |
| `CVisibility.cpp` | 2 |
| `CAIObjectType.cpp` | 1 |

### Messaging / engine / IO
| File | Count |
|------|-------|
| **`CMessage.cpp`** | 30 |
| `CInfGame.cpp` | 13 |
| `CChitin.cpp` | 3 |
| `CDimm.cpp` | 1 |
| `CImm.cpp` | 2 |
| `CInfinity.cpp` | 5 |
| `CBaldurEngine.cpp` | 1 |
| `CBaldurProjector.cpp` | 7 |
| `CGameSpy.cpp` | 4 |
| `CSwitchCDStatus.cpp` | 3 |
| `CUtil.cpp` | 1 |
| `BalDataTypes.h` | 1 |

### UI / screens / video
| File | Count |
|------|-------|
| `CScreenCharacter.cpp` | 13 |
| `CScreenInventory.cpp` | 12 |
| `CScreenStore.cpp` | 12 |
| `CScreenWorld.cpp` | 9 |
| `CInfButtonArray.cpp` | 9 |
| `CScreenCreateChar.cpp` | 7 |
| `CScreenConnection.cpp` | 3 |
| `CScreenWorldMap.cpp` | 3 |
| `CScreenChapter.cpp` | 1 |
| `CScreenMap.cpp` | 1 |
| `CScreenMultiPlayer.cpp` | 2 |
| `CScreenSave.cpp` | 1 |
| `CUIControlEdit.cpp` | 5 |
| `CUIControlEditMultiLine.cpp` | 6 |
| `CUIControlFactory.cpp` | 1 |
| `CVidMode.cpp` | 20 |
| `CVidCell.cpp` | 11 |
| `CVidBitmap.cpp` | 6 |
| `CVidImage.cpp` | 6 |
| `CVidPoly.cpp` | 7 |
| `CVidBlitter.cpp` | 2 |
| `CVidPalette.cpp` | 2 |
| `CVidMosaic.cpp` | 1 |
| `CBlood.cpp` | 5 |

> Regenerate this table: `rg -c "TODO: Incomplete" src/`.

---

## 8. Behavior-preserving notes (⚪ not divergences)

Listed so they are not mistaken for hacks:

- **`// NOTE: Uninline.`** — hundreds of sites; the original inlined a helper, we call it
  directly. Identical behavior.
- **`// NOTE: ... but does the same thing.`** — e.g. `CBaldurChitin.cpp:2254`,
  `CMessage.cpp:1959`, `CScreenKeymaps.cpp:575`, `CVidCell.cpp:1529,1614,1921`,
  `CDerivedStats.cpp:394,397` ("uses loop"). Different shape, same result.
- **Style `FIXME`s** — `should be reference`, `Redundant`, `Typo`, `Unused`, `Leaking`,
  `Why use globals?`. Code-quality only; do not change observable behavior.
