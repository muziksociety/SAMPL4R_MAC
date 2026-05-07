#!/usr/bin/env python3
"""
SM5 Release Linter — Check #4: Event bus topic sync.

SM5 uses a pub/sub EventBus. Code that publishes a topic nobody subscribes to
(or subscribes to a topic nobody publishes) is silent drift — it compiles fine
and never fires. This check detects both cases.

Strategy:
  - Scan all .cpp/.h under src/ (excluding EventBus.cpp itself, which defines
    the generic subscribe/publish signatures).
  - Extract every string-literal first argument to publish(...) / subscribe(...).
  - Report topics published-only (no subscribers) and subscribed-only (no publishers).

Usage:
    python tools/release_linter/check_events.py
    python tools/release_linter/check_events.py --json report.json

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
ALLOWLIST_FILE = Path(__file__).resolve().parent / "intentional_orphan_events.txt"

# Exclude EventBus implementation itself — it defines the generic signatures.
EXCLUDE_FILES = {
    REPO_ROOT / "src" / "app" / "EventBus.cpp",
    REPO_ROOT / "src" / "app" / "EventBus.h",
}

PUBLISH_RE = re.compile(r'\bpublish\s*\(\s*"([^"]+)"')
SUBSCRIBE_RE = re.compile(r'\bsubscribe\s*\(\s*"([^"]+)"')


def load_allowlist() -> set[str]:
    if not ALLOWLIST_FILE.exists():
        return set()
    out: set[str] = set()
    for line in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


def scan() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """Return (topic -> list[file] publishers, topic -> list[file] subscribers)."""
    pubs: dict[str, list[str]] = {}
    subs: dict[str, list[str]] = {}

    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        if path.resolve() in {p.resolve() for p in EXCLUDE_FILES}:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        rel = path.relative_to(REPO_ROOT).as_posix()
        for m in PUBLISH_RE.finditer(text):
            pubs.setdefault(m.group(1), []).append(rel)
        for m in SUBSCRIBE_RE.finditer(text):
            subs.setdefault(m.group(1), []).append(rel)
    return pubs, subs


def analyze() -> dict:
    pubs, subs = scan()
    allow = load_allowlist()

    published_only = sorted(t for t in pubs if t not in subs and t not in allow)
    subscribed_only = sorted(t for t in subs if t not in pubs and t not in allow)
    matched = sorted(t for t in pubs if t in subs)

    return {
        "publishers_total": len(pubs),
        "subscribers_total": len(subs),
        "matched_topics": matched,
        "published_only": published_only,
        "subscribed_only": subscribed_only,
        "publisher_files": {t: sorted(set(pubs[t])) for t in published_only},
        "subscriber_files": {t: sorted(set(subs[t])) for t in subscribed_only},
    }


def print_report(report: dict) -> None:
    print("=" * 70)
    print("SM5 RELEASE LINTER — Event Bus Topic Sync")
    print("=" * 70)
    print(f"Unique topics published:  {report['publishers_total']}")
    print(f"Unique topics subscribed: {report['subscribers_total']}")
    print(f"Matched (pub + sub):      {len(report['matched_topics'])}")
    print()

    if report["published_only"]:
        print(f"[PUBLISHED-ONLY] {len(report['published_only'])} topic(s) with no subscribers:")
        for t in report["published_only"]:
            print(f"  - {t}")
            for f in report["publisher_files"][t]:
                print(f"      published in {f}")
        print()

    if report["subscribed_only"]:
        print(f"[SUBSCRIBED-ONLY] {len(report['subscribed_only'])} topic(s) with no publishers:")
        for t in report["subscribed_only"]:
            print(f"  - {t}")
            for f in report["subscriber_files"][t]:
                print(f"      subscribed in {f}")
        print()

    if not report["published_only"] and not report["subscribed_only"]:
        print("OK: every topic has at least one publisher and one subscriber.")
    print("=" * 70)


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 event bus topic linter")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    report = analyze()
    if not args.quiet:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON: {args.json}")

    return 1 if (report["published_only"] or report["subscribed_only"]) else 0


if __name__ == "__main__":
    sys.exit(main())
