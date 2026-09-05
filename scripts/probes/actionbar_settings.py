#!/usr/bin/env python3
"""actionbar_settings.py - the ORIGINAL-vs-OURS oracle for CInfButtonArray.

`CInfButtonArray::UpdateButtons` (0x58A340) is a 43-arm switch whose entire
output is the twelve `CInfButtonSettings` blocks and the twelve button types.
Session 40 measured the arms but stopped, because "there is no way to SEE the
action bar from this box" and a half-verified rewrite of the most visible UI in
the game is exactly what "missing better than wrong" forbids.

This is that missing oracle, and it is better than a screenshot: it photographs
the FIELDS UpdateButtons wrote, on both sides, for the same save.

  1. the original           scripts/frida_orig.py --hooks <table> --load-slot 0
  2. our build              auto_start_game.py --slot 0, with the Iwd2DebugLog
                            block below pasted into RenderButton
  3. the diff               this script, --diff

Both sides are photographed at the entry of `CInfButtonArray::RenderButton`
(0x5950F0), which runs once per slot per frame and reads exactly what
UpdateButtons wrote -- so it needs no onLeave hook and no source change on the
original's side at all.  `--load-slot` and `--slot` are the same visible row of
the load screen, which is what keeps the two runs comparable.

Usage:
  python scripts/probes/actionbar_settings.py --emit-hooks c:/tmp/bar.json
  python scripts/frida_orig.py --hooks c:/tmp/bar.json --out c:/tmp/orig.jsonl \\
      --load-slot 0 --post-load 15 --timeout 240 --hit RenderButton
  python scripts/probes/actionbar_settings.py --report c:/tmp/orig.jsonl
  python scripts/probes/actionbar_settings.py --diff c:/tmp/orig.jsonl \\
      "C:/Juegos/Icewind Dale 2/iwd2-re-debug.log"

The our-side half is a deliberately armed Iwd2DebugLog, added to a CLEAN tree
and removed with one `git checkout` (see CLAUDE.md).  Paste at the top of
CInfButtonArray::RenderButton:

    {
        static INT nDbgCalls = 0;
        nDbgCalls++;
        if (nDbgCalls == 1 || nDbgCalls == 601) {
            Iwd2DebugLog("BAR call=%d state=%d selBtn=%d",
                nDbgCalls, m_nState, m_nSelectedButton);
            for (INT nDbg = 0; nDbg < 12; nDbg++) {
                const CInfButtonSettings& s = m_buttonArray[nDbg];
                Iwd2DebugLog("BAR %d type=%d f0=%d act=%d ovl=%d nf=%d sf=%d "
                    "seq=%d sel=%d aws=%d cnt=%d grey=%d res=%.8s hres=%.8s "
                    "frm=%d",
                    nDbg, m_buttonTypes[nDbg], s.field_0, s.m_bActive,
                    s.m_bHasOverlay, s.m_nIconNormalFrame,
                    s.m_nIconSelectedFrame, s.m_nIconSequence, s.m_bSelected,
                    s.m_bActiveWeaponSet, s.m_nCount, s.m_bGreyOut,
                    (const char*)s.m_iconCell.CResHelper<CResCell, 1000>::GetResRef().GetResRef(),
                    (const char*)s.m_iconCell.m_header.GetResRef().GetResRef(),
                    (INT)s.m_iconCell.m_nCurrentFrame);
            }
        }
    }

Two resref columns, because a CVidCell carries two: `res` is the cell's own
CResHelper<CResCell> resref at CVidCell+0xAC and `hres` is m_header's at
CVidCell+0xBC.  Both sides read both.  Session 41 compared the original's `res`
against our `hres` and reported the difference as an UpdateButtons bug; it is
not one, and the layout is not skewed either -- our CVidCell puts the base
CResHelper at +0xA4 and m_header at +0xB4, exactly as the inlined SetResRef
bodies inside UpdateButtons do.  A `res` that agrees while `hres` differs is a
resource-loading difference, not a difference in what UpdateButtons wrote.
"""
from __future__ import annotations

import argparse
import json
import re
import sys

