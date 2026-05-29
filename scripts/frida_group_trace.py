#!/usr/bin/env python3
"""Symmetric full-group differential trace: ORIGINAL IWD2.exe vs OUR iwd2-re.exe.

§D reopened. The earlier "faithful" verdict came from a degenerate move (one PC,
left-click, open ground, no bump/collision/formation/orientation/marker/failure).
This rig captures the FULL party under the formation pipeline for the SAME gesture
on BOTH binaries, so the two logs diff per party-slot (scripts/frida_diff_group.py).

Controlled test (see plan je-souhaite-corriger-le-vectorized-wigderson.md):
  - usual dock save (group order + formation + start positions already byte-identical)
  - USER gesture (no mouse hijack): right-click PRESS, drag WEST, RELEASE
      press -> OnActionButtonDown -> GroupDrawMove
      drag  -> OnMouseMove        -> GroupDrawMove (orientation preview)
      release-> OnActionButtonUp  -> GroupProtectPoint (commit)

Addressing is uniform: every function is given as an RVA (= addr - 0x400000) and
resolved at runtime as moduleBase + RVA.
  - --target original: RVA = GhidraAbsolute - 0x400000; module IWD2.exe (no ASLR,
    base 0x400000, so it resolves back to the Ghidra address).
  - --target ours:     RVA = (mapRvaBase - 0x400000) parsed from build/Debug/iwd2-re.map
    by mangled symbol; module iwd2-re.exe (ASLR base + RVA).

Usage:
  python scripts/frida_group_trace.py --target original   # spawn IWD2.exe
  python scripts/frida_group_trace.py --target ours        # spawn iwd2-re.exe
  add --attach to attach to an already-running instance instead of spawning.

Logs: tmp_frida_group_<target>.log (repo root).
"""
import frida
import sys
import os
import re
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(REPO, "build", "Debug", "iwd2-re.map")
IMAGE_BASE = 0x400000

# ---- ORIGINAL IWD2.exe: Ghidra absolute addresses (verified 2026-05-29) --------
ORIG_ABS = {
    "GroupSetTarget":     0x4063e0,
    "GroupProtectPoint":  0x407280,
    "GroupDrawMove":      0x407fc0,
    "RotateOffsets":      0x4058e0,   # FUN_004058e0 (unnamed free fn)
    "AdjustTarget":       0x46a3d0,
    "FindPath":           0x51e150,
    "AIUpdateWalk":       0x6f9040,
    "SetTargetReq":       0x707d40,   # CGameSprite::SetTarget(CSearchRequest*,int,BYTE)
    "JumpToPoint":        0x745950,
    "ClearBumpPath":      0x6fa900,
    "Face":               0x7462d0,
    "ResolveTargetPoint": 0x72b870,
    "GetDeny":            0x599c70,
    "GetCloseObjects":    0x46cd20,   # FUN_0046cd20 (CGameArea::GetCloseObjects, unnamed in Ghidra)
}

# ---- OUR iwd2-re.exe: MSVC-decorated names to look up in the fresh .map ---------
OURS_MANGLED = {
    "GroupSetTarget":     "?GroupSetTarget@CAIGroup@@QAEXVCPoint@@HF0@Z",
    "GroupProtectPoint":  "?GroupProtectPoint@CAIGroup@@QAEXVCPoint@@F0J@Z",
    "GroupDrawMove":      "?GroupDrawMove@CAIGroup@@QAEXVCPoint@@F0@Z",
    # RotateOffsets is a free/static fn; may be absent from the .map -> optional.
    "AdjustTarget":       "?AdjustTarget@CGameArea@@QAEHVCPoint@@PAUtagPOINT@@EF@Z",
    "FindPath":           "?FindPath@CPathSearch@@QAEHPAUtagPOINT@@0HJJPAVCSearchBitmap@@PAEHPAVCRect@@@Z",
    "AIUpdateWalk":       "?AIUpdateWalk@CGameSprite@@QAEXXZ",
    "SetTargetReq":       "?SetTarget@CGameSprite@@QAEXPAVCSearchRequest@@HE@Z",
    "JumpToPoint":        "?JumpToPoint@CGameSprite@@QAEFVCPoint@@H@Z",
    "ClearBumpPath":      "?ClearBumpPath@CGameSprite@@QAEHABVCPoint@@0@Z",
    "Face":               "?Face@CGameSprite@@QAEFXZ",
    "ResolveTargetPoint": "?ResolveTargetPoint@CGameSprite@@QAEXPBVCAIAction@@PAU__POSITION@@@Z",
    "GetDeny":            "?GetDeny@CGameObjectArray@@QAEEJEPAPAVCGameObject@@K@Z",
    "GetCloseObjects":    "?GetCloseObjects@CGameArea@@QAEXPAU__POSITION@@ABVCPoint@@ABVCAIObjectType@@FPBEAAV?$CTypedPtrList@VCPtrList@@PAJ@@HH@Z",
}

