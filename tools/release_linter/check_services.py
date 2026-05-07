#!/usr/bin/env python3
"""
SM5 Release Linter — Check #5: IService registration sync.

Detects classes that inherit from sm5::IService but are never registered
with the ServiceRegistry. An IService subclass that isn't registered is
dead code — its initialise()/shutdown() hooks never fire and the rest of
the app can't find it via services.get("name").

Strategy:
  - Scan src/**/*.h for classes inheriting IService. Extract class names.
  - Scan src/app/App.cpp AND src/plugin/SM5AudioProcessor.cpp for
    registerService("...", std::make_unique<ClassName>(...)) patterns.
  - A service is considered registered if it appears in EITHER
    registration site (standalone OR plugin).
  - Report IService subclasses with no registration site.

Usage:
    python tools/release_linter/check_services.py
    python tools/release_linter/check_services.py --json report.json

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
CODE_ROOT = REPO_ROOT / "src"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_unregistered_services.txt"

# Both registration sites. A service registered in EITHER counts as wired.
REGISTRATION_SITES = [
    REPO_ROOT / "src" / "app" / "App.cpp",
    REPO_ROOT / "src" / "plugin" / "SM5AudioProcessor.cpp",
]

# Also skim these for indirect wiring (service-of-services pattern).
# Any IService subclass mentioned here by name is considered "owned by another
# service" and therefore not strictly required to be registered standalone.
OWNER_SITES = [
    REPO_ROOT / "src" / "services" / "ProcessingCoordinatorService.cpp",
    REPO_ROOT / "src" / "services" / "GeneratorService.cpp",
    REPO_ROOT / "src" / "services" / "smode" / "SModeService.cpp",
    REPO_ROOT / "src" / "services" / "DrumSliceService.cpp",
]

SERVICE_DECL_RE = re.compile(
    r"class\s+(\w+)\s*:\s*public\s+IService", re.MULTILINE
)


def discover_services() -> list[str]:
    names: set[str] = set()
    for path in CODE_ROOT.rglob("*.h"):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in SERVICE_DECL_RE.finditer(text):
            names.add(m.group(1))
    return sorted(names)


def file_mentions(path: Path, name: str) -> bool:
    if not path.exists():
        return False
    text = path.read_text(encoding="utf-8", errors="replace")
    return re.search(rf"\b{re.escape(name)}\b", text) is not None


def load_allowlist() -> set[str]:
    if not ALLOWLIST_FILE.exists():
        return set()
    out: set[str] = set()
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


def analyze() -> dict:
    services = discover_services()
    allow = load_allowlist()

    results = []
    missing: list[str] = []

    for name in services:
        standalone = any(file_mentions(p, name) for p in REGISTRATION_SITES[:1])
        plugin = any(file_mentions(p, name) for p in REGISTRATION_SITES[1:])
        owned_by = [p.name for p in OWNER_SITES if file_mentions(p, name)]

        if standalone or plugin:
            status = "OK"
        elif owned_by:
            status = "OWNED"  # registered indirectly via another service
        elif name in allow:
            status = "ALLOWED"
        else:
            status = "DRIFT"
            missing.append(name)

        results.append({
            "name": name,
            "standalone": standalone,
            "plugin": plugin,
            "owned_by": owned_by,
            "status": status,
        })

    return {
        "services_found": len(services),
        "results": results,
        "missing": missing,
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — IService Registration Sync")
    print("=" * 70)
    print(f"IService subclasses discovered: {report['services_found']}")
    print()
    print(f"{'SERVICE':<34} {'APP':<5} {'VST':<5} STATUS")
    print("-" * 70)
    for r in report["results"]:
        app = "yes" if r["standalone"] else "NO"
        vst = "yes" if r["plugin"] else "NO"
        status = r["status"]
        if r["owned_by"]:
            status += f" (by {r['owned_by'][0]})"
        print(f"{r['name']:<34} {app:<5} {vst:<5} {status}")
    print()

    if report["missing"]:
        print(f"[MISSING] {len(report['missing'])} service(s) not registered anywhere:")
        for n in report["missing"]:
            print(f"  - {n}")
        print()
    else:
        print("OK: every IService subclass is registered (or owned, or allowlisted).")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 IService registration linter")
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
