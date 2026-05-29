#!/usr/bin/env python3
"""Diff two frida_group_trace.py logs (original IWD2.exe vs our iwd2-re.exe).

Aligns events per PARTY SLOT and prints a compact divergence table for the same
right-click-drag-WEST move. Slot-bearing events (sprite hooks) carry `slot`
directly; FindPath/AdjustTarget (non-sprite `this`) are attributed to the nearest
slot by grid position. Any orig/ours mismatch is flagged with '*'.

Usage:
  python scripts/frida_diff_group.py
  python scripts/frida_diff_group.py tmp_frida_group_original.log tmp_frida_group_ours.log
"""
import sys
import os
import json

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ATTR_THRESHOLD = 24   # max manhattan dist (grid cells) to attribute a pos to a slot


def load(path):
    rows = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("ERROR"):
                continue
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    return rows


def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def summarize(rows):
    """Return a per-slot summary dict + the committed formation params."""
    slot_pos = {}     # slot -> list of known [x,y] (for attributing FindPath/Adjust)
    s = {}            # slot -> aggregate

    def slot(i):
        if i not in s:
            s[i] = dict(start=None, final=None, facing=None,
                        findpath=0, settarget=0, jump=0, clearbump=0,
                        fp_rc=[], fp_n=[], adj=[], jumps=[])
            slot_pos[i] = []
        return s[i]

    def note_pos(i, p):
        if p and isinstance(p, list) and len(p) == 2:
            slot_pos.setdefault(i, []).append(p)

    # First pass: slot-bearing events establish positions per slot.
    for r in rows:
        t = r.get("tag")
        i = r.get("slot")
        if i is None or i < 0:
            continue
        d = slot(i)
        if t == "MEMBER":
            if d["start"] is None:
                d["start"] = r["pos"]
            note_pos(i, r.get("pos"))
        elif t == "AIWALK":
            d["final"] = r["pos"]
            note_pos(i, r["pos"])
        elif t == "FACE":
            d["facing"] = r.get("facing")
            note_pos(i, r.get("pos"))
            if r.get("pos"):
                d["final"] = r["pos"]
        elif t == "SETTARGET":
            d["settarget"] += 1
        elif t == "JUMP":
            d["jump"] += 1
            d["jumps"].append(r.get("dest"))
        elif t == "CLEARBUMP":
            d["clearbump"] += 1

    def attribute(pos):
        best, bestd = None, 1 << 30
        for i, pts in slot_pos.items():
            for p in pts:
                dd = manhattan(pos, p)
                if dd < bestd:
                    bestd, best = dd, i
        return best if bestd <= ATTR_THRESHOLD else None

    # Second pass: attribute non-slot events (FindPath, Adjust) by position.
    commit = {"GST": None, "GPP": None, "GDM": None}
    for r in rows:
        t = r.get("tag")
        if t in ("GST", "GPP", "GDM"):
            commit[t] = dict(target=r.get("target"), cursor=r.get("cursor"),
                             formationType=r.get("formationType"), members=r.get("members"))
        elif t == "FINDPATH":
            i = attribute(r.get("start", [0, 0]))
            if i is not None:
                d = slot(i)
                d["findpath"] += 1
                d["fp_rc"].append(r.get("rc"))
                d["fp_n"].append(r.get("n"))
        elif t == "ADJUST":
            i = attribute(r.get("start", [0, 0]))
            if i is not None:
                slot(i)["adj"].append(r.get("passable"))
    return s, commit


def fmt(v):
    return "-" if v is None else json.dumps(v, separators=(",", ":"))


def cell(a, b):
    """Side-by-side 'orig|ours', star on mismatch."""
    star = "" if a == b else " *"
    return f"{fmt(a)} | {fmt(b)}{star}"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    p_orig = args[0] if len(args) > 0 else os.path.join(REPO, "tmp_frida_group_original.log")
    p_ours = args[1] if len(args) > 1 else os.path.join(REPO, "tmp_frida_group_ours.log")
    for p in (p_orig, p_ours):
        if not os.path.exists(p):
            sys.exit(f"[!] missing log: {p}")

    so, co = summarize(load(p_orig))
    su, cu = summarize(load(p_ours))

    out = []
    out.append("=== COMMIT / FORMATION (orig | ours) ===")
    for k in ("GST", "GPP", "GDM"):
        a, b = co.get(k), cu.get(k)
        if a is None and b is None:
            continue
        a = a or {}
        b = b or {}
        out.append(f"[{k}]")
        out.append(f"  target        {cell(a.get('target'), b.get('target'))}")
        out.append(f"  cursor        {cell(a.get('cursor'), b.get('cursor'))}")
        out.append(f"  formationType {cell(a.get('formationType'), b.get('formationType'))}")
        out.append(f"  members       {cell(a.get('members'), b.get('members'))}")

    out.append("")
    out.append("=== PER-SLOT (orig | ours; * = divergence) ===")
    slots = sorted(set(so) | set(su))
    fields = [("start", "start"), ("final", "final"), ("facing", "facing"),
              ("findpath", "nFindPath"), ("settarget", "nSetTarget"),
              ("jump", "nJump"), ("clearbump", "nClearBump")]
    for i in slots:
        a = so.get(i, {})
        b = su.get(i, {})
        out.append(f"slot {i}:")
        for key, label in fields:
            out.append(f"  {label:11} {cell(a.get(key), b.get(key))}")
        out.append(f"  {'fp_rc':11} {cell(a.get('fp_rc'), b.get('fp_rc'))}")
        out.append(f"  {'fp_nodes':11} {cell(a.get('fp_n'), b.get('fp_n'))}")
        out.append(f"  {'adjust_ok':11} {cell(a.get('adj'), b.get('adj'))}")

    # Divergence summary
    out.append("")
    out.append("=== DIVERGENCES ===")
    diverged = False
    for k in ("GPP", "GDM", "GST"):
        a, b = co.get(k) or {}, cu.get(k) or {}
        for f in ("target", "cursor", "formationType", "members"):
            if a.get(f) != b.get(f):
                diverged = True
                out.append(f"  commit[{k}].{f}: {fmt(a.get(f))} != {fmt(b.get(f))}")
    for i in slots:
        a, b = so.get(i, {}), su.get(i, {})
        for key, label in fields + [("fp_rc", "fp_rc"), ("fp_n", "fp_nodes"), ("adj", "adjust_ok")]:
            if a.get(key) != b.get(key):
                diverged = True
                out.append(f"  slot {i}.{label}: {fmt(a.get(key))} != {fmt(b.get(key))}")
    if not diverged:
        out.append("  (none — full party behaviour matches within captured layers)")

    text = "\n".join(out)
    print(text)
    with open(os.path.join(REPO, "tmp_frida_diff.txt"), "w") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
