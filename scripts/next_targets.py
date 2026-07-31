#!/usr/bin/env python3
"""Rank the ~18.5k functions of IWD2.exe that are not recovered yet.

Until now "what next?" was answered once per session, by reasoning over
`gb unimplemented` + caller counts + judgement, and the answer survived only as
a sentence at the end of a commit message. This computes it instead, from data
the repo already has, and writes a top-N that survives a clone.

  scripts/next_targets.py                       top 25 to stdout
  scripts/next_targets.py --near CGameSprite    the holes in one class
  scripts/next_targets.py --write               refresh docs/next-targets.md
  scripts/next_targets.py --check               exit 1 if a listed target is now recovered
  scripts/next_targets.py --json out.json       the full ranking, every score term

Ranking (weights are constants below; every term lands in the JSON so a
ranking can be audited rather than trusted):

  +40 log2(1+recovered_callers)  a hole our own C++ already calls -- the only
                                 term backed by an observable defect
  +25 vtable_hole                an unrecovered slot in a mostly-done class.
                                 NOT decorative: _index.json reports
                                 num_callers 0 for every virtual-dispatch-only
                                 method (CGameSprite::Render included), so the
                                 call graph is structurally blind to virtuals,
                                 i.e. to most of this engine. vtable_map.json
                                 is the only signal that sees them.
  +15 class_completion           finishing a class makes its audits clean and
                                 reuses context already in the author's head
  +10 size_band                  byte-weighted .text is the headline metric, but
                                 a 12KB function is a session and a 20-byte thunk
                                 is worth nothing -- a band, not a monotone
   +8 log2(1+num_callers)        global hub-ness, as a tiebreak
  -10 log2(1+unrecovered_callees) drags in N new stubs; prefer near-leaves

There is no placeholder penalty: a FUN_ target is not a worse recovery target,
it is a different task, so it gets its own bucket instead of a fudge weight.
(A flat -12 was meaningless anyway against a +40*log2(585) caller term.)

Three buckets, never blended, because they are three kinds of work:

  Recover   a NAMED function with no `// 0xADDR` marker in src/
  Identify  a FUN_/sub_ placeholder -- you cannot recover what you have not
            identified, so this is a Ghidra naming pass first
  Complete  a `// TODO: Incomplete.` / `// PARTIAL:` site: a lie already in the
            tree, relevance guaranteed, but completing is not creating

Scope note. _index.json holds 29,664 Ghidra "functions", but 19,599 of them are
`Unwind@...` SEH funclets and 33 are `Catch@...` blocks -- compiler-generated
exception plumbing that nobody recovers. Excluding those (and thunks) the real
population is ~10,018, of which ~8,386 are recovered. So the honest remaining
backlog is ~1,632 functions, not the ~18.5k that 29,664 minus the marker count
suggests -- and the 37% headline in README/project_status.py is measured against
a denominator that is two thirds SEH funclets.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXPORTS = REPO / ".ghidra-exports"
INDEX = EXPORTS / "_index.json"
VTABLE_MAP = EXPORTS / "vtable_map.json"
CALLGRAPH = EXPORTS / "_callgraph.json"
OUT_MD = REPO / "docs" / "next-targets.md"
OUT_JSON = EXPORTS / "next_targets.json"

W_RECOVERED_CALLERS = 40
W_VTABLE_HOLE = 25
W_CLASS_COMPLETION = 15
W_SIZE_BAND = 10
W_CALLERS = 8
W_UNRECOVERED_CALLEES = -10

# A class counts as "mostly done" -- and therefore its remaining slots as holes
# worth filling -- above this recovered-slot ratio.
VTABLE_HOLE_MIN_COMPLETION = 0.60

# Unwind@/Catch@ are SEH funclets and thunk_ is a jump stub: compiler output,
# not code anyone recovers. They are 19,639 of the 29,664 index entries, so
# leaving them in makes the backlog look twelve times larger than it is.
SKIP_NAME_RE = re.compile(
    r"^(thunk_|_|__|\?\?_|std::|operator new|operator delete|"
    r"FID_|Catch|Unwind|__Init|_Init)")
MIN_SIZE = 16


def as_addr(v) -> int | None:
    """Export refs are not all plain addresses: imports appear as
    `EXTERNAL:00000122`. Anything that is not an in-image address is dropped."""
    if not isinstance(v, str) or ":" in v:
        return None
    try:
        n = int(v, 16)
    except ValueError:
        return None
    return n if 0x400000 <= n < 0x1000000 else None


def sh(*args: str) -> str:
    try:
        return subprocess.run(["git", *args], cwd=REPO, capture_output=True,
                              text=True, timeout=120).stdout
    except (OSError, subprocess.TimeoutExpired):
        return ""


# --------------------------------------------------------------------------
# inputs
# --------------------------------------------------------------------------

def recovered_addrs() -> set[int]:
    """Addresses carrying a `// 0xADDR` marker in src/. git grep over the
    working tree, so it cannot disagree with what is actually checked in."""
    out = sh("grep", "-ohE", r"//\s*0x[0-9A-Fa-f]{5,8}", "--", "src")
    return {int(m, 16) for m in re.findall(r"0x([0-9A-Fa-f]{5,8})", out)}


def load_index() -> dict[int, dict]:
    data = json.loads(INDEX.read_text(encoding="utf-8"))
    return {int(k, 16): v for k, v in data.items()}


def load_callgraph(index: dict[int, dict], rebuild: bool = False) -> dict[int, list[int]]:
    """addr -> list of caller addrs.

    _index.json only carries counts; the caller LISTS live in the 29k
    per-function exports, so this walks them once and caches the result.
    """
    if CALLGRAPH.exists() and not rebuild:
        try:
            raw = json.loads(CALLGRAPH.read_text(encoding="utf-8"))
            if len(raw) >= len(index) * 0.9:
                return {int(k, 16): [int(a, 16) for a in v] for k, v in raw.items()}
        except (json.JSONDecodeError, OSError, ValueError):
            pass

    print(f"building call graph from {len(index)} exports (once; cached in "
          f"{CALLGRAPH.relative_to(REPO)}) ...", file=sys.stderr)
    graph: dict[str, list[str]] = {}
    for addr in index:
        key = f"{addr:08x}"
        p = EXPORTS / f"{key}.json"
        if not p.exists():
            continue
        try:
            d = json.loads(p.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            continue
        callers = [f"{a:08x}" for a in
                   (as_addr(c.get("addr")) for c in (d.get("callers") or []))
                   if a is not None]
        graph[key] = callers
    CALLGRAPH.write_text(json.dumps(graph), encoding="utf-8")
    return {int(k, 16): [int(a, 16) for a in v] for k, v in graph.items()}


def load_callees(addr: int) -> list[int]:
    p = EXPORTS / f"{addr:08x}.json"
    if not p.exists():
        return []
    try:
        d = json.loads(p.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return []
    return [a for a in (as_addr(c.get("addr")) for c in (d.get("callees") or []))
            if a is not None]


def load_vtables() -> tuple[dict[int, str], dict[str, float]]:
    """(addr -> owning class for unrecovered slots, class -> recovered ratio)."""
    if not VTABLE_MAP.exists():
        return {}, {}
    data = json.loads(VTABLE_MAP.read_text(encoding="utf-8"))
    holes: dict[int, str] = {}
    completion: dict[str, float] = {}
    for cls, info in data.items():
        slots = info.get("slots") or {}
        if not slots:
            continue
        done = sum(1 for s in slots.values() if s.get("recovered"))
        completion[cls] = done / len(slots)
        for s in slots.values():
            if s.get("recovered") or not s.get("addr"):
                continue
            try:
                holes[int(s["addr"], 16)] = cls
            except ValueError:
                continue
    return holes, completion


def sizes_from_index(index: dict[int, dict]) -> dict[int, int]:
    """No export carries a size, so approximate it by the gap to the next entry
    address -- the same trick project_status.py validates its byte coverage on."""
    ordered = sorted(index)
    out = {}
    for i, a in enumerate(ordered):
        nxt = ordered[i + 1] if i + 1 < len(ordered) else a
        out[a] = max(0, min(nxt - a, 65536))
    return out


def stub_sites() -> list[dict]:
    """`// TODO: Incomplete.` / `// PARTIAL:` -- known-incomplete code already in
    the tree. Guaranteed relevant, but completing is not creating, so these are
    ranked separately and never blended into the main list."""
    out = []
    raw = sh("grep", "-n", "-E", r"//\s*(TODO: Incomplete\.|PARTIAL:)", "--", "src")
    for line in raw.splitlines():
        parts = line.split(":", 2)
        if len(parts) < 3:
            continue
        out.append({"file": parts[0], "line": int(parts[1]),
                    "text": parts[2].strip()[:100]})
    return out


# --------------------------------------------------------------------------
# scoring
# --------------------------------------------------------------------------

def size_band(n: int) -> float:
    if n < 32:
        return 0.15
    if n < 200:
        return 0.6
    if n <= 2000:
        return 1.0
    if n <= 6000:
        return 0.75
    return 0.5


def score_all(index, recovered, graph, holes, completion, sizes) -> list[dict]:
    rows = []
    for addr, meta in index.items():
        if addr in recovered:
            continue
        name = meta.get("name") or f"FUN_{addr:08x}"
        if SKIP_NAME_RE.match(name):
            continue
        size = sizes.get(addr, 0)
        hole_cls = holes.get(addr)
        if size < MIN_SIZE and not hole_cls:
            continue

        callers = graph.get(addr, [])
        rec_callers = sum(1 for c in callers if c in recovered)
        num_callers = meta.get("num_callers") or len(callers)
        is_fun = name.startswith(("FUN_", "sub_"))

        cls = hole_cls or (name.split("::")[0] if "::" in name else None)
        comp = completion.get(cls, 0.0) if cls else 0.0
        vhole = 1.0 if (hole_cls and comp >= VTABLE_HOLE_MIN_COMPLETION) else 0.0

        # Only paid for candidates that could plausibly rank; reading 18.5k
        # export files for the callee term would cost more than the term is worth.
        unrec_callees = 0
        prelim = (W_RECOVERED_CALLERS * math.log2(1 + rec_callers)
                  + W_VTABLE_HOLE * vhole
                  + W_CLASS_COMPLETION * comp
                  + W_SIZE_BAND * size_band(size)
                  + W_CALLERS * math.log2(1 + num_callers))
        if prelim > 30:
            callees = load_callees(addr)
            unrec_callees = sum(1 for c in callees if c not in recovered)
        score = prelim + W_UNRECOVERED_CALLEES * math.log2(1 + unrec_callees)

        rows.append({
            "address": f"0x{addr:06x}" if addr < 0x1000000 else f"0x{addr:08x}",
            "addr_int": addr,
            "bucket": "identify" if is_fun else "recover",
            "name": name,
            "class": cls,
            "size": size,
            "num_callers": num_callers,
            "recovered_callers": rec_callers,
            "unrecovered_callees": unrec_callees,
            "vtable_hole": bool(vhole),
            "vtable_class_completion": round(comp, 3),
            "is_placeholder": is_fun,
            "score": round(score, 2),
        })
    # Deterministic: score desc, then address asc -- so regenerating without a
    # data change produces a byte-identical file.
    rows.sort(key=lambda r: (-r["score"], r["addr_int"]))
    return rows


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------

def fmt_table(rows: list[dict], limit: int) -> list[str]:
    out = ["| score | address | size | callers | symbol | why |",
           "|------:|---------|-----:|---------|--------|-----|"]
    for r in rows[:limit]:
        why = []
        if r["vtable_hole"]:
            why.append("vtable hole")
        if r["recovered_callers"]:
            why.append(f"{r['recovered_callers']} recovered caller(s)")
        if r["is_placeholder"]:
            why.append("needs naming")
        out.append(
            f"| {r['score']:.2f} | `{r['address']}` | {r['size']} | "
            f"{r['num_callers']} ({r['recovered_callers']}R) | "
            f"`{r['name']}` | {', '.join(why) or '-'} |")
    return out


def scope_stats(index: dict[int, dict], recovered: set[int]) -> dict:
    """The honest denominator: what is actually recoverable, funclets excluded."""
    real = rec = funclets = 0
    for a, m in index.items():
        name = m.get("name") or ""
        if name.startswith(("Unwind", "Catch", "thunk_")):
            funclets += 1
            continue
        real += 1
        if a in recovered:
            rec += 1
    return {"real": real, "recovered": rec, "remaining": real - rec,
            "funclets": funclets, "index_total": len(index)}


def write_markdown(rows, stubs, recovered, index, path: Path, top: int) -> None:
    head_sha = sh("rev-parse", "--short", "HEAD").strip() or "unknown"
    mtime = ""
    if INDEX.exists():
        import datetime
        mtime = datetime.date.fromtimestamp(INDEX.stat().st_mtime).isoformat()
    st = scope_stats(index, recovered)
    recov = [r for r in rows if r["bucket"] == "recover"]
    ident = [r for r in rows if r["bucket"] == "identify"]

    lines = [
        "<!-- GENERATED by scripts/next_targets.py -- do not edit by hand.",
        f"     commit {head_sha}   _index.json {mtime}   "
        f"markers {len(recovered)}, matched to index entries {st['recovered']}",
        f"     weights: recovered_callers={W_RECOVERED_CALLERS} "
        f"vtable_hole={W_VTABLE_HOLE} class_completion={W_CLASS_COMPLETION} "
        f"size={W_SIZE_BAND} callers={W_CALLERS} "
        f"unrec_callees={W_UNRECOVERED_CALLEES}",
        "     Refresh: scripts/next_targets.py --write   Staleness: --check -->",
        "",
        "# Next targets",
        "",
        f"**Scope.** `_index.json` holds {st['index_total']:,} Ghidra functions, "
        f"but {st['funclets']:,} of them are `Unwind@`/`Catch@` SEH funclets and "
        f"thunks -- compiler-generated exception plumbing nobody recovers. The "
        f"real population is **{st['real']:,}**, of which **{st['recovered']:,} "
        f"are recovered ({100 * st['recovered'] / max(1, st['real']):.1f}%)**, "
        f"leaving **{st['remaining']:,}**.",
        "",
        "> The ~37% figure in `README.md` / `project_status.py` is measured "
        "against the {:,}-entry denominator, which is two thirds SEH funclets."
        .format(st["index_total"]),
        "",
        "Three buckets, because they are three kinds of work. Full ranking with "
        "every score term: `.ghidra-exports/next_targets.json` (gitignored).",
        "",
        "Regenerate after a recovery arc -- deliberately not wired to a hook, "
        "which would put an unrelated file in every commit and hard-fail for "
        "anyone without `.ghidra-exports/`.",
        "",
        f"## Recover ({len(recov)})",
        "",
        "Named functions with no `// 0xADDR` marker in `src/`.",
        "",
    ]
    lines += fmt_table(recov, top)
    lines += [
        "",
        f"## Identify ({len(ident)})",
        "",
        "`FUN_`/`sub_` placeholders. You cannot recover what you have not "
        f"identified, so these need a Ghidra naming pass first. Top {top}:",
        "",
    ]
    lines += fmt_table(ident, top)
    lines += [
        "",
        f"## Complete ({len(stubs)})",
        "",
        "`// TODO: Incomplete.` / `// PARTIAL:` sites -- code already in the "
        "tree that knowingly diverges. First 20:",
        "",
        "| file:line | marker |",
        "|-----------|--------|",
    ]
    for s in stubs[:20]:
        lines.append(f"| `{s['file']}:{s['line']}` | {s['text']} |")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def cmd_check(rows_path: Path, recovered: set[int]) -> int:
    """Exit 1 when a listed target has since been recovered -- a 0.3s probe that
    tells `arc status` the file is out of date."""
    if not OUT_MD.exists():
        print("docs/next-targets.md missing -> scripts/next_targets.py --write")
        return 1
    text = OUT_MD.read_text(encoding="utf-8")
    listed = {int(m, 16) for m in re.findall(r"`0x([0-9a-fA-F]{5,8})`", text)}
    done = listed & recovered
    if done:
        print(f"{len(done)} listed target(s) are now recovered "
              f"-> scripts/next_targets.py --write")
        for a in sorted(done)[:10]:
            print(f"  0x{a:06x}")
        return 1
    print(f"docs/next-targets.md is current ({len(listed)} targets, none recovered)")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--near", help="only this class (and its vtable holes)")
    ap.add_argument("--write", action="store_true", help="refresh docs/next-targets.md")
    ap.add_argument("--check", action="store_true", help="exit 1 if the md is stale")
    ap.add_argument("--json", nargs="?", const=str(OUT_JSON),
                    help="write the full ranking as JSON")
    ap.add_argument("--rebuild", action="store_true", help="rebuild the call-graph cache")
    ap.add_argument("--scope", action="store_true",
                    help="print the honest recoverable-scope counts as JSON and exit")
    args = ap.parse_args(argv)

    if not INDEX.exists():
        print(f"{INDEX.relative_to(REPO)} missing -- needs .ghidra-exports/",
              file=sys.stderr)
        return 2

    recovered = recovered_addrs()
    if args.check:
        return cmd_check(OUT_MD, recovered)
    if args.scope:
        print(json.dumps(scope_stats(load_index(), recovered)))
        return 0

    index = load_index()
    graph = load_callgraph(index, args.rebuild)
    holes, completion = load_vtables()
    sizes = sizes_from_index(index)
    rows = score_all(index, recovered, graph, holes, completion, sizes)
    stubs = stub_sites()

    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1), encoding="utf-8")
        print(f"wrote {args.json} ({len(rows)} ranked)")

    if args.write:
        write_markdown(rows, stubs, recovered, index, OUT_MD, args.top or 50)
        print(f"wrote {OUT_MD.relative_to(REPO)} "
              f"(top {args.top or 50} of {len(rows)}, {len(stubs)} stub sites)")
        return 0

    st = scope_stats(index, recovered)
    cls_stubs = []
    if args.near:
        cls = args.near
        rows = [r for r in rows if r["class"] == cls]
        comp = completion.get(cls)
        nholes = sum(1 for r in rows if r["vtable_hole"])
        if comp is not None:
            print(f"{cls}  {comp * 100:.0f}% of vtable slots recovered, "
                  f"{nholes} hole(s) left")
        else:
            print(f"{cls}  (no vtable in vtable_map.json)")
        cls_stubs = [s for s in stubs if Path(s["file"]).stem == cls]
    else:
        print(f"real scope {st['recovered']:,}/{st['real']:,} recovered "
              f"({100 * st['recovered'] / max(1, st['real']):.1f}%), "
              f"{st['remaining']:,} left "
              f"-- {st['funclets']:,} SEH funclets/thunks excluded")

    for bucket, title in (("recover", "RECOVER (named)"),
                          ("identify", "IDENTIFY (FUN_/sub_ -- name it first)")):
        sel = [r for r in rows if r["bucket"] == bucket]
        if not sel:
            continue
        print(f"\n  == {title}: {len(sel)} ==")
        print(f"  {'score':>6}  {'address':<10} {'size':>5}  {'callers':<9} what")
        for r in sel[:args.top]:
            why = "vtable hole" if r["vtable_hole"] else ""
            callers = f"{r['num_callers']} ({r['recovered_callers']}R)"
            print(f"  {r['score']:6.1f}  {r['address']:<10} {r['size']:5}  "
                  f"{callers:<9} {r['name']}  {why}".rstrip())
    if not rows:
        print("  (nothing matched)")
    if cls_stubs:
        print(f"\n  COMPLETE: {len(cls_stubs)} `TODO: Incomplete` in {args.near} "
              f"-> lines {', '.join(str(s['line']) for s in cls_stubs[:8])}")
    print(f"\n  {len(rows)} candidate(s); full ranking: --json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
