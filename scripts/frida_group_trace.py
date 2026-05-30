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

# Canonical move command (screen coords) injected with --force so BOTH binaries
# execute a byte-identical group move regardless of where the user clicks. Values
# are a reference dock west-drag (target SW of the dock, west-ish orientation,
# formation 6) observed in prior captures; adjust if the save/formation changes.
CANON = {"target": [2188, 1032], "cursor": [2218, 1037], "fType": 6}

# ---- ORIGINAL IWD2.exe: Ghidra absolute addresses (verified 2026-05-29) --------
ORIG_ABS = {
    "GroupSetTarget":     0x4063e0,
    "GroupProtectPoint":  0x407280,
    "GroupDrawMove":      0x407fc0,
    "RotateOffsets":      0x4058e0,   # FUN_004058e0 (unnamed free fn)
    "AdjustTarget":       0x46a3d0,
    "FindPath":           0x51e150,
    "DoAction":           0x44d780,
    "MoveToPoint":        0x73f560,
    "AIUpdateWalk":       0x6f9040,
    "SetTargetReq":       0x707d40,   # CGameSprite::SetTarget(CSearchRequest*,int,BYTE)
    "JumpToPoint":        0x745950,
    "ClearBumpPath":      0x6fa900,
    "Face":               0x7462d0,
    "ResolveTargetPoint": 0x72b870,
    "GetDeny":            0x599c70,
    "GetCloseObjects":    0x46cd20,   # FUN_0046cd20 (CGameArea::GetCloseObjects, unnamed in Ghidra)
    "GetCost":            0x547b10,
    "GetMobileCost":      0x547d30,
    "SnapshotGetCost":    0x5485a0,
    "SnapshotRemoveObject": 0x5488c0,
    "SnapshotAddObjectDiagonals": 0x5489e0,
    "AddObject":          0x547e60,
    "RemoveObject":       0x548020,
}

# ---- OUR iwd2-re.exe: MSVC-decorated names to look up in the fresh .map ---------
OURS_MANGLED = {
    "GroupSetTarget":     "?GroupSetTarget@CAIGroup@@QAEXVCPoint@@HF0@Z",
    "GroupProtectPoint":  "?GroupProtectPoint@CAIGroup@@QAEXVCPoint@@F0J@Z",
    "GroupDrawMove":      "?GroupDrawMove@CAIGroup@@QAEXVCPoint@@F0@Z",
    # RotateOffsets is a free/static fn; may be absent from the .map -> optional.
    "AdjustTarget":       "?AdjustTarget@CGameArea@@QAEHVCPoint@@PAUtagPOINT@@EF@Z",
    "FindPath":           "?FindPath@CPathSearch@@QAEHPAUtagPOINT@@0FJJPAVCSearchBitmap@@PAEHPAVCRect@@@Z",
    "DoAction":           "?DoAction@CGameAIBase@@UAEXXZ",
    "MoveToPoint":        "?MoveToPoint@CGameSprite@@QAEFXZ",
    "AIUpdateWalk":       "?AIUpdateWalk@CGameSprite@@QAEXXZ",
    "SetTargetReq":       "?SetTarget@CGameSprite@@QAEXPAVCSearchRequest@@HE@Z",
    "JumpToPoint":        "?JumpToPoint@CGameSprite@@QAEFVCPoint@@H@Z",
    "ClearBumpPath":      "?ClearBumpPath@CGameSprite@@QAEHABVCPoint@@0@Z",
    "Face":               "?Face@CGameSprite@@QAEFXZ",
    "ResolveTargetPoint": "?ResolveTargetPoint@CGameSprite@@QAEXPBVCAIAction@@PAU__POSITION@@@Z",
    "GetDeny":            "?GetDeny@CGameObjectArray@@QAEEJEPAPAVCGameObject@@K@Z",
    "GetCloseObjects":    "?GetCloseObjects@CGameArea@@QAEXPAU__POSITION@@ABVCPoint@@ABVCAIObjectType@@FPBEAAV?$CTypedPtrList@VCPtrList@@PAJ@@HH@Z",
    "GetCost":            "?GetCost@CSearchBitmap@@QAEEABVCPoint@@PBEEAAFH@Z",
    "GetMobileCost":      "?GetMobileCost@CSearchBitmap@@QAEEABVCPoint@@PBEEH@Z",
    "SnapshotGetCost":    "?SnapshotGetCost@CSearchBitmap@@QAEEVCPoint@@H@Z",
    "SnapshotRemoveObject": "?SnapshotRemoveObject@CSearchBitmap@@QAEXVCPoint@@EH@Z",
    "SnapshotAddObjectDiagonals": "?SnapshotAddObjectDiagonals@CSearchBitmap@@QAEXVCPoint@@EH@Z",
    "AddObject":          "?AddObject@CSearchBitmap@@QAEXABVCPoint@@EEHAAE@Z",
    "RemoveObject":       "?RemoveObject@CSearchBitmap@@QAEXABVCPoint@@EEHAAE@Z",
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
                 "reqSourcePt": 0x30, "reqPartyIds": 0x38, "reqTargetPoints": 0x40, "reqBump": 0x54,
                 "interrupt": 0x470, "actionCount": 0x474, "curAction": 0x476,
                 "actionSpec": 0x52c, "actionSpec2": 0x530, "actionSpec3": 0x534,
                 "actionDest": 0x540},
    "ours":     {"listHead": 0x0c, "mPos": 0x08,
                 "reqCollision": 0x04, "reqNParty": 0x09, "reqNTargetIds": 0x0a, "reqNPoints": 0x0b,
                 "reqRemoveSelf": 0x0c, "reqPathSmooth": 0x28, "reqSourceId": 0x30,
                 "reqSourcePt": 0x34, "reqPartyIds": 0x3c, "reqTargetPoints": 0x44, "reqBump": 0x58,
                 "interrupt": 0x4b8, "actionCount": 0x4bc, "curAction": 0x4c0,
                 "actionSpec": 0x584, "actionSpec2": 0x588, "actionSpec3": 0x58c,
                 "actionDest": 0x598},
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
const spriteBySlot = {};
// Sprite-ptr string -> last reported [x,y], to dedupe stationary AIUpdateWalk ticks.
const lastPos = {};
// Active party AIUpdateWalk calls. Used to attribute mobile-cost probes.
const aiwStack = [];
// Active party ClearBumpPath calls. Used to attribute nested search-map probes.
const cbpStack = [];

