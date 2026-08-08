#!/usr/bin/env python3
"""Differential trace of the ORIGINAL IWD2.exe Hide-in-Shadows pipeline via Frida.

IWD2.exe has no ASLR (ImageBase 0x400000), so Ghidra addresses are absolute.
We hook the stealth detection cycle + the modal-state toggle + the feedback
emitter, so we can see -- on the original -- exactly when stealth detection runs,
how the modal counter advances, and which feedback messages fire (sound /
invisibility cues).  Run our own iwd2-re.exe build with its STEALTH DebugLog
under the SAME action and diff the two.

Hooks (all addresses are IWD2.exe absolute, == Ghidra):
  0x757b40  CGameSprite::sub_757B40()   -- the per-3-cycle stealth detection pass.
                                            Fires only when (m_modalCounter % 3 == 0),
                                            i.e. each real detection cycle.
  0x71fc00  CGameSprite::SetModalState(BYTE modalState, BOOL bUpdateToolbar)
  0x7349a0  CGameSprite::FeedBack(WORD nFeedBackId, ...) -- filtered to stealth ids.
  0x72fd20  CGameSprite::sub_72FD20() -- modal upkeep; emits MODALTICK only on the
            id-staggered work tick (field_44A%100==m_id%100) for a stealthing sprite,
            so each line == one m_modalCounter advance (cadence / time-to-first-hide).
  0x728f80  CGameSprite::ExecuteAction() -- emits ACTION only for actionID 18 (Hide)
            or 0xE9 (HideCreature): proves whether the click ever queues an action.

Every payload carries ts (ms since hooks live) so the original's time-to-hide can
be diffed against our build's STEALTH DebugLog timestamps.

CGameSprite fields read off `this` (ecx):
  +0x005c m_id   +0x044a field_44A   +0x0476 m_curAction.m_actionID
  +0x4c53 m_nModalState   +0x723c m_modalCounter   +0x7240 m_bHiding

Usage (host -> VM):
  scripts/vm.sh frida scripts/frida_stealth_orig_trace.py            # spawn IWD2.exe
  scripts/vm.sh frida scripts/frida_stealth_orig_trace.py --attach   # attach running one

Then in-game (session 1): load the rogue save, click Hide in Shadows, watch.
Log (VM): C:\iwd2-re\tmp_frida_stealth.log -- read via `scripts/vm.sh tail -f
<that path>` or `scripts/vm.sh log . -f <that path>`.
"""
import frida
import sys
import os
import json
import time

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_stealth.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const SUB_757B40 = ptr(0x757b40);   // stealth detection cycle (gated %3)
const SETMODAL   = ptr(0x71fc00);   // CGameSprite::SetModalState
const FEEDBACK   = ptr(0x7349a0);   // CGameSprite::FeedBack
const SUB_72FD20 = ptr(0x72fd20);   // per-tick modal upkeep (id-staggered)
const EXECACTION = ptr(0x728f80);   // CGameSprite::ExecuteAction

// --- CInfButtonArray activation + state-stack (GAP B: bar refresh on reveal) ---
const ONLBTN     = ptr(0x58ff20);   // CInfButtonArray::OnLButtonPressed(buttonID)
const SETSTATE   = ptr(0x589110);   // CInfButtonArray::SetState(nState, a2)
const RESETSTATE = ptr(0x588ff0);   // CInfButtonArray::ResetState() -- pop+reapply
const POPSTATE   = ptr(0x589ff0);   // FUN_00589ff0 -- pop variant (back-clicks)
const MSGADDACT  = ptr(0x4f4f90);   // CMessageAddAction::ctor(action&, caller, target)

const O_ID = 0x5c, O_STATE = 0x4c53, O_COUNTER = 0x723c, O_HIDING = 0x7240;
const O_F44A = 0x44a, O_CURACTION_ID = 0x476;
// CInfButtonArray: m_nState @ +0x1982, m_nStateStackDepth @ +0x19b2.
const O_BTN_STATE = 0x1982, O_BTN_DEPTH = 0x19b2;

const t0 = Date.now();
function dt() { return Date.now() - t0; }   // ms since hooks live

// __thiscall: ecx = this; stack args start at args[0].

