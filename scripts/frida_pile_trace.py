#!/usr/bin/env python3
"""Spawn original IWD2.exe with Frida hooks on the ground-pile FX path.

Compares the original's FX-surface clear/key handling against the RE build's
black-box bug. Hooks (absolute addresses, no ASLR, ImageBase 0x400000):

  CGameContainer::Render   0x47F580  (gate: m_containerType @ this+0x5CA == 4)
  CVidInf::FXPrep          0x79C770  (read field_24 @ this+0x24, dwFlags=args[1])
  CVidInf::FXBltToBack     0x79CC90  (read field_24, dwFlags=args[6])

Per the capture workflow: this just spawns ready-to-log. Load save slot 1 in
the launched original and scroll to the tavern loot, then read the log.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import frida

GAME_DIR = Path(r"C:\GOG Games\Icewind Dale 2")
ORIG_EXE = GAME_DIR / "IWD2.exe"
LOG = Path(__file__).resolve().parents[1] / "tmp_frida_pile_orig.log"

JS = r"""
var RENDER   = ptr(0x47F580);
var FXPREP   = ptr(0x79C770);
var FXBLT    = ptr(0x79CC90);

var inPile = 0;
var pileId = 0;

Interceptor.attach(RENDER, {
  onEnter: function (args) {
    var self = this.context.ecx;
    var ct = self.add(0x5CA).readU16();
    if (ct === 4) {
      inPile = 1;
      pileId = self.add(0x5C).readS32();
    } else {
      inPile = 0;
    }
  },
  onLeave: function (r) { inPile = 0; }
});

Interceptor.attach(FXPREP, {
  onEnter: function (args) {
    if (!inPile) return;
    var self = this.context.ecx;
    send({ tag: 'FXPrep', id: pileId,
           field24: self.add(0x24).readU32() >>> 0,
           flags: args[1].toUInt32() >>> 0 });
  }
});

Interceptor.attach(FXBLT, {
  onEnter: function (args) {
    if (!inPile) return;
    var self = this.context.ecx;
    send({ tag: 'FXBltToBack', id: pileId,
           field24: self.add(0x24).readU32() >>> 0,
           flags: args[6].toUInt32() >>> 0 });
  }
});
"""


def main() -> int:
    if not ORIG_EXE.exists():
        raise SystemExit(f"missing {ORIG_EXE}")
    LOG.write_text("", encoding="utf-8")

    seen: set[str] = set()

    def on_message(message, data):
        if message.get("type") != "send":
            line = f"[frida] {message}"
        else:
            p = message["payload"]
            line = (f"{p['tag']} id={p['id']} "
                    f"field24=0x{p['field24']:X} flags=0x{p['flags']:X}")
        key = line.split("] ", 1)[-1]
        if key in seen:
            return
        seen.add(key)
        print(line, flush=True)
        with LOG.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    pid = frida.spawn([str(ORIG_EXE)], cwd=str(GAME_DIR))
    session = frida.attach(pid)
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print(f"pid={pid}; hooks live. Load slot 1, scroll to the tavern loot.")
    print(f"log -> {LOG}")
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\nstopping")
        session.detach()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
