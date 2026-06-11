#!/usr/bin/env python3
"""Full Magic Missile arc trace on the ORIGINAL IWD2.exe.

Confirms, end to end, what the stock engine does for one object-targeted Magic
Missile cast, so the recovered build can be diffed against it stage by stage:

  cast machine -> orient -> launch -> 5 fanning missiles -> homing flight ->
  per-missile arrival -> per-missile damage + impact VFX

Every hook is a function ENTRY (mid-function hooks crash). Absolute addresses,
ImageBase 0x400000, no ASLR.

  0x740270  FUN_00740270  cast SEQUENCE executor        -- orient gate + counter
  0x51EAF0  DecodeProjectile                            -- missile type
  0x530C90  SPMAGMIS::Fire                              -- the launch (fans 5)
  0x52CA10  CProjectileMagicMissile ctor                -- one per missile
  0x52C050  CProjectileTravelling::Fire                 -- per-missile target/vel
  0x52B900  CProjectileTravelling::AIUpdate             -- per-tick homing (sampled)
  0x529FB0  CProjectile::OnArrival      (vslot 0x70)    -- per-missile arrival
  0x52A1A0  CProjectile::DeliverEffects (vslot 0x78)    -- per-missile damage
  0x57E710  CProjectileSummonVFX::Fire                  -- per-missile impact overlay

Caster fields read at the CAST hook (this = ecx):
  ctr  m_actionCount      +0x474 s16   (cast-frame counter; reset => restart)
  id   m_curAction.id     +0x476 u16
  spec m_specificID       +0x52C s32   (caster level feed)
  nd   m_nNewDirection    +0x537E s16  (gradual-turn target)
  dir  m_nDirection       +0x5380 s16  (current facing)
  lvl  m_nLevel           +0x966 u8
  dst  m_curAction.m_dest +0x540/+0x544 s32  (cast point snapshot)

Travelling-projectile fields (TFIRE / AIU, this = ecx):
  px/py/pz +0x06/+0x0A/+0x0E s32   tx/ty +0xC8/+0xCC s32 (target)
  vel +0x70 s16   life +0x29E s16   tid +0x76 s32   id +0x5C s32   vt = *this

Run:  python -u scripts/frida_mm_arc_trace.py
Then: load the combat save (slot 2), cast Magic Missile ONCE at an enemy.
Stop: Ctrl+C.  Full JSON also written to tmp_mm_arc_trace.log
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

LOG_PATH = SCRIPT_DIR.parent / "tmp_mm_arc_trace.log"

JS = r"""
'use strict';
const CAST    = ptr(0x740270);
const DECODE  = ptr(0x51eaf0);
const SPFIRE  = ptr(0x530c90);
const CTOR    = ptr(0x52ca10);
const TFIRE   = ptr(0x52c050);
const AIU     = ptr(0x52b900);
const ARRIVE  = ptr(0x529fb0);
const DELIVER = ptr(0x52a1a0);
const VFX     = ptr(0x57e710);

function s32(p, o){ try { return p.add(o).readS32(); } catch(e){ return null; } }
function s16(p, o){ try { return (p.add(o).readU16() << 16) >> 16; } catch(e){ return null; } }
function u16(p, o){ try { return p.add(o).readU16(); } catch(e){ return null; } }
function u8(p, o){ try { return p.add(o).readU8(); } catch(e){ return null; } }
function vt(p){ try { return (p.readU32() >>> 0).toString(16); } catch(e){ return '?'; } }

// ---- cast machine: orient + counter, every tick ----------------------------
Interceptor.attach(CAST, { onEnter() { const t = this.context.ecx;
  send({ tag:'CAST', id:u16(t,0x476), ctr:s16(t,0x474), spec:s32(t,0x52c),
         dir:s16(t,0x5380), nd:s16(t,0x537e), lvl:u8(t,0x966),
         dx:s32(t,0x540), dy:s32(t,0x544) });
}});

Interceptor.attach(DECODE, { onEnter(a) {
  send({ tag:'DECODE', type:(a[0].toInt32() & 0xffff) });
}});

Interceptor.attach(SPFIRE, { onEnter() {
  send({ tag:'SPFIRE', ret:this.returnAddress.toString() });
}});

Interceptor.attach(CTOR, { onEnter() { const t = this.context.ecx;
  send({ tag:'CTOR', this:t.toString() });
}});

Interceptor.attach(TFIRE, { onEnter() { const t = this.context.ecx;
  send({ tag:'TFIRE', this:t.toString(), vt:vt(t),
         px:s32(t,0x06), py:s32(t,0x0a), pz:s32(t,0x0e),
         tx:s32(t,0xc8), ty:s32(t,0xcc), vel:s16(t,0x70),
         life:s16(t,0x29e), tid:s32(t,0x76), id:s32(t,0x5c) });
}});

