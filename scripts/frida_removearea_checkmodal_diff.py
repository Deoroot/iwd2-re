#!/usr/bin/env python3
"""Differential test against the ORIGINAL IWD2.exe: does CGameSprite::CheckModal
(0x72FD20) survive being called on a party member whose m_pArea has just been
nulled by CGameSprite::RemoveFromArea (0x6F4B90)?

Context: our build crashes inside CGameAIBase::GetVisualRange (called from
CheckModal's passive secret-door-search block, which unconditionally does
m_pArea->GetAllInRange(...)) when a party member's corpse finishes its twitch
decay and RemoveFromArea() nulls m_pArea. Static analysis of ProcessAI/
CheckModal found no m_pArea guard anywhere in that path in the original binary
either -- so this script tests it directly instead of guessing further.

Rather than waiting for a real combat death, this captures the live `this`
pointer of any party member the FIRST time it naturally enters CheckModal
(which happens every ~100 AI ticks for every party member regardless of alive/
dead state), then via RPC calls RemoveFromArea(that pointer) followed
immediately by CheckModal(that pointer) directly. If the original crashes the
same way, our recovery is faithful and the real bug is elsewhere (e.g. the
object should never be ticked again after removal). If it survives, CheckModal
itself (or something it calls) has a guard we haven't found yet.

Run:  python scripts/frida_removearea_checkmodal_diff.py
Then: from the main menu, Load Game -> pick any save with a party in an area.
Once a target is captured (printed), the script automatically fires the test
after a short delay. Ctrl+C to stop.
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
GAME_DIR = Path(r"C:\Juegos\Icewind Dale 2")
ORIG_EXE = GAME_DIR / "IWD2.exe"
LOG_PATH = SCRIPT_DIR.parent / "tmp_removearea_checkmodal_diff.log"

JS = r"""
'use strict';
const CheckModal = ptr(0x72FD20);
const RemoveFromArea = ptr(0x6F4B90);
const GetCharacterPortraitNum = ptr(0x5AF4E0);
const CHITIN = ptr(0x8CF6DC);  // g_pBaldurChitin, per CheckModal's own disasm at 0x72fd6e

function s32(p){ try { return p.readS32(); } catch(e){ return null; } }
function ptrStr(p){ try { return p.readPointer().toString(); } catch(e){ return 'ERR'; } }

let capturedThis = null;
let capturedId = null;
let hits = 0;

const GetCharacterPortraitNumFn = new NativeFunction(GetCharacterPortraitNum, 'int16', ['pointer', 'int32'], {abi: 'thiscall'});
const RemoveFromAreaFn = new NativeFunction(RemoveFromArea, 'void', ['pointer'], {abi: 'thiscall'});
const CheckModalFn = new NativeFunction(CheckModal, 'void', ['pointer'], {abi: 'thiscall'});

Interceptor.attach(CheckModal, {
  onEnter(args) {
    hits++;
    if (capturedThis !== null) return;
    const thiz = this.context.ecx;
    const id = s32(thiz.add(0x5c));
    if (id === null) return;
    const g = s32(CHITIN);
    if (!g) return;
    const pGame = s32(ptr(g).add(0x1c54));
    if (!pGame) return;
    let portrait;
    try { portrait = GetCharacterPortraitNumFn(ptr(pGame), id); } catch (e) { return; }
    if (portrait !== -1) {
      capturedThis = thiz;
      capturedId = id;
      send({tag: 'CAPTURED', id: id, pArea: ptrStr(thiz.add(0x12)), portrait: portrait});
    }
  }
});

rpc.exports = {
  status() {
    return { captured: capturedThis !== null, id: capturedId, hits: hits };
  },
  fireTest() {
    if (capturedThis === null) return 'no target captured yet';
    send({tag: 'BEFORE_REMOVE', id: capturedId, pArea: ptrStr(capturedThis.add(0x12))});
    RemoveFromAreaFn(capturedThis);
    send({tag: 'AFTER_REMOVE', id: capturedId, pArea: ptrStr(capturedThis.add(0x12))});
    // Force the once-per-100-ticks stagger check (field_44A % 100 == m_id % 100)
    // to pass deterministically -- without this, a synchronous RPC call could
    // land on a tick where the check fails and CheckModal returns instantly
    // without ever reaching the m_pArea-dependent block, giving a false pass.
    capturedThis.add(0x44a).writeU32(capturedId >>> 0);
    send({tag: 'FORCED_STAGGER_MATCH', id: capturedId, field44A: s32(capturedThis.add(0x44a))});
    send({tag: 'CALLING_CHECKMODAL', id: capturedId});
    CheckModalFn(capturedThis);
    send({tag: 'SURVIVED_CHECKMODAL', id: capturedId, modalCounterAfter: s32(capturedThis.add(0x723c))});
    return 'done';
  }
};
send({tag: 'ready'});
"""


def main() -> int:
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    log_f = open(LOG_PATH, "w", encoding="utf-8")
    t_start = [None]
    fired = [False]

    def rel(t):
        if t_start[0] is None:
            t_start[0] = t
        return t - t_start[0]

    def on_message(message, data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                print("[JS ERROR]", message.get("description"))
            return
        p = message["payload"]
        log_f.write(json.dumps(p) + "\n")
        log_f.flush()
        tag = p.get("tag")
        now = time.time() * 1000
        if tag == "CAPTURED":
            print(f"[+{rel(now):.0f}ms] [CAPTURED] id={p['id']} pArea={p['pArea']} portrait={p['portrait']}")
            print("[*] target captured -- firing test in 2s")
        elif tag == "BEFORE_REMOVE":
            print(f"[+{rel(now):.0f}ms] [BEFORE_REMOVE] id={p['id']} pArea={p['pArea']}")
        elif tag == "AFTER_REMOVE":
            print(f"[+{rel(now):.0f}ms] [AFTER_REMOVE] id={p['id']} pArea={p['pArea']}  <- should be 0x0")
        elif tag == "FORCED_STAGGER_MATCH":
            print(f"[+{rel(now):.0f}ms] [FORCED_STAGGER_MATCH] id={p['id']} field44A={p['field44A']}")
        elif tag == "CALLING_CHECKMODAL":
            print(f"[+{rel(now):.0f}ms] [CALLING_CHECKMODAL] id={p['id']}  <- if it crashes, this is the last line")
        elif tag == "SURVIVED_CHECKMODAL":
            print(f"[+{rel(now):.0f}ms] [SURVIVED_CHECKMODAL] id={p['id']} modalCounterAfter={p['modalCounterAfter']}  ***ORIGINAL SURVIVED***")

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] Original IWD2.exe spawned under Frida.")
    print("[*] From the main menu: Load Game -> pick any save with a party in an area.")
    print("[*] Waiting for a party member to naturally enter CheckModal...")
    print("[*] Ctrl+C to stop.\n")
    try:
        while True:
            time.sleep(1.0)
            st = script.exports_sync.status()
            if st["captured"] and not fired[0]:
                fired[0] = True
                time.sleep(2.0)
                try:
                    result = script.exports_sync.fire_test()
                    print(f"[*] fireTest() returned: {result}")
                except Exception as e:
                    print(f"[*] fireTest() call FAILED (process likely crashed): {e}")
                print("[*] Test complete -- check output above / log for crash vs survive.")
                time.sleep(1.0)
                break
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
