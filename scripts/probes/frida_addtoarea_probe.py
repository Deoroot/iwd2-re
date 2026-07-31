#!/usr/bin/env python3
"""Ground-truth the arguments CProjectileTravelling::Fire passes to
CGameObject::AddToArea (0x4C7BE0) on the ORIGINAL IWD2.exe.

Fire's launch tail calls AddToArea(pNewArea, pos, posZ, listType) to insert the
projectile into the area and set m_pos. The argument provenance (pArea, posZ) is
hard to read statically because Fire's esp-relative stack frame shifts under the
many push/pops. So we read the arguments live.

Strategy: hook Fire's ENTRY to record `this` (ecx), then log the AddToArea call
whose `this` matches -- that is the projectile inserting itself.

AddToArea is __thiscall: this = ecx, args on the stack:
  arg0 = pNewArea (CGameArea*)
  arg1 = pos      (const CPoint*)  -> read x (+0), y (+4)
  arg2 = posZ     (LONG)
  arg3 = listType (BYTE)

Run:  python scripts/frida_addtoarea_probe.py
Then: load a combat save, cast Magic Missile at an enemy ONCE.
Stop: Ctrl+C   (full JSON also in tmp_addtoarea.log)
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

LOG_PATH = SCRIPT_DIR.parent / "tmp_addtoarea.log"

JS = r"""
const Fire      = ptr(0x52c050);
const AddToArea = ptr(0x4c7be0);

function s32(p){ try { return p.readS32(); } catch(e){ return null; } }
function u32(p){ try { return p.readU32() >>> 0; } catch(e){ return null; } }
function u8(p){ try { return p.readU8(); } catch(e){ return null; } }

let fireThis = null;

Interceptor.attach(Fire, {
  onEnter(a){ fireThis = this.context.ecx.toString();
    send({tag:'FIRE_IN', this:fireThis, pArea:a[0].toInt32()}); }
});

Interceptor.attach(AddToArea, {
  onEnter(a){
    const t = this.context.ecx;
    // arg0..arg3 are at esp+4..esp+0x10 on entry (ret addr at esp+0)
    const sp = this.context.esp;
    const pNewArea = u32(sp.add(4));
    const pPos     = u32(sp.add(8));
    const posZ     = s32(sp.add(0xc));
    const listType = u8(sp.add(0x10));
    let px = null, py = null;
    try { px = s32(ptr(pPos)); py = s32(ptr(pPos).add(4)); } catch(e){}
    send({tag:'ADDTOAREA', this:t.toString(), vt:(u32(t)||0).toString(16),
      isProjectile:(t.toString()===fireThis),
      pNewArea:(pNewArea>>>0).toString(16), posX:px, posY:py, posZ:posZ,
      listType:listType,
      mpx:s32(t.add(6)), mpy:s32(t.add(0xa)), mpz:s32(t.add(0xe)) });
  }
});
"""


def main() -> int:
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    log_f = open(LOG_PATH, "w", encoding="utf-8")

    def on_message(message, data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                print("[JS ERROR]", message.get("description"))
            return
        p = message["payload"]
        log_f.write(json.dumps(p) + "\n")
        log_f.flush()
        if p.get("tag") == "FIRE_IN":
            print(f"\n[FIRE_IN] this={p['this']} pArea=0x{p['pArea']&0xffffffff:x}")
        elif p.get("tag") == "ADDTOAREA":
            mark = "  <== PROJECTILE (this==Fire this)" if p.get("isProjectile") else ""
            if p.get("isProjectile"):
                print(f"[ADDTOAREA]{mark}\n  this={p['this']} vt=0x{p['vt']}\n"
                      f"  pNewArea=0x{p['pNewArea']} pos=({p['posX']},{p['posY']}) "
                      f"posZ={p['posZ']} listType={p['listType']}\n"
                      f"  m_pos before=({p['mpx']},{p['mpy']},{p['mpz']})")

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] IWD2.exe spawned under Frida.")
    print("[*] 1) Load a combat save.  2) Cast Magic Missile at an enemy ONCE.")
    print("[*] Watch [ADDTOAREA] with '<== PROJECTILE': those are Fire's args.")
    print("[*] Full JSON -> tmp_addtoarea.log.  Ctrl+C to stop.\n")
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
