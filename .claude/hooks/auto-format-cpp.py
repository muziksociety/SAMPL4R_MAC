#!/usr/bin/env python3
"""
Auto-Format C++ Hook for Claude Code
======================================
Runs clang-format on C++ files after they are written/edited.

This is a PostToolUse hook - runs AFTER Write/Edit completes.

Exit codes:
  0 = Success (formatted or not applicable)
  1 = Warning (clang-format not found, file still written)
  2 = Error (should not block, just warn)

Environment variables used:
  CLAUDE_TOOL_ARGS - JSON string with tool arguments (file_path, etc.)
"""

import json
import os
import subprocess
import sys
from pathlib import Path


def get_target_file():
    """Get the target file path from tool arguments."""
    tool_args = os.environ.get('CLAUDE_TOOL_ARGS', '{}')

    try:
        args = json.loads(tool_args)
        return args.get('file_path', args.get('path', ''))
    except json.JSONDecodeError:
        return ''


def is_cpp_file(file_path):
    """Check if the file is a C++ file."""
    if not file_path:
        return False

    path = Path(file_path)
    cpp_extensions = {'.cpp', '.h', '.hpp', '.cc', '.cxx', '.c'}

    return path.suffix.lower() in cpp_extensions


def format_file(file_path):
    """Run clang-format on the file."""
    try:
        # Check if clang-format is available
        result = subprocess.run(
            ['clang-format', '--version'],
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            print("WARNING: clang-format not found, skipping auto-format", file=sys.stderr)
            return True  # Don't block

        # Run clang-format in-place
        result = subprocess.run(
            ['clang-format', '-i', file_path],
            capture_output=True,
            text=True
        )

        if result.returncode == 0:
            print(f"AUTO-FORMAT: Formatted {file_path}")
            return True
        else:
            print(f"WARNING: clang-format failed: {result.stderr}", file=sys.stderr)
            return True  # Don't block on format failure

    except FileNotFoundError:
        print("WARNING: clang-format not found, skipping auto-format", file=sys.stderr)
        return True
    except Exception as e:
        print(f"WARNING: Error running clang-format: {e}", file=sys.stderr)
        return True


def main():
    """Main entry point for the hook."""
    target_file = get_target_file()

    if not target_file:
        sys.exit(0)  # No file specified

    if not is_cpp_file(target_file):
        sys.exit(0)  # Not a C++ file

    if not Path(target_file).exists():
        sys.exit(0)  # File doesn't exist (shouldn't happen for PostToolUse)

    format_file(target_file)
    sys.exit(0)  # Always succeed (formatting is optional)


if __name__ == "__main__":
    main()
