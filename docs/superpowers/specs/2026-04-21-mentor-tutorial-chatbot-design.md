# Mentor Tutorial Chatbot — Design Spec

**Date:** 2026-04-21
**Status:** Approved by user, pending implementation plan
**Replaces:** Existing `ChatPage` + `ChatService` (deprecated)

---

## 1. Purpose

Replace the existing generic chat in SAMPLAR with a **scripted, workflow-organized, step-by-step mentor tutorial system** that guides users through SAMPLAR's four core workflows:

- **Full Song** — separating stems / extracting loops from full tracks
- **Drum Stem** — working with drum stems
- **S-Mode** — sampler mode features
- **Drum Library** — browsing and managing the drum library

No LLM / no network. All lessons are authored content compiled into the binary. Lightweight, deterministic, ship-ready.

## 2. Goals & Non-Goals

**Goals**
- Teach new users SAMPLAR's features by doing, inside the app.
- Be discoverable (dedicated page) and contextual (floating helper on each workflow page).
- Be fully modular — a single self-contained module (`src/mentor/`) with one registration entry point.
- Persist lesson progress across sessions.

**Non-Goals**
- No conversational AI / LLM integration.
- No action detection / auto-advance. Steps advance on manual Next/Back only.
- No inline UI highlighting / coach-marks (deferred to future iteration).
- No editing lessons at runtime. Content is edited in JSON source and requires rebuild.

## 3. User Interaction Model

- **Interaction:** Contextual coach (pattern D) — floating "?" button per workflow page opens lessons relevant to that workflow.
- **Content organization:** Scripted lessons (pattern A), grouped by workflow section.
- **Presentation:** Dedicated Mentor page + floating per-page helper (pattern C).
- **Step progression:** Manual Next / Back (pattern A).
- **Content storage:** JSON compiled into binary via JUCE `BinaryData` (pattern C).

## 4. Architecture

### 4.1 Module Layout — fully isolated

```
src/mentor/                         ← NEW self-contained module
├── MentorService.h / .cpp          ← samplar::IService, loads JSON, tracks state
├── MentorTypes.h                   ← Section / Lesson / Step structs
├── ui/
│   ├── MentorPage.h / .cpp              ← dedicated page (replaces ChatPage in nav)
│   ├── MentorSectionList.h / .cpp       ← left sidebar (sections + lessons)
│   ├── MentorStepView.h / .cpp          ← right pane (current step rendering)
│   └── MentorHelperButton.h / .cpp      ← floating "?" button on workflow pages
└── MentorRegistration.cpp          ← single entry point: registerMentorModule(App&)

resources/mentor/
├── mentor_lessons.json
└── images/*.png
```

The only coupling to the rest of the codebase:
- Each of the four workflow pages adds **one line** creating a `MentorHelperButton` bound to its section id and calling `addAndMakeVisible`.
- `MentorRegistration.cpp` is called once from `App.cpp` bootstrap.
- Communication via `EventBus` only. No direct includes of mentor internals outside `src/mentor/`.

### 4.2 Components

**`MentorService` (backend, `samplar::IService`)**
- Loads `mentor_lessons.json` from BinaryData during `initialise()`.
- Parses into `Array<MentorSection>`.
- Tracks: `currentSectionId`, `currentLessonId`, `currentStepIndex`, `completedLessons` (`std::set<String>`).
- Persists `completedLessons` to `PropertiesFile` (`userPrefs.xml`), key `mentor.completedLessons`.
- Public API:
  - `const Array<MentorSection>& getSections() const`
  - `const MentorSection* getSection(const String& id) const`
  - `const MentorLesson* getLesson(const String& sectionId, const String& lessonId) const`
  - `void openLesson(const String& sectionId, const String& lessonId)`
  - `void nextStep()` / `void prevStep()`
  - `const MentorStep* getCurrentStep() const`
  - `bool isLessonCompleted(const String& lessonId) const`
- Publishes: `mentor.stepChanged`, `mentor.lessonCompleted`, `mentor.lessonOpened`.

**`MentorPage` (UI)**
- Replaces `ChatPage` in main nav.
- Three visual states:
  - **Browse** — no lesson selected; right pane shows section description and lesson cards.
  - **Lesson** — active step view with progress bar, title, body, optional image, Back/Next.
  - **Complete** — "Lesson complete" screen with "Next lesson" / "Back to lessons" buttons.
