# Architecture Guide

This document is a navigation map for the `src/` tree.

It is written for the person who opens this repository, sees a flat source layout, and wants to know:

- where the engine starts
- which classes own the major subsystems
- which files are the best entry points for a given task
- which call chains are worth following first

Two repo-specific rules matter while reading:

- Ghidra is the source of truth when code and address comments disagree.
- `src/` is intentionally flat, so prefixes are your main orientation tool.

## 1. The Big Picture

At runtime, the code is organized more cleanly than the flat file layout suggests.

```text
WinMain
  -> CChitin
     -> CBaldurChitin
        -> active screen/engine (CWarp -> CBaldurEngine -> CScreen*)
        -> CInfCursor
        -> CInfGame
           -> CGameArea[12]
              -> CInfinity
              -> CSearchBitmap
              -> CVisibilityMap
              -> area object lists
           -> CGameObjectArray
              -> CGameObject
                 -> CGameAIBase
                    -> CGameSprite
                    -> CGameDoor
                    -> CGameContainer
                    -> CGameTrigger
                    -> CGameStatic
                    -> CGameTiledObject
           -> CRuleTables
           -> CTimerWorld
           -> CWorldMap
           -> CGameSave

Side systems used everywhere:
  - CDimm / CRes*         resource loading and caching
  - CMessage*             object/game messaging
  - CAI*                  scripts, triggers, actions, target filters
  - CPathSearch           pathfinding
  - CVid* / CVideo*       rendering primitives and surfaces
  - CUI* / CResUI         screen UI
  - CTlkTable / CStrRes   localized text and voiced strings
  - CSound* / music/      audio playback and music decoding
```

If you only remember one thing, remember this:

- `CBaldurChitin` owns the app-level shell and all screens.
- `CInfGame` owns game state.
- `CGameArea` owns a loaded area.
- `CGameSprite` is the center of most gameplay logic.
- `CInfinity` is the area renderer.
- `CDimm` + `CRes*` are the resource plumbing underneath almost everything.

## 2. Best Entry Points

Use this table as the fast path when you want to understand one specific part of the codebase.

| If you want to understand... | Open first | Then follow into |
|---|---|---|
| process startup | [src/main.cpp](src/main.cpp) | `CChitin::WinMain`, `CBaldurChitin::Init` |
| app shell and engine switching | [src/CChitin.h](src/CChitin.h), [src/CBaldurChitin.h](src/CBaldurChitin.h) | `CWarp`, `CBaldurEngine`, `CScreen*` |
| the active gameplay screen | [src/CScreenWorld.h](src/CScreenWorld.h) | `TimerAsynchronousUpdate`, `TimerSynchronousUpdate`, input handlers |
| global game state | [src/CInfGame.h](src/CInfGame.h) | area loading, party state, world timer, object array |
| area loading and rendering | [src/CGameArea.h](src/CGameArea.h) | `Unmarshal`, `OnActivation`, `Render`, `AIUpdate` |
| area renderer internals | [src/CInfinity.h](src/CInfinity.h) | `AttachWED`, `Render`, scrolling, weather/day-night |
| object hierarchy | [src/CGameObject.h](src/CGameObject.h) | `CGameAIBase`, `CGameSprite`, door/container/trigger types |
| script execution | [src/CGameAIBase.h](src/CGameAIBase.h), [src/CAIScript.h](src/CAIScript.h) | `CAIAction`, `CAITrigger`, `CAICondition`, `CAIObjectType` |
| pathfinding | [src/CPathSearch.h](src/CPathSearch.h), [src/CSearchBitmap.h](src/CSearchBitmap.h) | `SearchThreadMain`, search requests, collision/search-map cost |
| sprite rendering | [src/CGameSprite.h](src/CGameSprite.h) | `CGameAnimation`, `CGameAnimationType*`, `CVidCell` |
| UI screens and controls | [src/CBaldurEngine.h](src/CBaldurEngine.h), [src/CUIManager.h](src/CUIManager.h) | `CUIPanel`, `CUIControl*`, `CResUI`, `CScreen*` |
| resource loading | [src/CDimm.h](src/CDimm.h), [src/CRes.h](src/CRes.h) | concrete `CRes*` classes |
| rules and 2DA data | [src/CRuleTables.h](src/CRuleTables.h), [src/C2DArray.h](src/C2DArray.h) | gameplay table lookups |
| localized strings / TLK | [src/CTlkTable.h](src/CTlkTable.h) | `CStrRes`, tokens, voiced text |
| effects and derived stats | [src/CGameEffect.h](src/CGameEffect.h), [src/CDerivedStats.h](src/CDerivedStats.h) | `CGameEffectList`, equipment/spells |
| save/load | [src/CGameSave.h](src/CGameSave.h), [src/CInfGame.h](src/CInfGame.h) | `LoadGame`, `SaveGame`, `Unmarshal` |
| messages and multiplayer sync | [src/CMessage.h](src/CMessage.h), [src/CNetwork.h](src/CNetwork.h) | `CMessageHandler`, `CBaldurMessage` |

