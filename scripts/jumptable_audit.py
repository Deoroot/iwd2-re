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
  OUT-OF-RANGE  a source case outside the table that the binary never compares
                against either -- so no compare chain handles it
  NO-SWITCH-IN-SOURCE / TABLE COUNT / UNMATCHED-SOURCE-SWITCH -- structural
  UNPAIRED-TABLE / no-table-for-switch -- structural, and NOT findings: one is
                an inlined callee's table, the other a switch small or sparse
                enough that MSVC emitted a compare chain instead of a table

More than ten MISSING arms on one table collapse into a single line. A source
switch reaching 18 of 160 arms is an UNRECOVERED switch, not a list of gaps, and
printing all 145 buries every other function in the sweep.

A wrong pairing prints a wall of MISSING/DEFAULT about correct code, so step 5
only diffs a pairing it can DERIVE (see `pair_tables`):

  unique  the table's best switch beats every other switch, and no other table
          wants that switch as much -- a fact, not a guess
  forced  one table and one switch are left over, so the pairing is decided by
          elimination even at zero overlap (a fully normalized switch)
  order   the overlap TIED, broken by order of appearance -- a guess, and only
          offered under `--multi`

Anything still unresolved reports AMBIGUOUS-PAIRING and nothing else unless
`--multi` asks for the diff. INCOMPLETE still fires there: it needs no pairing
to be true.

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
  * A NESTED switch's closing brace is not its parent's. One relative depth
    counter ended both, dropping every parent case written below the nested
    one: CGameSprite::SetCursor's `case 3` sits under the icon-index switch
    nested in `case 2`, so the state table's fourth arm read as MISSING. The
    open switches are a STACK now, and CRuleTables::GetRaceString -- eight
    sequential switches, the reason the `switch (` test must stay ungated --
    still parses, because a switch is popped by depth, not by the next one.
  * An immediate is SIGNED. A downward bias is `add eax, 0xffffb2ca`, and
    reading that unsigned put CGameAIBase::EvaluateStatusTrigger's first case
    at -4294950902, whereupon the offset fold "corrected" it by 0x100000001.
  * A ONE-CASE switch can always be folded somewhere useful, so its fold is
    evidence of nothing: CScreenCharacter::UpdatePopupPanel's lone case shifted
    +57 onto a 54-slot table and made 19 arms read MISSING.
  * A case value outside the table is not a missing case: MSVC peels the values
    that fall outside the table's contiguous range into a compare chain around
    it. CScreenWorld::CancelPopup's table covers 0..8 while a chain handles -1,
    17, 19, 21 and 22 -- five OUT-OF-RANGE lines about cases the binary
    demonstrably tests for, and 53 such lines across the sweep.
  * An INLINED callee's table is not a hole in the caller. The source marks each
    one `// NOTE: Uninline.`, so the table is diffed against the callee's own
    recovery; without that, every screen's SummonPopup/DismissPopup wrapper read
    as NO-SWITCH-IN-SOURCE, and CScreenWorld::StartDeath -- which holds the
    inlined CancelPopup's table AND an unrelated switch of its own -- paired the
    two and printed three MISSING arms about two correct functions.

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


# The old resolver did `ord(expr.strip("'").lstrip("\\"))`, which reads EVERY C
# escape as the letter after the backslash: '\r' came out 114 ('r') instead of
# 13, '\\' came out 0 instead of 92, '\0' came out 48. In CTlkTable::ParseStr
# that shifted the whole switch by 13 and turned four correctly recovered cases
# into three MISSING plus a DEFAULT and two OUT-OF-RANGE.
C_ESCAPES = {"0": 0, "a": 7, "b": 8, "t": 9, "n": 10, "v": 11, "f": 12,
             "r": 13, "e": 27, '"': 34, "'": 39, "?": 63, "\\": 92}


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
    m = re.fullmatch(r"'\\x([0-9a-fA-F]{1,2})'", expr)
    if m:
        return int(m.group(1), 16)
    m = re.fullmatch(r"'\\([0-7]{1,3})'", expr)
    if m:
        return int(m.group(1), 8)
    m = re.fullmatch(r"'(\\.|[^'\\])'", expr)
    if m:
        body = m.group(1)
        return C_ESCAPES.get(body[1]) if body.startswith("\\") else ord(body)
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


