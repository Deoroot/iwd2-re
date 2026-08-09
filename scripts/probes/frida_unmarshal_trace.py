#!/usr/bin/env python3
"""Trace the ORIGINAL IWD2.exe CInfGame::Unmarshal on a save load.

Goal: drive the version-aware rewrite of CInfGame::Unmarshal. From the GAM
buffer (param_2 = pGame) we dump the header counts/offsets/flags so we know,
for a real IWD2 save, which sections actually execute:

  - party count (file +0x24, param_2[9])      -> loop B (portraits)
  - non-party count (file +0x34, param_2[0xd]) -> loop A (global creatures)
  - flags (file +0x60, param_2[0x18]) bits 0x10/0x20/0x40 -> ScreenWorld view restore
  - familiars offset (file +0x68, param_2[0x1a]) != 0 -> familiars section
  - version string (file +0x00) -> 0x14/0x15/0x16 (which convert path)

We also hook the two version upconverters (0x5A79E0 / 0x5A7BF0) to PROVE they
never fire for a native V2.2 save, and FUN_007e8182 filtered by return address
inside Unmarshal to see whether the trailing pocket-plane section runs.

IWD2.exe has no ASLR (ImageBase 0x400000) -> Ghidra addresses are absolute.

Fire-and-forget payload (vm.sh frida): no stdin, stdout redirect is unreliable,
so every line is written to a dedicated VM-side log with flush + fsync.

Usage (host): scripts/vm.sh frida scripts/frida_unmarshal_trace.py
Then in-game (session 1): load slot 3. Log: C:\\iwd2-re\\tmp_frida_unmarshal.log
"""
import frida
import sys
import os
import json
import time

LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_unmarshal.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const Unmarshal = ptr(0x5a7e40);
const Conv20to21 = ptr(0x5a79e0);
const Conv21to22 = ptr(0x5a7bf0);
const Gate7e8182 = ptr(0x7e8182);
// Unmarshal spans [0x5a7e40, 0x5a9660) (next fn = FUN_005a9660). Used to filter
// the shared FUN_007e8182 (82 callers) down to the call inside Unmarshal.
const UM_LO = ptr(0x5a7e40);
const UM_HI = ptr(0x5a9660);

function rdU32(p, off) { try { return p.add(off).readU32(); } catch (e) { return -1; } }

// CInfGame::Unmarshal(BYTE* pGame, LONG nGame, BOOLEAN bProgressBarInPlace)
// __thiscall: ecx = this(CInfGame*); args[0]=pGame, args[1]=nGame, args[2]=bProgress.
Interceptor.attach(Unmarshal, {
  onEnter(args) {
    const pGame = args[0];
    let sig = '?';
    try { sig = pGame.readUtf8String(8); } catch (e) { sig = 'ERR:' + e; }
    let mode = 0x14;
    if (sig === 'GAMEV2.2') mode = 0x16;
    else if (sig === 'GAMEV2.1') mode = 0x15;
    send({
      tag: 'UNMARSHAL',
      this: this.context.ecx.toString(),
      pGame: pGame.toString(),
      nGame: args[1].toInt32(),
      bProgress: args[2].toInt32() & 0xff,
      sig: sig,
      versionMode: '0x' + mode.toString(16),
      // header dwords (file offsets == param_2[idx]*4)
      partyCount:     rdU32(pGame, 0x24),  // param_2[9]  -> loop B
      partyOffset:    rdU32(pGame, 0x20),  // param_2[8]
      partyInvCount:  rdU32(pGame, 0x2c),  // param_2[0xb]
      nonPartyCount:  rdU32(pGame, 0x34),  // param_2[0xd] -> loop A
      nonPartyOffset: rdU32(pGame, 0x30),  // param_2[0xc]
      globalsCount:   rdU32(pGame, 0x3c),  // param_2[0xf]
      globalsOffset:  rdU32(pGame, 0x38),  // param_2[0xe]
      journalCount:   rdU32(pGame, 0x4c),  // param_2[0x13]
      flags:          '0x' + (rdU32(pGame, 0x60) >>> 0).toString(16), // param_2[0x18]
      storedLocOff:   rdU32(pGame, 0x64),  // param_2[0x19]
      familiarsOff:   rdU32(pGame, 0x68),  // param_2[0x1a] != 0 -> familiars section
      difficulty:     rdU32(pGame, 0x6c),  // param_2[0x1b]
    });
  }
});

// Version upconverters -- should NOT fire for a native V2.2 save. cdecl(src,dst).
Interceptor.attach(Conv20to21, { onEnter(args) {
  send({ tag: 'CONV_20to21', src: args[0].toString(), dst: args[1].toString() }); } });
Interceptor.attach(Conv21to22, { onEnter(args) {
  send({ tag: 'CONV_21to22', src: args[0].toString(), dst: args[1].toString() }); } });

// FUN_007e8182 gate -- only the call whose return address lies inside Unmarshal
// (decides whether the trailing pocket-plane / stored-locations section runs).
Interceptor.attach(Gate7e8182, {
  onEnter(args) {
    const ra = this.returnAddress;
    this.inUM = ra.compare(UM_LO) >= 0 && ra.compare(UM_HI) < 0;
    this.ra = ra;
  },
  onLeave(retval) {
    if (!this.inUM) return;
    send({ tag: 'GATE_7e8182', ret: retval.toInt32(), ra: this.ra.toString() });
  }
});

send({ tag: 'ready' });
"""


def main():
    fd = os.open(LOG, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)

    def emit(line):
        os.write(fd, (line + "\n").encode("utf-8", "replace"))
        os.fsync(fd)

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        emit(line)

    try:
        session = frida.attach("IWD2.exe")
    except Exception as e:
        emit(json.dumps({"tag": "ATTACH_FAIL", "err": str(e)}))
        return
    emit(json.dumps({"tag": "attached", "log": LOG}))

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    emit(json.dumps({"tag": "hooks_live",
                     "note": "load slot 3 now to trigger CInfGame::Unmarshal"}))

    # Stay alive (no stdin in the fire-and-forget payload); periodic fsync heartbeat.
    while True:
        time.sleep(0.5)
        try:
            os.fsync(fd)
        except Exception:
            pass


if __name__ == "__main__":
    main()
