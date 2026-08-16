#!/usr/bin/env python3
"""ITEM_EFFECT field audit: catch "right value, WRONG member" at ClearItemEffect sites.

`ClearItemEffect` zeroes all 0x30 bytes of an ITEM_EFFECT, so every call site
must re-store each field it needs before handing the struct to `DecodeEffect`.
Two bug classes live in that window, and NEITHER is visible to `re-agent parity`
(the call set is identical) nor to `parity_offsets.py` (which diffs `this`-relative
members, while an ITEM_EFFECT is usually a stack local):

  1. a DROPPED store   -- the binary writes `durationType = 1`, the source doesn't,
     so the effect expires at birth (the day-blindness / nondetection / combat-feat
     bug class, 7 instances found by hand);
  2. a MIS-ATTRIBUTED store -- right value, wrong field, e.g. `0x46 -> duration
     (+0x0E)` recovered as `savingThrow (+0x24)` (six instances in ResolveDamage).

This tool does mechanically what those hunts did by hand: for each ClearItemEffect
call in the ORIGINAL binary it resolves the struct's base, walks forward to the
matching `DecodeEffect`, maps every store landing inside the struct to an
ITEM_EFFECT field name, and diffs that set against the fields the C++ source
assigns at the corresponding site.

    .venv-reagent/bin/python scripts/lint_item_effect.py 0x726570
    .venv-reagent/bin/python scripts/lint_item_effect.py --sweep
    .venv-reagent/bin/python scripts/lint_item_effect.py --sweep "CGameSprite.cpp" -v

THE ESP PROBLEM, AND WHY THIS TOOL IS TRUSTWORTHY WHERE HAND-TRACING WASN'T.
A stack-local struct's stores are all `[esp + K]`, and esp moves under every push.
Hand-tracing that is where two prior sessions produced contradictory offsets. The
fix is not better arithmetic, it is a CHECK: the effect pointer is `lea`'d TWICE
(once for ClearItemEffect, once for DecodeEffect) at different esp values, so the
second `lea` independently re-derives the base. If both derivations agree the
walk is sound; if they disagree the tool reports UNSURE and audits nothing, rather
than inventing a finding. Heap-allocated effects (`new ITEM_EFFECT`, base held in
a register) need no esp tracking at all and are always sound.

Needs capstone (.venv-reagent). Run from repo root.
"""
from __future__ import annotations

import argparse
import bisect
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "src"
SYM = REPO / "scripts" / "sym.py"
PY = REPO / ".venv-reagent" / "bin" / "python"

CLEAR_ITEM_EFFECT = 0x4A2E00
DECODE_EFFECT = 0x48C800
SIZEOF_ITEM_EFFECT = 0x30
# `operator new` -- its eax is the base of a HEAP ITEM_EFFECT, which needs no esp
# tracking at all, so those sites are always sound.
OPERATOR_NEW = 0x7FC95B
# WORD 0 in the little ordinal table at 0x8491A8. A site loading it is storing an
# EXPLICIT zero into a field ClearItemEffect already zeroed, so leaving it out of
# the source is faithful -- exactly the distinction that separates the 0x8491AA
# (WORD 1) durationType bugs from their harmless neighbours.
ZERO_GLOBALS = {0x8491A8}

SUBREGS = {
    "eax": ("ax", "al", "ah"), "ebx": ("bx", "bl", "bh"),
    "ecx": ("cx", "cl", "ch"), "edx": ("dx", "dl", "dh"),
    "esi": ("si",), "edi": ("di",), "ebp": ("bp",),
}

# offset -> (name, size); from src/BalDataTypes.h `typedef struct Item_effect_st`.
ITEM_EFFECT_FIELDS = [
    (0x00, "effectID", 2),
    (0x02, "targetType", 1),
    (0x03, "spellLevel", 1),
    (0x04, "effectAmount", 4),
    (0x08, "dwFlags", 4),
    (0x0C, "durationType", 2),
    (0x0E, "duration", 4),
    (0x12, "probabilityUpper", 1),
    (0x13, "probabilityLower", 1),
    (0x14, "res", 8),
    (0x1C, "numDice", 4),
    (0x20, "diceSize", 4),
    (0x24, "savingThrow", 4),
    (0x28, "saveMod", 4),
    (0x2C, "special", 4),
]
FIELD_NAMES = {n for _, n, _ in ITEM_EFFECT_FIELDS}

