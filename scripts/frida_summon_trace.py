#!/usr/bin/env python3
"""Frida trace for the action-bar Summon Monster path."""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_audio_trace import (
    ORIGINAL_STARTUP_SKIP_SECONDS,
    original_load_driver,
    read_result,
)
from frida_intro_trace import (
    GAME_DIR,
    ORIG_EXE,
    REPO,
    RE_EXE,
    capture_game_surface,
    click_client,
    hover_client,
    post_click_client,
    send_key_to_pid,
    skip_original_startup_movies,
    window_metrics,
)

MAP_FILE = REPO / "build" / "Debug" / "iwd2-re.map"
LINK_IMAGE_BASE = 0x400000

UI_CLICKS = [
    ("cast-spell", (184, 462), 0.45),
    ("cleric-spells", (20, 462), 0.45),
    ("summon-monster", (67, 462), 0.65),
]
PORTRAIT_CENTERS = [
    (527, 460),
    (576, 460),
    (625, 460),
    (674, 460),
    (725, 460),
    (775, 460),
]
GROUND_POS = (421, 318)
GROUND_WORLD_POS = (435, 660)

ORIG_HOOKS = {
    "Area.OnActivation": 0x4750E0,
    "GameAIBase.ForceSpellPointAction": 0x461B80,
    "EffectSummon.ApplyEffect": 0x55F710,
    "EffectSummon.SpawnFromResRef": 0x55FAD0,
    "EffectSummon.PlayGroupVFX": 0x55F930,
    "Projectile.Factory": 0x51EAF0,
    "ButtonArray.SetState": 0x589110,
    "ButtonArray.OnLButtonPressed": 0x58FF20,
    "Game.OnPortraitLClick": 0x5AF870,
    "Game.SetState": 0x594080,
    "Game.UseMagicOnGround": 0x5BB980,
    "Sprite.ReadyCursor": 0x718B30,
    "Sprite.ReadySpell": 0x718CE0,
    "Sprite.UseButtonAction": 0x71A0E0,
    "Chitin.GetObjectGame": 0x40CB10,
    "Game.SynchronousUpdate": 0x5BE900,
    "Game.UnselectAll": 0x5AD7E0,
    "Game.SelectCharacter": 0x5AD8A0,
    "Game.SelectToolbar": 0x5ADAE0,
}

RE_SYMBOLS = {
    "Area.OnActivation": "?OnActivation@CGameArea@@QAEXXZ",
    "Area.OnActionButtonDown": "?OnActionButtonDown@CGameArea@@QAEXABVCPoint@@@Z",
    "Area.OnActionButtonUp": "?OnActionButtonUp@CGameArea@@QAEXABVCPoint@@@Z",
    "Area.OnActionButtonClickGround": "?OnActionButtonClickGround@CGameArea@@QAEXABVCPoint@@@Z",
    "GameAIBase.ForceSpellPointAction": "?ForceSpellPointAction@CGameAIBase@@QAEFXZ",
    "EffectSummon.ApplyEffect": "?ApplyEffect@IcewindCGameEffectSummon@@UAEHPAVCGameSprite@@@Z",
    "EffectSummon.SpawnFromResRef": "?SpawnFromResRef@IcewindCGameEffectSummon@@QAEPAVCGameSprite@@ABV?$CStringT@DV?$StrTraitMFC_DLL@DV?$ChTraitsCRT@D@ATL@@@@@ATL@@PBVCPoint@@@Z",
    "EffectSummon.PlayGroupVFX": "?PlayGroupVFX@IcewindCGameEffectSummon@@QAEXH@Z",
    "SummonGroup.ProjectileFactory": "?CreateSummonGroupProjectile@@YAPAVCProjectileSummonVFX@@H@Z",
    "ButtonArray.SetState": "?SetState@CInfButtonArray@@QAEHHH@Z",
    "ButtonArray.OnLButtonPressed": "?OnLButtonPressed@CInfButtonArray@@QAEXH@Z",
    "UI.PortraitWorld.OnLButtonUp": "?OnLButtonUp@CUIControlPortraitWorld@@UAEXVCPoint@@@Z",
    "UI.PortraitWorld.OnLButtonClick": "?OnLButtonClick@CUIControlPortraitWorld@@UAEXVCPoint@@@Z",
    "UI.ButtonAction.OnLButtonClick": "?OnLButtonClick@CUIControlButtonAction@@UAEXVCPoint@@@Z",
    "Game.OnPortraitLClick": "?OnPortraitLClick@CInfGame@@QAEXK@Z",
    "Game.SetState": "?SetState@CInfGame@@QAEXF@Z",
    "Game.UseMagicOnGround": "?UseMagicOnGround@CInfGame@@QAEXVCPoint@@@Z",
    "Sprite.ReadyCursor": "?ReadyCursor@CGameSprite@@QAEHXZ",
    "Sprite.ReadySpell": "?ReadySpell@CGameSprite@@QAEXFHE@Z",
    "Sprite.UseButtonAction": "?UseButtonAction@CGameSprite@@QAEXVCButtonData@@E@Z",
    "Chitin.GetObjectGame": "?GetObjectGame@CBaldurChitin@@QAEPAVCInfGame@@XZ",
    "Game.SynchronousUpdate": "?SynchronousUpdate@CInfGame@@QAEXXZ",
    "Game.UnselectAll": "?UnselectAll@CInfGame@@QAEXXZ",
    "Game.SelectCharacter": "?SelectCharacter@CInfGame@@QAEHJE@Z",
    "Game.SelectToolbar": "?SelectToolbar@CInfGame@@QAEXXZ",
}

