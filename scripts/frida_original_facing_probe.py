#!/usr/bin/env python3
"""Manual probe: spawn the ORIGINAL IWD2.exe and answer ONE question about a
ground (point) spell cast such as Summon Monster:

    Does the caster (PC) get oriented toward the click, and by which code?

Static RE established:
  * UseMagicOnGround case 4 (summon) queues ForceSpellPoint(114) -- a *force*
    action. Its handler CGameAIBase::ForceSpellPointAction (0x461B80) fires the
    effect but contains NO orient code.
  * The only caster-orient for a cast lives in the normal-cast sequence executor
    FUN_00742840, reached via the SEQ_CAST sprite-sequence dispatcher FUN_00728F80.
    It posts CMessageSetDirection -> CGameSprite::SetDirection(CPoint) (gradual
    turn via m_nNewDirection -- a SetFacing hook does NOT see it).

So the decisive runtime questions for a summon cast are:
  (A) Which action handler runs?  ForceSpellPointAction(114)?
  (B) Does FUN_00742840 (cast executor, the thing that orients) run for the PC?
  (C) Does CGameSprite::SetDirection(CPoint) fire for the PC -- and who called it?

Hooks (all function ENTRIES, absolute addresses, ImageBase 0x400000):
  0x5BB980  UseMagicOnGround        -- cast initiated
  0x461B80  ForceSpellPointAction   -- force point-cast handler (records casterId)
  0x742840  FUN_00742840            -- normal-cast executor (orients) <== KEY
  0x728F80  FUN_00728F80            -- SEQ_CAST sequence dispatcher
  0x706B40  SetDirection(CPoint)    -- gradual turn toward a point (+ backtrace)
  0x706F80  SetFacing(SHORT)        -- instant facing (+ backtrace)

A line tagged  <PC>  means the sprite id matches the caster that just ran
ForceSpellPointAction (i.e. it is the casting PC, not the spawned creature).

Run:  python scripts/frida_original_facing_probe.py
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

ID_OFF = 0x5C        # CGameObject::m_id
CAST_TIMER_OFF = 0x474   # ForceSpellPointAction cast countdown (param_1[0x11d])
PT_X_OFF = 0x540     # cast target point x (param_1[0x150])
PT_Y_OFF = 0x544     # cast target point y (param_1[0x151])

USE_MAGIC = 0x5BB980
FORCE_SPELL_POINT = 0x461B80
CAST_EXEC = 0x742840         # FUN_00742840 -- orients then fires
SEQ_DISPATCH = 0x728F80      # FUN_00728F80
SET_DIRECTION = 0x706B40     # CGameSprite::SetDirection(const CPoint&)
SET_FACING = 0x706F80        # CGameSprite::SetFacing(SHORT)

JS = r"""
'use strict';
const ID_OFF = %ID_OFF%;
const CAST_TIMER_OFF = %CAST_TIMER_OFF%;
const PT_X_OFF = %PT_X_OFF%;
const PT_Y_OFF = %PT_Y_OFF%;

let casterId = -1;          // PC that last entered ForceSpellPointAction
let castWindowUntil = 0;    // ms; "near a cast" window for backtraces

function nowMs() { return Date.now(); }
function rd32(p, off) { try { return p.add(off).readS32(); } catch (e) { return -1; } }
function sid(ecx) { return rd32(ecx, ID_OFF); }
function isPC(id) { return id === casterId && casterId !== -1; }
function near() { return nowMs() < castWindowUntil; }
function bt(ctx) {
  try {
    return Thread.backtrace(ctx, Backtracer.ACCURATE)
                 .slice(0, 10).map(function (a) { return a.toString(); });
  } catch (e) { return ['<bt-failed:' + e + '>']; }
}

Interceptor.attach(ptr(%USE_MAGIC%), {
  onEnter: function () {
    castWindowUntil = nowMs() + 6000;
    send({ tag: 'USE_MAGIC' });
  }
});

Interceptor.attach(ptr(%FORCE_SPELL_POINT%), {
  onEnter: function () {
    const ecx = this.context.ecx;
    casterId = sid(ecx);
    castWindowUntil = nowMs() + 6000;
    send({
      tag: 'FORCE_SPELL_POINT', caster: casterId,
      timer: rd32(ecx, CAST_TIMER_OFF),
      px: rd32(ecx, PT_X_OFF), py: rd32(ecx, PT_Y_OFF)
    });
  }
});

