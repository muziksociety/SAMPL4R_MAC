# Orchestrate Feature/Fix

Start a new workflow with full agent coordination.

## Task: $ARGUMENTS

## Instructions

You are starting a workflow orchestration. Follow these steps:

### Step 1: Extract Session ID
Look for `[WORKFLOW SESSION: xxx]` in the context above this prompt.
This is your session_id for the entire workflow.

### Step 2: Initialize Workflow
1. Create session folder: `.claude/workflow/sessions/{session_id}/`
2. Create manifest.json with this structure:
```json
{
  "session_id": "{session_id}",
  "task": "$ARGUMENTS",
  "enhanced_task": "",
  "started": "{ISO-timestamp}",
  "status": "in_progress",
  "current_phase": "prompt-enhancement",
  "phases": {
    "prompt-enhancement": { "status": "in_progress" },
    "investigation": { "status": "pending" },
    "design": { "status": "pending" },
    "user-approval": { "status": "pending" },
    "implementation": { "status": "pending" },
    "build": { "status": "pending" },
    "verification": { "status": "pending" },
    "documentation": { "status": "pending" }
  },
  "project_path": "{current_working_directory}"
}
```

### Step 3: Spawn Orchestrator Agent
Use the Task tool to spawn the orchestrator agent with the full task context:

```
Task(subagent_type="orchestrator", prompt="""
SESSION_ID: {session_id}
WORKFLOW_DIR: .claude/workflow/sessions/{session_id}/
TASK: $ARGUMENTS

Execute the full orchestration workflow:
1. Phase 0.5: Enhance the user's prompt into a detailed specification
2. Phase 1-3: Spawn investigation agents (parallel)
3. Phase 4: Spawn architect + risk-analyst (parallel)
4. Phase 5: Present findings and wait for user approval
5. Phase 6: Execute implementation pipeline

Follow all protocols in your agent definition.
""")
```

### Important Notes
- The orchestrator will first enhance the user's raw request into a detailed specification
- User must approve the enhanced specification before investigation begins
- User must approve the design before implementation begins
- All artifacts are saved to the session folder for audit trail
- Use `/workflow-status` to check progress at any time
