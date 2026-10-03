#!/usr/bin/env python3
"""layout_offsets.py - check `/* 0xNNN */` member comments against the compiler.

`struct_layout_audit.py` needs the VM.  This one needs only the local MSVC: it
asks the compiler where every commented member of a class really sits and
compares that with the comment.  It is how s55 found that CUIControlButton was
0x670 against the binary's 0x666.

The trick is an incomplete template:

    template <int N> struct Show;
    Show<offsetof(CUIControlButton, m_pText)> x;   // error C2079: ... 'Show<314>'

The error text carries N.  `offsetof` compiles fine on polymorphic classes.

A comment is a CLAIM, not the binary: a mismatch means the comment or the
declared type is wrong, and only the disassembly says which.  A match means
our layout equals the comment -- check a few comments against the bytes
(`sym.py disasm` on the constructor) before leaning on them.

Known noise: in a Debug compile `CCriticalSection` is 0x24, not 0x20 (MFC's
`_DEBUG`-only `m_strName`), so members after one report +4.

Usage:
  python scripts/layout_offsets.py CItem CUIControlButton      # these classes
  python scripts/layout_offsets.py --tree CUIControlBase       # it + descendants
  python scripts/layout_offsets.py --tree CUIControlBase --sizes
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "src")
VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
# vcvars32 fails on a PATH holding an unquoted "(x86)" entry, so give it a bare one.
BARE_PATH = r"C:\Windows\System32;C:\Windows;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
DEFINES = ["WIN32", "_WINDOWS", "_AFXDLL", "_CRT_NONSTDC_NO_WARNINGS",
           "_CRT_SECURE_NO_WARNINGS", "_MBCS", "_WIN32_WINNT=0x0501"]
INCLUDES = [SRC,
            os.path.join(REPO, "build", "_deps", "zlib-src"),
            os.path.join(REPO, "build", "_deps", "zlib-build"),
            os.path.join(REPO, "third_party", "directplay", "include")]
CHUNK = 90  # cl stops at 100 errors, and every probe IS an error

HEAD = re.compile(r"^(class|struct)\s+(\w+)\s*(?::\s*(?:public\s+|protected\s+|private\s+)?(\w+)[^{]*)?\{\s*$")
MEMBER = re.compile(r"^\s*/\*\s*([0-9A-Fa-f]{3,5})\s*\*/\s*([^()]*?)\s*;")
NAME = re.compile(r"(\w+)\s*(\[[^\]]*\]\s*)*$")
SHOWN = re.compile(r"'(O__(\w+?)__(\w+)__([0-9A-F]+)|SZ__(\w+))'[^\n]*'Show<(\d+)>'")


def parse_headers():
    classes = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.h"))):
        cur = None
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.rstrip("\r\n")
                m = HEAD.match(line)
                if m and cur is None:
                    cur = {"name": m.group(2), "base": m.group(3) or "",
                           "file": os.path.basename(path), "members": []}
                    classes[cur["name"]] = cur
                    continue
                if cur is None:
                    continue
                if line.startswith("};"):
                    cur = None
                    continue
                m = MEMBER.match(line)
                if not m or any(w in line for w in ("virtual", "static", "override")):
                    continue
                decl = m.group(2)
                if ":" in decl:  # bit-field: offsetof does not apply
                    continue
                n = NAME.search(decl)
                if n:
                    cur["members"].append((int(m.group(1), 16), n.group(1)))
    return classes


def ancestors(classes, name):
    out = []
    while name in classes and classes[name]["base"]:
        name = classes[name]["base"]
        out.append(name)
    return out


def compile_chunk(workdir, index, lines):
    cpp = os.path.join(workdir, "chk%d.cpp" % index)
    with open(cpp, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    flags = ["/nologo", "/Zs", "/std:c++17", "/EHsc", "/MDd", "/GR", "/W3"]
    flags += ["/D" + d for d in DEFINES] + ['/I"%s"' % i for i in INCLUDES]
    bat = os.path.join(workdir, "run.bat")
    with open(bat, "w") as fh:
        fh.write('@echo off\ncall "%s" >nul\ncl %s "%s"\n' % (VCVARS, " ".join(flags), cpp))
    env = dict(os.environ, PATH=BARE_PATH)
    r = subprocess.run(["cmd.exe", "/c", bat], capture_output=True, env=env,
                       cwd=workdir)
    return (r.stdout + r.stderr).decode("cp1252", errors="replace")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("classes", nargs="*", help="class names to check")
    ap.add_argument("--tree", action="append", default=[],
                    help="a class plus everything derived from it")
    ap.add_argument("--sizes", action="store_true", help="also print every sizeof")
    args = ap.parse_args()

    classes = parse_headers()
    want = list(args.classes)
    for root in args.tree:
        want.append(root)
        want += [c for c in classes if root in ancestors(classes, c)]
    missing = [c for c in want if c not in classes]
    if missing:
        sys.exit("not found in src/*.h: " + ", ".join(missing))
    targets = [classes[c] for c in dict.fromkeys(want) if classes[c]["members"]]
    if not targets:
        sys.exit("no class with commented members among: " + ", ".join(want))

    # Access control does not move a member; opening it up after MFC is in lets
    # offsetof reach private ones.
    head = ['#include "mfc.h"', "#define private public", "#define protected public"]
    head += ['#include "%s"' % f for f in sorted({c["file"] for c in targets})]
    head += ["#include <cstddef>", "template <int N> struct Show;"]
    probes = []
    for c in targets:
        probes.append("Show<sizeof(%s)> SZ__%s;" % (c["name"], c["name"]))
        for off, name in c["members"]:
            probes.append("Show<offsetof(%s,%s)> O__%s__%s__%X;"
                          % (c["name"], name, c["name"], name, off))

    text = ""
    with tempfile.TemporaryDirectory() as workdir:
        for i in range(0, len(probes), CHUNK):
            text += compile_chunk(workdir, i // CHUNK, head + probes[i:i + CHUNK])

    sizes, checked, bad = {}, 0, 0
    for m in SHOWN.finditer(text):
        if m.group(5):
            sizes[m.group(5)] = int(m.group(6))
            continue
        checked += 1
        claim, got = int(m.group(4), 16), int(m.group(6))
        if claim != got:
            bad += 1
            print("%-46s %-28s comment %#x  compiled %#x  (%+d)"
                  % (m.group(2), m.group(3), claim, got, got - claim))
    expected = sum(len(c["members"]) for c in targets)
    other = [l for l in text.splitlines() if " error " in l and "C2079" not in l]
    if args.sizes:
        for c in targets:
            print("sizeof(%s) = %#x" % (c["name"], sizes.get(c["name"], -1)))
    print("%d classes, %d of %d members measured, %d mismatch"
          % (len(targets), checked, expected, bad))
    if other or checked != expected:
        print("compile trouble (a private member? a header that does not stand "
              "alone?):")
        print("\n".join(other[:10]))
        return 2
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
