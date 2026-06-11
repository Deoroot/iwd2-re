#!/usr/bin/env python3
"""Measure the idle head-turn fidget cadence: original IWD2.exe vs our build.

Static RE says the whole chain is binary-faithful (AIUpdate case 6/7 ->
SetIdleSequence -> CGameSprite::SetSequence -> CGameAnimationTypeCharacter::
SetSequence case 6 with the 60/37/3 stand/turn/fidget roll, re-rolled only at
IsEndOfSequence of the 56-frame stand loop). Yet our PCs visibly fidget far
more often. So: measure, don't theorize.

Hooks (function ENTRIES only):
  anim   CGameAnimationTypeCharacter::SetSequence  (orig 0x6CDA00, ours via map)
         - counts every call per nSequence value
         - for nSequence==6 reads m_currentBamSequence (+0x1424) on leave
           -> which of stand(2) / head-turn(7) / fidget(8) the roll picked
  sprite CGameSprite::SetSequence                  (orig 0x707230, ours via map)
         - counts calls with nSequence in {6,7} per sprite id (+0x5C)

Run ON THE VM, in session 1 (vm_s1_payload.cmd), fully unattended:
  python scripts\frida_headturn_probe.py --original --slot 0 --duration 120
  python scripts\frida_headturn_probe.py --ours     --slot 0 --duration 120

Log: JSONL to tmp_headturn_<orig|ours>.log next to the repo root; final
"summary" record carries the rates. The game is killed on exit.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import threading
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_audio_trace import (  # noqa: E402
    ORIGINAL_STARTUP_SKIP_SECONDS,
    original_load_driver,
)
from frida_intro_trace import GAME_DIR, ORIG_EXE, REPO  # noqa: E402

RE_EXE = REPO / "build" / "Debug" / "iwd2-re.exe"  # canonical run path (auto_start_game.py); the GAME_DIR copy can be stale
MAP_FILE = REPO / "build" / "Debug" / "iwd2-re.map"
LINK_IMAGE_BASE = 0x400000

ORIG_HOOKS = {
    "animSetSeq": 0x6CDA00,      # CGameAnimationTypeCharacter::SetSequence
    "spriteSetSeq": 0x707230,    # CGameSprite::SetSequence
    "vidSetResRef": 0x58FC70,    # CVidCell::SetResRef
    "spriteCanSave": 0x75E890,   # CGameSprite::CanSaveGame (virtual)
    "areaCanSave": 0x46F5A0,     # CGameArea::CanSaveGame
    "incFrame": 0x6CC490,        # CGameAnimationTypeCharacter::IncrementFrame
    "spriteChangeDir": 0x6FBAD0, # CGameSprite::ChangeDirection
    "animChangeDir": 0x6C6320,   # CGameAnimationTypeCharacter::ChangeDirection
}
MAP_SYMBOL_SUBSTR = {
    "animSetSeq": "SetSequence@CGameAnimationTypeCharacter@@",
    "spriteSetSeq": "SetSequence@CGameSprite@@",
    "vidSetResRef": "SetResRef@CVidCell@@",
    "spriteCanSave": "CanSaveGame@CGameSprite@@",
    "areaCanSave": "CanSaveGame@CGameArea@@",
    "incFrame": "IncrementFrame@CGameAnimationTypeCharacter@@",
    "spriteChangeDir": "ChangeDirection@CGameSprite@@",
    "animChangeDir": "ChangeDirection@CGameAnimationTypeCharacter@@",
}

BAMSEQ_OFF = 0x1424       # CGameAnimationTypeCharacter::m_currentBamSequence (binary)
BAMSEQ_OFF_OURS = 0x1418  # same field in our build (layout drift; found via memwin scan)
ID_OFF = 0x5C             # CGameObject::m_id

JS = r"""
'use strict';
const CFG = %CFG%;

