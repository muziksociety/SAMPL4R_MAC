#!/usr/bin/env python3
"""
SM5 Release Linter — Check #3: Installer manifest sync.

Detects runtime resources referenced in C++ source but not shipped by
installer_sm5.iss. Prevents "works on dev machine, crashes in installer"
bugs where a new resource directory or external binary was added to code
but never wired into the Inno Setup script.

Scope (v1, conservative):
  - Scans src/**/*.cpp and src/**/*.h for string literals of the form
      "<dirname>/..."  -> top-level directory references
      "<name>.dll" / "<name>.exe"  -> external binary references
  - Parses installer_sm5.iss for shipped top-level directories and
    individual file basenames (from [Files] Source: lines).
  - Reports code-referenced resources missing from the installer.

Usage:
    python tools/release_linter/check_installer.py
    python tools/release_linter/check_installer.py --json report.json

Exit codes:
    0 = clean
    1 = drift detected
    2 = tool error
"""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
INSTALLER_FILE = REPO_ROOT / "installer_sm5.iss"
CODE_ROOT = REPO_ROOT / "src"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_unshipped_resources.txt"

# Tokens that look like directories in string literals but are NEVER runtime
# resources — they are source paths, URLs, log prefixes, etc.
DIR_DENYLIST = {
    # Source / build dirs
    "src", "tools", "build", "JUCE", "third_party", "docs", "tests",
    "Sample_monsta5", "SampleMonsta4", "SampleMonsta5", "sample_monsta4",
    # Source code subdirectories (used in #include paths or log tags)
    "Pages", "Components", "Debug", "Style", "WaveEditor", "sm4port",
    "smode", "analysis", "variation", "services", "licensing", "ml",
    "application", "Variation", "Preview", "Theme", "Games", "genome",
    "engines", "Separation", "ui", "app", "audio", "networking",
    "security", "utils", "plugin", "Licensing",
    # URL / protocol prefixes
    "http:", "https:", "file:", "ftp:", "ws:", "wss:",
    # Drive letters
    "C:", "D:", "E:", "F:", "B:", "tmp", "temp",
    # OS path fragments
    "Users", "Program", "AppData", "Documents", "Desktop",
    "var", "etc", "usr", "lib", "bin", "opt",
    "dev", "proc", "sys",
    # Reversed-DNS fragments
    "com", "org", "net", "io",
    # Common namespace tokens
    "juce", "std", "sm5", "nlohmann",
    # Drum-kit / category log tags used as "Conga/Snare" style strings
    "Kick", "Snare", "HiHat", "Hat", "Tom", "Cymbal", "Cymbals",
    "Conga", "Bongo", "Clap", "Rim", "Ride", "Crash", "Perc",
    "Impact", "Pitch", "Play", "Mute", "Show", "Stop", "Start",
    "Arrows", "HTTP", "Set", "Get", "On", "Off",
    # Log format tokens
    "r", "n", "t", "0", "1", "2", "s", "d", "x",
}

# System-provided binaries — never shipped by SM5 installer
SYSTEM_BINARIES = {
    "cmd.exe", "explorer.exe", "notepad.exe", "powershell.exe",
    "nvcuda.dll", "vcruntime140.dll", "vcruntime140_1.dll",
    "msvcp140.dll", "kernel32.dll", "user32.dll", "advapi32.dll",
    "shell32.dll", "ole32.dll", "oleaut32.dll",
}

# File extensions we consider "shippable binaries"
BINARY_EXTS = (".dll", ".exe", ".pt", ".onnx")

# Directory reference: "token/..."
DIR_REF_RE = re.compile(r'"([A-Za-z_][A-Za-z0-9_]{1,40})/')
# Binary reference with optional leading dir(s):
#   "name.ext"  |  "models/foo.pt"  |  "python/python.exe"
BIN_REF_RE = re.compile(
    r'"((?:[A-Za-z0-9_.\-]+/)*)([A-Za-z0-9_.\-]+\.(?:dll|exe|pt|onnx))"'
)

# Installer parser
INSTALLER_DIR_RE = re.compile(
    r'Source:\s*"[^"]*?\\([A-Za-z0-9_.\-]+)\\\*"', re.IGNORECASE
)
INSTALLER_FILE_RE = re.compile(
    r'Source:\s*"[^"]*?\\([A-Za-z0-9_.\-]+\.(?:dll|exe|pt|onnx))"',
    re.IGNORECASE,
)


