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
  arg_provenance.py --sweep                   every recovered fn calling the callee
      [--callee 0xADDR]   order-sensitive target (default 0x7fcdfd =
                          CString operator+(const CString&,const CString&))
      [--args N]          callee arg count (default 3: NRV result + a + b)
      [--nrv]             arg0 is the hidden return slot (default on for operator+)

Per site it prints the operand provenance IN BINARY ORDER. The binary is ground
truth -- pair the printed order against your source's `a + b` and confirm they
agree. Provenance classes (param{k} / loc / this+off / ret(fn) / imm) make the
pairing a one-line eyeball even when both operands are CStrings.
"""

import os
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

    if "--sweep" in args:
        args.remove("--sweep")
        sweep(callee, argc, nrv)
        return

    if not args:
        print(__doc__)
        return
    addr = resolve_name(args[0])
    lo, name, hits = audit_fn(addr, callee, argc, nrv)
    print(f"{name}  {lo:#x}  ({len(hits)} operator+ site(s))")
    print_hits(lo, name, hits, argc, nrv)


def sweep(callee, argc, nrv):
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
    total = 0
    for (lo, nm), sl in sorted(sites.items()):
        _, _, hits = audit_fn(lo, callee, argc, nrv)
        if hits:
            print(f"\n{nm}  {lo:#x}")
            total += print_hits(lo, nm, hits, argc, nrv)
    print(f"\n== {total} operator+ operand sites across {len(sites)} recovered fns ==")


if __name__ == "__main__":
    main()