let animBase, spriteBase, vidResRefBase, spriteCanSaveBase, areaCanSaveBase, incFrameBase,
    spriteChangeDirBase, animChangeDirBase;
if (CFG.ours) {
  const m = Process.findModuleByName('iwd2-re.exe');
  send({ tag: 'mod', base: m.base.toString() });
  const rva = function (v) { return v === null || v === undefined ? null : m.base.add(v); };
  animBase = rva(CFG.animRva);
  spriteBase = rva(CFG.spriteRva);
  vidResRefBase = rva(CFG.vidRva);
  spriteCanSaveBase = rva(CFG.spriteCanSaveRva);
  areaCanSaveBase = rva(CFG.areaCanSaveRva);
  incFrameBase = rva(CFG.incFrameRva);
  spriteChangeDirBase = rva(CFG.spriteChangeDirRva);
  animChangeDirBase = rva(CFG.animChangeDirRva);
} else {
  animBase = ptr(CFG.animVa);
  spriteBase = ptr(CFG.spriteVa);
  vidResRefBase = ptr(CFG.vidVa);
  spriteCanSaveBase = ptr(CFG.spriteCanSaveVa);
  areaCanSaveBase = ptr(CFG.areaCanSaveVa);
  incFrameBase = ptr(CFG.incFrameVa);
  spriteChangeDirBase = ptr(CFG.spriteChangeDirVa);
  animChangeDirBase = ptr(CFG.animChangeDirVa);
}

const seqCounts = {};        // anim-level nSequence -> calls
const chosenCounts = {};     // bam seq picked when nSequence==6
const perAnim6 = {};         // anim this -> {n, lastT, minDt, sumDt}
const spriteIdle = {};       // sprite id -> calls with seq 6|7
const resrefCounts = {};     // CVidCell::SetResRef resref during anim SetSequence(6)
const animResRef = {};       // anim this -> last resref seen during its SetSequence(6)
const incFrameCounts = {};   // anim this -> IncrementFrame calls
const canSaveCounts = { sprite0: 0, sprite1: 0, area0: 0, area1: 0 };
const spriteSeqAll = {};     // sprite id -> {seq: calls} for EVERY sequence value
const spriteDirChanges = {}; // sprite id -> CGameSprite::ChangeDirection rotations applied
const animDirChanges = {};   // anim this -> {bamseq: CGameAnimationTypeCharacter::ChangeDirection calls}
const animSeqTime = {};      // anim this -> {bamseq: 1Hz samples} (% wall time displayed per bamseq)
let ev6 = [];                // sample of seq==6 events
let inAnim6 = 0;
let curAnim6 = null;
let memwins = 0;

function tryAttach(target, cbs, name) {
  try { Interceptor.attach(target, cbs); send({ tag: 'hooked', name: name }); }
  catch (e) { send({ tag: 'hook-failed', name: name, err: '' + e }); }
}

tryAttach(animBase, {
  onEnter(args) {
    const seq = args[0].toInt32() & 0xffff;
    seqCounts[seq] = (seqCounts[seq] || 0) + 1;
    if (seq === 6) { this.is6 = true; this.thiz = this.context.ecx; inAnim6++; curAnim6 = this.context.ecx.toString(); }
  },
  onLeave() {
    if (!this.is6) return;
    inAnim6--;
    let chosen = -1;
    try { chosen = this.thiz.add(CFG.bamSeqOff).readU16(); } catch (e) {}
    if (!CFG.ours) {
      // original: CString m_resRef at anim+0x402 (MFC CString = ptr to chars)
      try {
        const s = this.thiz.add(0x402).readPointer().readUtf8String(8);
        if (s && /^[A-Za-z0-9]/.test(s)) animResRef[this.thiz.toString()] = s;
      } catch (e) {}
    }
    if (CFG.ours && memwins < 20) {
      memwins++;
      const vals = [];
      try {
        for (let off = 0x13c0; off <= 0x14a0; off += 2) vals.push(this.thiz.add(off).readS16());
        send({ tag: 'memwin', anim: this.thiz.toString(), startOff: 0x13c0, vals: vals });
      } catch (e) {}
    }
    chosenCounts[chosen] = (chosenCounts[chosen] || 0) + 1;
    const t = Date.now();
    const k = this.thiz.toString();
    let st = perAnim6[k];
    if (!st) { st = perAnim6[k] = { n: 0, lastT: 0, minDt: 1e9, sumDt: 0, nDt: 0 }; }
    st.n++;
    if (st.lastT) {
      const dt = t - st.lastT;
      if (dt < st.minDt) st.minDt = dt;
      st.sumDt += dt; st.nDt++;
    }
    st.lastT = t;
    if (ev6.length < 400) ev6.push({ t: t, anim: k, chosen: chosen });
  }
}, 'animSetSeq');

