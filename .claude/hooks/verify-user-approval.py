#!/usr/bin/env python3
"""
User Approval Hook for Claude Code
====================================
Blocks Write/Edit operations on source files without user approval.

Exit codes:
  0 = Allow operation
  1 = Error (allow operation, show warning)
  2 = Block operation

Environment variables used:
  CLAUDE_SESSION_ID - Current workflow session ID
  CLAUDE_TOOL_ARGS - JSON string with tool arguments (file_path, etc.)
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


def is_source_file(file_path):
    """Check if the file is a source code file that requires approval."""
    if not file_path:
        return False

    path = Path(file_path)

    # Source file extensions that require approval
    source_extensions = {'.cpp', '.h', '.hpp', '.c', '.cmake'}

    # Paths that always require approval
    source_dirs = {'src', 'Source', 'include'}

    # Check extension
    if path.suffix.lower() in source_extensions:
        return True

    # Check path
    for part in path.parts:
        if part in source_dirs:
            return True

    # CMakeLists.txt always requires approval
    if path.name == 'CMakeLists.txt':
        return True

    return False


def get_target_file():
    """Get the target file path from tool arguments."""
    tool_args = os.environ.get('CLAUDE_TOOL_ARGS', '{}')

    try:
        args = json.loads(tool_args)
        return args.get('file_path', args.get('path', ''))
    except json.JSONDecodeError:
        return ''


def verify_approval():
    """Verify user-approved.json exists for source file modifications."""
    target_file = get_target_file()

    # Only check for source files
    if not is_source_file(target_file):
        return True  # Non-source files don't need approval

    workflow_dir = get_workflow_dir()

    if not workflow_dir:
        # No workflow session - this might be a quick fix outside workflow
        print("WARNING: No workflow session detected", file=sys.stderr)
        print("  Operating outside orchestrated workflow", file=sys.stderr)
        return True  # Allow but warn

    approval_path = workflow_dir / "user-approved.json"

    if not approval_path.exists():
        print(f"USER APPROVAL BLOCKED: Cannot modify source file without approval", file=sys.stderr)
        print(f"  File: {target_file}", file=sys.stderr)
        print(f"  Missing: {approval_path}", file=sys.stderr)
        print(f"  Action: Complete design phase and get user approval first", file=sys.stderr)
        return False

    try:
        with open(approval_path, 'r') as f:
            approval = json.load(f)

        if approval.get('approved_by') == 'user':
            return True
        else:
            print(f"USER APPROVAL BLOCKED: Invalid approval record", file=sys.stderr)
            return False

    except Exception as e:
        print(f"WARNING: Error reading approval: {e}", file=sys.stderr)
        return True  # Allow on error


def main():
    """Main entry point for the hook."""
    if verify_approval():
        sys.exit(0)  # Allow operation
    else:
        sys.exit(2)  # Block operation


if __name__ == "__main__":
    main()
