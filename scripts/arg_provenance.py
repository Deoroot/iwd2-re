#!/home/wills/iwd2-re/.venv-reagent/bin/python
"""arg_provenance.py - binary argument provenance at order-sensitive call sites.

Catches the swapped-operand bug class that parity / parity_offsets are BLIND to:
the order of a binary operator's operands is not a structural or call-count
signal (a+b and b+a call the same operator the same number of times). The only
ground truth is the push order in the binary. This tool decodes it.

Origin: CDimm::FindFileInDirectoryList (0x786180) built `file + dir` where the
binary builds `dir + file`; FindFile never matched -> nDrive lost the resident
bit -> loading-bar overshoot. parity stayed GREEN. Found only via a Frida diff.

  arg_provenance.py 0xADDR|Class::Method      audit one function
  arg_provenance.py 0xADDR|Name --check       diff binary order vs source `A + B`
  arg_provenance.py --sweep [--check]         every recovered fn calling the callee
      [--callee 0xADDR]   order-sensitive target (default 0x7fcdfd =
                          CString operator+(const CString&,const CString&))
      [--args N]          callee arg count (default 3: NRV result + a + b)
      [--nrv]             arg0 is the hidden return slot (default on for operator+)

Default mode prints the operand provenance IN BINARY ORDER (the ground truth);
provenance classes (param{k} / loc / this+off / ret(fn) / imm, origin-traced)
make pairing against source a one-line eyeball.

--check parses the source `A + B` operands (param/member/local/ret from the
signature + CString decls), aligns them with the binary sites in order, and tags
each OK / review / SWAP?. It is a TRIAGE aid: PARAM stays strict so a real
param<->x reversal flags, and it never reports SWAP? unless the direct order
fails AND the reversed order fits -- so false alarms are by-design near zero.
LIMIT: the source side is a regex parser, not a C++ front-end -- when its concat
count != the binary's it prints COUNT MISMATCH -> review (the binary order is
still correct; pair it by hand). Aligned sites are reliable.
"""

import os
import re
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sym  # noqa: E402  (read / addr2name / names / image_base)

OP_CONCAT = 0x7FCDFD  # CString operator+(const CString&, const CString&), NRV, ret 0xc

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

_clean = {}  # callee addr -> stdcall cleanup bytes (from its `ret N`); 0 = cdecl/unknown


def stdcall_cleanup(addr):
    """Bytes the callee pops on return (its `ret imm16`), 0 if plain ret/unknown.
    Lets the forward esp model stay aligned across intervening calls."""
    if addr in _clean:
        return _clean[addr]
    _clean[addr] = 0
    try:
        data = sym.read(addr, 0x600)
    except Exception:
        return 0
    for ins in md.disasm(data, addr):
        if ins.mnemonic == "ret":
            _clean[addr] = ins.operands[0].imm if ins.operands else 0
            break
        if ins.mnemonic == "jmp" and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
            # tail-call thunk: follow once
            _clean[addr] = stdcall_cleanup(ins.operands[0].imm)
            break
    return _clean[addr]


def fn_bounds(addr):
    ns = sym.names()
    lo, name = None, None
    for i, (va, nm) in enumerate(ns):
        if va == addr or (va < addr and (i + 1 == len(ns) or ns[i + 1][0] > addr)):
            lo, name = va, nm
            hi = ns[i + 1][0] if i + 1 < len(ns) else va + 0x4000
            if va <= addr < hi:
                return va, hi, nm
    return None, None, None


def resolve_name(query):
    if query.lower().startswith("0x"):
        a = int(query, 16)
        return a
    for va, nm in sym.names():
        if nm == query:
            return va
    sys.exit(f"name not found: {query}")


# ---- provenance value -------------------------------------------------------
# a prov is a tuple; first element is the class tag used for rendering/compare.

def render(p):
    if p is None:
        return "?"
    t = p[0]
    if t == "param":
        return f"param{p[1]}"
    if t == "addr_param":
        return f"&param{p[1]}"
    if t == "local":
        org = f"<-{render(p[2])}" if len(p) > 2 and p[2] else ""
        return f"loc[{p[1]:#x}]{org}"
    if t == "addr_local":
        org = f"<-{render(p[2])}" if len(p) > 2 and p[2] else ""
        return f"&loc[{p[1]:#x}]{org}"
    if t == "member":
        return f"this+{p[1]:#x}"
    if t == "addr_member":
        return f"&this+{p[1]:#x}"
    if t == "this":
        return "this"
    if t == "ret":
        return f"ret({p[1]})"
    if t == "imm":
        return f"imm({p[1]:#x})"
    if t == "load":
        return f"[{render(p[1])}+{p[2]:#x}]"
    if t == "global":
        nm = sym.addr2name(p[1])
        return f"glob({nm or hex(p[1])})"
    if t == "reg":
        return p[1]
    if t == "framebase":
        return "framebase"
    return str(p)


