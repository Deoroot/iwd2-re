#!/usr/bin/env python3
"""First-pass reverse driver: assemble context -> deepseek -> mechanical gates -> emit.

The back half of the integration driver. Chains the pieces built so far into the
tiered pipeline (strongest gate last), reserving Claude for the final human sign-off:

  1. assemble the offline context bundle      (reagent_assemble_context.py)
  2. deepseek first pass over it               (OpenCode Go, openai-compat + .env key)
  3. build+smoke the candidate in a worktree   (reagent_build_smoke.py)  -- HARD gate
  4. asm callee set-diff vs the binary         (reagent_asm_verify.py)   -- soft lint
  5. on BUILD_FAIL, feed the compiler error back for a fix round (loop, bounded)
  6. emit candidate + report to reports/re-agent/  -- NEVER writes src/

deepseek does the bulk cheaply (looser OpenCode Go limits); build+smoke is the
deterministic gate that rejects anything that will not compile or crashes the load;
the asm lint is advisory (inlining false positives, see reagent_asm_verify). Claude
reads the emitted candidate + report and decides whether it reaches src/.

Runs under the toolchain venv (needs ``openai`` + ``re_agent``)::

    .venv-reagent/Scripts/python scripts/reagent_first_pass.py --address 0x402b70
    .venv-reagent/Scripts/python scripts/reagent_first_pass.py --address 0x402b70 --dry-run
    .venv-reagent/Scripts/python scripts/reagent_first_pass.py --address 0x402b70 --gate asm
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(r"C:\iwd2-re")
SCRIPTS = REPO / "scripts"
CONFIG = REPO / "re-agent.yaml"
OUT_DIR = REPO / "reports" / "re-agent" / "code"
PYTHON = sys.executable

CODE_FENCE_RE = re.compile(r"```(?:cpp|c\+\+|c)?\s*\n(.*?)```", re.S)

# Decompiler artefacts: a fence full of these is an ECHO of the Ghidra decompile the
# model quoted while reasoning, NOT its recovered C++. We reject those -- deepseek
# (no tool channel) reasons in-content and litters the reply with such snippets.
ECHO_TOKENS = ("FUN_0", "DAT_0", "param_1", "param_2", "undefined", "uVar", "iVar",
               "CONCAT", "ExceptionList", "local_", "LAB_0", "in_EAX", "extraout_")


def run(cmd: list[str]) -> tuple[int, str]:
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       encoding="utf-8", errors="replace")
    return p.returncode, p.stdout


def assemble(address: str) -> str:
    rc, out = run([PYTHON, str(SCRIPTS / "reagent_assemble_context.py"), "--address", address])
    if rc != 0:
        raise SystemExit(f"assemble failed:\n{out}")
    return out


def _is_echo(block: str) -> bool:
    """A fence dominated by decompiler artefacts = quoted decompile, not recovered C++."""
    return sum(block.count(t) for t in ECHO_TOKENS) >= 3


def _marked_blocks(reply: str, address: int) -> list[str]:
    """Every fenced block carrying the ``// 0xADDR`` marker, in reply order, stripped."""
    marker = re.compile(r"//[ \t]*0x0*%X\b" % address, re.IGNORECASE)
    return [b.strip() for b in CODE_FENCE_RE.findall(reply) if marker.search(b)]


def extract_code(reply: str, address: int, require_marker: bool = False) -> str | None:
    """Pull the recovered function out of the model reply.

    The signature of a real FINAL answer (vs the decompile/header snippets deepseek
    quotes mid-reasoning) is the ``// 0xADDR`` marker the instructions demand. A marked
    fence WINS outright -- even if it still holds unresolved ``FUN_``/``DAT_`` tokens,
    which a faithful first pass legitimately leaves in (instruction 4). The echo filter
    only guards the markerless *best-effort* path, so a quoted decompile is not mistaken
    for the answer when the model never marked one. With ``require_marker`` we accept
    ONLY a marked fence (the caller uses that to decide whether to fire the closer turn).
    Returns None when nothing qualifies.
    """
    marked = _marked_blocks(reply, address)
    if marked:                             # marker wins: a marked fence is the final answer
        return marked[0]
    if require_marker:
        return None
    blocks = CODE_FENCE_RE.findall(reply)
    clean = [b for b in blocks if not _is_echo(b)]   # markerless: dodge decompile echoes
    return clean[-1].strip() if clean else None


# A MARKED block can still be a non-answer. deepseek sometimes emits the // 0xADDR marker
# and the signature but STUBS the body (`{ // function body }`), or fabricates
# `extern`/forward declarations for the unresolved FUN_/DAT_ tokens (a hack that violates
# missing>wrong and turns a compile error into a link error -- both useless). Either way
# the recovery is empty, so the marker must NOT short-circuit the closer turn.
HOLLOW_RE = re.compile(
    r"//\s*(?:function body|implementation(?: goes here)?|body here|your code|"
    r"fill in|stub|TODO|\.\.\.)\s*$", re.I | re.M)
