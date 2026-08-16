#!/usr/bin/env python3
"""save_party.py - read a save's party straight out of the .GAM.

Picking a save by driving the UI costs a full game launch per guess. The .GAM
answers "who is in this party, what class/race are they, which feats do they
have" statically, in well under a second, so a live test can be planned instead
of fished for. That is how session 27 found the two characters with POWER_ATTACK
after a whole session of blind picker sweeps found none.

Format (GAM v2.2), all little-endian:
  header  +0x20 party offset, +0x24 party count
  party NPC entry, stride 0x340: +4 CRE offset, +8 CRE size
  the embedded CRE starts "CRE ", its CCreatureFileHeader begins at CRE+8, so
  a struct field at +N is at CRE+8+N. Used here:
     m_feats[3]      struct +0x1B8  -> CRE+0x1C0
                     HasFeat(n) = bit n&31 of word n>>5
     m_classLevels   struct +0x83   -> CRE+0x8B, 11 bytes in CLASSMASK bit order
                     (barbarian, bard, cleric, druid, fighter, monk, paladin,
                     ranger, rogue, sorcerer, wizard)

Sanity check that proves the parse landed on the right bytes: no bit at or above
CGAMESPRITE_FEAT_NUMFEATS (75) may be set, and at least one of the five SIMPLE_*
weapon proficiencies (bits 53..57) must be. Landing one field early or late
lights up the high bits immediately. NOTE: "every character has all five
SIMPLE_* bits" is ALMOST true and is the wrong invariant to assert -- portrait 6
of save 000000009 is missing SIMPLE_CROSSBOW and parses perfectly otherwise.

Two traps this tool exists to keep you out of:
  * the innate spell list stores spell IDs, not resrefs, so grepping a CRE for
    "SPIN275" finds nothing and that means nothing;
  * holding the feat bit is NOT enough for the ability to appear -- the innate
    has to be there too AND HasFeat has to pass CheckFeatPrerequisites.

Usage:
  save_party.py                              # every save under MPSave/
  save_party.py 000000007                    # one save (substring match)
  save_party.py --feat POWER_ATTACK          # which saves hold a character with it
  save_party.py --class bard                 # which saves hold a bard
  save_party.py 000000007 --feats            # every feat each member has
  save_party.py --check                      # exit 1 if any parse looks wrong
"""

import argparse
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SAVE_DIRS = [
    r"C:\Juegos\Icewind Dale 2\MPSave",
    r"C:\GOG Games\Icewind Dale 2\MPSave",
]

PARTY_OFFSET = 0x20
PARTY_COUNT = 0x24
NPC_STRIDE = 0x340
NPC_CRE_OFFSET = 0x04
NPC_CRE_SIZE = 0x08
CRE_HEADER = 0x08          # CCreatureFileHeader starts here inside the CRE
FEATS_IN_HEADER = 0x1B8    # m_feats[3]
LEVELS_IN_HEADER = 0x83    # m_classLevels[11], CLASSMASK bit order
CLASS_NAMES = ["barbarian", "bard", "cleric", "druid", "fighter", "monk",
               "paladin", "ranger", "rogue", "sorcerer", "wizard"]
NUMFEATS = 75              # CGAMESPRITE_FEAT_NUMFEATS
SIMPLE_PROFICIENCY_BITS = range(53, 58)


def feat_names():
    """{value: NAME} from CGameSprite.h's CGAMESPRITE_FEAT_* defines."""
    out = {}
    pat = re.compile(r"^#define\s+CGAMESPRITE_FEAT_([A-Z_0-9]+)\s+(\d+)\s*$")
    path = os.path.join(REPO, "src", "CGameSprite.h")
    try:
        with open(path, errors="replace") as f:
            for line in f:
                m = pat.match(line.rstrip())
                if m and "_MAX_" not in m.group(1) and not m.group(1).startswith("NUMFEATS"):
                    out.setdefault(int(m.group(2)), m.group(1))
    except OSError:
        pass
    # NUMFEATS is 75 and shares the prefix; anything at or past it is not a feat.
    return {v: n for v, n in out.items() if v < 75}


def read_gam(path):
    with open(path, "rb") as f:
        return f.read()


def party(data):
    """[(index, cre_bytes)] for each party member the .GAM embeds."""
    if data[:4] != b"GAME":
        return []
    off = struct.unpack_from("<I", data, PARTY_OFFSET)[0]
    count = struct.unpack_from("<I", data, PARTY_COUNT)[0]
    out = []
    for i in range(count):
        base = off + i * NPC_STRIDE
        if base + NPC_STRIDE > len(data):
            break
        cre_off = struct.unpack_from("<I", data, base + NPC_CRE_OFFSET)[0]
        cre_len = struct.unpack_from("<I", data, base + NPC_CRE_SIZE)[0]
        cre = data[cre_off:cre_off + cre_len]
        if cre[:4] == b"CRE ":
            out.append((i, cre))
    return out