## 3. Runtime Layers

### 3.1 Application Shell

The application starts in [src/main.cpp](src/main.cpp), creates a `CBaldurChitin`, and then hands control to the engine shell.

Core files:

- [src/CChitin.h](src/CChitin.h): base application framework
- [src/CBaldurChitin.h](src/CBaldurChitin.h): game-specific app shell
- [src/CWarp.h](src/CWarp.h): base class for switchable engines/screens
- [src/CBaldurEngine.h](src/CBaldurEngine.h): screen base class with shared UI behavior

Important responsibilities:

- message pump and update cadence
- resource/video/sound/network initialization
- owning all screen engines
- switching the active engine
- creating `CInfGame` and `CInfCursor`

The most important constructor-like runtime path is `CBaldurChitin::Init`, because it instantiates:

- all `CScreen*` engines
- `CInfCursor`
- `CInfGame`
- the initial screen flow

### 3.2 Screens and Engine Switching

Every major UI mode is a `CWarp`, usually through `CBaldurEngine`.

The main screen families live in:

- [src/CScreenWorld.h](src/CScreenWorld.h)
- [src/CScreenInventory.h](src/CScreenInventory.h)
- [src/CScreenCharacter.h](src/CScreenCharacter.h)
- [src/CScreenJournal.h](src/CScreenJournal.h)
- [src/CScreenMap.h](src/CScreenMap.h)
- [src/CScreenSpellbook.h](src/CScreenSpellbook.h)
- [src/CScreenStore.h](src/CScreenStore.h)
- [src/CScreenLoad.h](src/CScreenLoad.h)
- [src/CScreenSave.h](src/CScreenSave.h)
- [src/CScreenConnection.h](src/CScreenConnection.h)

Shared behavior comes from `CBaldurEngine`:

- selected/picked character state
- common GUI sounds
- `CUIManager`
- shared portrait/toolbar helpers

As a rule:

- `CBaldurChitin` decides which engine is active.
- the active `CScreen*` handles input and screen-specific logic.
- `CScreenWorld` is the gameplay screen where the world, area, party, and UI all meet.

### 3.3 Global Game State

[src/CInfGame.h](src/CInfGame.h) is the highest-value header in the repo.

It owns:

- loaded areas (`m_gameAreas`)
- the visible area (`m_visibleArea`)
- party slots and portraits
- the global object registry (`CGameObjectArray`)
- the pathfinding/search thread state
- quick buttons, timers, saves, options, journal, world map
- `CRuleTables`

This is the class to open when you want the answer to "where does the game remember that?"

Practical mental model:

```text
CInfGame
  = "session/game state"
  = party + areas + global registries + rules + save/load + world timer
```

### 3.4 Areas

[src/CGameArea.h](src/CGameArea.h) is the owner of a loaded `.ARE` plus its runtime state.

