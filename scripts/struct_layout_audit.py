#!/usr/bin/env python3
"""Struct-layout drift auditor for IWD2.exe binary-mirror classes.

WHY THIS EXISTS
    A binary-mirror class (members carry /* 0xNNN */ offset comments) only matches
    IWD2.exe if the COMPILER places every member at the commented offset. The IE
    engine packs its structs to 2 bytes; a class that forgets `#pragma pack(2)`
    lets the compiler 4-align a sub-4-aligned LONG/WORD tail, drifting it a couple
    bytes. The code still compiles, parity stays GREEN (parity checks code, not
    layout), and the only symptom is a runtime/visual bug -- e.g. the Cloudkill
    detonation over-density: m_visual2MaxSpawn drifted +0x30 vs the packed +0x2E,
    so IcewindCProjectileSpellHit::OnArrival's reinterpret_cast read garbage, the ring spawn
    ran uncapped (36 particles vs 13).

WHAT IT DOES
    Compares the MSVC-COMPILED layout (cl /d1reportSingleClassLayout<Class>, the
    real offsets) against the header's /* 0xNNN */ comments. The comparison is on
    DELTAS between members, not absolute offsets, so it is invariant to the known
    CGameObject base-size discrepancy (compiled 0x78 vs binary 0x6E) that shifts
    every leaf -- only genuine *spacing* drift (the packing bug) trips it.

    offsetof()-based static_asserts can't do this: offsetof on a polymorphic
    (non-standard-layout) class like CProjectile is rejected by MSVC (C2618).
    The compiler layout dump has no such limit and covers every member.

USAGE
    python scripts/struct_layout_audit.py CProjectile
    python scripts/struct_layout_audit.py CProjectile --header src/CProjectile.h \
                                                       --source src/CProjectile.cpp
    Exit 0 = no drift, 1 = drift found, 2 = harness/parse error.

    The dump is produced on the VM (MSVC) via vm_build.cmd with the CL env var;
    the source TU is touched to force its recompile. Header + source are scp'd
    first so the compiled layout matches the comments under audit.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

VM = "win11vm"
VM_REPO = "C:/iwd2-re"
HOST_REPO = Path(__file__).resolve().parent.parent

# Header member with an offset comment, e.g.  "/* 053C */ LONG m_visual2MaxSpawn;"
# or "/* 04C4 */ std::map<LONG, int> m_miniA;" or "BYTE _pad[8];".
_HDR_MEMBER = re.compile(
    r"/\*\s*(0x)?([0-9A-Fa-f]+)\s*\*/\s*.*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;")
# MSVC layout dump member line, e.g.  " 1398\t| m_visual2MaxSpawn"  (single pipe =
# a direct member; "| |" = an inherited base member, skipped).
_DUMP_MEMBER = re.compile(r"^\s*(\d+)\s*\|\s+(?!\||\+)(.*\b)([A-Za-z_]\w*)\s*$")


def parse_header(header: Path, cls: str):
    """Ordered [(member, comment_offset)] for the direct members of `cls`."""
    text = header.read_text(errors="replace").splitlines()
    # Find the class body and walk it at brace-depth 1 (skip nested structs).
    # The class *definition* (name then ':' base-list, a '/*..*/' tag, or '{') --
    # not a forward declaration ("class X;").
    start = next((i for i, ln in enumerate(text)
                  if re.search(rf"\bclass\s+{re.escape(cls)}\b\s*(:|/\*|\{{)", ln)), None)
    if start is None:
        sys.exit(f"[audit] class {cls} not found in {header}")
    members, depth, seen = [], 0, False
    for ln in text[start:]:
        opens, closes = ln.count("{"), ln.count("}")
        # A member counts only at the class's own scope (depth 1, outside nested {}).
        at_class_scope = seen and depth == 1
        if at_class_scope:
            m = _HDR_MEMBER.search(ln)
            if m:
                members.append((m.group(3), int(m.group(2), 16)))
        depth += opens - closes
        if opens:
            seen = True
        if seen and depth <= 0:
            break
    return members


def run_layout_dump(cls: str, source_rel: str) -> str:
    """Force-recompile `source_rel` on the VM with the layout flag; return stdout."""
    src_vm = f"{VM_REPO}/{source_rel}".replace("/", "\\")
    build_vm = f"{VM_REPO}/scripts/vm_build.cmd".replace("/", "\\")
    # Sent verbatim to PowerShell on the VM (subprocess list -> no local shell, so
    # $env is PowerShell's and needs no escaping): bump the TU's mtime so cmake
    # recompiles it, set CL so cl.exe emits the layout, then build.
    ps = (
        f"(Get-Item {src_vm}).LastWriteTime = Get-Date; "
        f"$env:CL='/d1reportSingleClassLayout{cls}'; "
        f"cmd /c {build_vm}"
    )
    out = subprocess.run(["ssh", VM, ps],
                         capture_output=True, text=True, timeout=600)
    return out.stdout + out.stderr


def parse_dump(dump: str, cls: str):
    """member -> compiled_offset, from the base `class <cls>` block (single pipe)."""
    lines = dump.splitlines()
    start = next((i for i, ln in enumerate(lines)
                  if re.match(rf"^\s*class\s+{re.escape(cls)}\s+size\(", ln)), None)
    if start is None:
        sys.exit(f"[audit] no '/d1reportSingleClassLayout{cls}' block in build output "
                 f"(did the TU recompile? is MSVC on the VM?)")
    offsets = {}
    for ln in lines[start + 1:]:
        if re.match(r"^\s*class\s", ln):        # next class block -> end
            break
        m = _DUMP_MEMBER.match(ln)
        if m:
            offsets.setdefault(m.group(3), int(m.group(1)))
    return offsets


def main():
    ap = argparse.ArgumentParser(description="Audit a binary-mirror class for layout drift.")
    ap.add_argument("cls", help="class name, e.g. CProjectile")
    ap.add_argument("--header", help="header file (default: src/<cls>.h)")
    ap.add_argument("--source", help="TU that defines the class (default: src/<cls>.cpp)")
    args = ap.parse_args()

    header = Path(args.header) if args.header else HOST_REPO / "src" / f"{args.cls}.h"
    source_rel = args.source or f"src/{args.cls}.cpp"
    if not header.is_absolute():
        header = HOST_REPO / header

    hdr = parse_header(header, args.cls)
    if not hdr:
        sys.exit(f"[audit] no /* 0xNNN */ member comments for {args.cls} in {header}")

    # Sync the files under audit so the compiled layout matches the comments.
    for rel in {f"src/{header.name}", source_rel}:
        subprocess.run(["scp", "-q", str(HOST_REPO / rel), f"{VM}:{VM_REPO}/{rel}"],
                       check=False)

    dump = run_layout_dump(args.cls, source_rel)
    compiled = parse_dump(dump, args.cls)

    common = [(m, off) for m, off in hdr if m in compiled]
    if len(common) < 2:
        sys.exit(f"[audit] only {len(common)} member(s) matched between header and dump "
                 f"-- cannot diff spacing")

    # Adjacent-member spacing: a uniform base/STL size shift (e.g. our CGameObject
    # 0x78 vs the binary's 0x6E, or a wider STL node) cancels out, so only a genuine
    # spacing change trips -- a sub-4-aligned LONG/WORD tail that drifted for want of
    # #pragma pack(2) (the Cloudkill ring bug), or a member whose own size differs
    # from the binary. The break is localized to the member that moved.
    breaks = []
    for (pname, pcmt), (name, cmt) in zip(common, common[1:]):
        cmt_gap = cmt - pcmt
        bin_gap = compiled[name] - compiled[pname]
        if cmt_gap != bin_gap:
            breaks.append((pname, name, cmt_gap, bin_gap))

    print(f"[audit] {args.cls}: {len(common)} members checked vs /* 0xNNN */ comments")
    if not breaks:
        print("[audit] PASS -- compiled member spacing matches the comments.")
        return 0
    print(f"[audit] {len(breaks)} spacing break(s) -- the compiled gap INTO the named "
          f"member differs from the binary. The cause is the PRECEDING member's "
          f"compiled size: a wider STL node / CVidCell / base type (a known, benign "
          f"whole-codebase discrepancy) OR a tail that drifted for want of "
          f"#pragma pack(2). It is the packing bug when the moved member is one a "
          f"reinterpret_cast relies on (e.g. the visual-slot tails):")
    for prev, name, cg, bg in breaks:
        print(f"   {prev} -> {name}: comment gap 0x{cg:X}, compiled gap 0x{bg:X} "
              f"(drift {bg - cg:+d} B)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
