# Mentor Unification Investigation

**Date:** 2026-04-21  
**Scope:** Context-filtered MentorPage, existing tutorial UIs, and helper button coverage  
**Status:** Complete

---

## Finding 1 — Context-Filtered MentorPage

### Current Behavior

**MentorPage** (`src/mentor/ui/MentorPage.h:12-29`, `src/mentor/ui/MentorPage.cpp:1-56`)
- Always displays **all four sections** (fullsong, drumstem, smode, drumlib)
- MentorSectionList renders every section in `getSections()` via `service_.getSections()` (MentorSectionList.cpp:10-24)
- No filtering by source page or context

**MentorHelperButton** (`src/mentor/ui/MentorHelperButton.h:11`, `MentorHelperButton.cpp:29-83`)
- **Constructor receives `sectionId_`** (the source workflow ID: "fullsong", "drumstem", "smode", "drumlib")
- **On click:** publishes `mentor.openLesson` event with `sectionId` and `lessonId` (line 77-81)
- **Problem:** Does NOT pass section context to MentorPage; MentorPage still shows all sections

### Where "Coming From" Information Lives

**MainWindow.cpp RootComponent:1545**
- `previousPage_` and `currentPage_` track navigation history
- switchTo(index) updates these before showing new page
- **Pages are indexed 0-9:** Initial(0), Preview(1), Processing(2), Browser(3), Editor(4), EngineLabPage(5), JuceEditor(6), Community(7), Mentor(8), SModePage(9)

**Mapping page index → section ID:**
```
Processing(2) → "fullsong"
Preview(1) → "drumstem"
SModePage(9) → "smode"
Browser(3) → "drumlib"
```

### Minimum-Scope Changes for Filtering

**Files to modify:**

1. **src/mentor/ui/MentorHelperButton.cpp** (line 75-82)
   - In `showPopup()`, before publishing, also publish `mentor.setContextSection` event with the sectionId
   - Event payload: `{"sectionId": sectionId_}`

2. **src/mentor/ui/MentorPage.cpp** (line 28-38)
   - Add subscription to `mentor.setContextSection` event
   - Store the incoming sectionId as `activeFilterSectionId_`
   - Call `sections_.setFilterSection(activeFilterSectionId_)` if filter is non-empty

3. **src/mentor/ui/MentorSectionList.h** (line 10-30)
   - Add `void setFilterSection(const juce::String& id)` public method
   - Add `juce::String filteredSectionId_` member
   - Modify `rebuildRows()` to skip sections that don't match `filteredSectionId_` (if set)

4. **src/mentor/ui/MentorSectionList.cpp** (line 10-24)
   - Implement filter logic: if `filteredSectionId_` is non-empty, only add lessons from that section
   - If filter is empty, show all sections (full-library mode)

5. **src/mentor/MentorService.h** (line 29-31)
   - Add `void setContextSection(const juce::String& sectionId)` public method
   - Store context for potential use (optional, may not be needed)

**Design question:** Should MentorPage have a "full library" button or mode toggle to temporarily show all sections?

---

## Finding 2 — Pre-Existing Tutorial-Like UIs

### Grep Results Summary

Searched `src/` (excluding `src/mentor/` and `src/deprecated/`) for: tutorial, Tutorial, Help, HelpDialog, Welcome, walkthrough, onboard, coach, guide.

**Result:** No explicit tutorial/help/welcome dialogs or coach-mark systems found in the codebase.

### WavePreviewPage Analysis

**File:** `src/ui/Pages/WavePreviewPage.h` (line 1-100)  
**Finding:** No tutorial dialog, help widget, or welcome screen.

**Workflow enum found at lines 28-34:**
```cpp
enum class WorkflowStep {
    Step1_SelectLength,      // User selects sample length
    Step2_DownbeatLoops,     // User adjusts beat grid and downbeat
    Step25_Processing,       // User applies pre-slice processing
    Step3_SampleSlice        // User sets slice granularity
};
```