RE_GLOBALS = {
    "g_pBaldurChitin": "?g_pBaldurChitin@@3PAVCBaldurChitin@@A",
}

ORIG_GLOBALS = {
    "g_pBaldurChitin": 0x008CF6DC,
}


def emit(out: Path, payload: dict[str, object]) -> None:
    line = json.dumps(payload, ensure_ascii=True)
    with out.open("a", encoding="utf-8") as f:
        f.write(line + "\n")
    print(line, flush=True)


def map_offsets() -> dict[str, int]:
    wanted = {decorated: name for name, decorated in RE_SYMBOLS.items()}
    found: dict[str, int] = {}
    for line in MAP_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        name = wanted.get(parts[1])
        if name is None:
            continue
        if re.fullmatch(r"[0-9A-Fa-f]{8}", parts[2]):
            found[name] = int(parts[2], 16) - LINK_IMAGE_BASE
    missing = sorted(set(RE_SYMBOLS) - set(found))
    if missing:
        raise SystemExit(f"missing map symbols: {missing}")
    return found


def map_globals() -> dict[str, int]:
    wanted = {decorated: name for name, decorated in RE_GLOBALS.items()}
    found: dict[str, int] = {}
    for line in MAP_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        name = wanted.get(parts[1])
        if name is None:
            continue
        if re.fullmatch(r"[0-9A-Fa-f]{8}", parts[2]):
            found[name] = int(parts[2], 16) - LINK_IMAGE_BASE
    missing = sorted(set(RE_GLOBALS) - set(found))
    if missing:
        raise SystemExit(f"missing map globals: {missing}")
    return found


