#!/usr/bin/env python3
"""
SM5 Release Linter — Check #1: CMake sources vs. filesystem drift.

Detects:
  - ORPHAN:  .cpp file exists in src/ (or tools/) but is NOT referenced in CMakeLists.txt
             → Will silently not be compiled; symbols will be missing at link time.
  - PHANTOM: .cpp file referenced in CMakeLists.txt but does NOT exist on disk
             → CMake configure will fail.

Usage:
    python tools/release_linter/check_sources.py
    python tools/release_linter/check_sources.py --json report.json
    python tools/release_linter/check_sources.py --graph graph.dot

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
CMAKE_FILE = REPO_ROOT / "CMakeLists.txt"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_orphans.txt"

# Directories that should be fully tracked by CMake
TRACKED_DIRS = ["src", "tools/license_cleaner"]

# Subpaths to ignore (generated, vendored, tests, etc.)
IGNORE_SUBSTRINGS = [
    "/build/",
    "/JUCE/",
    "/third_party/",
    "/python_bridge/",
    "/.git/",
    "_autogen",
]


def parse_cmake_sources(cmake_path: Path) -> set[str]:
    """
    Extract every .cpp path referenced in CMakeLists.txt.

    Strategy: after stripping comments, match any token that looks like a
    relative path ending in .cpp. This catches both `set(SM5_SOURCES ...)`
    variables and direct `target_sources(...)` entries without needing a
    real CMake parser.
    """
    if not cmake_path.exists():
        print(f"ERROR: CMakeLists.txt not found at {cmake_path}", file=sys.stderr)
        sys.exit(2)

    text = cmake_path.read_text(encoding="utf-8", errors="replace")
    # Strip line comments
    text = re.sub(r"#[^\n]*", "", text)
    # Find all .cpp paths (relative, with forward slashes)
    matches = re.findall(r"[\w./\-]+\.cpp", text)
    # Normalize
    return {m.replace("\\", "/").lstrip("./") for m in matches}


def glob_disk_sources(root: Path) -> set[str]:
    """Find every .cpp file on disk in TRACKED_DIRS, normalized to repo-relative."""
    found: set[str] = set()
    for d in TRACKED_DIRS:
        base = root / d
        if not base.exists():
            continue
        for cpp in base.rglob("*.cpp"):
            rel = cpp.relative_to(root).as_posix()
            if any(ig in f"/{rel}" for ig in IGNORE_SUBSTRINGS):
                continue
            found.add(rel)
    return found


def load_allowlist(path: Path) -> set[str]:
    """Load intentional orphans (one path per line, # comments allowed)."""
    if not path.exists():
        return set()
    allow: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            allow.add(line.replace("\\", "/"))
    return allow


def analyze() -> dict:
    cmake_refs = parse_cmake_sources(CMAKE_FILE)
    disk_files = glob_disk_sources(REPO_ROOT)
    allowlist = load_allowlist(ALLOWLIST_FILE)

    # Keep only cmake refs that look like tracked-dir paths for a fair diff
    cmake_tracked = {
        p for p in cmake_refs if any(p.startswith(d + "/") for d in TRACKED_DIRS)
    }

    raw_orphans = disk_files - cmake_tracked
    allowed_orphans = sorted(raw_orphans & allowlist)
    orphans = sorted(raw_orphans - allowlist)
    phantoms = sorted(cmake_tracked - disk_files)

    return {
        "cmake_refs_total": len(cmake_refs),
        "cmake_refs_tracked": len(cmake_tracked),
        "disk_files_total": len(disk_files),
        "orphans": orphans,                 # flagged: on disk, not in CMake, not allowlisted
        "allowed_orphans": allowed_orphans, # silenced by allowlist
        "phantoms": phantoms,               # in CMake, not on disk → configure fail
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Source/CMake Drift Check")
    print("=" * 70)
    print(f"CMake references (total):   {report['cmake_refs_total']}")
    print(f"CMake references (tracked): {report['cmake_refs_tracked']}")
    print(f"Disk files (tracked dirs):  {report['disk_files_total']}")
    print()

    orphans = report["orphans"]
    phantoms = report["phantoms"]

    if orphans:
        print(f"[ORPHANS] {len(orphans)} .cpp file(s) on disk NOT in CMakeLists.txt:")
        print("  (these will NOT be compiled — symbols will be missing)")
        for p in orphans:
            print(f"  - {p}")
        print()

    if phantoms:
        print(f"[PHANTOMS] {len(phantoms)} .cpp file(s) in CMakeLists.txt but NOT on disk:")
        print("  (CMake configure will fail)")
        for p in phantoms:
            print(f"  - {p}")
        print()

    if report["allowed_orphans"]:
        print(f"[ALLOWED] {len(report['allowed_orphans'])} orphan(s) silenced via intentional_orphans.txt:")
        for p in report["allowed_orphans"]:
            print(f"  - {p}")
        print()

    if not orphans and not phantoms:
        print("OK: CMakeLists.txt and filesystem are in sync.")
    print("=" * 70)


def write_dot(report: dict, out: Path) -> None:
    """Emit an Obsidian-style DOT graph: green = synced, red = drift."""
    lines = [
        "digraph sm5_sources {",
        '  rankdir=LR;',
        '  node [shape=box, style=filled, fontname="Helvetica"];',
        '  graph [label="SM5 Source/CMake Sync", labelloc=t, fontsize=18];',
        '  cmake [label="CMakeLists.txt", fillcolor="#4a90e2", fontcolor=white];',
    ]
    for p in report["orphans"]:
        node = re.sub(r"[^\w]", "_", p)
        lines.append(f'  {node} [label="{p}\\n(ORPHAN)", fillcolor="#ff6b6b", fontcolor=white];')
        lines.append(f'  cmake -> {node} [color="#ff6b6b", style=dashed, label="missing"];')
    for p in report["phantoms"]:
        node = re.sub(r"[^\w]", "_", p)
        lines.append(f'  {node} [label="{p}\\n(PHANTOM)", fillcolor="#ffa500", fontcolor=white];')
        lines.append(f'  cmake -> {node} [color="#ffa500", label="dangling"];')
    lines.append("}")
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote DOT graph: {out}")


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 source/CMake drift linter")
    ap.add_argument("--json", type=Path, help="Write JSON report to this path")
    ap.add_argument("--graph", type=Path, help="Write Graphviz DOT to this path")
    ap.add_argument("--quiet", action="store_true", help="Suppress stdout report")
    args = ap.parse_args()

    report = analyze()

    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON report: {args.json}")
    if args.graph:
        write_dot(report, args.graph)

    return 1 if (report["orphans"] or report["phantoms"]) else 0


if __name__ == "__main__":
    sys.exit(main())
