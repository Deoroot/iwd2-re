#!/usr/bin/env python3
"""Changed-line checks for reverse-engineered source hygiene.

This is intentionally not a C++ style linter. It only looks for patterns that
commonly create IntelliSense/build drift or make Ghidra/source sync harder.

Default mode checks added lines in the current staged and unstaged src/ diff.
Pass explicit paths to scan whole files, or --all for a repository audit.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}


@dataclass(frozen=True)
class Issue:
    path: Path
    line_no: int
    code: str
    message: str
    line: str


@dataclass(frozen=True)
class PatternRule:
    code: str
    regex: re.Pattern[str]
    message: str


RULES = [
    PatternRule(
        "RE001",
        re.compile(r"\.\s*Format\s*\(\s*\""),
        "CString::Format uses a narrow literal; prefer _T(\"...\") or a CString/LPCTSTR conversion.",
    ),
    PatternRule(
        "RE002",
        re.compile(r"\bCString\b\s*(?:[A-Za-z_]\w*\s*)?\(\s*\""),
        "CString constructed from a narrow literal; prefer _T(\"...\") when editing this code.",
    ),
    PatternRule(
        "RE003",
        re.compile(r"\bLPCSTR\b|\(\s*LPCSTR\s*\)|static_cast\s*<\s*LPCSTR\s*>"),
        "LPCSTR in CString-facing code is usually an IntelliSense/Unicode trap; prefer LPCTSTR unless Ghidra requires bytes.",
    ),
    PatternRule(
        "RE008",
        re.compile(
            r"\b(?:fopen|freopen|FindFirstFileA|CreateFileA|GetPrivateProfile(?:Int|String)A|WritePrivateProfileStringA)"
            r"\s*\([^;\n]*(?:\bCString\b|\bGetDir[A-Za-z0-9_]*\s*\(|\bs[A-Z]\w*\b|\bm_s[A-Za-z0-9_]*\b)"
        ),
        "narrow CRT/Win32 API receives a CString-like expression; prefer a TCHAR wrapper or an explicit byte conversion if the A API is intentional.",
    ),
    PatternRule(
        "RE004",
        re.compile(r"\bsub_[0-9A-Fa-f]{4,}\b"),
        "new sub_ placeholder in touched code; rename in Ghidra first when the function is understood.",
    ),
    PatternRule(
        "RE005",
        re.compile(r"TODO:\s*Incomplete"),
        "new incomplete stub marker; keep stubs isolated and deliberate.",
    ),
]

STRICT_PLACEHOLDER_RULE = PatternRule(
    "RE007",
    re.compile(r"\bfield_[0-9A-Fa-f]{2,}\b"),
    "new field_ placeholder in touched code; confirm this is intentional and not a missed PDB/Ghidra name.",
)

FUNCTION_DEF_RE = re.compile(
    r"^\s*(?!//)(?!.*;)(?:[\w:<>,~*&\s]+\s+)?[A-Za-z_]\w*::[~A-Za-z_]\w*\s*\([^;]*\)\s*(?:const)?\s*$"
)
FUNCTION_MARKER_RE = re.compile(
    r"^\s*//\s*(?:0x[0-9A-Fa-f]+(?:\s+\(virtual\))?|NOTE:\s*(?:Inlined|Probably inlined|Uninline|Convenience)\b.*)\s*$"
)


def run_git(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def is_source_path(path: Path) -> bool:
    return path.suffix.lower() in SOURCE_EXTENSIONS


def normalize_path(path: str) -> Path:
    return Path(path.replace("/", os.sep))


def parse_diff(diff_text: str) -> dict[Path, set[int]]:
    changed: dict[Path, set[int]] = {}
    current_path: Path | None = None
    new_line_no: int | None = None

    for raw_line in diff_text.splitlines():
        if raw_line.startswith("+++ "):
            marker_path = raw_line[4:]
            if marker_path == "/dev/null":
                current_path = None
                continue
            if marker_path.startswith("b/"):
                marker_path = marker_path[2:]
            path = normalize_path(marker_path)
            current_path = path if is_source_path(path) and path.parts[:1] == ("src",) else None
            continue

        if current_path is None:
            continue

        if raw_line.startswith("@@ "):
            match = re.search(r"\+(\d+)(?:,\d+)?", raw_line)
            new_line_no = int(match.group(1)) if match else None
            continue

        if new_line_no is None:
            continue

        if raw_line.startswith("+") and not raw_line.startswith("+++"):
            changed.setdefault(current_path, set()).add(new_line_no)
            new_line_no += 1
        elif raw_line.startswith(" "):
            new_line_no += 1
        elif raw_line.startswith("-"):
            pass

    return changed


def merge_line_maps(target: dict[Path, set[int]], source: dict[Path, set[int]]) -> None:
    for path, line_numbers in source.items():
        target.setdefault(path, set()).update(line_numbers)


def changed_source_lines(include_staged: bool, include_worktree: bool) -> dict[Path, set[int]]:
    changed: dict[Path, set[int]] = {}

    if include_staged:
        result = run_git(["diff", "--cached", "--unified=0", "--no-ext-diff", "--", "src"])
        merge_line_maps(changed, parse_diff(result.stdout))

    if include_worktree:
        result = run_git(["diff", "--unified=0", "--no-ext-diff", "--", "src"])
        merge_line_maps(changed, parse_diff(result.stdout))

    return changed


def tracked_source_files() -> list[Path]:
    result = run_git(["ls-files", "src"])
    if result.returncode != 0:
        return []
    return [
        normalize_path(line)
        for line in result.stdout.splitlines()
        if line and is_source_path(normalize_path(line))
    ]


def collect_explicit_files(paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for raw_path in paths:
        path = Path(raw_path)
        if path.is_dir():
            for child in path.rglob("*"):
                if child.is_file() and is_source_path(child):
                    files.append(child)
        elif path.is_file() and is_source_path(path):
            files.append(path)
    return sorted(set(files))


def disabled(line: str, code: str) -> bool:
    marker = "re-lint: disable"
    if marker not in line:
        return False
    return code in line or "all" in line or line.strip().endswith(marker)


def previous_nonblank_line(lines: list[str], line_no: int) -> str:
    index = line_no - 2
    while index >= 0:
        candidate = lines[index].strip()
        if candidate:
            return lines[index]
        index -= 1
    return ""


def next_nonblank_line(lines: list[str], line_no: int) -> str:
    index = line_no
    while index < len(lines):
        candidate = lines[index].strip()
        if candidate:
            return lines[index]
        index += 1
    return ""


def scan_file(path: Path, line_numbers: set[int], rules: list[PatternRule]) -> list[Issue]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        return [Issue(path, 0, "RE000", f"could not read file: {exc}", "")]

    issues: list[Issue] = []

    for line_no in sorted(line_numbers):
        if line_no < 1 or line_no > len(lines):
            continue

        line = lines[line_no - 1]
        for rule in rules:
            if rule.regex.search(line) and not disabled(line, rule.code):
                issues.append(Issue(path, line_no, rule.code, rule.message, line))

        if FUNCTION_DEF_RE.match(line) and not disabled(line, "RE006"):
            next_line = next_nonblank_line(lines, line_no).lstrip()
            if next_line != "{" and not next_line.startswith(":"):
                continue

            previous = previous_nonblank_line(lines, line_no)
            if not FUNCTION_MARKER_RE.match(previous):
                issues.append(
                    Issue(
                        path,
                        line_no,
                        "RE006",
                        "function definition is missing the address comment immediately above it.",
                        line,
                    )
                )

    return issues


def make_full_file_line_map(paths: list[Path]) -> dict[Path, set[int]]:
    line_map: dict[Path, set[int]] = {}
    for path in paths:
        try:
            count = len(path.read_text(encoding="utf-8", errors="replace").splitlines())
        except OSError:
            count = 0
        line_map[path] = set(range(1, count + 1))
    return line_map


def print_issues(issues: list[Issue]) -> None:
    for issue in issues:
        location = f"{issue.path}:{issue.line_no}" if issue.line_no else str(issue.path)
        print(f"{location}: {issue.code}: {issue.message}")
        if issue.line:
            print(f"    {issue.line.strip()}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Small RE-safe lint checks for changed source lines.")
    parser.add_argument("paths", nargs="*", help="Optional source files or directories to scan fully.")
    parser.add_argument("--all", action="store_true", help="Scan all tracked source files.")
    parser.add_argument("--staged", action="store_true", help="Scan staged diff only.")
    parser.add_argument("--worktree", action="store_true", help="Scan unstaged diff only.")
    parser.add_argument(
        "--strict-placeholders",
        action="store_true",
        help="Also flag newly touched field_ placeholders.",
    )
    args = parser.parse_args(argv)

    rules = list(RULES)
    if args.strict_placeholders:
        rules.append(STRICT_PLACEHOLDER_RULE)

    if args.paths:
        line_map = make_full_file_line_map(collect_explicit_files(args.paths))
    elif args.all:
        line_map = make_full_file_line_map(tracked_source_files())
    else:
        include_staged = args.staged or not args.worktree
        include_worktree = args.worktree or not args.staged
        line_map = changed_source_lines(include_staged, include_worktree)

    if not line_map:
        print("re-lint: no source lines to check")
        return 0

    issues: list[Issue] = []
    for path, line_numbers in sorted(line_map.items(), key=lambda item: str(item[0]).lower()):
        issues.extend(scan_file(path, line_numbers, rules))

    if issues:
        print_issues(issues)
        return 1

    print("re-lint: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
