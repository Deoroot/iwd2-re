#!/usr/bin/env python3
"""Why does no creature ever walk? Trace the script->action chain in OUR build.

Hooks (our build, RVAs from the linker map):
  CAIScript::Find                       calls + non-NULL responses
  CGameSprite::EvaluateStatusTrigger    trigger ids seen (is 0x4097 TimerActive evaluated?)
  CGameAIBase::ExecuteAction            m_curAction id histogram is NOT readable portably;
                                        instead hook CGameSprite::RandomWalk + StartRandomTimer path
  CGameSprite::RandomWalk               the recovered handler -- does it ever run?

Run on the VM in session 1:
  python scripts\frida_randomwalk_trace.py --slot 0 --duration 90
Log: C:\iwd2-re\tmp_randomwalk_trace.log
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_intro_trace import GAME_DIR, REPO  # noqa: E402

RE_EXE = REPO / "build" / "Debug" / "iwd2-re.exe"
MAP_FILE = REPO / "build" / "Debug" / "iwd2-re.map"
LINK_IMAGE_BASE = 0x400000

SYMBOLS = {
    "scriptFind": "?Find@CAIScript@@",
    "evalStatusTrigger": "?EvaluateStatusTrigger@CGameSprite@@",
    "evalStatusTriggerBase": "?EvaluateStatusTrigger@CGameAIBase@@",
    "randomWalk": "?RandomWalk@CGameSprite@@",
    "execActionSprite": "?ExecuteAction@CGameSprite@@",
    "execActionBase": "?ExecuteAction@CGameAIBase@@",
    "checkTimers": "?CheckTimers@CGameAIBase@@",
    "addAction": "?AddAction@CGameAIBase@@",
    "insertResponse": "?InsertResponse@CGameAIBase@@",
    "setCurrAction": "?SetCurrAction@CGameAIBase@@",
    "msgInsertActionRun": "?Run@CMessageInsertAction@@",
    "insertAction": "?InsertAction@CGameAIBase@@",
    "getNextAction": "?GetNextAction@CGameAIBase@@",
}

JS = r"""
'use strict';
const CFG = %CFG%;
const m = Process.findModuleByName('iwd2-re.exe');
const counts = {};
const trigIds = {};

function bump(k) { counts[k] = (counts[k] || 0) + 1; }

function tryAttach(name, rva, cbs) {
  if (rva === null || rva === undefined) { send({ tag: 'hook-skip', name: name }); return; }
  try { Interceptor.attach(m.base.add(rva), cbs); send({ tag: 'hooked', name: name }); }
  catch (e) { send({ tag: 'hook-failed', name: name, err: '' + e }); }
}

tryAttach('scriptFind', CFG.scriptFind, {
  onEnter() { bump('find'); },
  onLeave(rv) { if (!rv.isNull()) bump('findHit'); }
});

// __thiscall BOOL EvaluateStatusTrigger(const CAITrigger&): args[0] = &trigger,
// CAITrigger::m_triggerID is the first dword.
tryAttach('evalStatusTrigger', CFG.evalStatusTrigger, {
  onEnter(args) {
    bump('evalStatus');
    try {
      const id = args[0].readU32();
      const k = '0x' + id.toString(16);
      trigIds[k] = (trigIds[k] || 0) + 1;
    } catch (e) {}
  }
});
tryAttach('evalStatusTriggerBase', CFG.evalStatusTriggerBase, {
  onEnter() { bump('evalStatusBase'); }
});

tryAttach('randomWalk', CFG.randomWalk, { onEnter() { bump('randomWalk'); } });
tryAttach('execActionSprite', CFG.execActionSprite, { onEnter() { bump('execSprite'); } });
tryAttach('execActionBase', CFG.execActionBase, { onEnter() { bump('execBase'); } });
tryAttach('checkTimers', CFG.checkTimers, { onEnter() { bump('checkTimers'); } });
tryAttach('insertResponse', CFG.insertResponse, { onEnter() { bump('insertResponse'); } });

