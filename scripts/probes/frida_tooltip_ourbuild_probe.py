#!/usr/bin/env python3
"""Trace OUR iwd2-re.exe creature-tooltip wrap to find the truncation regression.

The original IWD2.exe renders "Goblin" / "Uninjured" on two whole lines
(status SplitString nLineLength=250, nMaxStrings=1, no split). Our build
truncates. Hook the same pipeline in our build and compare the runtime
numbers (endcap sizes, measured string widths, nLineLength, field_5E6).

Our build is ASLR (DYNAMIC_BASE), so resolve module base at runtime and add
the map RVA (VA - 0x400000):
  SetTextRef(STRREF&,CString&)  VA 0x5f4c80  RVA 0x1f4c80
  SplitString                   VA 0x7ce740  RVA 0x3ce740
  GetStringLength               VA 0x7e3ae0  RVA 0x3e3ae0
  GetFrameSize                  VA 0x7d5f40  RVA 0x3d5f40

GetStringLength / GetFrameSize fire constantly, so only log them while we are
inside SetTextRef (the tooltip layout).

Usage:
  python scripts/frida_tooltip_ourbuild_probe.py          # spawn our build
  python scripts/frida_tooltip_ourbuild_probe.py --attach  # attach to running one
Then: load slot 2, hover a goblin. Log: tmp_frida_tooltip_our.log.
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "iwd2-re.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_tooltip_our.log")

JS = r"""
'use strict';

const m = Process.findModuleByName('iwd2-re.exe');
const base = m.base;
send({ tag: 'mod', base: base.toString() });

const SetTextRef      = base.add(0x1f4c80);
const SplitString     = base.add(0x3ce740);
const GetStringLength = base.add(0x3e3ae0);
const GetFrameSize    = base.add(0x3d5f40);

const F_5E4 = 0x5e4, F_5E6 = 0x5e6, F_5EC = 0x5ec, F_5F0 = 0x5f0;

let inSetTextRef = 0;

function readCStr(pObj) {
  try {
    const pch = pObj.readPointer();
    if (pch.isNull()) return '';
    const len = pch.sub(8).readInt();
    if (len < 0 || len > 4096) return '<len=' + len + '>';
    return pch.readUtf8String(len);
  } catch (e) { return '<ERR:' + e + '>'; }
}

Interceptor.attach(SetTextRef, {
  onEnter(args) {
    this.thiz = this.context.ecx;
    inSetTextRef++;
    let ref = null; try { ref = args[0].readInt(); } catch (e) { ref = 'ERR'; }
    send({ tag: 'SetTextRef_in', textRef: ref, sExtra: readCStr(args[1]),
           field_5E4: this.thiz.add(F_5E4).readU16() });
  },
  onLeave() {
    send({ tag: 'SetTextRef_out',
           line0: readCStr(this.thiz.add(F_5EC)),
           line1: readCStr(this.thiz.add(F_5F0)),
           field_5E6: this.thiz.add(F_5E6).readS16() });
    inSetTextRef--;
  }
});

Interceptor.attach(SplitString, {
  onEnter(args) {
    this.out = args[4];
    this.nMax = args[3].toInt32() & 0xff;
    send({ tag: 'SplitString_in', sSource: readCStr(args[1]),
           nLineLength: args[2].toInt32() & 0xffff,
           nMaxStrings: this.nMax, bDivideWords: args[5].toInt32() });
  },
  onLeave(retval) {
    const lines = [];
    for (let i = 0; i < this.nMax && i < 4; i++) lines.push(readCStr(this.out.add(i * 4)));
    send({ tag: 'SplitString_out', nLines: retval.toInt32() & 0xff, lines: lines });
  }
});

// __thiscall GetStringLength(const CString& s, BOOLEAN bDemanded) -> LONG width
Interceptor.attach(GetStringLength, {
  onEnter(args) { this.log = inSetTextRef > 0; if (this.log) this.s = readCStr(args[0]); },
  onLeave(retval) { if (this.log) send({ tag: 'GetStringLength', s: this.s, width: retval.toInt32() }); }
});

// __thiscall GetFrameSize(SHORT seq, SHORT frame, CSize& out, BOOLEAN bDemanded)
Interceptor.attach(GetFrameSize, {
  onEnter(args) {
    this.log = inSetTextRef > 0;
    if (this.log) { this.seq = (args[0].toInt32()<<16)>>16; this.frame = (args[1].toInt32()<<16)>>16; this.out = args[2]; }
  },
  onLeave(retval) {
    if (this.log) send({ tag: 'GetFrameSize', seq: this.seq, frame: this.frame,
                         cx: this.out.readS32(), cy: this.out.add(4).readS32() });
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
        try:
            print(line, flush=True)
        except Exception:
            pass
        with open(LOG, "a") as f:
            f.write(line + "\n")

    if attach:
        session = frida.attach("iwd2-re.exe")
        pid = None
        print("[*] attached to running iwd2-re.exe", flush=True)
    else:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned iwd2-re.exe pid={pid}", flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print("[*] hooks live. In-game: load slot 2, hover a goblin.", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