LINE_RE = re.compile(r"^0x([0-9a-fA-F]+):\s+(\S+)\s*(.*?)\s*(?:;.*)?$")
ADDR_COMMENT_RE = re.compile(r"//\s*0x([0-9A-Fa-f]+)\b")
FUNC_SIG_RE = re.compile(r"^[A-Za-z][\w:<>,\*&\s]*?\b(\w+)::~?(\w+)\s*\(")
# capstone prints displacements below 10 bare (`[edi + 4]`) and larger ones as hex
# (`[esp + 0x28]`) -- a regex demanding `0x` silently drops every small-offset store,
# which for an ITEM_EFFECT means missing effectAmount at +4.
DISP = r"([-+]) (0x[0-9a-fA-F]+|\d+)"
LEA_ESP_RE = re.compile(rf"^(e[a-z][a-z]),\s*\[esp {DISP}\]$")
LEA_REG_RE = re.compile(rf"^(e[a-z][a-z]),\s*\[(e[a-z][a-z]) {DISP}\]$")
# `mov dword ptr [esp + 0x28], eax` / `mov word ptr [esp + 0xf8], ax` / `mov [edi + 4], dx`
STORE_RE = re.compile(
    rf"^(?:(byte|word|dword|qword) ptr )?\[(e[a-z][a-z])(?: {DISP})?\],\s*(.+)$"
)


ZERO_GLOBAL_RE = re.compile(r"\[(0x[0-9a-fA-F]+)\]")


def _reads_zero_global(src: str) -> bool:
    m = ZERO_GLOBAL_RE.search(src)
    return bool(m) and int(m.group(1), 16) in ZERO_GLOBALS


def signed(sign: str | None, value: str | None) -> int:
    if not value:
        return 0
    n = int(value, 0)
    return -n if sign == "-" else n
SUB_ADD_ESP_RE = re.compile(r"^esp,\s*(0x[0-9a-fA-F]+|\d+)$")
MOV_REG_REG_RE = re.compile(r"^(e[a-z][a-z]),\s*(\S+)$")

PTR_SIZE = {"byte": 1, "word": 2, "dword": 4, "qword": 8}

# How far past ClearItemEffect to look for the matching DecodeEffect before giving up.
MAX_WINDOW = 400


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def python_exe() -> str:
    return str(PY) if PY.exists() else sys.executable


def disasm(addr: int, count: int) -> list[tuple[int, str, str]]:
    """[(addr, mnemonic, operands)] -- BOM/CRLF tolerated (Windows redirects add both)."""
    out = run([python_exe(), str(SYM), "disasm", hex(addr), str(count)])
    insns = []
    for ln in out.splitlines():
        m = LINE_RE.match(ln.lstrip("﻿").rstrip())
        if m:
            insns.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return insns


GHIDRA_EXPORTS = REPO / ".ghidra-exports"
_EXPORT_ADDRS: list[int] | None = None


def export_addrs() -> list[int]:
    """Sorted candidate function starts from Ghidra's export set, keeping only
    16-BYTE-ALIGNED addresses.

    Ghidra also defines functions at mid-body addresses (shared tails, blocks it
    reached only by a jump): 0x73DDD5 sits inside CGameSprite::ResolveDamage, and
    taking it as the boundary truncates the scan to 2 of that function's 9
    ClearItemEffect sites. MSVC aligns real entry points to 16 here -- 7100 of the
    7101 top-level `// 0xADDR` markers in src/ are aligned (the lone exception is
    an MFC thunk) -- so alignment separates the two cleanly. Erring long is also
    the safe direction: an overshoot inflates the site count and reports a COUNT
    MISMATCH (audit by hand), it never invents a DROPPED field.
    """
    global _EXPORT_ADDRS
    if _EXPORT_ADDRS is None:
        out = []
        if GHIDRA_EXPORTS.is_dir():
            for p in GHIDRA_EXPORTS.glob("*.json"):
                try:
                    a = int(p.stem, 16)
                except ValueError:
                    continue
                if a % 16 == 0:
                    out.append(a)
        _EXPORT_ADDRS = sorted(out)
    return _EXPORT_ADDRS


def function_end(addr: int, marker_addrs: list[int]) -> int | None:
    exps = export_addrs()
    i = bisect.bisect_right(exps, addr)
    if i < len(exps):
        return exps[i]
    nexts = [a for a in marker_addrs if a > addr]
    return nexts[0] if nexts else None


