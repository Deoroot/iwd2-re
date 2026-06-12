#!/usr/bin/env python3
"""ds_batch.py - offload mechanical, verifiable batch analysis to DeepSeek.

Preserves Claude quota for actual recovery work; DeepSeek output lands in a file
that Claude reads compactly. Mechanical tasks only -- results are verifiable
against the repo, so a weaker model is acceptable.

  scripts/ds_batch.py parity_triage tmp_parity.json [--out tmp_ds_triage.md]
        rank RED/YELLOW parity functions: failing signals -> suspected cause -> order
  scripts/ds_batch.py rename_sweep src/CGameSprite.cpp [more files...]
        audit /*#guess*/ renames: confirm/reject table with one-line reasons
  scripts/ds_batch.py free --prompt-file p.md [--files a,b,c] [--out tmp_ds.md]
        freeform: prompt file + optional attached files

Env: DEEPSEEK_API_KEY (required), DEEPSEEK_MODEL (default deepseek-chat),
DEEPSEEK_URL (default https://api.deepseek.com/chat/completions).
"""
import argparse
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
URL = os.environ.get("DEEPSEEK_URL", "https://api.deepseek.com/chat/completions")
MODEL = os.environ.get("DEEPSEEK_MODEL", "deepseek-chat")
MAX_INPUT_CHARS = 300_000


def call(prompt, system="You are a precise reverse-engineering assistant. Answer with the requested table/format only, no preamble."):
    key = os.environ.get("DEEPSEEK_API_KEY")
    if not key:
        sys.exit("DEEPSEEK_API_KEY not set")
    if len(prompt) > MAX_INPUT_CHARS:
        sys.exit(f"input too large ({len(prompt)} chars > {MAX_INPUT_CHARS}); split it")
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "system", "content": system},
                     {"role": "user", "content": prompt}],
        "temperature": 0,
        "max_tokens": 8000,
    }).encode()
    req = urllib.request.Request(URL, data=body, headers={
        "Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        resp = json.load(r)
    u = resp.get("usage", {})
    print(f"deepseek {MODEL}: in={u.get('prompt_tokens', '?')} out={u.get('completion_tokens', '?')}",
          file=sys.stderr)
    return resp["choices"][0]["message"]["content"]


def t_parity_triage(args):
    data = Path(args.input[0]).read_text()
    prompt = f"""Below is re-agent parity output (JSON) for recovered C++ vs the Ghidra decompile
of IWD2.exe. For every function whose verdict is RED or YELLOW, produce ONE markdown
table sorted by triage priority (most likely real under-implementation first):

| addr | name | verdict | failing signals | suspected cause (1 line) |

Rules: STL-inline call-count mismatches are usually false positives (note them as such,
lowest priority). Control-flow divergence and missing-call signals rank highest.
After the table add a 3-line summary: how many real vs false-positive suspects.

{data}"""
    return prompt


def t_rename_sweep(args):
    chunks = []
    for f in args.input:
        text = Path(f).read_text(errors="replace")
        keep = [f"{i + 1}: {l}" for i, l in enumerate(text.splitlines()) if "#guess" in l]
        if keep:
            chunks.append(f"### {f}\n" + "\n".join(keep))
    if not chunks:
        sys.exit("no /*#guess*/ markers in the given files")
    prompt = """Each line below carries a /*#guess*/ rename marker from hand-recovered IWD2
C++ (BG2-family Infinity Engine). For each, judge the guessed identifier name from the
visible context. ONE markdown table:

| file:line | guessed name | verdict (keep/rename/unsure) | better name or reason (1 line) |

Be conservative: 'keep' unless the context clearly contradicts the name.

""" + "\n\n".join(chunks)
    return prompt


def t_free(args):
    prompt = Path(args.prompt_file).read_text()
    if args.files:
        for f in args.files.split(","):
            prompt += f"\n\n### {f}\n```\n{Path(f).read_text(errors='replace')}\n```"
    return prompt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("template", choices=["parity_triage", "rename_sweep", "free"])
    ap.add_argument("input", nargs="*", help="input file(s) for the template")
    ap.add_argument("--prompt-file", help="free: prompt markdown")
    ap.add_argument("--files", help="free: comma-separated files to attach")
    ap.add_argument("--out", help="output file (default tmp_ds_<template>.md)")
    ap.add_argument("--dry", action="store_true", help="assemble the prompt, print stats, no API call")
    args = ap.parse_args()

    if args.template == "free":
        if not args.prompt_file:
            sys.exit("free needs --prompt-file")
        prompt = t_free(args)
    elif args.template == "parity_triage":
        if not args.input:
            sys.exit("parity_triage needs the parity JSON path")
        prompt = t_parity_triage(args)
    else:
        if not args.input:
            sys.exit("rename_sweep needs at least one source file")
        prompt = t_rename_sweep(args)

    out = Path(args.out or REPO / f"tmp_ds_{args.template}.md")
    if args.dry:
        print(f"prompt: {len(prompt)} chars (~{len(prompt) // 4} tokens), model {MODEL}, would write {out}")
        return
    result = call(prompt)
    out.write_text(result, encoding="utf-8")
    lines = result.count("\n") + 1
    print(f"{out}  ({lines} lines)")
    head = "\n".join(result.splitlines()[:6])
    print(head)


if __name__ == "__main__":
    main()
