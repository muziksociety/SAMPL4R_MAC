#!/usr/bin/env python3
"""
SM5 Release Linter — Check #8: Staging directory sync.

The installer ships from SM5_Portable/, NOT from build/. If nobody copies
the build output into SM5_Portable/ before running ISCC, the installer
packages a STALE binary — silently shipping an old exe with different
dependencies, missing features, or security holes.

This check caught the v1.2.2 "c10.dll not found" crash where the installer
shipped a pre-refactor exe that still imported LibTorch, while the build
output was correctly torch-free.

Strategy:
  - Compare SM5_Portable/Sample Monsta.exe against
    build/SM5App_artefacts/Release/Sample Monsta.exe
  - Compare SM5_Portable/VST3/.../Sample Monsta.vst3 against
    build/SM5Plugin_artefacts/Release/VST3/.../Sample Monsta.vst3
  - Flag as DRIFT if:
      (a) sizes differ (different binary)
      (b) staging copy is OLDER than build copy (stale)
      (c) build artifact doesn't exist (not built yet)
      (d) staging artifact doesn't exist (never copied)

Usage:
    python tools/release_linter/check_staging_sync.py
    python tools/release_linter/check_staging_sync.py --json report.json

Exit codes:
    0 = clean (staging matches build)
    1 = drift (stale or missing staging artifacts)
    2 = tool error
"""
from __future__ import annotations
import argparse
import json
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_staging_skips.txt"

# Pairs of (build_path, staging_path) relative to REPO_ROOT.
# Each pair represents a binary that MUST be synced before building the installer.
SYNC_PAIRS = [
    {
        "name": "Standalone EXE",
        "build":   "build/SM5App_artefacts/Release/Sample Monsta.exe",
        "staging": "SM5_Portable/Sample Monsta.exe",
    },
    {
        "name": "VST3 Plugin",
        "build":   "build/SM5Plugin_artefacts/Release/VST3/Sample Monsta.vst3/Contents/x86_64-win/Sample Monsta.vst3",
        "staging": "SM5_Portable/VST3/Sample Monsta.vst3/Contents/x86_64-win/Sample Monsta.vst3",
    },
]


def load_allowlist() -> set[str]:
    if not ALLOWLIST_FILE.exists():
        return set()
    out: set[str] = set()
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


def check_pair(pair: dict) -> dict:
    build_path = REPO_ROOT / pair["build"]
    staging_path = REPO_ROOT / pair["staging"]

    result = {
        "name": pair["name"],
        "build_path": pair["build"],
        "staging_path": pair["staging"],
        "build_exists": build_path.exists(),
        "staging_exists": staging_path.exists(),
        "build_size": 0,
        "staging_size": 0,
        "build_mtime": 0.0,
        "staging_mtime": 0.0,
        "status": "OK",
        "issue": "",
    }

    if not build_path.exists():
        result["status"] = "DRIFT"
        result["issue"] = "Build artifact not found — run cmake --build first"
        return result

    if not staging_path.exists():
        result["status"] = "DRIFT"
        result["issue"] = "Staging artifact not found — copy build output to SM5_Portable/"
        return result

    build_stat = build_path.stat()
    staging_stat = staging_path.stat()

    result["build_size"] = build_stat.st_size
    result["staging_size"] = staging_stat.st_size
    result["build_mtime"] = build_stat.st_mtime
    result["staging_mtime"] = staging_stat.st_mtime

    if build_stat.st_size != staging_stat.st_size:
        result["status"] = "DRIFT"
        result["issue"] = (
            f"Size mismatch: build={build_stat.st_size:,} bytes vs "
            f"staging={staging_stat.st_size:,} bytes — staging has a DIFFERENT binary"
        )
        return result

    if staging_stat.st_mtime < build_stat.st_mtime - 1.0:
        result["status"] = "DRIFT"
        result["issue"] = "Staging is OLDER than build — copy the fresh build to SM5_Portable/"
        return result

    return result


def analyze() -> dict:
    allow = load_allowlist()
    results = []
    drift_items: list[str] = []

    for pair in SYNC_PAIRS:
        r = check_pair(pair)
        if r["name"] in allow:
            r["status"] = "ALLOWED"
        if r["status"] == "DRIFT":
            drift_items.append(r["name"])
        results.append(r)

    return {
        "pairs_checked": len(results),
        "results": results,
        "missing": drift_items,
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Staging Directory Sync")
    print("=" * 70)
    print(f"Artifacts checked: {report['pairs_checked']}")
    print()

    for r in report["results"]:
        marker = "OK " if r["status"] == "OK" else ("!! " if r["status"] == "DRIFT" else "-- ")
        print(f"  {marker}{r['name']}")
        if r["status"] == "DRIFT":
            print(f"      {r['issue']}")
            print(f"      build:   {r['build_path']}")
            print(f"      staging: {r['staging_path']}")
        elif r["status"] == "OK":
            print(f"      {r['build_size']:,} bytes, staging is current")
    print()

    if report["missing"]:
        print(f"[DRIFT] {len(report['missing'])} artifact(s) out of sync:")
        for name in report["missing"]:
            print(f"  - {name}")
        print()
        print("  Fix: copy build output to SM5_Portable/ before running ISCC:")
        print('    cp "build/SM5App_artefacts/Release/Sample Monsta.exe" "SM5_Portable/"')
        print('    cp -r "build/SM5Plugin_artefacts/Release/VST3/Sample Monsta.vst3" "SM5_Portable/VST3/"')
    else:
        print("OK: SM5_Portable/ matches the build output. Installer will ship the correct binaries.")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 staging directory sync linter")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    report = analyze()
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON: {args.json}")

    return 1 if report["missing"] else 0


if __name__ == "__main__":
    sys.exit(main())
