#!/usr/bin/env python3
"""
SAMPLAR Release Linter - Check #4: EventBus topic sync.

A topic that is published but never subscribed (or subscribed but never
published) compiles fine and silently does nothing. Scans src/**/*.cpp|h for
string-literal topics passed to publish(...) / subscribe(...) (including the
LicenseSystemService subscribe() wrapper) and reports one-sided topics that are
not in intentional_orphan_events.txt. Allowlist entries that no longer match a
one-sided topic are reported as INFO (prune them).

Exit codes: 0 clean, 1 drift, 2 tool error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys

from linter_common import REPO_ROOT, common_args, load_allowlist

CODE_ROOT = REPO_ROOT / "src"
EXCLUDE = {"src/app/EventBus.cpp", "src/app/EventBus.h"}
PUBLISH_RE = re.compile(r'\bpublish\s*\(\s*"([^"]+)"')
SUBSCRIBE_RE = re.compile(r'\bsubscribe\s*\(\s*"([^"]+)"')


def scan() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    pubs: dict[str, list[str]] = {}
    subs: dict[str, list[str]] = {}
    for path in CODE_ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        r = path.relative_to(REPO_ROOT).as_posix()
        if r in EXCLUDE:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in PUBLISH_RE.finditer(text):
            pubs.setdefault(m.group(1), []).append(r)
        for m in SUBSCRIBE_RE.finditer(text):
            subs.setdefault(m.group(1), []).append(r)
    return pubs, subs


def analyze() -> dict:
    pubs, subs = scan()
    allow = load_allowlist("intentional_orphan_events.txt")
    one_sided = {t for t in pubs if t not in subs} | {t for t in subs if t not in pubs}
    published_only = sorted(t for t in pubs if t not in subs and t not in allow)
    subscribed_only = sorted(t for t in subs if t not in pubs and t not in allow)
    return {
        "publishers_total": len(pubs),
        "subscribers_total": len(subs),
        "matched_topics": sorted(t for t in pubs if t in subs),
        "published_only": published_only,
        "subscribed_only": subscribed_only,
        "allowed": sorted(one_sided & allow),
        "stale_allowlist": sorted(allow - one_sided),
        "publisher_files": {t: sorted(set(pubs[t])) for t in published_only},
        "subscriber_files": {t: sorted(set(subs[t])) for t in subscribed_only},
    }


def print_report(r: dict) -> None:
    print("=" * 70)
    print("SAMPLAR RELEASE LINTER - EventBus Topic Sync")
    print("=" * 70)
    print(f"Topics published / subscribed / matched: {r['publishers_total']} / "
          f"{r['subscribers_total']} / {len(r['matched_topics'])}")
    print()
    for key, label, files in (("published_only", "PUBLISHED-ONLY (no subscriber)", "publisher_files"),
                              ("subscribed_only", "SUBSCRIBED-ONLY (no publisher)", "subscriber_files")):
        if r[key]:
            print(f"[{label}] {len(r[key])} topic(s):")
            for t in r[key]:
                print(f"  - {t}   ({', '.join(r[files][t])})")
            print()
    if r["allowed"]:
        print(f"[ALLOWED] {len(r['allowed'])} one-sided topic(s) in intentional_orphan_events.txt: {', '.join(r['allowed'])}")
    if r["stale_allowlist"]:
        print(f"[INFO] stale allowlist entries (prune): {', '.join(r['stale_allowlist'])}")
    if not r["published_only"] and not r["subscribed_only"]:
        print("OK: every topic has a publisher and a subscriber (or is allowlisted).")
    print("=" * 70)


def main() -> int:
    ap = common_args(argparse.ArgumentParser(description="SAMPLAR EventBus topic linter"))
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
    return 1 if (report["published_only"] or report["subscribed_only"]) else 0


if __name__ == "__main__":
    sys.exit(main())
