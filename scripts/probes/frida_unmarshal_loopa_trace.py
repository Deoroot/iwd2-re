#!/usr/bin/env python3
"""Runtime trace of CInfGame::Unmarshal loop A (global-creature loader) in the
ORIGINAL IWD2.exe -- resolve the iteration / source-advance mechanism that the
Ghidra lift garbles (back-edge `cmp [pGame+0x34], edi; ja` with no visible
in-place count decrement and a fixed pGame+pGame[0x20] record base).

Also dumps the pocket-plane (stored-locations) raw records for later field-name
recovery of CSavedGameStoredLocation.

IWD2.exe = no ASLR (ImageBase 0x400000) -> Ghidra addresses are absolute.
This is an ATTACH driver: launch the ORIGINAL IWD2.exe to the main menu first,
run this, THEN load the save. Unmarshal fires on save-load.

Run VM-side as a session-1 payload:  scripts/vm.sh frida frida_unmarshal_loopa_trace.py
Captures -> C:\iwd2-re\tmp_loopa_trace.log  (dedicated, flushed+fsync'd; the
vm_s1_out.txt redirect is unreliable once the VBS parent exits).
"""
import frida
import sys
import os
import time

TARGET = "IWD2.exe"
OUT = r"C:\iwd2-re\tmp_loopa_trace.log"

JS = r"""
'use strict';
// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const ENTRY     = ptr(0x5a7e40);  // CInfGame::Unmarshal entry (__thiscall)
const LOOPA_REC = ptr(0x5a8341);  // loop A: ebp = record ptr, al = record[0xC] (resref[0])
const LOOPA_BE  = ptr(0x5a87e7);  // loop A back-edge: cmp [pGame+0x34], edi
const POCKET    = ptr(0x5a947d);  // after pocket-plane gate passes; ebp=pGame, [esp+0x10]=acc

let g_pGame = ptr(0);
let g_nGame = 0;
let g_iter  = 0;   // loop A iteration counter (JS-side ground truth)

function hex(p, n) {
  try { return p.readByteArray(n); } catch (e) { return null; }
}

// --- entry: capture pGame/nGame + header counts -------------------------------
Interceptor.attach(ENTRY, {
  onEnter(args) {
    const esp = this.context.esp;
    g_pGame = esp.add(4).readPointer();
    g_nGame = esp.add(8).readU32();
    g_iter = 0;
    const p = g_pGame;
    send({ t: 'entry',
           pGame: p.toString(),
           nGame: g_nGame,
           off20_base:     p.add(0x20).readU32(),
           cnt24_party:    p.add(0x24).readU32(),
           off30:          p.add(0x30).readU32(),
           cnt34_global:   p.add(0x34).readU32(),
           cfg64:          p.add(0x64).readU32(),
           sig: p.readUtf8String(8) },
         hex(p, 0xB4));
  }
});

// --- loop A per-record: does ebp (record ptr) advance across iterations? ------
Interceptor.attach(LOOPA_REC, {
  onEnter(args) {
    const rec = this.context.ebp;
    const al  = this.context.eax.toInt32() & 0xff;
    let resref = '';
    try { resref = rec.add(0xc).readAnsiString(8); } catch (e) {}
    const delta = g_pGame.isNull() ? 0 : rec.sub(g_pGame).toInt32();
    send({ t: 'loopA_rec',
           iter: g_iter++,
           rec: rec.toString(),
           rec_minus_pGame: delta,
           resref0_al: al,
           creOffset: rec.add(0x4).readU32(),
           creSize:   rec.add(0x8).readU32(),
           resref: resref });
  }
});

// --- loop A back-edge: what are edi and pGame[0x34] at the compare? ------------
Interceptor.attach(LOOPA_BE, {
  onEnter(args) {
    send({ t: 'loopA_be',
           edi: this.context.edi.toInt32(),
           cnt34_now: g_pGame.isNull() ? -1 : g_pGame.add(0x34).readU32() });
  }
});

// --- pocket-plane: acc + raw record dump (for CSavedGameStoredLocation names) --
Interceptor.attach(POCKET, {
  onEnter(args) {
    const esp = this.context.esp;
    const acc = esp.add(0x10).readU32();
    const base = g_pGame.add(acc);
    let count = -1;
    try { count = base.readU32(); } catch (e) {}
    send({ t: 'pocket',
           acc: acc,
           pocket_base: base.toString(),
           count: count },
         hex(base, 0x140));   // count dword + first records
  }
});

send({ t: 'ready' });
"""


def main():
    log = open(OUT, "w", buffering=1)
    fd = log.fileno()

    def w(line):
        log.write(line + "\n")
        log.flush()
        os.fsync(fd)

    def on_message(msg, data):
        if msg.get("type") == "send":
            payload = msg["payload"]
            w(repr(payload))
            if data:
                w("  HEX " + data.hex())
        else:
            w("ERR " + repr(msg))

    try:
        session = frida.attach(TARGET)
    except Exception as e:
        w("ATTACH FAILED: " + repr(e))
        return
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    w("=== hooks installed; load the save now ===")
    # No stdin under the VBS payload -> keep alive explicitly.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