// m_currentSearchRequest field offset, auto-calibrated from a SetTarget call
// (after SetTarget, sprite[CSR_OFF] == the request just passed). Avoids hardcoding
// per-build offsets (binary 0x53d6; our build is shifted). csr=1/0 at a bump-fail
// tells whether the blocked sprite still has a pending search (branch-1 re-search)
// or not (branch-2 JumpToPoint-to-path-end = the teleport).
let CSR_OFF = -1;
function calibrateCsr(sprite, reqPtr) {
  if (CSR_OFF >= 0 || reqPtr.isNull()) return;
  for (let off = 0x4000; off <= 0x5600; off += 2) {   // binary m_currentSearchRequest@0x53d6 is 2-aligned
    try {
      if (sprite.add(off).readPointer().equals(reqPtr)) {
        CSR_OFF = off;
        emit({ tag: "CALIB_CSR", off: off });
        return;
      }
    } catch (e) { /* unmapped — keep scanning */ }
  }
}
function csrState(sprite) {
  if (CSR_OFF < 0) return null;
  try { return sprite.add(CSR_OFF).readPointer().isNull() ? 0 : 1; } catch (e) { return null; }
}

// CGameSprite::m_curDest is far beyond the CAIAction block. It gates the
// MoveToPoint "new destination" branch, so log it separately from action.dest.
let CURDEST_OFF = (CFG.target === "original") ? 0x556e : -1;
function samePt(a, b) {
  return a && b && a[0] === b[0] && a[1] === b[1];
}
function calibrateCurDest(sprite, desired) {
  if (CURDEST_OFF >= 0 || dockFormationScore(desired) < 0) return;

  let best = null;
  const expected = CSR_OFF >= 0 ? CSR_OFF + 0x198 : 0x556e;
  const scanLo = CSR_OFF >= 0 ? Math.max(0x4000, expected - 0x500) : 0x4800;
  const scanHi = CSR_OFF >= 0 ? Math.min(0x7000, expected + 0x500) : 0x5c80;
  for (let off = scanLo; off <= scanHi; off += 2) {
    let cur;
    try { cur = pt(sprite.add(off)); } catch (e) { continue; }
    if (!samePt(cur, desired)) continue;

    let score = 1000 - Math.abs(off - expected);
    if ((off & 3) === 0) score += 2;
    if (best === null || score > best.score) {
      best = { off: off, score: score, pt: cur };
    }
  }

  if (best !== null) {
    CURDEST_OFF = best.off;
    emit({ tag: "CALIB_CURDEST", off: best.off, pt: best.pt, score: best.score });
  }
}
function curDestState(sprite) {
  if (CURDEST_OFF < 0) return null;
  try { return { off: CURDEST_OFF, pt: pt(sprite.add(CURDEST_OFF)) }; } catch (e) { return null; }
}