if (spriteBase !== null) {
  tryAttach(spriteBase, {
    onEnter(args) {
      const seq = args[0].toInt32() & 0xffff;
      let id = -1;
      try { id = this.context.ecx.add(CFG.idOff).readS32(); } catch (e) {}
      let all = spriteSeqAll[id];
      if (!all) { all = spriteSeqAll[id] = {}; }
      all[seq] = (all[seq] || 0) + 1;
      if (seq !== 6 && seq !== 7) return;
      const k = id + ':' + seq;
      spriteIdle[k] = (spriteIdle[k] || 0) + 1;
    }
  }, 'spriteSetSeq');
}

if (spriteChangeDirBase !== null) {
  tryAttach(spriteChangeDirBase, {
    onEnter() {
      let id = -1;
      try { id = this.context.ecx.add(CFG.idOff).readS32(); } catch (e) {}
      spriteDirChanges[id] = (spriteDirChanges[id] || 0) + 1;
    }
  }, 'spriteChangeDir');
}

if (animChangeDirBase !== null) {
  tryAttach(animChangeDirBase, {
    onEnter() {
      const k = this.context.ecx.toString();
      let bs = -1;
      try { bs = this.context.ecx.add(CFG.bamSeqOff).readS16(); } catch (e) {}
      let d = animDirChanges[k];
      if (!d) { d = animDirChanges[k] = {}; }
      d[bs] = (d[bs] || 0) + 1;
    }
  }, 'animChangeDir');
}

// 1 Hz displayed-sequence sampler over every anim seen by incFrame: how much
// wall time each anim spends showing stand(2) / head-turn(7) / fidget(8) etc.
setInterval(function () {
  for (const k in incFrameCounts) {
    try {
      const bs = ptr(k).add(CFG.bamSeqOff).readS16();
      let tmap = animSeqTime[k];
      if (!tmap) { tmap = animSeqTime[k] = {}; }
      tmap[bs] = (tmap[bs] || 0) + 1;
    } catch (e) {}
  }
}, 1000);

if (vidResRefBase !== null) {
  tryAttach(vidResRefBase, {
    onEnter(args) {
      if (inAnim6 <= 0) return;
      let s = '';
      try { s = args[0].readUtf8String(8); } catch (e) { s = '<err>'; }
      resrefCounts[s] = (resrefCounts[s] || 0) + 1;
      if (curAnim6 !== null) animResRef[curAnim6] = s;
    }
  }, 'vidSetResRef');
}

if (incFrameBase !== null) {
  tryAttach(incFrameBase, {
    onEnter() {
      const k = this.context.ecx.toString();
      incFrameCounts[k] = (incFrameCounts[k] || 0) + 1;
    }
  }, 'incFrame');
}

if (spriteCanSaveBase !== null) {
  tryAttach(spriteCanSaveBase, {
    onLeave(retval) {
      const r = retval.toInt32() & 0xff;
      if (r === 0) canSaveCounts.sprite0++; else canSaveCounts.sprite1++;
    }
  }, 'spriteCanSave');
}

