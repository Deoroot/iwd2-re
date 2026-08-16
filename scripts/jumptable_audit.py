#!/usr/bin/env python3
"""jumptable_audit.py - diff a binary switch's jump table against the source's `case` labels.

This is the audit that found the three missing arms of
`CGameSprite::CheckFeatPrerequisites` (POWER_ATTACK was one of them, which made
the feat dead in our build). Parity is blind to it: a `case` that returns FALSE
where the binary has a real body has the same call counts as the binary.

The method, generalized:

  1. Disassemble the function -- BOUNDED by its Ghidra end -- and find every
     indirect `jmp dword ptr [reg*4 + TABLE]`, plus the preceding byte index
     table `mov cl, byte ptr [reg + IDXTABLE]` when MSVC emitted the two-level
     form.
  2. Recover each switch's bounds from the `dec`/`sub`/`add`/`lea` bias on the
     INDEX REGISTER plus the `cmp` that guards the `ja` to the default arm.
  3. Read the table(s) out of the PE and group case VALUES by target address.
  4. Parse the source function, splitting its `case` labels per `switch` and
     resolving each through the project's `#define`s, file-scope `const`s and
     enumerators.
  5. Pair each binary table with the source switch its values overlap most, then
     report the differences.

Verdicts, most actionable first:
  MISSING       a binary arm no source case reaches   <- the POWER_ATTACK class
  INCOMPLETE    the source case is a `// TODO: Incomplete.` stub, binary has a body
  DEFAULT       source has a case the binary sends to the default arm
  SPLIT         source merges cases the binary gives different bodies
  CLONE         same, but the bodies are instruction-identical: MERGING IS CORRECT
  OUT-OF-RANGE  a source case value outside the table
  NO-SWITCH-IN-SOURCE / TABLE COUNT / UNMATCHED-SOURCE-SWITCH -- structural

ONE table and ONE switch is the only shape where the pairing is a fact. With
several of either, the value-overlap match is a guess, and a wrong guess prints
a wall of MISSING/DEFAULT about correct code -- so those functions report
AMBIGUOUS-PAIRING and nothing else unless `--multi` asks for the diff. INCOMPLETE
still fires there: it needs no pairing to be true.

False-positive classes handled, each one paid for at least once:

  * MSVC CLONES a block to resolve a later branch early, so two targets can hold
    instruction-identical bodies (AEGIS_OF_RIME vs the other three bard-song
    feats). Merging those cases in source is CORRECT, and CLONE says so.
  * A clone's blocks are not byte-identical: each carries its own MSVC EH state
    index (`mov dword ptr [esp + 0xNN], <n>` inside a `new`+ctor region), so
    CGameSprite::SetMonkAbilities' cases 13/14 vs 15 differ by one constant while
    both construct "00MFIST6". That store is collapsed; string and vtable
    POINTERS are NOT, or a genuinely different resref would pass as a clone.
  * A naive name filter breaks on a constant sharing a prefix with a `_MAX_`
    ceiling `#define` (MAXIMIZED_ATTACKS read as missing while it was present).
    Values, never names, are what gets compared here.
  * The offset fold below must strictly IMPROVE the match, not merely be
    possible: any sparse switch over a wide table admits a shift, and a loose
    test moved GetSndWalk -- correct as written -- onto four bogus MISSING lines.
  * A NORMALIZED switch expression -- `switch (m_nModalState - 1)` with cases
    0..3 against a binary table of 1..4 -- reads term-by-term as every case both
    MISSING and OUT-OF-RANGE. The constant offset is detected and folded in.
  * The default arm is taken from the `ja`, never from the most common target:
    CheckFeatPrerequisites' shared `return TRUE` body has 20 arms and the real
    default has none.
  * Ghidra's function end is the next function GHIDRA HAS DEFINED, so every
    UNDEFINED function in between falls inside the extent -- with its jump
    table. CGameEffectBaseAttackBonus::ApplyEffect is 0x40 bytes of `dec`/`je`
    compare chain and owns no table at all, yet its extent reached 0x4b31b4,
    a table belonging to an unrelated SEH function 0x320 bytes further on,
    whose seven arms read as four MISSING cases against the wrong source body.
    Bounding cannot fix this; a table now counts only when control flow reaches
    its `jmp` from the entry (`reachable_from`).
  * A `case` group ends at the switch's CLOSING BRACE, not at the end of the
    function. Scanning on past it let the last group of the last switch absorb
    the whole tail, so CScreenCreateChar::UpdateCharacterStats' SORCERER/WIZARD
    arm inherited a `// TODO: Incomplete (base skills).` written 46 lines below
    the switch and reported INCOMPLETE against a table that matched exactly.

Usage:
  jumptable_audit.py 0x763200                 # audit one recovered function
  jumptable_audit.py CGameSprite::CheckModal  # by name
  jumptable_audit.py --sweep                  # every recovered fn with a jump table
  jumptable_audit.py --sweep --multi          # ... including the guessed pairings
  jumptable_audit.py 0x763200 --prefix CGAMESPRITE_FEAT_   # name the values

Exit 0 = nothing actionable, 1 = at least one finding, 2 = could not resolve.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sym  # noqa: E402

REPO = sym.REPO
SRC = os.path.join(REPO, "src")

# ---------------------------------------------------------------- #define map


def define_map():
    """{NAME: int} over the three ways this codebase spells a case constant.

    Headers use `#define`, but a fair number of switches label their cases with
    file-scope `const BYTE NAME = N;` declared in the .cpp (CItem's
    ITEM_ANIMATION_*) or with an enumerator. Missing either just makes every
    case in that switch unresolvable, which reads as a wall of MISSING."""
    out = {}
    define = re.compile(r"^#define\s+([A-Za-z_]\w*)\s+(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*(?://.*)?$")
    const = re.compile(r"^\s*(?:static\s+)?const\s+\w+\s+([A-Za-z_]\w*)\s*=\s*"
                       r"(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*;")
    enumerator = re.compile(r"^\s*([A-Z_]\w*)\s*(?:=\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*)?,?\s*(?://.*)?$")
    for root, _dirs, files in os.walk(SRC):
        for fn in sorted(files):
            if not fn.endswith((".h", ".cpp")):
                continue
            try:
                lines = open(os.path.join(root, fn), errors="replace").read().splitlines()
            except OSError:
                continue
            in_enum, next_value = False, 0
            for line in lines:
                if re.match(r"^\s*(typedef\s+)?enum\b", line):
                    in_enum, next_value = True, 0
                    continue
                if in_enum:
                    if "}" in line:
                        in_enum = False
                        continue
                    me = enumerator.match(line)
                    if me:
                        if me.group(2) is not None:
                            next_value = int(me.group(2), 0)
                        out.setdefault(me.group(1), next_value)
                        next_value += 1
                    continue
                m = define.match(line.rstrip()) or const.match(line)
                if m:
                    out.setdefault(m.group(1), int(m.group(2), 0))
    return out


def resolve_label(expr, defines):
    """Evaluate a case label: a literal, a #define, or a simple arithmetic mix."""
    expr = expr.strip()
    try:
        return int(expr, 0)
    except ValueError:
        pass
    if expr in defines:
        return defines[expr]
    # Allow `FOO + 1`, `FOO - 1`, and character literals.
    if re.fullmatch(r"'\\?.'", expr):
        return ord(expr.strip("'").lstrip("\\") or "\0")
    tokens = re.findall(r"[A-Za-z_]\w*", expr)
    if tokens and all(t in defines for t in tokens):
        safe = re.sub(r"[A-Za-z_]\w*", lambda m: str(defines[m.group(0)]), expr)
        if re.fullmatch(r"[-+*/() 0-9]+", safe):
            try:
                return int(eval(safe))  # noqa: S307 - operands are all ints from our own map
            except Exception:
                return None
    return None


