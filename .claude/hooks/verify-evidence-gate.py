#!/usr/bin/env python3
"""
Evidence Gate Hook for Claude Code
====================================
Blocks design agents (architect, frontend-architect) from starting
without verified evidence.

Exit codes:
  0 = Allow operation
  1 = Error (allow operation, show warning)
  2 = Block operation

Environment variables used:
  CLAUDE_SESSION_ID - Current workflow session ID
"""

import json
import os
import sys
from pathlib import Path


def get_workflow_dir():
    """Get the workflow directory from environment or find it."""
    session_id = os.environ.get('CLAUDE_SESSION_ID', '')

    if session_id:
        return Path(f".claude/workflow/sessions/{session_id}")

    # Fallback: find most recent session
    sessions_dir = Path(".claude/workflow/sessions")
    if sessions_dir.exists():
        sessions = sorted(sessions_dir.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True)
        if sessions:
            return sessions[0]

    return None


def verify_evidence():
    """Verify evidence-report.json exists with verified findings."""
    workflow_dir = get_workflow_dir()

    if not workflow_dir:
        print("WARNING: Cannot determine workflow directory", file=sys.stderr)
        return True  # Allow operation but warn

    evidence_path = workflow_dir / "evidence-report.json"

    # Check evidence-report.json exists
    if not evidence_path.exists():
        print(f"EVIDENCE GATE BLOCKED: evidence-report.json does not exist", file=sys.stderr)
        print(f"  Expected at: {evidence_path}", file=sys.stderr)
        print(f"  Run evidence-gatherer agent first", file=sys.stderr)
        return False

    # Check evidence quality
    try:
        with open(evidence_path, 'r') as f:
            evidence = json.load(f)

        findings = evidence.get('data', {}).get('findings', [])
        if not findings:
            findings = evidence.get('findings', [])

        unverified = []
        missing_lines = []

        for finding in findings:
            if not finding.get('verified', False):
                unverified.append(finding.get('file', 'unknown'))
            if finding.get('line') == 'unknown' or finding.get('line') is None:
                missing_lines.append(finding.get('file', 'unknown'))

        if unverified:
            print(f"EVIDENCE GATE BLOCKED: Unverified findings:", file=sys.stderr)
            for f in unverified[:5]:  # Show first 5
                print(f"  - {f}", file=sys.stderr)
            return False

        if missing_lines:
            print(f"EVIDENCE GATE BLOCKED: Findings with unknown line numbers:", file=sys.stderr)
            for f in missing_lines[:5]:
                print(f"  - {f}", file=sys.stderr)
            return False

        return True

    except json.JSONDecodeError:
        print(f"EVIDENCE GATE BLOCKED: Invalid JSON in evidence-report.json", file=sys.stderr)
        return False
    except Exception as e:
        print(f"WARNING: Error reading evidence: {e}", file=sys.stderr)
        return True  # Allow on error to not block workflow


def main():
    """Main entry point for the hook."""
    if verify_evidence():
        print("EVIDENCE GATE: Passed - verified evidence exists")
        sys.exit(0)  # Allow operation
    else:
        sys.exit(2)  # Block operation


if __name__ == "__main__":
    main()
