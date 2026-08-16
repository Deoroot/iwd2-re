#!/home/wills/iwd2-re/.venv-reagent/bin/python
"""sym.py - terse binary micro-CLI for IWD2.exe (replaces ad-hoc pefile/capstone heredocs).

  sym.py bytes  0xADDR [N=64]          hexdump
  sym.py u32    0xADDR [count=1]       little-endian dwords, name-annotated
  sym.py str    0xADDR [max=256]       NUL-terminated string
  sym.py disasm 0xADDR [count=24]      capstone x86-32, call/jmp targets named
  sym.py findptr 0xVALUE               scan all sections for LE dword (vtable slot discovery)
  sym.py callsites 0xTARGET            scan .text for E8 rel32 calls to target (factory case wiring)
  sym.py scan HEXBYTES [--limit N] [--fn RE]  .text byte pattern -> containing fns; a field
                                       displacement is little-endian ('scan 38560000' = [reg+0x5638]).
                                       Caps at 96 hits: --limit 0 for all, --fn to filter by function.
  sym.py vtable 0xADDR [slots=16]      dump vtable slots resolved to names
  sym.py addr2fn 0xADDR                containing function (address_map bisect) + src file:line
  sym.py crash  dump.dmp [N] [--loose]  minidump: exception + symbolicated stack scan (--loose for our-build dumps)
  sym.py exe                           show resolved binary + md5 + canonical verdict
  sym.py whatis TOKEN...               resolve MANY mixed tokens at once (batch this, don't
                                       call it once per question). Token forms:
                                         0xADDR         section, u32/WORD value, CONTAINING
                                                        fn (ghidra) + nearest recovered name,
                                                        both with their START address, and
                                                        the src `// 0xADDR` constant
                                         CClass+0xNNN   member path, RECURSING into embedded
                                                        classes (CGameSprite+0x966 ->
                                                        m_derivedStats.m_nLevel)
                                         vtable:0xADDR  owning class + the slots that name it
                                         data:0xADDR    bytes, dword interpretations, and every
                                                        src mention of the address
                                       `--data` applies the data view to every bare address.

EXE: the CANONICAL pristine IWD2.exe Ghidra imported (md5 25cb3d8a), vendored at
REPO/.bin/iwd2.exe. The host game install is IWD2EE-modified (patched .data
strings + .rsrc; .text/.rdata code is byte-identical but .data globals differ),
so do NOT read it. ImageBase 0x400000, no ASLR.
Names: .ghidra-exports/address_map.json + src_index.json (file:line join).
"""

