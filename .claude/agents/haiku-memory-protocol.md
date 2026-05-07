# Haiku Working Memory Protocol

## The Problem

Haiku loses track in longer sessions - forgets variable names, file locations, and context established earlier. This leads to:
- Repeated searches for the same information
- Inconsistent naming in reports
- Lost context between tool calls

## The Solution: Working Memory Files

Each Haiku agent maintains a **working memory file** that persists critical information across tool calls.

---

## Memory File Location

```
.claude/workflow/sessions/{session_id}/memory/
├── evidence-gatherer-memory.json
├── integration-supervisor-memory.json
├── build-deployment-memory.json
└── documentation-memory.json
```

---

## Memory File Structure

```json
{
  "session_id": "xxx",
  "agent": "evidence-gatherer",
  "last_updated": "ISO-timestamp",
  "turn_count": 5,

  "task_context": {
    "original_task": "Add granular synthesis engine",
    "current_focus": "Finding engine registration pattern",
    "success_criteria": ["Find IVariationEngine pattern", "Find registration in PluginProcessor"]
  },

  "discovered_facts": {
    "key_files": {
      "engine_interface": "src/services/variation/IVariationEngine.h",
      "engine_registration": "src/services/GeneratorService.cpp:67",
      "ui_combobox": "src/ui/Pages/WavePreviewPage.cpp:234"
    },
    "key_patterns": {
      "engine_creation": "engines_[N] = std::make_unique<EngineClass>();",
      "settings_map": "samplar::EffectSettingsMap with string keys"
    },
    "key_names": {
      "class_names": ["SimpleVariationEngine", "CreativeVariationEngine"],
      "parameter_ids": ["engineType", "mutationRate"],
      "function_names": ["generateVariation", "setEffectSettings"]
    }
  },

  "progress": {
    "completed": [
      "Found IVariationEngine interface at src/services/variation/IVariationEngine.h",
      "Found 8 existing engines in src/services/variation/engines/"
    ],
    "in_progress": "Searching for UI registration pattern",
    "blocked": null,
    "next_steps": ["Check GeneratorService for engine array", "Find UI combobox population"]
  },

  "warnings": [
    "PluginProcessor.cpp is large (2000+ lines) - search by function name",
    "Engine indices must match UI combobox IDs"
  ]
}
```

---

## REMIND YOURSELF GATE (Mandatory)

### When to Trigger

**BEFORE every tool call**, Haiku agents MUST:

1. **Read memory file** (if exists)
2. **Refresh context** from memory
3. **Proceed with operation**
4. **Update memory file** (after operation)

### Implementation in Agent Prompts

Add this to every Haiku agent's prompt:

```markdown
## REMIND YOURSELF GATE (MANDATORY)

Before EVERY tool call, you MUST:

1. **READ your memory file**:
   ```
   Read(.claude/workflow/sessions/{session_id}/memory/{agent-name}-memory.json)
   ```

2. **REFRESH your context** from memory:
   - What files have I already found?
   - What patterns have I identified?
   - What is my current task focus?
   - What warnings should I remember?

3. **PROCEED with your operation**

4. **UPDATE your memory file** after EACH significant discovery:
   - New file location found
   - New pattern identified
   - New variable/class name learned
   - Progress milestone reached

### Memory Update Template

After each discovery, update your memory:
```json
{
  "discovered_facts": {
    "key_files": {
      "NEW_KEY": "path/to/file.cpp:line"
    }
  },
  "progress": {
    "completed": ["Previous items...", "NEW: Found X at Y"]
  }
}
```

**CRITICAL**: If you find yourself searching for something you already found,
CHECK YOUR MEMORY FIRST. The answer is likely already there.
```

---

## Agent-Specific Memory Patterns

### evidence-gatherer Memory Focus

```json
{
  "discovered_facts": {
    "bug_location": "file:line where bug was found",
    "working_examples": ["file1:line", "file2:line"],
    "pattern_analysis": "Description of JUCE pattern identified"
  }
}
```

### integration-supervisor Memory Focus

```json
{
  "discovered_facts": {
    "integration_points": {
      "point_1": {"file": "...", "line": 0, "verified": true},
      "point_2": {"file": "...", "line": 0, "verified": false}
    },
    "code_path": ["Step 1: file:line", "Step 2: file:line"]
  }
}
```

### build-deployment Memory Focus

```json
{
  "discovered_facts": {
    "source_files": ["file1.cpp", "file2.cpp"],
    "cmake_entries": ["line 42: file1.cpp", "line 43: file2.cpp"],
    "previous_errors": ["Error X at file:line - fixed by Y"]
  }
}
```

### documentation Memory Focus

```json
{
  "discovered_facts": {
    "files_changed": ["file1.cpp:lines", "file2.h:lines"],
    "features_added": ["Feature 1 description", "Feature 2 description"],
    "logs_to_cleanup": ["file1.cpp:45-60", "file2.cpp:120-135"]
  }
}
```