- Subscribes to `mentor.openLesson`, `mentor.openSection`, `mentor.stepChanged`.

**`MentorSectionList`**
- Left sidebar; tree of sections → lessons. Completed lessons get a ✓ marker. Clicking a lesson calls `MentorService::openLesson()`.

**`MentorStepView`**
- Renders `MentorService::getCurrentStep()`. Progress bar shows `step N of M`. Buttons bound to `nextStep()` / `prevStep()`. When on last step and user clicks Next → lesson marked complete, view transitions to Complete state.

**`MentorHelperButton` (floating)**
- 32×32 circular "?" button, bottom-right of the hosting workflow page, matches SAMPLAR LookAndFeel.
- Constructed with a section id (e.g. `"fullsong"`).
- Click → popup menu listing that section's lessons. Selecting a lesson publishes `mentor.openLesson` with section+lesson ids; main window navigates to MentorPage.
- Hideable via user preference (`mentor.helperVisible`, default `true`). Right-click the button → "Hide mentor helper" menu item. Re-enabled via a "Show mentor helper" toggle inside the MentorPage header.

### 4.3 Event Bus Topics

| Topic | Direction | Payload |
|---|---|---|
| `mentor.openSection` | helper → page | `{ "sectionId": "fullsong" }` |
| `mentor.openLesson` | helper → service/page | `{ "sectionId": "fullsong", "lessonId": "fullsong_intro" }` |
| `mentor.stepChanged` | service → view | `{ "sectionId", "lessonId", "stepIndex", "totalSteps" }` |
| `mentor.lessonCompleted` | service → listeners | `{ "lessonId" }` |
| `mentor.lessonOpened` | service → listeners | `{ "sectionId", "lessonId" }` |

## 5. Data Model

### 5.1 JSON Schema (`resources/mentor/mentor_lessons.json`)

```json
{
  "version": 1,
  "sections": [
    {
      "id": "fullsong",
      "title": "Full Song",
      "description": "Process entire songs — separate stems, extract loops.",
      "icon": "fullsong",
      "lessons": [
        {
          "id": "fullsong_intro",
          "title": "Getting Started with Full Song",
          "estimatedMinutes": 3,
          "steps": [
            {
              "title": "Open the Full Song page",
              "body": "Click 'Full Song' in the left nav to open the workflow.",
              "image": "fullsong_nav.png"
            }
          ]
        }
      ]
    },
    { "id": "drumstem", "title": "Drum Stem", "description": "...", "lessons": [] },
    { "id": "smode",    "title": "S-Mode",    "description": "...", "lessons": [] },
    { "id": "drumlib",  "title": "Drum Library", "description": "...", "lessons": [] }
  ]
}
```

Field conventions:
- All `id` values are lowercase snake_case and globally unique within their scope.
- `image` field is optional. When present, it is the filename of a PNG under `resources/mentor/images/` embedded in BinaryData.
- `estimatedMinutes` is advisory (displayed in lesson cards), not enforced.

### 5.2 C++ Types (`MentorTypes.h`)

```cpp
struct MentorStep {
    juce::String title;
    juce::String body;
    juce::String imageName; // empty if none
};

struct MentorLesson {
    juce::String id;
    juce::String title;
    int estimatedMinutes { 0 };
    juce::Array<MentorStep> steps;
};

struct MentorSection {
    juce::String id;
    juce::String title;
    juce::String description;
    juce::String icon;
    juce::Array<MentorLesson> lessons;
};
```

## 6. UI Layout

### 6.1 MentorPage (lesson state)

```
┌──────────────────────────────────────────────────────────────┐
│  MENTOR                                                      │
├──────────────────┬───────────────────────────────────────────┤
│ WORKFLOWS        │  Lesson: Getting Started with Full Song   │
│                  │  Step 2 of 5                              │
│ ▸ Full Song      │  ━━━━━━░░░░░░░░░░  40%                    │
│   • Getting Sta… │                                           │
│   • Separate St… │  ┌─────────────────────────────────────┐  │
│   • Export Loops │  │  [optional step image]              │  │
│ ▸ Drum Stem      │  │                                     │  │
│ ▸ S-Mode         │  └─────────────────────────────────────┘  │
│ ▸ Drum Library   │                                           │
│                  │  Drop an audio file                       │
│                  │  Drag any WAV/MP3 into the drop zone...   │
│                  │          [ ◀ Back ]  [ Next ▶ ]           │
└──────────────────┴───────────────────────────────────────────┘
```

