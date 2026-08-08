#!/usr/bin/env python3
"""Differential trace of the ORIGINAL IWD2.exe detect-traps (Search) modal sweep.

IWD2.exe has no ASLR (ImageBase 0x400000), so Ghidra addresses are absolute.
The per-round modal upkeep CGameSprite::sub_72FD20 (0x72fd20) dispatches, for a
sprite in modal state 2 (detect traps), a sweep over every door / trigger /
container in half its visual range and reveals each armed-but-undetected trap
whose detection difficulty is beaten.  The DECISION for a TRIGGER trap is, at
0x7309a8 .. 0x7309c3:

    searchSkill = (char) m_derivedStats.m_nSkills[SEARCH]   // byte [esi+0xa70]
    difficulty  = trigger->m_trapDetectionDifficulty        // word [ecx+0x60e]
    if (searchSkill*7 >= difficulty && difficulty != 100 && (flags & 8)) detect;

Our recovered build reproduces that gate exactly, yet on save slot 4 a level-20
rogue1/barbarian19 with Search=4 (=> 4*7=28) fails the two nearby triggers
(difficulty 40 and 50) -- while on the ORIGINAL the same character finds them.
So at RUNTIME the original must see a different searchSkill and/or a different
difficulty than our build does.  This trace captures, on the original, the exact
runtime values the gate reads, so we can diff them against our build's
[FT] DebugLog (searchSkill / TRIGGER diff=...).

Hooks (IWD2.exe absolute == Ghidra):
  0x72fd20  sub_72FD20 entry  -- emit SWEEP for a modal-state-2 sprite on its
            id-staggered work tick (field_44A%100==m_id%100): the searcher's id
            and the searchSkill byte [this+0xa70] the gate will use.
  0x7309a8  trigger-detect gate (trapAct!=0, undetected, flags&0x100==0 already
            passed): esi=sprite, ecx=trigger.  Emit TRIGGER with the runtime
            searchSkill, difficulty (+0x60e), trapAct (+0x612), trapDet (+0x614),
            flags (+0x5d6) and both ids -- the values the *7 gate compares.
  0x730b29  trigger detected (mov word[ebp],1): emit DETECTED (esi=sprite).
  0x71fc00  SetModalState -- see the search toggle (newState 2 / oldState 2).

Usage (host -> VM):
  scripts/vm.sh frida scripts/frida_findtraps_orig_trace.py            # spawn IWD2.exe
  scripts/vm.sh frida scripts/frida_findtraps_orig_trace.py --attach   # attach running

Then in-game (session 1): load save slot 4, select the rogue, toggle Find Traps,
walk near the trap, watch.
Log (host repo root): tmp_frida_findtraps.log
"""
import frida
import sys
import os
import json
import time

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_findtraps.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
// ENTRY-only hooks (function prologues) -- mid-function inline hooks on the
// trigger gate crashed the original, so we read everything off `this` at entry.
const SUB_72FD20 = ptr(0x72fd20);   // per-tick modal upkeep (id-staggered)
const SETMODAL   = ptr(0x71fc00);   // CGameSprite::SetModalState

// CGameSprite fields off `this` (ecx):
const O_ID = 0x5c, O_STATE = 0x4c53, O_F44A = 0x44a;
// SEARCH = skill index 12.  Three stat blocks:
const O_DER_SEARCH  = 0xa70;   // m_derivedStats.m_nSkills[12]  (0x920+0x144+12) -- the gate input
const O_BASE_SEARCH = 0x7c0;   // m_baseStats.m_skills[12]      (0x5a4+0x210+12) -- raw ranks
const O_BON_SEARCH  = 0x2720;  // m_bonusStats.m_nSkills[12]    (0x25d0+0x144+12) -- effect/item bonus
const O_DER_INT     = 0x976;   // m_derivedStats.m_nINT         (0x920+0x56)

const t0 = Date.now();
function dt() { return Date.now() - t0; }   // ms since hooks live

function s8(p) { const v = p.readU8(); return v >= 128 ? v - 256 : v; }

function dumpSkill(t) {
  return {
    base:    s8(t.add(O_BASE_SEARCH)),   // raw ranks
    bonus:   s8(t.add(O_BON_SEARCH)),    // item/effect bonus block
    derived: s8(t.add(O_DER_SEARCH)),    // final -- the gate reads THIS
    derINT:  t.add(O_DER_INT).readS16(),
  };
}

// Modal upkeep entry: only for a detect-traps (state 2) sprite, only on its
// id-staggered work tick -- one emit == one real sweep, with the skill breakdown.
Interceptor.attach(SUB_72FD20, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      if (t.add(O_STATE).readU8() !== 2) return;
      const f = t.add(O_F44A).readU32();
      const id = t.add(O_ID).readS32();
      if ((f % 100) !== (((id % 100) + 100) % 100)) return;
      const sk = dumpSkill(t);
      send({ tag: 'SWEEP', ts: dt(), id, skill7: sk.derived * 7, search: sk });
    } catch (e) { send({ tag: 'SWEEP_ERR', err: '' + e }); }
  }
});

// Search toggle (newState 2 on, oldState 2 off): same breakdown at toggle time.
Interceptor.attach(SETMODAL, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'SETMODAL', ts: dt(),
        id:       t.add(O_ID).readS32(),
        oldState: t.add(O_STATE).readU8(),
        newState: args[0].toInt32() & 0xff,
        search:   dumpSkill(t) });
    } catch (e) { send({ tag: 'SETMODAL_ERR', err: '' + e }); }
  }
});
"""


def main():
    attach = "--attach" in sys.argv
    # Dedicated, line-buffered log; flush + fsync each line so the fire-and-forget
    # VBS payload never loses writes when its parent exits.
    logf = open(LOG, "w", buffering=1)
    fd = logf.fileno()

    def emit(line):
        logf.write(line + "\n")
        logf.flush()
        os.fsync(fd)

    def on_message(message, data):
        if message["type"] == "send":
            emit(json.dumps(message["payload"]))
        else:
            emit("ERROR " + json.dumps(message))

    if attach:
        session = frida.attach("IWD2.exe")
        pid = None
        emit("ATTACHED IWD2.exe")
    else:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        emit("SPAWNED pid=%s" % pid)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)
    emit("HOOKS_LIVE")

    # No stdin under the payload: block forever so the hooks persist.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
