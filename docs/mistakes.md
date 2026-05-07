# SAMPLAR - Mistakes & Lessons Learned

This document captures mistakes made during development and the lessons learned to prevent recurrence.

**Purpose**: Every agent MUST read this file before starting work and apply lessons proactively.

---

## Mistake 1: Combined Frontend/Backend Implementation Falls Short

**Date:** 2025-12-08

**What Happened:**
When implementing features that have both UI and backend components, the implementation consistently fell short on one or both sides. Either:
- UI was incomplete (missing controls, poor UX)
- Backend worked but UI didn't connect properly
- UI looked right but backend data flow was broken
- Both implemented but integration was faulty

This pattern repeated across multiple feature implementations.

**The Problem:**
Single-track architecture design tried to handle BOTH frontend and backend in one design document. This caused:
1. **Cognitive overload** - Too many concerns in one design
2. **Bias toward backend** - Developer thinking prioritized code over UX
3. **Missing details** - UI specifics glossed over or described vaguely
4. **No visual planning** - Implementing UI without seeing what it should look like
5. **Integration gaps** - No explicit mapping between UI elements and backend services

**What Should Have Been Done:**
DUAL-TRACK ARCHITECTURE:
1. **Frontend Track** (frontend-architect agent)
   - ASCII mockups showing BEFORE and AFTER states
   - Component hierarchy diagram
   - User interaction flow (step-by-step)
   - UI files to modify/create
   - Integration points with backend

2. **Backend Track** (architect agent)
   - Service architecture
   - Engine implementations
   - Data models and flow
   - APVTS parameters
   - Thread safety considerations

3. **Both tracks run in PARALLEL** and explicitly document integration points

**The Commitment:**
WE WILL ALWAYS USE DUAL-TRACK ARCHITECTURE FOR FEATURES WITH UI COMPONENTS.

Specifically:
1. Orchestrator spawns BOTH frontend-architect AND architect agents in parallel
2. Frontend design MUST include ASCII mockups (no exceptions)
3. Backend design MUST specify integration points for UI
4. BOTH designs must be complete before presenting to user
5. Implementation cannot proceed without BOTH designs approved

**CLAUDE.md Rule Violated:**
"Complete Context - Read everything, assume nothing"
We assumed UI details would be figured out during implementation. They weren't.

**Agent Responsible:**
- **Orchestrator**: Should have spawned separate frontend/backend design agents
- **Architect**: Should have delegated UI design instead of attempting both
- **Implementation**: Should have BLOCKED when UI design was vague

**Agent Profile Updates:**

**For orchestrator.md:**
- Added Phase 4 DUAL-TRACK design pattern
- Added frontend-architect to parallel design spawning
- Added gate check for frontend-design.json
- Added DUAL-TRACK RULE to "You NEVER Implement Without" section

**For architect.md:**
- Renamed to "Backend Architect"
- Added CRITICAL section explaining dual-track coordination
- Clarified focus on backend: services, engines, data flow, parameters
- Removed responsibility for UI details (delegated to frontend-architect)

**NEW AGENT CREATED:**
`.claude/agents/frontend-architect.md`
- Specializes in UI/UX design
- MANDATORY ASCII mockups
- Component hierarchy
- User interaction flows
- Modern UX patterns
- Integration points with backend

**Proactive Prevention:**
1. When user requests feature with UI component, orchestrator MUST spawn frontend-architect
2. Frontend-architect design MUST include ASCII mockups (validation check)
3. Implementation agent BLOCKS if frontend-design.json missing for UI features
4. Risk-analyst evaluates risks for BOTH frontend AND backend

---

## Mistake 2: Planning on Superficial Information (No Evidence Gate)

**Date:** 2025-12-08

**What Happened:**
Agents repeatedly made plans and designs based on superficial information:
- Assumed file locations without verifying they exist
- Guessed at code patterns without reading actual code
- Planned modifications to lines without checking what's actually there
- Designed solutions without finding working examples in the codebase
- Used "probably" and "likely" instead of verified facts