It combines:

- area headers and file-derived state
- `CInfinity` for rendering
- `CSearchBitmap` for collision/search-map costs
- `CVisibilityMap`
- ambient sounds and music state
- sorted object lists for rendering and updates
- area-local variables and notes

Important methods:

- `Unmarshal`: build runtime state from area data
- `OnActivation` / `OnDeactivation`: enter/leave visible-area status
- `AIUpdate`: per-area update work
- `Render`: draw the area and the objects in it
- `SetTimeOfDay`: synchronize lighting/day-night state with the world timer

Practical mental model:

```text
CGameArea
  = "one loaded map plus everything needed to simulate and render it"
```

### 3.5 Object Model

The gameplay object hierarchy starts here:

- [src/CGameObject.h](src/CGameObject.h)
- [src/CGameAIBase.h](src/CGameAIBase.h)
- [src/CGameSprite.h](src/CGameSprite.h)

```text
CGameObject
  -> CGameAIBase
     -> CGameSprite
     -> CGameDoor
     -> CGameContainer
     -> CGameTrigger
     -> CGameStatic
     -> CGameTiledObject
```

What each layer means:

- `CGameObject`: generic thing with position, area, rendering, and update hooks
- `CGameAIBase`: scripts, triggers, action queue, timers, script names
- `CGameSprite`: creatures, party members, NPCs, combatants, spellcasters

`CGameSprite` is one of the main "gravity wells" in the codebase. It ties together:

- AI
- animation
- equipment
- spellbooks
- derived stats
- effects
- pathing and movement
- portraits and UI feedback
- sounds and dialog state

### 3.6 Rendering Stack

The rendering stack is layered.

Bottom layer:

- [src/CVideo.h](src/CVideo.h): video system bootstrap
- [src/CVidMode.h](src/CVidMode.h): surfaces, flips, fades, drawing primitives

Area/world layer:

- [src/CInfinity.h](src/CInfinity.h): map viewport, WED/TIS rendering, scrolling, weather, lightning, day-night

Sprite/image layer:

- [src/CGameAnimation.h](src/CGameAnimation.h)
- [src/CGameAnimationType.h](src/CGameAnimationType.h)
- `CGameAnimationType*` files for concrete animation families
- [src/CVidCell.h](src/CVidCell.h): BAM playback and blitting
- [src/CVidBitmap.h](src/CVidBitmap.h): bitmap wrapper
- [src/CVidPalette.h](src/CVidPalette.h): palette control

Resource-backed image layer:

- [src/CResCell.h](src/CResCell.h)
- [src/CResCellHeader.h](src/CResCellHeader.h)
- [src/CResBitmap.h](src/CResBitmap.h)
- [src/CResTile.h](src/CResTile.h)
- [src/CResWED.h](src/CResWED.h)

Useful rule of thumb:

- if it is a map/viewport problem, start in `CInfinity`
- if it is a creature animation problem, start in `CGameSprite` or `CGameAnimation`
- if it is a BAM frame/palette/blit problem, start in `CVidCell`
- if it is a surface/fade/flip problem, start in `CVidMode`

### 3.7 UI Stack

UI is data-driven and screen-specific.

Core files:

- [src/CUIManager.h](src/CUIManager.h)
- [src/CUIPanel.h](src/CUIPanel.h)
- [src/CUIControlBase.h](src/CUIControlBase.h)
- `CUIControl*` files for button/edit/label/scrollbar/slider/text display controls
- [src/CResUI.h](src/CResUI.h): parses UI resources

Structure:

```text
CScreen*
  -> CUIManager
     -> CUIPanel
        -> CUIControl*
```

This is the standard path when debugging a button or panel:

1. find the owning `CScreen*`
2. inspect `EngineActivated` / `TimerAsynchronousUpdate` / control handlers
3. inspect the `CUIManager` panel/control lookup
4. inspect the matching `CUIControl*` subclass
5. inspect `CResUI` if the bug is in panel/control resource parsing