# Struct field offsets differ between the two binaries: IWD2.exe is tightly
# packed; our rebuild only packs files with an explicit #pragma pack (CPathSearch,
# BalDataTypes), so CAIGroup/CGameObject use MSVC default alignment and their
# members shift. Hand-derived from the headers (verified in-session via CALIB_*):
#   CAIGroup: m_memberList@0x08 (default-aligned), MFC CPtrList m_pNodeHead@+0x04.
#   CGameObject: vptr@0, m_objectType@0x04(1B), CPoint m_pos 4-aligns to 0x08.
#   CPathSearch is #pragma pack(2) -> m_pathBegin@0x10 / m_nPathNodes@0x14 (both).
#   MFC CNode (next@0, prev@4, data@8) is unchanged.
# CSearchRequest is also unpacked in our build: m_serviceState@0, m_collisionDelay@1,
# then BOOL m_collisionSearch 4-aligns to 0x04 (binary 0x02), shifting the byte
# counters: m_nPartyIds/m_nTargetIds/m_nTargetPoints land at 0x09/0x0a/0x0b (binary
# 0x07/0x08/0x09). The request-field dump below identifies which builder sets
# nTargetIds = party (the 7-goal search the binary issues and ours does not).
OFFSETS = {
    "original": {"listHead": 0x0a, "mPos": 0x06,
                 "reqCollision": 0x02, "reqNParty": 0x07, "reqNTargetIds": 0x08, "reqNPoints": 0x09,
                 "reqRemoveSelf": 0x0a, "reqPathSmooth": 0x24, "reqSourceId": 0x2c,
                 "reqSourcePt": 0x30, "reqPartyIds": 0x38, "reqTargetPoints": 0x40, "reqBump": 0x54},
    "ours":     {"listHead": 0x0c, "mPos": 0x08,
                 "reqCollision": 0x04, "reqNParty": 0x09, "reqNTargetIds": 0x0a, "reqNPoints": 0x0b,
                 "reqRemoveSelf": 0x0c, "reqPathSmooth": 0x28, "reqSourceId": 0x30,
                 "reqSourcePt": 0x34, "reqPartyIds": 0x3c, "reqTargetPoints": 0x44, "reqBump": 0x58},
}

# .map line: " 0001:00090140       ?Sym@... 00491140 f   Obj.obj"
MAP_LINE = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+f\b")


def parse_map(path):
    """mangled symbol -> RvaBase (int). Public function symbols only ('f' class)."""
    table = {}
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = MAP_LINE.match(line)
            if m:
                table.setdefault(m.group(1), int(m.group(2), 16))
    return table


def build_rvas(target):
    """Return (module_basename, {fnName: rva}). Missing optional fns are omitted."""
    if target == "original":
        return "IWD2.exe", {k: v - IMAGE_BASE for k, v in ORIG_ABS.items()}
    if not os.path.exists(MAP):
        sys.exit(f"[!] {MAP} missing — build with /MAP first (cmake --build build --config Debug).")
    syms = parse_map(MAP)
    rvas = {}
    for name, mangled in OURS_MANGLED.items():
        if mangled in syms:
            rvas[name] = syms[mangled] - IMAGE_BASE
        elif name in ("RotateOffsets",):
            pass  # optional
        else:
            print(f"[!] symbol not in .map: {name} ({mangled})", flush=True)
    return "iwd2-re.exe", rvas


