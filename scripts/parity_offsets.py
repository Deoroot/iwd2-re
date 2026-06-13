#!/usr/bin/env python3
"""Member-offset parity: catch "right callee, right count, WRONG member" bugs.

`re-agent parity` checks callees and call-counts -- both match for the whole
class of bugs where a recovered function calls the right method on the WRONG
member (e.g. the 3-yr-old corpse-tint bug: ClearColorEffectsAll's extend block
called DeleteResPaletteAffect on m_*VidCellBase instead of m_*VidCellExtend).
The only discriminating signal is the per-thiscall `this` OFFSET, which the
high-level decompile masks but the asm exposes cleanly:

    lea ecx, [esi + 0x4EC]      ; this = &member-at-0x4EC
    call CVidCell::DeleteResPaletteAffect

This tool extracts the SET of `this`-relative offsets the ORIGINAL binary
function touches, maps them to members via the class header `/* 0xNNN */`
comments (the recovery's own name->offset annotations -- no PDB needed), and
diffs against the members the C++ source actually references. A member the
BINARY touches but the SOURCE never names = a wrong/missing member.

    .venv-reagent/bin/python scripts/parity_offsets.py 0x6E6490
    .venv-reagent/bin/python scripts/parity_offsets.py 0x6E6490 --count 700
    .venv-reagent/bin/python scripts/parity_offsets.py 0x6E6490 --src src/Foo.cpp:1802-1909 --class CGameAnimationTypeMonsterIcewind

Needs capstone (in .venv-reagent). Run from repo root.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "src"
SYM = REPO / "scripts" / "sym.py"
SRC_FIND = REPO / "scripts" / "src_find.py"

# `this` is parked in one of these by the prologue (`mov <reg>, ecx`).
THIS_REGS = ("esi", "edi", "ebx", "ebp")
ECX_LEA_RE = re.compile(r"\b(?:lea|mov)\s+ecx,\s*\[(e[a-z][a-z]) \+ (0x[0-9a-fA-F]+)\]")
ECX_OTHER_RE = re.compile(r"\bmov\s+ecx,")          # ecx set from something else -> this unknown
CALL_RE = re.compile(r"\bcall\b.*?;\s*(\S+)")        # call ... ; Class::Method
LINE_ADDR_RE = re.compile(r"^0x([0-9a-fA-F]+):")     # disasm line address
ADDR_COMMENT_RE = re.compile(r"//\s*0x([0-9A-Fa-f]+)\b")  # `// 0xADDR` recovery markers
MOV_THIS_RE = re.compile(r"\bmov\s+(e[a-z][a-z]),\s+ecx\b")
DECL_RE = re.compile(r"/\*\s*([0-9A-Fa-f]+)\s*\*/\s*[^;{]*?\bm_(\w+)\s*(?:\[[^\]]*\])?\s*;")
# member method call: m_foo.Bar(  /  m_foo->Bar(
MEMBER_CALL_RE = re.compile(r"\bm_(\w+)\s*(?:\.|->)\s*(\w+)\s*\(")


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def disasm(addr: int, count: int) -> list[str]:
    out = run([str(REPO / ".venv-reagent/bin/python"), str(SYM), "disasm", hex(addr), str(count)])
    return out.splitlines()


def binary_call_pairs(addr: int, count: int, end_addr: int | None) -> tuple[Counter, str]:
    """Multiset of (this_offset, method) for each thiscall: `lea ecx,[this+off]; call M`.

    Stops at end_addr (the next function start) so it never bleeds into the next
    function and invents pairs it never makes.
    """
    lines = disasm(addr, count)
    this_reg = "esi"
    for ln in lines[:14]:
        m = MOV_THIS_RE.search(ln)
        if m and m.group(1) in THIS_REGS:
            this_reg = m.group(1)
            break

    pairs: Counter = Counter()
    pending = None  # this-offset staged for the next call
    for ln in lines:
        if end_addr is not None:
            am = LINE_ADDR_RE.match(ln)
            if am and int(am.group(1), 16) >= end_addr:
                break
        m = ECX_LEA_RE.search(ln)
        if m:
            pending = int(m.group(2), 16) if m.group(1) == this_reg else None
            continue
        if ECX_OTHER_RE.search(ln):  # ecx loaded from non-this -> unknown receiver
            pending = None
            continue
        c = CALL_RE.search(ln)
        if c:
            if pending is not None:
                method = c.group(1).split("::")[-1].split("__")[-1]
                pairs[(pending, method)] += 1
            pending = None
    return pairs, this_reg


def header_offset_map(class_name: str) -> dict[str, int]:
    """member-name -> declared offset, from `/* 0xNNN */ ... m_name;` comments.

    Walks the class header + (shallow) base headers it includes.
    """
    out: dict[str, int] = {}
    seen: set[Path] = set()

    def absorb(hpath: Path):
        if hpath in seen or not hpath.exists():
            return
        seen.add(hpath)
        text = hpath.read_text(errors="replace")
        for m in DECL_RE.finditer(text):
            out.setdefault(f"m_{m.group(2)}", int(m.group(1), 16))

    # primary header
    cands = list(SRC.glob(f"{class_name}.h"))
    for c in cands:
        absorb(c)
    # base headers (best-effort: any `: public Base` in the .cpp/.h)
    for c in cands:
        for bm in re.finditer(r":\s*(?:public|protected|private)?\s*(\w+)", c.read_text(errors="replace")):
            absorb(SRC / f"{bm.group(1)}.h")
    return out


def resolve_source(addr: int):
    """(file, start, end, class) via src_find.py 0xADDR."""
    out = run(["python3", str(SRC_FIND), hex(addr)])
    m = re.search(r"(\S+\.cpp):(\d+)\s+0x[0-9a-fA-F]+\s+\[(\d+)-(\d+)\]", out)
    cls = None
    cm = re.search(r"\b(\w+)::\w+", out)
    if cm:
        cls = cm.group(1)
    if not m:
        return None
    return REPO / m.group(1), int(m.group(3)), int(m.group(4)), cls


def function_end(path: Path, addr: int) -> int | None:
    """Next-greater `// 0xADDR` recovery marker in the file = the next function start."""
    addrs = sorted({int(m.group(1), 16) for m in ADDR_COMMENT_RE.finditer(path.read_text(errors="replace"))})
    nexts = [a for a in addrs if a > addr]
    return nexts[0] if nexts else None


def source_pairs(path: Path, start: int, end: int) -> Counter:
    lines = path.read_text(errors="replace").splitlines()
    body = "\n".join(lines[start - 1 : end])
    return Counter((f"m_{m.group(1)}", m.group(2)) for m in MEMBER_CALL_RE.finditer(body))


def method_adj(offsets: list[int], hvals: set[int]) -> int:
    """Per-method base-subobject shift: this-offset = member_offset + adj (CVidImage=+4, CVidCell=+0)."""
    deltas: Counter = Counter()
    for off in offsets:
        for d in (0, 4, 8, 12, 16):
            if off - d in hvals:
                deltas[d] += 1
    return deltas.most_common(1)[0][0] if deltas else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("addr", help="binary function address, e.g. 0x6E6490")
    ap.add_argument("--count", type=int, default=600, help="instructions to disasm")
    ap.add_argument("--src", help="file.cpp:start-end (override src_find)")
    ap.add_argument("--class", dest="cls", help="class name (override)")
    args = ap.parse_args()

    addr = int(args.addr, 16)

    if args.src:
        f, rng = args.src.split(":")
        s, e = rng.split("-")
        path, start, end, cls = REPO / f, int(s), int(e), args.cls
    else:
        r = resolve_source(addr)
        if not r:
            print(f"src_find could not resolve {args.addr}; pass --src", file=sys.stderr)
            return 2
        path, start, end, cls = r
        if args.cls:
            cls = args.cls

    if not cls:
        print("could not determine class; pass --class", file=sys.stderr)
        return 2

    end_addr = function_end(path, addr)
    count = args.count
    if end_addr is not None:
        count = max(count, (end_addr - addr) // 2 + 32)  # ensure the disasm reaches end_addr
    pairs, this_reg = binary_call_pairs(addr, count, end_addr)
    hdr = header_offset_map(cls)
    if not hdr:
        print(f"no `/* 0xNNN */ m_*` offset comments found for {cls}", file=sys.stderr)
        return 2
    off2name = {off: name for name, off in hdr.items()}
    hvals = set(hdr.values())

    # per-method base-subobject shift, then map each binary (offset, method) -> (member, method)
    by_method: dict[str, list[int]] = {}
    for (off, meth), n in pairs.items():
        by_method.setdefault(meth, []).extend([off] * n)
    adj = {meth: method_adj(offs, hvals) for meth, offs in by_method.items()}

    bin_pairs: Counter = Counter()       # (member, method) the binary calls
    unmapped: Counter = Counter()
    for (off, meth), n in pairs.items():
        name = off2name.get(off - adj[meth])
        if name:
            bin_pairs[(name, meth)] += n
        else:
            unmapped[(off, meth)] += n

    src = source_pairs(path, start, end)
    missing = sorted(set(bin_pairs) - set(src))

    print(f"== offset parity {args.addr}  {cls}  ({path.name}:{start}-{end})")
    print(f"   this-reg={this_reg}  binary thiscalls mapped to {len(bin_pairs)} (member,method) pair(s), "
          f"source has {len(set(src))}")
    if missing:
        print("\n  !! WRONG/MISSING MEMBER -- binary calls these, source never does:")
        for name, meth in missing:
            off = hdr[name]
            print(f"     {name}.{meth}()  (member 0x{off:X}, this 0x{off + adj[meth]:X}, {bin_pairs[(name, meth)]}x in binary)")
        print("\n  Likely a copy-paste calling the method on a sibling member. Verify vs Ghidra and fix.")
    else:
        print("  OK: every (member,method) thiscall in the binary is present in the source.")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
