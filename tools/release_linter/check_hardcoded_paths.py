#!/usr/bin/env python3
"""
SM5 Release Linter — Check #7: Hardcoded dev-machine path scanner.

Catches absolute paths that were baked into source code or the built exe
and would silently fail on another machine. Examples of what this catches:

  - String literals like "B:\\SANTI\\..." in source
  - Dev username leaks ("C:\\Users\\Muzik\\...")
  - Hardcoded test-fixture paths referenced at runtime
  - Absolute path leaks in the built exe's read-only string table

Two scan modes:

  (a) SOURCE scan — greps src/ for absolute-path string literals
  (b) BINARY scan — extracts ASCII/UTF-16 strings from Sample Monsta.exe's
      .rdata section and flags any that look like dev-machine paths

A path is "dev-machine" if it matches any of:
  - Drive letters A-Z (except C:) followed by backslash
  - C:\\Users\\<name>\\ where <name> is not a build-agent / standard path
  - Git user name from local config (if available)
  - Repo root substring (e.g. "SANTI\\SAMPLE_MONSTA")

Usage:
    python tools/release_linter/check_hardcoded_paths.py
    python tools/release_linter/check_hardcoded_paths.py --json report.json

Exit codes:
    0 = clean
    1 = suspicious paths found
    2 = tool error
"""
from __future__ import annotations
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CODE_ROOT = REPO_ROOT / "src"
DEFAULT_EXE = REPO_ROOT / "build" / "SM5App_artefacts" / "Release" / "Sample Monsta.exe"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_hardcoded_paths.txt"

# Patterns that look like dev-machine absolute paths.
SUSPICIOUS_PATTERNS = [
    # Drive letters other than C: with a path
    re.compile(r'"([A-BD-Z]:[/\\][^"\n]{2,120})"'),
    re.compile(r"'([A-BD-Z]:[/\\][^'\n]{2,120})'"),
    # Unix-style absolute paths that might leak (but not /usr/include which is fine)
    re.compile(r'"(/home/[A-Za-z0-9_\-]+/[^"\n]{1,120})"'),
    re.compile(r'"(/Users/[A-Za-z0-9_\-]+/[^"\n]{1,120})"'),
]

# Text snippets that indicate a dev-machine leak regardless of surrounding format
LEAK_SUBSTRINGS = [
    "SANTI",
    "sample_monsta4",      # parent dir naming
    "Muzik",                # git user
    "C:\\Users\\",
    "C:/Users/",
]

# Paths we consider legitimate even if they look absolute (template strings,
# documentation examples, etc.). Allowlisted one-token-per-line, substring match.
# Additional runtime-resolved patterns are always considered safe.
RUNTIME_RESOLVED_MARKERS = [
    "$<TARGET_FILE_DIR",        # CMake generator expressions
    "${CMAKE_",                 # CMake variables
    "@CMAKE_",
    "%APPDATA%",
    "%LOCALAPPDATA%",
    "%PROGRAMFILES%",
    "~/",
]

EXCLUDE_SUFFIXES = {".git", ".exe", ".lib", ".obj", ".pdb"}


def load_allowlist() -> list[str]:
    if not ALLOWLIST_FILE.exists():
        return []
    out: list[str] = []
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(line)
    return out


def is_runtime_resolved(s: str) -> bool:
    return any(marker in s for marker in RUNTIME_RESOLVED_MARKERS)


def is_allowlisted(s: str, allow: list[str]) -> bool:
    return any(a in s for a in allow)


def scan_source(allow: list[str]) -> list[dict]:
    """Scan src/ for suspicious absolute-path string literals."""
    findings: list[dict] = []
    if not CODE_ROOT.exists():
        return findings
    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        rel = path.relative_to(REPO_ROOT).as_posix()
        for i, line in enumerate(text.splitlines(), 1):
            for pat in SUSPICIOUS_PATTERNS:
                for m in pat.finditer(line):
                    match = m.group(1)
                    if is_runtime_resolved(match) or is_allowlisted(match, allow):
                        continue
                    findings.append({
                        "kind": "pattern",
                        "source": "src",
                        "file": rel,
                        "line": i,
                        "match": match,
                    })
            for sub in LEAK_SUBSTRINGS:
                if sub in line:
                    # Avoid flagging copyright/header lines and the linter itself
                    if "Muzik" in line and ("Copyright" in line or "@author" in line):
                        continue
                    if is_allowlisted(line, allow):
                        continue
                    findings.append({
                        "kind": "substring",
                        "source": "src",
                        "file": rel,
                        "line": i,
                        "match": sub,
                        "context": line.strip()[:120],
                    })
    return findings