# CInfButtonArray layout, from src/CInfButtonArray.h (binary offsets, pack(2)).
SETTINGS_STRIDE = 0x1E0
NBUTTONS = 12
BUTTON_TYPES = 0x16B0
SELECTED = 0x197E
STATE = 0x1982

# m_iconCell sits at settings+0x14 and m_countCell at settings+0xEE.  A CVidCell
# has TWO resrefs -- its own CResHelper<CResCell> at +0xAC and m_header's at
# +0xBC -- and the current frame at +0xC4.
ICON_CELL = 0x14
COUNT_CELL = 0xEE
VIDCELL_RESREF = 0xAC
VIDCELL_HEADER_RESREF = 0xBC
VIDCELL_FRAME = 0xC4

RENDER_BUTTON = "0x5950F0"
RENDER_BUTTON_OVERLAY = "0x5957C0"
UPDATE_BUTTONS = "0x58A340"

# The scalar half of a settings block: everything UpdateButtons writes that is
# not a CVidCell.  Order is the report's column order.
FIELDS = [
    (0x000, "f0"),
    (0x004, "act"),
    (0x008, "ovl"),
    (0x00C, "nf"),
    (0x010, "sf"),
    (0x1C8, "seq"),
    (0x1CC, "sel"),
    (0x1D0, "aws"),
    (0x1D8, "cnt"),
    (0x1DC, "grey"),
]
COLUMNS = ["type"] + [name for _, name in FIELDS] + ["res", "hres", "frm"]


def build_hooks():
    dump = []
    for n in range(NBUTTONS):
        base = n * SETTINGS_STRIDE
        dump.append({"off": hex(BUTTON_TYPES + n * 4), "type": "s32",
                     "label": "b%d.type" % n})
        for off, name in FIELDS:
            dump.append({"off": hex(base + off), "type": "s32",
                         "label": "b%d.%s" % (n, name)})
        dump.append({"off": hex(base + ICON_CELL + VIDCELL_RESREF), "type": "str",
                     "label": "b%d.res" % n})
        dump.append({"off": hex(base + ICON_CELL + VIDCELL_HEADER_RESREF),
                     "type": "str", "label": "b%d.hres" % n})
        dump.append({"off": hex(base + ICON_CELL + VIDCELL_FRAME), "type": "s32",
                     "label": "b%d.frm" % n})
        dump.append({"off": hex(base + COUNT_CELL + VIDCELL_RESREF), "type": "str",
                     "label": "b%d.cres" % n})
        dump.append({"off": hex(base + COUNT_CELL + VIDCELL_FRAME), "type": "s32",
                     "label": "b%d.cfrm" % n})
    dump.append({"off": hex(SELECTED), "type": "s32", "label": "selBtn"})
    dump.append({"off": hex(STATE), "type": "s32", "label": "state"})

    # nButton is args[3], not args[4]: measured, and it is why the raw dwords are
    # logged rather than a guessed prototype.  Both render entries take the same
    # shape and BOTH are called for all twelve slots every frame -- neither one
    # is dispatched per arm.
    render_args = ["u32"] * 6
    return {
        "process": "IWD2.exe",
        "hooks": [
            {"addr": RENDER_BUTTON, "name": "RenderButton", "conv": "thiscall",
             "args": render_args, "this_dump": dump, "max": 14},
            {"addr": RENDER_BUTTON_OVERLAY, "name": "RenderButtonOverlay",
             "conv": "thiscall", "args": render_args, "max": 14},
            {"addr": UPDATE_BUTTONS, "name": "UpdateButtons", "conv": "thiscall",
             "args": [],
             "this_dump": [
                 {"off": hex(STATE), "type": "s32", "label": "state.in"},
                 {"off": hex(SELECTED), "type": "s32", "label": "selBtn.in"},
             ],
             "max": 6},
        ],
    }


def read_original(path):
    """-> (state, selBtn, {slot: {column: value}}, n_photographs, unstable)."""
    recs = [json.loads(l) for l in open(path) if l.strip()]
    shots = [r for r in recs if r.get("tag") == "RenderButton"]
    if not shots:
        return None
    last = shots[-1]
    rows = {}
    for n in range(NBUTTONS):
        rows[n] = {c: last.get("b%d.%s" % (n, c)) for c in COLUMNS}
    keys = [k for k in shots[0] if k.startswith("b")]
    unstable = [k for k in keys
                if len({json.dumps(s.get(k)) for s in shots}) > 1]
    return last.get("state"), last.get("selBtn"), rows, len(shots), unstable