// Each entry == one real detection cycle (the %3 gate already passed upstream).
Interceptor.attach(SUB_757B40, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'DETECT', ts: dt(),
        id:      t.add(O_ID).readS32(),
        state:   t.add(O_STATE).readU8(),
        counter: t.add(O_COUNTER).readU32(),
        hiding:  t.add(O_HIDING).readU32() });
    } catch (e) { send({ tag: 'DETECT_ERR', err: '' + e }); }
  }
});

// Stealth toggles on (newState 3) / off (oldState 3).
Interceptor.attach(SETMODAL, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'SETMODAL', ts: dt(),
        id:       t.add(O_ID).readS32(),
        oldState: t.add(O_STATE).readU8(),
        newState: args[0].toInt32() & 0xff,
        toolbar:  args[1].toInt32(),
        hiding:   t.add(O_HIDING).readU32() });
    } catch (e) { send({ tag: 'SETMODAL_ERR', err: '' + e }); }
  }
});

// Stealth feedback: 0x53 = skill-check (strref in a6 tells which message),
// 0xD = leaving-shadows / reveal cue, 0x11 = the hidden-upkeep cue.
Interceptor.attach(FEEDBACK, {
  onEnter(args) {
    const id = args[0].toInt32() & 0xffff;
    if (id !== 0x53 && id !== 0xd && id !== 0x11) return;
    const t = this.context.ecx;
    try {
      send({ tag: 'FB', ts: dt(),
        sprite: t.add(O_ID).readS32(),
        fbid:   id,
        a2:     args[1].toInt32(),
        a3:     args[2].toInt32(),
        a4:     args[3].toInt32(),
        a5:     args[4].toInt32() });
    } catch (e) { send({ tag: 'FB_ERR', err: '' + e }); }
  }
});

// Modal upkeep, emitted ONLY on the id-staggered work tick (field_44A % 100 ==
// m_id % 100), for a sprite in stealth (state 3).  This is the cadence: each
// emit == one m_modalCounter advance; sub_757B40 runs when counter % 3 == 0.
Interceptor.attach(SUB_72FD20, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      if (t.add(O_STATE).readU8() !== 3) return;
      const f = t.add(O_F44A).readU32();
      const id = t.add(O_ID).readS32();
      if ((f % 100) !== (((id % 100) + 100) % 100)) return;
      const c = t.add(O_COUNTER).readU32();   // pre-increment
      send({ tag: 'MODALTICK', ts: dt(), id, f44a: f,
        counter: c, nextGate: (c + 1) % 3 });
    } catch (e) { send({ tag: 'MODALTICK_ERR', err: '' + e }); }
  }
});

// Action dispatch -- log only the two Hide-family actions, to settle whether
// the player click ever queues Hide() (18) or HideCreature (0xE9) in the
// original, or whether stealth is purely modal.
Interceptor.attach(EXECACTION, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      const aid = t.add(O_CURACTION_ID).readU16();
      if (aid !== 18 && aid !== 0xe9) return;
      send({ tag: 'ACTION', ts: dt(),
        id:     t.add(O_ID).readS32(),
        action: aid,
        state:  t.add(O_STATE).readU8() });
    } catch (e) { send({ tag: 'ACTION_ERR', err: '' + e }); }
  }
});

// --- UPSTREAM: action-bar click + state-stack (CInfButtonArray, __thiscall: ecx) ---

// Every left-click on the bar, with the bar state + stack depth.  The
// skills-submenu entry (state 0x72 -> 0x73) and the stealth click inside the
// submenu are both visible; depth tells whether the original pushed the prior
// state so the auto-reveal ResetState can pop->refresh.
Interceptor.attach(ONLBTN, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'ONLBTN', ts: dt(),
        btn:   args[0].toInt32(),
        state: t.add(O_BTN_STATE).readS32(),
        depth: t.add(O_BTN_DEPTH).readS32() });
    } catch (e) { send({ tag: 'ONLBTN_ERR', err: '' + e }); }
  }
});

// SetState -- depth/state BEFORE the call.  Watch depth climb 0->1 when the
// original descends into a submenu (the push our port omits).
Interceptor.attach(SETSTATE, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'SETSTATE', ts: dt(),
        nState:      args[0].toInt32(),
        a2:          args[1].toInt32(),
        depthBefore: t.add(O_BTN_DEPTH).readS32(),
        stateBefore: t.add(O_BTN_STATE).readS32() });
    } catch (e) { send({ tag: 'SETSTATE_ERR', err: '' + e }); }
  }
});

