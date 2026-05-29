#!/usr/bin/env python3
"""Differential trace of the ORIGINAL IWD2.exe A* pathfinder via Frida.

Captures every CPathSearch::FindPath call's inputs (start, goals, bBump) and its
output path (m_pathBegin[0..m_nPathNodes-1], grid positions) so we can diff the
binary's A* result against our build's for the SAME move and find path-quality
divergences. See docs/frida-differential-tracing.md.

  0x51E150  CPathSearch::FindPath(POINT* start, POINT* goals, int nGoals,
                LONG minNode, LONG maxNode, CSearchBitmap*, BYTE* serviceState,
                BOOL bBump, CRect* gridVirtual)  __thiscall
            -> this->m_pathBegin @ +0x10 (LONG*), this->m_nPathNodes @ +0x14 (SHORT)

Grid stride = 320 (GRID_ACTUALX); position = y * 320 + x (PointToPosition).

Usage:
  python scripts/frida_path_trace.py           # spawn IWD2.exe, hook, log
  python scripts/frida_path_trace.py --attach   # attach to a running IWD2.exe

In-game (pause AI to cut ambient-NPC noise if possible): select ONE character,
left-click a far open-ground point. Log: tmp_frida_path.log.
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_path.log")

JS = r"""
'use strict';
// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const FindPath = ptr(0x51e150);
const STRIDE = 320;                  // GRID_ACTUALX

function pt(p) { return [p.readS32(), p.add(4).readS32()]; }
function decode(pos) { return [pos % STRIDE, (pos / STRIDE) | 0]; }

// CPathSearch::FindPath(POINT* start, POINT* goals, int nGoals, ...) __thiscall.
// this = ecx = CPathSearch*; stack args start at args[0].
Interceptor.attach(FindPath, {
  onEnter(args) {
    this.cps = this.context.ecx;
    this.start = args[0];
    this.goals = args[1];
    this.nGoals = args[2].toInt32();
    this.bBump = args[7].toInt32();
  },
  onLeave(rv) {
    try {
      const n = this.cps.add(0x14).readS16();         // m_nPathNodes
      const pb = this.cps.add(0x10).readPointer();     // m_pathBegin
      const path = [];
      if (!pb.isNull() && n > 0 && n < 4096) {
        for (let i = 0; i < n; i++) path.push(pb.add(i * 4).readS32());
      }
      const goals = [];
      for (let i = 0; i < this.nGoals && i < 8; i++) goals.push(pt(this.goals.add(i * 8)));
      send({
        tag: 'FINDPATH',
        rc: rv.toInt32(),
        bBump: this.bBump,
        start: pt(this.start),
        goals: goals,
        n: n,
        // decode first/last few nodes for readability; full raw positions too
        head: path.slice(0, 3).map(decode),
        tail: path.slice(-3).map(decode),
        path: path,
      });
    } catch (e) { send({ tag: 'FINDPATH_ERR', err: '' + e }); }
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

    print("[*] hooks live. In-game: select one PC, left-click far open ground.", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