---

## Memory Lifecycle

### Session Start
1. Create memory directory: `.claude/workflow/sessions/{session_id}/memory/`
2. Initialize empty memory file for each Haiku agent

### During Session
1. Agent reads memory at start of each turn
2. Agent updates memory after each significant discovery
3. Memory accumulates facts across all tool calls

### Session End
1. Memory files preserved in session folder
2. Can be used for resume if session continues later

---

## Session Isolation (CRITICAL)

### The Problem
Without isolation, Haiku could:
- Read memory from a different session
- Corrupt another session's data
- Use stale information from days ago

### Automatic Safeguards

The `remind-yourself.py` hook enforces these rules:

**1. Session ID Validation**
```python
# Memory stores session_id
{
  "session_id": "feature-abc-2025-01-23",  # Must match current session
  ...
}

# Hook checks on every operation:
if memory.session_id != current_session_id:
    RESET memory with fresh state
```

**2. Staleness Detection**
```python
# Memory older than 24 hours is considered stale
MEMORY_STALE_HOURS = 24

if memory.last_updated older than threshold:
    RESET memory with fresh state
```

**3. Session Path Isolation**
```
Session A: .claude/workflow/sessions/feature-abc/memory/
Session B: .claude/workflow/sessions/feature-xyz/memory/

# Agent can ONLY access memory for current session
# Path is computed from session_id, not user input
```

### What Gets Reset

When memory is invalid, the hook:
1. Prints warning: `MEMORY RESET: Memory is from session 'old-session'...`
2. Creates fresh memory with current session_id
3. Agent starts with clean slate

This is SAFE because:
- Better to lose memory than use wrong memory
- Agent will rediscover facts (slightly slower, but correct)
- Session artifacts (evidence-report.json, etc.) are still available

---

## Cleanup Protocol

### When to Clean Up

Old sessions accumulate disk space. Clean up:
- **Automatically**: Sessions older than 7 days
- **Manually**: Before starting new major feature
- **On Demand**: If disk space is low

### Cleanup Command

```bash
# List sessions with age
python .claude/hooks/cleanup-old-sessions.py --list

# Dry run (show what would be deleted)
python .claude/hooks/cleanup-old-sessions.py --dry-run

# Delete sessions older than 7 days (default)
python .claude/hooks/cleanup-old-sessions.py

# Delete sessions older than 3 days
python .claude/hooks/cleanup-old-sessions.py --days 3
```

### Cleanup Rules

| Session Status | Age | Action |
|----------------|-----|--------|
| completed | > 7 days | DELETE |
| in_progress | > 14 days | DELETE (stale/abandoned) |
| any | < 7 days | KEEP |

### What Gets Deleted

```
.claude/workflow/sessions/{old-session}/
├── manifest.json         <- Deleted
├── evidence-report.json  <- Deleted
├── architecture.json     <- Deleted
├── memory/               <- Deleted
│   └── *.json
└── devlog.json           <- Deleted
```

### What to Preserve

If you need to keep a session longer:
1. Copy important artifacts to `docs/` or version control
2. Or rename session folder with `_keep` suffix (cleanup skips these)

### Recommended Schedule

- **Weekly**: Run cleanup with default settings
- **Before major features**: Run cleanup to start fresh
- **After project milestones**: Archive important sessions, then cleanup

---

## Anti-Patterns to Avoid

### Bad: Searching Again for Known Information
```
Turn 1: Found IVariationEngine at src/services/variation/IVariationEngine.h
Turn 5: Grep("IVariationEngine") <- WRONG! Already in memory!
```

### Good: Check Memory First
```
Turn 1: Found IVariationEngine at src/services/variation/IVariationEngine.h
Turn 5: Read memory -> "I already know it's at src/services/variation/IVariationEngine.h"
```

### Bad: Forgetting Variable Names
```
Turn 1: Found class "SimpleVariationEngine"
Turn 5: Looking for "SimpleEngine" or "VariationEngineSimple" <- WRONG!
```

### Good: Reference Memory for Names
```
Turn 1: Found class "SimpleVariationEngine" -> Store in memory.key_names.class_names
Turn 5: Read memory -> Use exact name "SimpleVariationEngine"
```

---

## Integration with Hooks

The memory system can be enforced via hooks:

```json
{
  "PreToolUse": [
    {
      "matcher": "Read|Grep|Glob",
      "hooks": [{
        "type": "command",
        "command": "python .claude/hooks/remind-yourself.py"
      }]
    }
  ]
}
```

The hook would:
1. Check if memory file exists for current agent
2. Log a reminder to check memory
3. (Optional) Block if searching for something already in memory

---

## Success Metrics

- **Reduced redundant searches**: Same file not searched twice
- **Consistent naming**: Variable/class names match across turns
- **Faster completion**: Less time re-discovering known facts
- **Better reports**: All discoveries consolidated in memory file