def field_of(off: int) -> str | None:
    """Field containing byte offset `off` (a RESREF store at +0x18 is still `res`)."""
    for base, name, size in ITEM_EFFECT_FIELDS:
        if base <= off < base + size:
            return name
    return None


class Frame:
    """esp simulation + which registers currently hold a pointer into the frame.

    `sp` counts bytes below the function-entry esp. Only push/pop/add/sub touch it;
    a `call` is assumed not to (callee-cleaned thiscall args would break that, which
    is exactly what the DecodeEffect anchor check below catches).
    """

    def __init__(self) -> None:
        self.sp = 0
        self.ok = True
        # reg -> ("frame", absolute_frame_offset) | ("heap<N>", byte offset into it)
        self.regs: dict[str, tuple[str, int]] = {}
        self.pushes: list[tuple[str, int] | None] = []
        self.push_text: list[str] = []
        self.n_alloc = 0
        # registers provably holding 0 right now -- a store of one of these into an
        # ITEM_EFFECT field re-zeroes what ClearItemEffect already zeroed
        self.zero: set[str] = set()

    def clobber(self, reg: str) -> None:
        self.regs.pop(reg, None)
        for r in (reg,) + SUBREGS.get(reg, ()):
            self.zero.discard(r)
        for wide, subs in SUBREGS.items():
            if reg in subs:
                self.zero.discard(wide)
                self.zero.discard(reg)

    def set_zero(self, reg: str) -> None:
        self.zero.add(reg)
        for r in SUBREGS.get(reg, ()):
            self.zero.add(r)

    def is_zero(self, rhs: str) -> bool:
        rhs = rhs.strip()
        if re.fullmatch(r"0|0x0+", rhs):
            return True
        return rhs in self.zero

    def step(self, mnem: str, ops: str) -> None:
        if mnem == "push":
            self.sp -= 4
            self.pushes.append(self.regs.get(ops.strip()))
            self.push_text.append(ops.strip())
            return
        if mnem == "pop":
            self.sp += 4
            self.clobber(ops.strip())
            if self.pushes:
                self.pushes.pop()
                self.push_text.pop()
            if ops.strip() == "esp":
                self.ok = False
            return
        if mnem in ("sub", "add") and ops.startswith("esp,"):
            m = SUB_ADD_ESP_RE.match(ops)
            if not m:
                self.ok = False
                return
            n = int(m.group(1), 0)
            self.sp += -n if mnem == "sub" else n
            return
        if mnem == "leave" or (mnem == "mov" and ops.startswith("esp,")):
            self.ok = False
            return
        if mnem == "lea":
            m = LEA_ESP_RE.match(ops)
            if m:
                self.regs[m.group(1)] = ("frame", self.sp + signed(m.group(2), m.group(3)))
                return
            m = LEA_REG_RE.match(ops)
            if m and m.group(2) in self.regs:
                kind, val = self.regs[m.group(2)]
                self.regs[m.group(1)] = (kind, val + signed(m.group(3), m.group(4)))
                return
            self.clobber(ops.split(",")[0].strip())
            return
        if mnem == "call":
            # return value lands in eax; every caller-saved reg is suspect afterwards
            for r in ("eax", "ecx", "edx"):
                self.clobber(r)
            m = re.fullmatch(r"0x([0-9a-fA-F]+)", ops.strip())
            if m and int(m.group(1), 16) == OPERATOR_NEW:
                self.n_alloc += 1
                self.regs["eax"] = (f"heap{self.n_alloc}", 0)
            return
        if mnem == "xor":
            dst, _, src = ops.partition(",")
            dst, src = dst.strip(), src.strip()
            self.clobber(dst)
            if dst == src:
                self.set_zero(dst)
            return
        if mnem in ("mov", "movzx", "movsx"):
            dst, _, src = ops.partition(",")
            dst, src = dst.strip(), src.strip()
            if "[" in dst:
                return  # a store, not a register write
            self.clobber(dst)
            if re.fullmatch(r"e[a-z][a-z]", dst) and src in self.regs:
                self.regs[dst] = self.regs[src]  # propagate a base through `mov edi, eax`
            if self.is_zero(src) or _reads_zero_global(src):
                self.set_zero(dst)
            return
        if mnem in ("or", "and", "inc", "dec", "imul", "idiv", "add", "sub", "sar", "shl", "shr"):
            dst = ops.split(",")[0].strip()
            if "[" not in dst:
                self.clobber(dst)

    def resolve(self, reg: str, disp: int) -> tuple[str, int] | None:
        """Absolute (kind, offset) a `[reg + disp]` memory operand points at."""
        if reg == "esp":
            return ("frame", self.sp + disp) if self.ok else None
        if reg in self.regs:
            kind, val = self.regs[reg]
            return (kind, val + disp)
        return None