def make_js(hooks: dict[str, int], globals_: dict[str, int], module_name: str, mode: str) -> str:
    return f"""
'use strict';
const hooks = {json.dumps(hooks)};
const globals = {json.dumps(globals_)};
const isRe = {json.dumps(mode == "re")};
const base = Process.getModuleByName({json.dumps(module_name)}).base;
const startMs = Date.now();
let activationMs = null;
let pendingPortrait = -1;
let lastArea = NULL;

function safe(fn, fallback) {{
  try {{ return fn(); }} catch (e) {{ return fallback; }}
}}

function u8(p) {{ return safe(() => p.readU8(), 0); }}
function u16(p) {{ return safe(() => p.readU16(), 0); }}
function s32(p) {{ return safe(() => p.readS32(), 0); }}
function u32(p) {{ return safe(() => p.readU32(), 0); }}

function resref(p) {{
  return safe(() => {{
    let s = '';
    for (let i = 0; i < 8; i++) {{
      const c = p.add(i).readU8();
      if (c === 0) break;
      s += String.fromCharCode(c);
    }}
    return s;
  }}, '');
}}

function cstr(p) {{
  if (p.isNull()) return '';
  return safe(() => p.readCString(), '');
}}

function cstringObj(p) {{
  if (p.isNull()) return '';
  return safe(() => {{
    const s = p.readPointer();
    if (s.isNull()) return '';
    return s.readCString();
  }}, '');
}}

function point(p) {{
  return {{ x: s32(p), y: s32(p.add(4)) }};
}}

function pointValue(args) {{
  return {{ x: args[0].toInt32(), y: args[1].toInt32() }};
}}

function objInfo(p) {{
  if (p.isNull()) return {{ ptr: p.toString(), id: -1 }};
  return {{
    ptr: p.toString(),
    type: u8(p.add(4)),
    id: s32(p.add(0x5c)),
    pos: {{ x: s32(p.add(6)), y: s32(p.add(10)) }},
  }};
}}

function effectInfo(p) {{
  if (p.isNull()) return {{ ptr: p.toString() }};
  return {{
    ptr: p.toString(),
    effectId: u32(p.add(0x08)),
    targetType: u32(p.add(0x0c)),
    amount: s32(p.add(0x14)),
    durationType: u32(p.add(0x1c)),
    duration: u32(p.add(0x20)),
    res: resref(p.add(0x28)),
    dice: {{ num: u32(p.add(0x30)), size: u32(p.add(0x34)) }},
    source: {{ x: s32(p.add(0x78)), y: s32(p.add(0x7c)) }},
    target: {{ x: s32(p.add(0x80)), y: s32(p.add(0x84)) }},
    sourceId: s32(p.add(0x10c)),
    field190: s32(p.add(0x190)),
  }};
}}

function uiControlInfo(p) {{
  const panel0x08 = safe(() => p.add(0x08).readPointer(), NULL);
  const panel0x06 = safe(() => p.add(0x06).readPointer(), NULL);
  return {{
    ptr: p.toString(),
    id0x0a: safe(() => p.add(0x0a).readU32(), 0xffffffff),
    id0x0c: safe(() => p.add(0x0c).readU32(), 0xffffffff),
    active0x1e: u8(p.add(0x1e)),
    active0x20: u8(p.add(0x20)),
    panel0x08: panel0x08.toString(),
    panel0x06: panel0x06.toString(),
    panelId0x08: panel0x08.isNull() ? 0xffffffff : safe(() => panel0x08.add(0x20).readU32(), 0xffffffff),
    panelId0x06: panel0x06.isNull() ? 0xffffffff : safe(() => panel0x06.add(0x20).readU32(), 0xffffffff),
  }};
}}

function buttonArrayInfo(p, buttonID) {{
  const state = s32(p.add(0x1982));
  const selected = s32(p.add(0x197e));
  const pickerPage = s32(p.add(0x197a));
  let buttonType = -1;
  let grey = -1;
  if (buttonID >= 0 && buttonID < 12) {{
    buttonType = s32(p.add(0x16b0 + buttonID * 4));
    grey = s32(p.add(buttonID * 0x1e0 + 0x1dc));
  }}
  return {{
    ptr: p.toString(),
    state,
    selected,
    pickerPage,
    buttonID,
    buttonType,
    grey,
  }};
}}

function gameInfo(p) {{
  return {{
    ptr: p.toString(),
    state: safe(() => p.add(0x1b96).readS16(), -1),
    icon: safe(() => p.add(0x1b98).readU8(), 0xff),
    tempCursor: safe(() => p.add(0x1ba1).readU8(), 0xff),
  }};
}}

function buttonDataInfo(p) {{
  if (p.isNull()) return {{ ptr: p.toString() }};
  return {{
    ptr: p.toString(),
    icon: resref(p),
    count: safe(() => p.add(0x18).readS16(), -1),
    itemType: safe(() => p.add(0x1a).readS16(), -1),
    itemNum: safe(() => p.add(0x1c).readS16(), -1),
    abilityNum: safe(() => p.add(0x1e).readS16(), -1),
    res: resref(p.add(0x20)),
    targetType: safe(() => p.add(0x28).readS16(), -1),
    nClass: safe(() => p.add(0x36).readU8(), 0xff),
    canUse: safe(() => p.add(0x37).readU8(), 0xff),
    tooltip: safe(() => p.add(0x38).readS16(), -1),
    disabled: safe(() => p.add(0x3a).readU8(), 0xff),
  }};
}}

function spriteUseButtonInfo(p) {{
  return buttonDataInfo(p.add(0x56a0));
}}

function emit(tag, data) {{
  const now = Date.now();
  data.tag = tag;
  data.tMs = now - startMs;
  if (activationMs !== null) data.afterActivationMs = now - activationMs;
  send(data);
}}

function addr(name) {{
  return isRe ? base.add(hooks[name]) : ptr(hooks[name]);
}}

function globalPtr(name) {{
  return isRe ? base.add(globals[name]) : ptr(globals[name]);
}}

function objectGame() {{
  const bald = globalPtr('g_pBaldurChitin').readPointer();
  if (bald.isNull()) return NULL;
  const fn = new NativeFunction(addr('Chitin.GetObjectGame'), 'pointer', ['pointer'], 'thiscall');
  return fn(bald);
}}

function attach(name, callbacks) {{
  if (!(name in hooks)) {{
    emit('hook-skip', {{ name }});
    return;
  }}
  try {{
    Interceptor.attach(addr(name), callbacks);
    emit('hooked', {{ name, addr: addr(name).toString() }});
  }} catch (e) {{
    emit('hook-error', {{ name, error: String(e) }});
  }}
}}

attach('Area.OnActivation', {{
  onEnter() {{
    this.area = this.context.ecx;
    lastArea = this.area;
  }},
  onLeave() {{
    if (activationMs === null) activationMs = Date.now();
    emit('Area.OnActivation.out', {{ area: this.area.toString() }});
  }},
}});

for (const name of ['Area.OnActionButtonDown', 'Area.OnActionButtonUp', 'Area.OnActionButtonClickGround']) {{
  attach(name, {{
    onEnter(args) {{
      emit(name, {{ area: this.context.ecx.toString(), pt: point(args[0]) }});
    }},
  }});
}}

attach('GameAIBase.ForceSpellPointAction', {{
  onEnter() {{
    this.self = this.context.ecx;
    emit('ForceSpellPoint.in', {{ self: objInfo(this.self) }});
  }},
  onLeave(rv) {{
    emit('ForceSpellPoint.out', {{ self: objInfo(this.self), ret: rv.toInt32() }});
  }},
}});

attach('EffectSummon.ApplyEffect', {{
  onEnter(args) {{
    this.effect = this.context.ecx;
    emit('Summon.Apply.in', {{ effect: effectInfo(this.effect), sprite: objInfo(args[0]) }});
  }},
  onLeave(rv) {{
    emit('Summon.Apply.out', {{ effect: effectInfo(this.effect), ret: rv.toInt32() }});
  }},
}});

attach('EffectSummon.SpawnFromResRef', {{
  onEnter(args) {{
    this.effect = this.context.ecx;
    this.res = cstringObj(args[0]);
    emit('Summon.Spawn.in', {{
      effect: effectInfo(this.effect),
      res: this.res,
      target: point(args[1]),
    }});
  }},
  onLeave(rv) {{
    emit('Summon.Spawn.out', {{
      effect: effectInfo(this.effect),
      res: this.res,
      spawned: objInfo(rv),
    }});
  }},
}});

attach('EffectSummon.PlayGroupVFX', {{
  onEnter(args) {{
    emit('Summon.PlayGroupVFX', {{ effect: effectInfo(this.context.ecx), vfx: args[0].toInt32() }});
  }},
}});

attach('Projectile.Factory', {{
  onEnter(args) {{
    this.projectileType = args[0].toInt32();
    emit('Projectile.Factory.in', {{
      projectileType: this.projectileType,
      caster: objInfo(args[1]),
      useHeight: args[2].toInt32(),
    }});
  }},
  onLeave(rv) {{
    emit('Projectile.Factory.out', {{ projectileType: this.projectileType, projectile: rv.toString() }});
  }},
}});

attach('SummonGroup.ProjectileFactory', {{
  onEnter(args) {{
    this.projectileType = args[0].toInt32();
    emit('SummonGroup.ProjectileFactory.in', {{ projectileType: this.projectileType }});
  }},
  onLeave(rv) {{
    emit('SummonGroup.ProjectileFactory.out', {{ projectileType: this.projectileType, projectile: rv.toString() }});
  }},
}});

attach('ButtonArray.SetState', {{
  onEnter(args) {{
    this.self = this.context.ecx;
    this.newState = args[0].toInt32();
    this.arg2 = args[1].toInt32();
    emit('ButtonArray.SetState.in', {{
      before: buttonArrayInfo(this.self, -1),
      newState: this.newState,
      arg2: this.arg2,
    }});
  }},
  onLeave(rv) {{
    emit('ButtonArray.SetState.out', {{
      after: buttonArrayInfo(this.self, -1),
      newState: this.newState,
      arg2: this.arg2,
      ret: rv.toInt32(),
    }});
  }},
}});

attach('ButtonArray.OnLButtonPressed', {{
  onEnter(args) {{
    this.self = this.context.ecx;
    this.buttonID = args[0].toInt32();
    emit('ButtonArray.OnLButtonPressed.in', {{
      button: buttonArrayInfo(this.self, this.buttonID),
    }});
  }},
  onLeave() {{
    emit('ButtonArray.OnLButtonPressed.out', {{
      button: buttonArrayInfo(this.self, this.buttonID),
    }});
  }},
}});

attach('UI.PortraitWorld.OnLButtonUp', {{
  onEnter(args) {{
    this.control = this.context.ecx;
    emit('UI.PortraitWorld.OnLButtonUp.in', {{ control: uiControlInfo(this.control), pt: pointValue(args) }});
  }},
  onLeave() {{
    emit('UI.PortraitWorld.OnLButtonUp.out', {{ control: uiControlInfo(this.control) }});
  }},
}});

attach('UI.PortraitWorld.OnLButtonClick', {{
  onEnter(args) {{
    this.control = this.context.ecx;
    emit('UI.PortraitWorld.OnLButtonClick.in', {{ control: uiControlInfo(this.control), pt: pointValue(args) }});
  }},
  onLeave() {{
    emit('UI.PortraitWorld.OnLButtonClick.out', {{ control: uiControlInfo(this.control) }});
  }},
}});

attach('UI.ButtonAction.OnLButtonClick', {{
  onEnter(args) {{
    this.control = this.context.ecx;
    emit('UI.ButtonAction.OnLButtonClick.in', {{ control: uiControlInfo(this.control), pt: pointValue(args) }});
  }},
  onLeave() {{
    emit('UI.ButtonAction.OnLButtonClick.out', {{ control: uiControlInfo(this.control) }});
  }},
}});

attach('Game.OnPortraitLClick', {{
  onEnter(args) {{
    emit('Game.OnPortraitLClick.in', {{ game: this.context.ecx.toString(), id: args[0].toUInt32() }});
  }},
  onLeave() {{
    emit('Game.OnPortraitLClick.out', {{}});
  }},
}});

attach('Game.SetState', {{
  onEnter(args) {{
    this.game = this.context.ecx;
    this.newState = args[0].toInt32();
    emit('Game.SetState.in', {{ before: gameInfo(this.game), newState: this.newState }});
  }},
  onLeave() {{
    emit('Game.SetState.out', {{ after: gameInfo(this.game), newState: this.newState }});
  }},
}});

attach('Game.UseMagicOnGround', {{
  onEnter(args) {{
    emit('Game.UseMagicOnGround.in', {{ game: gameInfo(this.context.ecx), pt: pointValue(args) }});
  }},
  onLeave() {{
    emit('Game.UseMagicOnGround.out', {{ game: gameInfo(this.context.ecx) }});
  }},
}});

attach('Sprite.ReadyCursor', {{
  onEnter() {{
    this.sprite = this.context.ecx;
    emit('Sprite.ReadyCursor.in', {{ sprite: objInfo(this.sprite), current: spriteUseButtonInfo(this.sprite) }});
  }},
  onLeave(rv) {{
    emit('Sprite.ReadyCursor.out', {{ sprite: objInfo(this.sprite), current: spriteUseButtonInfo(this.sprite), ret: rv.toInt32() }});
  }},
}});

attach('Sprite.ReadySpell', {{
  onEnter(args) {{
    this.sprite = this.context.ecx;
    emit('Sprite.ReadySpell.in', {{ sprite: objInfo(this.sprite), buttonNum: args[0].toInt32(), type: args[1].toInt32(), firstCall: args[2].toInt32() }});
  }},
  onLeave() {{
    emit('Sprite.ReadySpell.out', {{ sprite: objInfo(this.sprite), current: spriteUseButtonInfo(this.sprite) }});
  }},
}});

attach('Sprite.UseButtonAction', {{
  onEnter(args) {{
    this.sprite = this.context.ecx;
    this.buttonData = args[0];
    emit('Sprite.UseButtonAction.in', {{ sprite: objInfo(this.sprite), buttonData: buttonDataInfo(this.buttonData), firstCall: args[1].toInt32() }});
  }},
  onLeave() {{
    emit('Sprite.UseButtonAction.out', {{ sprite: objInfo(this.sprite), argButtonData: buttonDataInfo(this.buttonData), current: spriteUseButtonInfo(this.sprite) }});
  }},
}});

attach('Game.SynchronousUpdate', {{
  onEnter() {{
    if (pendingPortrait < 0) return;
    const id = pendingPortrait;
    pendingPortrait = -1;
    const game = this.context.ecx;
    try {{
      const characterId = s32(game.add(0x382e + (id * 4)));
      const unselectAll = new NativeFunction(addr('Game.UnselectAll'), 'void', ['pointer'], 'thiscall');
      const selectCharacter = new NativeFunction(addr('Game.SelectCharacter'), 'int', ['pointer', 'int32', 'uint8'], 'thiscall');
      const selectToolbar = new NativeFunction(addr('Game.SelectToolbar'), 'void', ['pointer'], 'thiscall');
      unselectAll(game);
      const ret = selectCharacter(game, characterId, 1);
      selectToolbar(game);
      emit('Driver.frida.selectportrait.applied', {{ ok: true, id: id >>> 0, characterId, ret, game: game.toString() }});
    }} catch (e) {{
      emit('Driver.frida.selectportrait.error', {{ id: id >>> 0, game: game.toString(), err: String(e) }});
    }}
  }},
}});

rpc.exports = {{
  selectportrait(id) {{
    pendingPortrait = id >>> 0;
    return {{ ok: true, queued: pendingPortrait }};
  }},
  clickground(x, y) {{
    if (lastArea.isNull()) return {{ ok: false, error: 'no area' }};
    const pt = Memory.alloc(8);
    pt.writeS32(x | 0);
    pt.add(4).writeS32(y | 0);
    const fn = new NativeFunction(addr('Area.OnActionButtonClickGround'), 'void', ['pointer', 'pointer'], 'thiscall');
    emit('Driver.frida.clickground', {{ area: lastArea.toString(), pt: {{ x: x | 0, y: y | 0 }} }});
    fn(lastArea, pt);
    return {{ ok: true }};
  }},
  usemagiconground(x, y) {{
    const game = objectGame();
    if (game.isNull()) return {{ ok: false, error: 'no game' }};
    const fn = new NativeFunction(addr('Game.UseMagicOnGround'), 'void', ['pointer', 'int32', 'int32'], 'thiscall');
    emit('Driver.frida.usemagiconground', {{ game: game.toString(), pt: {{ x: x | 0, y: y | 0 }} }});
    fn(game, x | 0, y | 0);
    return {{ ok: true }};
  }}
}};
"""


