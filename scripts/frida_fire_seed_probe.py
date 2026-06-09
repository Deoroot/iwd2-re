#!/usr/bin/env python3
"""Ground-truth the launch tail of CProjectileTravelling::Fire (0x52C050) on the
ORIGINAL IWD2.exe.

Fire's trajectory core is already recovered; this probe pins the launch tail we
still stub -- specifically the subpixel-position SEED. The binary seeds
m_posAccumX (+0x9C) / m_posAccumY (+0xA0) from stack locals whose provenance is
hard to read statically. Reading them right after Fire returns reveals the
relationship to m_pos / the target, so we can transcribe the seed faithfully to
observed behaviour instead of guessing.

We hook Fire's ENTRY (save `this` = ecx) and read the seeded fields on LEAVE:
  +0x06 m_pos.x   +0x0A m_pos.y   +0x0E m_posZ
  +0x9C m_posAccumX   +0xA0 m_posAccumY    <== the seed
  +0xC8 m_targetX     +0xCC m_targetY
  +0x1DA m_direction  +0x1DC m_facing       (initial facing from GetDirection)
  +0xE6 m_renderFlags (expect 0x20008 -> confirms where the 0x8 is OR'd in)
  +0x70 m_velocity    +0x29E m_lifetime     +0x182 m_nTargetId (attached object)

Derivation to check (shift = 10, so <<10 == *1024):
  m_posAccumX >> 10 == m_pos.x ?        -> seed X = m_pos.x << 10
  (m_posAccumY * 3 / 4) >> 10 == m_pos.y ? -> seed Y = (m_pos.y << 10) * 4/3

Run:  python scripts/frida_fire_seed_probe.py
Then: load a combat save, cast Magic Missile at an enemy ONCE.
Stop: Ctrl+C   (full JSON also in tmp_fire_seed.log)
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

LOG_PATH = SCRIPT_DIR.parent / "tmp_fire_seed.log"

JS = r"""
const Fire = ptr(0x52c050);

function s32(p){ try { return p.readS32(); } catch(e){ return null; } }
function s16(p){ try { return (p.readU16() << 16) >> 16; } catch(e){ return null; } }
function u32(p){ try { return p.readU32() >>> 0; } catch(e){ return null; } }

Interceptor.attach(Fire, {
  onEnter(a){ this.t = this.context.ecx;
    this.a = { src:a[1].toInt32(), tgt:a[2].toInt32(),
               tpx:a[3].toInt32(), tpy:a[4].toInt32() }; },
  onLeave(r){
    const t = this.t;
    send({ tag:'FIRE_OUT', vt:(u32(t)||0).toString(16),
      args:this.a,
      px:s32(t.add(0x06)), py:s32(t.add(0x0a)), pz:s32(t.add(0x0e)),
      accX:s32(t.add(0x9c)), accY:s32(t.add(0xa0)),
      tx:s32(t.add(0xc8)), ty:s32(t.add(0xcc)),
      dir:s16(t.add(0x1da)), facing:s16(t.add(0x1dc)),
      rflags:(u32(t.add(0xe6))||0).toString(16),
      vel:s16(t.add(0x70)), life:s16(t.add(0x29e)),
      ntid:s32(t.add(0x182)) });
  }
});
"""


def main() -> int:
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    log_f = open(LOG_PATH, "w", encoding="utf-8")

    SHIFT = 10

    def on_message(message, data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                print("[JS ERROR]", message.get("description"))
            return
        p = message["payload"]
        log_f.write(json.dumps(p) + "\n")
        log_f.flush()
        if p.get("tag") != "FIRE_OUT":
            return
        print(f"\n[FIRE_OUT] vt={p['vt']} args={p['args']}")
        print(f"  m_pos=({p['px']},{p['py']},{p['pz']})  target=({p['tx']},{p['ty']})")
        print(f"  m_posAccum=({p['accX']},{p['accY']})  vel={p['vel']} life={p['life']} "
              f"ntid={p['ntid']}")
        print(f"  dir={p['dir']} facing={p['facing']} rflags=0x{p['rflags']}")
        # derive the seed relationship
        if p['accX'] is not None and p['px'] is not None:
            ax = p['accX'] >> SHIFT
            ay = ((p['accY'] * 3) // 4) >> SHIFT if p['accY'] is not None else None
            print(f"  >> accX>>10={ax} (m_pos.x={p['px']}, match={ax==p['px']})  "
                  f"(accY*3/4)>>10={ay} (m_pos.y={p['py']}, match={ay==p['py']})")

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] IWD2.exe spawned under Frida.")
    print("[*] 1) Load a combat save.  2) Cast Magic Missile at an enemy ONCE.")
    print("[*] Watch [FIRE_OUT]: the seed match lines tell us seed = m_pos<<10.")
    print("[*] Full JSON -> tmp_fire_seed.log.  Ctrl+C to stop.\n")
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