### 3.8 Resource System

The resource system sits under almost everything.

Core files:

- [src/CDimm.h](src/CDimm.h)
- [src/CRes.h](src/CRes.h)
- [src/CResRef.h](src/CResRef.h)

Pattern:

```text
CDimm
  -> locates resources, manages cache/service queues
  -> hands out CRes objects

CRes
  -> base resource object

CResHelper<T, Type>
  -> convenience wrapper embedded inside higher-level classes
```

Concrete resource types include:

- `CResArea`, `CResGame`, `CResCRE`, `CResCHR`, `CResItem`, `CResStore`
- `CResCell`, `CResBitmap`, `CResTile`, `CResWED`, `CResMosaic`
- `CResText`, `CResDLG`, `CResUI`, `CResWave`

If a class stores a `CResHelper<...>`, that usually means:

- it owns a resource reference
- it can request/release resource data lazily
- the actual file parsing happens in the matching `CRes*` class

### 3.9 AI, Scripts, and Pathfinding

AI/scripting files all live under the `CAI*` prefix.

Start here:

- [src/CAIScript.h](src/CAIScript.h)
- [src/CAIAction.h](src/CAIAction.h)
- [src/CAITrigger.h](src/CAITrigger.h)
- [src/CAICondition.h](src/CAICondition.h)
- [src/CAIObjectType.h](src/CAIObjectType.h)
- [src/CGameAIBase.h](src/CGameAIBase.h)

Roles:

- `CAIScript`: parsed script plus response lookup
- `CAICondition` / `CAITrigger`: script predicates and events
- `CAIAction`: executable action requests
- `CAIObjectType`: target filtering and type matching
- `CGameAIBase`: queueing and executing actions on real game objects

Pathfinding/search files:

- [src/CPathSearch.h](src/CPathSearch.h)
- [src/CSearchBitmap.h](src/CSearchBitmap.h)

Important runtime detail:

- path search is threaded
- `CInfGame` owns the search request queues
- `SearchThreadMain` processes `CSearchRequest`
- `CGameObjectArray` has thread-specific share/deny modes for safe access

If you are debugging movement, formation motion, bumping, or "why did this path fail?", you usually need all four of these together:

- `CGameSprite`
- `CGameArea`
- `CSearchBitmap`
- `CPathSearch`

### 3.10 Effects, Stats, Equipment, and Spells

This is the gameplay-rules layer attached most tightly to `CGameSprite`.

Core files:

- [src/CGameEffect.h](src/CGameEffect.h)
- [src/CGameEffectList.h](src/CGameEffectList.h)
- [src/CDerivedStats.h](src/CDerivedStats.h)
- [src/CGameSpriteEquipment.h](src/CGameSpriteEquipment.h)
- [src/CGameSpriteSpells.h](src/CGameSpriteSpells.h)
- [src/CGameStatsSprite.h](src/CGameStatsSprite.h)
- [src/CSpell.h](src/CSpell.h)
- [src/CItem.h](src/CItem.h)

Useful mental model:

- `CGameEffect` = one effect/opcode-like behavior
- `CGameEffectList` = a list of timed or equipped effects
- `CDerivedStats` = computed result after rules, equipment, and effects
- `CGameSpriteEquipment` = what is worn/wielded
- `CGameSpriteSpells` = known/memorized/special spell data

If you are debugging "stat X is wrong", the shortest route is usually:

1. `CGameSprite`
2. `CDerivedStats`
3. `CGameEffectList`
4. `CGameEffect*`
5. `CRuleTables`

### 3.11 Rules, Tables, and Strings

These files are the bridge between engine code and game data tables.

Rules/tables:

- [src/CRuleTables.h](src/CRuleTables.h)
- [src/C2DArray.h](src/C2DArray.h)

Strings/TLK:

- [src/CTlkTable.h](src/CTlkTable.h)
- [src/CStrRes.h](src/CStrRes.h)
- [src/CTlkFileOverride.h](src/CTlkFileOverride.h)

Useful rule of thumb:

- if you see a gameplay rule that smells like a 2DA lookup, start in `CRuleTables`
- if you see a `STRREF`, start in `CTlkTable`
- if you need token replacement inside text, inspect the TLK/token path

### 3.12 Messaging and Multiplayer

Messaging is a major decoupling mechanism.

Core files:

- [src/CMessage.h](src/CMessage.h)
- [src/CNetwork.h](src/CNetwork.h)
- [src/CMultiplayerSettings.h](src/CMultiplayerSettings.h)
- [src/CGamePermission.h](src/CGamePermission.h)

Layers:

- `CMessageHandler`: local queue and dispatch
- `CMessage*`: concrete message types
- `CBaldurMessage`: network/multiplayer message orchestration

If one subsystem triggers behavior in another without a direct call, there is a good chance a `CMessage*` class is involved.

### 3.13 Audio

Core files:

- [src/CSound.h](src/CSound.h)
- [src/CSoundChannel.h](src/CSoundChannel.h)
- [src/CSoundMixer.h](src/CSoundMixer.h)
- [src/music/music.h](src/music/music.h)
- [src/music/audio.h](src/music/audio.h)

Practical split:

- `CSound*` handles engine-side sound channels and playback
- `music/` handles music/audio-file support used by the mixer

## 4. File Families in the Flat `src/` Layout

Because `src/` is flat, prefixes are the fastest way to stay oriented.

| Prefix / file family | Meaning | Start here |
|---|---|---|
| `CChitin*` | application shell, services, update loop | `CChitin.h` |
| `CBaldur*` | game-specific shell and shared screen code | `CBaldurChitin.h`, `CBaldurEngine.h` |
| `CWarp` | base engine/screen abstraction | `CWarp.h` |
| `CScreen*` | individual screens and modes | `CScreenWorld.h` |
| `CUI*` | UI manager, panels, controls | `CUIManager.h` |
| `CInf*` | Infinity-engine-facing gameplay helpers | `CInfGame.h`, `CInfinity.h`, `CInfCursor.h` |
| `CGame*` | runtime gameplay objects and state | `CGameSprite.h` |
| `CAI*` | scripts, triggers, actions, object-type filters | `CAIScript.h` |
| `CRes*` | parsed resource types | `CRes.h` |
| `CVid*` | rendering primitives, BAM/bitmap helpers, palettes | `CVidCell.h` |
| `CVideo*` | low-level video mode / surface system | `CVideo.h`, `CVidMode.h` |
| `CSound*` | sound playback/mixer/channel logic | `CSoundMixer.h` |
| `CTlk*`, `CStrRes` | localized text and voice linkage | `CTlkTable.h` |
| `CRuleTables`, `C2DArray` | gameplay table lookups | `CRuleTables.h` |
| `Icewind*` | IWD2-specific effect/visual helpers | `IcewindCGameEffects.h`, `IcewindCVisualEffect.h` |
| `FileFormat.h`, `BalDataTypes.h`, `ChDataTypes.h` | shared binary structures and constants | open only when you need on-disk formats or shared engine structs |

Also note the only notable subdirectory inside `src/` is:

- `src/music/`: music/audio implementation files

## 5. Follow These Call Chains First

These are the most useful "story lines" in the code.

### 5.1 Process Startup

```text
main.cpp
  -> CBaldurChitin::Init
  -> AddEngine(...)
  -> create CInfGame / CInfCursor
  -> select initial screen
```

Open:

- [src/main.cpp](src/main.cpp)
- [src/CBaldurChitin.cpp](src/CBaldurChitin.cpp)

### 5.2 Entering the World Screen

```text
CBaldurChitin
  -> active engine becomes CScreenWorld
  -> CInfGame::WorldEngineActivated
  -> visible CGameArea::OnActivation
```