Interceptor.attach(ptr(%CAST_EXEC%), {
  onEnter: function () {
    const id = sid(this.context.ecx);
    send({ tag: 'CAST_EXEC', sprite: id, pc: isPC(id) });
  }
});

Interceptor.attach(ptr(%SEQ_DISPATCH%), {
  onEnter: function () {
    const id = sid(this.context.ecx);
    if (isPC(id) && near()) send({ tag: 'SEQ_DISPATCH', sprite: id, pc: true });
  }
});

Interceptor.attach(ptr(%SET_DIRECTION%), {
  onEnter: function () {
    const ecx = this.context.ecx;
    const id = sid(ecx);
    let px = -1, py = -1;
    try { const cp = this.context.esp.add(4).readPointer(); px = cp.readS32(); py = cp.add(4).readS32(); } catch (e) {}
    const n = near();
    send({ tag: 'SET_DIRECTION', sprite: id, pc: isPC(id), px: px, py: py,
           bt: (n ? bt(this.context) : []) });
  }
});

Interceptor.attach(ptr(%SET_FACING%), {
  onEnter: function () {
    const ecx = this.context.ecx;
    const id = sid(ecx);
    const dir = this.context.esp.add(4).readU32() & 0xFFFF;
    const n = near();
    send({ tag: 'SET_FACING', sprite: id, pc: isPC(id), dir: dir,
           bt: (n ? bt(this.context) : []) });
  }
});

send({ tag: 'ready' });
"""


def main() -> int:
    js = (
        JS.replace("%ID_OFF%", hex(ID_OFF))
        .replace("%CAST_TIMER_OFF%", hex(CAST_TIMER_OFF))
        .replace("%PT_X_OFF%", hex(PT_X_OFF))
        .replace("%PT_Y_OFF%", hex(PT_Y_OFF))
        .replace("%USE_MAGIC%", hex(USE_MAGIC))
        .replace("%FORCE_SPELL_POINT%", hex(FORCE_SPELL_POINT))
        .replace("%CAST_EXEC%", hex(CAST_EXEC))
        .replace("%SEQ_DISPATCH%", hex(SEQ_DISPATCH))
        .replace("%SET_DIRECTION%", hex(SET_DIRECTION))
        .replace("%SET_FACING%", hex(SET_FACING))
    )

    print(f"[*] spawning {ORIG_EXE}")
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)

    def tag_pc(p):
        return "  <PC>" if p.get("pc") else ""

    def on_message(message, _data):
        if message["type"] != "send":
            print("[frida-error]", message)
            return
        p = message["payload"]
        tag = p.get("tag")
        if tag == "ready":
            print("[*] hooks installed -- load a save, ready the summon, click the ground.\n")
        elif tag == "USE_MAGIC":
            print(">>> UseMagicOnGround (ground cast initiated)")
        elif tag == "FORCE_SPELL_POINT":
            print(f"    ACTION ForceSpellPointAction(114) caster={p['caster']} "
                  f"castTimer={p['timer']} point=({p['px']},{p['py']})")
        elif tag == "CAST_EXEC":
            print(f"    CAST_EXEC FUN_00742840 sprite={p['sprite']}{tag_pc(p)}"
                  f"{'   <== executor RAN for the PC (orient path)' if p.get('pc') else ''}")
        elif tag == "SEQ_DISPATCH":
            print(f"    SEQ_DISPATCH FUN_00728F80 sprite={p['sprite']}{tag_pc(p)}")
        elif tag == "SET_DIRECTION":
            print(f"    SET_DIRECTION sprite={p['sprite']} point=({p['px']},{p['py']}){tag_pc(p)}"
                  f"{'   <== PC TURNS toward point' if p.get('pc') else ''}")
            for a in p.get("bt", []):
                print(f"        {a}")
        elif tag == "SET_FACING":
            print(f"    SET_FACING sprite={p['sprite']} dir={p['dir']}{tag_pc(p)}")
            for a in p.get("bt", []):
                print(f"        {a}")

    script = session.create_script(js)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] running. Cast the summon ONCE on the ground.")
    print("[*] Look for:  CAST_EXEC ... <PC>   (orient path runs)  vs only")
    print("[*]            ACTION ForceSpellPointAction with no PC CAST_EXEC/SET_DIRECTION")
    print("[*]            (force path, no orient -> recovered build already faithful).")
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
