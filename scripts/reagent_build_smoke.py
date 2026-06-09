#!/usr/bin/env python3
"""Build+smoke gate for a first-pass reverse, in an ISOLATED scratch worktree.

The strongest automated faithfulness gate we have: drop a candidate function into a
throwaway git worktree, build it (VS2019 Win32), and load a save -- WITHOUT touching
/src or the canonical iwd2-re.exe. A first-pass reverse that does not even compile, or
that crashes the game on load, is rejected here deterministically (no LLM needed).

Pipeline tier 3 (see the project-re-agent-workflow memory):
  mechanical resolve (a/b) -> deepseek bulk pass -> THIS build+smoke gate -> Claude -> Frida.

Steps:
  1. resolve --address -> src file via address_map.json
  2. sync a persistent detached worktree to the repo's current HEAD (resets src/)
  3. (optional) swap the target function's body with --code
  4. cmake build Debug -> <worktree>/build/Debug/iwd2-re.exe
  5. smoke: scripts/auto_start_game.py --exe <that> --slot N  (loads a save)
  6. report PASS | BUILD_FAIL | SMOKE_FAIL

Usage:
  python scripts/reagent_build_smoke.py --address 0x402b70                  # baseline: /src as-is
  python scripts/reagent_build_smoke.py --address 0x402b70 --code cand.cpp  # gate a candidate
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(r"C:\iwd2-re")
DEFAULT_MAP = REPO / ".ghidra-exports" / "address_map.json"
DEFAULT_WORKTREE = Path(r"C:\iwd2-re-scratch")
AUTO_START = REPO / "scripts" / "auto_start_game.py"
GENERATOR = "Visual Studio 16 2019"


def run(cmd, **kw) -> tuple[int, str]:
    """Run a command, returning (returncode, combined stdout+stderr)."""
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       encoding="utf-8", errors="replace", **kw)
    return p.returncode, p.stdout


def tail(s: str, n: int = 25) -> str:
    return "\n".join(s.strip().splitlines()[-n:])


# ---- function-span location ----------------------------------------------
def match_brace(s: str, start: int) -> int:
    """Index just past the '}' matching the '{' at *start*, skipping comments/strings."""
    depth = 0
    i, n = start, len(s)
    line = block = dq = sq = False
    while i < n:
        c = s[i]
        nxt = s[i + 1] if i + 1 < n else ""
        if line:
            if c == "\n":
                line = False
        elif block:
            if c == "*" and nxt == "/":
                block = False
                i += 1
        elif dq:
            if c == "\\":
                i += 1
            elif c == '"':
                dq = False
        elif sq:
            if c == "\\":
                i += 1
            elif c == "'":
                sq = False
        elif c == "/" and nxt == "/":
            line = True
            i += 1
        elif c == "/" and nxt == "*":
            block = True
            i += 1
        elif c == '"':
            dq = True
        elif c == "'":
            sq = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def locate_function(text: str, addr: int):
    """Return the (start, end) char span of the function marked ``// 0xADDR``."""
    m = re.search(r"//[ \t]*0x0*%X\b" % addr, text, re.IGNORECASE)
    if not m:
        return None
    start = text.rfind("\n", 0, m.start()) + 1
    brace = text.find("{", m.end())
    if brace == -1:
        return None
    end = match_brace(text, brace)
    if end == -1:
        return None
    nl = text.find("\n", end)
    return start, (len(text) if nl == -1 else nl + 1)


def src_file_for(addr: int, map_path: Path) -> str | None:
    raw = json.loads(map_path.read_text(encoding="utf-8"))
    info = raw.get(f"{addr:08x}")
    return info.get("file") if isinstance(info, dict) else None


# ---- worktree -------------------------------------------------------------
def sync_worktree(wt: Path) -> None:
    """Create the detached scratch worktree (once) or reset its src/ to HEAD."""
    _, head = run(["git", "-C", str(REPO), "rev-parse", "HEAD"])
    head = head.strip()
    if not (wt / ".git").exists():
        rc, out = run(["git", "-C", str(REPO), "worktree", "add", "--detach", str(wt), head])
        if rc != 0:
            raise SystemExit(f"worktree add failed:\n{out}")
    else:
        rc, out = run(["git", "-C", str(wt), "reset", "--hard", head])
        if rc != 0:
            raise SystemExit(f"worktree reset failed:\n{out}")


# ---- main -----------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--address", required=True, help="target function address (e.g. 0x402b70)")
    ap.add_argument("--code", type=Path,
                    help="file with the replacement function (incl // 0xADDR + signature + body); "
                         "omit for a baseline build+smoke of /src as-is")
    ap.add_argument("--slot", type=int, default=2, help="save slot to load in the smoke (default 2 = combat save)")
    ap.add_argument("--timeout", type=float, default=180.0, help="seconds to wait for world activation")
    ap.add_argument("--worktree", type=Path, default=DEFAULT_WORKTREE, help="persistent scratch worktree path")
    ap.add_argument("--map", type=Path, default=DEFAULT_MAP, help="address_map.json (address -> src file)")
    ap.add_argument("--remove", action="store_true",
                    help="remove the worktree when done (slower: next run rebuilds from scratch)")
    args = ap.parse_args()

    try:
        addr = int(args.address, 16)
    except ValueError:
        print(f"bad address: {args.address}")
        return 2

    rel = src_file_for(addr, args.map)
    if rel is None:
        print(f"address {args.address} not in {args.map.name}")
        return 2
    print(f"[1/5] target {args.address} -> src/{rel}")

    sync_worktree(args.worktree)
    print(f"[2/5] worktree synced @ {args.worktree}")

    if args.code:
        wt_file = args.worktree / "src" / rel
        text = wt_file.read_text(encoding="utf-8", errors="replace")
        span = locate_function(text, addr)
        if span is None:
            print(f"could not locate function {args.address} in {wt_file}")
            return 2
        s, e = span
        repl = args.code.read_text(encoding="utf-8", errors="replace").rstrip() + "\n"
        wt_file.write_text(text[:s] + repl + text[e:], encoding="utf-8")
        print(f"[3/5] swapped function body ({e - s} -> {len(repl)} chars) in src/{rel}")
    else:
        print("[3/5] no --code: baseline build of /src as-is")

    build = args.worktree / "build"
    rc, out = run(["cmake", "-S", str(args.worktree), "-B", str(build), "-G", GENERATOR, "-A", "Win32"])
    if rc != 0:
        print("RESULT: BUILD_FAIL (configure)\n" + tail(out))
        return 1
    rc, out = run(["cmake", "--build", str(build), "--config", "Debug"])
    if rc != 0:
        print("RESULT: BUILD_FAIL\n" + tail(out))
        return 1
    exe = build / "Debug" / "iwd2-re.exe"
    print(f"[4/5] built {exe}")

    rc, out = run([sys.executable, str(AUTO_START), "--exe", str(exe),
                   "--slot", str(args.slot), "--timeout", str(args.timeout)])
    print("[5/5] smoke output:\n" + tail(out, 10))

    if args.remove:
        run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(args.worktree)])

    result = "PASS" if rc == 0 else "SMOKE_FAIL"
    print(f"RESULT: {result}")
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