def has_feat(cre, n):
    word = struct.unpack_from("<I", cre, CRE_HEADER + FEATS_IN_HEADER + 4 * (n >> 5))[0]
    return bool(word & (1 << (n & 31)))


def member_feats(cre, names):
    return [names[v] for v in sorted(names) if has_feat(cre, v)]


def classes(cre):
    """['bard1', 'fighter3'] - every class this character has a level in."""
    lv = cre[CRE_HEADER + LEVELS_IN_HEADER:CRE_HEADER + LEVELS_IN_HEADER + 11]
    return [f"{CLASS_NAMES[i]}{lv[i]}" for i in range(11) if lv[i]]


def sanity(cre):
    """No bit past the last real feat, and at least one SIMPLE_* proficiency."""
    words = struct.unpack_from("<3I", cre, CRE_HEADER + FEATS_IN_HEADER)
    if words[2] >> (NUMFEATS - 64):
        return False
    return any(has_feat(cre, b) for b in SIMPLE_PROFICIENCY_BITS)


def save_dirs(explicit, all_roots=False):
    """The save root to read. There are TWO game installs on this box carrying
    the same save names, so returning both prints every save twice; the first
    that exists wins unless --all-roots says otherwise."""
    if explicit:
        return [explicit]
    found = [d for d in DEFAULT_SAVE_DIRS if os.path.isdir(d)]
    return found if all_roots else found[:1]


def gam_files(root):
    for name in sorted(os.listdir(root)):
        sub = os.path.join(root, name)
        if not os.path.isdir(sub):
            continue
        for f in os.listdir(sub):
            if f.lower().endswith(".gam"):
                yield name, os.path.join(sub, f)
                break


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("match", nargs="?", help="substring of the save folder name")
    ap.add_argument("--dir", help="save root (default: the MPSave next to the game)")
    ap.add_argument("--feat", help="only list saves holding a member with this feat "
                                   "(name without the CGAMESPRITE_FEAT_ prefix, or a number)")
    ap.add_argument("--feats", action="store_true", help="list every feat per member")
    ap.add_argument("--class", dest="klass",
                    help="only list saves holding a member of this class (e.g. bard)")
    ap.add_argument("--all-roots", action="store_true",
                    help="read every game install found, not just the first")
    ap.add_argument("--check", action="store_true",
                    help="fail unless every member passes the SIMPLE_* sanity check")
    args = ap.parse_args()

    names = feat_names()
    want = None
    if args.feat:
        if args.feat.isdigit():
            want = int(args.feat)
        else:
            key = args.feat.upper().replace("CGAMESPRITE_FEAT_", "")
            want = next((v for v, n in names.items() if n == key), None)
            if want is None:
                print(f"unknown feat {args.feat!r}")
                return 2

    roots = save_dirs(args.dir, args.all_roots)
    if not roots:
        print("no save directory found; pass --dir")
        return 2

    bad = 0
    for root in roots:
        for folder, gam in gam_files(root):
            if args.match and args.match not in folder:
                continue
            members = party(read_gam(gam))
            if not members:
                continue
            rows = []
            for idx, cre in members:
                ok = sanity(cre)
                if not ok:
                    bad += 1
                feats = member_feats(cre, names)
                rows.append((idx, ok, feats, classes(cre)))
            if want is not None and not any(names.get(want) in f for _i, _o, f, _c in rows):
                continue
            if args.klass and not any(any(c.startswith(args.klass.lower()) for c in cls)
                                      for _i, _o, _f, cls in rows):
                continue
            print(f"== {folder}   {len(rows)} members")
            for idx, ok, feats, cls in rows:
                mark = "" if ok else "   [SANITY FAIL - parse landed wrong]"
                who = f"   party slot {idx}  (portrait {idx + 1})  {'/'.join(cls)}"
                if want is not None:
                    hit = "HAS" if names.get(want) in feats else "no"
                    print(f"{who}  {hit} {names[want]}{mark}")
                elif args.feats:
                    print(f"{who}{mark}")
                    print(f"      {', '.join(feats) or '(none)'}")
                else:
                    notable = [f for f in feats if not f.startswith("SIMPLE_")]
                    print(f"{who}  {len(feats)} feats: "
                          f"{', '.join(notable) or '(only SIMPLE_*)'}{mark}")
    if args.check and bad:
        print(f"\n{bad} members failed the SIMPLE_* sanity check")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
