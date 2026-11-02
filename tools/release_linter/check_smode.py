#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #2: S-Mode service + event wiring.

Replaces SM5's variation-engine registry check (SAMPLAR has no engines; its
product is S-Mode: separate a song into stems, slice, map to MIDI, play).

  1. Registry lookups: every ServiceRegistry lookup by name in src/
     (`->get("name")` / `.get("name")`) resolves to a name the VST3 registers in
     src/plugin/SamplarAudioProcessor.cpp (a typo = nullptr at runtime).
  2. Required services: "smode", "midi.mapping", "license" are registered.
  3. S-Mode event chain: each required topic has >= 1 publisher AND >= 1 subscriber
     (separation progress/complete/error, workflow ready/error, segment
     triggered/stopped, transport stopped, downbeat detected).
  4. S-Mode sources are compiled: the core S-Mode / separation .cpp files are
     referenced by a CMake file.

Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys

from linter_common import REPO_ROOT, cmake_source_refs, common_args

CODE_ROOT = REPO_ROOT / "src"
PLUGIN_SITE = REPO_ROOT / "src" / "plugin" / "SamplarAudioProcessor.cpp"

REQUIRED_SERVICES = ["smode", "midi.mapping", "license"]
REQUIRED_TOPICS = [
    "smode.separation.progress",
    "smode.separation.complete",
    "smode.separation.error",
    "smode.workflow.ready",
    "smode.workflow.error",
    "smode.segment.triggered",
    "smode.segment.stopped",
    "smode.transport.stopped",
    "smode.downbeat.detected",
]
REQUIRED_SOURCES = [
    "src/services/smode/SModeService.cpp",
    "src/services/smode/SModeSeparationService.cpp",
    "src/services/smode/SModeSegmentModel.cpp",
    "src/services/FullSongService.cpp",
    "src/services/separation/OnnxSeparationProvider.cpp",
    "src/services/separation/HtdemucsIstft.cpp",
    "src/services/MidiMappingService.cpp",
    "src/ui/Pages/SModePage.cpp",
]

REGISTER_RE = re.compile(r'registerService\s*\(\s*"([^"]+)"')
LOOKUP_RE = re.compile(r'(?:->|\.)\s*get\s*\(\s*"([a-z][a-z0-9_.]*)"\s*\)')
PUBLISH_RE = re.compile(r'\bpublish\s*\(\s*"([^"]+)"')
SUBSCRIBE_RE = re.compile(r'\bsubscribe\s*\(\s*"([^"]+)"')


def analyze() -> dict:
    plugin_text = PLUGIN_SITE.read_text(encoding="utf-8", errors="replace") if PLUGIN_SITE.exists() else ""
    registered = sorted(set(REGISTER_RE.findall(plugin_text)))

    lookups: dict[str, set[str]] = {}
    pubs: set[str] = set()
    subs: set[str] = set()
    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        r = path.relative_to(REPO_ROOT).as_posix()
        for m in LOOKUP_RE.finditer(text):
            lookups.setdefault(m.group(1), set()).add(r)
        pubs |= set(PUBLISH_RE.findall(text))
        subs |= set(SUBSCRIBE_RE.findall(text))

    refs = cmake_source_refs()
    missing: list[str] = []
    unresolved = {name: sorted(files) for name, files in lookups.items() if name not in registered}
    for name, files in sorted(unresolved.items()):
        missing.append(f"lookup of unregistered service '{name}' ({', '.join(files)})")
    for name in REQUIRED_SERVICES:
        if name not in registered:
            missing.append(f"required service '{name}' not registered in SamplarAudioProcessor.cpp")
    topics = []
    for t in REQUIRED_TOPICS:
        ok_pub, ok_sub = t in pubs, t in subs
        topics.append({"topic": t, "published": ok_pub, "subscribed": ok_sub})
        if not (ok_pub and ok_sub):
            missing.append(f"topic '{t}' " + ("has no subscriber" if ok_pub else "has no publisher" if ok_sub else "is gone"))
    for s in REQUIRED_SOURCES:
        if s not in refs:
            missing.append(f"source not compiled by any CMake file: {s}")
        elif not (REPO_ROOT / s).exists():
            missing.append(f"source missing on disk: {s}")

    return {
        "plugin_registered_services": registered,
        "service_lookups": {k: sorted(v) for k, v in sorted(lookups.items())},
        "topics": topics,
        "required_sources": REQUIRED_SOURCES,
        "missing": missing,
    }


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - S-Mode Service + Event Wiring")
    print("=" * 70)
    print(f"VST3 registry: {', '.join(r['plugin_registered_services'])}")
    print(f"Lookups by name: {', '.join(r['service_lookups']) or '(none)'}")
    for t in r["topics"]:
        mark = "OK " if t["published"] and t["subscribed"] else "!! "
        print(f"  {mark}{t['topic']:<30} pub={'y' if t['published'] else 'N'} sub={'y' if t['subscribed'] else 'N'}")
    print()
    if r["missing"]:
        print(f"[DRIFT] {len(r['missing'])} problem(s):")
        for m in r["missing"]:
            print(f"  - {m}")
    else:
        print("OK: S-Mode services, lookups, event chain and sources are wired.")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR S-Mode wiring linter"))
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
