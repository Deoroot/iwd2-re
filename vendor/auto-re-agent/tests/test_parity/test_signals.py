"""Tests for individual parity signals."""
from __future__ import annotations

from re_agent.core.models import GhidraData, HookEntry, SourceMatch
from re_agent.parity.signals import (
    check_call_count_mismatch,
    check_fp_sensitivity,
    check_inline_wrapper,
    check_large_asm_tiny_source,
    check_missing_source,
    check_param_swap,
    check_short_body,
    check_stub_markers,
    check_trivial_stub,
    check_wrong_member,
)


def _make_source(**kwargs: object) -> SourceMatch:
    defaults = dict(
        path="test.cpp", line=1, body="{ code; }", body_no_comments="{ code; }",
        body_lines=10, call_count=5, plugin_call_count=0, non_plugin_call_count=5,
        control_flow_count=3, has_stub_marker=False, has_fp_token=False,
        is_inline_internal_forwarder=False,
    )
    defaults.update(kwargs)
    return SourceMatch(**defaults)  # type: ignore[arg-type]


def _make_ghidra(**kwargs: object) -> GhidraData:
    defaults = dict(
        decompile_ok=True, asm_ok=True,
        asm_instruction_count=50, asm_call_count=5,
        asm_has_fp_sensitive=False, callees=5,
    )
    defaults.update(kwargs)
    return GhidraData(**defaults)  # type: ignore[arg-type]


def _wm_entry() -> HookEntry:
    return HookEntry(
        class_path="Cls", fn_name="ClearColorEffectsAll", address="0x6e6490",
        reversed=True, locked=False, is_virtual=True,
    )


_WM_OFFSETS = {"Cls": {"m_a1VidCellBase": 0x40E, "m_a1VidCellExtend": 0x4E8}}
# real Ghidra dump-asm: CALL targets are RAW ADDRESSES, resolved via fn_names.
_WM_FN_NAMES = {"7ad3a0": "DeleteResPaletteAffect"}
_WM_ASM = (
    "006e6490 MOV ESI,ECX\n"
    "006e64ea LEA ECX,[ESI + 0x40e]\n"
    "006e64f0 CALL 0x007ad3a0\n"
    "006e64f6 LEA ECX,[ESI + 0x4e8]\n"
    "006e64fc CALL 0x007ad3a0\n"
    "006e6500 RET\n"
)


def test_wrong_member_flagged() -> None:
    # source clears only the BASE cell -> the extend DeleteResPaletteAffect is the
    # copy-paste / wrong-member bug (the corpse-tint class).
    src = _make_source(body="{ m_a1VidCellBase.DeleteResPaletteAffect(); }")
    ghidra = _make_ghidra(asm_instructions=_WM_ASM)
    f = check_wrong_member(
        source=src, ghidra=ghidra, entry=_wm_entry(),
        member_offsets=_WM_OFFSETS, fn_names=_WM_FN_NAMES,
    )
    assert f is not None
    assert f.level == "red"
    assert "m_a1VidCellExtend.DeleteResPaletteAffect()" in f.reason


def test_wrong_member_clean() -> None:
    src = _make_source(
        body="{ m_a1VidCellBase.DeleteResPaletteAffect(); m_a1VidCellExtend.DeleteResPaletteAffect(); }"
    )
    ghidra = _make_ghidra(asm_instructions=_WM_ASM)
    f = check_wrong_member(
        source=src, ghidra=ghidra, entry=_wm_entry(),
        member_offsets=_WM_OFFSETS, fn_names=_WM_FN_NAMES,
    )
    assert f is None


def test_wrong_member_absent_method_not_flagged() -> None:
    # the method is entirely absent from the source = a stub/incomplete recovery, NOT a
    # wrong-member bug -> a different signal owns it, so this one stays silent.
    src = _make_source(body="{ /* unrelated body */ }")
    ghidra = _make_ghidra(asm_instructions=_WM_ASM)
    f = check_wrong_member(
        source=src, ghidra=ghidra, entry=_wm_entry(),
        member_offsets=_WM_OFFSETS, fn_names=_WM_FN_NAMES,
    )
    assert f is None


def test_missing_source() -> None:
    f = check_missing_source(source=None)
    assert f is not None
    assert f.level == "red"


def test_source_present() -> None:
    f = check_missing_source(source=_make_source())
    assert f is None


def test_stub_marker_detected() -> None:
    f = check_stub_markers(source=_make_source(has_stub_marker=True))
    assert f is not None
    assert f.level == "red"


def test_no_stub_marker() -> None:
    f = check_stub_markers(source=_make_source(has_stub_marker=False))
    assert f is None


def test_trivial_stub() -> None:
    src = _make_source(plugin_call_count=2, non_plugin_call_count=0, body_lines=5, control_flow_count=0)
    f = check_trivial_stub(source=src)
    assert f is not None
    assert f.level == "red"


def test_short_body() -> None:
    f = check_short_body(source=_make_source(body_lines=3))
    assert f is not None
    assert f.level == "yellow"