### 6.2 MentorHelperButton

- 32×32 circular, bottom-right corner of each of the four workflow pages.
- Uses SAMPLAR LookAndFeel colors; hover state lifts opacity.
- On click, `juce::PopupMenu` listing that section's lessons with completed ✓ markers.

## 7. State & Persistence

- Runtime state lives in `MentorService`.
- Only `completedLessons` is persisted (lesson IDs, as a space-delimited string) under `PropertiesFile` key `mentor.completedLessons`.
- Preference `mentor.helperVisible` (bool, default `true`) controls floating helper visibility.
- No other state persists. Opening MentorPage always starts in Browse state; the last-open lesson is not restored (simplicity).

## 8. Deprecation of Existing Chat

- Remove `ChatPage` registration from main nav.
- Remove `ChatService` registration from `ServiceRegistry`.
- Move `src/ui/Pages/ChatPage.{h,cpp}` and `src/services/ChatService.{h,cpp}` → `src/deprecated/` with a `README.md` noting "Replaced by mentor module in this release; delete in the next release."
- `CMakeLists.txt`: exclude `src/deprecated/` from the build (keep files out of target sources).
- No data migration — old chat stored no user content worth preserving.

## 9. Testing

**Unit tests** (`tests/mentor/MentorServiceTests.cpp`):
- JSON parses cleanly from BinaryData; all four sections present.
- `nextStep()` / `prevStep()` respect bounds (no underflow on step 0, no overflow past last step).
- Completing a lesson adds it to `completedLessons` and fires `mentor.lessonCompleted`.
- Persistence round-trip: completed set survives service shutdown and re-initialise.

**Manual smoke tests**:
- Open each workflow page, click the floating "?" — popup shows that section's lessons.
- Select a lesson — navigates to MentorPage with that lesson active on step 1.
- Walk through a full lesson with Next; completion screen appears; ✓ appears on the lesson in the sidebar.
- Restart app — ✓ still present.
- Right-click a helper button → "Hide mentor helper" — buttons disappear across all workflow pages. Re-enable from MentorPage header toggle; they reappear.

## 10. Logging (per project rules)

All `MentorService` public methods log entry/exit with the 🎓 emoji prefix, e.g.:

```cpp
juce::Logger::writeToLog("🎓 [MentorService] === openLesson START ===");
juce::Logger::writeToLog("🎓 [MentorService]   sectionId=" + sectionId + " lessonId=" + lessonId);
// ...
juce::Logger::writeToLog("🎓 [MentorService] === openLesson END ===");
```

`MentorPage`, `MentorHelperButton`, and `MentorStepView` log user interactions (click Next, click lesson, open helper popup).

## 11. Authoring Workflow

1. Edit `resources/mentor/mentor_lessons.json`.
2. (Optional) add new PNGs to `resources/mentor/images/`.
3. Rebuild — CMake's `juce_add_binary_data` regenerates the embedded blob.
4. Run the app — new content appears automatically.

## 12. Out of Scope (future iterations)

- Auto-advance on EventBus state (e.g., detect "sample.loaded").
- Coach-mark overlays (arrows pointing at real UI elements).
- LLM-backed "ask me anything" mode.
- In-app lesson editor.
- Localization of lesson text.

## 13. Acceptance Criteria

- [ ] Existing `ChatPage` / `ChatService` no longer appear in the running app.
- [ ] `src/mentor/` contains the full module; no mentor symbols exist outside that directory except the one `addAndMakeVisible` line on each of the four workflow pages and one registration call from `App.cpp`.
- [ ] MentorPage is reachable from main nav and renders all four sections loaded from BinaryData JSON.
- [ ] Each of the four workflow pages shows the floating "?" button (when preference enabled).
- [ ] Clicking the helper opens a popup scoped to that workflow's lessons; selecting one navigates to MentorPage on that lesson's first step.
- [ ] Next/Back progress through steps; completion screen appears; ✓ persists across restarts.
- [ ] Unit tests pass; manual smoke tests pass on all four workflows.