def screenshot(pid: int, label: str, out: Path) -> None:
    try:
        path, capture = capture_game_surface(pid, f"summon_{label}")
        emit(out, {"tag": "Driver.screenshot", "label": label, "path": str(path), "capture": capture})
    except Exception as e:
        emit(out, {"tag": "Driver.screenshot-error", "label": label, "err": str(e)})


def count_tag(out: Path, tag: str) -> int:
    count = 0
    try:
        for line in out.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                if json.loads(line).get("tag") == tag:
                    count += 1
            except json.JSONDecodeError:
                continue
    except OSError:
        return 0
    return count


def wait_for_cast_result(out: Path, timeout: float = 8.0) -> dict[str, int | bool]:
    deadline = time.time() + timeout
    last_force = count_tag(out, "ForceSpellPoint.in")
    while time.time() < deadline:
        apply_count = count_tag(out, "Summon.Apply.in")
        spawn_count = count_tag(out, "Summon.Spawn.in")
        projectile_count = count_tag(out, "SummonGroup.ProjectileFactory.in")
        force_count = count_tag(out, "ForceSpellPoint.in")
        if apply_count or spawn_count or projectile_count:
            return {
                "ok": True,
                "force": force_count,
                "apply": apply_count,
                "spawn": spawn_count,
                "projectile": projectile_count,
            }
        last_force = force_count
        time.sleep(0.25)
    return {
        "ok": False,
        "force": last_force,
        "apply": count_tag(out, "Summon.Apply.in"),
        "spawn": count_tag(out, "Summon.Spawn.in"),
        "projectile": count_tag(out, "SummonGroup.ProjectileFactory.in"),
    }