// CAIAction sits inside CGameAIBase, whose default-aligned rebuild layout has
// drifted from the packed original. Calibrate from live MoveToPoint calls:
// current action is one of the movement opcodes and its CAIAction destination is
// a plausible world point near the forced dock formation.
let ACTION_LAYOUT = null;
function staticActionLayout() {
  return { curAction: CFG.off.curAction, actionCount: CFG.off.actionCount,
           interrupt: CFG.off.interrupt, actionSpec: CFG.off.actionSpec,
           actionSpec2: CFG.off.actionSpec2, actionSpec3: CFG.off.actionSpec3,
           actionDest: CFG.off.actionDest, source: "static" };
}
function movementActionId(id) {
  return id === 23 || id === 90 || id === 261;
}
function plausibleWorldPoint(p) {
  return p[0] >= 0 && p[0] < 10000 && p[1] >= 0 && p[1] < 10000;
}
function dockFormationScore(p) {
  if (!plausibleWorldPoint(p)) return -100;
  let score = 1;
  const inDock = p[0] >= 1800 && p[0] <= 2600 && p[1] >= 800 && p[1] <= 1250;
  if (CFG.force && !inDock) return -100;
  if (inDock) score += 5;
  if (CFG.force) {
    const dx = Math.abs(p[0] - CFG.force.target[0]);
    const dy = Math.abs(p[1] - CFG.force.target[1]);
    if (dx <= 250 && dy <= 250) score += 2;
  }
  return score;
}
function chooseActionCountOff(sprite, curOff) {
  const offs = [curOff - 2, curOff - 4, curOff - 6, curOff - 8, curOff - 10, curOff - 12];
  for (let i = 0; i < offs.length; i++) {
    try {
      const v = sprite.add(offs[i]).readS16();
      if (v >= 0 && v <= 500) return offs[i];
    } catch (e) { /* try next */ }
  }
  return CFG.off.actionCount;
}
function chooseInterruptOff(sprite, curOff) {
  const offs = [curOff - 8, curOff - 6, curOff - 10, curOff - 12, curOff - 4];
  for (let i = 0; i < offs.length; i++) {
    try {
      const v = sprite.add(offs[i]).readS32();
      if (v === 0 || v === 1) return offs[i];
    } catch (e) { /* try next */ }
  }
  return CFG.off.interrupt;
}
function calibrateAction(sprite) {
  if (ACTION_LAYOUT !== null) return;
  if (CFG.target === "original") {
    ACTION_LAYOUT = staticActionLayout();
    return;
  }

  let best = null;
  const destRels = [0xd8, 0xca, 0xcc, 0xc8, 0xd0, 0xc6, 0xd2, 0xc4, 0xd4, 0xdc];
  for (let curOff = 0x420; curOff <= 0x5c0; curOff += 2) {
    let actionId;
    try { actionId = sprite.add(curOff).readS16(); } catch (e) { continue; }
    if (!movementActionId(actionId)) continue;

    for (let i = 0; i < destRels.length; i++) {
      const destOff = curOff + destRels[i];
      let dest;
      try { dest = pt(sprite.add(destOff)); } catch (e) { continue; }
      let score = dockFormationScore(dest);
      if (score < 0) continue;
      if (actionId === 23) score += 3;
      try {
        const spec2 = sprite.add(destOff - 0x10).readS32();
        if (spec2 >= 0 && spec2 <= 10) score += 2;
      } catch (e) { /* optional */ }
      const countOff = chooseActionCountOff(sprite, curOff);
      const interruptOff = chooseInterruptOff(sprite, curOff);
      if (countOff >= 0) score += 1;
      if (interruptOff >= 0) score += 1;
      score -= Math.abs(curOff - CFG.off.curAction) / 64.0;

      if (best === null || score > best.score) {
        best = { score: score, curAction: curOff, actionCount: countOff,
                 interrupt: interruptOff, actionSpec: destOff - 0x14,
                 actionSpec2: destOff - 0x10, actionSpec3: destOff - 0x0c,
                 actionDest: destOff, actionId: actionId, dest: dest,
                 destRel: destRels[i] };
      }
    }
  }

  if (best !== null && best.score >= 6) {
    ACTION_LAYOUT = { curAction: best.curAction, actionCount: best.actionCount,
                      interrupt: best.interrupt, actionSpec: best.actionSpec,
                      actionSpec2: best.actionSpec2, actionSpec3: best.actionSpec3,
                      actionDest: best.actionDest, source: "calibrated" };
    emit({ tag: "CALIB_ACTION", curAction: best.curAction,
           actionCount: best.actionCount, interrupt: best.interrupt,
           actionDest: best.actionDest, destRel: best.destRel,
           actionId: best.actionId, dest: best.dest, score: best.score });
  }
}