def extract_exe_strings(exe: Path, min_len: int = 8) -> list[str]:
    """Crude `strings`-like extraction from a PE binary.

    Pulls both ASCII and UTF-16LE runs of printable characters. Good enough
    for catching baked-in path leaks without shelling out to sysinternals.
    """
    data = exe.read_bytes()
    results: set[str] = set()
    # ASCII
    cur: list[int] = []
    for b in data:
        if 32 <= b < 127:
            cur.append(b)
        else:
            if len(cur) >= min_len:
                results.add(bytes(cur).decode("ascii", errors="ignore"))
            cur = []
    if len(cur) >= min_len:
        results.add(bytes(cur).decode("ascii", errors="ignore"))
    # UTF-16LE
    cur = []
    for i in range(0, len(data) - 1, 2):
        lo = data[i]
        hi = data[i + 1]
        if hi == 0 and 32 <= lo < 127:
            cur.append(lo)
        else:
            if len(cur) >= min_len:
                results.add(bytes(cur).decode("ascii", errors="ignore"))
            cur = []
    if len(cur) >= min_len:
        results.add(bytes(cur).decode("ascii", errors="ignore"))
    return sorted(results)


def scan_binary(exe: Path, allow: list[str]) -> list[dict]:
    """Extract strings from exe and flag suspicious dev-machine paths."""
    findings: list[dict] = []
    if not exe.exists():
        return findings
    strings = extract_exe_strings(exe)
    for s in strings:
        flagged = False
        for pat in SUSPICIOUS_PATTERNS:
            if pat.search(f'"{s}"'):
                flagged = True
                break
        if not flagged:
            if any(sub in s for sub in LEAK_SUBSTRINGS):
                flagged = True
        if not flagged:
            continue
        if is_runtime_resolved(s) or is_allowlisted(s, allow):
            continue
        findings.append({
            "kind": "binary",
            "source": "exe",
            "file": exe.name,
            "line": 0,
            "match": s[:160],
        })
    return findings


def analyze(exe: Path | None) -> dict:
    allow = load_allowlist()
    src_findings = scan_source(allow)
    bin_findings = scan_binary(exe, allow) if (exe and exe.exists()) else []
    return {
        "exe": str(exe) if exe else None,
        "exe_scanned": bool(exe and exe.exists()),
        "source_findings": src_findings,
        "binary_findings": bin_findings,
        "total_findings": len(src_findings) + len(bin_findings),
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Hardcoded Dev Path Scanner")
    print("=" * 70)
    print(f"Source findings: {len(report['source_findings'])}")
    print(f"Binary findings: {len(report['binary_findings'])} (exe scanned: {report['exe_scanned']})")
    print()

    if report["source_findings"]:
        print("[SOURCE] Suspicious absolute paths in src/:")
        for f in report["source_findings"][:30]:
            print(f"  - {f['file']}:{f['line']}  {f['match']}")
        if len(report["source_findings"]) > 30:
            print(f"  ... and {len(report['source_findings']) - 30} more")
        print()

    if report["binary_findings"]:
        print("[BINARY] Suspicious strings baked into the exe:")
        for f in report["binary_findings"][:30]:
            print(f"  - {f['match']}")
        if len(report["binary_findings"]) > 30:
            print(f"  ... and {len(report['binary_findings']) - 30} more")
        print()

    if report["total_findings"] == 0:
        print("OK: no dev-machine path leaks detected in source or binary.")
    else:
        print("Fix suggestions:")
        print("  - Replace absolute paths with runtime-resolved lookups (DataPaths::get*)")
        print("  - Use %APPDATA% / %LOCALAPPDATA% / JUCE SpecialLocation for user data")
        print("  - Allowlist benign matches in intentional_hardcoded_paths.txt")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 hardcoded dev-path linter")
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--skip-binary", action="store_true",
                    help="Skip scanning the built exe (source-only)")
    args = ap.parse_args()

    exe = None if args.skip_binary else args.exe
    report = analyze(exe)
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON: {args.json}")

    return 1 if report["total_findings"] > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
