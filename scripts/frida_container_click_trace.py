#!/usr/bin/env python3
"""Differential trace of the ORIGINAL IWD2.exe container left-click pipeline.

Bug 1: left-clicking a ground loot pile moves the whole party (instead of just
the leader) and never opens the loot window. Root cause found statically:
CGameContainer overrides OnActionButton (vtable slot 0x40 -> 0x47e970) but our
RE build is MISSING that override, so container clicks fall through to the base
CGameObject::OnActionButton (0x4c7ca0) which just issues a formation group-move.

This script hooks the ORIGINAL to capture the exact action sequence the missing
override queues, so the recovery can match it byte-for-byte (the only part that
is hard to read off the aliased decompiler stack is the per-call GroupAction
wiring: which CAIAction is primary vs leaderAction, and the override flag).

Hooks (IWD2.exe absolute == Ghidra, no ASLR, ImageBase 0x400000), __thiscall:
  0x47e970  CGameContainer::OnActionButton(const CPoint& pt)   this=ecx=container
  0x4c7ca0  CGameObject::OnActionButton(const CPoint& pt)      base (group-move)
  0x404d00  CAIGroup::GroupAction(CAIAction action /*byval 0xd8*/, BOOL override,
                                  CAIAction* leaderAction)      this=ecx=group
  0x5bc450  CInfGame::UseMagicOnObject(LONG obj)                this=ecx=game

CAIAction layout (sizeof 0xd8): m_actionID @ +0x00 (SHORT). On the GroupAction
stack: args[0] = &byval action, args[0]+0xd8 = override (BOOL),
args[0]+0xdc = leaderAction (CAIAction*).

Container fields: m_id @ +0x5c, item count @ +0x5ba, m_containerType @ +0x5ca,
m_ptWalkToUse @ +0x5cc/+0x5d0.

GroupAction is called every frame by area AI, so it is only logged while a
container/base OnActionButton is on the stack (inClick gate) to kill the noise.

Usage:
  python scripts/frida_container_click_trace.py            # spawn IWD2.exe
  python scripts/frida_container_click_trace.py --attach   # attach to running

Then in-game: load the save, select the party, left-click the ground loot pile.
Log: tmp_frida_container.log (repo root). Ctrl-C / kill to stop.
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_container.log")

JS = r"""
'use strict';

const ContainerOAB = ptr(0x47E970);   // CGameContainer::OnActionButton
const BaseOAB      = ptr(0x4C7CA0);   // CGameObject::OnActionButton
const GroupAction  = ptr(0x404D00);   // CAIGroup::GroupAction
const UseMagicObj  = ptr(0x5BC450);   // CInfGame::UseMagicOnObject

const CAIACTION_SIZE = 0xd8;

function s16(p) { try { return (p.readU16() << 16) >> 16; } catch (e) { return -99999; } }
function u32(p) { try { return p.readU32(); } catch (e) { return -1; } }

// Only log the per-frame GroupAction polling while a click handler is running.
let inClick = 0;

// CGameContainer::OnActionButton(const CPoint& pt)
Interceptor.attach(ContainerOAB, {
  onEnter(args) {
    const c = this.context.ecx;
    inClick++;
    let id = -1, type = -1, cnt = -1, wx = 0, wy = 0;
    try {
      id   = c.add(0x5c).readS32();
      type = s16(c.add(0x5ca));
      cnt  = c.add(0x5ba).readU32();
      wx   = c.add(0x5cc).readS32();
      wy   = c.add(0x5d0).readS32();
    } catch (e) {}
    send({ tag: 'Container.OnActionButton.in', container: id, type: type,
           items: cnt, walkTo: [wx, wy] });
  },
  onLeave() { if (inClick > 0) inClick--; send({ tag: 'Container.OnActionButton.out' }); }
});

// CGameObject::OnActionButton (base) -- what the RE build wrongly hits for piles.
Interceptor.attach(BaseOAB, {
  onEnter(args) {
    inClick++;
    send({ tag: 'Base.OnActionButton.in', this: this.context.ecx.toString() });
  },
  onLeave() { if (inClick > 0) inClick--; send({ tag: 'Base.OnActionButton.out' }); }
});

// CAIGroup::GroupAction(CAIAction action /*byval*/, BOOL override, CAIAction* leader)
// The CAIAction is copied INLINE onto the stack (non-trivial copy-ctor), so the
// struct starts at [esp+4]; m_actionID is the low word of that first dword.
Interceptor.attach(GroupAction, {
  onEnter(args) {
    if (!inClick) return;
    const sp = this.context.esp;
    const actee = sp.add(4);                       // &byval CAIAction (= &m_actionID)
    const actionId = s16(actee);
    const override = u32(sp.add(4 + CAIACTION_SIZE));
    let leaderId = null;
    try {
      const leader = sp.add(4 + CAIACTION_SIZE + 4).readPointer();
      if (!leader.isNull()) leaderId = s16(leader);
    } catch (e) {}
    send({ tag: 'GroupAction', actionId: actionId, override: override,
           leaderActionId: leaderId });
  }
});

// CInfGame::UseMagicOnObject(LONG obj)
Interceptor.attach(UseMagicObj, {
  onEnter(args) {
    if (!inClick) return;
    send({ tag: 'UseMagicOnObject', obj: args[0].toInt32() });
  }
});

send({ tag: 'ready' });
"""


def main():
    attach = "--attach" in sys.argv
    open(LOG, "w").close()

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        print(line, flush=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")

    if attach:
        session = frida.attach("IWD2.exe")
        pid = None
        print("[*] attached to running IWD2.exe", flush=True)
    else:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned IWD2.exe pid={pid}", flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print("[*] hooks live. In-game: load the save, select the party, and LEFT-CLICK",
          flush=True)
    print("[*] the ground loot pile. Watch the GroupAction sequence.", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
