#!/usr/bin/env python3
"""progress_badges.py - how much of IWD2.exe is left, per subsystem.

Every real function of the binary (Ghidra's entries minus the SEH funclets and
thunks, the same denominator as project_status.py) is put in one SUBSYSTEM and
measured on three axes:

  recovered  the address has a `// 0xADDR` marker in src/ -- C++ exists for it.
             Also weighted by bytes, so a 2000-instruction function counts for
             what it is and a 5-byte stub for what it is.
  complete   recovered AND its body carries no `TODO: Incomplete`.
  faithful   recovered AND the offline parity sweep (parity_cache_sweep.py
             --json) left it GREEN.  That sweep runs only the decompile-based
             signals, so GREEN here is "no faithfulness break the cheap lint
             can see", NOT "proved against the binary".  Session 53's Cast
             Spell button was recovered, complete and GREEN, and still wrong.

How a function gets its class, in order:
  1. .ghidra-exports/address_map.json (the source-linked subset),
  2. the `Class::` prefix of its Ghidra name,
  3. the src/ function carrying its marker (qualifier, else the file name),
  4. its NEIGHBOURS: MSVC links each .obj contiguously, so an anonymous
     FUN_ sitting between two functions of the same class is that class's.
     Counted separately as "inferred" so the report says how much rests on it.
Anything still unplaced lands in "Unclassified", which is reported, not hidden.

Classes map to subsystems through scripts/progress_groups.json: an ordered list
of {name, patterns}, first regex match wins.  Edit that file to regroup; the
`--classes` dump shows what is not matched yet.

Usage (repo root):
  python scripts/progress_badges.py                  # table on stdout
  python scripts/progress_badges.py --classes        # class -> count, unmatched first
  python scripts/progress_badges.py --write          # docs/badges/*.svg + README block
  python scripts/parity_cache_sweep.py --json docs/badges/parity.json   # refresh fidelity (~8 min)

Pure stdlib apart from what the parity sweep needs.  No Ghidra boot, no network.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import project_status  # noqa: E402  (same denominator, same marker scan)
import src_find  # noqa: E402  (function bodies, for TODO: Incomplete)

EXPORTS = os.path.join(ROOT, ".ghidra-exports")
GROUPS = os.path.join(ROOT, "scripts", "progress_groups.json")
BADGES = os.path.join(ROOT, "docs", "badges")
PARITY = os.path.join(BADGES, "parity.json")
README = os.path.join(ROOT, "README.md")
BEGIN = "<!-- progress-badges:begin -->"
END = "<!-- progress-badges:end -->"

INCOMPLETE_RE = re.compile(r"TODO:\s?Incomplete")
HEX8 = re.compile(r"^[0-9a-fA-F]{8}\.json$")


# --------------------------------------------------------------------------
# data

def load_functions():
    """{addr: ghidra name} for every real function, plus {addr: size}."""
    names = {}
    for fn in os.listdir(EXPORTS):
        if not HEX8.match(fn):
            continue
        addr = int(fn[:8], 16)
        try:
            with open(os.path.join(EXPORTS, fn), encoding="utf-8", errors="replace") as fh:
                d = json.load(fh)
        except (OSError, ValueError):
            continue
        n = d.get("name") or d.get("function_name") or ""
        if n.startswith(project_status.FUNCLET_PREFIXES):
            names[addr] = None          # kept for sizing, excluded from counts
            continue
        names[addr] = n
    alla = sorted(names)
    sizes = {a: alla[i + 1] - a for i, a in enumerate(alla[:-1])}
    sizes[alla[-1]] = 16
    real = {a: n for a, n in names.items() if n is not None}
    return real, sizes


def load_source():
    """{addr: (qual, file, incomplete)} for every marked function in src/."""
    cache = src_find.load_index()
    out = {}
    for s in src_find.all_syms(cache):
        if not s.get("addr"):
            continue
        try:
            addr = int(s["addr"], 16)
        except ValueError:
            continue
        incomplete = False
        path = os.path.join(ROOT, s["file"])
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
            body = "".join(lines[s["line"] - 1:(s.get("end") or s["line"])])
            incomplete = bool(INCOMPLETE_RE.search(body))
        except OSError:
            pass
        prev = out.get(addr)
        # A header declaration and a .cpp body can share an address: keep the
        # body, and let any Incomplete mark win.
        if prev is None or (prev[1].endswith(".h") and not s["file"].endswith(".h")):
            out[addr] = (s.get("qual") or "", s["file"], incomplete or (prev[2] if prev else False))
        elif incomplete:
            out[addr] = (prev[0], prev[1], True)
    # Markers the index does not attach to a definition still count as
    # recovered (project_status.py counts every `// 0xADDR`).
    for a in project_status.scan_source()["recovered_addr_ints"]:
        out.setdefault(a, ("", "", False))
    return out


def load_parity():
    if not os.path.isfile(PARITY):
        return None
    with open(PARITY, encoding="utf-8") as fh:
        d = json.load(fh)
    return {int(a, 16): v for a, v in d.get("verdicts", {}).items()}, d.get("generated", "")


def classify(real, source):
    amap = {}
    p = os.path.join(EXPORTS, "address_map.json")
    if os.path.isfile(p):
        with open(p, encoding="utf-8") as fh:
            amap = {int(k, 16): v for k, v in json.load(fh).items()}
    cls, how = {}, {}
    for a, n in real.items():
        c = (amap.get(a) or {}).get("class")
        if not c and "::" in n:
            c = n.split("::")[0]
        if not c and a in source:
            q, f, _ = source[a]
            c = q or (os.path.splitext(os.path.basename(f))[0] if f else None)
        if c:
            cls[a], how[a] = c, "named"
    # Neighbour inference over the address-ordered list.
    order = sorted(real)
    known = [i for i, a in enumerate(order) if a in cls]
    for k in range(len(known) - 1):
        i, j = known[k], known[k + 1]
        if j - i > 1 and cls[order[i]] == cls[order[j]]:
            for m in range(i + 1, j):
                cls[order[m]], how[order[m]] = cls[order[i]], "inferred"
    for a in real:
        if a not in cls:
            cls[a], how[a] = None, "none"
    return cls, how


LIBRARY = "Runtime library"


def load_groups():
    with open(GROUPS, encoding="utf-8") as fh:
        g = json.load(fh)
    return [(e["name"], [re.compile(p) for p in e["patterns"]]) for e in g["groups"]]


def library_start():
    """Everything from here to the end of .text is linked-in library code --
    the tail of zlib, import thunks, then MFC and the C runtime.  Nobody
    recovers it, so it is reported as its own row and kept OUT of the game
    totals."""
    with open(GROUPS, encoding="utf-8") as fh:
        return int(json.load(fh).get("library_start", "0xFFFFFFFF"), 16)


def library_ranges():
    """Library code linked in BELOW `library_start`, as half-open [lo, hi)
    pairs.  zlib 1.1.2 is split in three by the link order: compress and
    uncompress after CCrypt, deflate.c + inflate.c before InitOpenGL, and the
    rest (zutil, trees, adler32, inf*) right up to the import thunks."""
    with open(GROUPS, encoding="utf-8") as fh:
        return [(int(lo, 16), int(hi, 16))
                for lo, hi in json.load(fh).get("library_ranges", [])]


def group_of(c, groups):
    if not c:
        return "Unclassified"
    for name, pats in groups:
        if any(p.search(c) for p in pats):
            return name
    return "Unclassified"


# --------------------------------------------------------------------------
# measurement

def measure():
    real, sizes = load_functions()
    source = load_source()
    par = load_parity()
    verdicts, generated = par if par else ({}, "")
    cls, how = classify(real, source)
    groups = load_groups()
    lib = library_start()
    lib_ranges = library_ranges()

    rows = defaultdict(Counter)
    for a in real:
        in_lib = a >= lib or any(lo <= a < hi for lo, hi in lib_ranges)
        g = LIBRARY if in_lib else group_of(cls[a], groups)
        r = rows[g]
        sz = sizes.get(a, 0)
        r["fns"] += 1
        r["bytes"] += sz
        r["inferred"] += how[a] == "inferred"
        if a in source:
            r["rec"] += 1
            r["rec_bytes"] += sz
            if not source[a][2]:
                r["complete"] += 1
            v = verdicts.get(a)
            if v == "UNMATCHED":
                r["unmatched"] += 1
                v = None
            if v == "GREEN":
                r["green"] += 1
            elif v == "RED":
                r["red"] += 1
            if v is not None:
                r["swept"] += 1
    total = Counter()
    for n, r in rows.items():
        if n != LIBRARY:
            total.update(r)
    order = [n for n, _ in groups] + ["Unclassified"]
    return [(n, rows[n]) for n in order if rows[n]["fns"]], total, generated, cls, how, rows[LIBRARY]


def pct(a, b):
    return 100.0 * a / b if b else 0.0


# --------------------------------------------------------------------------
# rendering

def colour(p):
    if p is None:
        return "#9f9f9f"
    # Same bands as the reference image: red, orange, yellow-green, green.
    if p >= 85:
        return "#4c1"
    if p >= 60:
        return "#97ca00"
    if p >= 30:
        return "#dfb317"
    if p >= 15:
        return "#fe7d37"
    return "#e05d44"


# Advance widths of Verdana at 11px, rounded; anything unlisted gets the
# lowercase average.  Close enough that the text never leaves its box.
_NARROW = {" ": 3.9, ",": 4.0, ".": 4.0, "·": 4.0, "/": 4.9, "(": 4.9, ")": 4.9,
           "i": 3.1, "l": 3.1, "j": 3.4, "f": 3.9, "t": 4.3, "r": 4.7, "&": 8.0,
           "%": 11.9, "m": 10.7, "w": 8.9, "M": 9.6, "W": 10.8}


def text_width(s):
    w = 0.0
    for ch in s:
        if ch in _NARROW:
            w += _NARROW[ch]
        elif ch.isdigit():
            w += 7.0
        elif ch.isupper():
            w += 7.6
        else:
            w += 6.6
    return int(w + 0.5) + 12


def svg_badge(label, value, p):
    lw, vw = text_width(label), text_width(value)
    w = lw + vw
    c = colour(p)
    esc = lambda s: s.replace("&", "&amp;").replace("<", "&lt;")
    # Ids are per badge, so several badges inlined into one page do not all
    # clip to the first one's width.
    k = slug(label)
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="20" role="img" '
        f'aria-label="{esc(label)}: {esc(value)}">'
        f'<title>{esc(label)}: {esc(value)}</title>'
        f'<linearGradient id="s-{k}" x2="0" y2="100%"><stop offset="0" stop-color="#bbb" stop-opacity=".1"/>'
        f'<stop offset="1" stop-opacity=".1"/></linearGradient>'
        f'<clipPath id="r-{k}"><rect width="{w}" height="20" rx="3" fill="#fff"/></clipPath>'
        f'<g clip-path="url(#r-{k})"><rect width="{lw}" height="20" fill="#555"/>'
        f'<rect x="{lw}" width="{vw}" height="20" fill="{c}"/>'
        f'<rect width="{w}" height="20" fill="url(#s-{k})"/></g>'
        f'<g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="11">'
        f'<text x="{lw / 2:.1f}" y="15" fill="#010101" fill-opacity=".3">{esc(label)}</text>'
        f'<text x="{lw / 2:.1f}" y="14">{esc(label)}</text>'
        f'<text x="{lw + vw / 2:.1f}" y="15" fill="#010101" fill-opacity=".3">{esc(value)}</text>'
        f'<text x="{lw + vw / 2:.1f}" y="14">{esc(value)}</text></g></svg>'
    )


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def summary_badges(total):
    t = total
    return [
        ("recovered", f"{t['rec']:,}/{t['fns']:,} · {pct(t['rec'], t['fns']):.0f}%", pct(t["rec"], t["fns"])),
        ("code bytes", f"{pct(t['rec_bytes'], t['bytes']):.1f}%", pct(t["rec_bytes"], t["bytes"])),
        ("complete", f"{t['complete']:,}/{t['fns']:,} · {pct(t['complete'], t['fns']):.0f}%", pct(t["complete"], t["fns"])),
        ("parity green", f"{t['green']:,}/{t['swept']:,} · {pct(t['green'], t['swept']):.0f}%", pct(t["green"], t["swept"]))
        if t["swept"] else ("parity green", "not measured", None),
    ]


def table(rows, total, generated, library=None):
    out = []
    hdr = "| Subsystem | Functions | Recovered | Bytes | Complete | Parity GREEN | Parity RED | Left |"
    out.append(hdr)
    out.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    extra = [(f"*{LIBRARY} (not counted)*", library)] if library and library["fns"] else []
    for name, r in rows + [("**Total (game code)**", total)] + extra:
        fid = f"{pct(r['green'], r['swept']):.0f}%" if r["swept"] else "—"
        out.append(
            f"| {name} | {r['fns']:,} | {r['rec']:,} ({pct(r['rec'], r['fns']):.0f}%) "
            f"| {pct(r['rec_bytes'], r['bytes']):.0f}% "
            f"| {r['complete']:,} ({pct(r['complete'], r['fns']):.0f}%) "
            f"| {fid} | {r['red'] if r['swept'] else '—'} | {r['fns'] - r['rec']:,} |"
        )
    return out


def readme_block(rows, total, generated, library):
    lines = [BEGIN, ""]
    lines.append(" ".join(f"![{l}](docs/badges/{slug(l)}.svg)" for l, _, _ in summary_badges(total)))
    lines.append("")
    lines.append(" ".join(f"![{n}](docs/badges/group-{slug(n)}.svg)" for n, _ in rows))
    lines.append("")
    lines += table(rows, total, generated, library)
    lines.append("")
    sweep = f"sweep of {generated}" if generated else "no sweep yet"
    unmatched = (f" {total['unmatched']:,} recovered functions are left out of both parity columns "
                 f"because the sweep could not match them to a source body."
                 if total["unmatched"] else "")
    lines.append(
        f"*Generated by `scripts/progress_badges.py --write`. **Recovered** = C++ exists "
        f"(a `// 0xADDR` marker). **Complete** = recovered with no `TODO: Incomplete`, as a share "
        f"of ALL functions. **Parity GREEN** = share of the recovered functions the offline parity "
        f"sweep finds no fault in ({sweep}), and **Parity RED** = how many it flags as a likely "
        f"faithfulness break -- a lint, not a proof against the binary.{unmatched} "
        f"{total['inferred']:,} functions are placed in a subsystem by their neighbours' class, not "
        f"their own name; edit `scripts/progress_groups.json` to regroup. Linked-in library code "
        f"(import thunks, MFC, the C runtime) is shown but kept out of every total, which is why the "
        f"totals here are lower than `project_status.py`'s, which count it.*"
    )
    lines += ["", END]
    return "\n".join(lines)


def write_all(rows, total, generated, library):
    os.makedirs(BADGES, exist_ok=True)
    for label, value, p in summary_badges(total):
        with open(os.path.join(BADGES, f"{slug(label)}.svg"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(svg_badge(label, value, p))
    for name, r in rows:
        value = f"{r['rec']:,}/{r['fns']:,} · {pct(r['rec'], r['fns']):.0f}%"
        with open(os.path.join(BADGES, f"group-{slug(name)}.svg"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(svg_badge(name, value, pct(r["rec"], r["fns"])))

    with open(README, "rb") as fh:
        raw = fh.read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8")
    block = readme_block(rows, total, generated, library)
    if crlf:
        block = block.replace("\n", "\r\n")
    if BEGIN in text and END in text:
        i, j = text.index(BEGIN), text.index(END) + len(END)
        text = text[:i] + block + text[j:]
    else:
        nl = "\r\n" if crlf else "\n"
        text = text.rstrip() + nl + nl + "## Progress by subsystem" + nl + nl + block + nl
    with open(README, "wb") as fh:
        fh.write(text.encode("utf-8"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true", help="write docs/badges/*.svg and the README block")
    ap.add_argument("--classes", action="store_true", help="class -> function count, by subsystem")
    ap.add_argument("--json", action="store_true")
    ns = ap.parse_args()
    # The Windows console is cp1252; the table carries an em dash and a middle dot.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    rows, total, generated, cls, how, library = measure()

    if ns.classes:
        groups = load_groups()
        per = defaultdict(Counter)
        for a, c in cls.items():
            per[group_of(c, groups)][c or "(none)"] += 1
        for g in [n for n, _ in groups] + ["Unclassified"]:
            if not per[g]:
                continue
            print(f"== {g}  ({sum(per[g].values())})")
            for c, k in per[g].most_common():
                print(f"   {k:5}  {c}")
        return 0
    if ns.json:
        print(json.dumps({"total": total, "groups": {n: r for n, r in rows}, "parity": generated}, indent=1))
        return 0

    for line in table(rows, total, generated, library):
        print(line)
    print(f"\ninferred by neighbours: {total['inferred']:,}   parity sweep: {generated or 'none'}")
    if ns.write:
        write_all(rows, total, generated, library)
        print(f"wrote {BADGES} and the README block")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