FABRICATED_DECL_RE = re.compile(r"\bextern\b[^\n;{]*\b(?:FUN_|DAT_)[0-9a-fA-F]+", re.I)


def _bodies(text: str) -> list[str]:
    """Every brace-balanced body that opens right after a ``)`` -- function definitions.

    A vtable/struct/initializer ``{`` is preceded by a name/``=``, not ``)``, so this
    isolates real function bodies from the decls deepseek puts around them.
    """
    out: list[str] = []
    i = 0
    while True:
        j = text.find("{", i)
        if j < 0:
            break
        k = j - 1
        while k >= 0 and text[k] in " \t\r\n":
            k -= 1
        if k >= 0 and text[k] == ")":          # `... ) {` -- a function body opens here
            depth, m = 1, j + 1
            while m < len(text) and depth:
                depth += (text[m] == "{") - (text[m] == "}")
                m += 1
            out.append(text[j + 1:m - 1])
            i = m
        else:
            i = j + 1
    return out


def _is_hollow(block: str) -> bool:
    """True if the recovered function has no real body (placeholder or empty braces)."""
    if HOLLOW_RE.search(block):
        return True
    no_comments = re.sub(r"//.*", "", re.sub(r"/\*.*?\*/", "", block, flags=re.S))
    bodies = _bodies(no_comments)
    if not bodies:
        return False                           # no function body found -> not this signal
    biggest = max(bodies, key=len).strip()     # the recovered fn is the largest body
    return ";" not in biggest and not re.search(
        r"\b(return|if|for|while|switch|goto)\b", biggest)


def _needs_closer(block: str | None) -> str | None:
    """Why a marked block still needs a closer turn (None = it is a real answer)."""
    if block is None:
        return "no marked block"
    if _is_hollow(block):
        return "hollow body (stub/placeholder)"
    if FABRICATED_DECL_RE.search(block):
        return "fabricated extern decls for FUN_/DAT_"
    return None


def _closer_prompt(address: int) -> str:
    return (f"Stop analysing. Output ONLY the final recovered function now: exactly one "
            f"```cpp code block whose first line is the `// 0x{address:X}` marker, "
            f"idiomatic C++ matching src/ (real this->/CClass::). Write the REAL body -- "
            f"every statement the decompile shows, NOT a `// function body` placeholder or "
            f"empty `{{}}`. Leave unresolved FUN_/DAT_ calls as their raw token; do NOT "
            f"invent extern/forward declarations for them. No prose before or after.")


def recover_with_closer(llm, cid, address: int, reply: str, out_dir: Path, tag: str) -> str | None:
    """A real (marked, non-hollow, no-fabricated-decl) function from *reply*, else a closer.

    deepseek (no tool channel) reasons in-content: reply 1 is often pure analysis with no
    final block, OR a marked block whose body is a stub / invented externs. The thinking
    is already in context, so ONE "closer" turn demanding the marked block ALONE plays to
    the model's grain instead of fighting its ramble. Prefers the closer's block, then any
    clean block; returns None only if nothing extractable surfaced at all.
    """
    marked = _marked_blocks(reply, address)
    for b in marked:                           # a clean marked block already IS the answer
        if _needs_closer(b) is None:
            return b
    why = _needs_closer(marked[0] if marked else None)
    print(f"  {why} -> closer turn ({tag})...")
    reply2 = llm.resume(cid, _closer_prompt(address))
    (out_dir / f"{address:08x}.{tag}.txt").write_text(reply2, encoding="utf-8")
    cands = [c for c in (extract_code(reply2, address, require_marker=True),
                         extract_code(reply2, address),
                         extract_code(reply, address, require_marker=True),
                         extract_code(reply, address)) if c]
    for c in cands:                            # prefer a clean block (the closer's first)
        if _needs_closer(c) is None:
            return c
    return cands[0] if cands else None         # best-effort; let the build gate judge it


def make_llm():
    from re_agent.config.loader import load_config
    from re_agent.llm.openai_compat import OpenAIProvider
    cfg = load_config(CONFIG)
    llm = OpenAIProvider(
        api_key=cfg.llm.api_key, model=cfg.llm.model, base_url=cfg.llm.base_url,
        max_tokens=cfg.llm.max_tokens, temperature=cfg.llm.temperature,
    )
    rounds = getattr(getattr(cfg, "orchestrator", None), "max_review_rounds", 5)
    return llm, rounds


def gate_build(address: str, candidate: Path) -> tuple[str, str]:
    rc, out = run([PYTHON, str(SCRIPTS / "reagent_build_smoke.py"),
                   "--address", address, "--code", str(candidate)])
    m = re.search(r"RESULT:\s*(\w+)", out)
    return (m.group(1) if m else "UNKNOWN"), out