# ------------------------------------------------------- binary side: the table

JMP_TBL = re.compile(r"jmp\s+dword ptr \[(\w+)\*4 \+ (0x[0-9a-f]+)\]")
IDX_TBL = re.compile(r"mov\s+\w+, byte ptr \[(\w+) \+ (0x[0-9a-f]+)\]")

# Every one of these must be matched against the register that actually indexes
# the table. Accepting any `lea`/`sub`/`cmp` within a few instructions of the
# `jmp` reads the prologue's arithmetic as the switch's bias -- that is what
# turned CItem::Equip's cases 1..4 into "-68..-65" on the first sweep.
CMP_IMM = re.compile(r"cmp (\w+), (0x[0-9a-f]+|\d+)$")
BIAS_LEA = re.compile(r"lea (\w+), \[\w+ ([-+]) (0x[0-9a-f]+|\d+)\]$")
BIAS_SUB = re.compile(r"sub (\w+), (0x[0-9a-f]+|\d+)$")
BIAS_ADD = re.compile(r"add (\w+), (0x[0-9a-f]+|\d+)$")
BIAS_DEC = re.compile(r"dec (\w+)$")
MOV_REG = re.compile(r"mov (\w+), (\w+)$")

REG32 = {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"}


def disasm_lines(start, count):
    """[(va, "mnemonic operands")] - capstone directly; sym.cmd_disasm only prints."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    out = []
    for ins in md.disasm(sym.read(start, max(16 * count, 64)), start):
        out.append((ins.address, f"{ins.mnemonic} {ins.op_str}".strip()))
        if len(out) >= count:
            break
    return out


def fn_end(start):
    """VA one past the last byte of the Ghidra function starting at `start`.

    Bounding the scan is not optional: without it a 400-instruction sweep runs
    off the end of a short function and finds the NEXT function's jump table,
    which then diffs against the wrong source body. That produced 545 bogus
    'MISSING' functions on the first whole-repo run.

    This is an UPPER bound only, and a loose one: it is the next function GHIDRA
    HAS DEFINED, so every undefined function in between falls inside it. Real
    containment is decided by `reachable_from` below."""
    fns = sym.ghidra_funcs()
    if not fns:
        return start + 0x4000
    import bisect
    i = bisect.bisect_right(fns, (start, "\xff"))
    return fns[i][0] if i < len(fns) else start + 0x4000


TERMINATOR = re.compile(r"^(ret|retf|iret|int3|ud2|jmp|hlt)\b")
DIRECT_BRANCH = re.compile(r"^(jmp|j[a-z]{1,3}|loopn?e?|loop)\s+(0x[0-9a-f]+)$")


def reachable_from(start, end, lines):
    """The subset of `lines` control flow can actually reach from `start`.

    Ghidra's end is the next DEFINED function, so when the functions in between
    were never defined -- common in this project -- a short function's extent
    swallows several unrelated ones. `CGameEffectBaseAttackBonus::ApplyEffect`
    is 0x40 bytes of `dec eax`/`je` compare chain with no table at all, yet its
    Ghidra extent reaches 0x4b31b4, an unrelated SEH function's table, whose
    seven arms then read as four MISSING cases against the wrong source body.
    Bounding by the next defined function cannot catch that; reachability can.

    Falling off the end of the linear sweep is also handled: a branch into a
    region capstone desynced on (embedded data) is re-disassembled from the
    target, so real code after a jump table is not silently lost."""
    import bisect
    by_va = dict(lines)
    order = sorted(by_va)               # kept sorted alongside `by_va`

    def resync(va):
        """Disassemble from `va` when the linear sweep desynced past it."""
        more = [l for l in disasm_lines(va, 256) if l[0] < end]
        if not more or more[0][0] != va:
            return False
        for a, t in more:
            if a not in by_va:
                by_va[a] = t
                bisect.insort(order, a)
        return True

    seen = set()
    work = [start]
    tables = set()
    while work:
        va = work.pop()
        while start <= va < end and va not in seen:
            if va not in by_va and not resync(va):
                break
            text = by_va[va]
            seen.add(va)
            mb = DIRECT_BRANCH.match(text)
            if mb:
                work.append(int(mb.group(2), 16))
            else:
                mt = JMP_TBL.search(text)
                if mt:
                    tables.add(int(mt.group(2), 16))
            if TERMINATOR.match(text):
                break
            i = bisect.bisect_right(order, va)
            if i >= len(order):
                break
            va = order[i]
        # An arm body is reachable only through its table, so once the linear
        # walk stalls, feed the discovered tables' targets back in -- otherwise a
        # switch nested inside a case arm reads as unreachable.
        if not work and tables:
            for tbl in tables:
                for i in range(1024):
                    t = int.from_bytes(sym.read(tbl + 4 * i, 4), "little")
                    if not (start <= t < end):
                        break
                    if t not in seen:
                        work.append(t)
            tables = set()
    return seen


def find_switch(start, limit=None):
    """The FIRST switch dispatch in this function, or None. See find_switches."""
    found = find_switches(start, limit)
    return found[0] if found else None


def find_switches(start, limit=None):
    """EVERY switch dispatch inside this function, in address order.

    Reading only the first one is what produced 1279 bogus OUT-OF-RANGE lines on
    the first whole-repo sweep: a function with two switches got all of its
    source cases diffed against one of the two tables."""
    end = fn_end(start)
    # One instruction per 2 bytes is a safe upper bound for x86.
    span = min(max(end - start, 32), 0x8000)
    lines = [l for l in disasm_lines(start, limit or span // 2 + 8) if l[0] < end]
    reach = reachable_from(start, end, lines)
    out = []
    for i, (va, text) in enumerate(lines):
        m = JMP_TBL.search(text)
        if not m:
            continue
        # Ghidra's end runs to the next DEFINED function, so an undefined one in
        # between lands inside this extent with its table. Only dispatches
        # control flow actually reaches from the entry belong to this function.
        if va not in reach:
            continue
        info = {"jmp_va": va, "jump_table": int(m.group(2), 16), "index_table": None,
                "bias": 0, "count": None, "default": None}
        # `key` is the register the SWITCH VALUE lives in. For the two-level form
        # the jmp indexes with the byte pulled from the index table, so the value
        # is really in that table's base register, not the jmp's.
        key = m.group(1)
        # Walk backwards for the byte index table, the bound, the bias and the
        # out-of-range branch. That `ja` names the DEFAULT arm -- do not guess it
        # from the most common target: a big shared `return TRUE` body outvotes
        # the real default (CheckFeatPrerequisites, 20 arms vs the default's 0).
        for _back_va, back in reversed(lines[max(0, i - 14):i]):
            mi = IDX_TBL.search(back)
            if mi and info["index_table"] is None and int(mi.group(2), 16) > 0x400000:
                info["index_table"] = int(mi.group(2), 16)
                key = mi.group(1)
                continue
            mj = re.match(r"ja (0x[0-9a-f]+)$", back)
            if mj and info["default"] is None:
                info["default"] = int(mj.group(1), 16)
                continue
            mc = CMP_IMM.match(back)
            if mc and mc.group(1) == key and info["count"] is None:
                info["count"] = int(mc.group(2), 0) + 1
                continue
            if info["bias"]:
                continue
            mb = BIAS_LEA.match(back)
            if mb and mb.group(1) == key:
                info["bias"] = int(mb.group(3), 0) * (-1 if mb.group(2) == "-" else 1)
                break
            ms = BIAS_SUB.match(back)
            if ms and ms.group(1) == key:
                info["bias"] = -int(ms.group(2), 0)
                break
            ma = BIAS_ADD.match(back)
            if ma and ma.group(1) == key:
                info["bias"] = int(ma.group(2), 0)
                break
            md = BIAS_DEC.match(back)
            if md and md.group(1) == key:
                info["bias"] = -1
                break
            # `mov key, other` retargets the walk at the source register.
            mm = MOV_REG.match(back)
            if mm and mm.group(1) == key and mm.group(2) in REG32:
                key = mm.group(2)
        out.append(info)
    return out


def read_tables(info):
    """Return {case_value: target_va} plus the default target."""
    n = info["count"]
    if not n or n > 1024:
        return None, None
    # `lea eax, [edi - 3]` means the table's slot 0 is case value 3.
    first_value = -info["bias"]
    if info["index_table"]:
        idx = sym.read(info["index_table"], n)
        slots = max(idx) + 1
        jt = [int.from_bytes(sym.read(info["jump_table"] + 4 * s, 4), "little")
              for s in range(slots)]
        default = info["default"] or jt[max(idx)]
        return {first_value + i: jt[b] for i, b in enumerate(idx)}, default
    jt = [int.from_bytes(sym.read(info["jump_table"] + 4 * i, 4), "little")
          for i in range(n)]
    return {first_value + i: t for i, t in enumerate(jt)}, info["default"]


# ------------------------------------------------------- source side: the cases


def _uncomment(code):
    """Drop comments and literals so brace counting cannot be fooled by them."""
    code = re.sub(r"//.*$", "", code)
    code = re.sub(r"/\*.*?\*/", "", code)
    code = re.sub(r"'(?:\\.|[^'])*'", "''", code)
    code = re.sub(r'"(?:\\.|[^"])*"', '""', code)
    return code


def source_switches(name, defines):
    """[(labels, first_line, is_stub)] - one entry per source `case` group.

    `is_stub` marks a group whose body is a `// TODO: Incomplete.` placeholder.
    Those are the arms this audit would otherwise be blind to: the case EXISTS,
    so it is not MISSING, but the binary has a real body behind it.

    A switch ends at its closing brace, tracked by brace depth. Without that the
    LAST case group of the LAST switch stays open to the end of the function and
    absorbs every line after it -- which is how an unrelated
    `// TODO: Incomplete (base skills).` in the tail of
    CScreenCreateChar::UpdateCharacterStats marked its SORCERER/WIZARD arm a stub
    while the binary's table matched the source exactly."""
    import subprocess
    r = subprocess.run([sys.executable, os.path.join(REPO, "scripts", "src_find.py"),
                        name, "--body"], capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr.strip()
    body = r.stdout
    switches = []          # [[group, ...], ...] - one list per `switch` statement
    groups = []
    pending = []
    pending_line = None
    in_body = False
    stub = False
    depth = None           # None = outside a switch; int = brace depth within one

    def close_group():
        nonlocal pending, pending_line, in_body, stub
        if pending:
            groups.append((pending, pending_line, stub))
        pending, pending_line, in_body, stub = [], None, False, False

    def close_switch():
        nonlocal groups, depth
        close_group()
        if groups:
            switches.append(groups)
            groups = []
        depth = None

    for line in body.splitlines():
        m = re.match(r"^\s*(\d+)\t(.*)$", line)
        if not m:
            continue
        lineno, code = int(m.group(1)), m.group(2)
        bare = _uncomment(code)
        # A second `switch` starts a second table's worth of cases. Keeping them
        # in one flat list is what made every case of switch #2 read as MISSING
        # from switch #1. This test comes FIRST and is NOT gated on brace depth:
        # gating it cost CRuleTables::GetRaceString's eight switches, which
        # collapsed into one the moment a closing brace went unseen.
        if re.search(r"\bswitch\s*\(", bare):
            close_switch()
            depth = bare.count("{") - bare.count("}")
            continue
        if depth is None:
            continue
        mc = re.match(r"^\s*case\s+(.+?)\s*:\s*(.*)$", code)
        if mc:
            if in_body:  # a new label after a body closes the previous group
                close_group()
            if pending_line is None:
                pending_line = lineno
            pending.append((mc.group(1).strip(), resolve_label(mc.group(1), defines)))
            tail = mc.group(2).strip()
        else:
            tail = code
        if pending and tail:
            in_body = True
            if "TODO: Incomplete" in code:
                stub = True
        was, depth = depth, depth + bare.count("{") - bare.count("}")
        if was > 0 and depth <= 0:
            close_switch()
    close_switch()
    return switches, None


# ----------------------------------------------------------------- clone check


# `mov dword ptr [esp + 0xNN], <imm>` right inside a `new`+ctor block is MSVC's
# EH state index, one distinct value per block, reset to -1 when the region ends.
# It is bookkeeping, not behaviour: CGameSprite::SetMonkAbilities emits one block
# per state, so cases 13/14 and case 15 differ ONLY in that constant while both
# construct "00MFIST6". Collapse it, or every such pair reads as a real SPLIT.
EH_STATE = re.compile(r"^(mov dword ptr \[esp \+ 0x[0-9a-f]+\], )(?:0x[0-9a-f]{1,2}|\d{1,3})$")
BRANCH_TARGET = re.compile(r"0x[0-9a-f]+$")


def identical_bodies(a, b, span=24):
    """True when two case targets hold the same code up to bookkeeping (MSVC clone).

    Normalizes JUMP targets and the EH state constant -- and nothing else. An
    earlier version normalized every 6-8 digit hex operand, which also erased
    string and vtable POINTERS: two arms constructing different resrefs would
    have compared equal and been dismissed as a clone. `call` operands stay
    verbatim for the same reason; a clone calls the same callee anyway.

    Deliberately STRUCTURAL, so it under-reports: cloned blocks are register
    -allocated independently, and the difference is not always a consistent
    renaming. ReadyCursor's cases 5/6 do the same three things in the same order
    yet one reuses `ecx` as its own source where the other reads `eax`, so no
    alpha-renaming makes them equal. Those come out SPLIT and a human has to read
    the two bodies. Loosening this to ignore registers outright WOULD collapse
    them -- and would also collapse an operand-order swap, the exact defect class
    lint_twin_symmetry.py exists to catch. Under-reporting is the safe direction:
    a SPLIT costs a manual read, a wrong CLONE hides a bug."""
    la = disasm_lines(a, span)
    lb = disasm_lines(b, span)
    if not la or not lb:
        return False

    def norm(lines):
        out = []
        for _va, text in lines:
            mnemonic = text.split(" ")[0]
            if mnemonic.startswith("j"):
                text = BRANCH_TARGET.sub("<t>", text)
            else:
                text = EH_STATE.sub(r"\1<ehstate>", text)
            out.append(text)
            if mnemonic in ("ret", "jmp"):
                break
        return out

    na, nb = norm(la), norm(lb)
    return len(na) > 1 and na == nb


# ---------------------------------------------------------------------- report


def audit(target, prefix=None, show_clones=True, multi=False):
    import subprocess
    va, name = None, None
    if target.lower().startswith("0x"):
        va = int(target, 16)
        name = sym.addr2name(va, exact_only=True)
    else:
        name = target
    if va is None:
        r = subprocess.run([sys.executable, os.path.join(REPO, "scripts", "src_find.py"), name],
                           capture_output=True, text=True)
        m = re.search(r"(0x[0-9A-Fa-f]{6,8})", r.stdout)
        if m:
            va = int(m.group(1), 16)
    if va is None:
        print(f"{target}: cannot resolve an address")
        return 2

    infos = find_switches(va)
    if not infos:
        print(f"{name or hex(va)}: no jump table inside this function")
        return 0

    defines = define_map()
    switches, err = source_switches(name, defines) if name else (None, "no source name")

    print(f"== {name or hex(va)}  {va:#x}")
    if switches is None:
        print(f"   source: NOT PARSED ({err})")
        return 2

    # One table and one switch is the only shape where the pairing is a FACT.
    # With several of either, matching by value overlap is a guess, and a wrong
    # guess prints a wall of MISSING/DEFAULT about code that is fine. Say so
    # rather than burying the real findings; --multi opts back in.
    confident = len(infos) == 1 and len(switches) == 1
    if len(infos) != len(switches):
        print(f"   TABLE COUNT: {len(infos)} jump table(s) in the binary vs "
              f"{len(switches)} switch(es) in source"
              + ("   [the extra table(s) are most likely inside an inlined callee]"
                 if len(infos) > len(switches) else ""))
    if not confident and not multi:
        print("   AMBIGUOUS-PAIRING - more than one table or switch here; rerun with "
              "--multi to see the per-table diff")
        # INCOMPLETE survives an ambiguous pairing: "this source case is a
        # `// TODO: Incomplete.` stub and SOME binary table sends its value at a
        # non-default arm" needs no pairing to be true.
        rc = 0
        for info in infos:
            table, default = read_tables(info)
            if table is None:
                continue
            for sw in switches:
                for labels, line, stub in sw:
                    if not stub:
                        continue
                    hit = [v for _t, v in labels
                           if v is not None and table.get(v) not in (None, default)]
                    if hit:
                        print(f"   INCOMPLETE   {', '.join(str(v) for v in hit)} "
                              f"@{table[hit[0]]:#x}")
                        print(f"                source case is a `// TODO: Incomplete.` stub "
                              f"but the binary has a body, src line {line}")
                        rc = 1
        return rc

    # Pair each binary table with the source switch whose value set overlaps it
    # most. Position is NOT a safe pairing key: MSVC reorders, and an inlined
    # callee can drop a table between two of ours.
    tables = []
    for info in infos:
        table, default = read_tables(info)
        if table is not None:
            tables.append((info, table, default))
        else:
            print(f"   jump table {info['jump_table']:#x}: bounds not recovered, skipped")

    def values_of(sw):
        return {v for labels, _l, _s in sw for _t, v in labels if v is not None}

    used = set()
    rc = 0
    for info, table, default in tables:
        best, best_score = None, -1
        for si, sw in enumerate(switches):
            if si in used:
                continue
            score = len(values_of(sw) & set(table))
            if score > best_score:
                best, best_score = si, score
        if best is not None and best_score > 0:
            used.add(best)
        rc = max(rc, audit_one(name, info, table, default,
                               switches[best] if best is not None and best_score > 0 else [],
                               defines, prefix, show_clones))
    for si, sw in enumerate(switches):
        if si not in used and values_of(sw):
            print(f"   UNMATCHED-SOURCE-SWITCH at src line "
                  f"{sw[0][1]} - {len(values_of(sw))} case values with no binary table")
            rc = max(rc, 1)
    return rc


def audit_one(name, info, table, default, groups, defines, prefix, show_clones):
    by_target = {}
    for value, t in sorted(table.items()):
        by_target.setdefault(t, []).append(value)
    real = {t: v for t, v in by_target.items() if t != default}

    print(f"   jump table {info['jump_table']:#x}"
          + (f"   index table {info['index_table']:#x}" if info["index_table"] else "")
          + f"   {info['count']} case values from {-info['bias']}")
    print(f"   binary: {len(real)} real arms, {len(by_target.get(default, []))} -> default {default:#x}")

    def label(v):
        if prefix:
            for n, val in defines.items():
                if val == v and n.startswith(prefix):
                    return f"{n}({v})"
        return str(v)

    src_values = {}
    for gi, (labels, line, _stub) in enumerate(groups):
        for _txt, val in labels:
            if val is not None:
                src_values[val] = gi
    unresolved = [txt for labels, _l, _s in groups for txt, val in labels if val is None]
    print(f"   source: {len(groups)} case groups, {len(src_values)} values"
          + (f", {len(unresolved)} labels unresolved" if unresolved else ""))

    # A source switch may normalize its expression -- `switch (m_nModalState - 1)`
    # with cases 0..3 against a binary table of 1..4 (CGameSprite::CheckModal).
    # That is a faithful rewrite, not a gap, but term-by-term it reads as every
    # case MISSING plus every case OUT-OF-RANGE. Detect the constant and fold it.
    # Fold only when it strictly IMPROVES the match. A weaker test ("the shifted
    # values all exist in the table") fires on any sparse switch whose table
    # spans a wide range, and shifted GetSndWalk -- which is correct as written
    # -- into four bogus MISSING lines.
    def landed(vals):
        return sum(1 for v in vals if table.get(v) not in (None, default))

    if src_values and table and set(src_values) != set(table):
        k = min(table) - min(src_values)
        if (k != 0
                and {v + k for v in src_values} <= set(table)
                and landed(v + k for v in src_values) > landed(src_values)):
            src_values = {v + k: gi for v, gi in src_values.items()}
            groups = [([(t, v + k if v is not None else None) for t, v in labels], line, stub)
                      for labels, line, stub in groups]
            print(f"   NOTE: source case values are offset by {-k:+d} from the binary's "
                  f"(a normalized switch expression); compared after folding it in")

    findings = []
    if not src_values:
        # Not N missing cases -- ONE fact: our source has no switch here at all.
        # Usually the table belongs to a callee MSVC inlined, occasionally it is
        # a whole unrecovered block. Either way, N MISSING lines say less.
        print(f"   NO-SWITCH-IN-SOURCE - {len(real)} binary arms, source has no `case` "
              f"labels; the table is most likely inside an inlined callee")
        return 1
    for t, vals in sorted(real.items()):
        missing = [v for v in vals if v not in src_values]
        if missing:
            findings.append(("MISSING", t, missing,
                             f"binary arm at {t:#x} has no source case"))
    for v, gi in sorted(src_values.items()):
        if v not in table:
            findings.append(("OUT-OF-RANGE", None, [v],
                             f"source case is outside the table's {info['count']} slots"))
        elif table[v] == default:
            findings.append(("DEFAULT", None, [v],
                             "source has a case where the binary falls to the default arm"))

    # Split groups: values the binary sends to different arms but source merges.
    for gi, (labels, line, stub) in enumerate(groups):
        targets = {table.get(val) for _t, val in labels if val is not None and val in table}
        targets.discard(None)
        if stub and targets and not targets <= {default}:
            findings.append(("INCOMPLETE", sorted(targets)[0],
                             [val for _t, val in labels if val is not None],
                             f"source case is a `// TODO: Incomplete.` stub but the "
                             f"binary has a body, src line {line}"))
        if len(targets) > 1:
            ts = sorted(targets)
            clones = show_clones and all(identical_bodies(ts[0], t) for t in ts[1:])
            findings.append(("CLONE" if clones else "SPLIT", None,
                             [val for _t, val in labels if val is not None],
                             ("MSVC emitted the block once per arm -- identical up to jump targets and "
                              "the EH state index; merging in source is correct"
                              if clones else
                              "source merges cases the binary sends to different bodies")
                             + f" [{', '.join(hex(t) for t in ts)}] src line {line}"))

    if not findings:
        print("   CLEAN - every binary arm has a source case and vice versa")
        return 0
    for kind, t, vals, why in findings:
        print(f"   {kind:12s} {', '.join(label(v) for v in vals)}"
              + (f" @{t:#x}" if t else ""))
        print(f"                {why}")
    return 1 if any(k in ("MISSING", "DEFAULT", "SPLIT", "INCOMPLETE")
                    for k, _t, _v, _w in findings) else 0


def sweep(prefix=None, multi=False):
    """Every recovered function (a `// 0xADDR` marker in src/) whose body holds a table."""
    markers, _mentions = sym.src_scan()
    candidates = []
    for addr in sorted(markers):
        name = sym.addr2name(addr, exact_only=True)
        if not name:
            continue
        try:
            if find_switch(addr):
                candidates.append(name)
        except Exception:
            continue
    print(f"# {len(candidates)} recovered functions carry a jump table\n")
    hits = 0
    for name in candidates:
        try:
            hits += audit(name, prefix=prefix, multi=multi) or 0
        except Exception as e:
            print(f"== {name}\n   HARNESS ERROR {e!r}")
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", nargs="?", help="0xADDR or Class::Method")
    ap.add_argument("--sweep", action="store_true", help="audit every recovered fn with a table")
    ap.add_argument("--prefix", help="name case values from #defines with this prefix")
    ap.add_argument("--no-clones", action="store_true", help="do not byte-compare split arms")
    ap.add_argument("--multi", action="store_true",
                    help="also diff functions where table<->switch pairing is a guess")
    args = ap.parse_args()
    if args.sweep:
        return 0 if sweep(args.prefix, args.multi) == 0 else 1
    if not args.target:
        ap.error("give an address/name or --sweep")
    return audit(args.target, prefix=args.prefix, show_clones=not args.no_clones,
                 multi=args.multi)


if __name__ == "__main__":
    sys.exit(main())
