#!/usr/bin/env python3
"""
SM5 Release Linter — Check #6: Runtime DLL dependency walker.

This is the "will it run on a fresh Windows 10/11 machine?" gate.

Parses the built SM5App exe's PE import table and categorizes every imported
DLL into one of four buckets:

  (a) WIN_SYSTEM    — shipped with every Windows 10/11 install (kernel32 etc.)
  (b) VC_REDIST     — supplied by Microsoft Visual C++ Redistributable, which
                      the installer bundles as vc_redist.x64.exe
  (c) BUNDLED       — explicitly shipped in installer_sm5.iss
  (d) MISSING       — NOT in any of the above → the exe will fail to load
                      on a pristine machine with "DLL not found" error

Requires: pip install pefile

Usage:
    python tools/release_linter/check_runtime_deps.py
    python tools/release_linter/check_runtime_deps.py --exe path/to/exe
    python tools/release_linter/check_runtime_deps.py --json report.json

Exit codes:
    0 = clean (all DLLs accounted for)
    1 = drift (at least one unresolved DLL)
    2 = tool error (exe not found, pefile missing, etc.)
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

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = REPO_ROOT / "build" / "SM5App_artefacts" / "Release" / "Sample Monsta.exe"
INSTALLER_FILE = REPO_ROOT / "installer_sm5.iss"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_unshipped_dlls.txt"

# Curated set of DLLs that ship with every supported Windows version (Win10 1809+
# and Win11). An exe importing ONLY these + VC redist + bundled DLLs is
# guaranteed to load on any user's machine.
#
# Source: Microsoft "Windows API sets" + the default System32 contents of a
# fresh Win10 x64 install. List is lowercase, extension included.
WINDOWS_SYSTEM_DLLS = {
    # Core Win32 kernel/user
    "kernel32.dll", "kernelbase.dll", "user32.dll", "gdi32.dll", "gdi32full.dll",
    "advapi32.dll", "sechost.dll", "rpcrt4.dll", "msvcrt.dll", "ntdll.dll",
    # Shell / COM / OLE
    "ole32.dll", "oleaut32.dll", "combase.dll", "shell32.dll", "shlwapi.dll",
    "shcore.dll", "comctl32.dll", "comdlg32.dll", "propsys.dll",
    # Graphics / display
    "d3d9.dll", "d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll",
    "d2d1.dll", "dwrite.dll", "windowscodecs.dll", "gdiplus.dll",
    "opengl32.dll", "glu32.dll", "dwmapi.dll", "uxtheme.dll", "dcomp.dll",
    # Audio
    "winmm.dll", "avrt.dll", "mmdevapi.dll", "audioses.dll", "mfplat.dll",
    "mf.dll", "mfreadwrite.dll", "dsound.dll",
    # Networking
    "ws2_32.dll", "wininet.dll", "winhttp.dll", "iphlpapi.dll", "dnsapi.dll",
    "crypt32.dll", "cryptsp.dll", "bcrypt.dll", "ncrypt.dll", "secur32.dll",
    "schannel.dll", "wldap32.dll", "mswsock.dll", "netapi32.dll",
    # IME / text services
    "imm32.dll", "msctf.dll",
    # Power / device
    "powrprof.dll", "setupapi.dll", "cfgmgr32.dll", "devobj.dll",
    "hid.dll", "winusb.dll", "bluetoothapis.dll",
    # Misc
    "psapi.dll", "version.dll", "userenv.dll", "profapi.dll",
    "wtsapi32.dll", "wer.dll", "kernel.appcore.dll",
    "msimg32.dll", "riched20.dll", "riched32.dll",
    "winspool.drv", "winspool.dll",
    # Debug / diagnostics (ships with every Windows)
    "dbghelp.dll", "imagehlp.dll", "pdh.dll", "faultrep.dll",
    "api-ms-win-*",  # Win10 API sets — wildcard matched below
    "ext-ms-*",      # Extension sets — wildcard matched below
    # COM automation surface
    "activeds.dll", "adsldp.dll",
    # Codecs / Media Foundation (shipped with Win10+)
    "avicap32.dll", "msvfw32.dll",
    # Windows CoreUI
    "coreuicomponents.dll", "coremessaging.dll", "twinapi.dll", "twinapi.appcore.dll",
}

# Visual C++ Redistributable DLLs (shipped via vc_redist.x64.exe, which the
# installer bundles under [Files]).
VC_REDIST_DLLS = {
    "vcruntime140.dll", "vcruntime140_1.dll",
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll",
    "concrt140.dll", "vccorlib140.dll",
    "mfc140.dll", "mfc140u.dll",
    # Universal CRT (technically Windows 10+ system, but ship with VC redist)
    "ucrtbase.dll", "ucrtbased.dll",
}


def is_windows_system_dll(name: str) -> bool:
    n = name.lower()
    if n in WINDOWS_SYSTEM_DLLS:
        return True
    # Win10 API sets — "api-ms-win-core-file-l1-2-0.dll" etc.
    if n.startswith("api-ms-win-") or n.startswith("ext-ms-"):
        return True
    return False


def is_vc_redist_dll(name: str) -> bool:
    return name.lower() in VC_REDIST_DLLS


def parse_installer_shipped_dlls(installer: Path) -> set[str]:
    """Return lowercase basenames of every .dll shipped via installer [Files]."""
    if not installer.exists():
        return set()
    text = installer.read_text(encoding="utf-8", errors="replace")
    # Match "Source: ...something\foo.dll" or wildcards
    out: set[str] = set()
    # Direct file references
    for m in re.finditer(r'Source:\s*"[^"]*?\\([A-Za-z0-9_.\-]+\.dll)"', text, re.IGNORECASE):
        out.add(m.group(1).lower())
    return out


def installer_ships_vc_redist(installer: Path) -> bool:
    if not installer.exists():
        return False
    text = installer.read_text(encoding="utf-8", errors="replace")
    return bool(re.search(r'vc_redist[._]?x64\.exe', text, re.IGNORECASE))


def parse_installer_wildcard_dirs(installer: Path) -> set[str]:
    """Lowercase top-level dirs shipped via wildcard (e.g., python\\*)."""
    if not installer.exists():
        return set()
    text = installer.read_text(encoding="utf-8", errors="replace")
    return {m.group(1).lower() for m in re.finditer(
        r'Source:\s*"[^"]*?\\([A-Za-z0-9_.\-]+)\\\*"', text, re.IGNORECASE
    )}


def load_allowlist() -> set[str]:
    if not ALLOWLIST_FILE.exists():
        return set()
    out: set[str] = set()
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line.lower())
    return out


def walk_exe_imports(exe: Path) -> list[str]:
    """Return lowercase sorted list of DLLs imported by the exe."""
    try:
        pe = pefile.PE(str(exe), fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]]
        )
    except Exception as e:
        print(f"ERROR: failed to parse {exe}: {e}", file=sys.stderr)
        sys.exit(2)

    dlls: set[str] = set()
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            name = entry.dll.decode("utf-8", errors="replace").lower()
            dlls.add(name)
    return sorted(dlls)


def analyze(exe: Path) -> dict:
    imports = walk_exe_imports(exe)
    shipped = parse_installer_shipped_dlls(INSTALLER_FILE)
    wildcard_dirs = parse_installer_wildcard_dirs(INSTALLER_FILE)
    has_redist = installer_ships_vc_redist(INSTALLER_FILE)
    allow = load_allowlist()

    categorized: dict[str, list[str]] = {
        "win_system": [],
        "vc_redist": [],
        "bundled": [],
        "allowlisted": [],
        "missing": [],
    }

    for dll in imports:
        if is_windows_system_dll(dll):
            categorized["win_system"].append(dll)
        elif is_vc_redist_dll(dll):
            if has_redist:
                categorized["vc_redist"].append(dll)
            else:
                categorized["missing"].append(dll)
        elif dll in shipped:
            categorized["bundled"].append(dll)
        elif dll in allow:
            categorized["allowlisted"].append(dll)
        else:
            categorized["missing"].append(dll)

    return {
        "exe": str(exe),
        "exe_exists": exe.exists(),
        "installer_ships_vc_redist": has_redist,
        "imports_total": len(imports),
        "win_system": sorted(set(categorized["win_system"])),
        "vc_redist": sorted(set(categorized["vc_redist"])),
        "bundled": sorted(set(categorized["bundled"])),
        "allowlisted": sorted(set(categorized["allowlisted"])),
        "missing": sorted(set(categorized["missing"])),
        "shipped_dll_count": len(shipped),
        "wildcard_dirs": sorted(wildcard_dirs),
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Runtime DLL Dependency Walker")
    print("=" * 70)
    print(f"EXE: {report['exe']}")
    print(f"Imports: {report['imports_total']} DLLs")
    print(f"VC++ Redist shipped: {'YES' if report['installer_ships_vc_redist'] else 'NO'}")
    print()
    print(f"  Windows system: {len(report['win_system'])}")
    print(f"  VC++ redist:    {len(report['vc_redist'])}")
    print(f"  Bundled:        {len(report['bundled'])}")
    print(f"  Allowlisted:    {len(report['allowlisted'])}")
    print(f"  MISSING:        {len(report['missing'])}")
    print()

    if report["missing"]:
        print(f"[MISSING] {len(report['missing'])} DLL(s) NOT accounted for — app will fail to load:")
        for d in report["missing"]:
            print(f"  - {d}")
        print()
        print("  Options for each missing DLL:")
        print("    1. Add it to installer_sm5.iss [Files] section")
        print("    2. If it's a VC++ runtime DLL, ensure vc_redist.x64.exe is shipped")
        print("    3. If it's a newer Windows API set, add to intentional_unshipped_dlls.txt")
        print("       with a comment explaining minimum supported Windows version")
    else:
        print("OK: every imported DLL is accounted for.")
        print("    The exe will load on any Windows 10 1809+ x64 machine with")
        print("    VC++ Redistributable installed (bundled in the installer).")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 runtime DLL dependency linter")
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not args.exe.exists():
        print(f"ERROR: exe not found: {args.exe}", file=sys.stderr)
        print(f"Build SM5App first: cmake --build build --config Release --target SM5App", file=sys.stderr)
        return 2

    report = analyze(args.exe)
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON: {args.json}")

    return 1 if report["missing"] else 0


if __name__ == "__main__":
    sys.exit(main())
