"""Individual parity heuristic signals — each returns a Finding or None."""
from __future__ import annotations

import json
import re
from collections import Counter
from collections.abc import Callable
from pathlib import Path

from re_agent.core.models import Finding, GhidraData, HookEntry, SourceMatch

SignalFn = Callable[..., Finding | None]


def check_missing_source(source: SourceMatch | None, **_kw: object) -> Finding | None:
    if source is None:
        return Finding(level="red", reason="Source function body not found")
    return None


def check_stub_markers(
    source: SourceMatch | None,
    stub_markers: tuple[str, ...] = ("NOTSA_UNREACHABLE",),
    **_kw: object,
) -> Finding | None:
    if source is not None and source.has_stub_marker:
        return Finding(level="red", reason=f"Source contains stub marker ({', '.join(stub_markers)})")
    return None


def check_trivial_stub(source: SourceMatch | None, **_kw: object) -> Finding | None:
    if source is None or source.plugin_call_count == 0:
        return None
    likely_trivial = (
        source.body_lines <= 14
        and source.non_plugin_call_count <= 1
        and source.control_flow_count == 0
    )
    if likely_trivial:
        return Finding(level="red", reason="Source appears to be a trivial plugin::Call* stub")
    return None


def check_large_asm_tiny_source(
    source: SourceMatch | None,
    ghidra: GhidraData | None = None,
    inline_skip: bool = False,
    **_kw: object,
) -> Finding | None:
    if source is None or ghidra is None or not ghidra.asm_ok or inline_skip:
        return None
    if ghidra.asm_instruction_count >= 80 and source.body_lines <= 12:
        return Finding(level="red", reason="Large ASM body but tiny source body, likely mismatch/stub")
    return None


def check_plugin_call_heavy(source: SourceMatch | None, **_kw: object) -> Finding | None:
    if source is None or source.plugin_call_count == 0:
        return None
    plugin_heavy = source.plugin_call_count >= max(2, source.non_plugin_call_count)
    trivial = (
        source.body_lines <= 14
        and source.non_plugin_call_count <= 1
        and source.control_flow_count == 0
    )
    if plugin_heavy and not trivial:
        return Finding(
            level="yellow",
            reason=(
                f"Source relies heavily on plugin::Call* "
                f"({source.plugin_call_count} plugin vs {source.non_plugin_call_count} non-plugin calls)"
            ),
        )
    return None


def check_short_body(source: SourceMatch | None, inline_skip: bool = False, **_kw: object) -> Finding | None:
    if source is None or inline_skip:
        return None
    if source.body_lines < 6:
        return Finding(level="yellow", reason=f"Very short body ({source.body_lines} lines), inspect manually")
    return None


def check_low_call_count(
    source: SourceMatch | None,
    ghidra: GhidraData | None = None,
    inline_skip: bool = False,
    **_kw: object,
) -> Finding | None:
    if source is None or ghidra is None or not ghidra.decompile_ok or inline_skip:
        return None
    if ghidra.callees is not None and ghidra.callees >= 6 and source.call_count <= 1:
        return Finding(
            level="yellow",
            reason=f"Source call count is very low ({source.call_count}) vs Ghidra callees ({ghidra.callees})",
        )
    return None


def check_fp_sensitivity(
    source: SourceMatch | None,
    ghidra: GhidraData | None = None,
    inline_skip: bool = False,
    **_kw: object,
) -> Finding | None:
    if source is None or ghidra is None or not ghidra.asm_ok or inline_skip:
        return None
    if ghidra.asm_has_fp_sensitive and not source.has_fp_token:
        return Finding(
            level="yellow",
            reason="ASM contains floating-point sensitive ops but source has no obvious math tokens",
        )
    return None


def check_call_count_mismatch(
    source: SourceMatch | None,
    ghidra: GhidraData | None = None,
    inline_skip: bool = False,
    call_count_warn_diff: int = 3,
    **_kw: object,
) -> Finding | None:
    if source is None or ghidra is None or not ghidra.asm_ok or inline_skip:
        return None
    call_diff = abs(ghidra.asm_call_count - source.call_count)
    if call_diff > call_count_warn_diff:
        return Finding(
            level="yellow",
            reason=(
                f"Call count mismatch: vanilla has {ghidra.asm_call_count} calls, "
                f"source has {source.call_count} calls (diff: {call_diff})"
            ),
        )
    return None


