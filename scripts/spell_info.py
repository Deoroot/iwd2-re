#!/usr/bin/env python3
"""Dump an IWD2 spell (SPL V2.0) by resref -- header + abilities + feature blocks.

The thing I kept hand-parsing with throwaway heredocs while reverse-engineering
spell mechanics (Call Lightning: which opcode, target type, projectile, sub-spell).
Resolves the human names so the output is comment-ready for src/commits.

Usage:
    python scripts/spell_info.py SPPR302            # by resref (the SPL filename)
    python scripts/spell_info.py SPPR302 --json
    python scripts/spell_info.py /path/to/X.SPL     # or an explicit path

Resolves:
    name/description strref -> dialog.tlk (via reagent_asset_names.TlkFile)
    primary type            -> SCHOOL.IDS
    opcode                  -> refs/iesdp/_opcodes/opNNN[-iwd2].html  (opname)
    spell/target/timing     -> builtin maps from the spl_v2 format doc

SPL V2.0 layout (refs/iesdp/file_formats/ie_formats/spl_v2.htm):
    header  : name@0x08  type@0x1c  school@0x25  level@0x34  desc@0x50
              abilities@0x64 (count@0x68)  feature-table@0x6a
              casting-fx index@0x6e count@0x70
    ability : 0x28 bytes -- target@0x0c  range@0x0e  castTime@0x12
              fxCount@0x1e  fxIndex@0x20  projectile@0x26
    feature : 0x30 bytes -- opcode@0x00  target@0x02  power@0x03
              p1@0x04 p2@0x08  timing@0x0c  duration@0x0e  prob@0x12/0x13
              resource@0x14  diceThrown@0x1c  diceSides@0x20  save@0x24 bonus@0x28
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SPL_DIR = REPO / "data" / "near_infinity_export" / "SPL"
IDS_DIR = REPO / "data" / "near_infinity_export" / "IDS"
IESDP_OPCODES = REPO / "refs" / "iesdp" / "_opcodes"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reagent_asset_names import TlkFile, find_default_tlk  # noqa: E402

SPELL_TYPE = {0: "Special", 1: "Wizard", 2: "Cleric", 3: "Psionic", 4: "Innate", 5: "Bard"}
# feature target type (feature +0x02)
FX_TARGET = {
    0: "None", 1: "Self(pre-projectile)", 2: "Preset target", 3: "Party",
    4: "Everyone", 5: "Everyone(not party)", 6: "Caster-specific",
    7: "Specific-type", 8: "Everyone(excl. caster)", 9: "Party(excl. caster)",
}
# ability target (ability +0x0c)
AB_TARGET = {
    0: "Invalid", 1: "Creature", 2: "Inventory", 3: "Dead", 4: "Point",
    5: "Self", 6: "Crash", 7: "Point(crash)",
}
TIMING = {
    0: "Duration", 1: "Permanent", 2: "WhileEquipped", 3: "DelayedDuration",
    4: "Delayed", 5: "Delayed(perm)", 6: "DurationJustExpired", 7: "Permanent(unsaved)",
    8: "Permanent", 9: "Permanent(unsaved)", 10: "TriggerDelay",
}
SAVE_BITS = {1: "Fortitude", 2: "Reflex", 3: "Will"}


def load_ids(name: str) -> dict[int, str]:
    out: dict[int, str] = {}
    p = IDS_DIR / name
    if not p.exists():
        return out
    for line in p.read_text(errors="replace").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2:
            try:
                out[int(parts[0], 0)] = parts[1].strip()
            except ValueError:
                pass
    return out


def opcode_name(op: int) -> str | None:
    for cand in (f"op{op:03d}-iwd2.html", f"op{op:03d}.html"):
        p = IESDP_OPCODES / cand
        if p.exists():
            m = re.search(r'opname:\s*"([^"]*)"', p.read_text(errors="replace"))
            if m:
                return m.group(1)
    return None


def resref(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("latin1").strip()


def save_types(mask: int) -> str:
    names = [v for bit, v in SAVE_BITS.items() if mask & (1 << bit)]
    return "+".join(names) if names else ("none" if mask == 0 else f"0x{mask:X}")


def resolve_spl(arg: str) -> Path:
    p = Path(arg)
    if p.exists():
        return p
    cand = SPL_DIR / (arg if arg.lower().endswith(".spl") else arg.upper() + ".SPL")
    if cand.exists():
        return cand
    sys.exit(f"spell not found: {arg} (looked in {SPL_DIR})")


def parse_feature(data: bytes, off: int) -> dict:
    op = struct.unpack_from("<H", data, off)[0]
    return {
        "opcode": op,
        "opcode_name": opcode_name(op),
        "target": data[off + 0x02],
        "target_name": FX_TARGET.get(data[off + 0x02], "?"),
        "power": data[off + 0x03],
        "param1": struct.unpack_from("<i", data, off + 0x04)[0],
        "param2": struct.unpack_from("<i", data, off + 0x08)[0],
        "timing": data[off + 0x0C],
        "timing_name": TIMING.get(data[off + 0x0C], "?"),
        "duration": struct.unpack_from("<I", data, off + 0x0E)[0],
        "probability": [data[off + 0x12], data[off + 0x13]],
        "resource": resref(data[off + 0x14 : off + 0x1C]),
        "dice_thrown": struct.unpack_from("<I", data, off + 0x1C)[0],
        "dice_sides": struct.unpack_from("<I", data, off + 0x20)[0],
        "save": save_types(struct.unpack_from("<I", data, off + 0x24)[0]),
        "save_bonus": struct.unpack_from("<i", data, off + 0x28)[0],
    }


def parse_spl(path: Path, tlk: TlkFile) -> dict:
    data = path.read_bytes()
    if data[:8] != b"SPL V2.0":
        sys.exit(f"not SPL V2.0: {path} (sig={data[:8]!r})")
    school_ids = load_ids("SCHOOL.IDS")

    def name(strref: int) -> str | None:
        e = tlk.get(strref)
        return re.sub(r"\s+", " ", e.text).strip() if e else None

    name_ref = struct.unpack_from("<i", data, 0x08)[0]
    desc_ref = struct.unpack_from("<i", data, 0x50)[0]
    stype = struct.unpack_from("<H", data, 0x1C)[0]
    school = data[0x25]
    level = struct.unpack_from("<I", data, 0x34)[0]

    ab_off = struct.unpack_from("<I", data, 0x64)[0]
    ab_cnt = struct.unpack_from("<H", data, 0x68)[0]
    feat_base = struct.unpack_from("<I", data, 0x6A)[0]
    cast_idx = struct.unpack_from("<H", data, 0x6E)[0]
    cast_cnt = struct.unpack_from("<H", data, 0x70)[0]

    def features(index: int, count: int) -> list[dict]:
        return [parse_feature(data, feat_base + (index + i) * 0x30) for i in range(count)]

    abilities = []
    for a in range(ab_cnt):
        o = ab_off + a * 0x28
        fx_cnt = struct.unpack_from("<H", data, o + 0x1E)[0]
        fx_idx = struct.unpack_from("<H", data, o + 0x20)[0]
        abilities.append({
            "index": a,
            "form": data[o + 0x00],   # 1=standard 2=projectile
            "target": data[o + 0x0C],
            "target_name": AB_TARGET.get(data[o + 0x0C], "?"),
            "range": struct.unpack_from("<H", data, o + 0x0E)[0],
            "level_req": struct.unpack_from("<H", data, o + 0x10)[0],
            "casting_time": struct.unpack_from("<H", data, o + 0x12)[0],
            "projectile": struct.unpack_from("<H", data, o + 0x26)[0],
            "features": features(fx_idx, fx_cnt),
        })

    return {
        "resref": path.stem.upper(),
        "name": name(name_ref),
        "name_strref": name_ref,
        "description": name(desc_ref),
        "spell_type": SPELL_TYPE.get(stype, f"?{stype}"),
        "school": school_ids.get(school, str(school)),
        "level": level,
        "casting_features": features(cast_idx, cast_cnt),
        "abilities": abilities,
    }


def fmt_feature(f: dict, indent: str) -> str:
    nm = f["opcode_name"] or "?"
    parts = [f'{indent}opcode {f["opcode"]:>3} {nm!r}  target={f["target"]}:{f["target_name"]}']
    extra = [f'p1={f["param1"]}', f'p2={f["param2"]}',
             f'timing={f["timing"]}:{f["timing_name"]}', f'dur={f["duration"]}']
    if f["resource"]:
        extra.append(f'res={f["resource"]}')
    if f["dice_thrown"] or f["dice_sides"]:
        extra.append(f'dice={f["dice_thrown"]}d{f["dice_sides"]}')
    if f["save"] != "none":
        extra.append(f'save={f["save"]}{f["save_bonus"]:+d}')
    if f["probability"] != [100, 0] and f["probability"] != [0, 100]:
        extra.append(f'prob={f["probability"][1]}-{f["probability"][0]}')
    parts.append(f"{indent}    " + "  ".join(extra))
    return "\n".join(parts)


def _ability_sig(a: dict) -> tuple:
    # structural identity, ignoring per-level scaling (param1 / duration)
    return (a["form"], a["target"], a["range"], a["casting_time"], a["projectile"],
            tuple((f["opcode"], f["target"], f["timing"], f["resource"]) for f in a["features"]))


def render(s: dict) -> str:
    out = [
        f'{s["name"]} ({s["resref"]}.SPL, strref {s["name_strref"]})',
        f'  type={s["spell_type"]}  school={s["school"]}  level={s["level"]}',
        f'  desc: {s["description"]}' if s["description"] else '',
    ]
    out = [line for line in out if line]  # drop empty strings
    if s["casting_features"]:
        out.append("  casting features (on caster):")
        for f in s["casting_features"]:
            out.append(fmt_feature(f, "    "))

    # group consecutive abilities sharing structure (the per-caster-level rows)
    groups: list[list[dict]] = []
    for a in s["abilities"]:
        if groups and _ability_sig(a) == _ability_sig(groups[-1][0]):
            groups[-1].append(a)
        else:
            groups.append([a])

    out.append(f'  abilities ({len(s["abilities"])} total, {len(groups)} distinct):')
    for g in groups:
        a = g[0]
        span = f'abilities {a["index"]}..{g[-1]["index"]}' if len(g) > 1 else f'ability {a["index"]}'
        lvls = f'  levelReq {a["level_req"]}..{g[-1]["level_req"]}' if len(g) > 1 else ""
        out.append(
            f'    [{span}] form={a["form"]} target={a["target"]}:{a["target_name"]} '
            f'range={a["range"]} castTime={a["casting_time"]} '
            f'projectile={a["projectile"]}' + (" (None)" if a["projectile"] == 1 else "") + lvls
        )
        for i, f in enumerate(a["features"]):
            line = fmt_feature(f, "      ")
            if len(g) > 1:
                p1s = sorted({gg["features"][i]["param1"] for gg in g})
                if len(p1s) > 1:
                    line += f"\n        (p1 across levels: {p1s[0]}..{p1s[-1]})"
            out.append(line)
    return "\n".join(out)


def main() -> None:
    ap = argparse.ArgumentParser(description="Dump an IWD2 SPL spell by resref.")
    ap.add_argument("spell", help="resref (e.g. SPPR302) or path to a .SPL")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    args = ap.parse_args()

    tlk = TlkFile(find_default_tlk())
    s = parse_spl(resolve_spl(args.spell), tlk)
    print(json.dumps(s, indent=2) if args.json else render(s))


if __name__ == "__main__":
    main()
