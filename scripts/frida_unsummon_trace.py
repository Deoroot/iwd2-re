#!/usr/bin/env python3
"""Ground-truth trace of the summon despawn lifecycle on the ORIGINAL IWD2.exe.

Our build arms the delayed SkillUnsummon (op 68) correctly (UNSUMMON_ATTACH +
EFFECT_DELAY_ARM in the debug log) but the effect is never resolved again, so
the creature never despawns.  This probe answers, on the original binary:

  (A) Is the unsummon effect resolved every AI tick while waiting (durType 7)?
  (B) On which sprite/list does it live, and what do durType/duration look like
      across the wait -> fire transition?
  (C) Does CGameEffectSkillUnsummon::ApplyEffect fire at the deadline?

Hooks (function ENTRIES, absolute, ImageBase 0x400000):
  0x55F710  IcewindCGameEffectSummon::ApplyEffect  -- summon cast lands
  0x585C20  IcewindMisc::CreateEffectSkillUnsummon -- tracks the effect ptr
  0x733050  CGameSprite::AddEffect                 -- which sprite/list gets it
  0x4A3030  CGameEffect::ResolveEffect             -- resolve cadence (op 68)
  0x4B4BD0  CGameEffectSkillUnsummon::ApplyEffect  -- the despawn itself

Run:  python scripts/frida_unsummon_trace.py
Then: load slot 2, cast Summon Monster III, leave the game UNPAUSED ~2 min.
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

JS = r"""
'use strict';
const ID_OFF = 0x5c;            // CGameObject::m_id
const FX_ID_OFF = 0x08;         // CGameEffect::m_effectID
const FX_DURTYPE_OFF = 0x1c;    // CGameEffect::m_durationType
const FX_DUR_OFF = 0x20;        // CGameEffect::m_duration
const CHITIN = ptr(0x8cf6dc);   // g_pBaldurChitin
const OP_UNSUMMON = 68;

let trackedFx = null;           // CreateEffectSkillUnsummon return value
let resolveCount = 0;
let lastDurType = -1;

function gameTime() {
  try {
    return CHITIN.readPointer().add(0x1c54).readPointer().add(0x1b78).readU32();
  } catch (e) { return -1; }
}
function rd32(p, off) { try { return p.add(off).readU32(); } catch (e) { return -1; } }
function fxInfo(fx) {
  return { op: rd32(fx, FX_ID_OFF), durType: rd32(fx, FX_DURTYPE_OFF),
           dur: rd32(fx, FX_DUR_OFF), gt: gameTime() };
}

Interceptor.attach(ptr(0x55f710), {
  onEnter: function () {
    const i = fxInfo(this.context.ecx);
    send({ tag: 'SUMMON_APPLY', op: i.op, durType: i.durType, dur: i.dur, gt: i.gt });
  }
});

Interceptor.attach(ptr(0x585c20), {
  onEnter: function () {
    this.dur = this.context.esp.add(8).readU32();
  },
  onLeave: function (retval) {
    trackedFx = retval;
    resolveCount = 0;
    lastDurType = -1;
    send({ tag: 'CREATE_UNSUMMON', fx: retval.toString(), secs: this.dur, gt: gameTime() });
  }
});

Interceptor.attach(ptr(0x733050), {
  onEnter: function () {
    const fx = this.context.esp.add(4).readPointer();
    if (rd32(fx, FX_ID_OFF) !== OP_UNSUMMON) return;
    const list = this.context.esp.add(8).readU32();
    send({ tag: 'ADD_EFFECT', fx: fx.toString(),
           sprite: rd32(this.context.ecx, ID_OFF), list: list,
           durType: rd32(fx, FX_DURTYPE_OFF), dur: rd32(fx, FX_DUR_OFF), gt: gameTime() });
  }
});

Interceptor.attach(ptr(0x4a3030), {
  onEnter: function () {
    const fx = this.context.ecx;
    if (rd32(fx, FX_ID_OFF) !== OP_UNSUMMON) return;
    const i = fxInfo(fx);
    resolveCount++;
    // always log transitions; otherwise first 5 + every 200th
    if (i.durType !== lastDurType || resolveCount <= 5 || resolveCount % 200 === 0) {
      send({ tag: 'RESOLVE', fx: fx.toString(), n: resolveCount,
             sprite: rd32(this.context.esp.add(4).readPointer(), ID_OFF),
             durType: i.durType, dur: i.dur, gt: i.gt,
             transition: i.durType !== lastDurType });
      lastDurType = i.durType;
    }
  }
});

Interceptor.attach(ptr(0x4b4bd0), {
  onEnter: function () {
    send({ tag: 'UNSUMMON_FIRE',
           sprite: rd32(this.context.esp.add(4).readPointer(), ID_OFF),
           gt: gameTime() });
  }
});

send({ tag: 'ready' });
"""


def main() -> int:
    print(f"[*] spawning {ORIG_EXE}")
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)

    def on_message(message, _data):
        if message["type"] != "send":
            print("[frida-error]", message)
            return
        p = message["payload"]
        t = p.get("tag")
        if t == "ready":
            print("[*] hooks installed -- load slot 2, cast Summon Monster III, "
                  "leave UNPAUSED ~2 min.\n")
        elif t == "SUMMON_APPLY":
            print(f">>> SUMMON_APPLY op={p['op']} durType={p['durType']:#x} "
                  f"dur={p['dur']} gameTime={p['gt']}")
        elif t == "CREATE_UNSUMMON":
            print(f"    CREATE_UNSUMMON fx={p['fx']} secs={p['secs']} gameTime={p['gt']}")
        elif t == "ADD_EFFECT":
            print(f"    ADD_EFFECT fx={p['fx']} -> sprite={p['sprite']} list={p['list']} "
                  f"durType={p['durType']:#x} dur={p['dur']} gameTime={p['gt']}")
        elif t == "RESOLVE":
            mark = "  <== durType TRANSITION" if p.get("transition") else ""
            print(f"    RESOLVE#{p['n']} fx={p['fx']} sprite={p['sprite']} "
                  f"durType={p['durType']:#x} deadline={p['dur']} gameTime={p['gt']}{mark}")
        elif t == "UNSUMMON_FIRE":
            print(f"<<< UNSUMMON_FIRE sprite={p['sprite']} gameTime={p['gt']}   "
                  f"<== creature despawns NOW")

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] running. Ctrl+C to stop.\n")
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