def imm32(text):
    """A capstone immediate as the SIGNED 32-bit value the CPU works with.

    A switch biased downwards is `add eax, 0xffffb2ca`, and reading that
    unsigned put CGameAIBase::EvaluateStatusTrigger's first case at
    -4294950902. The offset fold then "corrected" it by 0x100000001 and the
    audit printed 145 MISSING lines about a function whose table it had never
    located."""
    v = int(text, 0)
    return v - 0x100000000 if v >= 0x80000000 else v


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
                info["count"] = imm32(mc.group(2)) + 1
                continue
            if info["bias"]:
                continue
            mb = BIAS_LEA.match(back)
            if mb and mb.group(1) == key:
                info["bias"] = imm32(mb.group(3)) * (-1 if mb.group(2) == "-" else 1)
                break
            ms = BIAS_SUB.match(back)
            if ms and ms.group(1) == key:
                info["bias"] = -imm32(ms.group(2))
                break
            ma = BIAS_ADD.match(back)
            if ma and ma.group(1) == key:
                info["bias"] = imm32(ma.group(2))
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


CMP_ANY = re.compile(r"^cmp \w+, (-?(?:0x[0-9a-f]+|\d+))$")
_CMP_CACHE = {}


def compare_immediates(start):
    """Every value the function compares against outside its jump table.

    A switch is not only its table: MSVC peels the values that sit outside the
    table's contiguous range into a compare chain around it. CScreenWorld::
    CancelPopup's table covers 0..8 and a chain handles -1, 17, 19, 21 and 22 --
    all five of which read as OUT-OF-RANGE, "source case outside the table",
    about cases the binary demonstrably tests for. Fifty-three of the sweep's
    lines were that.

    Any `cmp` immediate counts, so an unrelated comparison can silence a case
    that really is missing. That is the safe direction, and the same one
    `identical_bodies` takes: a suppressed line costs a missed finding, an
    unsuppressed one costs trust in every other line."""
    if start not in _CMP_CACHE:
        end = fn_end(start)
        span = min(max(end - start, 32), 0x8000)
        out = set()
        for va, text in disasm_lines(start, span // 2 + 8):
            if va >= end:
                break
            m = CMP_ANY.match(text)
            if m:
                imm = int(m.group(1), 0)
                out.add(imm)
                if imm >= 0x80000000:      # `cmp eax, 0xffffffff` is `case -1`
                    out.add(imm - 0x100000000)
        _CMP_CACHE[start] = out
    return _CMP_CACHE[start]


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


_BODY_CACHE = {}


def src_body(name):
    """The recovered source of one function, or None if src_find cannot place it.

    Cached because the sweep asks for the same callee once per function that
    inlined it -- CUIManager::KillCapture is marked `Uninline` in a dozen
    screens."""
    if name not in _BODY_CACHE:
        import subprocess
        r = subprocess.run([sys.executable, os.path.join(REPO, "scripts", "src_find.py"),
                            name, "--body"], capture_output=True, text=True)
        _BODY_CACHE[name] = r.stdout if r.returncode == 0 else None
    return _BODY_CACHE[name]


UNINLINE_MARK = re.compile(r"^\s*//\s*NOTE:\s*Uninline\b")
CALL_NAME = re.compile(r"([A-Za-z_]\w*)\s*\(")
NOT_A_CALL = {"if", "for", "while", "switch", "return", "sizeof", "static_cast",
              "reinterpret_cast", "const_cast", "dynamic_cast", "UTIL_ASSERT"}


def inlined_switches(name, defines):
    """[(callee, switch)] for every switch the source says MSVC inlined into us.

    A callee's jump table sits inside the caller's extent with no `switch` in the
    caller's source to pair it against, which is how every screen's
    SummonPopup/DismissPopup wrapper reads as NO-SWITCH-IN-SOURCE -- 43 of them
    the moment the pairing improved enough to diff those functions at all. This
    codebase already marks each one `// NOTE: Uninline.` on the line above the
    call, so the table can be diffed against the callee's OWN recovery instead.

    Every identifier before a `(` on the marked line is offered as a candidate,
    chains included: the pairing accepts a candidate only when its case values
    match the table, so a wrong guess costs nothing. Resolution prefers
    OurClass::callee -- a bare `DismissPopup` has fifteen definitions and
    src_find just picks one."""
    body = src_body(name)
    if body is None:
        return []
    owner = name.split("::")[0] if "::" in name else None
    wanted, marked = [], False
    for line in body.splitlines():
        m = re.match(r"^\s*(\d+)\t(.*)$", line)
        if not m:
            continue
        code = m.group(2)
        if UNINLINE_MARK.match(code):
            marked = True
            continue
        if marked and code.strip():
            marked = False
            for cand in CALL_NAME.findall(_uncomment(code)):
                if cand not in NOT_A_CALL and cand not in wanted:
                    wanted.append(cand)
    out = []
    for cand in wanted:
        for full in ([f"{owner}::{cand}"] if owner else []) + [cand]:
            if full == name:
                continue
            body = src_body(full)
            # src_find falls back to "# N matches, showing ..." on an ambiguous
            # bare name; that pick is arbitrary, so refuse it.
            if body is None or body.startswith("# "):
                continue
            sws, _err = source_switches(full, defines)
            if sws:
                out.extend((full, sw) for sw in sws)
            break
    return out


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
    body = src_body(name)
    if body is None:
        return None, "src_find could not place this function"
    switches = []          # [[group, ...], ...] - one list per `switch` statement
    stack = []             # the switches currently open, innermost last
    depth = 0              # absolute brace depth in the function body

    def close_group(ctx):
        if ctx["pending"]:
            ctx["groups"].append((ctx["pending"], ctx["line"], ctx["stub"]))
        ctx["pending"], ctx["line"], ctx["body"], ctx["stub"] = [], None, False, False

    for line in body.splitlines():
        m = re.match(r"^\s*(\d+)\t(.*)$", line)
        if not m:
            continue
        lineno, code = int(m.group(1)), m.group(2)
        bare = _uncomment(code)
        delta = bare.count("{") - bare.count("}")
        # A second `switch` starts a second table's worth of cases. Keeping them
        # in one flat list is what made every case of switch #2 read as MISSING
        # from switch #1. This test comes FIRST and is NOT gated on brace depth:
        # gating it cost CRuleTables::GetRaceString's eight switches, which
        # collapsed into one the moment a closing brace went unseen.
        if re.search(r"\bswitch\s*\(", bare):
            if stack:
                close_group(stack[-1])
            stack.append({"groups": [], "pending": [], "line": None, "body": False,
                          "stub": False, "exit": depth})
            depth += delta
            continue
        if stack:
            ctx = stack[-1]
            mc = re.match(r"^\s*case\s+(.+?)\s*:\s*(.*)$", code)
            if mc:
                if ctx["body"]:  # a new label after a body closes the previous group
                    close_group(ctx)
                if ctx["line"] is None:
                    ctx["line"] = lineno
                ctx["pending"].append((mc.group(1).strip(),
                                       resolve_label(mc.group(1), defines)))
                tail = mc.group(2).strip()
            else:
                tail = code
            if ctx["pending"] and tail:
                ctx["body"] = True
                if "TODO: Incomplete" in code:
                    ctx["stub"] = True
        depth += delta
        # A switch ends at ITS OWN closing brace. Tracking one relative depth
        # instead of a stack meant a NESTED switch's closing brace ended the
        # parent too, and every parent case written after it was dropped:
        # CGameSprite::SetCursor's `case 3` sits below the icon-index switch
        # nested in `case 2`, so the state table's fourth arm read as MISSING.
        while stack and depth <= stack[-1]["exit"]:
            ctx = stack.pop()
            close_group(ctx)
            if ctx["groups"]:
                switches.append(ctx["groups"])
    while stack:
        ctx = stack.pop()
        close_group(ctx)
        if ctx["groups"]:
            switches.append(ctx["groups"])
    # A switch is appended when it CLOSES, so an inner one lands before its
    # parent; the order tie-break and every "at src line" need source order.
    switches.sort(key=lambda sw: sw[0][1])
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


# --------------------------------------------------------------------- pairing


def fold_shift(src_values, table, default):
    """The constant a NORMALIZED source switch is offset by, or 0.

    `switch (m_nModalState - 1)` with cases 0..3 against a binary table of 1..4
    (CGameSprite::CheckModal) is a faithful rewrite, but term-by-term it reads as
    every case both MISSING and OUT-OF-RANGE.

    The fold must strictly IMPROVE the match, not merely be possible: any sparse
    switch over a wide table admits SOME shift, and the loose test ("the shifted
    values all exist in the table") moved GetSndWalk -- correct as written --
    onto four bogus MISSING lines."""
    # One value can always be shifted onto something useful, so a single-case
    # switch has no evidence to offer: CScreenCharacter::UpdatePopupPanel's
    # lone case folded +57 onto a 54-slot table and made 19 arms read MISSING.
    if len(src_values) < 2 or not table or set(src_values) == set(table):
        return 0
    k = min(table) - min(src_values)
    if k == 0 or not {v + k for v in src_values} <= set(table):
        return 0

    def landed(vals):
        return sum(1 for v in vals if table.get(v) not in (None, default))

    return k if landed(v + k for v in src_values) > landed(src_values) else 0


def values_of(sw):
    """The resolved case values of one source switch."""
    return {v for labels, _l, _s in sw for _t, v in labels if v is not None}


def coverage(table, default, values):
    """(real arms covered, table slots covered) for one source switch.

    Slot overlap alone cannot separate two candidate switches whose cases are
    all small integers, which every switch over a state or a type id is:
    CScreenWorld::StartDeath's own `switch (field_10F0)` overlaps the inlined
    CancelPopup's table on two slots, and so does CancelPopup itself. Counting
    the slots that reach a REAL ARM rather than the default separates them 4 to
    1, because the cases that matter are the ones with a body.

    Fold-aware on purpose: a normalized switch has ZERO raw overlap with the
    table it belongs to, and scoring it zero leaves the right pairing looking
    like no pairing at all. `fold_shift`'s strict test is what keeps that from
    inventing an overlap."""
    def score(vs):
        return (sum(1 for v in vs if table.get(v) not in (None, default)),
                len(set(vs) & set(table)))

    best = score(values)
    k = fold_shift(values, table, default)
    return max(best, score({v + k for v in values})) if k else best


def pair_score(table, default, values):
    """How many of this table's slots a source switch's case values account for."""
    return coverage(table, default, values)[1]


def pair_tables(tables, switches, allow_order=False):
    """({table index: switch index}, {table index: rule}, determined).

    Position is NOT a safe pairing key: MSVC reorders, and a table belonging to
    an inlined callee can land between two of ours. So the value overlap decides,
    and it decides only when it is UNAMBIGUOUS -- a table takes a switch when
    that switch beats every other switch for it AND no rival table wants the same
    switch as much. Each such claim shrinks both pools, so the next round can
    resolve pairings the first could not; that elimination is what promotes most
    multi-switch functions out of AMBIGUOUS-PAIRING.

    `order` is the last resort and the only guess here: when the overlap ties,
    tables in ADDRESS order are zipped against switches in SOURCE order. It fires
    only under `--multi`, only when both sides have the same number left, and
    only when every one of those tables overlaps something -- a table matching
    nothing is an inlined callee's, and ordering it against a real switch would
    print that switch's every case as MISSING."""
    vals = [values_of(sw) for sw in switches]
    score = [[pair_score(tbl, dflt, v) for v in vals] for _info, tbl, dflt in tables]
    assign, rule = {}, {}
    free_t, free_s = set(range(len(tables))), set(range(len(switches)))

    progress = True
    while progress:
        progress = False
        for ti in sorted(free_t):
            cand = sorted(((score[ti][si], si) for si in free_s if score[ti][si] > 0),
                          reverse=True)
            if not cand or (len(cand) > 1 and cand[0][0] == cand[1][0]):
                continue                                  # this table cannot choose
            top, si = cand[0]
            if any(score[tj][si] >= top for tj in free_t if tj != ti):
                continue                                  # a rival wants it as much
            assign[ti], rule[ti] = si, "unique"
            free_t.discard(ti)
            free_s.discard(si)
            progress = True

    if len(free_t) == 1 and len(free_s) == 1:
        ti, si = free_t.pop(), free_s.pop()
        assign[ti], rule[ti] = si, "forced"

    determined = not (free_t and free_s)
    if allow_order and free_t and len(free_t) == len(free_s) and all(
            any(score[ti][sj] > 0 for sj in free_s) for ti in free_t):
        for ti, si in zip(sorted(free_t), sorted(free_s)):
            assign[ti], rule[ti] = si, "order"
        free_t.clear()
        free_s.clear()
    return assign, rule, determined


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

    if len(infos) != len(switches):
        print(f"   TABLE COUNT: {len(infos)} jump table(s) in the binary vs "
              f"{len(switches)} switch(es) in source"
              + ("   [the extra table(s) are most likely inside an inlined callee]"
                 if len(infos) > len(switches) else ""))

    tables = []
    for info in infos:
        table, default = read_tables(info)
        if table is not None:
            tables.append((info, table, default))
        else:
            print(f"   jump table {info['jump_table']:#x}: bounds not recovered, skipped")

    assign, rule, determined = pair_tables(tables, switches, allow_order=multi)
    if not determined and not multi:
        unpaired = [ti for ti in range(len(tables)) if ti not in assign]
        print(f"   AMBIGUOUS-PAIRING - {len(unpaired)} of {len(tables)} table(s) match no "
              f"one source switch better than another; rerun with --multi to see the "
              f"per-table diff")
        # INCOMPLETE survives an ambiguous pairing: "this source case is a
        # `// TODO: Incomplete.` stub and SOME binary table sends its value at a
        # non-default arm" needs no pairing to be true.
        rc = 0
        for _info, table, default in tables:
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

    rc = 0
    inlined = None
    for ti, (info, table, default) in enumerate(tables):
        si = assign.get(ti)
        # A source switch of OUR OWN can win the pairing and still be the wrong
        # answer: CScreenWorld::StartDeath holds the inlined CancelPopup's table
        # plus a `switch (field_10F0)` MSVC turned into a compare chain, and
        # pairing those two printed three MISSING arms and a DEFAULT about two
        # functions that are both correct. So whenever our own switch leaves
        # real arms unexplained, the callees the source marks inlined get to
        # compete for the table -- and it is self-validating, since a wrong
        # candidate explains no arm at all.
        own = coverage(table, default, values_of(switches[si])) if si is not None else (0, 0)
        if own[0] < sum(1 for t in table.values() if t != default):
            if inlined is None:
                inlined = inlined_switches(name, defines)
            best = max(((coverage(table, default, values_of(sw))[0], c, sw)
                        for c, sw in inlined), default=(0, None, None),
                       key=lambda x: x[0])
            if best[0] > own[0]:
                print(f"   table {info['jump_table']:#x} is the INLINED {best[1]}'s "
                      f"(the source marks it `// NOTE: Uninline.`); diffed against "
                      f"that function's own recovery")
                assign.pop(ti, None)      # let the displaced switch be reported
                rc = max(rc, audit_one(best[1], va, info, table, default, best[2],
                                       defines, prefix, show_clones))
                continue
        if si is None and len(tables) > 1:
            # One of several tables matching no source switch is the ordinary
            # shape of an inlined callee's table, not a gap in our source. Only
            # a function whose ONLY table has no switch behind it is a finding,
            # and audit_one reports that one as NO-SWITCH-IN-SOURCE.
            arms = len(set(table.values()) - {default})
            print(f"   UNPAIRED-TABLE {info['jump_table']:#x} - {arms} arms, no source "
                  f"switch claims its values [most likely an inlined callee]")
            continue
        if si is not None and len(tables) + len(switches) > 2:
            print(f"   paired: table {info['jump_table']:#x} <-> source switch at line "
                  f"{switches[si][0][1]} ({rule[ti]})")
        rc = max(rc, audit_one(name, va, info, table, default,
                               switches[si] if si is not None else [],
                               defines, prefix, show_clones))
    for si, sw in enumerate(switches):
        if si in assign.values() or not values_of(sw):
            continue
        # MSVC emits a compare chain, not a table, for a switch that is small or
        # sparse, so "no table for this switch" is only news when the switch is
        # big and dense enough that a table was the certain choice.
        vals = values_of(sw)
        dense = len(vals) >= 4 and len(vals) * 2 > max(vals) - min(vals) + 1
        print(f"   {'UNMATCHED-SOURCE-SWITCH' if dense else 'no-table-for-switch'} at src "
              f"line {sw[0][1]} - {len(vals)} case values with no binary table"
              + ("" if dense else " [small or sparse: MSVC emits a compare chain]"))
        if dense:
            rc = max(rc, 1)
    return rc


def audit_one(name, fn_va, info, table, default, groups, defines, prefix, show_clones):
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

    # A source switch may normalize its expression (see `fold_shift`, which the
    # pairing consults with the same rule, so the two cannot disagree).
    k = fold_shift(set(src_values), table, default)
    if k:
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
    outside = compare_immediates(fn_va)
    for v, gi in sorted(src_values.items()):
        if v not in table:
            if v in outside:
                continue          # the binary peeled this case into a compare chain
            findings.append(("OUT-OF-RANGE", None, [v],
                             f"source case is outside the table's {info['count']} slots "
                             f"and the function never compares against it"))
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

    # A source switch that reaches a handful of a big table's arms is not a list
    # of gaps, it is a switch nobody has recovered yet -- CGameAIBase::
    # EvaluateStatusTrigger covers 18 of 160 trigger ids and printed 145 MISSING
    # lines, which buries every other function in the sweep. Say the one fact.
    gaps = [f for f in findings if f[0] == "MISSING"]
    if len(gaps) > 10:
        values = sorted(v for _k, _t, vs, _w in gaps for v in vs)
        findings = [("MISSING", None, values[:6],
                     f"+{len(values) - 6} more values: the source switch reaches "
                     f"{len(real) - len(gaps)} of {len(real)} arms, so this is an "
                     f"UNRECOVERED switch rather than a list of gaps")] \
            + [f for f in findings if f[0] != "MISSING"]

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
