# vtable_audit triage — 2026-07-31

First full `arc sweep` reported **275 vtable findings**. Triaged here.

**203 of them were a tool bug.** After fixing it the real count is **72**, split
into six buckets below, verified against the binary rather than against the
audit's own wording.

---

## The 203: two compounding bugs in `vtable_audit.py` (fixed)

`CGameAnimationTypeEffect` alone produced 203 of the 275, with targets like
`CBaldurEngine::UpdateContainerStatus` — a class from an unrelated hierarchy,
repeated three slots in a row. That is the signature of reading past the end of
a vtable into its neighbour.

1. **`SLOT_RE` matched a member-offset comment.** In a binary-mirror header
   `/* NNNN */` marks *member offsets* as well as vtable slots, and `NAME_RE`
   was applied to the whole line including the trailing `//` comment:

   ```
   /* 05E7 */ unsigned char m_animMode;  // ... random-sequence opt-in (==1)
                                                                  ^^^^^^ "in ("
   ```

   `in` was read as a method name, creating a phantom slot at 0x5E7. Since the
   audit caps its scan at `maxslot + 4`, one phantom lifted the ceiling from
   0xc8 to 0x5E7. Fix: strip the `//` comment before matching. Three classes
   were affected (`CGameAnimationTypeEffect`, `IcewindCProjectileTravellingVFX`,
   `CProjectileSkullTrap`).

2. **`vtable_len` never stopped at the table boundary.** It walked while the
   dword looked like a code pointer — but vtables are laid out back-to-back in
   `.rdata`, so the next table's first entry is a code pointer too. Every class
   reported `len 0x400`, the hard cap. `CGameAnimationTypeEffect` is at
   0x85ac48 and `CGameAnimationTypeFlying` at 0x85ad1c, so its true length is
   **0xd4** — and the first bogus finding was at slot **0x00d4**, exactly the
   boundary. Fix: a first pass locates every class's vtable, and each table now
   stops at the next base.

Slot reading itself was never wrong: `0x847fe4` really does contain
`0x00422c50`, as the audit claimed.

---

## The 72 real findings

### A. Wrong implementation dispatched — 12. **DONE (11/12), `ce9eff57`.**

> Every one was a *correct body carrying the wrong `// 0xADDR`* — relabels, not
> rewrites. Plus one real constant fix (`CSCREENSINGLEPLAYER_VIRTUAL_KEYS`
> 92 → 90, which `struct_layout_audit` confirmed was widening the compiled
> class by 16 + 2 bytes). The 12th, `CGameAIBase::ProcessAI`, is **not** fixed:
> slot 0x008C holds an unrecovered 736-byte wrapper (0x45C730) that *calls*
> ProcessAI, and ProcessAI itself is not virtual at all. Removing `virtual`
> would shift every slot below it, so the declaration carries a TODO with the
> recipe instead. Audit went 72 → 61.


The binary's slot points at a different function than our source declares. This
is the Fireball wrong-render class of bug: the call compiles, runs, and does
something else.