JS_TEMPLATE = r"""
'use strict';
const CFG = __CFG__;
const STRIDE = 320;                 // GRID_ACTUALX (PointToPosition stride)
const M_POS  = CFG.off.mPos;        // CGameObject::m_pos (orig 0x06, ours 0x08)
const LIST_HEAD = CFG.off.listHead; // CAIGroup CPtrList m_pNodeHead (orig 0x0a, ours 0x0c)
const NODE_DATA = 0x08;             // MFC CNode: next@0, prev@4, data@8 (same both)

function mainBase(name) {
  const mods = Process.enumerateModules();
  for (let i = 0; i < mods.length; i++) {
    if (mods[i].name.toLowerCase() === name.toLowerCase()) return mods[i].base;
  }
  return mods[0].base;   // fallback: main executable is the first module
}
let base = mainBase(CFG.module);

function A(name) {                  // resolve fn address, or null if not configured
  const rva = CFG.rvas[name];
  return (rva === undefined) ? null : base.add(rva);
}
function s16(v) { return (v.toInt32() << 16) >> 16; }
function pt(p) { return [p.readS32(), p.add(4).readS32()]; }
function spritePos(s) { return [s.add(M_POS).readS32(), s.add(M_POS + 4).readS32()]; }
function decode(pos) { return [pos % STRIDE, (pos / STRIDE) | 0]; }

let SEQ = 0;
function emit(o) { o.seq = SEQ++; send(o); }

function reqInfo(req) {
  const o = {
    collisionSearch: req.add(CFG.off.reqCollision).readS32(),
    nPartyIds: req.add(CFG.off.reqNParty).readU8(),
    nTargetIds: req.add(CFG.off.reqNTargetIds).readU8(),
    nTargetPoints: req.add(CFG.off.reqNPoints).readU8(),
    removeSelf: req.add(CFG.off.reqRemoveSelf).readS32(),
    pathSmooth: req.add(CFG.off.reqPathSmooth).readS32(),
    sourceId: req.add(CFG.off.reqSourceId).readS32(),
    sourcePt: pt(req.add(CFG.off.reqSourcePt)),
    bBump: req.add(CFG.off.reqBump).readS32(),
  };
  try {
    const tp = req.add(CFG.off.reqTargetPoints).readPointer();
    if (!tp.isNull() && o.nTargetPoints > 0) o.target0 = pt(tp);
  } catch (e) { o.targetErr = "" + e; }
  try {
    const pp = req.add(CFG.off.reqPartyIds).readPointer();
    o.partyIds = [];
    if (!pp.isNull()) {
      for (let i = 0; i < o.nPartyIds && i < 8; i++) o.partyIds.push(pp.add(i * 4).readS32());
    }
  } catch (e) { o.partyErr = "" + e; }
  return o;
}

// One-shot raw dumps so the offsets above can be confirmed in-session.
let calibGroup = false, calibSprite = false;
function hbytes(p, n) {
  const b = [];
  for (let i = 0; i < n; i++) b.push(("0" + p.add(i).readU8().toString(16)).slice(-2));
  return b.join("");
}

// Member object-id -> slot ordinal, rebuilt at each commit (GST/GPP/GDM entry).
let idToSlot = {};
// Sprite-ptr string -> slot ordinal, persistent (party sprites are stable).
const slotOf = {};
// Sprite-ptr string -> last reported [x,y], to dedupe stationary AIUpdateWalk ticks.
const lastPos = {};

function walkMembers(group) {
  const ids = [];
  try {
    let node = group.add(LIST_HEAD).readPointer();
    let guard = 0;
    while (!node.isNull() && guard < 32) {
      ids.push(node.add(NODE_DATA).readU32());
      node = node.readPointer();   // next @ +0
      guard++;
    }
  } catch (e) { /* ignore */ }
  return ids;
}

function onCommit(fn, group, ids) {
  idToSlot = {};
  ids.forEach((id, i) => { idToSlot[id] = i; });
}

function slotForSprite(ecx) {
  const k = ecx.toString();
  return (k in slotOf) ? slotOf[k] : -1;
}

// ---- Commit layer (formation + orientation) ---------------------------------
function hookCommit(name, fn, layout) {
  const a = A(name);
  if (a === null) return;
  Interceptor.attach(a, {
    onEnter(args) {
      const group = this.context.ecx;
      if (!calibGroup) {
        calibGroup = true;
        emit({ tag: "CALIB_GROUP", bytes: hbytes(group, 0x20), listHead: LIST_HEAD,
               headPtr: group.add(LIST_HEAD).readPointer().toString() });
      }
      const ids = walkMembers(group);
      onCommit(name, group, ids);
      const o = { tag: fn, members: ids };
      o.target = [args[0].toInt32(), args[1].toInt32()];
      if (layout === "GST") {        // (CPoint, BOOL additive, SHORT fType, CPoint cursor)
        o.additive = args[2].toInt32();
        o.formationType = s16(args[3]);
        o.cursor = [args[4].toInt32(), args[5].toInt32()];
      } else if (layout === "GPP") { // (CPoint, SHORT fType, CPoint cursor, LONG additive)
        o.formationType = s16(args[2]);
        o.cursor = [args[3].toInt32(), args[4].toInt32()];
        o.additive = args[5].toInt32();
      } else {                       // GDM (CPoint, SHORT fType, CPoint cursor)
        o.formationType = s16(args[2]);
        o.cursor = [args[3].toInt32(), args[4].toInt32()];
      }
      emit(o);
    }
  });
}

// ---- GetDeny: resolve member id -> sprite-ptr -> slot ------------------------
(function () {
  const a = A("GetDeny");
  if (a === null) return;
  // CGameObjectArray::GetDeny(LONG id, BYTE thread, CGameObject** out, DWORD timeout)
  Interceptor.attach(a, {
    onEnter(args) {
      this.id = args[0].toInt32() >>> 0;
      this.outpp = args[2];
      this.known = (this.id in idToSlot);
    },
    onLeave(retval) {
      if (!this.known) return;
      if ((retval.toInt32() & 0xff) !== 0) return;   // SUCCESS == 0
      try {
        const sprite = this.outpp.readPointer();
        if (sprite.isNull()) return;
        if (!calibSprite) {
          calibSprite = true;
          emit({ tag: "CALIB_SPRITE", bytes: hbytes(sprite, 0x18),
                 mPos: M_POS, pos: spritePos(sprite) });
        }
        const slot = idToSlot[this.id];
        slotOf[sprite.toString()] = slot;
        emit({ tag: "MEMBER", slot: slot, id: this.id,
               sprite: sprite.toString(), pos: spritePos(sprite) });
      } catch (e) { /* ignore */ }
    }
  });
})();

// ---- AdjustTarget (passability / collision placement) -----------------------
(function () {
  const a = A("AdjustTarget");
  if (a === null) return;
  // CGameArea::AdjustTarget(CPoint start, POINT* goal, BYTE personalSpace, SHORT tol) __thiscall
  Interceptor.attach(a, {
    onEnter(args) {
      this.start = [args[0].toInt32(), args[1].toInt32()];
      this.goalp = args[2];
      this.goalIn = pt(this.goalp);
      this.ps = args[3].toInt32() & 0xff;
      this.tol = s16(args[4]);
      this.ra = this.returnAddress.sub(base).toString();   // RVA of caller site
    },
    onLeave(retval) {
      emit({ tag: "ADJUST", start: this.start, goalIn: this.goalIn,
             goalOut: pt(this.goalp), personalSpace: this.ps, tol: this.tol,
             passable: retval.toInt32() & 0xff, raRva: this.ra });
    }
  });
})();

// ---- FindPath (A* search; ecx = CPathSearch, attributed to slot by start) ----
(function () {
  const a = A("FindPath");
  if (a === null) return;
  Interceptor.attach(a, {
    onEnter(args) {
      this.cps = this.context.ecx;
      this.start = args[0];
      this.goals = args[1];
      this.nGoals = args[2].toInt32();
      this.minNodes = args[3].toInt32();   // minNodeLimit
      this.maxNodes = args[4].toInt32();   // maxNodeLimit
      this.bBump = args[7].toInt32();
      this.req = args[6];   // serviceState == &request->m_serviceState == request base
    },
    onLeave(rv) {
      try {
        const n = this.cps.add(0x14).readS16();          // m_nPathNodes
        const pb = this.cps.add(0x10).readPointer();      // m_pathBegin (LONG*)
        const path = [];
        if (!pb.isNull() && n > 0 && n < 4096) {
          for (let i = 0; i < n; i++) path.push(pb.add(i * 4).readS32());
        }
        const goals = [];
        for (let i = 0; i < this.nGoals && i < 8; i++) goals.push(pt(this.goals.add(i * 8)));
        let reqBytes = "";
        let req = null;
        try { reqBytes = hbytes(this.req, 0x10); } catch (e) {}
        try { req = reqInfo(this.req); } catch (e) {}
        emit({ tag: "FINDPATH", rc: rv.toInt32(), bBump: this.bBump,
               nGoalsRaw: this.nGoals, minNodes: this.minNodes, maxNodes: this.maxNodes,
               start: pt(this.start), goals: goals, n: n,
               head: path.slice(0, 3).map(decode), tail: path.slice(-3).map(decode),
               path: path, reqBytes: reqBytes, req: req });
      } catch (e) { emit({ tag: "FINDPATH_ERR", err: "" + e }); }
    }
  });
})();

// ---- Per-member execution hooks (ecx = CGameSprite; skip non-party) ---------
function hookSprite(name, tag, onE, onL) {
  const a = A(name);
  if (a === null) return;
  Interceptor.attach(a, {
    onEnter(args) {
      this.slot = slotForSprite(this.context.ecx);
      if (this.slot < 0) return;
      this.ecx = this.context.ecx;
      if (onE) onE.call(this, args);
    },
    onLeave(rv) {
      if (this.slot < 0) return;
      if (onL) onL.call(this, rv);
    }
  });
}

// AIUpdateWalk(): per-tick; emit only when the sprite's grid position changed.
hookSprite("AIUpdateWalk", "AIWALK", function () {
  const p = spritePos(this.ecx);
  const k = this.ecx.toString();
  const prev = lastPos[k];
  if (!prev || prev[0] !== p[0] || prev[1] !== p[1]) {
    lastPos[k] = p;
    emit({ tag: "AIWALK", slot: this.slot, pos: p });
  }
}, null);

// SetTarget(CSearchRequest*,...): re-search enqueue — the churn / stutter signal.
// Dump the request goal-counts + the caller (raRva) to find which builder sets
// nTargetIds = party (the binary's 7-goal collision-aware search).
hookSprite("SetTargetReq", "SETTARGET", function (args) {
  const o = { tag: "SETTARGET", slot: this.slot,
              raRva: this.returnAddress.sub(base).toString() };
  try {
    const req = args[0];
    o.req = reqInfo(req);
  } catch (e) { o.reqErr = "" + e; }
  emit(o);
}, null);

// JumpToPoint(CPoint dest, int): bump displacement.
hookSprite("JumpToPoint", "JUMP", function (args) {
  this.jumpDest = [args[0].toInt32(), args[1].toInt32()];
  this.jumpRa = this.returnAddress.sub(base).toString();
  this.jumpPosBefore = spritePos(this.ecx);
}, function (rv) {
  emit({ tag: "JUMP", slot: this.slot, dest: this.jumpDest, raRva: this.jumpRa,
         posBefore: this.jumpPosBefore, posAfter: spritePos(this.ecx), rc: rv.toInt32() });
});

// ClearBumpPath(CPoint& start, CPoint& goal) -> BOOL: bump path clear.
// Log start/goal (grid cells), the RETURN (1=cleared/bumped, 0=failed) and the
// caller RVA (separates top-level AIUpdateWalk:2908 from the recursive :3266).
// rc=0 with members still stuck = the bump fails to displace the blocking ally.
hookSprite("ClearBumpPath", "CLEARBUMP", function (args) {
  this.cbpStart = pt(args[0]);
  this.cbpGoal = pt(args[1]);
  this.cbpRa = this.returnAddress.sub(base).toString();
}, function (rv) {
  emit({ tag: "CLEARBUMP", slot: this.slot, start: this.cbpStart, goal: this.cbpGoal,
         rc: rv.toInt32() & 0xff, raRva: this.cbpRa });
});

// Face() -> SHORT: arrival/orientation facing (return value = new direction).
hookSprite("Face", "FACE", null, function (rv) {
  emit({ tag: "FACE", slot: this.slot, facing: s16(rv), pos: spritePos(this.ecx) });
});

hookCommit("GroupSetTarget", "GST", "GST");
hookCommit("GroupProtectPoint", "GPP", "GPP");
hookCommit("GroupDrawMove", "GDM", "GDM");

send({ tag: "ready", target: CFG.target, module: CFG.module,
       base: base.toString(), resolved: Object.keys(CFG.rvas) });
"""


def main():
    target = "original"
    for i, a in enumerate(sys.argv):
        if a == "--target" and i + 1 < len(sys.argv):
            target = sys.argv[i + 1]
    if target not in ("original", "ours"):
        sys.exit("--target must be 'original' or 'ours'")
    attach = "--attach" in sys.argv

    module, rvas = build_rvas(target)
    exe = os.path.join(GAME_DIR, module)
    log = os.path.join(REPO, f"tmp_frida_group_{target}.log")
    open(log, "w").close()

    cfg = {"target": target, "module": module, "rvas": rvas, "off": OFFSETS[target]}
    js = JS_TEMPLATE.replace("__CFG__", json.dumps(cfg))

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        print(line, flush=True)
        with open(log, "a") as f:
            f.write(line + "\n")

    if attach:
        session = frida.attach(module)
        pid = None
        print(f"[*] attached to running {module}", flush=True)
    else:
        pid = frida.spawn(exe, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned {module} pid={pid}", flush=True)

    script = session.create_script(js)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print(f"[*] target={target}  hooks live. Load the dock save, then:", flush=True)
    print("[*]   right-click PRESS, drag WEST, RELEASE (you perform it; no mouse hijack).", flush=True)
    print(f"[*] logging to {log}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