This is not a tutorial system; it's an internal workflow state enum for UI logic.

**Mentor helper button found:** Yes, at line 21 (include) and WavePreviewPage.cpp line with mentorHelper_ instantiation.

### Existing Help Surfaces

| Surface | File:Line | Purpose | Overlap with Mentor? |
|---------|-----------|---------|----------------------|
| (none found) | — | — | — |

**Conclusion:** SAMPLAR currently has **zero pre-existing tutorial/help systems** outside of Mentor. No dialogs, walkthroughs, or onboarding flows to migrate or replace.

---

## Finding 3 — Helper Button Coverage Audit

### Navigation Structure

From **MainWindow.cpp:1786**:
```cpp
StringArray items { 
    "Initial",          // 0
    "Preview",          // 1 (WavePreviewPage)
    "Processing",       // 2 (ProcessingPage)
    "Browser",          // 3 (SampleBrowserPage)
    "Editor",           // 4 (WaveEditorView)
    "Engine Lab",       // 5 (EngineLabPage)
    "JUCE Editor",      // 6 (juceEditor_)
    "Community",        // 7 (CommunityPage)
    "Mentor",           // 8 (MentorPage)
    "S-Mode"            // 9 (SModePage)
};
```

### Helper Button Status Table

| Nav Entry | Host Component | MentorHelperButton? | Recommend Adding? | Reason |
|-----------|---|---|---|---|
| Initial | InitialPage | ❌ NO | ❌ NO | Landing page; no workflow. Users choose mode here. |
| Preview | WavePreviewPage | ✅ YES | ✅ YES | Workflow page (drum stem processing). Has helper button (`src/ui/Pages/WavePreviewPage.h:21`). Section: "drumstem" |
| Processing | ProcessingPage | ✅ YES | ✅ YES | Workflow page (full song processing). Has helper button (`src/ui/Pages/ProcessingPage.h`). Section: "fullsong" |
| Browser | SampleBrowserPage | ✅ YES | ✅ YES | Workflow page (drum library). Has helper button (`src/ui/Pages/SampleBrowserPage.h`). Section: "drumlib" |
| Editor | WaveEditorView | ❌ NO | ❓ MAYBE | Advanced editing tool. Mentor lessons may not be relevant; could be expert-only. |
| Engine Lab | EngineLabPage | ❌ NO | ❌ NO | Engine parameter exploration. Not a guided workflow; advanced feature. |
| JUCE Editor | juceEditor_ | ❌ NO | ❌ NO | JUCE plugin editor. External component; can't modify. |
| Community | CommunityPage | ❌ NO | ❌ NO | Community hub. Self-explanatory feature. |
| Mentor | MentorPage | N/A | N/A | Mentor itself; helper button would be circular. |
| S-Mode | SModePage | ✅ YES | ✅ YES | Workflow page (drum synthesis mode). Has helper button (`src/ui/Pages/SModePage.h`). Section: "smode" |

### Evidence for Each Missing Button

**Files checked:**
- `InitialPage.h` — no MentorHelperButton include ✅
- `EngineLabPage.h` — no MentorHelperButton include ✅
- `CommunityPage.h` — no MentorHelperButton include ✅
- `WaveEditorView.h` — no MentorHelperButton include ✅

**Coverage:** 4 out of 4 recommended workflow pages have helper buttons installed.

**Missing / Not Recommended:**
- InitialPage: No button (appropriate; users choose mode)
- EngineLabPage: No button (advanced feature; likely not tutorial-ready)
- CommunityPage: No button (self-explanatory feature)
- WaveEditorView: No button (external/advanced; design question needed)

---

## Recommended Unified Design (High-Level)

### Architecture