function actionInfo(sprite) {
  const layout = ACTION_LAYOUT || staticActionLayout();
  const o = {};
  o.layout = layout.source;
  try { o.action = sprite.add(layout.curAction).readS16(); } catch (e) { o.actionErr = "" + e; }
  try { o.actionCount = sprite.add(layout.actionCount).readS16(); } catch (e) {}
  try { o.interrupt = sprite.add(layout.interrupt).readS32(); } catch (e) {}
  try { o.spec = sprite.add(layout.actionSpec).readS32(); } catch (e) {}
  try { o.spec2 = sprite.add(layout.actionSpec2).readS32(); } catch (e) {}
  try { o.spec3 = sprite.add(layout.actionSpec3).readS32(); } catch (e) {}
  try { o.dest = pt(sprite.add(layout.actionDest)); } catch (e) {}
  const cd = curDestState(sprite);
  if (cd !== null) {
    o.curDestOff = cd.off;
    o.curDest = cd.pt;
  }
  return o;
}

const searchLayout = {};
let FP_CTX = null;
const SNAP_TRACE_KEYS = {};

function getSearchLayout(search) {
  const key = search.toString();
  if (key in searchLayout) return searchLayout[key];

  // CSearchBitmap is packed in the original (m_GridSquareDimensions@0xe6) and
  // slightly shifted in our build. m_GridSquareDimensions is 0x2c bytes after
  // m_pDynamicCost. Try known offsets first, then scan for plausible CSize pairs.
  const dimCandidates = CFG.target === "original"
    ? [0xe6, 0xe8, 0xe4, 0xea]
    : [0xe8, 0xe6, 0xea, 0xec];
  for (let i = 0; i < dimCandidates.length; i++) {
    const off = dimCandidates[i];
    try {
      const cx = search.add(off).readS32();
      const cy = search.add(off + 4).readS32();
      const dynOff = off - 0x2c;
      const dynPtr = search.add(dynOff).readPointer();
      if (cx > 0 && cx <= 1024 && cy > 0 && cy <= 1024 && !dynPtr.isNull()) {
        const layout = { dimOff: off, dynOff: dynOff, cx: cx, cy: cy, dynPtr: dynPtr };
        searchLayout[key] = layout;
        emit({ tag: "CALIB_SEARCH", dimOff: off, dynOff: dynOff, cx: cx, cy: cy,
               dynPtr: dynPtr.toString() });
        return layout;
      }
    } catch (e) { /* try next */ }
  }

  for (let off = 0x80; off < 0x140; off++) {
    try {
      const cx = search.add(off).readS32();
      const cy = search.add(off + 4).readS32();
      if (cx > 0 && cx <= 1024 && cy > 0 && cy <= 1024) {
        const dynOff = off - 0x2c;
        const dynPtr = search.add(dynOff).readPointer();
        if (dynPtr.isNull()) continue;
        const layout = { dimOff: off, dynOff: dynOff, cx: cx, cy: cy, dynPtr: dynPtr };
        searchLayout[key] = layout;
        emit({ tag: "CALIB_SEARCH", dimOff: off, dynOff: dynOff, cx: cx, cy: cy,
               dynPtr: dynPtr.toString() });
        return layout;
      }
    } catch (e) { /* keep scanning */ }
  }

  const layout = { dimOff: -1, dynOff: -1, cx: 320, cy: 320, dynPtr: ptr("0") };
  searchLayout[key] = layout;
  emit({ tag: "CALIB_SEARCH_FAIL", search: key });
  return layout;
}

function dynamicFlags(search, point, personalSpace) {
  const layout = getSearchLayout(search);
  const out = [];
  if (layout.dynPtr.isNull()) return out;
  const radius = ((personalSpace - 2) / 2) | 0;
  const minX = Math.max(point[0] - radius, 0);
  const maxX = Math.min(point[0] + radius, layout.cx);
  const minY = Math.max(point[1] - radius, 0);
  const maxY = Math.min(point[1] + radius, layout.cy);
  for (let x = minX; x <= maxX; x++) {
    for (let y = minY; y <= maxY; y++) {
      try {
        const flags = layout.dynPtr.add(y * layout.cx + x).readU8();
        out.push([x, y, flags]);
      } catch (e) {
        out.push([x, y, -1]);
      }
    }
  }
  return out;
}

