#!/usr/bin/env python3
"""Manual probe: spawn the ORIGINAL IWD2.exe and answer ONE question about an
OBJECT (creature-targeted) spell cast such as Magic Missile at a goblin:

    When the target is "too far", does the caster WALK into range before casting,
    or does the original cast from where it stands?

Why: our recovered build routes Spell(0x1F) through ForceSpellAction, where a
move-to-range gate was added (commit d567e6f) mirroring FUN_00740270's range
branch.  In game the PC did NOT visibly walk.  This probe reads the ORIGINAL's
ground truth so we can tell a real bug from a generous spell range.

Static RE (confirmed via xrefs):
  * FUN_00740270 (0x740270) is the OBJECT-cast per-tick executor (SEQ_CAST path).
  * It calls the range test FUN_007567F0 (0x7567F0) at 0x7408B6, then -- when out
    of range -- CGameSprite::MoveToObject FUN_0073EDD0 (0x73EDD0) at 0x740A37.
  * Range test verdict: in-range iff  dx*dx + dy*dy <= (range+2)^2  where dx/dy
    are (casterPos - targetPos) divided by the search grid (16 x, 12 y).
    range == -1 (0xFFFF) is unbounded.  range is the chosen ability's WORD@+0x0E.

Hooks (all function ENTRIES, absolute, ImageBase 0x400000):
  0x740270  FUN_00740270   object-cast executor   -- records the PC caster id
  0x7567F0  FUN_007567F0   spell range test       -- target pos + caster pos +
                                                     grid distance + IN/OUT verdict
  0x73EDD0  FUN_0073EDD0   CGameSprite::MoveToObject -- the walk actually fired

A line tagged  <PC>  means the sprite id matches the caster that most recently
entered FUN_00740270 (the casting PC).

Run:  python scripts/frida_cast_range_probe.py
Stop: Ctrl+C
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_intro_trace import GAME_DIR, ORIG_EXE  # noqa: E402

ID_OFF = 0x5C            # CGameObject::m_id
ACTION_COUNT_OFF = 0x474  # m_actionCount (object-cast cast counter free-runs 0..14)
DIR_OFF = 0x5380          # m_nDirection (current facing)
GRID_X = 16              # DAT_0084d6a0 GRID_SQUARE_SIZEX
GRID_Y = 12              # DAT_0084d6a1 GRID_SQUARE_SIZEY

OBJ_EXEC = 0x740270      # FUN_00740270  object-cast executor
RANGE_TEST = 0x7567F0    # FUN_007567F0  spell range test
MOVE_TO_OBJECT = 0x73EDD0  # FUN_0073EDD0 CGameSprite::MoveToObject (the walk)

JS = r"""
'use strict';
const ID_OFF = %ID_OFF%;
const ACTION_COUNT_OFF = %ACTION_COUNT_OFF%;
const DIR_OFF = %DIR_OFF%;
const GRID_X = %GRID_X%;
const GRID_Y = %GRID_Y%;

let pcId = -1;            // sprite that last entered the object-cast executor
let castWindowUntil = 0;  // ms; window in which range/walk lines are "this cast"

function nowMs() { return Date.now(); }
function rd32(p, off) { try { return p.add(off).readS32(); } catch (e) { return -1; } }
function sid(ecx) { return rd32(ecx, ID_OFF); }
function near() { return nowMs() < castWindowUntil; }

// caster->GetPos() : vtable+0x1c returns int* to {x,y} (no by-value copy).
function getPos(ecx) {
  try {
    const vt = ecx.readPointer();
    const fn = new NativeFunction(vt.add(0x1c).readPointer(), 'pointer', ['pointer'], 'thiscall');
    const cp = fn(ecx);
    return [cp.readS32(), cp.add(4).readS32()];
  } catch (e) { return [-999999, -999999]; }
}

Interceptor.attach(ptr(%OBJ_EXEC%), {
  onEnter: function () {
    const ecx = this.context.ecx;
    pcId = sid(ecx);
    castWindowUntil = nowMs() + 4000;
    send({ tag: 'OBJ_EXEC', sprite: pcId,
           actionCount: rd32(ecx, ACTION_COUNT_OFF),
           dir: rd32(ecx, DIR_OFF) & 0xffff });
  }
});