def click_until_button(pid: int, out: Path, name: str, pos: tuple[int, int], delay: float) -> bool:
    for attempt in range(1, 4):
        before = count_tag(out, "ButtonArray.OnLButtonPressed.in")
        emit(out, {"tag": "Driver.click", "target": name, "pos": pos, "attempt": attempt, "window": window_metrics(pid)})
        if not click_client(pid, *pos, activation_click=False, hover_seconds=0.12):
            emit(out, {"tag": "Driver.error", "stage": name, "err": "window not found"})
            return False
        time.sleep(delay)
        if count_tag(out, "ButtonArray.OnLButtonPressed.in") > before:
            return True
        emit(out, {"tag": "Driver.click.retry", "target": name, "attempt": attempt})
        time.sleep(0.35)
    return True


def drive_summon(pid: int, out: Path, pc_slot: int, frida_script) -> None:
    screenshot(pid, "loaded", out)
    portrait_id = max(0, min(pc_slot - 1, 5))
    portrait_pos = PORTRAIT_CENTERS[portrait_id]
    emit(out, {"tag": "Driver.click", "target": "portrait", "pcSlot": pc_slot, "portraitId": portrait_id, "pos": portrait_pos, "window": window_metrics(pid)})
    if not click_client(pid, *portrait_pos, activation_click=True, hover_seconds=0.12):
        emit(out, {"tag": "Driver.error", "stage": "select-pc", "err": "window not found"})
        return
    time.sleep(0.65)
    screenshot(pid, f"after_select_pc{pc_slot}", out)
    for name, pos, delay in UI_CLICKS:
        if not click_until_button(pid, out, name, pos, delay):
            return
        if name != "summon-monster":
            screenshot(pid, f"after_{name}", out)

    before_force = count_tag(out, "ForceSpellPoint.in")
    emit(out, {"tag": "Driver.frida-click", "target": "ground", "pos": GROUND_POS, "world": GROUND_WORLD_POS, "window": window_metrics(pid)})
    try:
        ret = frida_script.exports_sync.usemagiconground(*GROUND_WORLD_POS)
        emit(out, {"tag": "Driver.frida-click.result", "target": "ground", "result": ret})
    except Exception as e:
        emit(out, {"tag": "Driver.error", "stage": "ground", "err": str(e)})
        return
    time.sleep(0.5)
    if count_tag(out, "ForceSpellPoint.in") <= before_force:
        emit(out, {"tag": "Driver.frida-clickground-fallback", "target": "ground", "world": GROUND_WORLD_POS, "window": window_metrics(pid)})
        try:
            ret = frida_script.exports_sync.clickground(*GROUND_WORLD_POS)
            emit(out, {"tag": "Driver.frida-clickground-fallback.result", "target": "ground", "result": ret})
        except Exception as e:
            emit(out, {"tag": "Driver.frida-clickground-fallback.error", "target": "ground", "err": str(e)})
        time.sleep(0.5)
    if count_tag(out, "ForceSpellPoint.in") <= before_force:
        emit(out, {"tag": "Driver.ground-fallback", "target": "physical-click", "pos": GROUND_POS, "window": window_metrics(pid)})
        click_client(pid, *GROUND_POS, activation_click=False, hover_seconds=0.12)
    result = wait_for_cast_result(out)
    emit(out, {"tag": "Driver.cast-result-wait", "result": result, "window": window_metrics(pid)})
    screenshot(pid, "after_ground_cast", out)
    time.sleep(2.0)
    screenshot(pid, "after_ground_final", out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["re", "original"], default="re")
    ap.add_argument("--slot", type=int, default=1)
    ap.add_argument("--pc-slot", type=int, default=4)
    ap.add_argument("--timeout", type=float, default=80.0)
    ap.add_argument("--post-click-seconds", type=float, default=20.0)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--original-startup-skip-seconds", type=float, default=ORIGINAL_STARTUP_SKIP_SECONDS)
    ns = ap.parse_args()

    label = f"{ns.mode}_slot{ns.slot}"
    out = ns.output or (REPO / f"tmp_summon_trace_{label}.jsonl")
    out.write_text("", encoding="utf-8")

    result_path = Path(tempfile.gettempdir()) / f"iwd2-re-summon-{uuid.uuid4().hex}.txt"
    env = os.environ.copy()
    spawned = True
    if ns.mode == "re":
        env["IWD2_RE_AUTO_RESULT"] = str(result_path)
        env["IWD2_RE_AUTO_ACTION"] = "load"
        env["IWD2_RE_AUTO_SLOT"] = str(ns.slot)
        emit(out, {"tag": "Driver.before-spawn", "exe": str(RE_EXE), "slot": ns.slot})
        pid = frida.spawn(str(RE_EXE), env=env, cwd=str(GAME_DIR))
        emit(out, {"tag": "Driver.after-spawn", "pid": pid})
        hooks = map_offsets()
        globals_ = map_globals()
        emit(out, {"tag": "Driver.after-map", "hooks": len(hooks), "globals": len(globals_)})
        module_name = RE_EXE.name
    else:
        emit(out, {"tag": "Driver.before-spawn", "exe": str(ORIG_EXE), "slot": ns.slot})
        pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
        emit(out, {"tag": "Driver.after-spawn", "pid": pid})
        hooks = ORIG_HOOKS
        globals_ = ORIG_GLOBALS
        emit(out, {"tag": "Driver.after-map", "hooks": len(hooks), "globals": len(globals_)})
        module_name = ORIG_EXE.name

    session = frida.attach(pid)
    emit(out, {"tag": "Driver.after-attach"})
    loaded_at = 0.0
    driven = False
    stop_movies = threading.Event()

    def on_message(message, _data):
        nonlocal loaded_at
        payload = message["payload"] if message["type"] == "send" else {"tag": "ERROR", "message": str(message)}
        if payload.get("tag") == "Area.OnActivation.out" and not loaded_at:
            loaded_at = time.time()
        emit(out, payload)

    script = session.create_script(make_js(hooks, globals_, module_name, ns.mode))
    emit(out, {"tag": "Driver.after-create-script"})
    script.on("message", on_message)
    script.load()
    emit(out, {"tag": "Driver.after-load-script"})
    frida.resume(pid)
    emit(out, {"tag": "Driver.after-resume"})

    if ns.mode == "original":
        threading.Thread(
            target=skip_original_startup_movies,
            args=(pid, stop_movies),
            daemon=True,
        ).start()
        threading.Thread(
            target=original_load_driver,
            args=(pid, ns.slot, ns.original_startup_skip_seconds, lambda p: emit(out, p)),
            daemon=True,
        ).start()

    emit(out, {"tag": "Driver.spawned", "pid": pid, "mode": ns.mode, "slot": ns.slot, "output": str(out)})
    deadline = time.time() + ns.timeout
    try:
        while time.time() < deadline:
            if ns.mode == "re" and not loaded_at and result_path.exists():
                result = read_result(result_path)
                emit(out, {"tag": "Driver.loaded-result", "result": result, "window": window_metrics(pid)})
                if result.get("status") == "loaded":
                    loaded_at = time.time()
                else:
                    return 1
            if loaded_at and not driven and time.time() - loaded_at > 2.0:
                drive_summon(pid, out, ns.pc_slot, script)
                driven = True
            if driven and time.time() - loaded_at > ns.post_click_seconds:
                screenshot(pid, "final", out)
                return 0
            time.sleep(0.25)
        emit(out, {"tag": "Driver.timeout", "driven": driven, "loaded": bool(loaded_at)})
        return 1
    finally:
        stop_movies.set()
        try:
            if spawned:
                frida.kill(pid)
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