**The Problem:**
There was no hard gate requiring evidence BEFORE planning could begin. Agents would:
1. Receive task description
2. Make assumptions about codebase structure
3. Design solutions based on those assumptions
4. Implementation fails because assumptions were wrong

**Examples of superficial evidence that caused failures:**
```
BAD: "BPM display is probably in WavePreviewPage.cpp"
BAD: "The pattern likely uses setText()"
BAD: "Line ~200 should have the control"
BAD: "Similar to how other pages work"
```

**What Should Have Been Done:**
Evidence FIRST, planning SECOND. Always.

**ACCEPTABLE evidence:**
```
GOOD: "BPM display at src/ui/Pages/WavePreviewPage.cpp:234"
GOOD: "Code: bpmLabel.setText(juce::String(detectedBpm, 1));"
GOOD: "Verified by reading file on 2025-12-08"
GOOD: "Working example at SimpleVariationEngine.cpp:45"
```

**The Commitment:**
WE WILL ENFORCE AN EVIDENCE GATE THAT HARD-BLOCKS ALL PLANNING WITHOUT VERIFIED EVIDENCE.

Specifically:
1. evidence-report.json MUST exist before design phase
2. All findings MUST have `verified: true`
3. All findings MUST have actual line numbers (not "unknown" or "~200")
4. All findings MUST have actual code snippets (not "assumed")
5. Planning agents MUST read evidence and verify before designing
6. Implementation agents MUST verify file:line references before editing

**CLAUDE.md Rule Violated:**
"Complete Context - Read everything, assume nothing"
We assumed instead of reading. We guessed instead of verifying.

**Agent Responsible:**
- **Orchestrator**: Should have blocked design phase without evidence-report.json
- **Architect**: Should have refused to design with unverified findings
- **Frontend-Architect**: Should have refused to design without verified UI locations
- **Implementation**: Should have verified file:line before editing

**Agent Profile Updates:**

**For orchestrator.md:**
- Added EVIDENCE GATE section with hard block logic
- Added evidence quality standards (acceptable vs unacceptable)
- Added verification checklist before spawning design agents

**For architect.md:**
- Added EVIDENCE GATE section
- Added self-enforcement rules
- Must verify evidence before designing

**For frontend-architect.md:**
- Added EVIDENCE GATE section
- ASCII mockups must be based on ACTUAL current UI (not imagined)
- Must read source files before designing

**For implementation.md:**
- Added EVIDENCE GATE section
- Must read actual source files before editing
- Must verify line numbers match before making changes

**Proactive Prevention:**
1. Orchestrator HARD BLOCKS design phase if evidence-report.json missing
2. All planning agents verify evidence quality before starting
3. Implementation agent reads files and verifies lines before editing
4. Any agent can report "Evidence Gate blocked" to stop workflow

---

## Mistake 3: Fixing Bugs Without Log Evidence (Guessing)

**Date:** 2025-12-10

**What Happened:**
When bugs were reported, agents would:
1. Read the bug description
2. GUESS at the cause based on code reading
3. Implement a fix based on the guess
4. Fix often didn't work because the guess was wrong

No one checked if logging existed. No one analyzed actual runtime behavior. Fixes were based on static code analysis alone.

**The Problem:**
Static code analysis shows WHAT code does, but not:
- What values are at runtime
- Which code paths are actually taken
- Where exactly things go wrong
- The sequence of events leading to the bug

Without logs, debugging is guesswork with ~20% success rate.
With logs, debugging is evidence-based with ~90%+ success rate.

**Examples of guessing that caused failed fixes:**
```
BAD FIX ATTEMPT 1:
- Bug: "BPM shows wrong value"
- Guess: "Probably a rounding issue in the display"
- Fix: Added rounding to display
- Result: FAILED - actual bug was in data passing, not display

BAD FIX ATTEMPT 2:
- Bug: "Samples don't appear after processing"
- Guess: "The array probably isn't being populated"
- Fix: Added array population code
- Result: FAILED - array WAS populated, display component wasn't refreshing
```

