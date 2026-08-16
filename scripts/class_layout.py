#!/usr/bin/env python3
"""Recursive class layout from the src headers: "CGameSprite+0x966" -> the member PATH.

`parity_offsets.header_offset_map` is FLAT -- it merges a header's classes and their
bases into one name->offset dict, so a byte inside an embedded object resolves to the
object, not to the field. Two lookups instead of one, every time:

    CGameSprite+0x966  ->  m_derivedStats @0x920  ->  CDerivedStats+0x46  ->  m_nLevel

This module does the whole descent in one call. It reads the same `/* NNNN */`
annotations, plus the member TYPE, and walks into any embedded class that has a header.

Two layout sources, and the report always says which:
  [annotated] the header's own `/* NNNN */` offsets -- ground truth from the recovery.
  [computed]  summed member sizes under the header's `#pragma pack` -- for classes that
              were never annotated (CAIAction). Verified against the hand-checked
              CAIAction offsets (actionID +0 .. internalFlags +0xD2, sizeof 0xD6).

Base subobjects: a base sits at +0, EXCEPT when the derived class introduces the vptr
(derived declares `virtual`, base declares none) -- then the base starts at +4. That is
why CGameEffect+0x18 is CGameEffectBase::m_effectAmount (+0x14), not m_dwFlags.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "src"

sys.path.insert(0, str(Path(__file__).resolve().parent))

# `/* 0920 */ CDerivedStats m_derivedStats;` / `/* 50CB */ BYTE m_visibleTerrainTable[16];`
ANNOT_DECL_RE = re.compile(
    r"/\*\s*([0-9A-Fa-f]+)\s*\*/\s*([A-Za-z_][\w:\s\*&<>,]*?)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*;")
# a plain data member, for the classes that carry no offset comments
PLAIN_DECL_RE = re.compile(
    r"^\s*([A-Za-z_][\w:\s\*&<>,]*?)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*;\s*$")
CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+(\w+)\s*(?::([^{]*))?\{", re.M)
BASE_RE = re.compile(r"(?:public|protected|private|virtual)?\s*(\w+)\s*(?:,|$)")
PACK_RE = re.compile(r"#pragma\s+pack\s*\(\s*(?:push\s*,\s*)?(\d+)\s*\)")

# Sizes the headers never spell out. Class types are measured from their own layout.
PRIM = {
    "bool": 1, "char": 1, "BYTE": 1, "unsigned char": 1, "signed char": 1,
    "short": 2, "unsigned short": 2, "SHORT": 2, "WORD": 2, "USHORT": 2, "wchar_t": 2,
    "int": 4, "unsigned": 4, "unsigned int": 4, "long": 4, "unsigned long": 4,
    "UINT": 4, "DWORD": 4, "LONG": 4, "ULONG": 4, "BOOL": 4, "float": 4,
    "POSITION": 4, "HWND": 4, "HANDLE": 4, "COLORREF": 4, "time_t": 4,
    "double": 8, "__int64": 8, "unsigned __int64": 8, "LONGLONG": 8,
    "CString": 4, "CPoint": 8, "CSize": 8, "CRect": 16, "CResRef": 8, "SCRIPTNAME": 32,
}
MAX_DEPTH = 6


class ClassInfo:
    def __init__(self, name, path, body, bases, pack, has_virtual):
        self.name = name
        self.path = path
        self.body = body
        self.bases = bases
        self.pack = pack
        self.has_virtual = has_virtual


_INDEX: dict[str, ClassInfo] | None = None


def _split_bases(spec: str) -> list[str]:
    out = []
    for part in spec.split(","):
        m = BASE_RE.search(part.strip())
        if m and m.group(1) not in ("public", "protected", "private", "virtual"):
            out.append(m.group(1))
    return out


def _class_body(text: str, open_brace: int) -> str:
    """Text between the class's braces, brace-matched (nested unions/structs included)."""
    depth = 0
    for i in range(open_brace, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1 : i]
    return text[open_brace + 1 :]


def class_index() -> dict[str, ClassInfo]:
    """class name -> ClassInfo, over every src header. One pass, cached."""
    global _INDEX
    if _INDEX is not None:
        return _INDEX
    _INDEX = {}
    for hdr in sorted(SRC.glob("*.h")):
        text = hdr.read_text(errors="replace")
        packs = [(m.start(), int(m.group(1))) for m in PACK_RE.finditer(text)]
        for m in CLASS_RE.finditer(text):
            name = m.group(1)
            if name in _INDEX:
                continue
            body = _class_body(text, m.end() - 1)
            pack = 8
            for pos, val in packs:            # the pack in force at the declaration
                if pos < m.start():
                    pack = val
            _INDEX[name] = ClassInfo(
                name, hdr, body, _split_bases(m.group(2) or ""), pack,
                bool(re.search(r"\bvirtual\b", body)))
    return _INDEX


