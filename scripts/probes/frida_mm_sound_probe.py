#!/usr/bin/env python3
"""Ground-truth the Magic Missile SOUND + VFX-selection fingerprint on the
ORIGINAL IWD2.exe.

User reports our recovered Magic Missile sounds different from the stock engine.
To know what "correct" is, capture EVERY sound the original emits for one MM cast
-- which resref, from which projectile, how many times, and when -- plus the
spell-hit overlay selection (op-233 typeIndex), so our build can be diffed
against it event by event.

Hooks (function ENTRIES only, absolute, ImageBase 0x400000, no ASLR):
  0x530C90  CProjectileSPMAGMIS::Fire        -- launch (t0 marker)
  0x529FB0  CProjectile::OnArrival           -- per-missile arrival marker
  0x5601E0  CGameEffectVisualSpellHitIWD::ApplyEffect (op-233) -- impact effect;
            reads m_dwFlags(+0x1c) = the DecodeSpellHitProjectile typeIndex
            (Magic Missile is expected to be Type 3 = Invocation)
  0x52A4E0  CProjectile::PlaySound           -- THE projectile sound: the 8-byte
            CResRef is passed by value at [esp+4]; arg2=loop, arg3=fireAndForget.
            this=ecx, *this=vtable identifies the projectile class.
  0x7A9DB0  CSound::Play                     -- the low-level positional emit;
            counts ALL sounds.  If a CSound::Play `this` does NOT equal a recent
            projectile's this+0xEE (its embedded m_sound), it is a NON-projectile
            sound (cast / launch / UI) we are not seeing via PlaySound.

Known projectile vtables (to label the PlaySound caller):
  0x8510A4 MMissiT (homing sub-missile)   0x851234 SummonVFX (spell-hit overlay)
  0x84E0C4 SPMAGMIS (launcher)            0x84D9C4 Travelling   0x84E58C Arrow

Read the timeline + the Ctrl+C summary:
  - resref histogram (e.g. 5x EFF_M06) = the impact-sound fingerprint
  - any CSound::Play with no matching projectile = an extra cast/launch sound
  - op-233 typeIndex per impact = the overlay/sound the school selects

Run:  python -u scripts/frida_mm_sound_probe.py
Then: load combat save (slot 2), cast Magic Missile at an enemy ONCE.
Stop: Ctrl+C.  Full JSON also in tmp_mm_sound_probe.log
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

LOG_PATH = SCRIPT_DIR.parent / "tmp_mm_sound_probe.log"

VTABLES = {
    "8510a4": "MMissiT",
    "851234": "SummonVFX",
    "84e0c4": "SPMAGMIS",
    "84d9c4": "Travelling",
    "84e58c": "Arrow",
}

JS = r"""
'use strict';
const LAUNCH = ptr(0x530c90);  // SPMAGMIS::Fire
const ARRIVE = ptr(0x529fb0);  // CProjectile::OnArrival
const OP233  = ptr(0x5601e0);  // VisualSpellHitIWD::ApplyEffect
const PSND   = ptr(0x52a4e0);  // CProjectile::PlaySound
const CPLAY  = ptr(0x7a9db0);  // CSound::Play

function s32(p, o){ try { return p.add(o).readS32(); } catch(e){ return null; } }
function vt(p){ try { return (p.readU32() >>> 0).toString(16); } catch(e){ return '?'; } }
function resref(p){ // 8-byte CResRef, null-padded
  try { let s=''; for (let i=0;i<8;i++){ const c=p.add(i).readU8(); if(c===0) break; s+=String.fromCharCode(c);} return s; }
  catch(e){ return '?'; }
}

Interceptor.attach(LAUNCH, { onEnter() {
  send({ tag:'LAUNCH', this:this.context.ecx.toString() });
}});

Interceptor.attach(ARRIVE, { onEnter() { const t=this.context.ecx;
  send({ tag:'ARRIVE', this:t.toString(), vt:vt(t), tid:s32(t,0x76) });
}});

Interceptor.attach(OP233, { onEnter() { const t=this.context.ecx;
  send({ tag:'OP233', this:t.toString(), typeIndex:s32(t,0x1c) });
}});