class Site:
    def __init__(self, addr: int) -> None:
        self.addr = addr
        self.decode_addr: int | None = None
        self.base: tuple[str, int] | None = None
        self.heap = False
        self.stores: list[tuple[int, str, str]] = []  # (field offset, field name, rhs)
        self.unsure: str | None = None
        self.id_via_arg = False
        self.effect_id: int | None = None

    @property
    def fields(self) -> set[str]:
        f = {n for _, n, _ in self.stores}
        if self.id_via_arg:
            f.add("effectID")
        return f


def scan_binary(addr: int, end_addr: int | None) -> list[Site]:
    """Every ClearItemEffect site in [addr, end_addr) with the fields it stores."""
    count = 4000 if end_addr is None else max(64, (end_addr - addr) // 2 + 64)
    insns = disasm(addr, min(count, 20000))

    fr = Frame()
    sites: list[Site] = []
    open_site: Site | None = None
    window = 0

    for ia, mnem, ops in insns:
        if end_addr is not None and ia >= end_addr:
            break

        if mnem == "call":
            target = None
            m = re.fullmatch(r"0x([0-9a-fA-F]+)", ops.strip())
            if m:
                target = int(m.group(1), 16)

            if target == CLEAR_ITEM_EFFECT:
                site = Site(ia)
                # cdecl: ClearItemEffect(pEffect, effectID) -> pEffect pushed LAST,
                # effectID the one before it. A non-zero id means the call itself sets
                # effectID, so a source that passes it is not "missing" that field.
                site.base = fr.pushes[-1] if fr.pushes else None
                if len(fr.push_text) >= 2:
                    site.effect_id = resolve_id(fr.push_text[-2])
                    if not fr.is_zero(fr.push_text[-2]):
                        site.id_via_arg = True
                if site.base is None:
                    site.unsure = "could not resolve the ITEM_EFFECT pointer argument"
                else:
                    site.heap = site.base[0].startswith("heap")
                sites.append(site)
                open_site = site
                window = 0
                fr.step(mnem, ops)
                continue

            if target == DECODE_EFFECT and open_site is not None:
                # DecodeEffect(pEffect, ...) -- pEffect pushed LAST, an INDEPENDENT
                # re-derivation of the base. Disagreement means the esp walk drifted.
                got = fr.pushes[-1] if fr.pushes else None
                open_site.decode_addr = ia
                if open_site.base is not None and got != open_site.base:
                    open_site.unsure = (
                        f"base disagrees at DecodeEffect ({open_site.base} vs {got}) "
                        "-- esp walk drifted, audit this site by hand"
                    )
                open_site = None
                fr.step(mnem, ops)
                continue

        if open_site is not None:
            window += 1
            if window > MAX_WINDOW:
                open_site.unsure = open_site.unsure or "no DecodeEffect within the window"
                open_site = None
            elif mnem == "lea" and open_site.base is not None:
                # A field filled by a HELPER (`memcpy(&effect.res, src, 8)`) has no
                # store instruction at all -- only its address being taken. Missing
                # this reads as a dropped resref on every site that copies one.
                lm = LEA_ESP_RE.match(ops) or LEA_REG_RE.match(ops)
                if lm:
                    g = lm.groups()
                    tgt = (("frame", fr.sp + signed(g[1], g[2])) if len(g) == 3
                           else fr.resolve(g[1], signed(g[2], g[3])))
                    if tgt is not None and tgt[0] == open_site.base[0]:
                        off = tgt[1] - open_site.base[1]
                        if 0 < off < SIZEOF_ITEM_EFFECT:
                            name = field_of(off)
                            if name:
                                open_site.stores.append((off, name, "address taken (filled by a call)"))
            elif mnem == "mov" and open_site.base is not None:
                m = STORE_RE.match(ops)
                if m:
                    size = PTR_SIZE.get(m.group(1) or "dword", 4)
                    rhs = m.group(5)
                    tgt = fr.resolve(m.group(2), signed(m.group(3), m.group(4)))
                    if tgt is not None and tgt[0] == open_site.base[0]:
                        off = tgt[1] - open_site.base[1]
                        # Re-zeroing a field ClearItemEffect already zeroed is a no-op,
                        # so omitting it in source is faithful, not a dropped store.
                        if (0 <= off < SIZEOF_ITEM_EFFECT
                                and not fr.is_zero(rhs) and not _reads_zero_global(rhs)):
                            name = field_of(off)
                            if name:
                                open_site.stores.append((off, name, f"{rhs} ({size}b)"))

        fr.step(mnem, ops)

    if open_site is not None:
        open_site.unsure = open_site.unsure or "no DecodeEffect found after this site"
    return sites


def enumerate_functions(path: Path):
    """(addr, class, method, start_line, end_line) per top-level recovered fn."""
    lines = path.read_text(errors="replace").splitlines()
    raw = []
    pending = None
    for i, ln in enumerate(lines):
        if ln.startswith("// 0x"):
            m = ADDR_COMMENT_RE.search(ln)
            if m:
                pending = int(m.group(1), 16)
            continue
        if pending is not None and not ln.startswith(("//", " ", "\t", "/*", "*")):
            sm = FUNC_SIG_RE.match(ln)
            if sm and not ln.rstrip().endswith(";"):
                raw.append((pending, sm.group(1), sm.group(2), i))
                pending = None
    out = []
    for j, (addr, cls, meth, di) in enumerate(raw):
        end_line = raw[j + 1][3] - 1 if j + 1 < len(raw) else len(lines)
        out.append((addr, cls, meth, di + 1, end_line))
    return out


# The closing paren is deliberately NOT required: the call is often split across
# lines, and demanding `)` silently drops those sites (it took OnLButtonPressed
# from 4 source sites to 1, which then read as a count mismatch).
CLEAR_CALL_RE = re.compile(r"ClearItemEffect\s*\(\s*&?\s*([\w\[\]\.\->]+)\s*(?:,\s*([^),]+))?")
DECODE_CALL_RE = re.compile(r"DecodeEffect\s*\(")
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/")


DEFINE_RE = re.compile(r"^#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$", re.M)
_DEFINES: dict[str, int] | None = None


def defines() -> dict[str, int]:
    """`#define CGAMEEFFECT_POISON 25` across src/*.h -- lets a source site's effect
    id be compared with the immediate the binary pushes, so sites are paired by WHAT
    THEY ARE rather than by position."""
    global _DEFINES
    if _DEFINES is None:
        out: dict[str, int] = {}
        for h in SRC.glob("*.h"):
            for m in DEFINE_RE.finditer(h.read_text(errors="replace")):
                out.setdefault(m.group(1), int(m.group(2), 0))
        _DEFINES = out
    return _DEFINES


def resolve_id(text: str) -> int | None:
    text = (text or "").strip()
    if re.fullmatch(r"0x[0-9a-fA-F]+|\d+", text):
        return int(text, 0)
    return defines().get(text)


def is_zero_literal(rhs: str | None) -> bool:
    if rhs is None:
        return False
    return re.fullmatch(r"\(?\s*(0|0x0+|FALSE)\s*\)?\s*;?", rhs.strip()) is not None


def strip_comment(line: str) -> str:
    line = BLOCK_COMMENT_RE.sub("", line)
    i = line.find("//")
    return line[:i] if i >= 0 else line


def source_sites(path: Path, start: int, end: int) -> list[tuple[int, str, set[str]]]:
    """(line, varname, fields assigned) per ClearItemEffect site, in source order.

    Fields are collected between the ClearItemEffect call and the DecodeEffect that
    consumes the same variable -- the same window the binary walk uses.
    """
    lines = path.read_text(errors="replace").splitlines()
    # Strip comments: a `// ClearItemEffect ...` note in a recovery comment would
    # otherwise count as a call site and desynchronise the pairing.
    raw = lines[start - 1 : end]
    # A recovered function's source extent ends at its own column-0 `}`. Without
    # this, the range runs to the NEXT `// 0xADDR` marker and swallows any file-static
    # helpers defined in between -- which is why UseDoor appeared to hold a
    # ClearItemEffect call its binary function does not make.
    for j in range(1, len(raw)):
        if strip_comment(raw[j]).startswith("}"):
            raw = raw[: j + 1]
            break
    body = [strip_comment(ln) for ln in raw]
    out = []
    for i, ln in enumerate(body):
        if i == 0:
            continue  # the function's own signature line -- ClearItemEffect's definition
        m = CLEAR_CALL_RE.search(ln)
        if not m:
            continue
        # The codebase marks a call the compiler inlined with `// NOTE: Uninline.`
        # -- the binary makes no call there, so counting it desynchronises the pairing.
        if any("NOTE: Uninline" in raw[k] for k in range(max(0, i - 3), i)):
            continue
        var = m.group(1).lstrip("&")
        arg_id_text = m.group(2)
        if arg_id_text is None:
            # The call is split across lines and the effect id sits on the next
            # one. Without this the id reads as absent, the site reports
            # UNRESOLVED, and a whole function's verdict is thrown away over
            # nothing but line wrapping.
            for ln2 in body[i + 1 : i + 3]:
                m2 = re.match(r"\s*,?\s*([^),]+)\s*\)", ln2)
                if m2:
                    arg_id_text = m2.group(1)
                    break
        # Any MENTION of the field counts, not just an assignment LHS: the resref is
        # filled by handing the field to a helper (`SPIN232.GetResRef(effect.res)`),
        # which an LHS-only regex reads as a dropped store.
        assign = re.compile(
            r"\b" + re.escape(var) + r"\s*(?:\.|->)\s*(\w+)\s*(?:=(?!=)\s*(.*))?"
        )
        fields: set[str] = set()
        arg_id = (arg_id_text or "").strip()
        if arg_id and arg_id != "0":
            fields.add("effectID")  # ClearItemEffect's 2nd argument IS effectID
        for ln2 in body[i + 1 : i + 1 + 60]:
            if DECODE_CALL_RE.search(ln2):
                break
            for fm in assign.finditer(ln2):
                # `effect.savingThrow = 0;` re-writes what ClearItemEffect already
                # zeroed, so the binary legitimately has no store for it -- the exact
                # mirror of the zero-store suppression on the binary side.
                if fm.group(1) in FIELD_NAMES and not is_zero_literal(fm.group(2)):
                    fields.add(fm.group(1))
        out.append((start + i, var, fields, resolve_id(arg_id)))
    return out


def pair_sites(bin_sites, src_sites):
    """Pair binary and source sites by EFFECT ID where both resolve, else by position.

    Position-pairing alone is wrong in branchy functions: ResolveDamage has nine
    sites, and the compiler emits them in an order that puts the source's 5th
    opposite the binary's 6th -- which reads as a pile of DROPPED/EXTRA fields on
    both sites. The effect id identifies each site independently of order.
    """
    if len(bin_sites) != len(src_sites):
        return None
    b_ids = [b.effect_id for b in bin_sites]
    s_ids = [s[3] for s in src_sites]
    if None in b_ids or None in s_ids or sorted(b_ids) != sorted(s_ids):
        return list(zip(bin_sites, src_sites))
    used = set()
    pairs = []
    for s in src_sites:
        for j, b in enumerate(bin_sites):
            if j not in used and b.effect_id == s[3]:
                used.add(j)
                pairs.append((b, s))
                break
    return pairs if len(pairs) == len(src_sites) else list(zip(bin_sites, src_sites))


def id_name(eid: int | None) -> str:
    """`0x88 (CGAMEEFFECT_FORCEVISIBLE)` -- the effect id is what identifies a site."""
    if eid is None:
        return "?"
    for name, val in defines().items():
        if val == eid and name.startswith("CGAMEEFFECT_"):
            return f"0x{eid:X} ({name})"
    return f"0x{eid:X}"


def audit_count_mismatch(bin_sites, src_sites, header, path, verbose):
    """Binary and source disagree on HOW MANY sites there are -> (findings, lines).

    Two very different things produce that, and only one is a bug:

      * the compiler CLONED a block, so one source site is emitted N times.  MSVC
        does this to resolve a later branch early -- `CGameSprite::Spell` tests
        `m_actionID != SPELLNODEC` after the cast-start block and gets two copies
        of the whole block, hence 4 binary sites for 2 source ones.  Every binary
        effect id is still one the source has, so nothing is missing;
      * the binary emits an effect id the source NEVER mentions, i.e. a whole
        effect block is unrecovered.  That is the finding worth chasing.

    Distinguishing them needs only the ids, so do that rather than giving up.
    """
    lines = [header]
    b_ids = [b.effect_id for b in bin_sites]
    s_ids = [s[3] for s in src_sites]
    # An id that does not resolve (held in a register, or a source constant this
    # tool cannot see) is reported on its own line rather than abandoning the whole
    # function: the ids that DO resolve still identify unrecovered blocks.
    unresolved = [b for b in bin_sites if b.effect_id is None]
    unresolved_src = [s for s in src_sites if s[3] is None]
    # A SOURCE site whose id is a variable is exactly what the recovery of a BINARY
    # site whose id is in a register looks like -- neither side folds to a constant,
    # so pair them off and report only the surplus. Without this the tool can never
    # go quiet on a real recovery: 0x592A0B holds 0x1C5/0x1C6 in edi and the source
    # holds them in a local, and reporting both is reporting the same site twice.
    paired = min(len(unresolved), len(unresolved_src))
    unresolved = unresolved[paired:]
    unresolved_src = unresolved_src[paired:]
    unrecovered = sorted(set(b_ids) - set(s_ids) - {None})
    orphaned = sorted(set(s_ids) - set(b_ids) - {None})
    if unrecovered or orphaned or unresolved or unresolved_src:
        lines.append(
            f"  !! SITE COUNT MISMATCH: binary has {len(bin_sites)} ClearItemEffect "
            f"call(s), source has {len(src_sites)}"
        )
        for eid in unrecovered:
            at = ", ".join(f"0x{b.addr:X}" for b in bin_sites if b.effect_id == eid)
            lines.append(
                f"     !! UNRECOVERED  {id_name(eid)}  binary calls at {at}, "
                f"no source site"
            )
        for eid in orphaned:
            at = ", ".join(str(s[0]) for s in src_sites if s[3] == eid)
            lines.append(
                f"     !! ORPHANED     {id_name(eid)}  source site(s) at "
                f"{path.name}:{at}, binary makes no such call"
            )
        for b in unresolved:
            lines.append(
                f"     ?? UNRESOLVED   binary 0x{b.addr:X} passes an effect id this "
                f"tool cannot fold to a constant -- audit by hand"
            )
        for s in unresolved_src:
            lines.append(
                f"     ?? UNRESOLVED   {path.name}:{s[0]} `{s[1]}` passes an effect id "
                f"this tool cannot fold to a constant -- audit by hand"
            )
        return 1, lines

    # Same id SET on both sides, different counts: the compiler cloned. Audit each
    # source site against every clone -- a clone that disagrees with its siblings
    # is itself worth seeing, so none of them is skipped.
    findings = 0
    for ssite in src_sites:
        for bsite in [b for b in bin_sites if b.effect_id == ssite[3]]:
            findings += compare_site(bsite, ssite, path, header, lines, verbose)
    if findings == 0 and not verbose:
        return 0, []
    if findings == 0:
        lines.insert(
            1,
            f"  {len(bin_sites)} binary sites for {len(src_sites)} source site(s) "
            f"-- compiler-cloned block, ids all accounted for",
        )
    return findings, lines


def compare_site(bsite, ssite, path, header, lines, verbose):
    """Field-diff one binary site against its source site -> 0 or 1 finding."""
    findings = 0
    sline, var, sfields, _sid = ssite
    where = f"  {path.name}:{sline}  `{var}`  (binary 0x{bsite.addr:X})"
    if bsite.unsure:
        if verbose:
            if header not in lines:
                lines.append(header)
            lines.append(f"{where}  UNSURE: {bsite.unsure}")
        return 0
    bfields = bsite.fields
    dropped = sorted(bfields - sfields)
    extra = sorted(sfields - bfields)
    if dropped or extra:
        findings += 1
        if header not in lines:
            lines.append(header)
        lines.append(where)
        for name in dropped:
            # `effectID` can come from ClearItemEffect's own second argument
            # rather than from a store, so there is not always a store to cite.
            hits = [(o, r) for o, n, r in bsite.stores if n == name]
            if hits:
                lines.append(
                    f"     !! DROPPED  {name} (+0x{hits[0][0]:02X})  binary stores: "
                    + "; ".join(r for _, r in hits)
                )
            else:
                lines.append(
                    f"     !! DROPPED  {name}  passed as ClearItemEffect's second "
                    f"argument (0x{bsite.effect_id:X})"
                    if bsite.effect_id is not None
                    else f"     !! DROPPED  {name}"
                )
        for name in extra:
            lines.append(
                f"     !! EXTRA    {name}  assigned in source, never stored by the binary"
            )
    elif verbose:
        if header not in lines:
            lines.append(header)
        lines.append(f"{where}  ok  [{', '.join(sorted(bfields)) or 'no stores'}]")
    return findings


def audit_function(addr, cls, meth, path, start, end, marker_addrs, verbose):
    """-> (n_findings, printed_lines)."""
    src_sites = source_sites(path, start, end)
    if not src_sites:
        return 0, []
    bin_sites = scan_binary(addr, function_end(addr, marker_addrs))

    lines = []
    findings = 0
    header = f"== {cls}::{meth}  0x{addr:X}  ({path.name}:{start}-{end})"

    if len(bin_sites) != len(src_sites):
        return audit_count_mismatch(bin_sites, src_sites, header, path, verbose)

    for (bsite, (sline, var, sfields, _sid)) in pair_sites(bin_sites, src_sites):
        where = f"  {path.name}:{sline}  `{var}`  (binary 0x{bsite.addr:X})"
        if bsite.unsure:
            if verbose:
                lines.append(header) if header not in lines else None
                lines.append(f"{where}  UNSURE: {bsite.unsure}")
            continue
        bfields = bsite.fields
        dropped = sorted(bfields - sfields)
        extra = sorted(sfields - bfields)
        if dropped or extra:
            findings += 1
            if header not in lines:
                lines.append(header)
            lines.append(where)
            for name in dropped:
                rhs = "; ".join(r for _, n, r in bsite.stores if n == name)
                off = next(o for o, n, _ in bsite.stores if n == name)
                lines.append(
                    f"     !! DROPPED  {name} (+0x{off:02X})  binary stores: {rhs}"
                )
            for name in extra:
                lines.append(
                    f"     !! EXTRA    {name}  assigned in source, never stored by the binary"
                )
        elif verbose:
            if header not in lines:
                lines.append(header)
            lines.append(f"{where}  ok  [{', '.join(sorted(bfields)) or 'no stores'}]")
    return findings, lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("addr", nargs="?", help="binary function address, e.g. 0x726570")
    ap.add_argument("--sweep", nargs="*", metavar="FILE",
                    help="scan every recovered fn in these src files (default: all)")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="also print clean and UNSURE sites")
    args = ap.parse_args()

    if not export_addrs():
        print("warning: .ghidra-exports/ is empty; function bounds fall back to "
              "source markers and may overshoot", file=sys.stderr)

    if args.sweep is not None:
        pats = args.sweep or ["*.cpp"]
        files: list[Path] = []
        for pat in pats:
            p = Path(pat)
            files.extend([p] if p.exists() else sorted(SRC.glob(pat)))
        total = 0
        checked = 0
        for f in sorted(set(files)):
            if "ClearItemEffect" not in f.read_text(errors="replace"):
                continue
            marker_addrs = sorted(
                {int(m.group(1), 16) for m in ADDR_COMMENT_RE.finditer(f.read_text(errors="replace"))}
            )
            for a, cls, meth, s, e in enumerate_functions(f):
                n, out = audit_function(a, cls, meth, f, s, e, marker_addrs, args.verbose)
                if out:
                    checked += 1
                    print("\n".join(out))
                    print()
                total += n
        print(f"== item-effect sweep: {checked} function(s) reported, {total} finding(s).")
        return 1 if total else 0

    if not args.addr:
        ap.error("give an addr or --sweep")
    addr = int(args.addr, 16)
    for f in sorted(SRC.glob("*.cpp")):
        text = f.read_text(errors="replace")
        marker_addrs = sorted({int(m.group(1), 16) for m in ADDR_COMMENT_RE.finditer(text)})
        for a, cls, meth, s, e in enumerate_functions(f):
            if a == addr:
                n, out = audit_function(a, cls, meth, f, s, e, marker_addrs, True)
                print("\n".join(out) if out else f"{cls}::{meth}: no ClearItemEffect site in source")
                return 1 if n else 0
    print(f"no recovered function with marker 0x{addr:X}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
