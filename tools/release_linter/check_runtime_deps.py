#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #6: runtime DLL dependency walker.

"Will the VST3 load on a fresh Windows 10/11 machine?" Parses the PE import AND
delay-import tables of the shipped binary (Contents/x86_64-win/SAMPLAR.vst3) and,
recursively, of every DLL the bundle ships (onnxruntime.dll, DirectML.dll), and
puts every imported DLL in one bucket:

  win_system  - part of every supported Windows (Win10 1809+ / Win11), incl. API sets
  vc_redist   - from the VC++ 2015-2022 x64 redistributable (installer runs vc_redist)
  bundled     - shipped next to the binary in the bundle payload
  allowlisted - intentional_unshipped_dlls.txt
  missing     - none of the above -> "DLL not found" on a customer machine

Delay-loaded DLLs that only some Windows builds have (dxcore.dll: Win10 2004+) are
listed as win_system_optional: ONNX Runtime / DirectML fall back to CPU without them.

Requires: pip install pefile. Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

try:
    import pefile
except ImportError:
    print("ERROR: pefile not installed. Run: pip install pefile", file=sys.stderr)
    sys.exit(2)

from linter_common import bundle_bin_dir, bundle_binary, common_args, default_bundle, iss_text, load_allowlist, shipped_payload

WINDOWS_SYSTEM_DLLS = {
    "kernel32.dll", "kernelbase.dll", "user32.dll", "gdi32.dll", "gdi32full.dll", "advapi32.dll", "sechost.dll",
    "rpcrt4.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "combase.dll", "shell32.dll",
    "shlwapi.dll", "shcore.dll", "comctl32.dll", "comdlg32.dll", "propsys.dll", "d3d9.dll", "d3d10.dll",
    "d3d11.dll", "d3d12.dll", "dxgi.dll", "d2d1.dll", "dwrite.dll", "windowscodecs.dll", "gdiplus.dll",
    "opengl32.dll", "glu32.dll", "dwmapi.dll", "uxtheme.dll", "dcomp.dll", "winmm.dll", "avrt.dll",
    "mmdevapi.dll", "audioses.dll", "mfplat.dll", "mf.dll", "mfreadwrite.dll", "dsound.dll", "ws2_32.dll",
    "wininet.dll", "winhttp.dll", "iphlpapi.dll", "dnsapi.dll", "crypt32.dll", "cryptsp.dll", "bcrypt.dll",
    "ncrypt.dll", "secur32.dll", "wldap32.dll", "mswsock.dll", "netapi32.dll", "imm32.dll", "msctf.dll",
    "powrprof.dll", "setupapi.dll", "cfgmgr32.dll", "hid.dll", "psapi.dll", "version.dll", "userenv.dll",
    "wtsapi32.dll", "dbghelp.dll", "imagehlp.dll", "pdh.dll", "msimg32.dll", "winspool.drv", "dxva2.dll",
    "dinput8.dll", "xinput1_4.dll", "wintrust.dll", "normaliz.dll", "urlmon.dll", "credui.dll",
}
# Present on current Windows but not on every supported build; only ever delay-loaded.
WINDOWS_OPTIONAL_DLLS = {"dxcore.dll"}
VC_REDIST_DLLS = {
    "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "concrt140.dll", "vccorlib140.dll",
    "ucrtbase.dll",
}


def is_system(name: str) -> bool:
    return name in WINDOWS_SYSTEM_DLLS or name.startswith("api-ms-win-") or name.startswith("ext-ms-")


def pe_imports(path: Path) -> tuple[list[str], list[str]]:
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
                                           pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])
    imp = sorted({e.dll.decode("utf-8", "replace").lower() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])})
    dly = sorted({e.dll.decode("utf-8", "replace").lower() for e in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", [])})
    pe.close()
    return imp, dly


def analyze(bundle: Path) -> dict:
    binary = bundle_binary(bundle)
    if not binary.exists():
        return {"error": f"binary not found: {binary}"}
    shipped, _ = shipped_payload(bundle)
    bin_dir = bundle_bin_dir(bundle)
    bundled = {Path(s).name.lower(): bundle / s for s in shipped
               if s.lower().startswith("contents/x86_64-win/") and s.lower().endswith(".dll")
               and "/" not in s[len("contents/x86_64-win/"):]}
    has_redist = bool(re.search(r"vc_redist[._]?x64\.exe", iss_text(), re.IGNORECASE))
    allow = load_allowlist("intentional_unshipped_dlls.txt", lower=True)

    buckets: dict[str, dict[str, list[str]]] = {k: {} for k in
                                               ("win_system", "win_system_optional", "vc_redist", "bundled", "allowlisted", "missing")}
    graph: dict[str, dict[str, list[str]]] = {}
    queue = [(binary.name, binary)]
    seen = set()
    while queue:
        name, path = queue.pop(0)
        if name.lower() in seen:
            continue
        seen.add(name.lower())
        imp, dly = pe_imports(path)
        graph[name] = {"imports": imp, "delay_imports": dly}
        for dll in imp + dly:
            delayed = dll in dly and dll not in imp
            if is_system(dll):
                b = "win_system"
            elif dll in WINDOWS_OPTIONAL_DLLS and delayed:
                b = "win_system_optional"
            elif dll in VC_REDIST_DLLS:
                b = "vc_redist" if has_redist else "missing"
            elif dll in bundled:
                b = "bundled"
                queue.append((bundled[dll].name, bundled[dll]))
            elif dll in allow:
                b = "allowlisted"
            else:
                b = "missing"
            buckets[b].setdefault(dll, []).append(name + (" (delay)" if delayed else ""))
    return {
        "binary": str(binary),
        "bundle_dlls": sorted(bundled),
        "installer_ships_vc_redist": has_redist,
        "graph": graph,
        **{k: sorted(v) for k, v in buckets.items()},
        "importers": {k: v for b in buckets.values() for k, v in b.items()},
        "missing_detail": {k: buckets["missing"][k] for k in sorted(buckets["missing"])},
        "bin_dir": str(bin_dir),
    }


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - Runtime DLL Dependency Walker")
    print("=" * 70)
    if "error" in r:
        print(f"ERROR: {r['error']}")
        print("=" * 70)
        return
    print(f"Binary: {r['binary']}")
    for mod, g in r["graph"].items():
        print(f"  {mod}: {len(g['imports'])} imports, delay-loads: {', '.join(g['delay_imports']) or '-'}")
    print(f"VC++ redist shipped by installer: {'YES' if r['installer_ships_vc_redist'] else 'NO'}")
    for k in ("win_system", "win_system_optional", "vc_redist", "bundled", "allowlisted"):
        print(f"  {k:<20} {len(r[k]):>3}  {', '.join(d for d in r[k] if not d.startswith('api-ms-'))}")
    print()
    if r["missing"]:
        print(f"[MISSING] {len(r['missing'])} DLL(s) will not be found on a fresh machine:")
        for d in r["missing"]:
            print(f"  - {d}   (imported by {', '.join(r['missing_detail'][d])})")
    else:
        print("OK: every imported DLL is a Windows system DLL, VC++ redist, or shipped in the bundle.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR runtime DLL dependency walker"), bundle=True)
    ap.add_argument("--exe", type=Path, help=argparse.SUPPRESS)   # legacy flag: path to the binary
    args = ap.parse_args()
    bundle = args.bundle or default_bundle()
    if args.exe:
        bundle = args.exe.resolve().parents[2]
    try:
        report = analyze(bundle)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if "error" in report:
        return 2
    return 1 if report["missing"] else 0


if __name__ == "__main__":
    sys.exit(main())
