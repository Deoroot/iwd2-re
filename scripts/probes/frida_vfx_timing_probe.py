#!/usr/bin/env python3
"""Ground-truth the TIMING of the Magic Missile impact VFX/SFX (opcode 233) on
the ORIGINAL IWD2.exe: does it fire at CAST (launch) or at ARRIVAL?

User reports our recovered op-233 (IcewindCGameEffectVisualSpellHit::ApplyEffect,
0x5601E0) flashes at cast instead of when each missile lands. To know what the
correct timing is, watch the original: hook the launch, the per-missile arrival,
the effect delivery, and both effect ApplyEffects, then read the ORDER + the
wall-clock gap between launch and the op-233 apply.

Hooks (function ENTRIES only, absolute, ImageBase 0x400000, no ASLR):
  0x530C90  CProjectileSPMAGMIS::Fire        -- launcher fires (CAST, t0)
  0x52C050  CProjectileTravelling::Fire      -- each sub-missile launched
  0x529FB0  CProjectile::OnArrival           -- a missile reaches target (ARRIVAL)
  0x52A1A0  CProjectile::DeliverEffects       -- arrival -> queue carried effects
  0x4A7900  CGameEffectDamage::ApplyEffect    -- op 12 damage applied
  0x5601E0  IcewindCGameEffectVisualSpellHit::ApplyEffect -- op 233 VFX/SFX  <== KEY

Read conclusion from the printed timeline:
  LAUNCH at t0, then FIRE x5, then [flight] then ARRIVAL/DELIVER, then APPLY.
  - op-233 APPLY clustered with ARRIVAL (big gap after LAUNCH)  -> correct = at arrival
  - op-233 APPLY right after LAUNCH (tiny gap)                  -> at cast

Projectile hooks read m_id (+0x5C), m_targetId (+0x76), vtable (*this).
Effect hooks read m_dwFlags (+? via known base) only for context; the hook
address alone identifies the opcode.

Run:  python scripts/frida_vfx_timing_probe.py
Then: load combat save (slot 2), cast Magic Missile at an enemy ONCE.
Stop: Ctrl+C  (full JSON also in tmp_vfx_timing.log)
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_intro_trace import GAME_DIR, ORIG_EXE  # noqa: E402

LOG_PATH = SCRIPT_DIR.parent / "tmp_vfx_timing.log"

JS = r"""
const LAUNCH   = ptr(0x530c90);  // CProjectileSPMAGMIS::Fire
const TRAVFIRE = ptr(0x52c050);  // CProjectileTravelling::Fire
const ONARRIVE = ptr(0x529fb0);  // CProjectile::OnArrival
const DELIVER  = ptr(0x52a1a0);  // CProjectile::DeliverEffects
const DMGAPPLY = ptr(0x4a7900);  // CGameEffectDamage::ApplyEffect
const VFXAPPLY = ptr(0x5601e0);  // IcewindCGameEffectVisualSpellHit::ApplyEffect

let seq = 0;

function s32(p){ try { return p.readS32(); } catch(e){ return null; } }
function u32(p){ try { return p.readU32() >>> 0; } catch(e){ return null; } }

function proj(tag, addr){
  Interceptor.attach(addr, { onEnter(a){ const t=this.context.ecx;
    send({ tag, seq: seq++, this:t.toString(), vt:(u32(t)||0).toString(16),
           id:s32(t.add(0x5c)), tid:s32(t.add(0x76)) }); }});
}
function eff(tag, addr){
  Interceptor.attach(addr, { onEnter(a){ const t=this.context.ecx;
    // __thiscall: this=ecx, ApplyEffect(CGameSprite*) -> sprite = a[0] (stack).
    // The hook address alone identifies the opcode.
    send({ tag, seq: seq++, this:t.toString(),
           sprite:a[0].toInt32() }); }});
}

proj('LAUNCH',  LAUNCH);
proj('FIRE',    TRAVFIRE);
proj('ARRIVE',  ONARRIVE);
proj('DELIVER', DELIVER);
eff('DMG',      DMGAPPLY);
eff('VFX',      VFXAPPLY);
"""


def main() -> int:
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    log_f = open(LOG_PATH, "w", encoding="utf-8")

    t0 = {"v": None}

    def on_message(message, data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                print("[JS ERROR]", message.get("description"))
            return
        p = message["payload"]
        now = time.time()
        if t0["v"] is None:
            t0["v"] = now
        dt = (now - t0["v"]) * 1000.0
        p["dt_ms"] = round(dt, 1)
        log_f.write(json.dumps(p) + "\n")
        log_f.flush()
        tag = p.get("tag")
        if tag in ("LAUNCH", "FIRE", "ARRIVE", "DELIVER"):
            print(f"[{dt:8.1f}ms] {tag:8s} seq={p['seq']:<3} id={p['id']} tid={p['tid']} "
                  f"vt={p['vt']} this={p['this'][-6:]}")
        else:
            print(f"[{dt:8.1f}ms] {tag:8s} seq={p['seq']:<3} <== effect apply  this={p['this'][-6:]}")

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] IWD2.exe spawned under Frida.")
    print("[*] 1) Load combat save (slot 2).  2) Cast Magic Missile at an enemy ONCE.")
    print("[*] Read the timeline: LAUNCH=t0, FIRE x5, [flight], ARRIVE/DELIVER, then APPLY.")
    print("[*]   op-233 VFX clustered with ARRIVE (big gap after LAUNCH) -> correct=arrival")
    print("[*]   op-233 VFX right after LAUNCH (tiny gap)                 -> at cast")
    print("[*] Full JSON -> tmp_vfx_timing.log.  Ctrl+C to stop.\n")
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n[*] stopping")
    finally:
        log_f.close()
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