Interceptor.attach(PSND, { onEnter(args) { const t=this.context.ecx;
  // __thiscall: this=ecx; CResRef passed BY VALUE -> 8 bytes at [esp+4].
  const rr = resref(this.context.esp.add(4));
  send({ tag:'PSND', this:t.toString(), vt:vt(t), resref:rr,
         loop:args[2].toInt32(), ff:args[3].toInt32(),
         sound:t.add(0xee).toString() });   // embedded m_sound @ +0xEE
}});

Interceptor.attach(CPLAY, { onEnter(args) { const t=this.context.ecx;
  // CSound holds its current CResRef at +0xC (8 bytes, null-padded).
  send({ tag:'CPLAY', this:t.toString(), resref:resref(t.add(0xc)),
         ret:this.returnAddress.toString(),
         x:args[0].toInt32(), y:args[1].toInt32() });
}});

send({ tag:'ready' });
"""


def main() -> int:
    log_f = open(LOG_PATH, "w", encoding="utf-8")
    t0 = {"v": None}
    res_hist: dict[str, int] = {}
    counts = {"LAUNCH": 0, "ARRIVE": 0, "OP233": 0, "PSND": 0, "CPLAY": 0}
    recent_sounds: list[str] = []   # projectile m_sound pointers seen at PSND
    orphan_cplay = {"n": 0}

    def emit(s):
        print(s, flush=True)
        log_f.write(s + "\n")
        log_f.flush()

    def on_message(message, _data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                emit(f"[JS ERROR] {message.get('description')}")
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
        if tag in counts:
            counts[tag] += 1

        if tag == "ready":
            emit("[*] hooks installed -- load slot 2, cast Magic Missile ONCE at an enemy.\n")
        elif tag == "LAUNCH":
            emit(f"[{dt:8.1f}ms] >>> LAUNCH  SPMAGMIS::Fire  this={p['this'][-6:]}")
        elif tag == "ARRIVE":
            cls = VTABLES.get(p["vt"], p["vt"])
            emit(f"[{dt:8.1f}ms] <<< ARRIVE  {cls:9s} this={p['this'][-6:]} tid={p['tid']}")
        elif tag == "OP233":
            emit(f"[{dt:8.1f}ms]     OP233   typeIndex={p['typeIndex']} this={p['this'][-6:]}")
        elif tag == "PSND":
            cls = VTABLES.get(p["vt"], p["vt"])
            rr = p["resref"] or "(empty)"
            res_hist[rr] = res_hist.get(rr, 0) + 1
            recent_sounds.append(p["sound"])
            emit(f"[{dt:8.1f}ms]   * PSND   {cls:9s} resref={rr:8s} "
                 f"loop={p['loop']} ff={p['ff']} this={p['this'][-6:]}")
        elif tag == "CPLAY":
            # orphan = CSound::Play whose `this` is not a projectile m_sound we saw
            if p["this"] not in recent_sounds:
                orphan_cplay["n"] += 1
                rr = p.get("resref") or "(empty)"
                # only surface the interesting (non-ambient) sounds with caller
                if rr not in ("AM1000K", "BLANK", "(empty)"):
                    emit(f"[{dt:8.1f}ms]   ~ CPLAY  resref={rr:8s} ret={p.get('ret','?')} "
                         f"this={p['this'][-6:]} pos=({p['x']},{p['y']})")

    print(f"[*] log file: {LOG_PATH}", flush=True)
    print(f"[*] spawning {ORIG_EXE}", flush=True)
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] running. Cast Magic Missile ONCE. Expect LAUNCH, 5x ARRIVE/OP233,", flush=True)
    print("[*] and the impact-sound PSND histogram. Ctrl+C -> summary.\n", flush=True)
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        emit("\n[*] stopping -- summary:")
        emit(f"    markers: LAUNCH={counts['LAUNCH']} ARRIVE={counts['ARRIVE']} "
             f"OP233={counts['OP233']}")
        emit(f"    projectile PlaySound calls: {counts['PSND']}")
        for rr, n in sorted(res_hist.items(), key=lambda kv: -kv[1]):
            emit(f"        {n}x  {rr}")
        emit(f"    CSound::Play total={counts['CPLAY']}  "
             f"non-projectile(orphan)={orphan_cplay['n']}")
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
