#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #5: IService registration sync.

Every class deriving from samplar::IService must be either
  - registered with the plugin's ServiceRegistry (src/plugin/SamplarAudioProcessor.cpp
    is the shipping registration site; src/app/App.cpp is the dev-only standalone), or
  - owned/constructed by another compiled source (e.g. SModeService owns
    SModeSeparationService, which constructs a FullSongService per separation), or
  - allowlisted in intentional_unregistered_services.txt.
An IService that is registered ONLY in the standalone app is drift too: the
VST3 (the only shipped product) would run without it.

Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys

from linter_common import REPO_ROOT, common_args, load_allowlist

CODE_ROOT = REPO_ROOT / "src"
PLUGIN_SITE = REPO_ROOT / "src" / "plugin" / "SamplarAudioProcessor.cpp"
STANDALONE_SITE = REPO_ROOT / "src" / "app" / "App.cpp"
DECL_RE = re.compile(r"class\s+(\w+)\s*(?:final\s*)?:\s*(?:public\s+\w+\s*,\s*)*public\s+IService\b")
REGISTER_RE = re.compile(r'registerService\s*\(\s*"([^"]+)"\s*,\s*(?:std::make_unique\s*<\s*(?:samplar::)?(\w+)|std::move\s*\(\s*(\w+))')


def discover() -> dict[str, str]:
    out: dict[str, str] = {}
    for path in CODE_ROOT.rglob("*.h"):
        for m in DECL_RE.finditer(path.read_text(encoding="utf-8", errors="replace")):
            out[m.group(1)] = path.relative_to(REPO_ROOT).as_posix()
    return out


def registrations(site) -> dict[str, str]:
    """class -> registry name, from registerService("name", make_unique<Class>) or
    registerService("name", std::move(var)) with `auto var = std::make_unique<Class>`."""
    if not site.exists():
        return {}
    text = site.read_text(encoding="utf-8", errors="replace")
    out: dict[str, str] = {}
    for m in REGISTER_RE.finditer(text):
        name, cls, var = m.group(1), m.group(2), m.group(3)
        if var and not cls:
            vm = re.search(rf"\b{re.escape(var)}\s*=\s*std::make_unique\s*<\s*(?:samplar::)?(\w+)", text)
            cls = vm.group(1) if vm else None
        if cls:
            out[cls] = name
    return out


def owners(name: str, decl_header: str) -> list[str]:
    own_stem = decl_header.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    found = []
    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h"):
            continue
        if path.stem == own_stem or path == PLUGIN_SITE or path == STANDALONE_SITE:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(rf"\b{re.escape(name)}\s+\w+\s*[({{]|make_unique\s*<\s*(?:samplar::)?{re.escape(name)}\b|\b{re.escape(name)}\s+\w+\s*;", text):
            found.append(path.relative_to(REPO_ROOT).as_posix())
    return found


def analyze() -> dict:
    services = discover()
    plugin = registrations(PLUGIN_SITE)
    standalone = registrations(STANDALONE_SITE)
    allow = load_allowlist("intentional_unregistered_services.txt")
    results, missing = [], []
    for name, header in sorted(services.items()):
        own = owners(name, header)
        if name in plugin:
            status = "OK"
        elif own:
            status = "OWNED"
        elif name in allow:
            status = "ALLOWED"
        elif name in standalone:
            status = "STANDALONE-ONLY"
            missing.append(name)
        else:
            status = "DRIFT"
            missing.append(name)
        results.append({"name": name, "header": header, "plugin": plugin.get(name, ""),
                        "standalone": standalone.get(name, ""), "owned_by": own, "status": status})
    return {
        "services_found": len(services),
        "results": results,
        "missing": missing,
        "stale_allowlist": sorted(a for a in allow if a not in services or a in plugin),
        "plugin_registry_names": sorted(plugin.values()),
    }


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - IService Registration Sync")
    print("=" * 70)
    print(f"{'SERVICE':<28}{'VST3 name':<16}{'standalone':<16}STATUS")
    print("-" * 70)
    for s in r["results"]:
        extra = f" (by {', '.join(x.rsplit('/', 1)[-1] for x in s['owned_by'])})" if s["status"] == "OWNED" else ""
        print(f"{s['name']:<28}{s['plugin'] or '-':<16}{s['standalone'] or '-':<16}{s['status']}{extra}")
    print()
    if r["missing"]:
        print(f"[MISSING] {len(r['missing'])} service(s) not wired into the VST3: {', '.join(r['missing'])}")
    if r["stale_allowlist"]:
        print(f"[INFO] stale allowlist entries (prune): {', '.join(r['stale_allowlist'])}")
    if not r["missing"]:
        print("OK: every IService is registered in the plugin or owned by a compiled service.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR IService registration linter"))
    args = ap.parse_args()
    try:
        report = analyze()
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
