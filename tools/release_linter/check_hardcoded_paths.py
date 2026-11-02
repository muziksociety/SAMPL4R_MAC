#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #7: hardcoded dev-machine path scanner.

  (a) SOURCE: string literals in src/ that are absolute paths on a non-C: drive,
      /home|/Users paths, or contain dev-machine markers (SANTI, VST PROJECT,
      sample_monsta4, the dev user name, C:\\Users\\...).
  (b) BINARY: ASCII + UTF-16LE strings of the shipped SAMPLAR.vst3 binary
      (e.g. an absolute PDB path baked in by /DEBUG, __FILE__ paths).

intentional_hardcoded_paths.txt entries are SCRUBBED (case-insensitive) from a
candidate string before it is judged - e.g. "MuzikSociety" (company name in the
HKCU\\Software\\MuzikSociety\\Samplar registry key and the VST3 vendor string)
no longer trips the "Muzik" user-name marker, while "C:\\Users\\Muzik\\..." in
the same string still would.

Exit codes: 0 clean, 1 findings, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from linter_common import REPO_ROOT, bundle_binary, common_args, default_bundle

CODE_ROOT = REPO_ROOT / "src"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_hardcoded_paths.txt"

PATH_PATTERNS = [
    # drive + at least one real folder name (binary noise like "Z:\:^0`e" is not a path)
    re.compile(r'([A-BD-Z]:[/\\][A-Za-z0-9_][A-Za-z0-9_ .()\-]+[/\\][^"\'\n]{0,160})'),
    re.compile(r'(/home/[A-Za-z0-9_\-]+/[^"\'\n]{1,160})'),
    re.compile(r'(/Users/[A-Za-z0-9_\-]+/[^"\'\n]{1,160})'),
]
LEAK_MARKERS = [
    re.compile(r"SANTI"),
    re.compile(r"VST PROJECT", re.IGNORECASE),
    re.compile(r"sample_monsta4", re.IGNORECASE),
    re.compile(r"\bMuzik"),                      # dev user name (company name is scrubbed first)
    re.compile(r"C:[/\\]+Users[/\\]+(?!Public\b)", re.IGNORECASE),
]
STRING_LITERAL_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
RUNTIME_RESOLVED = ["$<TARGET_FILE_DIR", "${CMAKE_", "@CMAKE_", "%APPDATA%", "%LOCALAPPDATA%", "%PROGRAMFILES%"]


def load_scrub_list() -> list[str]:
    if not ALLOWLIST_FILE.exists():
        return []
    out = []
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8-sig").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(line)
    return out


def scrub(s: str, scrubs: list[str]) -> str:
    for a in scrubs:
        s = re.sub(re.escape(a), "", s, flags=re.IGNORECASE)
    return s


def suspicious(s: str, scrubs: list[str]) -> str | None:
    if any(m in s for m in RUNTIME_RESOLVED):
        return None
    t = scrub(s, scrubs)
    for p in PATH_PATTERNS:
        m = p.search(t)
        if m:
            return m.group(1)
    for p in LEAK_MARKERS:
        if p.search(t):
            return p.pattern
    return None


def scan_source(scrubs: list[str]) -> list[dict]:
    findings = []
    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc", ".in"):
            continue
        r = path.relative_to(REPO_ROOT).as_posix()
        for i, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if line.lstrip().startswith(("//", "*", "/*")):
                continue   # comments are not baked into the binary
            for lit in STRING_LITERAL_RE.findall(line):
                hit = suspicious(lit.replace("\\\\", "\\"), scrubs)
                if hit:
                    findings.append({"source": "src", "file": r, "line": i, "match": hit, "context": lit[:120]})
    return findings


def extract_strings(data: bytes, min_len: int = 8) -> set[str]:
    out = {m.group(0).decode("ascii", "ignore") for m in re.finditer(rb"[\x20-\x7e]{%d,}" % min_len, data)}
    out |= {m.group(0).decode("utf-16le", "ignore") for m in re.finditer(rb"(?:[\x20-\x7e]\x00){%d,}" % min_len, data)}
    return out


def scan_binary(binary: Path, scrubs: list[str]) -> list[dict]:
    if not binary.exists():
        return []
    findings = []
    for s in sorted(extract_strings(binary.read_bytes())):
        hit = suspicious(s, scrubs)
        if hit:
            findings.append({"source": "binary", "file": binary.name, "line": 0, "match": s[:200]})
    return findings


def analyze(binary: Path) -> dict:
    scrubs = load_scrub_list()
    src = scan_source(scrubs)
    bins = scan_binary(binary, scrubs)
    return {"binary": str(binary), "binary_scanned": binary.exists(), "allowlist": scrubs,
            "source_findings": src, "binary_findings": bins, "total_findings": len(src) + len(bins)}


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - Hardcoded Dev Path Scanner")
    print("=" * 70)
    print(f"Source findings: {len(r['source_findings'])}")
    print(f"Binary findings: {len(r['binary_findings'])} (scanned: {r['binary_scanned']} {r['binary']})")
    for f in r["source_findings"]:
        print(f"  - {f['file']}:{f['line']}  {f['context']}")
    for f in r["binary_findings"]:
        print(f"  - [{f['file']}] {f['match']}")
    if not r["total_findings"]:
        print("OK: no dev-machine paths in src/ or the shipped binary.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR hardcoded dev-path scanner"), bundle=True)
    ap.add_argument("--exe", type=Path, help="Binary to scan (default: <bundle>/Contents/x86_64-win/SAMPLAR.vst3)")
    args = ap.parse_args()
    binary = args.exe or bundle_binary(args.bundle or default_bundle())
    try:
        report = analyze(binary)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if report["total_findings"] else 0


if __name__ == "__main__":
    sys.exit(main())