function snapshotFlags(search, point, personalSpace) {
  const layout = getSearchLayout(search);
  const out = [];
  if (layout.dynPtr.isNull() || layout.dynOff < 0) return out;
  let snapPtr = ptr("0");
  try { snapPtr = search.add(layout.dynOff + 4).readPointer(); } catch (e) {}
  if (snapPtr.isNull()) return out;
  const radius = ((personalSpace - 2) / 2) | 0;
  const minX = Math.max(point[0] - radius, 0);
  const maxX = Math.min(point[0] + radius, layout.cx);
  const minY = Math.max(point[1] - radius, 0);
  const maxY = Math.min(point[1] + radius, layout.cy);
  for (let x = minX; x <= maxX; x++) {
    for (let y = minY; y <= maxY; y++) {
      try {
        const flags = snapPtr.add(y * layout.cx + x).readU8();
        out.push([x, y, flags]);
      } catch (e) {
        out.push([x, y, -1]);
      }
    }
  }
  return out;
}

function fpKey(start, goal0) {
  return "" + start[0] + "," + start[1] + "->" + goal0[0] + "," + goal0[1];
}

function shouldTraceSnap(start, goal0) {
  const key = fpKey(start, goal0);
  if (key !== "148,72->141,84") return false;
  if (key in SNAP_TRACE_KEYS) return false;
  SNAP_TRACE_KEYS[key] = true;
  return true;
}

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
function slotForInterior(p) {
  for (const k in spriteBySlot) {
    const sprite = spriteBySlot[k];
    if (p.compare(sprite) >= 0 && p.compare(sprite.add(0x9000)) < 0) {
      return { slot: parseInt(k, 10), off: p.sub(sprite).toInt32() };
    }
  }
  return { slot: -1, off: -1 };
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
      // Canonicalize the move command so BOTH binaries receive a byte-identical
      // target/cursor/formationType -> removes click-variance (you still do a rough
      // right-click drag; we just normalize the args at the engine API). Makes the
      // per-slot differential deterministic + repeatable.
      if (CFG.force) {
        args[0] = ptr(CFG.force.target[0] >>> 0);
        args[1] = ptr(CFG.force.target[1] >>> 0);
        if (layout === "GST") {       // (CPoint, BOOL additive, SHORT fType, CPoint cursor)
          args[3] = ptr(CFG.force.fType >>> 0);
          args[4] = ptr(CFG.force.cursor[0] >>> 0);
          args[5] = ptr(CFG.force.cursor[1] >>> 0);
        } else {                      // GPP/GDM: (CPoint, SHORT fType, CPoint cursor, ...)
          args[2] = ptr(CFG.force.fType >>> 0);
          args[3] = ptr(CFG.force.cursor[0] >>> 0);
          args[4] = ptr(CFG.force.cursor[1] >>> 0);
        }
      }
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
        spriteBySlot[slot] = sprite;
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
      this.nGoalsRaw = args[2].toInt32();
      this.nGoalsShort = s16(args[2]);
      this.minNodes = args[3].toInt32();   // minNodeLimit
      this.maxNodes = args[4].toInt32();   // maxNodeLimit
      this.bBump = args[7].toInt32();
      this.req = args[6];   // serviceState == &request->m_serviceState == request base
      this.startIn = pt(this.start);
      this.goal0In = pt(this.goals);
      this.traceSnap = shouldTraceSnap(this.startIn, this.goal0In);
      this.snapCount = 0;
      FP_CTX = this;
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
        for (let i = 0; i < this.nGoalsShort && i < 8; i++) goals.push(pt(this.goals.add(i * 8)));
        const goalBuf = [];
        for (let i = 0; i < 8; i++) goalBuf.push(pt(this.goals.add(i * 8)));
        let reqBytes = "";
        let req = null;
        try { reqBytes = hbytes(this.req, 0x10); } catch (e) {}
        try { req = reqInfo(this.req); } catch (e) {}
        emit({ tag: "FINDPATH", rc: rv.toInt32(), bBump: this.bBump,
               nGoalsRaw: this.nGoalsRaw, nGoalsShort: this.nGoalsShort,
               minNodes: this.minNodes, maxNodes: this.maxNodes,
               start: pt(this.start), goals: goals, goalBuf: goalBuf, n: n,
               head: path.slice(0, 3).map(decode), tail: path.slice(-3).map(decode),
               path: path, reqBytes: reqBytes, req: req });
      } catch (e) { emit({ tag: "FINDPATH_ERR", err: "" + e }); }
      if (FP_CTX === this) FP_CTX = null;
    }
  });
})();

