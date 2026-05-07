#!/usr/bin/env python3
"""
Cleanup Old Sessions for Claude Code
======================================
Removes old workflow session folders to prevent disk bloat.

Can be run manually or as a scheduled task.

Usage:
  python .claude/hooks/cleanup-old-sessions.py [--dry-run] [--days N]

Options:
  --dry-run   Show what would be deleted without deleting
  --days N    Delete sessions older than N days (default: 7)
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path
from datetime import datetime, timedelta


# Default retention period
DEFAULT_RETENTION_DAYS = 7

# Sessions directory
SESSIONS_DIR = Path(".claude/workflow/sessions")


def get_session_age(session_path):
    """Get the age of a session in days."""
    try:
        # Check manifest.json for started timestamp
        manifest_path = session_path / "manifest.json"
        if manifest_path.exists():
            with open(manifest_path, 'r') as f:
                manifest = json.load(f)
            started = manifest.get('started', '')
            if started:
                started_time = datetime.fromisoformat(started.replace('Z', '+00:00'))
                age = datetime.now(started_time.tzinfo) - started_time
                return age.days

        # Fallback: use directory modification time
        mtime = session_path.stat().st_mtime
        age = datetime.now() - datetime.fromtimestamp(mtime)
        return age.days

    except Exception:
        # If we can't determine age, return 0 (don't delete)
        return 0


def get_session_status(session_path):
    """Get the status of a session."""
    try:
        manifest_path = session_path / "manifest.json"
        if manifest_path.exists():
            with open(manifest_path, 'r') as f:
                manifest = json.load(f)
            return manifest.get('status', 'unknown')
        return 'no_manifest'
    except Exception:
        return 'error'


def list_sessions():
    """List all sessions with their age and status."""
    if not SESSIONS_DIR.exists():
        print("No sessions directory found")
        return []

    sessions = []
    for session_path in SESSIONS_DIR.iterdir():
        if session_path.is_dir():
            age_days = get_session_age(session_path)
            status = get_session_status(session_path)
            sessions.append({
                'path': session_path,
                'name': session_path.name,
                'age_days': age_days,
                'status': status
            })

    return sorted(sessions, key=lambda x: x['age_days'], reverse=True)


def cleanup_sessions(max_age_days, dry_run=False):
    """Clean up sessions older than max_age_days."""
    sessions = list_sessions()

    if not sessions:
        print("No sessions found")
        return 0

    deleted_count = 0
    kept_count = 0

    print(f"\n{'SESSION':<50} {'AGE':<10} {'STATUS':<15} {'ACTION':<10}")
    print("=" * 85)

    for session in sessions:
        age = session['age_days']
        status = session['status']
        name = session['name']

        # Determine action
        if age > max_age_days:
            action = "DELETE"
            deleted_count += 1
        elif status == 'in_progress' and age > max_age_days * 2:
            # Stale in-progress sessions (double the threshold)
            action = "DELETE"
            deleted_count += 1
        else:
            action = "KEEP"
            kept_count += 1

        print(f"{name:<50} {age:>3}d       {status:<15} {action:<10}")

        if action == "DELETE" and not dry_run:
            try:
                shutil.rmtree(session['path'])
            except Exception as e:
                print(f"  ERROR: Failed to delete: {e}")

    print("=" * 85)
    print(f"\nSummary:")
    print(f"  Kept: {kept_count}")
    print(f"  Deleted: {deleted_count}" + (" (dry run)" if dry_run else ""))

    return deleted_count


def main():
    parser = argparse.ArgumentParser(description="Clean up old workflow sessions")
    parser.add_argument('--dry-run', action='store_true', help="Show what would be deleted")
    parser.add_argument('--days', type=int, default=DEFAULT_RETENTION_DAYS,
                        help=f"Delete sessions older than N days (default: {DEFAULT_RETENTION_DAYS})")
    parser.add_argument('--list', action='store_true', help="Just list sessions, don't delete")

    args = parser.parse_args()

    if args.list:
        sessions = list_sessions()
        print(f"\n{'SESSION':<50} {'AGE':<10} {'STATUS':<15}")
        print("=" * 75)
        for session in sessions:
            print(f"{session['name']:<50} {session['age_days']:>3}d       {session['status']:<15}")
        print(f"\nTotal: {len(sessions)} sessions")
        return 0

    print(f"Cleaning up sessions older than {args.days} days...")
    if args.dry_run:
        print("(DRY RUN - no files will be deleted)\n")

    deleted = cleanup_sessions(args.days, dry_run=args.dry_run)

    return 0 if deleted >= 0 else 1


if __name__ == "__main__":
    sys.exit(main())