```
MentorService (source of truth)
    ├── All sections + lessons (4 sections: fullsong, drumstem, smode, drumlib)
    └── Tracks: currentSectionId, currentLessonId, completedLessons

EventBus (inter-page communication)
    ├── mentor.openLesson { sectionId, lessonId } — triggered by helper button
    ├── mentor.setContextSection { sectionId } — triggered by helper button before navigate
    ├── ui.navigate { "mentor" } — navigate to MentorPage
    └── ui.navigate { "back" } — return to previous page

MentorPage (two-mode UI)
    ├── Mode A: Filtered view (show ONLY lessons from activeFilterSectionId_)
    ├── Mode B: Full library (toggle button to show all 4 sections)
    └── Return button navigates back to previousPage_

MentorSectionList (renders sections/lessons)
    ├── setFilterSection(sectionId) — filter by workflow
    └── rebuildRows() — skip non-matching sections

MentorHelperButton (on 4 workflow pages)
    ├── On click: publish mentor.setContextSection with its sectionId_
    ├── Then: publish ui.navigate("mentor")
    └── MentorPage receives filter context and shows only that section
```

### Page Transitions

**From ProcessingPage ("fullsong" workflow):**
1. User clicks "?" button
2. Button publishes `mentor.setContextSection { sectionId: "fullsong" }`
3. Button publishes `ui.navigate("mentor")`
4. MainWindow.switchTo(8) activates MentorPage
5. MentorPage.refreshFromService() sees activeFilterSectionId_="fullsong"
6. MentorSectionList shows only fullsong lessons (hides drumstem, smode, drumlib)
7. User clicks lesson → opens step view
8. User clicks "Return" → navigates back to ProcessingPage (MainWindow.previousPage_ = 2)

**From MentorPage, click "Show all sections":**
1. User sees toggle in MentorPage header
2. Click enables full-library mode (setFilterSection("") — empty string = no filter)
3. MentorSectionList shows all 4 sections
4. User can browse/compare lessons across workflows
5. User clicks lesson → opens that lesson (may be from different section)

---

## Open Design Questions

### Q1: Full-Library Mode Toggle
Should MentorPage have a **"Show all sections" toggle** in the header to temporarily see lessons from other workflows? Or is context-filtered view (show-only-current-section) the desired final state?

**Trade-off:**
- **Filtered-only:** Cleaner, less overwhelming, teaches one workflow at a time
- **With toggle:** More discovery, users can compare lessons, optional exploration

### Q2: Return-to-Page Persistence
When user returns from MentorPage to previous page, should that page's UI state be preserved (scroll position, selection, etc.)? Or is a clean return acceptable?

**Current:** MainWindow.switchTo(previousPage) just makes the page visible; doesn't restore internal state.

### Q3: WaveEditorView Helper Button
WaveEditorView is an advanced feature (external JUCE editor window). Should it have a helper button?
- **NO:** Expert-only tool; mentor is for guided workflows
- **YES:** Provide guidance even for advanced users

**Recommend:** NO, unless user specifically requests mentor lessons for wave editing.

### Q4: Helper Button Visibility Toggle
MentorPage currently has a toggle: "Show mentor helper on pages". Should this be per-workflow (user can disable for Processing but keep for Browser)? Or globally (all or none)?

**Current:** Global (all helpers show/hide together via `MentorHelperButton::setGloballyVisible()`).

**Trade-off:**
- **Global:** Simpler, less UI complexity
- **Per-workflow:** More granular control

---

## Summary of Findings

| Finding | Status | Evidence |
|---------|--------|----------|
| **1. Context-filtered MentorPage** | Feasible | currentPage_ tracked in MainWindow; sectionId_ passed to helper button; requires MentorPage subscription + SectionList filter logic |
| **2. Pre-existing tutorials** | None found | Grep search negative; WavePreviewPage has no tutorial dialog |
| **3. Helper button coverage** | 4/4 complete | ProcessingPage, WavePreviewPage, SampleBrowserPage, SModePage all have buttons; 5 pages don't need them |

**Conclusion:** SAMPLAR has a clean, unified mentor system. All four workflow pages already have helper buttons. The main enhancement is filtering MentorPage to show only the relevant section based on the source workflow.