// ---- Snapshot search-map probes inside FindPath -----------------------------
function hookSnapshotObject(name, tag) {
  const a = A(name);
  if (a === null) return;
  Interceptor.attach(a, {
    onEnter(args) {
      this.search = this.context.ecx;
      this.point = [args[0].toInt32(), args[1].toInt32()];
      this.ps = args[2].toInt32() & 0xff;
      this.bumpable = args[3].toInt32();
      this.before = snapshotFlags(this.search, this.point, this.ps);
      this.ra = this.returnAddress.sub(base).toString();
    },
    onLeave() {
      emit({ tag: tag, point: this.point, personalSpace: this.ps,
             bumpable: this.bumpable, before: this.before,
             after: snapshotFlags(this.search, this.point, this.ps),
             raRva: this.ra });
    }
  });
}
hookSnapshotObject("SnapshotRemoveObject", "SNAPREMOVE");
hookSnapshotObject("SnapshotAddObjectDiagonals", "SNAPDIAG");

(function () {
  const a = A("SnapshotGetCost");
  if (a === null) return;
  Interceptor.attach(a, {
    onEnter(args) {
      this.search = this.context.ecx;
      this.point = [args[0].toInt32(), args[1].toInt32()];
      this.bump = args[2].toInt32();
      this.ctx = FP_CTX;
      this.trace = this.ctx !== null && this.ctx.traceSnap && this.ctx.snapCount < 96;
      if (this.trace) {
        this.ctx.snapCount++;
        this.flags = snapshotFlags(this.search, this.point, 3);
      }
    },
    onLeave(rv) {
      if (!this.trace) return;
      emit({ tag: "SNAPCOST", fp: fpKey(this.ctx.startIn, this.ctx.goal0In),
             idx: this.ctx.snapCount, point: this.point, bBump: this.bump,
             rc: rv.toInt32() & 0xff, flags: this.flags });
    }
  });
})();

// ---- Live search-map object updates -----------------------------------------
function hookMapObject(name, op) {
  const a = A(name);
  if (a === null) return;
  Interceptor.attach(a, {
    onEnter(args) {
      const owner = slotForInterior(args[4]); // BOOLEAN& m_bOnSearchMap
      this.slot = owner.slot;
      if (this.slot < 0) return;
      this.off = owner.off;
      this.point = pt(args[0]);
      this.side = args[1].toInt32() & 0xff;
      this.ps = args[2].toInt32() & 0xff;
      this.bumpable = args[3].toInt32();
      this.onPtr = args[4];
      this.onBefore = -1;
      try { this.onBefore = this.onPtr.readU8(); } catch (e) {}
      this.mapRa = this.returnAddress.sub(base).toString();
    },
    onLeave() {
      if (this.slot < 0) return;
      let onAfter = -1;
      try { onAfter = this.onPtr.readU8(); } catch (e) {}
      emit({ tag: "MAPOBJ", op: op, slot: this.slot, point: this.point,
             side: this.side, personalSpace: this.ps, bumpable: this.bumpable,
             onBefore: this.onBefore, onAfter: onAfter, onOff: this.off,
             raRva: this.mapRa });
    }
  });
}
hookMapObject("AddObject", "add");
hookMapObject("RemoveObject", "remove");

// ---- GetMobileCost probes inside AIUpdateWalk -------------------------------
// This is the gate that decides whether a sprite can enter the next search cell
// or must call ClearBumpPath. Log dynamic flags for the probed cell(s).
(function () {
  const a = A("GetMobileCost");
  if (a === null) return;
  const aiw = A("AIUpdateWalk");
  Interceptor.attach(a, {
    onEnter(args) {
      const ra = this.returnAddress;
      this.fromAiw = aiw !== null && ra.compare(aiw) >= 0 && ra.compare(aiw.add(0x2600)) < 0;
      if (!this.fromAiw) return;
      const ctx = aiwStack.length > 0 ? aiwStack[aiwStack.length - 1] : null;
      this.slot = ctx ? ctx.slot : -1;
      this.posBefore = ctx ? ctx.pos : null;
      this.search = this.context.ecx;
      this.point = pt(args[0]);
      this.ps = args[2].toInt32() & 0xff;
      this.bCheckBump = args[3].toInt32();
      this.mobileRa = ra.sub(base).toString();
      this.flags = dynamicFlags(this.search, this.point, this.ps);
    },
    onLeave(retval) {
      if (!this.fromAiw) return;
      emit({ tag: "MOBILECOST", slot: this.slot, posBefore: this.posBefore,
             point: this.point, personalSpace: this.ps, bCheckBump: this.bCheckBump,
             rc: retval.toInt32() & 0xff, flags: this.flags, raRva: this.mobileRa });
    }
  });
})();

