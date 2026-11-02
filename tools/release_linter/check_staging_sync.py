#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #8: build bundle vs sources vs installer (staleness).

SAMPLAR's installer packages the build-tree bundle directly (no separate staging
folder like SM5_Portable/), so "staging drift" means one of:

  1. STALE BUILD - a compiled source / CMake file / NOTICE.md is newer than the
     bundle's SAMPLAR.vst3 -> the installer would package an old binary.
  2. STALE STAGED RESOURCE - a runtime file in the bundle differs from the repo file
     it is staged from (onnxruntime.dll, DirectML.dll, models, NOTICE.md, licences).
  3. VERSION - the binary's FileVersion differs from CMake project(VERSION).
  4. STALE INSTALLER - release_assets/installer/output/SAMPLAR-Setup-<version>.exe
     exists but is older than the bundle (an old build carrying the current
     version number must never be uploaded).

Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    import pefile
except ImportError:
    pefile = None

from linter_common import (INSTALLER_ISS, REPO_ROOT, bundle_bin_dir, bundle_binary, cmake_files, cmake_project_version,
                           cmake_source_refs, common_args, default_bundle, rel, sha256)

LIC = "Contents/Resources/licenses"
# bundle-relative file -> repo source it is staged from (sha256 compared when < 64 MB, else size)
STAGED_FROM = {
    "Contents/x86_64-win/onnxruntime.dll": "third_party/onnxruntime-directml/lib/onnxruntime.dll",
    "Contents/x86_64-win/DirectML.dll": "third_party/onnxruntime-directml/lib/DirectML.dll",
    "Contents/x86_64-win/models/htdemucs_body.onnx": "models/htdemucs_body.onnx",
    "Contents/x86_64-win/models/beat_this.onnx": "third_party/beat_this/models/beat_this.onnx",
    f"{LIC}/NOTICE.md": "NOTICE.md",
    f"{LIC}/pffft-LICENSE.txt": "third_party/pffft/LICENSE.txt",
    f"{LIC}/DirectML-LICENSE.txt": "third_party/onnxruntime-directml/DirectML-LICENSE.txt",
    f"{LIC}/onnxruntime-LICENSE.txt": "third_party/onnxruntime-directml/LICENSE",
    f"{LIC}/signalsmith-stretch-LICENSE.txt": "third_party/signalsmith-stretch/LICENSE.txt",
    f"{LIC}/signalsmith-linear-LICENSE.txt": "third_party/signalsmith-linear/LICENSE.txt",
}
SOURCE_DIRS = ["src", "third_party/beat_this/src", "third_party/pffft/src"]


def file_version(binary: Path) -> str:
    if pefile is None:
        return ""
    pe = pefile.PE(str(binary), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    for fi in getattr(pe, "FileInfo", None) or []:
        for e in fi:
            for st in getattr(e, "StringTable", []):
                v = st.entries.get(b"FileVersion")
                if v:
                    return v.decode("utf-8", "replace")
    return ""


def newest_inputs(limit: int = 5) -> list[tuple[float, str]]:
    files: list[Path] = list(cmake_files())
    refs = cmake_source_refs()
    for d in SOURCE_DIRS:
        base = REPO_ROOT / d
        if base.is_dir():
            files += [f for f in base.rglob("*") if f.suffix in (".cpp", ".c", ".h", ".hpp", ".in")
                      and (f.suffix != ".cpp" or f.relative_to(REPO_ROOT).as_posix() in refs)]
    files.append(REPO_ROOT / "NOTICE.md")
    stamped = sorted(((f.stat().st_mtime, rel(f)) for f in files if f.exists()), reverse=True)
    return stamped[:limit]


def analyze(bundle: Path) -> dict:
    binary = bundle_binary(bundle)
    problems: list[str] = []
    info: dict = {"bundle": str(bundle)}
    if not binary.exists():
        return {"bundle": str(bundle), "missing": [f"bundle binary not found: {binary} (build SamplarPlugin_VST3)"]}
    bmt = binary.stat().st_mtime
    info["binary_mtime"] = bmt

    newer = [(t, f) for t, f in newest_inputs(1_000_000) if t > bmt + 2.0]
    info["inputs_newer_than_binary"] = [f for _, f in newer]
    if newer:
        problems.append(f"STALE BUILD: {len(newer)} input(s) changed after SAMPLAR.vst3 was linked "
                        f"(newest: {', '.join(f for _, f in newer[:5])}) - rebuild before packaging")

    for b_rel, src_rel in STAGED_FROM.items():
        b, s = bundle / b_rel, REPO_ROOT / src_rel
        if not s.exists():
            continue
        if not b.exists():
            problems.append(f"STALE STAGED RESOURCE: {b_rel} missing in bundle (source {src_rel} exists)")
            continue
        same = (b.stat().st_size == s.stat().st_size and
                (s.stat().st_size > 64 * 1048576 or sha256(b) == sha256(s)))
        if not same:
            problems.append(f"STALE STAGED RESOURCE: {b_rel} differs from {src_rel}")

    ver, cm = file_version(binary), cmake_project_version()
    info["binary_version"], info["cmake_version"] = ver, cm
    if ver and cm and ver.split(".")[:3] != cm.split(".")[:3]:
        problems.append(f"VERSION: SAMPLAR.vst3 FileVersion {ver} != CMake project VERSION {cm}")

    setup = INSTALLER_ISS.parent / "output" / f"SAMPLAR-Setup-{cm}.exe"
    info["installer"] = rel(setup) if setup.exists() else None
    if setup.exists() and setup.stat().st_mtime + 2.0 < bmt:
        problems.append(f"STALE INSTALLER: {rel(setup)} is older than the bundle binary - "
                        f"rebuild it (release_assets/build_installer.ps1) and never upload the old one")
    info["missing"] = problems
    return info


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - Build Bundle / Installer Staleness")
    print("=" * 70)
    print(f"Bundle:    {r['bundle']}")
    if "binary_version" in r:
        print(f"Versions:  binary {r['binary_version'] or '?'}  /  CMake {r['cmake_version'] or '?'}")
        print(f"Installer: {r.get('installer') or '(not built for this version)'}")
    print()
    if r["missing"]:
        print(f"[DRIFT] {len(r['missing'])} problem(s):")
        for p in r["missing"]:
            print(f"  - {p}")
    else:
        print("OK: bundle is current with its sources and the installer output.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR bundle/installer staleness linter"), bundle=True)
    args = ap.parse_args()
    try:
        report = analyze(args.bundle or default_bundle())
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if report["missing"] else 0


if __name__ == "__main__":
    sys.exit(main())
