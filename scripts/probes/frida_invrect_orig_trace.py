#!/usr/bin/env python3
r"""Differential trace of the ORIGINAL IWD2.exe inventory-slot invalidation.

Question: under the right-click item popup (panel 5), OUR build re-invalidates ALL
53 panel-2 inventory-slot buttons EVERY frame (caller 0x9E5F17 in our Debug exe),
so the disabled main panel keeps redrawing its 50% dither over the popup. We need
to know whether the ORIGINAL does the same (=> fix is "redraw the popup on top")
or NOT (=> our recovery dropped a popup guard on the slot-invalidator).

Hook (IWD2.exe absolute == Ghidra, no ASLR):
  0x4D56A0  CUIControlButton::InvalidateRect  __thiscall(this)
Offsets (binary-mirror, packed):
  control this+0x06 = m_pPanel ; panel +0x20 = m_nID ; control this+0x0A = m_nID

Aggregates per caller return-address (IWD2.exe addr -> sym.py addr2fn), counting
calls + distinct control ids, but ONLY for panel id 2 (the inventory main panel).
A SUMMARY is dumped every 2s so the rate before/after opening the popup is visible.

Run via:  scripts/vm.sh frida scripts/frida_invrect_orig_trace.py
In-game (ORIGINAL IWD2.exe, save loaded): open inventory, then RIGHT-CLICK an item
to open popup-5; leave it open a few seconds.

Output (VM-side, fsync'd):  C:\iwd2-re\tmp_invrect.log   -> read host-side.
"""
import frida
import sys
import os
import time
import json

OUT = r"C:\iwd2-re\tmp_invrect.log"

JS = r"""
'use strict';
const InvRect = ptr(0x4D56A0);
const byRa = {};        // ra -> total count
const ctrlsByRa = {};   // ra -> { ctrlId: 1 }

Interceptor.attach(InvRect, {
  onEnter(args) {
    const thiz = this.context.ecx;
    let panelId = -1, ctrlId = -1;
    try {
      const pPanel = thiz.add(0x6).readPointer();
      if (pPanel.isNull()) return;
      panelId = pPanel.add(0x20).readU32();
      if (panelId !== 2) return;             // inventory main panel only
      ctrlId = thiz.add(0xA).readU32();
    } catch (e) { return; }
    const ra = '0x' + (this.returnAddress.toInt32() >>> 0).toString(16);
    byRa[ra] = (byRa[ra] || 0) + 1;
    (ctrlsByRa[ra] = ctrlsByRa[ra] || {})[ctrlId] = 1;
  }
});

function dump() {
  const rows = [];
  for (const ra in byRa) {
    rows.push({ ra: ra, count: byRa[ra], distinctCtrls: Object.keys(ctrlsByRa[ra]).length });
  }
  rows.sort((a, b) => b.count - a.count);
  send({ tag: 'SUMMARY', rows: rows });
}
setInterval(dump, 2000);
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