def cls(p):
    """Coarse class for source comparison."""
    if p is None:
        return "?"
    t = p[0]
    if t in ("param", "addr_param"):
        return "PARAM"
    if t in ("local", "addr_local"):
        if len(p) > 2 and p[2]:        # copy of a member/param/ret -> classify by origin
            return cls(p[2])
        return "LOCAL"
    if t in ("member", "addr_member"):
        return "MEMBER"
    if t == "ret":
        return "RET"
    if t == "imm":
        return "IMM"
    if t == "global":
        return "GLOBAL"
    return "?"


def is_stack_base(name):
    return name in ("esp", "ebp")


def audit_fn(addr, callee, argc, nrv, show_clean=True):
    lo, hi, name = fn_bounds(addr)
    if lo is None:
        sys.exit(f"no containing function for {addr:#x}")
    data = sym.read(lo, hi - lo)
    reg = {}                 # reg name -> prov
    reg["ecx"] = ("this",)   # __thiscall entry
    slot_writer = {}         # entry-relative abs offset -> short note (last writer)
    esp = 0                  # esp delta from entry (negative as we push)
    ebp_is_frame = False     # set when `mov ebp, esp` seen
    push_stack = []          # pending pushed provs (each: (prov,))
    hits = []

    def stack_class(disp):
        # entry-relative absolute offset; param region starts at +4 (this in ecx)
        abs_off = (0 if ebp_is_frame and False else esp) + disp
        if ebp_is_frame:
            # ebp = entry_esp-4 after push ebp;mov ebp,esp -> [ebp+8]=param0
            if disp >= 8:
                return ("param", (disp - 8) // 4)
            return ("local", disp)
        if abs_off >= 4 and abs_off <= 0x80:
            return ("param", (abs_off - 4) // 4)
        return ("local", abs_off)

    for ins in md.disasm(data, lo):
        m = ins.mnemonic
        ops = ins.operands

        # ---- resolve provenance of this instruction's effect (before esp upd) ----
        if m == "push":
            o = ops[0]
            if o.type == capstone.x86.X86_OP_REG:
                p = reg.get(ins.reg_name(o.reg))
            elif o.type == capstone.x86.X86_OP_IMM:
                p = ("imm", o.imm & 0xFFFFFFFF)
            elif o.type == capstone.x86.X86_OP_MEM:
                p = mem_prov(o, ins, reg, stack_class, slot_writer)
            else:
                p = None
            push_stack.append(p)
            esp -= 4
            continue

        if m == "call":
            tgt = ops[0].imm if ops and ops[0].type == capstone.x86.X86_OP_IMM else None
            if tgt == callee:
                args = push_stack[-argc:] if len(push_stack) >= argc else [None] * (argc - len(push_stack)) + push_stack
                hits.append((ins.address, list(args)))
            # cleanup: keep push_stack + esp aligned
            cu = stdcall_cleanup(tgt) if tgt else 0
            # origin tracking: an assignment-SHAPED call (ecx=&local, 1-2 stack args,
            # e.g. CString::operator= / copy-ctor) stamps the local with the source
            # provenance -- so a later operand that is a copy of a member/param is
            # classified by its ORIGIN, disambiguating [LOCAL LOCAL] sites. The
            # cu in (4,8) guard avoids stamping on unrelated multi-arg method calls.
            rc = reg.get("ecx")
            if cu in (4, 8) and rc and rc[0] == "addr_local" and push_stack and push_stack[-1] is not None:
                slot_writer[rc[1]] = push_stack[-1]
            if cu:
                for _ in range(cu // 4):
                    if push_stack:
                        push_stack.pop()
                esp += cu
            else:
                # cdecl/unknown: assume the pushed args belonged to this call only
                # if a matching `add esp,N` follows; leave stack, eax = ret
                pass
            nm = (sym.addr2name(tgt) if tgt else None) or (hex(tgt) if tgt else "?")
            reg["eax"] = ("ret", nm.split("+")[0])
            continue

        if m in ("add", "sub") and ops and ops[0].type == capstone.x86.X86_OP_REG and ins.reg_name(ops[0].reg) == "esp":
            if ops[1].type == capstone.x86.X86_OP_IMM:
                n = ops[1].imm
                if m == "sub":
                    esp -= n
                else:
                    esp += n
                    for _ in range(n // 4):  # cdecl arg pop
                        if push_stack:
                            push_stack.pop()
            continue

        # pointer arithmetic to a member/local address: `add ecx, 0x4228` after
        # ecx=this builds &this->member -- MFC passes member CString addrs this way.
        if m == "add" and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG and \
           ops[1].type == capstone.x86.X86_OP_IMM:
            dst = ins.reg_name(ops[0].reg)
            base = reg.get(dst)
            if base:
                if base[0] == "this":
                    reg[dst] = ("addr_member", ops[1].imm)
                elif base[0] == "addr_member":
                    reg[dst] = ("addr_member", base[1] + ops[1].imm)
                elif base[0] == "addr_local":
                    reg[dst] = ("addr_local", base[1] + ops[1].imm, slot_writer.get(base[1] + ops[1].imm))
            continue

        if m == "pop":
            esp += 4
            if push_stack:
                push_stack.pop()
            if ops and ops[0].type == capstone.x86.X86_OP_REG:
                reg[ins.reg_name(ops[0].reg)] = None
            continue

        if m == "mov" and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG:
            dst = ins.reg_name(ops[0].reg)
            if dst == "ebp" and ops[1].type == capstone.x86.X86_OP_REG and ins.reg_name(ops[1].reg) == "esp":
                ebp_is_frame = True
                reg[dst] = ("framebase",)
                continue
            reg[dst] = src_prov(ops[1], ins, reg, stack_class, slot_writer)
            continue

        if m == "lea" and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG:
            dst = ins.reg_name(ops[0].reg)
            reg[dst] = lea_prov(ops[1], ins, reg, stack_class, slot_writer)
            continue

        if m == "xor" and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG and \
           ops[1].type == capstone.x86.X86_OP_REG and ops[0].reg == ops[1].reg:
            reg[ins.reg_name(ops[0].reg)] = ("imm", 0)
            continue

        # note: when ecx is a &local just before a call, mark that slot as written
        if m == "call":
            pass

    return lo, name, hits


def mem_prov(o, ins, reg, stack_class, slot_writer):
    base = ins.reg_name(o.mem.base) if o.mem.base else None
    disp = o.mem.disp
    if base in ("esp", "ebp"):
        p = stack_class(disp)
        if p[0] == "local":
            return ("local", p[1], slot_writer.get(p[1]))
        return p
    if base:
        bp = reg.get(base)
        if bp and bp[0] == "this":
            return ("member", disp)
        return ("load", bp or ("reg", base), disp)
    if o.mem.base is None and o.mem.index == 0:
        return ("global", disp & 0xFFFFFFFF)
    return None


def src_prov(o, ins, reg, stack_class, slot_writer):
    if o.type == capstone.x86.X86_OP_IMM:
        return ("imm", o.imm & 0xFFFFFFFF)
    if o.type == capstone.x86.X86_OP_REG:
        return reg.get(ins.reg_name(o.reg))
    if o.type == capstone.x86.X86_OP_MEM:
        return mem_prov(o, ins, reg, stack_class, slot_writer)
    return None


def lea_prov(o, ins, reg, stack_class, slot_writer):
    base = ins.reg_name(o.mem.base) if o.mem.base else None
    disp = o.mem.disp
    if base in ("esp", "ebp"):
        p = stack_class(disp)
        if p[0] == "param":
            return ("addr_param", p[1])
        return ("addr_local", p[1], slot_writer.get(p[1]))
    if base:
        bp = reg.get(base)
        if bp:
            if bp[0] == "this":
                return ("addr_member", disp)
            if bp[0] == "addr_member":
                return ("addr_member", bp[1] + disp)
            if bp[0] == "addr_local":
                off = bp[1] + disp
                return ("addr_local", off, slot_writer.get(off))
        return ("load", bp or ("reg", base), disp)
    return None


def print_hits(lo, name, hits, argc, nrv):
    if not hits:
        return 0
    for site, args in hits:
        off = site - lo
        # cdecl pushes right-to-left, so push order is REVERSED vs source order.
        # NRV operator+ takes the hidden result pointer as its 1st formal param,
        # which is therefore pushed LAST (args[-1]); drop it and reverse the rest.
        operands = list(reversed(args[:-1])) if nrv else list(reversed(args))
        labels = "abcdefgh"
        parts = []
        for i, p in enumerate(operands):
            parts.append(f"{labels[i]}={render(p)}")
        classes = " ".join(cls(p) for p in operands)
        print(f"  {site:#08x} (+{off:#x})  operator+( {',  '.join(parts)} )   [{classes}]")
    return len(hits)


def site_operand_classes(hits, nrv):
    """Per operator+ site (address order): (site, [class_a, class_b, ...])."""
    out = []
    for site, args in hits:
        operands = list(reversed(args[:-1])) if nrv else list(reversed(args))
        out.append((site, [cls(p) for p in operands]))
    return out


# ---- source side: classify `A + B` operands to diff against the binary order ---

def _split_top(expr, sep):
    """Split on top-level `sep`, respecting (), [], {} and string literals."""
    out, depth, i, start, instr = [], 0, 0, 0, None
    while i < len(expr):
        c = expr[i]
        if instr:
            if c == instr:
                j, bs = i - 1, 0           # even # of preceding backslashes = real close
                while j >= 0 and expr[j] == "\\":
                    bs += 1
                    j -= 1
                if bs % 2 == 0:
                    instr = None
        elif c in ('"', "'"):
            instr = c
        elif c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0 and expr[i:i + len(sep)] == sep:
            if sep == "+" and (expr[i + 1:i + 2] in ("+", "=") or expr[i - 1:i] == "+"):
                i += 1
                continue
            out.append(expr[start:i])
            start = i + len(sep)
            i += len(sep)
            continue
        i += 1
    out.append(expr[start:])
    return out


def _paren_groups(expr):
    out, depth, start = [], 0, None
    for i, c in enumerate(expr):
        if c == "(":
            if depth == 0:
                start = i + 1
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0 and start is not None:
                out.append(expr[start:i])
                start = None
    return out


def _classify_src(tok, params, locals_):
    t = tok.strip().strip("()").strip()
    if not t:
        return "?"
    if t[0] in "\"'":
        return "LITERAL"
    if t[0].isdigit() or t[0] == "-":
        return "IMM"
    if "(" in t:                                    # call / cast result
        return "RET"
    m = re.match(r"\w+", t)
    root = m.group(0) if m else ""
    if root in params:
        return "PARAM"
    if root.startswith("m_") or "->m_" in t or ".m_" in t or t.startswith("this->"):
        return "MEMBER"
    if root.startswith("g_") or "::" in t:
        return "GLOBAL"
    if root in locals_:
        return "LOCAL"
    if "." in t or "->" in t or "[" in t:
        return "?"
    return "LOCAL"                                   # bare identifier: assume local


def _is_noncstring(tok):
    """A `+` operand that is pointer/integer arithmetic, not a CString concat."""
    t = tok.strip()
    return bool(re.search(r"_cast<|\((?:char|BYTE|unsigned|int|DWORD|WORD|LONG|short)\b", t)) \
        or t.lstrip("-").isdigit()


def _is_cstring_ish(tok, locals_):
    """An operand plausibly of CString type (so the `+` is a concat, not int/ptr
    arithmetic): a string literal, a declared CString local, a string-Hungarian
    name (m_sX / sX / szX / strX), or a call result (may return CString)."""
    t = tok.strip().strip("()").strip()
    if t[:1] in ("\"", "'") or "(" in t:
        return True
    m = re.match(r"\w+", t)
    root = m.group(0) if m else ""
    return (root in locals_ or root.startswith(("m_s", "sz", "str"))
            or bool(re.match(r"s[A-Z]", root)))


def _chain_sites(ops, params, locals_):
    """ops = top-level `+`-split operand texts. Emit (lclass, rclass) per
    CString+CString (0x7fcdfd) site: a `+` whose right operand is non-literal and
    whose left (an operand, or the accumulated temp) is non-literal too. A
    `+ "lit"` is the LPCTSTR overload (different callee) and does not emit a site,
    but the running value stays a non-literal temp. Pointer/int `+` is skipped."""
    sites = []
    for i in range(1, len(ops)):
        left_text = ops[0] if i == 1 else ops[i - 1]
        if _is_noncstring(ops[i]) or (i == 1 and _is_noncstring(ops[0])):
            continue
        if not (_is_cstring_ish(left_text, locals_) or _is_cstring_ish(ops[i], locals_)):
            continue                                # neither operand is a CString -> int/ptr +
        rclass = _classify_src(ops[i], params, locals_)
        if i == 1:
            lclass = _classify_src(ops[0], params, locals_)
            left_nonlit = lclass != "LITERAL"
        else:
            lclass, left_nonlit = "RET", True
        if rclass != "LITERAL" and left_nonlit:
            sites.append((lclass, rclass))
    return sites


def _find_concats(expr, params, locals_, sites):
    ops = [o.strip() for o in _split_top(expr, "+")]
    if len(ops) >= 2:
        sites.extend(_chain_sites(ops, params, locals_))
    for sub in _paren_groups(expr):                 # recurse into call args / groups
        for arg in _split_top(sub, ","):
            if "+" in arg:
                _find_concats(arg, params, locals_, sites)


def parse_params(sig, body):
    text = sig if "(" in sig else body.split("\n", 1)[0]
    m = re.search(r"\((.*)\)", text)
    if not m:
        return set()
    params = set()
    for part in _split_top(m.group(1), ","):
        ids = re.findall(r"\w+", part)
        if ids and part.strip() not in ("void", ""):
            params.add(ids[-1])
    return params


def fn_source(name):
    """(params, CString-locals, comment-stripped body) for a recovered fn, via
    src_find.py's index line + the .cpp slice. None if not found."""
    import subprocess
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        out = subprocess.run([sys.executable, os.path.join(repo, "scripts", "src_find.py"), name],
                             capture_output=True, text=True, timeout=30).stdout
    except Exception:
        return None
    m = re.search(r"(\S+\.(?:cpp|h)):\d+\s+0x[0-9a-fA-F]+\s+\[(\d+)-(\d+)\]\s+(.*)", out)
    if not m:
        return None
    rel, start, end, sig = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
    try:
        lines = open(os.path.join(repo, rel)).read().splitlines()
    except Exception:
        return None
    body = re.sub(r"//.*", "", "\n".join(lines[start - 1:end]))
    params = parse_params(sig, body)
    # CString names = CString-typed params + CString locals (incl multi-decl) --
    # drives both the concat filter and LOCAL classification, so non-Hungarian
    # CStrings (e.g. `a3`) aren't mistaken for int/ptr operands.
    cstring = set()
    sigtext = sig if "(" in sig else body.split("\n", 1)[0]
    pm = re.search(r"\((.*)\)", sigtext)
    if pm:
        for part in _split_top(pm.group(1), ","):
            if "CString" in part:
                ids = re.findall(r"\w+", part)
                if ids:
                    cstring.add(ids[-1])
    for decl in re.findall(r"\bCString\s+(\w+(?:\s*,\s*\w+)*)\s*[;=]", body):
        cstring |= {w for w in re.split(r"\W+", decl) if w}
    return params, cstring, body


def source_concat_sites(name):
    src = fn_source(name)
    if not src:
        return None
    params, locals_, body = src
    if "{" in body and "}" in body:                 # isolate fn body from signature
        body = body[body.find("{") + 1: body.rfind("}")]
    sites = []
    for stmt in re.split(r";", body):
        if "+" in stmt:
            _find_concats(re.sub(r"\breturn\b", " ", stmt), params, locals_, sites)
    return sites


def _compat(b, s):
    """Is binary class `b` consistent with source class `s` (loose, to avoid
    false swaps -- a swap is only reported when the DIRECT order fails)."""
    if b == "?" or s == "?":
        return True
    if b == s:
        return True
    # a binary operand origin-traced to a member/global often appears as a plain
    # local (a copy) in source -- treat those as consistent. PARAM stays strict
    # (it is the strongest swap signal), so a real param<->x reversal still flags.
    ok = {("IMM", "LITERAL"), ("GLOBAL", "MEMBER"), ("RET", "LOCAL"),
          ("GLOBAL", "LOCAL"), ("RET", "MEMBER"), ("MEMBER", "LOCAL")}
    return (b, s) in ok or (s, b) in ok


def cmd_check(addr, callee, argc, nrv, quiet=False):
    """Diff binary operand order vs source `A + B` order; flag SWAP? on mismatch."""
    lo, name, hits = audit_fn(addr, callee, argc, nrv)
    binsites = site_operand_classes(hits, nrv)
    src = source_concat_sites(name)
    n_ok = n_rev = n_swap = n_stub = 0
    out = []
    if src is None:
        out.append("  (no source)")
        n_rev = len(binsites)
    elif not src and binsites:
        # binary concatenates but the source has none -> not (fully) recovered,
        # or the concat is inlined from a helper. Not a swap, not a parser miss.
        out.append(f"  STUB/inlined: binary has {len(binsites)} operator+ site(s), source has 0")
        n_stub = len(binsites)
    elif len(src) != len(binsites):
        out.append(f"  COUNT MISMATCH binary={len(binsites)} source={len(src)} -> review")
        n_rev = max(len(binsites), len(src))
    else:
        for (site, bcl), (sl, sr) in zip(binsites, src):
            b = (bcl + ["?", "?"])[:2]
            direct = _compat(b[0], sl) and _compat(b[1], sr)
            swap = _compat(b[0], sr) and _compat(b[1], sl)
            if direct:
                tag = "OK"; n_ok += 1
            elif swap:
                tag = "SWAP?"; n_swap += 1
            else:
                tag = "review"; n_rev += 1
            out.append(f"  +{site - lo:#06x}  bin[{b[0]} {b[1]}]  src[{sl} {sr}]  {tag}")
    if not quiet or n_swap or n_rev:
        print(f"{name}  {lo:#x}")
        for ln in out:
            print(ln)
    return n_ok, n_rev, n_swap, n_stub


def main():
    args = sys.argv[1:]
    callee = OP_CONCAT
    argc = 3
    nrv = True
    if "--callee" in args:
        i = args.index("--callee")
        callee = int(args[i + 1], 16)
        del args[i:i + 2]
    if "--args" in args:
        i = args.index("--args")
        argc = int(args[i + 1])
        del args[i:i + 2]
    if "--nrv" in args:
        nrv = True
        args.remove("--nrv")

    check = "--check" in args
    if check:
        args.remove("--check")

    if "--sweep" in args:
        args.remove("--sweep")
        return sweep(callee, argc, nrv, check)

    if not args:
        print(__doc__)
        return 0
    addr = resolve_name(args[0])
    if check:
        # Gate semantics: a suspected operand SWAP is a defect, so exit 1.
        # `review` is inconclusive and stays exit 0 (a warning for the caller).
        return 1 if cmd_check(addr, callee, argc, nrv)[2] else 0
    lo, name, hits = audit_fn(addr, callee, argc, nrv)
    print(f"{name}  {lo:#x}  ({len(hits)} operator+ site(s))")
    print_hits(lo, name, hits, argc, nrv)
    return 0


def sweep(callee, argc, nrv, check=False):
    # all call sites to callee, grouped by containing recovered fn
    import collections
    sites = collections.defaultdict(list)
    for sec in sym.pe().sections:
        if not sec.Name.startswith(b".text"):
            continue
        data = sec.get_data()
        base = sym.image_base() + sec.VirtualAddress
        import struct
        off = data.find(b"\xe8")
        while off != -1:
            (rel,) = struct.unpack_from("<i", data, off + 1)
            site = base + off
            if (site + 5 + rel) & 0xFFFFFFFF == callee:
                lo, hi, nm = fn_bounds(site)
                if nm and not nm.startswith("FUN_"):
                    sites[(lo, nm)].append(site)
            off = data.find(b"\xe8", off + 1)
    if check:
        t_ok = t_rev = t_swap = t_stub = 0
        for (lo, nm), sl in sorted(sites.items()):
            o, r, s, st = cmd_check(lo, callee, argc, nrv, quiet=True)
            t_ok += o; t_rev += r; t_swap += s; t_stub += st
        print(f"\n== check: {t_ok} OK, {t_rev} review, {t_swap} SWAP?, "
              f"{t_stub} stub/inlined site(s) across {len(sites)} recovered fns ==")
        return 1 if t_swap else 0
    total = 0
    for (lo, nm), sl in sorted(sites.items()):
        _, _, hits = audit_fn(lo, callee, argc, nrv)
        if hits:
            print(f"\n{nm}  {lo:#x}")
            total += print_hits(lo, nm, hits, argc, nrv)
    print(f"\n== {total} operator+ operand sites across {len(sites)} recovered fns ==")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
