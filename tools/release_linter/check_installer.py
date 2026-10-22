#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #3: installer script + VST3 bundle layout.

The installer (release_assets/installer/samplar.iss) copies the whole staged
bundle build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3 minus its
[Files] Excludes. This check verifies both halves:

  samplar.iss
    - [Files] ships the bundle recursively, from the same folder as --bundle
    - Excludes drop *.map / *.pdb / logs (debug + dev artefacts never ship)
    - the ISPP #error guards for the GPU runtime / models / DirectML licence exist
    - default MyAppVersion == CMake project(VERSION); vc_redist source exists
    - [UninstallDelete] removes {app}

  bundle payload (what the installer would actually copy)
    - SAMPLAR.vst3, onnxruntime.dll (ORT 1.19.2 DirectML, pinned sha256),
      DirectML.dll (1.15.1, pinned sha256), models/htdemucs_body.onnx (pinned sha256),
      models/beat_this.onnx (== third_party copy), data/samplar_bundle.marker,
      Contents/Resources/licenses/* (D19 notices)
    - nothing forbidden: *.map *.pdb *.ilk *.exp *.lib *.log logs/,
      models/htdemucs.onnx, onnxruntime_providers_shared.dll, tools/, BACKGROUNDS/,
      Samples/, data/*.json
    - every .dll/.onnx/.exe file name the code references is shipped, a Windows
      system binary, or listed in intentional_unshipped_resources.txt

Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import sys
from pathlib import Path

from linter_common import (DIRECTML_SHA256, HTDEMUCS_BODY_SHA256, INSTALLER_ISS, ORT_DML_SHA256, REPO_ROOT,
                           cmake_project_version, common_args, default_bundle, iss_files_entries,
                           iss_source_path, iss_text, load_allowlist, rel, sha256, shipped_payload)

BIN = "Contents/x86_64-win"
LIC = "Contents/Resources/licenses"
REQUIRED = {
    f"{BIN}/SAMPLAR.vst3": None,
    f"{BIN}/onnxruntime.dll": ORT_DML_SHA256,
    f"{BIN}/DirectML.dll": DIRECTML_SHA256,
    f"{BIN}/models/htdemucs_body.onnx": HTDEMUCS_BODY_SHA256,
    f"{BIN}/models/beat_this.onnx": "same-as:third_party/beat_this/models/beat_this.onnx",
    f"{BIN}/data/samplar_bundle.marker": None,
    f"{LIC}/onnxruntime-LICENSE.txt": None,
    f"{LIC}/onnxruntime-ThirdPartyNotices.txt": None,
    f"{LIC}/DirectML-LICENSE.txt": None,
    f"{LIC}/DirectML-ThirdPartyNotices.txt": None,
    f"{LIC}/pffft-LICENSE.txt": None,
    f"{LIC}/NOTICE.md": None,
    f"{LIC}/signalsmith-stretch-LICENSE.txt": None,
    f"{LIC}/signalsmith-linear-LICENSE.txt": None,
    f"{LIC}/beat_this_cpp-LICENSE.txt": None,
    f"{LIC}/pocketfft-LICENSE.md": None,
    f"{LIC}/JUCE-LICENSE.md": None,
}
# (desktop.ini + Plugin.ico at the bundle root are the VST3 bundle folder icon - allowed.)
FORBIDDEN_GLOBS = ["*.map", "*.pdb", "*.ilk", "*.exp", "*.lib", "*.log",
                   f"{BIN}/logs/*", f"{BIN}/models/htdemucs.onnx", f"{BIN}/onnxruntime_providers_shared.dll",
                   f"{BIN}/tools/*", f"{BIN}/BACKGROUNDS/*", f"{BIN}/Samples/*", f"{BIN}/data/*.json",
                   "Contents/data/*.json"]
REQUIRED_EXCLUDES = ["*.map", "*.pdb", "logs"]
REQUIRED_GUARDS = ["onnxruntime.dll", "DirectML.dll", "htdemucs_body.onnx", "beat_this.onnx",
                   "htdemucs.onnx", "DirectML-LICENSE.txt"]
SYSTEM_BINARIES = {"cmd.exe", "powershell.exe", "explorer.exe", "d3d12.dll", "dxgi.dll", "dxcore.dll",
                   "kernel32.dll", "user32.dll", "advapi32.dll", "shell32.dll", "ole32.dll", "bcrypt.dll"}
CODE_BIN_RE = re.compile(r'"((?:[\w.\-]+/)*)([\w.\-]+\.(?:dll|onnx|exe))"')


def check_iss(bundle: Path, problems: list[str]) -> dict:
    text = iss_text()
    if not text:
        problems.append(f"installer script not found: {rel(INSTALLER_ISS)}")
        return {}
    entries = iss_files_entries(text)
    bundle_entry = next((e for e in entries if "samplarplugin_artefacts" in e["source"].lower()), None)
    info: dict = {"files_entries": len(entries)}
    if not bundle_entry:
        problems.append("samplar.iss [Files] has no entry for the SamplarPlugin_artefacts VST3 bundle")
    else:
        src_dir = iss_source_path(bundle_entry["source"])
        info["iss_bundle"] = rel(src_dir)
        if os.path.normcase(str(src_dir)) != os.path.normcase(str(Path(os.path.normpath(default_bundle_from_build())))):
            problems.append(f"samplar.iss ships {rel(src_dir)}, not the release bundle build/.../SAMPLAR.vst3")
        if "recursesubdirs" not in [f.lower() for f in bundle_entry["flags"]]:
            problems.append("samplar.iss bundle entry lacks Flags: recursesubdirs")
        ex = [e.lower() for e in bundle_entry["excludes"]]
        for req in REQUIRED_EXCLUDES:
            if req not in ex:
                problems.append(f"samplar.iss bundle entry does not exclude '{req}'")
        info["excludes"] = bundle_entry["excludes"]
    for g in REQUIRED_GUARDS:
        if not re.search(rf"#if\s+!?FileExists\([^)]*{re.escape(g)}", text):
            problems.append(f"samplar.iss has no ISPP #error guard for {g}")
    m = re.search(r'#define\s+MyAppVersion\s+"([^"]+)"', text)
    cm = cmake_project_version()
    info["iss_version"], info["cmake_version"] = (m.group(1) if m else ""), cm
    if m and cm and m.group(1) != cm:
        problems.append(f"samplar.iss default MyAppVersion {m.group(1)} != CMake project VERSION {cm}")
    redist = next((e for e in entries if "vc_redist" in e["source"].lower()), None)
    if not redist:
        problems.append("samplar.iss does not ship vc_redist.x64.exe")
    elif not iss_source_path(redist["source"]).exists():
        problems.append(f"vc_redist source missing on this machine: {rel(iss_source_path(redist['source']))}")
    if not re.search(r'\[UninstallDelete\][^\[]*Name:\s*"\{app\}"', text, re.IGNORECASE):
        problems.append("samplar.iss [UninstallDelete] does not remove {app}")
    return info


def default_bundle_from_build() -> Path:
    return REPO_ROOT / "build" / "SamplarPlugin_artefacts" / "Release" / "VST3" / "SAMPLAR.vst3"


def check_bundle(bundle: Path, problems: list[str], allow: set[str]) -> dict:
    if not bundle.is_dir():
        problems.append(f"bundle not found: {bundle} (build SamplarPlugin_VST3 first)")
        return {}
    shipped, dropped = shipped_payload(bundle)
    shipped_l = {s.lower(): s for s in shipped}
    for path, want in REQUIRED.items():
        actual = shipped_l.get(path.lower())
        if not actual:
            problems.append(f"bundle payload missing: {path}")
            continue
        f = bundle / actual
        if f.stat().st_size == 0:
            problems.append(f"bundle file is empty: {path}")
        elif want and want.startswith("same-as:"):
            ref = REPO_ROOT / want[len("same-as:"):]
            if ref.exists() and ref.stat().st_size != f.stat().st_size:
                problems.append(f"{path} differs from {rel(ref)} (stale copy)")
        elif want and sha256(f) != want:
            problems.append(f"{path} has the wrong bits (sha256 mismatch; expected the pinned build)")
    forbidden = sorted(s for s in shipped if any(fnmatch.fnmatch(s.lower(), g.lower()) or
                                                 fnmatch.fnmatch(s.rsplit('/', 1)[-1].lower(), g.lower())
                                                 for g in FORBIDDEN_GLOBS))
    for s in forbidden:
        problems.append(f"would ship forbidden file: {s}")

    names = {s.rsplit("/", 1)[-1].lower() for s in shipped}
    unshipped = []
    for path in (REPO_ROOT / "src").rglob("*"):
        if path.suffix not in (".cpp", ".h"):
            continue
        for m in CODE_BIN_RE.finditer(path.read_text(encoding="utf-8", errors="replace")):
            base = m.group(2).lower()
            if base in names or base in SYSTEM_BINARIES or base in allow:
                continue
            unshipped.append(f"{base} (referenced in {path.relative_to(REPO_ROOT).as_posix()})")
    for u in sorted(set(unshipped)):
        problems.append(f"code references a binary the installer does not ship: {u}")
    return {"shipped_files": len(shipped), "excluded_files": dropped,
            "payload_mb": round(sum((bundle / s).stat().st_size for s in shipped) / 1048576, 1)}


def analyze(bundle: Path) -> dict:
    problems: list[str] = []
    allow = load_allowlist("intentional_unshipped_resources.txt", lower=True)
    iss = check_iss(bundle, problems)
    payload = check_bundle(bundle, problems, allow)
    return {"bundle": str(bundle), "iss": iss, "payload": payload, "missing": problems}


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - Installer Script + VST3 Bundle Layout")
    print("=" * 70)
    print(f"Installer: {rel(INSTALLER_ISS)}  (version {r['iss'].get('iss_version', '?')}, CMake {r['iss'].get('cmake_version', '?')})")
    print(f"Bundle:    {r['bundle']}")
    if r["payload"]:
        print(f"Payload:   {r['payload']['shipped_files']} files, {r['payload']['payload_mb']} MB "
              f"(excluded by [Files] Excludes: {', '.join(r['payload']['excluded_files']) or 'none'})")
    print()
    if r["missing"]:
        print(f"[DRIFT] {len(r['missing'])} problem(s):")
        for p in r["missing"]:
            print(f"  - {p}")
    else:
        print("OK: installer script and bundle payload match the SAMPLAR release layout.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR installer/bundle linter"), bundle=True)
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