// ---- GetCost probes inside ClearBumpPath ------------------------------------
// Split the rc=0 ClearBumpPath failures: if this returns COST_IMPASSABLE (255),
// ClearBumpPath exits before GetCloseObjects can gather/push blocking allies.
(function () {
  const a = A("GetCost");
  if (a === null) return;
  const cbp = A("ClearBumpPath");
  Interceptor.attach(a, {
    onEnter(args) {
      const ra = this.returnAddress;
      this.fromCbp = cbp !== null && ra.compare(cbp) >= 0 && ra.compare(cbp.add(0x2600)) < 0;
      if (!this.fromCbp) return;
      const ctx = cbpStack.length > 0 ? cbpStack[cbpStack.length - 1] : null;
      this.slot = ctx ? ctx.slot : -1;
      this.cbpStart = ctx ? ctx.start : null;
      this.cbpGoal = ctx ? ctx.goal : null;
      this.point = pt(args[0]);
      this.ps = args[2].toInt32() & 0xff;
      this.tableIndex = args[3];
      this.bCheckBump = args[4].toInt32();
      this.costRa = ra.sub(base).toString();
    },
    onLeave(retval) {
      if (!this.fromCbp) return;
      let tableOut = null;
      try { tableOut = this.tableIndex.readS16(); } catch (e) {}
      emit({ tag: "GETCOST", slot: this.slot, point: this.point, personalSpace: this.ps,
             bCheckBump: this.bCheckBump, rc: retval.toInt32() & 0xff,
             tableIndex: tableOut, cbpStart: this.cbpStart, cbpGoal: this.cbpGoal,
             raRva: this.costRa });
    }
  });
})();