import bisect
import json
import os
import re
import string
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Canonical = the binary Ghidra imported (md5 below = the VM's pristine
# C:\GOG Games\Icewind Dale 2\IWD2.exe), vendored at REPO/.bin/iwd2.exe. The host
# game install is IWD2EE-modified -- code identical, but .data globals/.rsrc/header
# differ -- so prefer the vendored pristine copy. Override with IWD2_EXE.
CANONICAL_MD5 = "25cb3d8aa2ec648e94e328d8c467973e"
_REPO_EXE = os.path.join(REPO, ".bin", "iwd2.exe")
EXE = os.environ.get("IWD2_EXE") or (
    _REPO_EXE if os.path.exists(_REPO_EXE)
    else "/home/wills/Games/Heroic/Icewind Dale 2/IWD2.exe")
ADDR_MAP = os.path.join(REPO, ".ghidra-exports", "address_map.json")
SRC_INDEX = os.path.join(REPO, ".ghidra-exports", "src_index.json")

_pe = None
_names = None        # sorted [(va, full_name)]
_src_loc = None      # full_name -> "file:line"


def resolve_exe(path):
    """Case-insensitive resolve: filesystems vary (IWD2.exe vs iwd2.exe)."""
    if os.path.exists(path):
        return path
    d, base = os.path.split(path)
    if d and os.path.isdir(d):
        low = base.lower()
        for f in os.listdir(d):
            if f.lower() == low:
                return os.path.join(d, f)
    return path  # let pefile raise a clear FileNotFoundError


def pe():
    global _pe
    if _pe is None:
        import pefile
        _pe = pefile.PE(resolve_exe(EXE), fast_load=True)
    return _pe


def image_base():
    return pe().OPTIONAL_HEADER.ImageBase


def image_end():
    return image_base() + pe().OPTIONAL_HEADER.SizeOfImage


def names():
    global _names
    if _names is None:
        try:
            d = json.load(open(ADDR_MAP))
            _names = sorted((int(k, 16), v.get("full_name") or v.get("name", "?")) for k, v in d.items())
        except Exception:
            _names = []
    return _names


def src_loc(full_name):
    global _src_loc
    if _src_loc is None:
        _src_loc = {}
        try:
            c = json.load(open(SRC_INDEX))
            for rel, syms in c.get("syms", {}).items():
                for s in syms:
                    fn = (s["qual"] + "::" + s["name"]) if s["qual"] else s["name"]
                    if s.get("kind") == "fn" and fn not in _src_loc:
                        _src_loc[fn] = f"{rel}:{s['line']}"
        except Exception:
            pass
    return _src_loc.get(full_name)


def addr2name(va, exact_only=False):
    """'CClass::Fn' or 'CClass::Fn+0xOFF' for any VA inside the image."""
    ns = names()
    if not ns:
        return None
    i = bisect.bisect_right(ns, (va, "\xff")) - 1
    if i < 0:
        return None
    start, name = ns[i]
    if va == start:
        return name
    if exact_only:
        return None
    bound = ns[i + 1][0] if i + 1 < len(ns) else start + 0x10000
    if va < bound:
        return f"{name}+0x{va - start:x}"
    return None


def is_retaddr(va):
    """True if the bytes just before va decode as a call instruction."""
    try:
        d = read(va - 6, 6)
    except Exception:
        return False
    return (d[1] == 0xE8                                   # call rel32
            or d[4] == 0xFF and (d[5] >> 3) & 7 == 2       # call r/m32 short forms
            or d[3] == 0xFF and (d[4] >> 3) & 7 == 2
            or d[0] == 0xFF and (d[1] >> 3) & 7 == 2)      # call [disp32]


def read(va, n):
    return pe().get_data(va - image_base(), n)


def parse_addr(s):
    return int(s, 16) if s.lower().startswith("0x") else int(s, 16)


def annot(va):
    nm = addr2name(va)
    return f"  ; {nm}" if nm else ""


def cmd_bytes(addr, n=64):
    data = read(addr, n)
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        asc = "".join(chr(b) if chr(b) in string.printable[:-5] else "." for b in chunk)
        print(f"0x{addr + off:08x}  {hexs:<47}  {asc}")


def cmd_u32(addr, count=1):
    data = read(addr, 4 * count)
    for i in range(count):
        (v,) = struct.unpack_from("<I", data, 4 * i)
        print(f"0x{addr + 4 * i:08x}: 0x{v:08x}  ({v}){annot(v)}")


def cmd_str(addr, maxlen=256):
    data = read(addr, maxlen)
    s = data.split(b"\0")[0]
    print(s.decode("latin-1"))


def cmd_disasm(addr, count=24):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    data = read(addr, max(16 * count, 64))
    n = 0
    for ins in md.disasm(data, addr):
        note = ""
        if ins.mnemonic.startswith(("call", "j")) or ins.mnemonic in ("push", "mov"):
            for tok in ins.op_str.replace(",", " ").split():
                t = tok.strip("[]")
                if t.startswith("0x"):
                    try:
                        va = int(t, 16)
                    except ValueError:
                        continue
                    if image_base() <= va < image_end():
                        nm = addr2name(va, exact_only=ins.mnemonic.startswith("call"))
                        if nm:
                            note = f"  ; {nm}"
                            break
        print(f"0x{ins.address:08x}: {ins.mnemonic:<7} {ins.op_str}{note}")
        n += 1
        if n >= count:
            break


def find_pointers(value, limit=65):
    """Every location holding *value* as a little-endian dword, as (va, section)."""
    pat = struct.pack("<I", value)
    hits = []
    for sec in pe().sections:
        name = sec.Name.rstrip(b"\0").decode("latin-1")
        data = sec.get_data()
        base = image_base() + sec.VirtualAddress
        off = data.find(pat)
        while off != -1:
            hits.append((base + off, name))
            if len(hits) >= limit:
                return hits
            off = data.find(pat, off + 4)
    return hits


def find_callsites(target):
    """Every `.text` E8 rel32 call whose destination is *target*."""
    sites = []
    for sec in pe().sections:
        if not sec.Name.startswith(b".text"):
            continue
        data = sec.get_data()
        base = image_base() + sec.VirtualAddress
        off = data.find(b"\xe8")
        while off != -1:
            if off + 5 <= len(data):
                (rel,) = struct.unpack_from("<i", data, off + 1)
                site = base + off
                if (site + 5 + rel) & 0xFFFFFFFF == target:
                    sites.append(site)
            off = data.find(b"\xe8", off + 1)
    return sites


def cmd_findptr(value):
    hits = find_pointers(value)
    for va, name in hits[:64]:
        print(f"0x{va:08x}  {name}{annot(value)}")
    if len(hits) > 64:
        print("... (>64 hits, stopping)")
        return
    if not hits:
        print("no hit")
        sys.exit(1)


def cmd_scan(hexbytes, sect=b".text", limit=96, fnpat=None):
    """List code addresses containing a raw byte pattern (e.g. a field disp:
    'scan 38560000' finds [reg+0x5638] accesses), mapped to containing fns.

    limit=0 lifts the cap. fnpat keeps only hits whose containing function
    matches the regex -- use it instead of a shell grep, so the cap counts
    what you asked for rather than truncating before the interesting range."""
    pat = bytes.fromhex(hexbytes)
    rx = re.compile(fnpat) if fnpat else None
    hits = 0
    for sec in pe().sections:
        if not sec.Name.startswith(sect):
            continue
        data = sec.get_data()
        base = image_base() + sec.VirtualAddress
        off = data.find(pat)
        while off != -1:
            va = base + off
            nm = addr2name(va) or "?"
            if rx is None or rx.search(nm):
                print(f"0x{va:08x}  {nm}")
                hits += 1
                if limit and hits > limit:
                    print(f"... (>{limit} hits, stopping -- raise with --limit N, 0 = all)")
                    return
            off = data.find(pat, off + 1)
    if not hits:
        print("no hit")
        sys.exit(1)


def cmd_callsites(target):
    sites = find_callsites(target)
    for site in sites:
        print(f"0x{site:08x}  {addr2name(site) or '?'}")
    if not sites:
        print("no call site")
        sys.exit(1)


def cmd_vtable(addr, slots=16):
    data = read(addr, 4 * slots)
    for i in range(slots):
        (v,) = struct.unpack_from("<I", data, 4 * i)
        nm = addr2name(v) or ("?" if image_base() <= v < image_end() else "(not code)")
        print(f"[{i:3d}] +0x{4 * i:03x}  0x{v:08x}  {nm}")


def cmd_addr2fn(addr):
    nm = addr2name(addr)
    if not nm:
        print(f"0x{addr:08x}: no containing function in address_map")
        sys.exit(1)
    base_name = nm.split("+")[0]
    loc = src_loc(base_name)
    print(f"0x{addr:08x}: {nm}" + (f"  {loc}" if loc else "  (no src)"))


def cmd_crash(path, frames=12, loose=False):
    """Symbolicated stack scan per thread. Works for crash AND freeze dumps;
    WOW64 dumps carry a 64-bit context, so registers are skipped and the
    recorded 32-bit stack memory of each thread is scanned instead.
    Names assume a dump of the ORIGINAL IWD2.exe (address_map layout);
    for our-build dumps use --loose (raw in-image dwords, no call check)."""
    from minidump.minidumpfile import MinidumpFile
    md = MinidumpFile.parse(path)
    exc = getattr(md, "exception", None)
    tid = None
    if exc and getattr(exc, "exception_records", None):
        r = exc.exception_records[0]
        tid = r.ThreadId
        ea = r.ExceptionRecord.ExceptionAddress
        # minidump lib may return ExceptionCode as an IntEnum (or its str name); coerce to int.
        _code = r.ExceptionRecord.ExceptionCode
        code = getattr(_code, "value", _code)
        if not isinstance(code, int):
            code = int(str(code), 0) if str(code).lower().startswith("0x") else 0
        print(f"exception 0x{code:08x} at 0x{ea:08x}  {addr2name(ea) or ''}  tid={tid}")
    reader = md.get_reader()
    for th in md.threads.threads:
        if tid is not None and th.ThreadId != tid:
            continue
        start = th.Stack.StartOfMemoryRange
        size = th.Stack.MemoryLocation.DataSize
        try:
            buf = reader.read(start, size)
        except Exception as e:
            print(f"tid={th.ThreadId}: stack read failed: {e}")
            continue
        hits = []
        for off in range(0, len(buf) - 3, 4):
            (v,) = struct.unpack_from("<I", buf, off)
            if image_base() <= v < image_end() and (loose or is_retaddr(v)):
                nm = addr2name(v) or "?"
                hits.append((start + off, v, nm))
        if not hits:
            continue
        kind = "in-image dwords" if loose else "return addrs"
        print(f"tid={th.ThreadId}  stack 0x{start:08x}+0x{size:x}  ({len(hits)} {kind}, top {min(frames, len(hits))}, innermost first)")
        for sa, v, nm in hits[:frames]:
            print(f"  0x{sa:08x}: 0x{v:08x}  {nm}")


# ---------------------------------------------------------------- whatis

GHIDRA_INDEX = os.path.join(REPO, ".ghidra-exports", "_index.json")
SRC_DIR = os.path.join(REPO, "src")
MARKER_RE = re.compile(r"^\s*//\s*(0x[0-9A-Fa-f]{5,8})\s*$")
LITERAL_RE = re.compile(r"0x([0-9A-Fa-f]{5,8})\b")
DEFINE_RE = re.compile(r"^\s*#define\s+(\w+)\s+\(?(0x[0-9A-Fa-f]+|\d+)\)?\s*$", re.M)

_gfuncs = None
_src_scan = None


def ghidra_funcs():
    """Sorted (start, name) for EVERY Ghidra function -- recovered or not. This is the
    only source that gives real function BOUNDS; address_map holds recovered names
    only, so its 'nearest preceding' can sit in a completely different function."""
    global _gfuncs
    if _gfuncs is None:
        try:
            d = json.load(open(GHIDRA_INDEX))
            _gfuncs = sorted((int(k, 16), v.get("name", "?")) for k, v in d.items())
        except Exception:
            _gfuncs = []
    return _gfuncs


def containing_fn(va):
    """(start, name) of the Ghidra function containing va, or None."""
    fns = ghidra_funcs()
    if not fns:
        return None
    i = bisect.bisect_right(fns, (va, "\xff")) - 1
    return fns[i] if i >= 0 else None


def src_scan():
    """(markers, mentions) over src/: `// 0xADDR` recovery markers -> the line they
    annotate, and every literal 0xADDR anywhere -> where it is written."""
    global _src_scan
    if _src_scan is None:
        markers, mentions = {}, {}
        for root, _dirs, files in os.walk(SRC_DIR):
            for f in sorted(files):
                if not f.endswith((".cpp", ".h")):
                    continue
                p = os.path.join(root, f)
                rel = os.path.relpath(p, REPO).replace("\\", "/")
                try:
                    lines = open(p, errors="replace").read().splitlines()
                except OSError:
                    continue
                for i, ln in enumerate(lines):
                    m = MARKER_RE.match(ln)
                    if m:
                        for j in range(i + 1, min(i + 4, len(lines))):
                            nxt = lines[j].strip()
                            if nxt and not nxt.startswith("//"):
                                markers.setdefault(int(m.group(1), 16),
                                                   (f"{rel}:{j + 1}", nxt[:100]))
                                break
                    for lm in LITERAL_RE.finditer(ln):
                        mentions.setdefault(int(lm.group(1), 16), []).append(
                            (f"{rel}:{i + 1}", ln.strip()[:100]))
        _src_scan = (markers, mentions)
    return _src_scan


def section_of(va):
    for sec in pe().sections:
        name = sec.Name.rstrip(b"\0").decode("latin-1")
        start = image_base() + sec.VirtualAddress
        if start <= va < start + max(sec.Misc_VirtualSize, sec.SizeOfRawData):
            return name, va < start + sec.SizeOfRawData
    return "?", False


def safe_read(va, n):
    try:
        d = read(va, n)
    except Exception:
        return b""
    return d


def define_named(value, hint_file):
    """A `#define NAME <value>` in the header next to *hint_file* -- how a WORD in an
    id table gets a name when no constant carries the address itself."""
    if not hint_file:
        return None
    hdr = os.path.join(REPO, os.path.splitext(hint_file.split(":")[0])[0] + ".h")
    if not os.path.exists(hdr):
        return None
    for m in DEFINE_RE.finditer(open(hdr, errors="replace").read()):
        if int(m.group(2), 0) == value:
            return f"{m.group(1)}  ({os.path.relpath(hdr, REPO)})"
    return None


def whatis_addr(va):
    markers, mentions = src_scan()
    sec, has_raw = section_of(va)
    print(f"== 0x{va:08X}")
    print(f"   section  {sec}" + ("" if has_raw else "   (no raw data -- zero-filled)"))
    d = safe_read(va, 4)
    if len(d) == 4:
        (u32,) = struct.unpack("<I", d)
        (w,) = struct.unpack("<H", d[:2])
        print(f"   value    u32 0x{u32:08X} ({u32})   WORD 0x{w:04X} ({w})"
              + (f"   -> {addr2name(u32)}" if image_base() <= u32 < image_end()
                 and addr2name(u32) else ""))
    cf = containing_fn(va)
    if cf and sec.startswith(".text"):
        print(f"   fn       {cf[1]:<40} start 0x{cf[0]:08X}  +0x{va - cf[0]:X}   [ghidra bounds]")
    nm = addr2name(va)
    if nm:
        base = nm.split("+")[0]
        start = next((a for a, n in names() if n == base), None)
        loc = src_loc(base)
        flag = ""
        if cf and start is not None and start != cf[0]:
            flag = "   <-- NEAREST PRECEDING NAME, a DIFFERENT function"
        print(f"   name     {base:<40} start 0x{start:08X}  +0x{va - start:X}"
              f"{'  ' + loc if loc else ''}{flag}")
    if va in markers:
        loc, text = markers[va]
        print(f"   src      {text}   {loc}   [exact // marker]")
    else:
        prev = max((a for a in markers if a < va and va - a < 0x400), default=None)
        if prev is not None:
            loc, text = markers[prev]
            print(f"   src      nearest marker 0x{prev:08X} (-0x{va - prev:X}): {text}   {loc}")
            if len(d) == 4:
                hit = define_named(w, loc)
                if hit:
                    print(f"   value=   {hit}")
    for loc, text in mentions.get(va, [])[:3]:
        print(f"   mention  {loc}: {text}")


def whatis_member(cls, off):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import class_layout
    print(f"== {cls}+0x{off:X}")
    print(f"   {class_layout.describe(cls, off)}")


def _bases_of(cls):
    """Transitive base classes, from the src headers (empty if the class is unknown)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        import class_layout
    except Exception:
        return set()
    idx = class_layout.class_index()
    out = set()
    todo = list(idx[cls].bases) if cls in idx else []
    while todo:
        b = todo.pop()
        if b in out:
            continue
        out.add(b)
        if b in idx:
            todo += idx[b].bases
    return out


def whatis_vtable(va, slots=8):
    """Owning class of a vtable: the class named by the most slots, exact hits first.

    Two things fool a plain majority vote and both are handled here. Slot 0 is a
    scalar-deleting dtor address_map does not know, so it resolves to whatever function
    happens to precede it; and /OPT:ICF folds identical one-line stubs, so an unrelated
    class (CWarp in CGameEffectDeath's vtable) can tie on exact hits. The tiebreak is
    inheritance: a candidate DERIVED from another candidate is the real owner, because
    the slots it does not override are exactly its base's.
    """
    data = safe_read(va, 4 * slots)
    if len(data) < 8:
        print(f"== vtable:0x{va:08X}\n   no data at this address")
        return
    score, rows = {}, []
    for i in range(len(data) // 4):
        (v,) = struct.unpack_from("<I", data, 4 * i)
        exact = addr2name(v, exact_only=True)
        nm = exact or addr2name(v)
        rows.append((i, v, nm, bool(exact)))
        if nm and "::" in nm:
            cls = nm.split("::")[0]
            score[cls] = score.get(cls, 0) + (2 if exact else 1)
    print(f"== vtable:0x{va:08X}")
    if score:
        rank = {c: score[c] + (2 if _bases_of(c) & set(score) else 0) for c in score}
        best = max(rank, key=lambda c: (rank[c], score[c], c))
        others = sorted((c for c in score if c != best), key=lambda c: -rank[c])[:3]
        loc = src_loc(f"{best}::{best}") or ""
        print(f"   class    {best}   (rank {rank[best]}, slots {score[best]}"
              + ("; over " + ", ".join(f"{c}={rank[c]}" for c in others) if others else "")
              + ")" + (f"   ctor {loc.replace(chr(92), '/')}" if loc else ""))
    for i, v, nm, exact in rows:
        mark = "" if exact else "   (unnamed fn -- nearest preceding name)" if nm else ""
        print(f"   [{i}] +0x{4 * i:02X}  0x{v:08X}  {nm or '?'}{mark}")


def whatis_data(va):
    markers, mentions = src_scan()
    sec, has_raw = section_of(va)
    print(f"== data:0x{va:08X}   section {sec}"
          + ("" if has_raw else "   (no raw data -- zero-filled at load)"))
    d = safe_read(va, 32)
    if not d:
        print("   bytes    <none in the image>")
    else:
        asc = "".join(chr(b) if chr(b) in string.printable[:-5] else "." for b in d[:16])
        print(f"   bytes    {' '.join(f'{b:02x}' for b in d[:16])}  {asc}")
        for i in range(min(4, len(d) // 4)):
            (v,) = struct.unpack_from("<I", d, 4 * i)
            note = ""
            if image_base() <= v < image_end():
                s, _ = section_of(v)
                note = f"  -> {addr2name(v) or s}"
            elif v == 0xFFFFFFFF:
                note = "  (-1)"
            print(f"   +0x{4 * i:02X}     0x{v:08X}{note}")
        if all(b == 0 for b in d):
            print("   note     all-zero -- a shared empty sentinel (CResRef/CString style)")
    n = len(find_pointers(va, limit=200))
    print(f"   refs     {n}{'+ (cap)' if n >= 200 else ''} pointer(s) to it in the image")
    if va in markers:
        loc, text = markers[va]
        print(f"   src      {text}   {loc}   [exact // marker]")
    for loc, text in mentions.get(va, [])[:4]:
        print(f"   mention  {loc}: {text}")


def cmd_whatis(tokens, data_view=False):
    for tok in tokens:
        if tok.lower().startswith("vtable:"):
            whatis_vtable(parse_addr(tok.split(":", 1)[1]))
        elif tok.lower().startswith("data:"):
            whatis_data(parse_addr(tok.split(":", 1)[1]))
        elif "+" in tok:
            cls, _, off = tok.partition("+")
            whatis_member(cls.strip(), int(off, 16))
        elif tok.lower().startswith("0x"):
            (whatis_data if data_view else whatis_addr)(parse_addr(tok))
        else:
            print(f"== {tok}\n   unrecognised token (want 0xADDR | CClass+0xNNN | "
                  f"vtable:0xADDR | data:0xADDR)")


def cmd_exe():
    import hashlib
    path = resolve_exe(EXE)
    print(f"path: {path}")
    if not os.path.exists(path):
        print("status: MISSING")
        sys.exit(1)
    data = open(path, "rb").read()
    md5 = hashlib.md5(data).hexdigest()
    print(f"size: {len(data)}  md5: {md5}")
    if md5 == CANONICAL_MD5:
        print("status: CANONICAL (matches the binary Ghidra imported)")
    else:
        print(f"status: NON-CANONICAL (expected md5 {CANONICAL_MD5})")
        print("  -> likely the IWD2EE-modified host install; vendor the pristine")
        print("     VM copy to REPO/.bin/iwd2.exe or set IWD2_EXE.")
        sys.exit(1)


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__.strip())
        return
    cmd, rest = args[0], args[1:]
    try:
        if cmd == "bytes":
            cmd_bytes(parse_addr(rest[0]), int(rest[1]) if len(rest) > 1 else 64)
        elif cmd == "u32":
            cmd_u32(parse_addr(rest[0]), int(rest[1]) if len(rest) > 1 else 1)
        elif cmd == "str":
            cmd_str(parse_addr(rest[0]), int(rest[1]) if len(rest) > 1 else 256)
        elif cmd == "disasm":
            cmd_disasm(parse_addr(rest[0]), int(rest[1]) if len(rest) > 1 else 24)
        elif cmd == "findptr":
            cmd_findptr(parse_addr(rest[0]))
        elif cmd == "callsites":
            cmd_callsites(parse_addr(rest[0]))
        elif cmd == "scan":
            limit = 96
            fnpat = None
            args = list(rest[1:])
            while args:
                if args[0] == "--limit":
                    limit = int(args[1])
                    args = args[2:]
                elif args[0] == "--fn":
                    fnpat = args[1]
                    args = args[2:]
                else:
                    sys.exit(f"scan: unknown argument {args[0]}")
            cmd_scan(rest[0], limit=limit, fnpat=fnpat)
        elif cmd == "vtable":
            cmd_vtable(parse_addr(rest[0]), int(rest[1]) if len(rest) > 1 else 16)
        elif cmd == "addr2fn":
            cmd_addr2fn(parse_addr(rest[0]))
        elif cmd == "whatis":
            toks = [a for a in rest if a != "--data"]
            if not toks:
                sys.exit("whatis: give at least one token (see --help)")
            cmd_whatis(toks, data_view="--data" in rest)
        elif cmd == "exe":
            cmd_exe()
        elif cmd == "crash":
            loose = "--loose" in rest
            pos = [a for a in rest if not a.startswith("--")]
            cmd_crash(pos[0], int(pos[1]) if len(pos) > 1 else 12, loose=loose)
        else:
            print(f"unknown subcommand {cmd!r} (see --help)")
            sys.exit(2)
    except IndexError:
        print("missing argument (see --help)")
        sys.exit(2)


if __name__ == "__main__":
    main()
