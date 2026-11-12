#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #9: plugin identity of the BUILT bundle.

A DAW finds a saved SAMPLAR instance by its identity codes, so a silent change
breaks customer sessions. cmake/identity.cmake is the single source of truth
(it mirrors MASTERMIND _registry/plugins/plugin-ids.md). This check reads the
identity back out of the built plugin and fails on any difference:

  1. identity.cmake is self-consistent: SAMPLAR_VST3_CLASS_ID equals the class ID
     JUCE derives from the codes (ABCDEF01 9182FAEB + hex(manufacturer) + hex(plugin)).
  2. VST3 Contents/Resources/moduleinfo.json: "Audio Module Class" CID and vendor.
  3. VST3 Contents/Info.plist (macOS bundle): CFBundleIdentifier.
  4. AU (macOS, only when a .component is built): Info.plist AudioComponents
     type / subtype / manufacturer.

Exit codes: 0 clean, 1 drift, 2 tool error.

Usage:
    python3 tools/release_linter/check_plugin_ids.py --bundle <SAMPLAR.vst3>
    python3 tools/release_linter/check_plugin_ids.py --bundle <SAMPLAR.vst3> --identity <scratch identity.cmake>
"""
from __future__ import annotations

import argparse
import json
import plistlib
import re
import sys
from pathlib import Path

from linter_common import REPO_ROOT, common_args, default_bundle, rel

IDENTITY_CMAKE = REPO_ROOT / "cmake" / "identity.cmake"
JUCE_VST3_CID_PREFIX = "ABCDEF019182FAEB"
# juce_add_plugin AU_MAIN_TYPE constant -> four-char AudioComponents "type"
AU_TYPE_CODES = {
    "kAudioUnitType_MusicDevice": "aumu",
    "kAudioUnitType_Effect": "aufx",
    "kAudioUnitType_MusicEffect": "aumf",
    "kAudioUnitType_MIDIProcessor": "aumi",
    "kAudioUnitType_Generator": "augn",
}
REQUIRED_KEYS = ("SAMPLAR_COMPANY_NAME", "SAMPLAR_PLUGIN_MANUFACTURER_CODE", "SAMPLAR_PLUGIN_CODE",
                 "SAMPLAR_VST3_CLASS_ID", "SAMPLAR_BUNDLE_ID", "SAMPLAR_BUNDLE_ID_NON_APPLE")

_SET_RE = re.compile(r'^\s*set\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+"([^"]*)"\s*\)', re.MULTILINE)
_TRAILING_COMMA_RE = re.compile(r",(\s*[}\]])")


def load_identity(path: Path) -> dict[str, str]:
    """set(NAME "value") pairs from identity.cmake (comments stripped)."""
    text = re.sub(r"#[^\n]*", "", path.read_text(encoding="utf-8"))
    ident = {m.group(1): m.group(2) for m in _SET_RE.finditer(text)}
    missing = [k for k in REQUIRED_KEYS if k not in ident]
    if missing:
        raise ValueError(f"{rel(path)} does not set: {', '.join(missing)}")
    return ident


def four_cc_hex(code: str) -> str:
    if len(code) != 4 or not code.isascii():
        raise ValueError(f"plugin code must be 4 ASCII chars, got {code!r}")
    return code.encode("ascii").hex().upper()


def derived_class_id(ident: dict[str, str]) -> str:
    return (JUCE_VST3_CID_PREFIX + four_cc_hex(ident["SAMPLAR_PLUGIN_MANUFACTURER_CODE"])
            + four_cc_hex(ident["SAMPLAR_PLUGIN_CODE"]))


def read_moduleinfo(bundle: Path) -> dict:
    """JUCE's moduleinfo.json is JSON5-ish (trailing commas); strip them first."""
    path = bundle / "Contents" / "Resources" / "moduleinfo.json"
    if not path.exists():
        raise FileNotFoundError(f"no moduleinfo.json in {rel(bundle)} - cannot read the VST3 class ID")
    return json.loads(_TRAILING_COMMA_RE.sub(r"\1", path.read_text(encoding="utf-8")))


def audio_module_class(info: dict) -> dict:
    for cls in info.get("Classes", []):
        if cls.get("Category") == "Audio Module Class":
            return cls
    raise ValueError("moduleinfo.json has no 'Audio Module Class' entry")


def read_plist(path: Path) -> dict | None:
    if not path.exists():
        return None
    with path.open("rb") as f:
        return plistlib.load(f)


def compare(found: dict, mismatches: list[str], what: str, actual: str, expected: str) -> None:
    found[what] = actual
    if actual != expected:
        mismatches.append(f"{what}: built '{actual}' != identity.cmake '{expected}'")