Open:

- [src/CScreenWorld.cpp](src/CScreenWorld.cpp)
- [src/CInfGame.cpp](src/CInfGame.cpp)
- [src/CGameArea.cpp](src/CGameArea.cpp)

### 5.3 Loading an Area

```text
CInfGame::LoadArea
  -> CGameArea::Unmarshal
  -> CInfinity::AttachWED
  -> CSearchBitmap::Init
  -> area object population
```

Open:

- [src/CInfGame.cpp](src/CInfGame.cpp)
- [src/CGameArea.cpp](src/CGameArea.cpp)
- [src/CInfinity.cpp](src/CInfinity.cpp)

### 5.4 Rendering a Frame in Gameplay

```text
CScreenWorld::TimerSynchronousUpdate
  -> CInfGame::SynchronousUpdate
  -> CGameArea::Render
  -> CInfinity::Render
  -> CGameObject::Render / CGameSprite::Render
  -> CGameAnimation::Render
  -> CVidCell blits
```

Open:

- [src/CScreenWorld.cpp](src/CScreenWorld.cpp)
- [src/CGameArea.cpp](src/CGameArea.cpp)
- [src/CInfinity.cpp](src/CInfinity.cpp)
- [src/CGameSprite.cpp](src/CGameSprite.cpp)
- [src/CGameAnimation.cpp](src/CGameAnimation.cpp)
- [src/CVidCell.cpp](src/CVidCell.cpp)

### 5.5 Clicking on the World

```text
CScreenWorld input handler
  -> CUIManager or area click path
  -> CGameArea::OnActionButton...
  -> object-specific OnActionButton / actions / messages
```

Open:

- [src/CScreenWorld.cpp](src/CScreenWorld.cpp)
- [src/CGameArea.cpp](src/CGameArea.cpp)
- [src/CGameObject.cpp](src/CGameObject.cpp)
- [src/CGameSprite.cpp](src/CGameSprite.cpp)

### 5.6 Running AI and Scripts

```text
CGameArea::AIUpdate
  -> CGameObject::AIUpdate
  -> CGameAIBase::ProcessAI / ExecuteAction
  -> CAIScript::Find
  -> CAIAction execution
```

Open:

- [src/CGameArea.cpp](src/CGameArea.cpp)
- [src/CGameAIBase.cpp](src/CGameAIBase.cpp)
- [src/CAIScript.cpp](src/CAIScript.cpp)
- [src/CAIAction.cpp](src/CAIAction.cpp)

### 5.7 Pathfinding

```text
CGameSprite movement request
  -> CSearchRequest queued on CInfGame
  -> SearchThreadMain
  -> CPathSearch::FindPath
  -> path returned to sprite
```

Open:

- [src/CGameSprite.cpp](src/CGameSprite.cpp)
- [src/CInfGame.cpp](src/CInfGame.cpp)
- [src/CSearchBitmap.cpp](src/CSearchBitmap.cpp)
- [src/CPathSearch.cpp](src/CPathSearch.cpp)

### 5.8 A UI Control on a Screen

```text
CScreen*::EngineActivated
  -> CUIManager::fInit
  -> CResUI panel/control data
  -> CUIControl* events/rendering
```

Open:

- [src/CUIManager.cpp](src/CUIManager.cpp)
- [src/CResUI.cpp](src/CResUI.cpp)
- the owning `CScreen*.cpp`
- the relevant `CUIControl*.cpp`

### 5.9 STRREF to Displayed Text

```text
gameplay/UI code
  -> STRREF
  -> CTlkTable::Fetch
  -> CStrRes
  -> optional token substitution / sound reference
```

Open:

- [src/CTlkTable.cpp](src/CTlkTable.cpp)
- [src/CStrRes.h](src/CStrRes.h)

## 6. Practical Reading Order for New Contributors

If you want a compact reading path without getting lost, this is a good sequence:

1. [src/main.cpp](src/main.cpp)
2. [src/CChitin.h](src/CChitin.h)
3. [src/CBaldurChitin.h](src/CBaldurChitin.h)
4. [src/CWarp.h](src/CWarp.h)
5. [src/CBaldurEngine.h](src/CBaldurEngine.h)
6. [src/CInfGame.h](src/CInfGame.h)
7. [src/CGameArea.h](src/CGameArea.h)
8. [src/CGameObject.h](src/CGameObject.h)
9. [src/CGameAIBase.h](src/CGameAIBase.h)
10. [src/CGameSprite.h](src/CGameSprite.h)
11. [src/CInfinity.h](src/CInfinity.h)
12. [src/CUIManager.h](src/CUIManager.h)
13. [src/CDimm.h](src/CDimm.h)
14. [src/CRes.h](src/CRes.h)
15. [src/CRuleTables.h](src/CRuleTables.h)
16. [src/CTlkTable.h](src/CTlkTable.h)

That sequence gives you:

- startup
- engine switching
- world ownership
- object hierarchy
- rendering
- UI
- resources
- data/rules

## 7. Common Task-to-File Map

When you know the problem but not the code location, start from here.

| Task | Start in |
|---|---|
| fix a world click/action bug | `CScreenWorld.cpp`, `CGameArea.cpp`, `CGameSprite.cpp` |
| fix a portrait/party-selection bug | `CInfGame.cpp`, `CBaldurEngine.cpp`, owning `CScreen*.cpp` |
| fix area rendering or scrolling | `CGameArea.cpp`, `CInfinity.cpp` |
| fix a BAM animation/blending issue | `CVidCell.cpp`, `CGameAnimation.cpp`, matching `CGameAnimationType*.cpp` |
| fix UI panel/control behavior | owning `CScreen*.cpp`, `CUIManager.cpp`, matching `CUIControl*.cpp` |
| fix a resource parse bug | matching `CRes*.cpp` |
| fix save/load state | `CInfGame.cpp`, `CGameSave.cpp`, marshal/unmarshal methods on object types |
| fix pathfinding | `CGameSprite.cpp`, `CSearchBitmap.cpp`, `CPathSearch.cpp` |
| fix scripts or action execution | `CGameAIBase.cpp`, `CAIScript.cpp`, `CAIAction.cpp` |
| fix combat/effect/stat calculations | `CGameEffect*.cpp`, `CDerivedStats.cpp`, `CGameSprite.cpp`, `CRuleTables.cpp` |
| fix strings or dialog text | `CTlkTable.cpp`, `CResDLG.cpp`, `CGameDialog.cpp` |
| fix music or sound | `CSound*.cpp`, `music/*.cpp` |

## 8. Current Repo Reality

This repository is reconstructed source, not a greenfield engine.

That has a few consequences:

- some names are still placeholders (`sub_*`, `field_*`)
- some high-level functions are still incomplete
- address comments are there for binary cross-reference, not as absolute truth
- patterns are often clearer when you compare code against Ghidra output

Practical advice while navigating:

- trust ownership relationships more than file order
- follow prefixes first, then call chains
- if a behavior looks oddly indirect, check `CMessage*`
- if a value seems to appear from nowhere, check `CRuleTables`, TLK, or `CRes*`
- if a runtime object is shared between systems, check `CGameObjectArray`

## 9. Short Version

If you want the shortest possible map:

- startup: `main.cpp` -> `CBaldurChitin`
- active screen: `CScreen*`
- game state: `CInfGame`
- loaded map: `CGameArea`
- renderer: `CInfinity`
- live actor: `CGameSprite`
- scripts/actions: `CAI*` + `CGameAIBase`
- resources: `CDimm` + `CRes*`
- UI: `CUIManager` + `CUIPanel` + `CUIControl*`
- rules/text: `CRuleTables` + `CTlkTable`

That is usually enough to stop wandering and start reading with intent.
