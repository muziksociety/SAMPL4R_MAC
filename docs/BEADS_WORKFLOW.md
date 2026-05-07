# Beads + SAMPLAR Integrated Workflow

## Overview

This project uses a hybrid workflow combining:
- **Beads (`bd`)**: Git-backed issue tracker with dependency tracking
- **SAMPLAR Workflow Sessions**: 8-phase development workflow with quality gates

## Quick Commands

```bash
# Binary location
b:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\beads-bin\bd.exe

# Or add to PATH and use: bd

# View ready work (unblocked tasks)
bd ready

# List all issues
bd list

# Create new issue
bd create "Title" -d "Description" -l "label1,label2"

# View dependency tree
bd dep tree <issue-id>

# Close issue
bd close <issue-id>
```

## Workflow Integration

### Task Hierarchy

```
BEADS (Task Execution Layer)
├── Epic: Sample_monsta5-xxx
│   ├── Task: Sample_monsta5-yyy
│   ├── Task: Sample_monsta5-zzz
│   └── ...
│
SAMPLAR SESSIONS (Artifact/Gate Layer)
├── .claude/workflow/sessions/<session-id>/
│   ├── manifest.json (phases, status)
│   ├── evidence-report.json
│   ├── integration-checklist.json
│   ├── frontend-design.json
│   └── architecture.json
```

### Creating a New Feature

1. **Create Beads Epic**
   ```bash
   bd create "Feature Name" -d "Description" -l "epic" -p P1
   ```

2. **Break Down into Tasks**
   ```bash
   bd create "Subtask 1" -d "Description"
   bd create "Subtask 2" -d "Description"
   ```

3. **Link Dependencies**
   ```bash
   bd dep add <subtask-id> <epic-id> -t parent-child
   bd dep add <task-2> <task-1> -t blocks  # task-2 blocks task-1
   ```

4. **Start SAMPLAR Workflow Session**
   Use `/orchestrate` command with epic reference:
   ```
   /orchestrate <task-description> [beads: Sample_monsta5-xxx]
   ```

5. **Check Ready Work**
   ```bash
   bd ready  # Shows unblocked tasks
   ```

### Mapping Beads to SAMPLAR Phases

| Beads Status | SAMPLAR Phase | Description |
|--------------|-----------|-------------|
| open | prompt-enhancement | Task created, planning |
| open + assigned | investigation | Evidence gathering |
| open + designed | design | Architecture complete |
| in_progress | implementation | Actively coding |
| in_review | verification | Testing |
| closed | documentation | Completed |

### Quality Gates (SAMPLAR)

Each SAMPLAR session enforces these gates:

1. **Evidence Gate**: No design without verified evidence
2. **Logging Gate**: No implementation without logging plan
3. **Log Evidence Gate**: No bug fix without log analysis

### Commands Reference

#### Issue Management
```bash
bd create "Title" -d "Desc" -l "labels" -p P1-P4
bd list                          # All issues
bd list -s open                  # Open only
bd show <id>                     # Details
bd close <id>                    # Close issue
```

#### Dependencies
```bash
bd dep add <from> <to> -t blocks       # Blocking dependency
bd dep add <child> <parent> -t parent-child  # Parent-child
bd dep tree <id>                       # View tree
bd dep cycles                          # Check for cycles
```

#### Workflow
```bash
bd ready                         # Unblocked work
bd onboard                       # Project overview (for new sessions)
bd graph                         # Dependency visualization
```

## Current Issues

```bash
bd ready  # Run this to see current ready work
```

## File Locations

- **Beads binary**: `beads-bin/bd.exe`
- **Database**: `.beads/beads.db`
- **Issue export**: `.beads/issues.jsonl`
- **Config**: `.beads/config.yaml`
- **SAMPLAR sessions**: `.claude/workflow/sessions/`

## Integration with Orchestrator

The orchestrator has a **Beads Gate** that runs automatically when you use `/orchestrate`:

### Unified UX Flow

```
USER: /orchestrate
         │
         ▼
┌─────────────────────────────────────────┐
│  BEADS GATE (automatic)                 │
├─────────────────────────────────────────┤
│  1. Run: bd ready                       │
│  2. Display unblocked tasks             │
│  3. User selects task OR enters new one │
│  4. Create Beads issue if new           │
│  5. Link beads_id to session            │
└─────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│  SAMPLAR WORKFLOW (8 phases)            │
├─────────────────────────────────────────┤
│  → Prompt Enhancement                   │
│  → Investigation (Evidence Gate)        │
│  → Design (Dual-Track)                  │
│  → User Approval                        │
│  → Implementation (Logging Gate)        │
│  → Build                                │
│  → Verification                         │
│  → Documentation                        │
└─────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────┐
│  COMPLETION (automatic)                 │
├─────────────────────────────────────────┤
│  1. Update manifest: status=completed   │
│  2. Run: bd close <beads_id>            │
│  3. Report summary to user              │
└─────────────────────────────────────────┘
```

### What You See When Running /orchestrate

```
READY WORK (6 unblocked tasks):
================================
1. [P1] Sample_monsta5-wjt: ProcessingPage Visual Fixes Epic
2. [P2] Sample_monsta5-yke: Fix ProcessingPage monster animation sizing
3. [P2] Sample_monsta5-cv5: Fix TILESET.png border not loading
4. [P2] Sample_monsta5-vo9: Fix LAVA.gif background not appearing
5. [P2] Sample_monsta5-ad7: GIF Loading Animation
6. [P2] Sample_monsta5-4fa: Spectral Separation Dialog Redesign

Options:
- Enter 1-6 to work on that task
- Enter "new" to create a new task
- Or describe a custom task
```

### Example Session Manifest

```json
{
  "session_id": "monster-animation-fix-2025-12-29",
  "beads_id": "Sample_monsta5-yke",
  "task": "Fix ProcessingPage monster animation sizing",
  "status": "in_progress",
  "current_phase": "implementation"
}
```

### Skipping Beads

If you want to skip task tracking:
```
/orchestrate Fix typo in readme (skip beads)
```