def check_vst3(bundle: Path, ident: dict[str, str], found: dict, mismatches: list[str]) -> None:
    cls = audio_module_class(read_moduleinfo(bundle))
    compare(found, mismatches, "vst3.class_id", str(cls.get("CID", "")).upper(), ident["SAMPLAR_VST3_CLASS_ID"])
    compare(found, mismatches, "vst3.vendor", str(cls.get("Vendor", "")), ident["SAMPLAR_COMPANY_NAME"])
    plist = read_plist(bundle / "Contents" / "Info.plist")
    if plist is not None:
        # Only macOS builds write a bundle Info.plist; there the Apple bundle id applies.
        compare(found, mismatches, "vst3.bundle_id", str(plist.get("CFBundleIdentifier", "")),
                ident["SAMPLAR_BUNDLE_ID"])


def check_au(component: Path, ident: dict[str, str], found: dict, mismatches: list[str]) -> None:
    plist = read_plist(component / "Contents" / "Info.plist")
    entries = (plist or {}).get("AudioComponents") or []
    if not entries:
        mismatches.append(f"au: {rel(component)} has no AudioComponents entry in Info.plist")
        return
    entry = entries[0]
    expected_type = AU_TYPE_CODES.get(ident.get("SAMPLAR_AU_MAIN_TYPE", ""), "")
    if expected_type:
        compare(found, mismatches, "au.type", str(entry.get("type", "")), expected_type)
    else:
        found["au.type"] = str(entry.get("type", ""))
        mismatches.append("au: a .component was built but identity.cmake sets no SAMPLAR_AU_MAIN_TYPE")
    compare(found, mismatches, "au.manufacturer", str(entry.get("manufacturer", "")),
            ident["SAMPLAR_PLUGIN_MANUFACTURER_CODE"])
    compare(found, mismatches, "au.subtype", str(entry.get("subtype", "")), ident["SAMPLAR_PLUGIN_CODE"])
    compare(found, mismatches, "au.bundle_id", str(plist.get("CFBundleIdentifier", "")), ident["SAMPLAR_BUNDLE_ID"])


def default_component(bundle: Path) -> Path:
    # juce_add_plugin layout: <artefacts>/<Config>/VST3/SAMPLAR.vst3 and .../AU/SAMPLAR.component
    return bundle.parent.parent / "AU" / (bundle.stem + ".component")


def analyze(bundle: Path, identity: Path, component: Path | None) -> dict:
    if not bundle.is_dir():
        raise FileNotFoundError(f"VST3 bundle not found: {bundle}")
    ident = load_identity(identity)
    found: dict[str, str] = {}
    mismatches: list[str] = []

    derived = derived_class_id(ident)
    if derived != ident["SAMPLAR_VST3_CLASS_ID"]:
        mismatches.append(f"identity.cmake: SAMPLAR_VST3_CLASS_ID '{ident['SAMPLAR_VST3_CLASS_ID']}' "
                          f"!= class ID JUCE derives from the codes '{derived}'")

    check_vst3(bundle, ident, found, mismatches)
    au = component or default_component(bundle)
    au_checked = au.is_dir()
    if au_checked:
        check_au(au, ident, found, mismatches)

    return {
        "bundle": rel(bundle),
        "identity": rel(identity),
        "au_component": rel(au) if au_checked else "",
        "expected": ident,
        "found": found,
        "mismatches": mismatches,
    }


def print_report(report: dict) -> None:
    print("SAMPLAR plugin identity check")
    print(f"  bundle:   {report['bundle']}")
    print(f"  identity: {report['identity']}")
    print(f"  au:       {report['au_component'] or '(no .component built - AU not checked)'}")
    for key, value in report["found"].items():
        print(f"  read {key:<16} {value}")
    if report["mismatches"]:
        print(f"FAIL - {len(report['mismatches'])} identity mismatch(es):")
        for m in report["mismatches"]:
            print(f"  - {m}")
    else:
        print("OK - built plugin identity matches identity.cmake")


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR plugin identity (ID drift) check"), bundle=True)
    ap.add_argument("--identity", type=Path, default=IDENTITY_CMAKE,
                    help="identity.cmake to compare against (default: cmake/identity.cmake)")
    ap.add_argument("--au-component", type=Path, default=None,
                    help="AU .component to check (default: ../AU/<name>.component next to the VST3 dir, if built)")
    args = ap.parse_args()
    try:
        report = analyze(args.bundle or default_bundle(), args.identity, args.au_component)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if report["mismatches"] else 0


if __name__ == "__main__":
    sys.exit(main())
