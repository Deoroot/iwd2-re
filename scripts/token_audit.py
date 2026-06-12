#!/usr/bin/env python3
"""token_audit.py - mine Claude Code transcripts for token-cost hotspots.

  scripts/token_audit.py                  host-era sessions (cwd under /home/wills)
  scripts/token_audit.py --era all        every session (includes pre-migration C:\\ era)
  scripts/token_audit.py --days 7         only sessions touched in the last N days

Reports: usage totals (cache_read = context depth x round-trips, THE dominant cost),
tool call counts vs injected result chars, Bash by normalized command, most re-read
files, agent spawns, ad-hoc python heredoc count. Success criteria after the 2026-06
optimization pass: avg context/msg < 120K, py heredocs < 20/era, raw `gb decompile`
< 30/era (use fn_digest.py), shrinking grep->Read chains on src/ (use src_find.py).
"""
import argparse
import json
import glob
import os
import re
import time
from collections import Counter

PROJ = os.path.expanduser("~/.claude/projects/-home-wills-iwd2-re")


def norm_bash(cmd):
    first = cmd.strip().split("\n")[0]
    first = re.sub(r"^cd\s+\S+\s*&&\s*", "", first)
    toks = first.split()
    if not toks:
        return "(empty)"
    t0 = toks[0]
    if t0 in ("python", "python3", ".venv-reagent/bin/python"):
        for t in toks[1:]:
            if t.endswith(".py"):
                return f"py {os.path.basename(t)}"
            if t == "-":
                return "py heredoc"
        return "py inline"
    if "ghidra-bridge" in t0:
        return "gb " + next((t for t in toks[1:] if not t.startswith("-") and "yaml" not in t), "?")
    if "re-agent" in t0:
        return "re-agent " + next((t for t in toks[1:] if not t.startswith("-") and "yaml" not in t), "?")
    if t0 == "ssh":
        return "ssh " + " ".join(toks[1:3])[:25]
    if t0.endswith(".sh") or t0.startswith("scripts/"):
        return os.path.basename(t0)
    if t0 in ("rtk", "git"):
        return t0 + " " + (toks[1] if len(toks) > 1 else "")
    return t0


def clen(c):
    if isinstance(c, str):
        return len(c)
    if isinstance(c, list):
        return sum(len(b.get("text", "") or "") for b in c if isinstance(b, dict))
    return 0


def session_cwd(path):
    for line in open(path, errors="replace"):
        if '"cwd"' in line:
            try:
                return json.loads(line).get("cwd", "")
            except Exception:
                continue
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--era", choices=["host", "all"], default="host")
    ap.add_argument("--days", type=int, default=0)
    ap.add_argument("--project", default=PROJ)
    args = ap.parse_args()

    files = glob.glob(os.path.join(args.project, "*.jsonl"))
    if args.days:
        cutoff = time.time() - args.days * 86400
        files = [f for f in files if os.path.getmtime(f) > cutoff]
    if args.era == "host":
        files = [f for f in files if session_cwd(f).startswith("/home/")]
    print(f"sessions: {len(files)}")

    usage = Counter()
    tool_count, tool_chars = Counter(), Counter()
    bash_count, bash_chars = Counter(), Counter()
    read_paths, read_chars = Counter(), Counter()
    agents = Counter()
    id2tool, id2desc = {}, {}

    for f in files:
        for line in open(f, errors="replace"):
            try:
                e = json.loads(line)
            except Exception:
                continue
            t = e.get("type")
            if t == "assistant":
                msg = e.get("message", {})
                u = msg.get("usage") or {}
                for k in ("input_tokens", "output_tokens",
                          "cache_creation_input_tokens", "cache_read_input_tokens"):
                    usage[k] += u.get(k, 0) or 0
                usage["n"] += 1
                for b in msg.get("content") or []:
                    if isinstance(b, dict) and b.get("type") == "tool_use":
                        name = b.get("name", "?")
                        tool_count[name] += 1
                        tid = b.get("id")
                        inp = b.get("input") or {}
                        id2tool[tid] = name
                        if name == "Bash":
                            nb = norm_bash(inp.get("command", ""))
                            bash_count[nb] += 1
                            id2desc[tid] = nb
                        elif name == "Read":
                            p = str(inp.get("file_path", "?"))
                            id2desc[tid] = p
                            read_paths[p] += 1
                        elif name in ("Agent", "Task"):
                            agents[str(inp.get("subagent_type", "(none)"))] += 1
            elif t == "user":
                c = (e.get("message") or {}).get("content")
                if isinstance(c, list):
                    for b in c:
                        if isinstance(b, dict) and b.get("type") == "tool_result":
                            tid = b.get("tool_use_id")
                            n = clen(b.get("content"))
                            name = id2tool.get(tid, "?")
                            tool_chars[name] += n
                            if name == "Bash":
                                bash_chars[id2desc.get(tid, "?")] += n
                            elif name == "Read":
                                read_chars[id2desc.get(tid, "?")] += n

    print("\n== usage ==")
    for k in ("input_tokens", "output_tokens", "cache_creation_input_tokens", "cache_read_input_tokens"):
        print(f"{k}: {usage[k]:,}")
    if usage["n"]:
        print(f"messages: {usage['n']:,}   avg context/msg (cache_read): {usage['cache_read_input_tokens'] // usage['n']:,}")

    print("\n== tools (count / result chars) ==")
    for name, cnt in tool_count.most_common(14):
        print(f"{cnt:5d}  {tool_chars.get(name, 0) / 1e6:6.2f}M  {name}")

    print("\n== bash by result chars (top 20) ==")
    for cmd, ch in sorted(bash_chars.items(), key=lambda x: -x[1])[:20]:
        print(f"{bash_count.get(cmd, 0):4d}x {ch / 1e3:7.0f}K  {cmd}")

    print("\n== most re-read files (top 12) ==")
    for p, c in read_paths.most_common(12):
        print(f"{c:3d}x {read_chars.get(p, 0) / 1e3:6.0f}K  {p.replace(os.path.expanduser('~'), '~')}")

    if agents:
        print("\n== agent spawns ==")
        for a, c in agents.most_common():
            print(f"{c:3d}  {a}")

    print("\n== watchlist ==")
    print(f"py heredoc/inline: {bash_count.get('py heredoc', 0) + bash_count.get('py inline', 0)}  (target <20; use sym.py / src_find.py)")
    print(f"gb decompile raw:  {bash_count.get('gb decompile', 0)}  (target <30; use fn_digest.py)")
    print(f"grep+sed+cat:      {bash_count.get('grep', 0) + bash_count.get('sed', 0) + bash_count.get('cat', 0)}  (use src_find.py on src/)")
    print(f"ssh/scp/iconv:     {sum(c for k, c in bash_count.items() if k.startswith(('ssh ', 'scp', 'iconv')))}  (use vm.sh)")


if __name__ == "__main__":
    main()