def check_nan_logic(
    source: SourceMatch | None,
    ghidra: GhidraData | None = None,
    **_kw: object,
) -> Finding | None:
    if source is None or ghidra is None or not ghidra.decompile_ok:
        return None
    if ghidra.decompile_has_nan and "isnan" not in source.body_no_comments and "NAN(" not in source.body_no_comments:
        return Finding(
            level="yellow",
            reason="Decompile includes NAN-sensitive logic; verify NaN behavior manually",
        )
    return None


def check_inline_wrapper(source: SourceMatch | None, **_kw: object) -> Finding | None:
    if source is not None and source.is_inline_internal_forwarder:
        return Finding(level="info", reason="Source is an inline forwarding wrapper to internal I_* implementation")
    return None


# --- member-offset parity: "right callee, right count, WRONG member" -----------------
# callee + call-count signals are blind to this whole class (both match); the only
# discriminating signal is the per-thiscall `this` offset, which the decompile masks but
# the Ghidra listing exposes: `LEA ECX,[ESI + 0xNNN]; CALL Class::Method`. Map the offset
# to a member via the header `/* 0xNNN */` comments and diff vs what the source names.

_MOV_THIS_RE = re.compile(r"\bMOV\s+([A-Z]{3}),ECX\b")
_ECX_OBJ_RE = re.compile(r"\b(?:LEA|MOV)\s+ECX,(?:[a-z]+ ptr )?\[([A-Z]{3}) \+ (0x[0-9a-fA-F]+)\]")
_ECX_OTHER_RE = re.compile(r"\bMOV\s+ECX,")
_CALL_TGT_RE = re.compile(r"\bCALL\s+(\S+)")
_DECL_RE = re.compile(r"/\*\s*([0-9A-Fa-f]+)\s*\*/\s*[^;{]*?\bm_(\w+)\s*(?:\[[^\]]*\])?\s*;")
_MEMBER_CALL_RE = re.compile(r"\bm_(\w+)\s*(?:\.|->)\s*(\w+)\s*\(")

_HEADER_CACHE: dict[str, dict[str, dict[str, int]]] = {}
_FNNAME_CACHE: dict[str, dict[str, str]] = {}


def build_member_offsets(source_root: Path) -> dict[str, dict[str, int]]:
    """{ClassName: {m_member: byte_offset}} from `/* 0xNNN */ ... m_x;` header comments —
    the recovery's own annotations, the bridge between C++ names and binary offsets."""
    key = str(source_root)
    cached = _HEADER_CACHE.get(key)
    if cached is not None:
        return cached
    out: dict[str, dict[str, int]] = {}
    for hp in Path(source_root).rglob("*.h"):
        d = out.setdefault(hp.stem, {})
        for m in _DECL_RE.finditer(hp.read_text(errors="replace")):
            d.setdefault(f"m_{m.group(2)}", int(m.group(1), 16))
    _HEADER_CACHE[key] = out
    return out


def build_fn_names(export_dir: Path) -> dict[str, str]:
    """{normalized_8hex_addr: method_name} from the Ghidra address_map.json, so a raw
    `CALL 0x00xxxxxx` in the listing resolves to a method name. Ctors/dtors are dropped
    (they are never written in the source body). Missing map -> empty (signal degrades)."""
    key = str(export_dir)
    cached = _FNNAME_CACHE.get(key)
    if cached is not None:
        return cached
    out: dict[str, str] = {}
    fpath = Path(export_dir) / "address_map.json"
    if fpath.exists():
        try:
            raw = json.loads(fpath.read_text(errors="replace"))
        except (ValueError, OSError):
            raw = {}
        for addr, info in raw.items():
            name = info.get("name", "")
            cls = info.get("class", "")
            if name and not name.startswith("~") and name != cls:
                out[addr.lower().lstrip("0").rjust(1, "0")] = name
    _FNNAME_CACHE[key] = out
    return out


def _norm_addr(s: str) -> str:
    return s.lower().removeprefix("0x").lstrip("0").rjust(1, "0")