def gate_asm(address: str, candidate: Path) -> dict:
    rc, out = run([PYTHON, str(SCRIPTS / "reagent_asm_verify.py"),
                   "--address", address, "--src", str(candidate), "--json"])
    try:
        return json.loads(out)
    except json.JSONDecodeError:
        return {"verdict": "UNKNOWN", "raw": out[-500:]}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--address", required=True, help="target function address")
    ap.add_argument("--gate", choices=["build", "asm", "both", "none"], default="both",
                    help="which gates to run (default both)")
    ap.add_argument("--rounds", type=int, help="max fix rounds (default: re-agent.yaml max_review_rounds)")
    ap.add_argument("--dry-run", action="store_true", help="assemble + print the prompt, no LLM call")
    ap.add_argument("--out-dir", type=Path, default=OUT_DIR)
    args = ap.parse_args()

    try:
        addr = int(args.address, 16)
    except ValueError:
        print(f"bad address: {args.address}")
        return 2

    bundle = assemble(args.address)
    print(f"[1/4] assembled context: {len(bundle)} chars")
    if args.dry_run:
        sys.stdout.write(bundle)
        return 0

    llm, cfg_rounds = make_llm()
    rounds = args.rounds if args.rounds is not None else cfg_rounds
    cid = llm.new_conversation(
        "You are an expert reverse engineer recovering IWD2.exe into idiomatic, "
        "buildable C++ that matches the existing src/ exactly. Be terse: minimal "
        "analysis, then the final function in ONE ```cpp block beginning with its "
        "// 0xADDR marker, with its REAL body -- every statement the decompile shows, "
        "never a placeholder or empty stub. Never put decompile snippets in code fences, "
        "and never fabricate extern/forward declarations for unresolved FUN_/DAT_ tokens "
        "(leave the raw call -- an unresolved call is the honest gap, a fake decl is a hack).")
    print("[2/4] deepseek first pass...")
    reply = llm.resume(cid, bundle)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / f"{addr:08x}.reply.txt").write_text(reply, encoding="utf-8")

    # A real first pass is a MARKED, non-hollow block with no fabricated decls. deepseek
    # (reasons in-content) often gives only analysis, or a marker over a stub / extern
    # hack; recover_with_closer fires ONE closer turn to converge it -- the thinking is
    # already in context, so playing to its grain beats fighting the ramble.
    candidate_code = recover_with_closer(llm, cid, addr, reply, args.out_dir, "reply2")
    if candidate_code is None:
        print("  WARNING: no extractable block anywhere -- emitting raw reply for inspection")
        candidate_code = reply.strip()

    candidate = args.out_dir / f"{addr:08x}.cpp"
    candidate.write_text(candidate_code + "\n", encoding="utf-8")
    print(f"  candidate -> {candidate} ({len(candidate_code)} chars)")

    report: dict = {"address": args.address, "rounds": [], "candidate": str(candidate)}

    build_verdict = None
    if args.gate in ("build", "both"):
        for r in range(1, rounds + 1):
            print(f"[3/4] build+smoke gate (round {r}/{rounds})...")
            build_verdict, out = gate_build(args.address, candidate)
            report["rounds"].append({"round": r, "build": build_verdict})
            print(f"  {build_verdict}")
            if build_verdict == "PASS":
                break
            if build_verdict == "BUILD_FAIL" and r < rounds:
                err = "\n".join(out.strip().splitlines()[-25:])
                reply = llm.resume(cid, f"That did not compile. Fix it and return ONLY the "
                                        f"corrected function in one ```cpp block whose first "
                                        f"line is the `// 0x{addr:X}` marker -- real body, no "
                                        f"invented extern/forward decls.\n\nBuild output:\n{err}")
                fixed = recover_with_closer(llm, cid, addr, reply, args.out_dir, f"fix{r}")
                if fixed is None:
                    print("  fix round produced no usable block; keeping previous candidate")
                    break
                candidate_code = fixed
                candidate.write_text(candidate_code + "\n", encoding="utf-8")
            else:
                break

    asm_verdict = None
    if args.gate in ("asm", "both"):
        print("[4/4] asm callee set-diff (advisory)...")
        asm = gate_asm(args.address, candidate)
        asm_verdict = asm.get("verdict")
        report["asm"] = asm
        print(f"  {asm_verdict}  missing={asm.get('missing')}")

    report["build_verdict"] = build_verdict
    report["asm_verdict"] = asm_verdict
    report_path = args.out_dir / f"{addr:08x}.report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    ok = (build_verdict in (None, "PASS"))
    print(f"\n{'READY for Claude review' if ok else 'NEEDS WORK'}: "
          f"build={build_verdict} asm={asm_verdict}  (report: {report_path})")
    print("Candidate is in reports/, NOT src/. Claude/human signs off before it lands.")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