def load_allowlist() -> set[str]:
    if not ALLOWLIST_FILE.exists():
        return set()
    out: set[str] = set()
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


def scan_code() -> tuple[set[str], list[tuple[str, str]]]:
    """
    Return:
      - set of referenced top-level dirs
      - list of (top_level_dir_or_empty, binary_basename_lower) tuples
    """
    dirs: set[str] = set()
    bins: list[tuple[str, str]] = []
    if not CODE_ROOT.exists():
        print(f"ERROR: code root not found: {CODE_ROOT}", file=sys.stderr)
        sys.exit(2)
    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in DIR_REF_RE.finditer(text):
            tok = m.group(1)
            if tok not in DIR_DENYLIST and not tok.startswith("_"):
                dirs.add(tok)
        for m in BIN_REF_RE.finditer(text):
            prefix = m.group(1)
            basename = m.group(2).lower()
            top = prefix.split("/", 1)[0] if prefix else ""
            bins.append((top, basename))
    return dirs, bins


def parse_installer() -> tuple[set[str], set[str]]:
    """Return (shipped top-level dirs, shipped file basenames)."""
    if not INSTALLER_FILE.exists():
        print(f"ERROR: installer not found: {INSTALLER_FILE}", file=sys.stderr)
        sys.exit(2)
    text = INSTALLER_FILE.read_text(encoding="utf-8", errors="replace")
    dirs = {m.group(1) for m in INSTALLER_DIR_RE.finditer(text)}
    files = {m.group(1).lower() for m in INSTALLER_FILE_RE.finditer(text)}
    return dirs, files


def analyze() -> dict:
    code_dirs, code_bins = scan_code()
    shipped_dirs, shipped_files = parse_installer()
    allow = load_allowlist()

    shipped_dirs_lc = {d.lower() for d in shipped_dirs}
    missing_dirs = sorted(
        d for d in code_dirs
        if d.lower() not in shipped_dirs_lc and d not in allow
    )

    # A binary is considered shipped if:
    #   - its basename is in SYSTEM_BINARIES (never ship), OR
    #   - its basename is explicitly in the installer [Files], OR
    #   - its leading directory is shipped via wildcard (e.g. models\*)
    missing_bin_set: set[str] = set()
    for top, basename in code_bins:
        if basename in SYSTEM_BINARIES or basename in allow:
            continue
        if basename in shipped_files:
            continue
        if top and top.lower() in shipped_dirs_lc:
            continue
        missing_bin_set.add(basename)
    missing_bins = sorted(missing_bin_set)

    return {
        "code_dirs_total": len(code_dirs),
        "code_bins_total": len(code_bins),
        "shipped_dirs": sorted(shipped_dirs),
        "shipped_files_total": len(shipped_files),
        "missing_dirs": missing_dirs,
        "missing_bins": missing_bins,
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Installer Manifest Sync")
    print("=" * 70)
    print(f"Code-referenced dirs:    {report['code_dirs_total']}")
    print(f"Code-referenced binaries:{report['code_bins_total']}")
    print(f"Installer shipped dirs:  {', '.join(report['shipped_dirs']) or '(none)'}")
    print(f"Installer shipped files: {report['shipped_files_total']}")
    print()

    if report["missing_dirs"]:
        print(f"[MISSING DIRS] {len(report['missing_dirs'])} dir(s) referenced in code but not shipped:")
        for d in report["missing_dirs"]:
            print(f"  - {d}/")
        print()

    if report["missing_bins"]:
        print(f"[MISSING BINARIES] {len(report['missing_bins'])} binary(ies) referenced in code but not shipped:")
        for b in report["missing_bins"]:
            print(f"  - {b}")
        print()

    if not report["missing_dirs"] and not report["missing_bins"]:
        print("OK: installer ships every referenced runtime resource.")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 installer manifest linter")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    report = analyze()
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON: {args.json}")

    return 1 if (report["missing_dirs"] or report["missing_bins"]) else 0


if __name__ == "__main__":
    sys.exit(main())