Interceptor.attach(ptr(%RANGE_TEST%), {
  onEnter: function () {
    const ecx = this.context.ecx;          // caster (this)
    const id = sid(ecx);
    if (!(id === pcId && near())) return;  // PC casts only
    // __thiscall stack: [esp+4]=param_2 spell, [esp+8]=param_3 targetPos ptr
    let tx = -999999, ty = -999999;
    try {
      const tp = this.context.esp.add(8).readPointer();
      tx = tp.readS32(); ty = tp.add(4).readS32();
    } catch (e) {}
    const c = getPos(ecx);
    const dx = Math.trunc(c[0] / GRID_X) - Math.trunc(tx / GRID_X);
    const dy = Math.trunc(c[1] / GRID_Y) - Math.trunc(ty / GRID_Y);
    this._d2 = dx * dx + dy * dy;
    this._info = { cx: c[0], cy: c[1], tx: tx, ty: ty, dx: dx, dy: dy };
  },
  onLeave: function (retval) {
    if (this._info === undefined) return;
    send({ tag: 'RANGE_TEST', sprite: pcId, info: this._info, d2: this._d2,
           verdict: retval.toInt32() });  // 1 = IN range, 0 = OUT
  }
});

Interceptor.attach(ptr(%MOVE_TO_OBJECT%), {
  onEnter: function () {
    const id = sid(this.context.ecx);
    send({ tag: 'MOVE_TO_OBJECT', sprite: id, pc: (id === pcId && near()) });
  }
});

send({ tag: 'ready' });
"""


def main() -> int:
    js = (
        JS.replace("%ID_OFF%", hex(ID_OFF))
        .replace("%ACTION_COUNT_OFF%", hex(ACTION_COUNT_OFF))
        .replace("%DIR_OFF%", hex(DIR_OFF))
        .replace("%GRID_X%", str(GRID_X))
        .replace("%GRID_Y%", str(GRID_Y))
        .replace("%OBJ_EXEC%", hex(OBJ_EXEC))
        .replace("%RANGE_TEST%", hex(RANGE_TEST))
        .replace("%MOVE_TO_OBJECT%", hex(MOVE_TO_OBJECT))
    )

    print(f"[*] spawning {ORIG_EXE}")
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)

    def on_message(message, _data):
        if message["type"] != "send":
            print("[frida-error]", message)
            return
        p = message["payload"]
        tag = p.get("tag")
        if tag == "ready":
            print("[*] hooks installed -- load a save, target a DISTANT creature, cast.\n")
        elif tag == "OBJ_EXEC":
            print(f">>> OBJ_EXEC FUN_00740270 sprite={p['sprite']} "
                  f"actionCount={p['actionCount']} dir={p['dir']}")
        elif tag == "RANGE_TEST":
            i = p["info"]
            v = p["verdict"]
            verdict = "IN range -> CAST" if v == 1 else "OUT of range -> WALK"
            print(f"    RANGE_TEST caster=({i['cx']},{i['cy']}) target=({i['tx']},{i['ty']}) "
                  f"gridDelta=({i['dx']},{i['dy']}) gridDist^2={p['d2']}  "
                  f"verdict={v} ({verdict})")
        elif tag == "MOVE_TO_OBJECT":
            tag_pc = "  <PC>  <== caster WALKS toward target" if p.get("pc") else ""
            print(f"    MOVE_TO_OBJECT FUN_0073EDD0 sprite={p['sprite']}{tag_pc}")

    script = session.create_script(js)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] running. Target a DISTANT creature and cast an OBJECT spell once.")
    print("[*] Decisive line:  RANGE_TEST ... verdict=0 (OUT) followed by MOVE_TO_OBJECT <PC>")
    print("[*]   -> original WALKS; if our build doesn't, real bug.")
    print("[*] vs  verdict=1 (IN) and no walk -> range was generous; our build faithful.")
    print("[*] Ctrl+C to stop.\n")
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n[*] stopping")
    finally:
        try:
            session.detach()
        except Exception:
            pass
        try:
            frida.kill(pid)
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
