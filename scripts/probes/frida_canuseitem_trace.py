#!/usr/bin/env python3
r"""Differential trace of the ORIGINAL IWD2.exe CGameSprite::CanUseItem chain.

Goal: nail the TRUE 4-arg signature of CanUseItem (0x5b9d20). The src recovery
collapsed it to 2 args (CItem*, STRREF&) but the binary epilogue is `ret 0x10`
=> this + 4 stack args. We hook the wrapper that supplies them and CanUseItem
itself, classifying every pointer arg by its vtable so the 2 mystery args can be
named (object? CItem? errref? flag?).

Hooks (IWD2.exe absolute == Ghidra, no ASLR):
  0x5b9c60  FUN_005b9c60  wrapper  __thiscall(this, WORD idx, a1, ptr a2, a3)
  0x5b9d20  CGameSprite::CanUseItem  __thiscall(this, a0, a1, a2, a3)  ret 0x10

Run via:  scripts/vm.sh frida scripts/frida_canuseitem_trace.py
Then in-game (ORIGINAL IWD2.exe, save loaded, cheat item created): open the
inventory and hover/move the item so the slot Render + Use-button check run.

Output (VM-side, fsync'd):  C:\iwd2-re\tmp_canuseitem.log   -> pull host-side.
"""
import frida
import sys
import os
import time
import json

OUT = r"C:\iwd2-re\tmp_canuseitem.log"

JS = r"""
'use strict';
const Wrapper    = ptr(0x5b9c60);
const CanUseItem = ptr(0x5b9d20);

function dwords(p, n) {
  const a = [];
  try { for (let i = 0; i < n; i++) a.push('0x' + (p.add(i*4).readU32() >>> 0).toString(16)); }
  catch (e) { a.push('ERR'); }
  return a;
}
// classify a 32-bit arg: small int (flag/idx), object (vtable in image), or data ptr
function cls(p) {
  const iv = p.toInt32();
  if (iv > -0x10000 && iv < 0x10000) return { v: iv, t: 'int' };
  const r = { v: '0x' + (iv >>> 0).toString(16) };
  try {
    const vt = p.readPointer();
    const vti = vt.toInt32() >>> 0;
    if (vti >= 0x401000 && vti < 0x8b0000) { r.t = 'obj'; r.vtable = '0x' + vti.toString(16); }
    else { r.t = 'ptr'; r.head = dwords(p, 2); }
  } catch (e) { r.t = 'bad'; }
  return r;
}
function s16(v) { return (v.toInt32() << 16) >> 16; }

const seen = {};
let nUnique = 0;

Interceptor.attach(Wrapper, {
  onEnter(args) {
    const thiz = this.context.ecx;
    let count = -1, objid = '?';
    try {
      count = s16(thiz.add(0x3846).readU16());
      const idx = s16(args[0].toInt32() & 0xffff);
      objid = (idx >= 0 && idx < count)
        ? '0x' + (thiz.add(0x382e + idx*4).readU32() >>> 0).toString(16)
        : 'DEFAULT(-1)';
    } catch (e) { objid = 'ERR:' + e; }
    send({ tag: 'WRAP', this: thiz.toString(), thisInfo: cls(thiz),
      idx: s16(args[0].toInt32() & 0xffff), count: count, resolvedObjId: objid,
      a1: cls(args[1]), a2: cls(args[2]), a3: cls(args[3]) });
  }
});

Interceptor.attach(CanUseItem, {
  onEnter(args) {
    const thiz = this.context.ecx;
    const ra = this.returnAddress;
    // ra in [wrapper, CanUseItem) == called FROM the wrapper 0x5b9c60.
    const fromWrap = ra.compare(ptr(0x5b9c60)) >= 0 && ra.compare(ptr(0x5b9d20)) < 0;
    const sig = [thiz, args[0], args[1], args[2], args[3], ra].map(p => p.toInt32()).join(',');
    this.dup = !!seen[sig];
    if (this.dup) return;
    seen[sig] = 1;
    if (++nUnique > 400) { this.dup = true; return; }
    this.rec = {
      tag: 'CUI', n: nUnique, ra: ra.toString(), fromWrap: fromWrap, this: thiz.toString(),
      a0: cls(args[0]), a1: cls(args[1]), a2: cls(args[2]), a3: cls(args[3]),
    };
    send(this.rec);
  },
  onLeave(retval) {
    if (this.dup || !this.rec) return;
    send({ tag: 'CUI_ret', n: this.rec.n, ret: retval.toInt32(),
           a2_after: dwords(ptr(this.rec.a2.v), 1)[0] });  // errref slot after?
  }
});

send({ tag: 'ready' });
"""


def main():
    out = open(OUT, "w", buffering=1)

    def write(line):
        out.write(line + "\n")
        out.flush()
        os.fsync(out.fileno())

    def on_message(message, data):
        if message["type"] == "send":
            write(json.dumps(message["payload"]))
        else:
            write("ERROR " + json.dumps(message))

    # Attach to the ORIGINAL game (our build's CanUseItem is the buggy 2-arg recompile).
    session = frida.attach("IWD2.exe")
    write(json.dumps({"tag": "attached", "to": "IWD2.exe"}))
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    write(json.dumps({"tag": "hooks_live", "out": OUT}))
    # vm.sh frida payload: NO stdin (EOF -> exit). Keep alive.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
