#!/usr/bin/env python3
"""
SM5 Release Linter — Check #2: Engine registry sync.

Detects classes implementing IVariationEngine that are missing from either:
  - GeneratorService.cpp (backend dispatcher)
  - UI engine selection surfaces (EngineLabPage, VariationManager, etc.)

A new engine must appear in ALL of these sites to be real. This linter
catches the recurring "engine exists but never reaches the UI" bug.

Usage:
    python tools/release_linter/check_engines.py
    python tools/release_linter/check_engines.py --json report.json

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
ENGINES_DIR = REPO_ROOT / "src" / "services" / "variation" / "engines"
GENERATOR_SERVICE = REPO_ROOT / "src" / "services" / "GeneratorService.cpp"
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_unregistered_engines.txt"

# Files where an engine name must appear for the UI to expose it.
# An engine is considered UI-wired if it shows up in AT LEAST ONE of these.
UI_SURFACES = [
    REPO_ROOT / "src" / "ui" / "Pages" / "EngineLabPage.cpp",
    REPO_ROOT / "src" / "ui" / "Pages" / "EngineLabPage.h",
    REPO_ROOT / "src" / "ui" / "Variation" / "VariationManager.cpp",
    REPO_ROOT / "src" / "ui" / "Variation" / "VariationManager.h",
    REPO_ROOT / "src" / "ui" / "Variation" / "VariationExpansionGrid.cpp",
    REPO_ROOT / "src" / "ui" / "Pages" / "SampleBrowserPage.cpp",
    REPO_ROOT / "src" / "ui" / "Theme" / "EngineColorManager.h",
]

ENGINE_DECL_RE = re.compile(
    r"class\s+(\w+)\s*:\s*public\s+IVariationEngine", re.MULTILINE
)


def discover_engines() -> list[str]:
    """Return sorted list of class names inheriting from IVariationEngine."""
    if not ENGINES_DIR.exists():
        print(f"ERROR: engines dir not found: {ENGINES_DIR}", file=sys.stderr)
        sys.exit(2)
    names: set[str] = set()
    for header in ENGINES_DIR.glob("*.h"):
        text = header.read_text(encoding="utf-8", errors="replace")
        for m in ENGINE_DECL_RE.finditer(text):
            names.add(m.group(1))
    return sorted(names)


def engine_stem(name: str) -> str:
    """SimpleVariationEngine -> Simple, DDSPNoiseEngine -> DDSPNoise."""
    for suffix in ("VariationEngine", "Engine"):
        if name.endswith(suffix) and name != suffix:
            return name[: -len(suffix)]
    return name


def camel_to_snake(name: str) -> str:
    """DirectMorph -> direct_morph, DDSPNoise -> ddsp_noise."""
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1).lower()


def engine_aliases(name: str) -> set[str]:
    """All tokens a file might use to reference this engine."""
    stem = engine_stem(name)
    return {name, stem, camel_to_snake(stem)}


def file_mentions(path: Path, name: str) -> bool:
    """True if the file mentions the engine by any known alias."""
    if not path.exists():
        return False
    text = path.read_text(encoding="utf-8", errors="replace")
    for alias in engine_aliases(name):
        if re.search(rf"\b{re.escape(alias)}\b", text):
            return True
    return False


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
    engines = discover_engines()
    allow = load_allowlist()

    results = []
    missing_backend: list[str] = []
    missing_ui: list[str] = []

    for name in engines:
        in_backend = file_mentions(GENERATOR_SERVICE, name)
        ui_hits = [p.name for p in UI_SURFACES if file_mentions(p, name)]
        in_ui = len(ui_hits) > 0
        status = "OK" if (in_backend and in_ui) else "DRIFT"
        if name in allow:
            status = "ALLOWED"

        results.append({
            "name": name,
            "backend": in_backend,
            "ui_surfaces": ui_hits,
            "status": status,
        })

        if status == "DRIFT":
            if not in_backend:
                missing_backend.append(name)
            if not in_ui:
                missing_ui.append(name)

    return {
        "engines_found": len(engines),
        "results": results,
        "missing_backend": missing_backend,
        "missing_ui": missing_ui,
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Engine Registry Sync")
    print("=" * 70)
    print(f"Engines discovered: {report['engines_found']}")
    print()
    print(f"{'ENGINE':<32} {'BACKEND':<10} {'UI':<6} STATUS")
    print("-" * 70)
    for r in report["results"]:
        be = "yes" if r["backend"] else "NO"
        ui = f"{len(r['ui_surfaces'])}" if r["ui_surfaces"] else "NO"
        print(f"{r['name']:<32} {be:<10} {ui:<6} {r['status']}")
    print()

    if report["missing_backend"]:
        print(f"[MISSING BACKEND] {len(report['missing_backend'])} engine(s) not in GeneratorService.cpp:")
        for n in report["missing_backend"]:
            print(f"  - {n}")
        print()

    if report["missing_ui"]:
        print(f"[MISSING UI] {len(report['missing_ui'])} engine(s) not in any UI surface:")
        for n in report["missing_ui"]:
            print(f"  - {n}")
        print("  UI surfaces checked:")
        for p in UI_SURFACES:
            print(f"    - {p.relative_to(REPO_ROOT).as_posix()}")
        print()

    if not report["missing_backend"] and not report["missing_ui"]:
        print("OK: every engine is wired into backend AND UI.")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 engine registry linter")
    ap.add_argument("--json", type=Path, help="Write JSON report")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    report = analyze()
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON: {args.json}")

    return 1 if (report["missing_backend"] or report["missing_ui"]) else 0


if __name__ == "__main__":
    sys.exit(main())
