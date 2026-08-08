#!/usr/bin/env python3
"""ORIGINAL IWD2.exe: capture the SFX (if any) played when a Search-mode rogue
REVEALS a trap.

Static recovery shows NO PlaySound on either reveal path:
  * sprite per-tick modal detect (CGameSprite, the door/trigger/container sweep)
    only sets m_trapDetected=1, sends a *Status message, and calls AutoPause(0x80);
  * CGameTrigger::AddEffect (Detect-Traps effect) reveal does the same minus the
    pause -- its only extra call is the trap-activator anim virtual [edx+0xB0].
So whether a reveal *sound* exists at all -- and which resref -- can only be
settled at runtime on the original.  This trace nets every CSound the engine
builds/plays in a window around the detect-traps AutoPause(0x80) and records WHO
built it (caller return address), so we can map the reveal sound back to source.

IWD2.exe has no ASLR (ImageBase 0x400000) -> Ghidra addresses are absolute, and
returnAddress values ARE Ghidra addresses (map with `sym.py addr2fn`).

Hooks (entry-only -- mid-function hooks crashed the original last arc):
  0x71DFB0  CGameSprite::AutoPause(type)        arg0==0x80 -> DETECT_PAUSE anchor
  0x7A8C90  CSound::CSound(CResRef,...)         resref @[esp+4] (8 bytes by value)
  0x7A9B10  CSound::Play(BOOL)                  this->cResRef @ this+0x8
  0x7A9DB0  CSound::Play(x,y,z,BOOL)            this->cResRef @ this+0x8

Output is windowed to cut footstep/ambient noise: on each AutoPause(0x80) we emit
the handful of sounds buffered just BEFORE the pause and every sound for 1500 ms
AFTER it.  Unwindowed sounds are dropped.

Usage (host -> VM):
  scripts/vm.sh frida scripts/frida_trap_reveal_sfx.py            # spawn IWD2.exe
  scripts/vm.sh frida scripts/frida_trap_reveal_sfx.py --attach   # attach running

Then in-game (session 1): load save slot 4, select the rogue, toggle Find Traps
(Search), walk onto / next to the armed trap so it reveals.  Watch the red poly
appear -- that is the reveal instant.
Log (host repo root): tmp_frida_trap_sfx.log
"""
import frida
import sys
import os
import json
import time

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_trap_sfx.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const AUTOPAUSE = ptr(0x71DFB0);   // CGameSprite::AutoPause(DWORD type)
const CSOUND_CT = ptr(0x7A8C90);   // CSound::CSound(CResRef, int, int, int, int, BOOL)
const CSOUND_P2 = ptr(0x7A9B10);   // CSound::Play(BOOL)
const CSOUND_P3 = ptr(0x7A9DB0);   // CSound::Play(int,int,int,BOOL)

const O_SPRITE_ID = 0x5c;   // CGameSprite::m_id
const O_CSND_RES  = 0x08;   // CSound: CObject@0, CResHelper@4 (pRes@4, cResRef@8)
const PAUSE_TRAP  = 0x80;   // AutoPause type bit for "trap detected"
const WINDOW_MS   = 1500;   // emit sounds for this long after a trap pause

const t0 = Date.now();
function dt() { return Date.now() - t0; }

// Read an 8-char CResRef (space/NUL padded) into a trimmed string.
function resStr(p) {
  let s = '';
  for (let i = 0; i < 8; i++) {
    const c = p.add(i).readU8();
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.replace(/\s+$/, '');
}

// Ring buffer of recent sounds so we can show what played just BEFORE the pause.
const recent = [];
const MAXR = 16;
let pauseTs = -1e9;

function onSound(ev) {
  recent.push(ev);
  if (recent.length > MAXR) recent.shift();
  if (dt() - pauseTs < WINDOW_MS) {
    send(Object.assign({ tag: 'SFX', when: 'after' }, ev));
  }
}

// AutoPause(0x80): the trap-detected instant. Anchor + flush the pre-window.
Interceptor.attach(AUTOPAUSE, {
  onEnter(args) {
    try {
      if ((args[0].toInt32() & 0xff) !== PAUSE_TRAP) return;
      pauseTs = dt();
      let id = -1;
      try { id = this.context.ecx.add(O_SPRITE_ID).readS32(); } catch (e) {}
      send({ tag: 'DETECT_PAUSE', ts: pauseTs, id });
      for (const ev of recent) send(Object.assign({ tag: 'SFX', when: 'before' }, ev));
    } catch (e) { send({ tag: 'PAUSE_ERR', err: '' + e }); }
  }
});

// CSound ctor: resref passed BY VALUE -> 8 bytes at [esp+4]. Caller tells us who.
Interceptor.attach(CSOUND_CT, {
  onEnter(args) {
    try {
      const res = resStr(this.context.esp.add(4));
      onSound({ ts: dt(), kind: 'ctor', res, caller: this.returnAddress.toString() });
    } catch (e) { send({ tag: 'CTOR_ERR', err: '' + e }); }
  }
});

function playHook(variant) {
  return {
    onEnter(args) {
      try {
        const res = resStr(this.context.ecx.add(O_CSND_RES));
        onSound({ ts: dt(), kind: 'play' + variant, res,
                  caller: this.returnAddress.toString() });
      } catch (e) { send({ tag: 'PLAY_ERR', err: '' + e }); }
    }
  };
}
Interceptor.attach(CSOUND_P2, playHook('2d'));
Interceptor.attach(CSOUND_P3, playHook('3d'));
"""


def main():
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

    # No stdin under the fire-and-forget payload: block forever.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