def _seed_offsets(cls: str) -> dict[str, int]:
    """parity_offsets' flat map, used only as a fallback when the class body has no
    annotations of its own (a class declared in a header we did not brace-match)."""
    try:
        from parity_offsets import header_offset_map
        return header_offset_map(cls)
    except Exception:
        return {}


def _norm_type(t: str) -> str:
    t = " ".join(t.split())
    for kw in ("const ", "mutable ", "static "):
        t = t.replace(kw, "")
    return t.strip()


def type_size(t: str, depth: int = 0) -> int | None:
    t = _norm_type(t)
    if t.endswith("*") or t.endswith("&"):
        return 4
    if t in PRIM:
        return PRIM[t]
    if depth < MAX_DEPTH:
        return sizeof(t, depth + 1)
    return None


def type_align(t: str, pack: int, depth: int = 0) -> int:
    sz = type_size(t, depth)
    t = _norm_type(t)
    if t.endswith("*") or t.endswith("&") or t in PRIM:
        return min(sz or 4, pack, 8)
    ci = class_index().get(t)
    if ci and depth < MAX_DEPTH:                # a class aligns like its widest member
        best = 1
        for _, mt, _, _, _ in members(t, depth + 1):
            best = max(best, type_align(mt, pack, depth + 1))
        return min(best, pack)
    return min(sz or 4, pack, 8)


def _array_count(raw: str | None, body: str) -> int:
    if not raw:
        return 1
    if raw.isdigit():
        return int(raw)
    m = re.search(rf"\b{re.escape(raw)}\s*=\s*(\d+)", body)   # `enum {{ N = 16 }};`
    return int(m.group(1)) if m else 1


SKIP_LINE_RE = re.compile(r"\b(?:static|virtual|typedef|friend|using|return|operator)\b|\(")


def members(cls: str, depth: int = 0) -> list[tuple[int | None, str, str, int, bool]]:
    """[(offset, type, name, count, annotated)] for the class's OWN members, in order.

    offset is absolute-in-the-object for annotated classes (that is how the recovery
    writes them) and computed-from-zero for the rest; `annotated` says which.
    """
    ci = class_index().get(cls)
    if ci is None:
        return []
    out: list[tuple[int | None, str, str, int, bool]] = []
    for m in ANNOT_DECL_RE.finditer(ci.body):
        t, name = _norm_type(m.group(2)), m.group(3)
        if not t or SKIP_LINE_RE.search(t):
            continue
        out.append((int(m.group(1), 16), t, name, _array_count(m.group(4), ci.body), True))
    if out:
        return sorted(out, key=lambda e: e[0])

    seed = _seed_offsets(cls)                    # fallback: the flat map, no types
    if seed and not any(PLAIN_DECL_RE.match(ln) for ln in ci.body.splitlines()):
        return sorted(((off, "?", name, 1, True) for name, off in seed.items()),
                      key=lambda e: e[0])

    # Nothing annotated: compute from declaration order under the header's pack.
    off = 0
    if ci.has_virtual and not any(class_index().get(b) and class_index()[b].has_virtual
                                  for b in ci.bases):
        off = 4                                  # this class introduces the vptr
    for b in ci.bases:
        bsz = sizeof(b, depth + 1)
        if bsz is None:
            return []
        off += bsz
    for ln in ci.body.splitlines():
        pm = PLAIN_DECL_RE.match(ln)
        if not pm or SKIP_LINE_RE.search(ln):
            continue
        t, name = _norm_type(pm.group(1)), pm.group(2)
        if t in ("public", "private", "protected") or not t:
            continue
        sz = type_size(t, depth + 1)
        if sz is None:
            return out                            # unknown type: stop, do not guess
        a = type_align(t, ci.pack, depth + 1)
        off = (off + a - 1) // a * a
        count = _array_count(pm.group(3), ci.body)
        out.append((off, t, name, count, False))
        off += sz * count
    return out


_SIZEOF: dict[str, int | None] = {}


def sizeof(cls: str, depth: int = 0) -> int | None:
    if cls in _SIZEOF:
        return _SIZEOF[cls]
    _SIZEOF[cls] = None                           # cycle guard
    ci = class_index().get(cls)
    if ci is None or depth >= MAX_DEPTH:
        return None
    mem = members(cls, depth)
    if not mem:
        return None
    last = mem[-1]
    lsz = type_size(last[1], depth + 1)
    if lsz is None:
        return None
    end = last[0] + lsz * last[3]
    align = max([1] + [type_align(t, ci.pack, depth + 1) for _, t, _, _, _ in mem])
    align = min(align, ci.pack)
    _SIZEOF[cls] = (end + align - 1) // align * align
    return _SIZEOF[cls]