Interceptor.attach(AIU, { onEnter() { const t = this.context.ecx;
  send({ tag:'AIU', this:t.toString(),
         px:s32(t,0x06), py:s32(t,0x0a), pz:s32(t,0x0e),
         tx:s32(t,0xc8), ty:s32(t,0xcc), vel:s16(t,0x70), life:s16(t,0x29e) });
}});

Interceptor.attach(ARRIVE,  { onEnter() { send({ tag:'ARRIVE',  this:this.context.ecx.toString(),
         tid:s32(this.context.ecx,0x76) }); }});
Interceptor.attach(DELIVER, { onEnter() { send({ tag:'DELIVER', this:this.context.ecx.toString(),
         tid:s32(this.context.ecx,0x76) }); }});
Interceptor.attach(VFX,     { onEnter() { send({ tag:'VFX',     this:this.context.ecx.toString() }); }});

send({ tag:'ready' });
"""


def main() -> int:
    log_f = open(LOG_PATH, "w", encoding="utf-8")

    def emit(s):
        print(s, flush=True)
        log_f.write(s + "\n")
        log_f.flush()

    # raw json + per-stage state
    last_cast = {"key": None}
    ctor_n = {"n": 0}
    aiu_state: dict[str, dict] = {}      # per missile: first/last/count
    arrive_n = {"n": 0}
    deliver_n = {"n": 0}
    vfx_n = {"n": 0}

    def on_message(message, _data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                emit(f"[JS ERROR] {message.get('description')}")
            return
        p = message["payload"]
        log_f.write(json.dumps(p) + "\n")
        log_f.flush()
        tag = p.get("tag")

        if tag == "ready":
            emit("[*] hooks installed -- load slot 2, cast Magic Missile ONCE.\n")
        elif tag == "CAST":
            key = (p["id"], p["ctr"], p["dir"], p["nd"])
            if key != last_cast["key"]:
                last_cast["key"] = key
                emit(f"  CAST  id=0x{p['id']:x} ctr={p['ctr']} dir={p['dir']} "
                     f"newDir={p['nd']} lvl={p['lvl']} spec={p['spec']} "
                     f"dest=({p['dx']},{p['dy']})")
        elif tag == "DECODE":
            emit(f"  DECODE  missileType=0x{p['type']:x}")
        elif tag == "SPFIRE":
            emit(f">>> SPFIRE  SPMAGMIS::Fire (launch)  ret={p['ret']}")
        elif tag == "CTOR":
            ctor_n["n"] += 1
            emit(f"  CTOR  #{ctor_n['n']}  missile this={p['this']}")
        elif tag == "TFIRE":
            emit(f"  TFIRE this={p['this'][-6:]} vt={p['vt']} pos=({p['px']},{p['py']}) "
                 f"target=({p['tx']},{p['ty']}) vel={p['vel']} life={p['life']} "
                 f"tid={p['tid']} id={p['id']}")
        elif tag == "AIU":
            t = p["this"]
            st = aiu_state.get(t)
            if st is None:
                st = {"n": 0, "first": p}
                aiu_state[t] = st
                emit(f"  AIU[{t[-6:]}] start pos=({p['px']},{p['py']}) "
                     f"target=({p['tx']},{p['ty']}) vel={p['vel']} life={p['life']}")
            st["n"] += 1
            st["last"] = p
            # show the homing approach near arrival
            if p["life"] is not None and p["life"] <= 3:
                emit(f"  AIU[{t[-6:]}] tick#{st['n']} pos=({p['px']},{p['py']}) "
                     f"life={p['life']}")
        elif tag == "ARRIVE":
            arrive_n["n"] += 1
            emit(f"<<< ARRIVE #{arrive_n['n']} this={p['this'][-6:]} tid={p['tid']}")
        elif tag == "DELIVER":
            deliver_n["n"] += 1
            emit(f"    DELIVER #{deliver_n['n']} (damage) this={p['this'][-6:]} tid={p['tid']}")
        elif tag == "VFX":
            vfx_n["n"] += 1
            emit(f"    VFX #{vfx_n['n']} (impact overlay) this={p['this'][-6:]}")

    print(f"[*] log file: {LOG_PATH}", flush=True)
    print(f"[*] spawning {ORIG_EXE}", flush=True)
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] running. Cast Magic Missile ONCE. Expect: CAST orient (dir->newDir),", flush=True)
    print("[*] ctr climb, SPFIRE, 5x CTOR/TFIRE, AIU homing, 5x ARRIVE/DELIVER/VFX.", flush=True)
    print("[*] Ctrl+C to stop -> prints per-missile AIU summary.\n", flush=True)
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        emit("\n[*] stopping -- AIU summary per missile:")
        for t, st in aiu_state.items():
            f, l = st["first"], st.get("last", st["first"])
            emit(f"    {t[-6:]}: {st['n']} ticks  "
                 f"({f['px']},{f['py']})->({l['px']},{l['py']})  "
                 f"life {f['life']}->{l['life']}")
        emit(f"    totals: CTOR={ctor_n['n']} ARRIVE={arrive_n['n']} "
             f"DELIVER={deliver_n['n']} VFX={vfx_n['n']}")
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
