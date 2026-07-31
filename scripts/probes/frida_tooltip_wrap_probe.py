#!/usr/bin/env python3
"""Trace the ORIGINAL IWD2.exe creature-tooltip wrap pipeline via Frida.

The mouseover creature tooltip truncates words in our iwd2-re build. The
original renders cleanly, so trace it to capture the exact strings + the
correct wrap so we can diff against the recovered code.

Pipeline (CGameSprite::SetCharacterToolTip -> CInfCursor::SetToolTip ->):
  0x598940  CInfToolTip::SetTextRef(const STRREF& textRef, const CString& sExtra)  __thiscall
              field_5E4 (max width) @ this+0x5E4 (WORD)
              field_5E6 (fitted balloon width) @ this+0x5E6 (SHORT)
              field_5EC[0], field_5EC[1] (the two rendered lines) @ this+0x5EC, +0x5F0
  0x780CF0  CUtil::SplitString(CVidFont*, const CString& sSource, WORD nLineLength,
              BYTE nMaxStrings, CString* out, BOOL bDivideWords, ...)              __stdcall

IWD2.exe has no ASLR (ImageBase 0x400000) -> Ghidra addresses are absolute.

Usage:
  python scripts/frida_tooltip_wrap_probe.py          # spawn IWD2.exe, hook, log
  python scripts/frida_tooltip_wrap_probe.py --attach  # attach to running IWD2.exe

Then in-game: load the combat save (slot 2), hover a goblin (full HP).
Log: tmp_frida_tooltip.log (repo root). Ctrl-C / kill to stop.
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_tooltip.log")

JS = r"""
'use strict';

const SetCharTip  = ptr(0x706720);   // CGameSprite::SetCharacterToolTip(CUIControlBase*)
const SetToolTip3 = ptr(0x5976e0);   // CInfCursor::SetToolTip(STRREF&, CUIControlBase*, CString&)
const SetTextRef  = ptr(0x598940);   // CInfToolTip::SetTextRef(STRREF&, CString&)
const SplitString = ptr(0x780cf0);   // CUtil::SplitString

const F_5E4 = 0x5e4;   // field_5E4  max width (WORD)
const F_5E6 = 0x5e6;   // field_5E6  fitted balloon width (SHORT)
const F_5EC = 0x5ec;   // field_5EC[0] CString
const F_5F0 = 0x5f0;   // field_5EC[1] CString

function s16(v) { return (v.toInt32() << 16) >> 16; }

// MFC CString: the object is a single char* (m_pchData) into a buffer;
// length is the int at m_pchData[-8]. pObj points at the CString variable.
function readCStr(pObj) {
  try {
    const pch = pObj.readPointer();
    if (pch.isNull()) return '';
    const len = pch.sub(8).readInt();
    if (len < 0 || len > 4096) return '<len=' + len + '>';
    return pch.readUtf8String(len);
  } catch (e) { return '<ERR:' + e + '>'; }
}

// CGameSprite::SetCharacterToolTip(CUIControlBase*) __thiscall -> confirms creature path.
Interceptor.attach(SetCharTip, {
  onEnter(args) { send({ tag: 'SetCharTip_in', sprite: this.context.ecx.toString() }); }
});

// CInfCursor::SetToolTip(const STRREF& toolTipRef, CUIControlBase*, const CString& sExtra) __thiscall
Interceptor.attach(SetToolTip3, {
  onEnter(args) {
    let ref = null;
    try { ref = args[0].readInt(); } catch (e) { ref = 'ERR'; }
    send({ tag: 'SetToolTip3_in', toolTipRef: ref, sExtra: readCStr(args[2]) });
  }
});

// __thiscall: ecx = this; stack args at [esp+4]=args[0], [esp+8]=args[1].
// SetTextRef(const STRREF& textRef, const CString& sExtra)
Interceptor.attach(SetTextRef, {
  onEnter(args) {
    this.thiz = this.context.ecx;
    let textRef = null;
    try { textRef = args[0].readInt(); } catch (e) { textRef = 'ERR'; }
    send({
      tag: 'SetTextRef_in',
      textRef: textRef,
      sExtra: readCStr(args[1]),
      field_5E4: this.thiz.add(F_5E4).readU16(),
    });
  },
  onLeave() {
    send({
      tag: 'SetTextRef_out',
      line0: readCStr(this.thiz.add(F_5EC)),
      line1: readCStr(this.thiz.add(F_5F0)),
      field_5E6: s16(this.thiz.add(F_5E6).readU16()),
    });
  }
});

// __stdcall static: args[0]=pFont, args[1]=&sSource, args[2]=nLineLength,
//                    args[3]=nMaxStrings, args[4]=pStringsOut, args[5]=bDivideWords
Interceptor.attach(SplitString, {
  onEnter(args) {
    this.out = args[4];
    this.nMax = args[3].toInt32() & 0xff;
    send({
      tag: 'SplitString_in',
      sSource: readCStr(args[1]),
      nLineLength: args[2].toInt32() & 0xffff,
      nMaxStrings: this.nMax,
      bDivideWords: args[5].toInt32(),
    });
  },
  onLeave(retval) {
    const lines = [];
    for (let i = 0; i < this.nMax && i < 4; i++) {
      lines.push(readCStr(this.out.add(i * 4)));
    }
    send({ tag: 'SplitString_out', nLines: retval.toInt32() & 0xff, lines: lines });
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

    print("[*] hooks live. In-game: load combat save (slot 2), hover a goblin.", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