def _ghidra_call_pairs(asm: str, fn_names: dict[str, str]) -> Counter:
    """(this_offset, method) for each thiscall in a Ghidra dump-asm listing. CALL targets
    are raw addresses (`CALL 0x007acd60`) resolved via fn_names, or named `Class::Method`."""
    lines = asm.splitlines()
    this_reg = "ESI"
    for ln in lines[:16]:
        m = _MOV_THIS_RE.search(ln)
        if m:
            this_reg = m.group(1)
            break
    pairs: Counter = Counter()
    pending: int | None = None
    for ln in lines:
        m = _ECX_OBJ_RE.search(ln)
        if m:
            pending = int(m.group(2), 16) if m.group(1) == this_reg else None
            continue
        if _ECX_OTHER_RE.search(ln):
            pending = None
            continue
        c = _CALL_TGT_RE.search(ln)
        if c:
            tgt = c.group(1)
            method = None
            if "::" in tgt:
                cls_part, _, m2 = tgt.rpartition("::")
                if not m2.startswith("~") and m2 != cls_part.rpartition("::")[2]:
                    method = m2
            elif tgt.startswith("0x"):
                method = fn_names.get(_norm_addr(tgt))
            if pending is not None and method:
                pairs[(pending, method)] += 1
            pending = None
    return pairs


def _method_adj(offsets: list[int], hvals: set[int]) -> int:
    """Per-method base-subobject shift (a base-class method's `this` is member+4, etc.)."""
    deltas: Counter = Counter()
    for off in offsets:
        for d in (0, 4, 8, 12, 16):
            if off - d in hvals:
                deltas[d] += 1
    return deltas.most_common(1)[0][0] if deltas else 0


def check_wrong_member(
    source: SourceMatch | None = None,
    ghidra: GhidraData | None = None,
    entry: HookEntry | None = None,
    member_offsets: dict[str, dict[str, int]] | None = None,
    fn_names: dict[str, str] | None = None,
    inline_skip: bool = False,
    **_kw: object,
) -> Finding | None:
    if (
        source is None
        or ghidra is None
        or not ghidra.asm_ok
        or not ghidra.asm_instructions
        or inline_skip
        or entry is None
        or not member_offsets
    ):
        return None
    hdr = member_offsets.get(entry.class_name)
    if not hdr:
        return None
    pairs = _ghidra_call_pairs(ghidra.asm_instructions, fn_names or {})
    if not pairs:
        return None
    off2name = {off: name for name, off in hdr.items()}
    hvals = set(hdr.values())
    by_method: dict[str, list[int]] = {}
    for (off, meth), n in pairs.items():
        by_method.setdefault(meth, []).extend([off] * n)
    adj = {meth: _method_adj(offs, hvals) for meth, offs in by_method.items()}

    bin_pairs: set[tuple[str, str]] = set()
    for off, meth in pairs:
        name = off2name.get(off - adj[meth])
        if name:
            bin_pairs.add((name, meth))

    src_pairs = {(f"m_{m.group(1)}", m.group(2)) for m in _MEMBER_CALL_RE.finditer(source.body)}
    src_methods = {meth for _, meth in src_pairs}
    # only the copy-paste / incomplete-member-set signature: the method IS used in the
    # source (on another member), just not on the member the binary calls it on. A method
    # entirely absent from the source = a stub/incomplete recovery, a different signal.
    missing = sorted(p for p in (bin_pairs - src_pairs) if p[1] in src_methods)
    if not missing:
        return None
    shown = ", ".join(f"{n}.{m}()" for n, m in missing[:6])
    more = f" (+{len(missing) - 6} more)" if len(missing) > 6 else ""
    return Finding(
        level="red",
        reason=(
            f"Wrong/missing member: binary calls {shown}{more} but the source never does "
            f"(likely a copy-paste on a sibling member)"
        ),
    )


ALL_SIGNALS: list[SignalFn] = [
    check_missing_source,
    check_stub_markers,
    check_trivial_stub,
    check_large_asm_tiny_source,
    check_plugin_call_heavy,
    check_short_body,
    check_low_call_count,
    check_fp_sensitivity,
    check_call_count_mismatch,
    check_nan_logic,
    check_inline_wrapper,
    check_wrong_member,
]
