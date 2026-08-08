"""Trap-chain differential trace on the ORIGINAL IWD2.exe.

Goal: find what the original calls to (a) RENDER a revealed trap polygon and
(b) SPRING a trap on contact -- the drivers our build is missing.

Method: entry-only hooks on the CGameTrigger runtime methods. For each, capture
the CALLER (return address on the stack at entry) and DEDUP per (fn, caller).
The distinct caller set per hook = the driver functions. Symbolize offline with
`scripts/sym.py addr2fn 0x<caller>` (IWD2.exe has no ASLR, base 0x400000, so the
return addresses are absolute Ghidra addresses).

Run (attach to a USER-launched original, slot 4 loaded):
  scripts/vm.sh frida scripts/frida_trap_chain_trace.py --attach
Then in the game: search for traps with a thief AND walk a character onto the
trap. Read the log: scripts/vm.sh tail  (and host: tmp_frida_trap_chain.log)
"""
import frida
import sys
import os
import json
import time

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_trap_chain.log")

JS = r"""
'use strict';
// IWD2.exe, no ASLR -> all addresses absolute (base 0x400000).
// ENTRY-only hooks: read everything off `this` (ecx) + the caller return addr
// ([esp] at entry). Mid-function hooks on the trigger crash the original.

// CGameTrigger runtime methods:
const HOOKS = [
  { fn: 'SetDrawPoly',    addr: 0x4D0230 },  // reveal: m_drawPoly := time
  { fn: 'IsOverActivate', addr: 0x4CECB0 },  // active-region test (spring/contact)
  { fn: 'IsOver',         addr: 0x4CEBE0 },  // over-test (cursor/contact)
  { fn: 'AddEffect',      addr: 0x4CD650 },  // effect delivered TO the trigger
  { fn: 'AIUpdate',       addr: 0x4CD630 },  // per-tick
  { fn: 'GetPos',         addr: 0x4D02A0 },  // render reads position/polygon
];

// CGameTrigger fields off `this`:
const O_ID = 0x5c, O_FLAGS = 0x5d6, O_TRAPACT = 0x612, O_TRAPDET = 0x614, O_DRAWPOLY = 0x626;

const seen = {};   // dedup key "fn|caller" -> true
const t0 = Date.now();
function ms() { return Date.now() - t0; }

function rd16(p, off) { try { return p.add(off).readU16(); } catch (e) { return -1; } }
function rd32(p, off) { try { return p.add(off).readU32(); } catch (e) { return -1; } }

HOOKS.forEach(function (h) {
  Interceptor.attach(ptr(h.addr), {
    onEnter: function (args) {
      try {
        var thisp = this.context.ecx;
        var caller = this.context.esp.readPointer();   // [esp] = return addr
        var key = h.fn + '|' + caller;
        var first = !seen[key];
        seen[key] = true;
        // AIUpdate/GetPos/IsOver fire every tick for every trigger -> only emit
        // the FIRST time we see each distinct caller (keeps the log a caller map).
        // SetDrawPoly/IsOverActivate/AddEffect are rare -> emit every time.
        var rare = (h.fn === 'SetDrawPoly' || h.fn === 'IsOverActivate' || h.fn === 'AddEffect');
        if (!first && !rare) return;
        var rec = {
          tag: 'TRAP', fn: h.fn, t: ms(),
          caller: '' + caller, first: first,
          m_id: rd32(thisp, O_ID),
          drawPoly: rd16(thisp, O_DRAWPOLY),
          trapAct: rd16(thisp, O_TRAPACT),
          trapDet: rd16(thisp, O_TRAPDET),
          flags: '0x' + rd32(thisp, O_FLAGS).toString(16),
        };
        if (h.fn === 'AddEffect') {
          // args[0] = CGameEffect* ; m_effectID at +0xc
          try { rec.effectID = args[0].add(0xc).readU32(); } catch (e) { rec.effectID = -1; }
        }
        if (h.fn === 'SetDrawPoly') {
          try { rec.time = args[0].toInt32() & 0xffff; } catch (e) {}
        }
        send(rec);
      } catch (e) { send({ tag: 'ERR', fn: h.fn, err: '' + e }); }
    }
  });
});
send({ tag: 'HOOKS_INSTALLED', n: HOOKS.length });
"""


def main():
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

    spawn = "--spawn" in sys.argv
    if spawn:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        emit("SPAWNED pid=%s" % pid)
    else:
        session = frida.attach("IWD2.exe")
        pid = None
        emit("ATTACHED IWD2.exe")

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
