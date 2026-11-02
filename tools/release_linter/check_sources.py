#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #1: CMake sources vs. filesystem drift.

Reads CMakeLists.txt AND every cmake/*.cmake (per-workstream target_sources,
e.g. cmake/extra_analysis.cmake / extra_qa.cmake) with their set() variables
resolved (${BEAT_THIS_DIR}, ${CMAKE_CURRENT_LIST_DIR}/.., ${_SAMPLAR_ROOT}, ...).

  - ORPHAN:  a .cpp/.c under a tracked dir that no CMake file references
             -> silently not compiled (missing symbols / dead code).
  - PHANTOM: a referenced .cpp/.c that does not exist -> configure fails.

Tracked dirs: src/, tests/, the tracked tools (license_cleaner, vst3_smoke_host,
separation_probe) and the vendored sources CMake builds (third_party/beat_this/src,
third_party/pffft/src).

Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from linter_common import REPO_ROOT, cmake_files, cmake_source_refs, common_args, load_allowlist, rel

TRACKED_DIRS = [
    "src",
    "tests",
    "tools/license_cleaner",
    "tools/vst3_smoke_host",
    "tools/separation_probe",
    "third_party/beat_this/src",
    "third_party/pffft/src",
]
SOURCE_EXTS = {".cpp", ".c", ".cc", ".mm"}


def glob_disk_sources() -> set[str]:
    found: set[str] = set()
    for d in TRACKED_DIRS:
        base = REPO_ROOT / d
        if not base.is_dir():
            continue
        for f in base.rglob("*"):
            if f.suffix in SOURCE_EXTS and f.is_file():
                found.add(f.relative_to(REPO_ROOT).as_posix())
    return found


def analyze() -> dict:
    refs = cmake_source_refs()
    disk = glob_disk_sources()
    allow = load_allowlist("intentional_orphans.txt")

    tracked_refs = {p for p in refs if any(p.startswith(d + "/") for d in TRACKED_DIRS)}
    raw_orphans = disk - tracked_refs
    phantoms = sorted(p for p in refs if not (REPO_ROOT / p).exists() and not Path(p).exists())

    return {
        "cmake_files": [rel(f) for f in cmake_files()],
        "cmake_refs_total": len(refs),
        "cmake_refs_tracked": len(tracked_refs),
        "disk_files_total": len(disk),
        "orphans": sorted(raw_orphans - allow),
        "allowed_orphans": sorted(raw_orphans & allow),
        "stale_allowlist": sorted(a for a in allow if a not in raw_orphans),
        "phantoms": phantoms,
        "phantom_sources": {p: refs[p] for p in phantoms},
    }


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - Source/CMake Drift Check")
    print("=" * 70)
    print(f"CMake files parsed:          {', '.join(r['cmake_files'])}")
    print(f"Source refs (total/tracked): {r['cmake_refs_total']} / {r['cmake_refs_tracked']}")
    print(f"Source files on disk:        {r['disk_files_total']}")
    print()
    if r["orphans"]:
        print(f"[ORPHANS] {len(r['orphans'])} source file(s) on disk referenced by NO CMake file:")
        for p in r["orphans"]:
            print(f"  - {p}")
        print()
    if r["phantoms"]:
        print(f"[PHANTOMS] {len(r['phantoms'])} referenced source file(s) missing on disk:")
        for p in r["phantoms"]:
            print(f"  - {p}   (in {', '.join(r['phantom_sources'][p])})")
        print()
    if r["allowed_orphans"]:
        print(f"[ALLOWED] {len(r['allowed_orphans'])} orphan(s) in intentional_orphans.txt:")
        for p in r["allowed_orphans"]:
            print(f"  - {p}")
        print()
    if r["stale_allowlist"]:
        print(f"[INFO] {len(r['stale_allowlist'])} allowlist entr(y/ies) no longer orphaned/present (prune them):")
        for p in r["stale_allowlist"]:
            print(f"  - {p}")
        print()
    if not r["orphans"] and not r["phantoms"]:
        print("OK: CMake files and filesystem are in sync.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR source/CMake drift linter"))
    args = ap.parse_args()
    try:
        report = analyze()
    except Exception as e:  # tool error, not drift
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if (report["orphans"] or report["phantoms"]) else 0


if __name__ == "__main__":
    sys.exit(main())