LOG_HDR = re.compile(r"BAR call=(\d+) state=(-?\d+) selBtn=(-?\d+)")
LOG_ROW = re.compile(r"BAR (\d+) ((?:\w+=\S*\s*)+)")


def read_ours(path, which=1):
    """Parse the Iwd2DebugLog side; `which` selects the armed call number."""
    state = sel = None
    rows = {}
    active = False
    for line in open(path, encoding="utf-8", errors="replace"):
        m = LOG_HDR.search(line)
        if m:
            active = int(m.group(1)) == which
            if active:
                state, sel = int(m.group(2)), int(m.group(3))
                rows = {}
            continue
        if not active:
            continue
        m = LOG_ROW.search(line)
        if not m:
            continue
        slot = int(m.group(1))
        vals = {}
        for pair in m.group(2).split():
            k, _, v = pair.partition("=")
            vals[k] = v
        rows[slot] = vals
    if not rows:
        return None
    return state, sel, rows


def fmt_table(state, sel, rows, title):
    out = ["%s   state=%s  selectedButton=%s" % (title, state, sel)]
    out.append("slot  " + "  ".join("%-8s" % c for c in COLUMNS))
    for n in sorted(rows):
        out.append("%-4d  " % n
                   + "  ".join("%-8s" % _s(rows[n].get(c)) for c in COLUMNS))
    return "\n".join(out)


def _s(v):
    return "" if v is None else str(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--emit-hooks", metavar="PATH",
                    help="write the frida_orig.py hook table")
    ap.add_argument("--report", metavar="JSONL",
                    help="print the original's settings table")
    ap.add_argument("--diff", nargs=2, metavar=("JSONL", "DEBUGLOG"),
                    help="original vs ours, field by field")
    ap.add_argument("--call", type=int, default=1,
                    help="which armed RenderButton call to read from the debug log")
    ns = ap.parse_args()

    if ns.emit_hooks:
        spec = build_hooks()
        with open(ns.emit_hooks, "w") as fh:
            json.dump(spec, fh, indent=1)
        n = sum(len(h.get("this_dump", [])) for h in spec["hooks"])
        print("wrote %s (%d hooks, %d dump entries)"
              % (ns.emit_hooks, len(spec["hooks"]), n))

    if ns.report:
        got = read_original(ns.report)
        if got is None:
            print("no RenderButton records in %s" % ns.report)
            return 2
        state, sel, rows, shots, unstable = got
        print(fmt_table(state, sel, rows, "ORIGINAL"))
        print("\n%d photographs; fields that differ across them: %s"
              % (shots, ", ".join(unstable) if unstable else "none"))

    if ns.diff:
        jsonl, log = ns.diff
        got = read_original(jsonl)
        ours = read_ours(log, ns.call)
        if got is None or ours is None:
            print("missing one side (original=%s ours=%s)"
                  % (got is not None, ours is not None))
            return 2
        ostate, osel, orows, _, _ = got
        ustate, usel, urows = ours
        print(fmt_table(ostate, osel, orows, "ORIGINAL"))
        print()
        print(fmt_table(ustate, usel, urows, "OURS    "))
        print()
        if (ostate, osel) != (ustate, usel):
            print("!! state/selectedButton differ -- the two runs are NOT "
                  "comparable (state %s/%s, sel %s/%s)"
                  % (ostate, ustate, osel, usel))
        bad = 0
        for n in range(NBUTTONS):
            for c in COLUMNS:
                o, u = _s(orows.get(n, {}).get(c)), _s(urows.get(n, {}).get(c))
                if o != u:
                    bad += 1
                    print("slot %-2d %-5s original=%-10s ours=%s" % (n, c, o, u))
        print("\n%d field(s) differ out of %d" % (bad, NBUTTONS * len(COLUMNS)))
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
