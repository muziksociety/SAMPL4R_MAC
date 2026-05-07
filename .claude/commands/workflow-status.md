# Workflow Status

Show detailed status of the current workflow session.

## Instructions

### Step 1: Get Current Session
Extract session_id from context: `[WORKFLOW SESSION: xxx]`

### Step 2: Read Session Data
Read `.claude/workflow/sessions/{session_id}/manifest.json`

### Step 3: Check Artifacts
List all files in `.claude/workflow/sessions/{session_id}/`:
- manifest.json (always present)
- enhanced-spec.json (after prompt enhancement)
- evidence-report.json (after investigation)
- integration-checklist.json (after investigation)
- audio-research.json (if applicable)
- risk-analysis.json (after design phase)
- architecture.json (after design phase)
- user-approved.json (after user approval)
- build-result.json (after build)
- verification-result.json (after verification)
- devlog.json (after documentation)

### Step 4: Display Status

```
WORKFLOW STATUS
===============

Session ID: {session_id}
Task: {original task}
Enhanced Task: {enhanced specification summary}
Status: {in_progress|complete|failed}
Current Phase: {current_phase}
Started: {timestamp}

PHASE PROGRESS
--------------
[X] Prompt Enhancement - complete
[X] Investigation - complete
[X] Design - complete
[ ] User Approval - WAITING FOR YOU
[ ] Implementation - pending
[ ] Build - pending
[ ] Verification - pending
[ ] Documentation - pending

ARTIFACTS
---------
[X] manifest.json
[X] enhanced-spec.json
[X] evidence-report.json
[X] integration-checklist.json
[X] risk-analysis.json
[X] architecture.json
[ ] user-approved.json - REQUIRED: Approve design to continue
[ ] build-result.json
[ ] verification-result.json
[ ] devlog.json

NEXT ACTION
-----------
Review the design and risk analysis above.
Reply "yes" to approve and proceed to implementation.
```

### Step 5: Show Blocking Issues (if any)

If workflow is blocked, show:
- What artifact is missing
- Which agent should have created it
- What action is needed to unblock

### If Session Not Found

```
SESSION NOT FOUND
=================

No workflow session found for: {session_id}

The session folder does not exist or was deleted.

Options:
1. Start new workflow: /orchestrate [task]
2. List all sessions: /workflows
```
