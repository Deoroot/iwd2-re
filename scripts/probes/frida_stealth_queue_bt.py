#!/usr/bin/env python3
"""Backtrace WHO queues the Hide() action (id 18) when the player clicks Hide.

The differential trace (frida_stealth_orig_trace.py) proved the ORIGINAL queues
a Hide() action (ACTION.IDS 18) ~68ms after the modal toggle, which reaches
CGameSprite::ExecuteAction -> sub_757B40 = the immediate hide.  Our build never
queues it.  This script finds the exact queuer.

Hooks (IWD2.exe absolute == Ghidra):
  0x4f4f90  CMessageAddAction::CMessageAddAction(const CAIAction& action,
            LONG caller, LONG target)  -- the action-queue message ctor.
            __thiscall: ecx=this; args[0]=&action; m_actionID at action+0 (U16).
            On a Hide (18) action we dump an ACCURATE backtrace -> module+offset,
            mapped to functions host-side with scripts/sym.py addr2fn.
  0x7338e0  CGameSprite::SetCurrAction(const CAIAction&)  -- the direct-set path,
            in case Hide is issued without a message.  Same filter + backtrace.

Re-attaches to the already-running IWD2.exe (the rogue save is still loaded), so
the only drive needed is one more click on Hide in Shadows.

Usage (host -> VM):  scripts/vm.sh frida scripts/frida_stealth_queue_bt.py --attach
Log (VM): C:\\iwd2-re\\tmp_frida_queue.log
"""
import frida
import sys
import os
import json
import time

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_queue.log")

JS = r"""
'use strict';

const ADDACTION_MSG = ptr(0x4f4f90);   // CMessageAddAction::CMessageAddAction
const SETCURR       = ptr(0x7338e0);   // CGameSprite::SetCurrAction
const O_ACTIONID = 0x0;                // CAIAction.m_actionID (SHORT) at +0

// IWD2.exe has no ASLR -> raw absolute addresses == Ghidra; map host-side with
// scripts/sym.py addr2fn.
function bt(ctx) {
  return Thread.backtrace(ctx, Backtracer.ACCURATE).map(a => '' + a);
}

// Both ctor and SetCurrAction are hot paths (every action, every sprite); gate
// on the Hide-family id BEFORE the expensive ACCURATE backtrace so the game does
// not crawl.  action arg is a CAIAction& -> m_actionID at +0.
const HIDE = 18, HIDECREATURE = 0xe9;

Interceptor.attach(ADDACTION_MSG, {
  onEnter(args) {
    try {
      const aid = args[0].add(O_ACTIONID).readU16();
      if (aid !== HIDE && aid !== HIDECREATURE) return;
      send({ tag: 'QUEUE', via: 'CMessageAddAction', actionID: aid,
             stack: bt(this.context) });
    } catch (e) { send({ tag: 'QUEUE_ERR', err: '' + e }); }
  }
});

Interceptor.attach(SETCURR, {
  onEnter(args) {
    try {
      const aid = args[0].add(O_ACTIONID).readU16();
      if (aid !== HIDE && aid !== HIDECREATURE) return;
      send({ tag: 'SETCURR', actionID: aid, stack: bt(this.context) });
    } catch (e) { send({ tag: 'SETCURR_ERR', err: '' + e }); }
  }
});

send({ tag: 'ready' });
"""


def main():
    # Spawn a fresh IWD2.exe by default (session-1 render so the user can drive);
    # pass --attach to hook an already-running instance instead.
    attach = "--attach" in sys.argv
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

    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
