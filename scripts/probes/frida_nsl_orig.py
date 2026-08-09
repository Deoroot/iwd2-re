#!/usr/bin/env python3
"""Original-build ground truth for the 00AMVW wander chain.

Hooks (original IWD2.exe, absolute addresses, no ASLR):
  0x731B30 CGameSprite::EvaluateStatusTrigger -- for NearSavedLocation (0x4099)
           and Range-family ids, log per-sprite saved location (+0x888/+0x88A),
           pos (+6/+0xA), id (+0x5C), trigger result (onLeave eax).
  0x748CA0 CGameSprite::RandomWalk -- per-sprite call count.

Run on the VM in session 1:
  python scripts\frida_nsl_orig.py --slot 0 --duration 90
Log: C:\iwd2-re\tmp_nsl_orig.log
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_audio_trace import ORIGINAL_STARTUP_SKIP_SECONDS, original_load_driver  # noqa: E402
from frida_intro_trace import ORIG_EXE, REPO  # noqa: E402

JS = r"""
'use strict';
const nslSamples = {};   // sprite id -> last {saved, pos, range, result}
const nslCounts = {};
const rwCounts = {};

Interceptor.attach(ptr(0x731B30), {
  onEnter(args) {
    this.trig = -1;
    try { this.trig = args[0].readU16(); } catch (e) {}
    if (this.trig !== 0x4099) return;
    this.thiz = this.context.ecx;
    this.range = 0;
    try { this.range = args[0].add(2).readS32(); } catch (e) {}
  },
  onLeave(rv) {
    if (this.trig !== 0x4099) return;
    try {
      const id = this.thiz.add(0x5C).readS32();
      const sx = this.thiz.add(0x888).readU16();
      const sy = this.thiz.add(0x88A).readU16();
      const px = this.thiz.add(0x6).readS32();
      const py = this.thiz.add(0xA).readS32();
      nslCounts[id] = (nslCounts[id] || 0) + 1;
      nslSamples[id] = { saved: [sx, sy], pos: [px, py], range: this.range,
                         result: rv.toInt32() & 0xff };
    } catch (e) {}
  }
});

Interceptor.attach(ptr(0x748CA0), {
  onEnter() {
    try {
      const id = this.context.ecx.add(0x5C).readS32();
      rwCounts[id] = (rwCounts[id] || 0) + 1;
    } catch (e) {}
  }
});

setInterval(function () {
  send({ tag: 'snapshot', t: Date.now(), nslSamples: nslSamples,
         nslCounts: nslCounts, rwCounts: rwCounts });
}, 5000);

send({ tag: 'ready' });
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--slot", type=int, default=0)
    ap.add_argument("--duration", type=float, default=90.0)
    args = ap.parse_args()

    out = REPO / "tmp_nsl_orig.log"
    out.write_text("", encoding="utf-8")

    def emit(payload: dict) -> None:
        with out.open("a", encoding="utf-8") as f:
            f.write(json.dumps(payload) + "\n")

    pid = frida.spawn([str(ORIG_EXE)], cwd=str(ORIG_EXE.parent))
    session = frida.attach(pid)

    snapshots: list[dict] = []

    def on_message(message, _data) -> None:
        if message.get("type") == "send":
            payload = message["payload"]
            if payload.get("tag") == "snapshot":
                snapshots.append(payload)
            emit(payload)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    drv = threading.Thread(
        target=original_load_driver,
        args=(pid, args.slot, ORIGINAL_STARTUP_SKIP_SECONDS, emit),
        daemon=True,
    )
    drv.start()
    drv.join(timeout=240.0)

    time.sleep(args.duration)

    emit({"tag": "final", "snapshot": snapshots[-1] if snapshots else None})
    try:
        session.detach()
    except Exception:
        pass
    try:
        frida.kill(pid)
    except Exception:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
