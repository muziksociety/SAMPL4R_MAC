#!/usr/bin/env python3
"""
Remind Yourself Hook for Claude Code
======================================
Reminds Haiku agents to check their working memory before operations.

This is a PreToolUse hook - runs BEFORE Read/Grep/Glob operations.

Exit codes:
  0 = Allow operation (with reminder printed)
  1 = Warning (allow operation)
  2 = Block operation (not used here - just reminds)

Environment variables used:
  CLAUDE_SESSION_ID - Current workflow session ID
  CLAUDE_AGENT_NAME - Name of the current agent
  CLAUDE_TOOL_NAME - Name of the tool being called
"""

import json
import os
import sys
from pathlib import Path
from datetime import datetime, timedelta


# Memory staleness threshold (hours)
MEMORY_STALE_HOURS = 24


def get_session_id():
    """Get the session ID from environment."""
    return os.environ.get('CLAUDE_SESSION_ID', '')


def get_agent_name():
    """Get the agent name from environment."""
    return os.environ.get('CLAUDE_AGENT_NAME', 'unknown')


def get_memory_path(session_id, agent_name):
    """Get the path to the agent's memory file."""
    if not session_id:
        return None
    return Path(f".claude/workflow/sessions/{session_id}/memory/{agent_name}-memory.json")


def ensure_memory_dir(session_id):
    """Ensure the memory directory exists."""
    if not session_id:
        return
    memory_dir = Path(f".claude/workflow/sessions/{session_id}/memory")
    memory_dir.mkdir(parents=True, exist_ok=True)


def initialize_memory(memory_path, agent_name, session_id):
    """Initialize an empty memory file."""
    memory = {
        "session_id": session_id,
        "agent": agent_name,
        "last_updated": datetime.now().isoformat(),
        "turn_count": 0,
        "task_context": {
            "original_task": "",
            "current_focus": "",
            "success_criteria": []
        },
        "discovered_facts": {
            "key_files": {},
            "key_patterns": {},
            "key_names": {
                "class_names": [],
                "parameter_ids": [],
                "function_names": []
            }
        },
        "progress": {
            "completed": [],
            "in_progress": "",
            "blocked": None,
            "next_steps": []
        },
        "warnings": []
    }

    memory_path.parent.mkdir(parents=True, exist_ok=True)
    with open(memory_path, 'w') as f:
        json.dump(memory, f, indent=2)

    return memory


def load_memory(memory_path):
    """Load the memory file."""
    try:
        with open(memory_path, 'r') as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def validate_memory_session(memory, expected_session_id):
    """Validate that memory belongs to current session."""
    if not memory:
        return True, None

    stored_session = memory.get('session_id', '')

    if stored_session != expected_session_id:
        return False, f"Memory is from session '{stored_session}', but current session is '{expected_session_id}'"

    return True, None


def check_memory_staleness(memory):
    """Check if memory is stale (too old)."""
    if not memory:
        return False, None

    last_updated = memory.get('last_updated', '')
    if not last_updated:
        return False, None

    try:
        updated_time = datetime.fromisoformat(last_updated)
        age = datetime.now() - updated_time
        hours_old = age.total_seconds() / 3600

        if hours_old > MEMORY_STALE_HOURS:
            return True, f"Memory is {hours_old:.1f} hours old (threshold: {MEMORY_STALE_HOURS}h)"

        return False, None
    except ValueError:
        return False, None


def reset_memory(memory_path, agent_name, session_id, reason):
    """Reset memory file with fresh state."""
    print(f"MEMORY RESET: {reason}")
    print(f"  Creating fresh memory for session: {session_id}")
    return initialize_memory(memory_path, agent_name, session_id)


def print_memory_reminder(memory):
    """Print a reminder of what's in memory."""
    if not memory:
        print("REMIND YOURSELF: No memory file found - this is your first operation")
        return

    print("=" * 60)
    print("REMIND YOURSELF: Check your working memory!")
    print("=" * 60)

    # Task context
    task_context = memory.get('task_context', {})
    if task_context.get('current_focus'):
        print(f"\nCURRENT FOCUS: {task_context['current_focus']}")

    # Key files already discovered
    key_files = memory.get('discovered_facts', {}).get('key_files', {})
    if key_files:
        print(f"\nKEY FILES ALREADY FOUND ({len(key_files)}):")
        for name, path in list(key_files.items())[:5]:  # Show first 5
            print(f"  - {name}: {path}")
        if len(key_files) > 5:
            print(f"  ... and {len(key_files) - 5} more")

    # Key names to remember
    key_names = memory.get('discovered_facts', {}).get('key_names', {})
    class_names = key_names.get('class_names', [])
    if class_names:
        print(f"\nKEY CLASS NAMES: {', '.join(class_names[:5])}")

    # Progress
    progress = memory.get('progress', {})
    if progress.get('in_progress'):
        print(f"\nIN PROGRESS: {progress['in_progress']}")

    completed = progress.get('completed', [])
    if completed:
        print(f"\nCOMPLETED ({len(completed)} items):")
        for item in completed[-3:]:  # Show last 3
            print(f"  - {item}")

    # Warnings
    warnings = memory.get('warnings', [])
    if warnings:
        print(f"\nWARNINGS TO REMEMBER:")
        for warning in warnings[:3]:
            print(f"  ! {warning}")

    print("\n" + "=" * 60)
    print("Before searching, check if the answer is already in your memory!")
    print("=" * 60)


def main():
    """Main entry point for the hook."""
    session_id = get_session_id()
    agent_name = get_agent_name()

    if not session_id:
        # No session - skip reminder
        sys.exit(0)

    # Only apply to Haiku agents
    haiku_agents = ['evidence-gatherer', 'integration-supervisor', 'build-deployment', 'documentation']
    if agent_name not in haiku_agents:
        sys.exit(0)

    memory_path = get_memory_path(session_id, agent_name)

    if not memory_path:
        sys.exit(0)

    # Load or initialize memory
    memory = load_memory(memory_path)

    if not memory:
        # No memory exists - create fresh
        ensure_memory_dir(session_id)
        memory = initialize_memory(memory_path, agent_name, session_id)
        print(f"REMIND YOURSELF: Initialized working memory at {memory_path}")
        sys.exit(0)

    # VALIDATION 1: Check session matches
    session_valid, session_error = validate_memory_session(memory, session_id)
    if not session_valid:
        # Memory is from different session - RESET IT
        memory = reset_memory(memory_path, agent_name, session_id, session_error)
        sys.exit(0)

    # VALIDATION 2: Check staleness
    is_stale, stale_reason = check_memory_staleness(memory)
    if is_stale:
        # Memory is too old - RESET IT
        memory = reset_memory(memory_path, agent_name, session_id, stale_reason)
        sys.exit(0)

    # Memory is valid - increment turn count and print reminder
    memory['turn_count'] = memory.get('turn_count', 0) + 1
    memory['last_updated'] = datetime.now().isoformat()

    with open(memory_path, 'w') as f:
        json.dump(memory, f, indent=2)

    # Print reminder with session info
    print(f"SESSION: {session_id}")
    print_memory_reminder(memory)

    sys.exit(0)  # Always allow the operation


if __name__ == "__main__":
    main()
