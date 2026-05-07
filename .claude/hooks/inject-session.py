#!/usr/bin/env python3
"""
Session Injection Hook for VARIA VST Workflow System

This hook runs on every UserPromptSubmit and injects the session ID
into the conversation context. This ensures all agents know which
workflow session they belong to.

Part of the Workflow Artifact System - see 000CLAUDEBIBLE.md
"""
import json
import sys
import os

def main():
    try:
        # Read hook input from stdin
        data = json.load(sys.stdin)

        session_id = data.get("session_id", "unknown")
        cwd = data.get("cwd", "")

        # Check if there's an active workflow for this session
        workflow_dir = os.path.join(cwd, ".claude", "workflow", "sessions", session_id)
        manifest_path = os.path.join(workflow_dir, "manifest.json")

        workflow_info = ""
        if os.path.exists(manifest_path):
            try:
                with open(manifest_path, 'r', encoding='utf-8') as f:
                    manifest = json.load(f)
                    task = manifest.get("task", "unknown")
                    phase = manifest.get("current_phase", "unknown")
                    status = manifest.get("status", "unknown")
                    workflow_info = f' | Task: "{task}" | Phase: {phase} | Status: {status}'
            except Exception as e:
                workflow_info = f' | Error reading manifest: {str(e)}'

        # Build context message
        context_msg = f"[WORKFLOW SESSION: {session_id}{workflow_info}]"

        # Output structured response with additionalContext
        output = {
            "hookSpecificOutput": {
                "hookEventName": "UserPromptSubmit",
                "additionalContext": context_msg
            }
        }

        print(json.dumps(output))
        sys.exit(0)

    except Exception as e:
        # Log error but don't block the conversation
        error_log_path = os.path.join(
            data.get("cwd", "."),
            ".claude", "workflow", "hook-errors.log"
        )
        try:
            with open(error_log_path, 'a', encoding='utf-8') as f:
                f.write(f"inject-session.py error: {str(e)}\n")
        except:
            pass

        # Still output valid JSON so hook doesn't break
        print(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "UserPromptSubmit",
                "additionalContext": "[WORKFLOW SESSION: error-reading-session]"
            }
        }))
        sys.exit(0)

if __name__ == "__main__":
    main()