if (areaCanSaveBase !== null) {
  tryAttach(areaCanSaveBase, {
    onLeave(retval) {
      const r = retval.toInt32() & 0xff;
      if (r === 0) canSaveCounts.area0++; else canSaveCounts.area1++;
    }
  }, 'areaCanSave');
}

setInterval(function () {
  send({ tag: 'snapshot', t: Date.now(), seqCounts: seqCounts,
         chosenCounts: chosenCounts, perAnim6: perAnim6,
         spriteIdle: spriteIdle, resrefCounts: resrefCounts,
         animResRef: animResRef, incFrameCounts: incFrameCounts,
         canSaveCounts: canSaveCounts, spriteSeqAll: spriteSeqAll,
         spriteDirChanges: spriteDirChanges, animDirChanges: animDirChanges,
         animSeqTime: animSeqTime, ev6: ev6 });
  ev6 = [];
}, 5000);

rpc.exports = {
  // original only: g_pBaldurChitin(0x8CF6DC) -> +0x1C88 m_pEngineWorld
  // (CScreenWorld) -> +0x13E m_bPaused
  readpause: function () {
    if (CFG.ours) return -1;
    try {
      const chitin = ptr(0x8CF6DC).readPointer();
      if (chitin.isNull()) return -2;
      const world = chitin.add(0x1C88).readPointer();
      if (world.isNull()) return -3;
      return world.add(0x13E).readU8();
    } catch (e) { return -4; }
  }
};

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
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--original", action="store_true", default=True)
    g.add_argument("--ours", dest="ours", action="store_true")
    ap.add_argument("--slot", type=int, default=0)
    ap.add_argument("--duration", type=float, default=120.0)
    ap.add_argument("--warmup", type=float, default=30.0,
                    help="seconds after the load driver finishes before the measured window starts")
    ap.add_argument("--unpause", action="store_true",
                    help="send SPACE after load (combat saves load paused) + screenshot")
    args = ap.parse_args()
    ours = bool(getattr(args, "ours", False))

    out = REPO / ("tmp_headturn_ours.log" if ours else "tmp_headturn_orig.log")
    out.write_text("", encoding="utf-8")

    def emit(payload: dict) -> None:
        with out.open("a", encoding="utf-8") as f:
            f.write(json.dumps(payload) + "\n")

    cfg: dict[str, object] = {
        "bamSeqOff": BAMSEQ_OFF_OURS if ours else BAMSEQ_OFF,
        "idOff": ID_OFF,
        "ours": ours,
    }
    if ours:
        anim_rva = map_lookup(MAP_SYMBOL_SUBSTR["animSetSeq"])
        if anim_rva is None:
            raise SystemExit(f"animSetSeq symbol not in {MAP_FILE}")
        cfg["animRva"] = anim_rva
        cfg["spriteRva"] = map_lookup(MAP_SYMBOL_SUBSTR["spriteSetSeq"])
        cfg["vidRva"] = map_lookup(MAP_SYMBOL_SUBSTR["vidSetResRef"])
        cfg["spriteCanSaveRva"] = map_lookup(MAP_SYMBOL_SUBSTR["spriteCanSave"])
        cfg["areaCanSaveRva"] = map_lookup(MAP_SYMBOL_SUBSTR["areaCanSave"])
        cfg["incFrameRva"] = map_lookup(MAP_SYMBOL_SUBSTR["incFrame"])
        cfg["spriteChangeDirRva"] = map_lookup(MAP_SYMBOL_SUBSTR["spriteChangeDir"])
        cfg["animChangeDirRva"] = map_lookup(MAP_SYMBOL_SUBSTR["animChangeDir"])
        exe = RE_EXE
    else:
        cfg["animVa"] = ORIG_HOOKS["animSetSeq"]
        cfg["spriteVa"] = ORIG_HOOKS["spriteSetSeq"]
        cfg["vidVa"] = ORIG_HOOKS["vidSetResRef"]
        cfg["spriteCanSaveVa"] = ORIG_HOOKS["spriteCanSave"]
        cfg["areaCanSaveVa"] = ORIG_HOOKS["areaCanSave"]
        cfg["incFrameVa"] = ORIG_HOOKS["incFrame"]
        cfg["spriteChangeDirVa"] = ORIG_HOOKS["spriteChangeDir"]
        cfg["animChangeDirVa"] = ORIG_HOOKS["animChangeDir"]
        exe = ORIG_EXE

    if ours:
        # Spawning our build under Frida black-screens it; launch it the proven
        # way (plain subprocess + IWD2_RE_AUTO_* auto-load) and attach afterwards.
        import subprocess
        import tempfile
        import uuid

        result_path = Path(tempfile.gettempdir()) / f"iwd2-re-auto-{uuid.uuid4().hex}.txt"
        env = dict(os.environ)
        env["IWD2_RE_AUTO_ACTION"] = "load"
        env["IWD2_RE_AUTO_SLOT"] = str(args.slot)
        env["IWD2_RE_AUTO_RESULT"] = str(result_path)
        emit({"tag": "Driver.launch", "exe": str(exe), "ours": True, "cfg": cfg})
        proc = subprocess.Popen([str(exe)], cwd=str(GAME_DIR), env=env)
        pid = proc.pid
        deadline = time.time() + 120.0
        loaded = False
        while time.time() < deadline:
            if result_path.exists():
                emit({"tag": "Driver.ours.loaded", "result": result_path.read_text(errors="replace")})
                loaded = True
                break
            if proc.poll() is not None:
                emit({"tag": "Driver.ours.exited", "code": proc.returncode})
                return 1
            time.sleep(0.5)
        if not loaded:
            emit({"tag": "Driver.ours.load-timeout"})
        session = frida.attach(pid)
    else:
        emit({"tag": "Driver.spawn", "exe": str(exe), "ours": ours, "cfg": cfg})
        pid = frida.spawn(str(exe), cwd=str(GAME_DIR), env=dict(os.environ))
        session = frida.attach(pid)

    snapshots: list[dict] = []

    def on_message(message, _data):
        if message["type"] != "send":
            emit({"tag": "frida-error", "message": str(message)})
            return
        p = message["payload"]
        emit(p)
        if p.get("tag") == "snapshot":
            snapshots.append(p)

    js = JS.replace("%CFG%", json.dumps(cfg))
    script = session.create_script(js)
    script.on("message", on_message)
    script.load()

    if not ours:
        frida.resume(pid)
        drv = threading.Thread(
            target=original_load_driver,
            args=(pid, args.slot, ORIGINAL_STARTUP_SKIP_SECONDS, emit),
            daemon=True,
        )
        drv.start()
        drv.join(timeout=240.0)

    # Watchdog. Two distinct blockers kept killing measurement windows:
    #   1. real game pause (m_bPaused, toggled by SPACE and by the engine on
    #      window activate/deactivate — see CScreenWorld::EngineActivated);
    #   2. engine deactivation: the original suspends AI+render entirely when
    #      its window is not the active one; only a real (physical) click on
    #      the window brings the engine back, SetForegroundWindow is not enough.
    # So: read m_bPaused via RPC (no more blind SPACE toggles); if paused,
    # SPACE to unpause; if unpaused but the hook counters are frozen, do the
    # physical title-bar activation click — exactly what unblocked the user's
    # manual runs.
    import ctypes
    import ctypes.wintypes

    from frida_intro_trace import (VK_SPACE, capture_game_surface,
                                   find_window_for_pid, physical_mouse_click,
                                   send_key_to_pid, user32)

    def title_bar_click(hwnd: int) -> bool:
        # What the user does manually to revive a deactivated engine: a real
        # click on the TITLE BAR (never the client area — that moves the party).
        rect = ctypes.wintypes.RECT()
        if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
            return False
        physical_mouse_click(hwnd, rect.left + 200, rect.top + 12)
        return True

    def read_pause() -> int:
        try:
            return int(script.exports_sync.readpause())
        except Exception:
            try:
                return int(script.exports.readpause())
            except Exception:
                return -9

    def total_calls() -> int:
        if not snapshots:
            return 0
        return sum(snapshots[-1].get("seqCounts", {}).values())

    stop_focus = threading.Event()

    def watchdog() -> None:
        last_calls = total_calls()
        frozen_since = None
        while not stop_focus.is_set():
            stop_focus.wait(5.0)
            if stop_focus.is_set():
                break
            try:
                paused = read_pause()
                calls = total_calls()
                advancing = calls > last_calls
                last_calls = calls
                if paused == 1:
                    sent = send_key_to_pid(pid, VK_SPACE, activation_click=True)
                    emit({"tag": "Driver.watchdog", "action": "unpause-space",
                          "paused": paused, "calls": calls, "sent": bool(sent)})
                    frozen_since = None
                elif not advancing:
                    if frozen_since is None:
                        frozen_since = time.time()
                    elif time.time() - frozen_since >= 10.0:
                        hwnd = find_window_for_pid(pid)
                        ok = title_bar_click(hwnd) if hwnd else False
                        emit({"tag": "Driver.watchdog", "action": "title-click",
                              "paused": paused, "calls": calls, "ok": bool(ok)})
                        frozen_since = None
                else:
                    frozen_since = None
            except Exception as e:
                emit({"tag": "Driver.watchdog-error", "err": str(e)})

    threading.Thread(target=watchdog, daemon=True).start()

    try:
        path, capture = capture_game_surface(pid, "headturn_postload")
        emit({"tag": "Driver.screenshot", "path": str(path), "capture": capture})
    except Exception as e:
        emit({"tag": "Driver.screenshot-error", "err": str(e)})

    emit({"tag": "Driver.warmup", "seconds": args.warmup})
    time.sleep(args.warmup)

    mark = time.time()
    base = None
    for s in reversed(snapshots):
        base = s
        break
    emit({"tag": "Driver.window.start", "baseSnapshot": bool(base)})
    time.sleep(args.duration)

    final = snapshots[-1] if snapshots else None

    def diff_counts(key: str) -> dict:
        a = (base or {}).get(key, {}) if base else {}
        b = (final or {}).get(key, {}) if final else {}
        return {k: b.get(k, 0) - a.get(k, 0) for k in b}

    def diff_nested(key: str) -> dict:
        a = (base or {}).get(key, {}) if base else {}
        b = (final or {}).get(key, {}) if final else {}
        out = {}
        for k, sub in b.items():
            asub = a.get(k, {})
            out[k] = {sk: sv - asub.get(sk, 0) for sk, sv in sub.items()}
        return out

    summary = {
        "tag": "summary",
        "ours": ours,
        "windowSeconds": args.duration,
        "animSeqCallsDelta": diff_counts("seqCounts"),
        "chosenDelta": diff_counts("chosenCounts"),
        "spriteIdleDelta": diff_counts("spriteIdle"),
        "resrefDelta": diff_counts("resrefCounts"),
        "incFrameDelta": diff_counts("incFrameCounts"),
        "canSaveDelta": diff_counts("canSaveCounts"),
        "spriteSeqAllDelta": diff_nested("spriteSeqAll"),
        "spriteDirChangesDelta": diff_counts("spriteDirChanges"),
        "animDirChangesDelta": diff_nested("animDirChanges"),
        "animSeqTimeDelta": diff_nested("animSeqTime"),
        "animResRef": (final or {}).get("animResRef", {}),
        "perAnim6Final": (final or {}).get("perAnim6", {}),
    }
    stop_focus.set()
    emit(summary)
    print(json.dumps(summary, indent=2))

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
    raise SystemExit(main())