// ResetState -- the auto-reveal path (sub_757B40) calls this to pop the stack
// and re-apply the prior bar state.  depth!=0 == the push happened (original);
// depth 0 == nothing to pop (our port's permanent condition).
Interceptor.attach(RESETSTATE, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'RESETSTATE', ts: dt(),
        depth: t.add(O_BTN_DEPTH).readS32(),
        state: t.add(O_BTN_STATE).readS32() });
    } catch (e) { send({ tag: 'RESETSTATE_ERR', err: '' + e }); }
  }
});

// FUN_00589ff0 -- the back-click pop variant (2 OnLButton call sites).
Interceptor.attach(POPSTATE, {
  onEnter(args) {
    const t = this.context.ecx;
    try {
      send({ tag: 'POPSTATE', ts: dt(),
        depth: t.add(O_BTN_DEPTH).readS32(),
        p2: args[0].toInt32(), p3: args[1].toInt32() & 0xff });
    } catch (e) { send({ tag: 'POPSTATE_ERR', err: '' + e }); }
  }
});

// CMessageAddAction ctor -- the QUEUE moment.  action 18 == Hide queued by the
// stealth button (GAP A: our case 0xB drops this).  args[0] = CAIAction&,
// m_actionID @ +0.
Interceptor.attach(MSGADDACT, {
  onEnter(args) {
    try {
      const aid = args[0].readU16();
      send({ tag: 'ADDACTION', ts: dt(),
        action: aid,
        caller: args[1].toInt32(),
        target: args[2].toInt32() });
    } catch (e) { send({ tag: 'ADDACTION_ERR', err: '' + e }); }
  }
});

// --- INVISIBILITY VISUAL: where the shimmer (m_visualEffects[INVISIBILITY], bit 25
// of sprite+0xa2c) is set/kept and whether un-hide removes the effect. ---

// CGameEffectInvisible::ApplyEffect -- __thiscall: ecx=effect, args[0]=sprite.
// If this STOPS firing right after un-hide, the original removes the effect on
// un-hide (the immediate reveal); if it keeps firing, bit25 clears another way.
Interceptor.attach(ptr(0x4aed10), {
  onEnter(args) {
    const eff = this.context.ecx;
    const sp = args[0];
    try {
      send({ tag: 'INVISAPPLY', ts: dt(),
        id:        sp.add(0x5c).readS32(),
        amount:    eff.add(0x1c).readS32(),
        bit25:     (sp.add(0xa2c).readU32() & 0x2000000) ? 1 : 0,
        sanctuary: sp.add(0x8a0).readU8() & 1 });
    } catch (e) { send({ tag: 'INVISAPPLY_ERR', err: '' + e }); }
  }
});

// CGameEffectForceVisible::ApplyEffect -- the un-hide effect.  bit25 before tells
// us whether it (or the rebuild after) clears the shimmer.
Interceptor.attach(ptr(0x4b2990), {
  onEnter(args) {
    const sp = args[0];
    try {
      send({ tag: 'FORCEVIS', ts: dt(),
        id:         sp.add(0x5c).readS32(),
        bit25:      (sp.add(0xa2c).readU32() & 0x2000000) ? 1 : 0,
        sanctuary:  sp.add(0x8a0).readU8() & 1,
        derivState: sp.add(0x920).readU32() });
    } catch (e) { send({ tag: 'FORCEVIS_ERR', err: '' + e }); }
  }
});

// CGameSprite::SetStealthGreyOut(LONG greyOut) -- the re-stealth cooldown timer
// (m_nStealthGreyOut @ +0x727a).  Un-hide sets it to 90; AIUpdate counts it down.
// While > 0 the original greys the Stealth button so it cannot be re-armed.
Interceptor.attach(ptr(0x4531e0), {
  onEnter(args) {
    const sp = this.context.ecx;
    try {
      send({ tag: 'GREYOUT', ts: dt(),
        id:     sp.add(0x5c).readS32(),
        was:    sp.add(0x727a).readU16(),
        set:    args[0].toInt32() });
    } catch (e) { send({ tag: 'GREYOUT_ERR', err: '' + e }); }
  }
});

send({ tag: 'ready' });
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