**What Should Have Been Done:**
1. Check if logging exists for the affected area
2. If NO logs: Add diagnostic logging first
3. Have user reproduce bug and provide console output
4. Analyze logs to find WHERE/WHAT/WHY
5. Form hypothesis with 90%+ certainty based on logs
6. THEN design fix

**ACCEPTABLE debugging process:**
```
GOOD FIX PROCESS:
- Bug: "BPM shows wrong value"
- Step 1: Check for logs -> Found logging in WavePreviewPage
- Step 2: User reproduces bug, provides console output:
    [WavePreviewPage] setBpm called with: 93.847
    [BpmDisplay] received bpm: 0.0  <-- BUG HERE
- Step 3: Analyze logs -> Value lost between components
- Step 4: Hypothesis: setBpm not calling display update (95% certain)
- Step 5: Read code at identified location -> Confirmed!
- Step 6: Fix the specific issue
- Result: SUCCESS - fixed on first try
```

**The Commitment:**
WE WILL NEVER FIX BUGS WITHOUT LOG EVIDENCE.

Specifically:
1. **LOGGING GATE** - All implementations MUST include comprehensive logging
2. **LOG EVIDENCE GATE** - All bug fixes MUST have log evidence showing the bug
3. If no logs exist: Add diagnostic logging FIRST, then fix
4. Root cause certainty must be 90%+ before designing fix
5. Guessing is explicitly forbidden

**CLAUDE.md Rule Violated:**
"Evidence FIRST, planning SECOND"
We planned fixes before gathering evidence. We guessed instead of observing.

**Agent Responsible:**
- **Orchestrator**: Should have enforced log evidence requirement for bug fixes
- **Evidence-Gatherer**: Should have analyzed logs or requested diagnostic logging
- **Architect**: Should have refused to design fix without log evidence
- **Implementation**: Should have included logging in all code

**Agent Profile Updates:**

**For orchestrator.md:**
- Added LOGGING GATE section
- Added LOG EVIDENCE GATE section for bug fixes
- Added debugging workflow flowchart
- Added 90% certainty requirement

**For implementation.md:**
- Added LOGGING GATE section
- Added mandatory logging requirements for every function
- Added logging checklist

**For evidence-gatherer.md:**
- Added LOG ANALYSIS PROTOCOL section
- Added diagnostic-logging-plan.json creation process
- Added log evidence quality standards

**Proactive Prevention:**
1. Every implementation MUST include comprehensive logging
2. Bug fixes BLOCKED until log evidence obtained
3. If no logs exist: Diagnostic logging added first
4. Evidence-gatherer analyzes logs before proposing root cause
5. 90%+ certainty required before fix design begins

---

## Success Patterns

### Success Pattern 1: DUAL-TRACK Architecture

**Date:** 2025-12-08

**What Worked:**
Separating frontend and backend design into parallel tracks with explicit integration points.

**Why It Worked:**
1. Each track focuses on its domain (UX vs code architecture)
2. ASCII mockups make UI intentions explicit and verifiable
3. Integration points documented in both directions
4. Neither track proceeds without the other being complete

**When to Reuse:**
- ANY feature with UI components
- ANY feature with both visual and data flow changes
- ANY feature where user interaction is part of the requirement

**Implementation:**
1. Orchestrator spawns frontend-architect + architect + risk-analyst in parallel
2. Frontend-architect produces frontend-design.json with ASCII mockups
3. Architect produces architecture.json with backend focus
4. Both reference each other's integration points
5. User approves BOTH before implementation

**Agent Skill:**
This pattern is embedded in:
- orchestrator.md (Phase 4 DUAL-TRACK)
- frontend-architect.md (entire agent)
- architect.md (CRITICAL: DUAL-TRACK section)

---

## Checklist for New Implementations

Before ANY implementation, verify:

- [ ] **Is this feature UI-related?**
  - If YES: frontend-design.json MUST exist
  - If NO: Can proceed with architecture.json only

- [ ] **Does frontend design have ASCII mockups?**
  - BEFORE state shown
  - AFTER state shown
  - Changes clearly marked