// ---- GetCloseObjects (obstacle gather inside ClearBumpPath) -----------------
// Case-1 split: how many obstacles does ClearBumpPath's gather actually find?
// count==0 -> the blocking ally is NOT detected (gather bug); count>0 -> the
// failure is eligibility (m_bBumpable) or placement. Filtered to ClearBumpPath
// callers (returnAddress inside the ClearBumpPath body); raRva logged to verify.
(function () {
  const a = A("GetCloseObjects");
  if (a === null) return;
  const cbp = A("ClearBumpPath");
  Interceptor.attach(a, {
    onEnter(args) {
      const ra = this.returnAddress;
      this.fromCbp = cbp !== null && ra.compare(cbp) >= 0 && ra.compare(cbp.add(0x2600)) < 0;
      if (!this.fromCbp) return;
      this.center = pt(args[1]);     // const CPoint& center (world)
      this.outList = args[5];        // CTypedPtrList<CPtrList,LONG*>& out
      this.gcoRa = ra.sub(base).toString();
    },
    onLeave() {
      if (!this.fromCbp) return;
      let count = -1;
      // MFC CPtrList (vptr@0, head@4, tail@8, count@0xc).
      try { count = this.outList.add(0x0c).readS32(); } catch (e) {}
      emit({ tag: "CLOSEOBJ", center: this.center, count: count, raRva: this.gcoRa });
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
  this.aiwCtx = { slot: this.slot, pos: p };
  aiwStack.push(this.aiwCtx);
  const k = this.ecx.toString();
  const prev = lastPos[k];
  if (!prev || prev[0] !== p[0] || prev[1] !== p[1]) {
    lastPos[k] = p;
    emit({ tag: "AIWALK", slot: this.slot, pos: p });
  }
}, function () {
  if (aiwStack.length > 0 && aiwStack[aiwStack.length - 1] === this.aiwCtx) {
    aiwStack.pop();
  }
});

// SetTarget(CSearchRequest*,...): re-search enqueue — the churn / stutter signal.
// Dump the request goal-counts + the caller (raRva) to find which builder sets
// nTargetIds = party (the binary's 7-goal collision-aware search).
hookSprite("SetTargetReq", "SETTARGET", function (args) {
  this.stReq = args[0];
  const o = { tag: "SETTARGET", slot: this.slot,
              raRva: this.returnAddress.sub(base).toString() };
  try {
    o.req = reqInfo(args[0]);
  } catch (e) { o.reqErr = "" + e; }
  emit(o);
}, function () {
  // After SetTarget, m_currentSearchRequest == this.stReq -> locate its offset.
  calibrateCsr(this.ecx, this.stReq);
});

// DoAction()/MoveToPoint(): action lifetime around short nPath==1 searches.
// The original keeps the MOVETOPOINT alive long enough to reissue a group
// search; if ours loses actionCount or advances to FACE too early this shows it.
hookSprite("DoAction", "DOACTION", function () {
  calibrateAction(this.ecx);
  const before = actionInfo(this.ecx);
  calibrateCurDest(this.ecx, before.dest);
  this.doBefore = actionInfo(this.ecx);
}, function (rv) {
  let after = actionInfo(this.ecx);
  calibrateCurDest(this.ecx, after.dest);
  after = actionInfo(this.ecx);
  emit({ tag: "DOACTION", slot: this.slot, pos: spritePos(this.ecx),
         before: this.doBefore, after: after, csr: csrState(this.ecx) });
});

hookSprite("MoveToPoint", "MOVETOPOINT", function () {
  calibrateAction(this.ecx);
  const before = actionInfo(this.ecx);
  calibrateCurDest(this.ecx, before.dest);
  this.mtpBefore = actionInfo(this.ecx);
}, function (rv) {
  let after = actionInfo(this.ecx);
  calibrateCurDest(this.ecx, after.dest);
  after = actionInfo(this.ecx);
  emit({ tag: "MOVETOPOINT", slot: this.slot, pos: spritePos(this.ecx),
         before: this.mtpBefore, after: after,
         csr: csrState(this.ecx), rc: s16(rv) });
});

// JumpToPoint(CPoint dest, int): bump displacement.
hookSprite("JumpToPoint", "JUMP", function (args) {
  this.jumpDest = [args[0].toInt32(), args[1].toInt32()];
  this.jumpRa = this.returnAddress.sub(base).toString();
  this.jumpPosBefore = spritePos(this.ecx);
}, function (rv) {
  emit({ tag: "JUMP", slot: this.slot, dest: this.jumpDest, raRva: this.jumpRa,
         posBefore: this.jumpPosBefore, posAfter: spritePos(this.ecx),
         csr: csrState(this.ecx), rc: rv.toInt32() });
});

// ClearBumpPath(CPoint& start, CPoint& goal) -> BOOL: bump path clear.
// Log start/goal (grid cells), the RETURN (1=cleared/bumped, 0=failed) and the
// caller RVA (separates top-level AIUpdateWalk:2908 from the recursive :3266).
// rc=0 with members still stuck = the bump fails to displace the blocking ally.
hookSprite("ClearBumpPath", "CLEARBUMP", function (args) {
  this.cbpStart = pt(args[0]);
  this.cbpGoal = pt(args[1]);
  this.cbpRa = this.returnAddress.sub(base).toString();
  this.cbpCtx = { slot: this.slot, start: this.cbpStart, goal: this.cbpGoal };
  cbpStack.push(this.cbpCtx);
}, function (rv) {
  emit({ tag: "CLEARBUMP", slot: this.slot, start: this.cbpStart, goal: this.cbpGoal,
         rc: rv.toInt32() & 0xff, csr: csrState(this.ecx), raRva: this.cbpRa });
  if (cbpStack.length > 0 && cbpStack[cbpStack.length - 1] === this.cbpCtx) {
    cbpStack.pop();
  }
});

// Face() -> SHORT: arrival/orientation facing (return value = new direction).
hookSprite("Face", "FACE", null, function (rv) {
  emit({ tag: "FACE", slot: this.slot, facing: s16(rv), pos: spritePos(this.ecx) });
});

hookCommit("GroupSetTarget", "GST", "GST");
hookCommit("GroupProtectPoint", "GPP", "GPP");
hookCommit("GroupDrawMove", "GDM", "GDM");

send({ tag: "ready", target: CFG.target, module: CFG.module,
       base: base.toString(), force: CFG.force, resolved: Object.keys(CFG.rvas) });
"""


def main():
    target = "original"
    for i, a in enumerate(sys.argv):
        if a == "--target" and i + 1 < len(sys.argv):
            target = sys.argv[i + 1]
    if target not in ("original", "ours"):
        sys.exit("--target must be 'original' or 'ours'")
    attach = "--attach" in sys.argv
    force = CANON if "--force" in sys.argv else None

    module, rvas = build_rvas(target)
    exe = os.path.join(GAME_DIR, module)
    log = os.path.join(REPO, f"tmp_frida_group_{target}.log")
    open(log, "w").close()

    cfg = {"target": target, "module": module, "rvas": rvas, "off": OFFSETS[target], "force": force}
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
