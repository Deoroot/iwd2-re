#!/usr/bin/env python3
"""Differential trace of the ORIGINAL IWD2.exe Color Spray cone via Frida.

Mirrors the DebugLog probes added to our build's CProjectileCone::Fire + Pulse,
so the two logs can be diffed under the SAME cast.

IWD2.exe: no ASLR (ImageBase 0x400000) -> Ghidra addresses are absolute.

Hooks (CProjectileCone, __thiscall, this = ecx):
  0x579EF0  Fire   -- onLeave: cone geometry + emission inputs (edgePoints built)
  0x57A970  Pulse  -- onEnter: per-pulse tick, edge count, base velocity

Color Spray filter: m_projectileType (+0x6E) == 0x60 (factory type 0x61 minus 1).

Run via vm.sh frida (session-1 payload, fire-and-forget). Output is written with
flush+fsync to a DEDICATED file (vm.sh frida stdout redirect is unreliable):
  C:\\iwd2-re\\tmp_frida_colorspray.jsonl
Then in-game (session 1): cast Color Spray ALONE on an enemy. Pull the log.
"""
import frida
import sys
import os
import time
import json

OUT = r"C:\iwd2-re\tmp_frida_colorspray.jsonl"
PROC = "IWD2.exe"   # the ORIGINAL game (never our iwd2-re.exe)

JS = r"""
'use strict';
const Fire  = ptr(0x579EF0);
const Pulse = ptr(0x57A970);

// CProjectileCone field offsets (binary, no debug shift).
const O_TYPE   = 0x6E;   // WORD  m_projectileType (Color Spray == 0x60)
const O_VEL    = 0x70;   // SHORT m_velocity
const O_CONELN = 0x2CE;  // LONG  m_coneLength
const O_OUTR   = 0x2D2;  // LONG  m_outerRadius
const O_SEGCNT = 0x2D6;  // LONG  m_segCount
const O_2EE    = 0x2EE;  // LONG  field_2EE (dirCount)
const O_EPBEG  = 0x2F6;  // CPoint* edgePoints._First
const O_EPEND  = 0x2FA;  // CPoint* edgePoints._Last
const O_SEGST  = 0x302;  // LONG  m_segmentStep
const O_306    = 0x306;  // LONG  field_306
const O_30A    = 0x30A;  // LONG  field_30A
const O_DUR    = 0x30E;  // LONG  m_duration
const O_PER    = 0x312;  // LONG  m_pulsePeriod
const O_TICK   = 0x32E;  // LONG  m_tickCount

function s16(p, off) { return (p.add(off).readU16() << 16) >> 16; }
function i32(p, off) { return p.add(off).readS32(); }
function edgeCount(p) {
  const b = p.add(O_EPBEG).readPointer();
  const e = p.add(O_EPEND).readPointer();
  if (b.isNull() || e.isNull()) return -1;
  return e.sub(b).toInt32() / 8;   // sizeof(CPoint) == 8
}

Interceptor.attach(Fire, {
  onEnter(args) { this.thiz = this.context.ecx; },
  onLeave(ret) {
    const p = this.thiz;
    const type = p.add(O_TYPE).readU16();
    if (type !== 0x60) return;   // Color Spray only
    send({
      tag: 'Fire', type: type,
      vel: s16(p, O_VEL), segCount: i32(p, O_SEGCNT), edgePts: edgeCount(p),
      coneLen: i32(p, O_CONELN), outR: i32(p, O_OUTR), segStep: i32(p, O_SEGST),
      dir2EE: i32(p, O_2EE), f306: i32(p, O_306), f30A: i32(p, O_30A),
      dur: i32(p, O_DUR), period: i32(p, O_PER),
    });
  }
});

Interceptor.attach(Pulse, {
  onEnter(args) {
    const p = this.context.ecx;
    const type = p.add(O_TYPE).readU16();
    if (type !== 0x60) return;   // Color Spray only
    send({
      tag: 'Pulse', type: type,
      tick: i32(p, O_TICK), edgePts: edgeCount(p), baseVel: s16(p, O_VEL),
    });
  }
});
send({tag: 'READY'});
"""


def main():
    out = open(OUT, "w", buffering=1)

    def on_message(msg, data):
        if msg.get("type") == "send":
            out.write(json.dumps(msg["payload"]) + "\n")
        else:
            out.write(json.dumps({"tag": "ERROR", "msg": str(msg)}) + "\n")
        out.flush()
        os.fsync(out.fileno())

    # Attach to the running ORIGINAL game (user launches it in session 1).
    for _ in range(120):
        try:
            session = frida.attach(PROC)
            break
        except frida.ProcessNotFoundError:
            time.sleep(1)
    else:
        out.write(json.dumps({"tag": "ERROR", "msg": "IWD2.exe not found"}) + "\n")
        out.flush()
        return

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    # vm.sh frida payload has no stdin -> keep alive explicitly.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