- [ ] **Are integration points documented?**
  - Frontend lists backend services it needs
  - Backend lists data it exposes to UI

- [ ] **Is the user flow defined?**
  - Step-by-step interaction documented
  - Edge cases considered

- [ ] **Has risk-analyst reviewed BOTH tracks?**
  - Frontend risks (UX edge cases, component interactions)
  - Backend risks (audio processing, memory, performance)

---

### Success Pattern 2: EVIDENCE GATE

**Date:** 2025-12-08

**What Worked:**
Hard-blocking all planning and design work until verified evidence exists.

**Why It Worked:**
1. Forces agents to READ before they WRITE
2. Eliminates assumptions with verified facts
3. Catches misunderstandings before implementation begins
4. Creates auditable evidence trail

**When to Reuse:**
- EVERY feature implementation
- EVERY bug fix
- ANY code modification

**Implementation:**
1. evidence-report.json must exist before design phase
2. All findings must be verified with actual file:line references
3. Planning agents verify evidence quality before starting
4. Implementation agents verify line numbers before editing

**Agent Enforcement:**
- orchestrator.md: EVIDENCE GATE section with hard block
- architect.md: EVIDENCE GATE section with self-enforcement
- frontend-architect.md: EVIDENCE GATE section
- implementation.md: EVIDENCE GATE with pre-edit verification

---

### Success Pattern 3: LOG-BASED DEBUGGING

**Date:** 2025-12-10

**What Worked:**
Requiring log evidence before attempting bug fixes, with diagnostic logging added when logs don't exist.

**Why It Worked:**
1. Logs show RUNTIME behavior (not just static code)
2. Logs reveal exact sequence of events
3. Logs pinpoint WHERE values go wrong
4. 90%+ certainty before fixing = first-time success

**When to Reuse:**
- EVERY bug fix
- ANY unexpected behavior
- Performance investigation
- Integration debugging

**Implementation:**
1. Check if logging exists for affected area
2. If no logs: Create diagnostic-logging-plan.json
3. Add diagnostic logging (minimal, targeted)
4. User reproduces bug with logging enabled
5. Analyze log output to identify root cause
6. Form hypothesis with 90%+ certainty
7. Design fix based on log evidence
8. Add more logging to verify fix works

**Agent Enforcement:**
- orchestrator.md: LOG EVIDENCE GATE for bug fixes
- evidence-gatherer.md: LOG ANALYSIS PROTOCOL
- implementation.md: LOGGING GATE - mandatory logging in all code

**The Debugging Workflow:**
```
Bug Report -> Check Logs -> [No Logs?] -> Add Diagnostic Logging
                              |
                              v
                    [Logs Exist] -> Analyze -> 90%+ Certainty -> Design Fix
```

---

## Evolution Log

| Date | Change | Reason |
|------|--------|--------|
| 2025-12-08 | Created dual-track architecture | Frontend/backend combined implementation failing repeatedly |
| 2025-12-08 | Added frontend-architect agent | Need specialized UI/UX design focus |
| 2025-12-08 | Updated orchestrator Phase 4 | Must spawn both design agents in parallel |
| 2025-12-08 | Renamed architect to backend architect | Clarify responsibility scope |
| 2025-12-08 | Added EVIDENCE GATE to all planning agents | Agents planning on superficial information |
| 2025-12-08 | Added evidence quality standards | Need to distinguish verified vs assumed evidence |
| 2025-12-08 | Added self-enforcement to all agents | Each agent must block itself if evidence lacking |
| 2025-12-10 | Added LOGGING GATE | Implementations without logging can't be debugged |
| 2025-12-10 | Added LOG EVIDENCE GATE | Bug fixes without log evidence are guesswork |
| 2025-12-10 | Added debugging workflow | Formal process: logs first, then fix |
| 2025-12-10 | Added diagnostic-logging-plan.json | When no logs exist, add them before fixing |
| 2025-12-10 | Added 90% certainty requirement | No fix attempts without high confidence from logs |