// AddAction(const CAIAction&) / SetCurrAction(const CAIAction&):
// m_actionID is the first SHORT of CAIAction (no vtable).
const addActionIds = {};
tryAttach('addAction', CFG.addAction, {
  onEnter(args) {
    bump('addAction');
    try {
      const id = args[0].readS16();
      addActionIds[id] = (addActionIds[id] || 0) + 1;
    } catch (e) {}
  }
});
const currActionIds = {};
tryAttach('setCurrAction', CFG.setCurrAction, {
  onEnter(args) {
    bump('setCurrAction');
    try {
      const id = args[0].readS16();
      currActionIds[id] = (currActionIds[id] || 0) + 1;
    } catch (e) {}
  }
});

tryAttach('msgInsertActionRun', CFG.msgInsertActionRun, { onEnter() { bump('msgInsertActionRun'); } });

const insertActionIds = {};
tryAttach('insertAction', CFG.insertAction, {
  onEnter(args) {
    bump('insertAction');
    try {
      const id = args[0].readS16();
      insertActionIds[id] = (insertActionIds[id] || 0) + 1;
    } catch (e) {}
  }
});

// CAIAction& GetNextAction(CAIAction& out): read the returned action's id.
const nextActionIds = {};
tryAttach('getNextAction', CFG.getNextAction, {
  onLeave(rv) {
    bump('getNextAction');
    try {
      const id = rv.readS16();
      nextActionIds[id] = (nextActionIds[id] || 0) + 1;
    } catch (e) {}
  }
});

setInterval(function () {
  send({ tag: 'snapshot', t: Date.now(), counts: counts, trigIds: trigIds,
         addActionIds: addActionIds, currActionIds: currActionIds,
         insertActionIds: insertActionIds, nextActionIds: nextActionIds });
}, 5000);

send({ tag: 'ready' });
"""


def map_lookup(substr: str) -> int | None:
    for line in MAP_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3 or substr not in parts[1] or not parts[1].startswith("?"):
            continue
        if re.fullmatch(r"[0-9A-Fa-f]{8}", parts[2]):
            return int(parts[2], 16) - LINK_IMAGE_BASE
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--slot", type=int, default=0)
    ap.add_argument("--duration", type=float, default=90.0)
    args = ap.parse_args()

    out = REPO / "tmp_randomwalk_trace.log"
    out.write_text("", encoding="utf-8")

    def emit(payload: dict) -> None:
        with out.open("a", encoding="utf-8") as f:
            f.write(json.dumps(payload) + "\n")

    cfg = {name: map_lookup(sub) for name, sub in SYMBOLS.items()}
    emit({"tag": "cfg", "rvas": {k: (hex(v) if v is not None else None) for k, v in cfg.items()}})

    result_path = Path(tempfile.gettempdir()) / f"iwd2-re-auto-{uuid.uuid4().hex}.txt"
    env = dict(os.environ)
    env["IWD2_RE_AUTO_ACTION"] = "load"
    env["IWD2_RE_AUTO_SLOT"] = str(args.slot)
    env["IWD2_RE_AUTO_RESULT"] = str(result_path)
    proc = subprocess.Popen([str(RE_EXE)], cwd=str(GAME_DIR), env=env)
    pid = proc.pid
    emit({"tag": "launched", "pid": pid})

    deadline = time.time() + 120.0
    while time.time() < deadline:
        if result_path.exists():
            emit({"tag": "loaded", "result": result_path.read_text(errors="replace")})
            break
        if proc.poll() is not None:
            emit({"tag": "exited", "code": proc.returncode})
            return 1
        time.sleep(0.5)

    session = frida.attach(pid)
    snapshots: list[dict] = []

    def on_message(message, _data) -> None:
        if message.get("type") == "send":
            payload = message["payload"]
            if payload.get("tag") == "snapshot":
                snapshots.append(payload)
            emit(payload)

    script = session.create_script(JS.replace("%CFG%", json.dumps(cfg)))
    script.on("message", on_message)
    script.load()

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
