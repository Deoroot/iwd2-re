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


def run(cmd: list[str]) -> tuple[int, str]:
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       encoding="utf-8", errors="replace")
    return p.returncode, p.stdout


def assemble(address: str) -> str:
    rc, out = run([PYTHON, str(SCRIPTS / "reagent_assemble_context.py"), "--address", address])
    if rc != 0:
        raise SystemExit(f"assemble failed:\n{out}")
    return out


def extract_code(reply: str, address: int) -> str:
    """Pull the function body out of the model reply (fenced block or marker span)."""
    blocks = CODE_FENCE_RE.findall(reply)
    if blocks:
        # Prefer a block that carries the address marker, else the longest.
        for b in blocks:
            if re.search(r"//[ \t]*0x0*%X\b" % address, b, re.IGNORECASE):
                return b.strip()
        return max(blocks, key=len).strip()
    return reply.strip()


def make_llm():
    from re_agent.config.loader import load_config
    from re_agent.llm.openai_compat import OpenAIProvider
    cfg = load_config(str(CONFIG))
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
        "buildable C++ that matches the existing src/ exactly. Output only code.")
    print("[2/4] deepseek first pass...")
    reply = llm.resume(cid, bundle)
    candidate_code = extract_code(reply, addr)

    args.out_dir.mkdir(parents=True, exist_ok=True)
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
                reply = llm.resume(cid, f"That did not compile. Fix it; return only the "
                                        f"corrected function.\n\nBuild output:\n{err}")
                candidate_code = extract_code(reply, addr)
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