def base_shift(derived: str, base: str) -> int:
    """Where the base subobject starts inside the derived object."""
    d, b = class_index().get(derived), class_index().get(base)
    if d is None or b is None:
        return 0
    return 4 if d.has_virtual and not b.has_virtual else 0


def flatten(cls: str, base: int = 0, path: str = "", depth: int = 0,
            seen: frozenset = frozenset()) -> list[dict]:
    """Every field reachable from `cls`, as absolute offsets, bases expanded in place."""
    if depth > MAX_DEPTH or cls in seen:
        return []
    ci = class_index().get(cls)
    if ci is None:
        return []
    seen = seen | {cls}
    out: list[dict] = []
    for b in ci.bases:
        out += flatten(b, base + base_shift(cls, b), path, depth + 1, seen)
    mem = members(cls, depth)
    for i, (off, t, name, count, annotated) in enumerate(mem):
        abs_off = base + off
        sz = type_size(t, depth + 1)
        sz = sz * count if sz else None
        # A declared sibling is a harder bound than any size we estimate: an embedded
        # object whose own header over-measures must not swallow the field after it
        # (CInfGame's CAIIdList ran over m_lastClick).
        nxt = base + mem[i + 1][0] if i + 1 < len(mem) else None
        if nxt is not None and nxt > abs_off and (sz is None or abs_off + sz > nxt):
            sz = nxt - abs_off
        full = f"{path}{name}" if not path else f"{path}.{name}"
        out.append({"off": abs_off, "path": full, "type": t, "count": count,
                    "size": sz, "owner": cls, "annotated": annotated, "depth": depth})
        bare = _norm_type(t)
        if (count == 1 and not bare.endswith(("*", "&")) and bare not in PRIM
                and bare in class_index()):
            inner = flatten(bare, abs_off, full, depth + 1, seen)
            out += [e for e in inner if sz is None or e["off"] < abs_off + sz]
    return out


def resolve(cls: str, target: int) -> dict | None:
    """The field of `cls` that covers byte `target`, innermost-first. None if out of range.

    A byte in the MIDDLE of an embedded object wants the innermost field
    (CGameSprite+0x966 = m_derivedStats.m_nLevel); a byte ON its first byte wants the
    object itself (CAIAction+0x3E = m_acteeID, not m_acteeID.m_sName at its +0).
    """
    covering = [e for e in flatten(cls)
                if e["off"] <= target
                and (e["size"] is None or e["off"] + e["size"] > target)]
    if not covering:
        return None
    top = max(e["off"] for e in covering)
    at_top = [e for e in covering if e["off"] == top]
    return min(at_top, key=lambda e: e["depth"]) if top == target else \
        max(at_top, key=lambda e: e["depth"])


def describe(cls: str, target: int) -> str:
    """One line: the member path, its type, and the offset inside it."""
    if cls not in class_index():
        return f"unknown class {cls} (no src/*.h declares it)"
    e = resolve(cls, target)
    if e is None:
        sz = sizeof(cls)
        return f"no member at +0x{target:X}" + (f" (sizeof {cls} = 0x{sz:X})" if sz else "")
    inner = target - e["off"]
    arr = f"[{e['count']}]" if e["count"] > 1 else ""
    kind = "annotated" if e["annotated"] else "computed"
    tail = f"  +0x{inner:X} into it" if inner else ""
    if e["size"] is None and inner > 0x400:
        # last field of a known size is far behind: the offset is probably not this class's
        tail += "  ?? past every sized field -- wrong class?"
    if not inner:                                # container hit: name what starts here
        deeper = [d for d in flatten(cls)
                  if d["off"] == e["off"] and d["depth"] == e["depth"] + 1]
        if deeper:
            tail = f"  (first field {deeper[0]['path'].split('.')[-1]} {deeper[0]['type']})"
    # unions declare several fields at one offset -- name them all, never silently pick
    alts = [d["path"] for d in flatten(cls)
            if d["off"] == e["off"] and d["depth"] == e["depth"] and d["path"] != e["path"]]
    if alts:
        tail += "   union with " + ", ".join(sorted(alts)[:3])
    return (f"{e['path']}  {e['type']}{arr}  "
            f"(@0x{e['off']:X} in {e['owner']}, {kind}){tail}")


if __name__ == "__main__":
    for tok in sys.argv[1:]:
        cls, _, off = tok.partition("+")
        print(f"{tok}: {describe(cls, int(off, 16))}")