| class | slot | method | our source points at | binary has |
|---|---|---|---|---|
| CScreenMap | 0x00a0 | GetNumVirtualKeys | `CScreenWorld::` | `CScreenWorldMap::` |
| CScreenMap | 0x00a8 | GetVirtualKeysFlags | `CScreenSpellbook::` | `CScreenWorldMap::` |
| CScreenSinglePlayer | 0x00a0 | GetNumVirtualKeys | `CScreenWorld::` | `CScreenWorldMap::` |
| CScreenSinglePlayer | 0x00a8 | GetVirtualKeysFlags | `CScreenSpellbook::` | `CScreenWorldMap::` |
| CScreenSave | 0x00c0 | TimerAsynchronousUpdate | `CScreenSave::` (= CScreenLoad's body) | `CScreenSpellbook::` |
| CScreenMultiPlayer | 0x0104 | CancelEngine | `CScreenSinglePlayer::` | `FUN_0064d950` (unrecovered) |
| CGameAIBase | 0x008c | ProcessAI | `CGameAIBase::ProcessAI` | `FUN_0045c730` (unrecovered) |
| CGameDoor | 0x0030 | DebugDump | `CGameDoor::DebugDump` | `FUN_004876e0` (unrecovered) |
| CBaldurChitin | 0x0054 | FontRectOutline | 0x422c60 = `GetSavedBitsPerPixel` | 0x422c50 (not a Ghidra entry) |
| CBaldurEngine | 0x0100 | EnablePortrait | 0x4277c0 | 0x427ac0 |
| CScreenConnection | 0x00a8 | GetVirtualKeysFlags | 0x5fa8d0 | 0x5fa8e0 |
| CGameAnimationTypeMonsterQuadrant | 0x0090 | Render | 0x6e7a40 | 0x6bc5f0 |

**Root cause for most of the screen cases: we attached the class to the wrong
`/OPT:ICF` group.** The linker folds identical function bodies, so
`0x686660` is legitimately shared by six screen classes' `GetNumVirtualKeys`
(Journal, Keymaps, Map, SinglePlayer, Spellbook, World) — that is not a
duplicate-address bug. But the binary says `CScreenMap` dispatches to
`0x699600` instead, i.e. it belongs to a *different* folded group and returns a
different key count. **The vtable is the ground truth for which group a class
belongs to; a matching body is not.**

A source-wide scan found **116 addresses defined by two or more classes**. Most
are benign ICF folding. The ones above are where we picked the wrong group.

### B. Never-recovered virtual — 23. Backlog, not a bug.

The binary overrides a slot with a function we have not recovered. Notable:
`CProjectile` slots 0x0000 and 0x002c, `CGameDoor` 0x002c/0x0040/0x004c,
`CGameAnimationTypeMonsterMulti` 0x000c/0x0090/0x00ac, `CBaldurEngine` 0x0004.
These are legitimate `arc targets` entries.

### C. Binary has a stub, we wrote real code — 3. **DONE, `369ea705`.**

> `CScreenCreateChar::CheckMouseRButton` deleted (slot 0x0088 = the inherited
> `CWarp` FALSE; 17 of 18 screens override it to TRUE, this one deliberately
> does not). `CVidInf::DestroySurfaces3d` is no longer an `override` — slot
> 0x0124 is identical to `CVidMode`'s, and the real teardown is a non-virtual
> member with one direct caller. `CGameEffect::OnAdd` was only mislabelled: its
> no-op body was already right, but it had drifted above `FireSpell`'s
> `// 0x4A3FF0` comment block. Audit 61 → 58.


| class | slot | method | binary actually does |
|---|---|---|---|
| CGameEffect | 0x0010 | OnAdd | `ret 4` — empty |
| CVidInf | 0x0124 | DestroySurfaces3d | `xor eax,eax; ret` — return 0 |
| CScreenCreateChar | 0x0088 | CheckMouseRButton | `xor eax,eax; ret` — but our slot points at `mov eax,1; ret` |

The last one is a genuine behavioural inversion: **returns TRUE where the binary
returns FALSE.** `CGameEffect::OnAdd` is worth checking first anyway — it is on
the spell-effect path.

### D. Slot shift — 2. **DONE, `93ab04fb`.**

> A copy-paste: `CChitin.h` and `CBaldurChitin.h` both declared
> `GetLogFileName` **and** `GetErrFileName` at `/* 00A8 */`. The returned
> literals settle it — 0x00A8 gives `"Chitin.log"` / `".\Icewind2.log"`, 0x00AC
> gives the `.err` twins. All four .cpp definitions already had the right
> addresses and the declaration order was already right, so the emitted vtable
> was never wrong; comments only. Audit 58 → 55, and the only WRONGADDR left in
> the tree is `CGameAIBase::ProcessAI`.


`CBaldurChitin` and `CBaldurProjector` declare `GetErrFileName` at slot 0x00a8,
but the binary has `GetLogFileName` there (`0x422e00` = `mov eax,0x8a6db0; ret`,
returning a different literal) and `GetErrFileName` one slot later at 0x00ac.
Our header is missing `GetLogFileName` and has `GetErrFileName` one slot early.

### E. Undeclared trivial override — 15. Low, but not zero.

The binary's slot is an `/OPT:ICF`-folded trivial body we never declared:
`0x49fc40` = `mov eax,1; ret`, `0x71e750` = `ret 8`, `0x78e6e0` =
`xor eax,eax; ret`, `0x799ca0` = `ret`. Mostly `CScreenStart` (5) and
`CScreenMovies` (3).

**It only matters when the inherited base implementation differs.** Where the
base already returns the same value, declaring the override changes nothing;
where it does not, this is a silent behaviour change. Check the base before
dismissing.

### F. Undeclared real override — 14.

Same as E but the target is real code, so declaring it is a genuine recovery:
`0x47c830` (writes -1 through its out-param) shared by `CGameContainer`,
`CGameDoor`, `CGameTrigger` at slot 0x0028; `0x6c2d70` shared by three
`CGameAnimationTypeMonster*` at 0x00cc.

### G. Spurious override — 3. **DONE, `c8cd9fe5`.**

> Two were tool bugs: `find_vtable` resolved `CPersistantEffect84C4A4` to its
> *derived* class's table (0x84C420), because the base's only anchor is
> inherited verbatim — comparing a class against itself makes every slot look
> `== base`. Reading both tables shows the class really does override.
> `audit()` now refuses a base whose vtable equals the derived one.
>
> The third, `CGameAIBase::CanSaveGame`, is a real and *deliberate* divergence
> — now marked `// HACK:`. Its premise was narrowed: of the seven subclasses,
> five override slot 0x0028 to `0x47C830` (return TRUE) and CGameSprite has its
> own; the three that were undeclared (`CGameContainer`, `CGameDoor`,
> `CGameTrigger` — also bucket F entries) are now recovered. Only
> `CGameTiledObject` still inherits the FALSE, and its vtable resolves from a
> single anchor, too weak to bet saving on. Audit 55 → 50.


We declare an override where the binary's slot equals the base's:
`CGameAIBase::CanSaveGame` (0x0028), `CPersistantEffect84C420::Copy` (0x0008)
and `::ApplyEffect` (0x000c).

---

## Suggested order

1. **A** (12) — wrong dispatch, observable behaviour, all in already-recovered code.
2. **C** (3) — we invented behaviour the binary does not have.
3. **D** (2) — mechanical, 10 minutes.
4. **G** (3) — cheap to confirm and remove.
5. **F** (14) then **E** (15) — declare the missing overrides, base-check first.
6. **B** (23) — feed into `arc targets`.

## Follow-up worth building

`vtable_audit` now catches the wrong-ICF-group class of bug, but only for
classes whose vtable it can anchor. A source-only lint over the 116
multiply-defined addresses — flagging those whose owning class's vtable slot
disagrees — would find the same defect without needing an anchored vtable.
