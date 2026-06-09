#!/usr/bin/env python3
"""Probe: why does an object-target UI Spell cast (Magic Missile at a creature)
loop on the cast animation in the recovered build without ever firing?

Our ForceSpell stopgap gates the cast on facing:
    if (m_nDirection != GetDirection(target->m_pos)) { SetDirection(...); return; }
and the cast machine re-enters every tick.  If the caster's facing never settles
on GetDirection(target) -- or settles then drifts -- the gate re-fires mid-cast,
resets the cast-frame counter, and the cast animation replays forever.

Capture the ORIGINAL's ground truth for the same cast:
  * Does the caster orient THEN fire (counter climbs monotonically, no reset)?
  * Or does it re-orient mid-cast?
  * What are m_nDirection vs m_nNewDirection per tick, and when does it launch?

Hooks (function ENTRIES, absolute, ImageBase 0x400000):
  0x742840  FUN_00742840  cast sequence executor A (point / normal)
  0x746480  FUN_00746480  cast sequence executor B (object target?)
  0x530C90  SPMAGMIS::Fire  the Magic Missile launch -- marks "cast fired"

Per-executor tick we log the casting sprite's:
  id   +0x5C
  dir  m_nDirection     +0x5380   (current facing)
  new  m_nNewDirection  +0x537E   (gradual-turn target)
  ctr  cast-frame counter +0x474  (SHORT; resets => cast restarted)
  pt   cast point       +0x540/+0x544

Run:  python scripts/frida_cast_orient_loop_probe.py
Then: load the combat save, cast Magic Missile ONCE at an enemy, watch the log.
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

ID_OFF = 0x5C
NEWDIR_OFF = 0x537E
DIR_OFF = 0x5380
CTR_OFF = 0x474
PTX_OFF = 0x540
PTY_OFF = 0x544

EXEC_A = 0x742840
EXEC_B = 0x746480
LAUNCH = 0x530C90

JS = r"""
'use strict';
const ID_OFF = %ID_OFF%;
const NEWDIR_OFF = %NEWDIR_OFF%;
const DIR_OFF = %DIR_OFF%;
const CTR_OFF = %CTR_OFF%;
const PTX_OFF = %PTX_OFF%;
const PTY_OFF = %PTY_OFF%;

function rd32(p, off) { try { return p.add(off).readS32(); } catch (e) { return -999; } }
function rd16(p, off) { try { return p.add(off).readS16(); } catch (e) { return -999; } }

function execHook(tag) {
  return {
    onEnter: function () {
      const t = this.context.ecx;
      send({
        tag: tag,
        id: rd32(t, ID_OFF),
        dir: rd16(t, DIR_OFF),
        nd: rd16(t, NEWDIR_OFF),
        ctr: rd16(t, CTR_OFF),
        px: rd32(t, PTX_OFF),
        py: rd32(t, PTY_OFF)
      });
    }
  };
}

Interceptor.attach(ptr(%EXEC_A%), execHook('EXEC_A'));
Interceptor.attach(ptr(%EXEC_B%), execHook('EXEC_B'));
Interceptor.attach(ptr(%LAUNCH%), {
  onEnter: function () { send({ tag: 'LAUNCH', ret: this.returnAddress.toString() }); }
});

send({ tag: 'ready' });
"""


def main() -> int:
    js = (
        JS.replace("%ID_OFF%", hex(ID_OFF))
        .replace("%NEWDIR_OFF%", hex(NEWDIR_OFF))
        .replace("%DIR_OFF%", hex(DIR_OFF))
        .replace("%CTR_OFF%", hex(CTR_OFF))
        .replace("%PTX_OFF%", hex(PTX_OFF))
        .replace("%PTY_OFF%", hex(PTY_OFF))
        .replace("%EXEC_A%", hex(EXEC_A))
        .replace("%EXEC_B%", hex(EXEC_B))
        .replace("%LAUNCH%", hex(LAUNCH))
    )

    log_path = SCRIPT_DIR.parent / "tmp_cast_loop_trace.log"
    logf = open(log_path, "w", encoding="utf-8")

    def emit(s):
        print(s, flush=True)
        logf.write(s + "\n")
        logf.flush()

    emit(f"[*] log file: {log_path}")
    emit(f"[*] spawning {ORIG_EXE}")
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)

    last = {"id": None, "dir": None, "nd": None, "ctr": None}

    def on_message(message, _data):
        if message["type"] != "send":
            emit(f"[frida-error] {message}")
            return
        p = message["payload"]
        tag = p.get("tag")
        if tag == "ready":
            emit("[*] hooks installed -- load the combat save, cast Magic Missile "
                 "ONCE at an enemy.\n")
        elif tag == "LAUNCH":
            emit(">>> LAUNCH  (SPMAGMIS::Fire -- the cast FIRED)")
        elif tag in ("EXEC_A", "EXEC_B"):
            # collapse identical repeats to keep the convergence readable
            key = (p["id"], p["dir"], p["nd"], p["ctr"])
            if key != (last["id"], last["dir"], last["nd"], last["ctr"]):
                emit(f"    {tag} id={p['id']} dir={p['dir']} newDir={p['nd']} "
                     f"castCtr={p['ctr']} pt=({p['px']},{p['py']})")
                last["id"], last["dir"], last["nd"], last["ctr"] = key

    script = session.create_script(js)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    emit("[*] running. Cast Magic Missile ONCE. Look for dir -> newDir convergence,")
    emit("[*] whether castCtr climbs monotonically (no reset), and the LAUNCH line.")
    emit("[*] Ctrl+C to stop.\n")
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        emit("\n[*] stopping")
    finally:
        try:
            session.detach()
        except Exception:
            pass
        try:
            frida.kill(pid)
        except Exception:
            pass
        try:
            logf.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