def test_short_body_inline_skip() -> None:
    f = check_short_body(source=_make_source(body_lines=3), inline_skip=True)
    assert f is None


def test_large_asm_tiny_source() -> None:
    src = _make_source(body_lines=5)
    ghidra = _make_ghidra(asm_instruction_count=100)
    f = check_large_asm_tiny_source(source=src, ghidra=ghidra)
    assert f is not None
    assert f.level == "red"


def test_fp_sensitivity() -> None:
    src = _make_source(has_fp_token=False)
    ghidra = _make_ghidra(asm_has_fp_sensitive=True)
    f = check_fp_sensitivity(source=src, ghidra=ghidra)
    assert f is not None
    assert f.level == "yellow"


def test_call_count_mismatch() -> None:
    src = _make_source(call_count=2)
    ghidra = _make_ghidra(asm_call_count=8)
    f = check_call_count_mismatch(source=src, ghidra=ghidra, call_count_warn_diff=3)
    assert f is not None
    assert f.level == "yellow"


def test_call_count_within_threshold() -> None:
    src = _make_source(call_count=5)
    ghidra = _make_ghidra(asm_call_count=7)
    f = check_call_count_mismatch(source=src, ghidra=ghidra, call_count_warn_diff=3)
    assert f is None


def test_inline_wrapper() -> None:
    src = _make_source(is_inline_internal_forwarder=True)
    f = check_inline_wrapper(source=src)
    assert f is not None
    assert f.level == "info"


# --- check_param_swap: "right callee/count, WRONG argument" ---------------------------

_PS_DEC_SIG = (
    "undefined1 __thiscall C__F(int param_1,int param_2,int param_3,int param_4,"
    "int *param_5,RECT *param_6,char param_7,uint param_8)"
)
# decompile body: param_3 used 3x, param_4 used 2x (the faithful binary)
_PS_DEC_BODY = (
    "{ x=(param_7!=0)+1; param_8&0x10; param_8&0x20; param_8&0x400; param_8&0x400;"
    " if(param_3<u)d; (param_2-a)*(b)+(param_3-u)*(c);"
    " local_78=param_6->bottom+param_4; local_80=param_6->top+param_4;"
    " local_8c=*param_5+param_2; local_88=param_5[1]+param_3; }"
)
_PS_SRC_SIG = (
    "BOOL C::F(INT nPosX, INT nPosY, INT nPosZ, const CPoint& ptRef, "
    "const CRect& rGCBounds, BOOLEAN bDithered, DWORD dwBlitFlags)"
)
# buggy source: nPosZ fed where nPosY belongs (the Call Lightning canopy notch)
_PS_SRC_BUGGY = (
    "dwFillFlags=(bDithered!=0)+1; dwBlitFlags&MIRROR_FX; dwBlitFlags&MIRROR_FX_UPDOWN;"
    " dwBlitFlags&CLIPPING_IGNORE_VERTICAL; if(nPosZ<pA->y)bDraw;"
    " (nPosX-pA->x)*(pB->y-pA->y)+(nPosZ-pA->y)*(pA->x-pB->x);"
    " CRect rClip(rGCBounds.left,rGCBounds.top+nPosZ,rGCBounds.right,rGCBounds.bottom+nPosZ);"
    " CPoint ptFill(ptRef.x+nPosX,ptRef.y+nPosY);"
)
_PS_SRC_FIXED = _PS_SRC_BUGGY.replace("nPosZ<pA->y", "nPosY<pA->y").replace(
    "(nPosZ-pA->y)", "(nPosY-pA->y)"
)


def test_param_swap_flags_swapped_scalar() -> None:
    src = _make_source(body_no_comments=_PS_SRC_BUGGY, signature=_PS_SRC_SIG)
    gh = _make_ghidra(decompiled=_PS_DEC_SIG + "\n" + _PS_DEC_BODY)
    f = check_param_swap(source=src, ghidra=gh)
    assert f is not None
    assert f.level == "yellow"
    assert "nPosZ" in f.reason and "nPosY" in f.reason
    # struct/ref params (accessed via . -> []) must not be flagged
    assert "rGCBounds" not in f.reason and "ptRef" not in f.reason


def test_param_swap_clean_on_faithful() -> None:
    src = _make_source(body_no_comments=_PS_SRC_FIXED, signature=_PS_SRC_SIG)
    gh = _make_ghidra(decompiled=_PS_DEC_SIG + "\n" + _PS_DEC_BODY)
    assert check_param_swap(source=src, ghidra=gh) is None


def test_param_swap_bails_without_alignment() -> None:
    # decompiler param count neither n nor n+1 -> not safely alignable
    gh = _make_ghidra(decompiled="int F(int param_1,int param_2)\n{ param_1; param_2; }")
    src = _make_source(body_no_comments=_PS_SRC_BUGGY, signature=_PS_SRC_SIG)
    assert check_param_swap(source=src, ghidra=gh) is None
