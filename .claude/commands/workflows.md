# List Active Workflows

List all workflow sessions in this project.

## Instructions

1. Read all manifest.json files from `.claude/workflow/sessions/*/manifest.json`

2. For each session found, extract:
   - session_id
   - task (original request)
   - current_phase
   - status
   - started timestamp

3. Display as a table:

```
ACTIVE WORKFLOW SESSIONS
========================

| Session ID | Task | Phase | Status | Started |
|------------|------|-------|--------|---------|
| abc123... | Add granular engine | architecture | in_progress | 2024-11-24 14:30 |
| def456... | Fix buffer bug | verification | in_progress | 2024-11-24 15:45 |
| ghi789... | Add reverb | complete | complete | 2024-11-24 12:00 |

Total: 3 sessions (2 active, 1 complete)
```

4. Highlight the current session (match session_id from `[WORKFLOW SESSION: xxx]` in context)

5. Show quick actions:
   - `/workflow-status` - View current session details
   - `/orchestrate [task]` - Start new workflow

## If No Sessions Found

```
NO ACTIVE WORKFLOWS
===================

No workflow sessions found in .claude/workflow/sessions/

Start a new workflow with:
  /orchestrate [your task description]
```
