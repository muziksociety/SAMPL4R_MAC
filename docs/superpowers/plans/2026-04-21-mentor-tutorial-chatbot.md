# Mentor Tutorial Chatbot — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace existing `ChatPage`/`ChatService` with a scripted, workflow-organized mentor tutorial system (`src/mentor/`) — one dedicated MentorPage + floating "?" helper on each of the four workflow pages (Full Song, Drum Stem, S-Mode, Drum Library).

**Architecture:** Self-contained module under `src/mentor/`. `MentorService` (IService) loads JSON lessons from JUCE BinaryData and owns runtime state. `MentorPage` renders the dedicated UI. `MentorHelperButton` provides contextual access per workflow page. All cross-module communication goes through `EventBus`; no direct includes of mentor internals outside `src/mentor/`.

**Tech Stack:** C++17, JUCE 7.x, CMake, JUCE `juce_add_binary_data`, JUCE `PropertiesFile` for persistence, GoogleTest-style manual asserts via JUCE `UnitTest`.

**Reference spec:** `docs/superpowers/specs/2026-04-21-mentor-tutorial-chatbot-design.md`

---

## Pre-Task: Identify Workflow Host Components

**Files:**
- Investigate: `src/ui/Pages/*.h`, `src/ui/MainWindow.cpp`
- Create: `docs/superpowers/plans/notes/mentor-host-pages.md` (scratch notes)

- [ ] **Step 1: Map each workflow section to its host Component**

For each of the four section ids (`fullsong`, `drumstem`, `smode`, `drumlib`), find the single Component class that owns the visible "workflow" page in SAMPLAR today. Candidates seen so far: `SModePage` (almost certainly `smode`), `SampleBrowserPage` (likely `drumlib`). For `fullsong` and `drumstem`, inspect `MainWindow.cpp` and any files in `src/ui/Pages/` that reference separation, stems, or full-song workflow.

Record findings as a table in `docs/superpowers/plans/notes/mentor-host-pages.md`:

```markdown
| Section id  | Host Component class | File (path)                   |
|-------------|----------------------|-------------------------------|
| fullsong    | <ClassName>          | src/ui/Pages/<File>.h         |
| drumstem    | <ClassName>          | src/ui/Pages/<File>.h         |
| smode       | SModePage            | src/ui/Pages/SModePage.h      |
| drumlib     | <ClassName>          | src/ui/Pages/<File>.h         |
```

Every subsequent task that says "on each of the four workflow pages" refers to the four classes recorded in this table. If any section has no host Component, stop and flag it to the user before continuing.

- [ ] **Step 2: Commit the notes**

```bash
git add docs/superpowers/plans/notes/mentor-host-pages.md
git commit -m "docs(mentor): record host pages for mentor helper buttons"
```

---

## Task 1: Module Skeleton — MentorTypes + empty MentorService

**Files:**
- Create: `src/mentor/MentorTypes.h`
- Create: `src/mentor/MentorService.h`
- Create: `src/mentor/MentorService.cpp`
- Create: `src/mentor/MentorRegistration.h`
- Create: `src/mentor/MentorRegistration.cpp`
- Modify: `CMakeLists.txt` (add new sources to the main target, alongside existing `src/services/*` entries)

- [ ] **Step 1: Create `MentorTypes.h`**

```cpp
#pragma once
#include <juce_core/juce_core.h>

namespace samplar {

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

} // namespace samplar
```

- [ ] **Step 2: Create `MentorService.h` (skeleton — no JSON yet)**

```cpp
#pragma once
#include "../app/Service.h"
#include "../app/EventBus.h"
#include "MentorTypes.h"
#include <juce_core/juce_core.h>
#include <set>

namespace samplar {

class MentorService : public IService {
public:
    explicit MentorService(EventBus& bus);
    ~MentorService() override;

    void initialise() override;
    void shutdown() override;

    const juce::Array<MentorSection>& getSections() const { return sections_; }
    const MentorSection* getSection(const juce::String& id) const;
    const MentorLesson*  getLesson(const juce::String& sectionId, const juce::String& lessonId) const;

    void openLesson(const juce::String& sectionId, const juce::String& lessonId);
    void nextStep();
    void prevStep();
    const MentorStep* getCurrentStep() const;

    bool isLessonCompleted(const juce::String& lessonId) const;

    juce::String currentSectionId() const { return currentSectionId_; }
    juce::String currentLessonId()  const { return currentLessonId_; }
    int          currentStepIndex() const { return currentStepIndex_; }

private:
    void loadFromBinaryData();
    void loadCompletedFromPrefs();
    void saveCompletedToPrefs() const;
    void publishStepChanged();
    void markCurrentLessonCompleted();

    EventBus& eventBus_;
    juce::Array<MentorSection> sections_;
    juce::String currentSectionId_;
    juce::String currentLessonId_;
    int currentStepIndex_ { -1 };
    std::set<juce::String> completedLessons_;

    static constexpr const char* LOG = "[MentorService]";
};

} // namespace samplar
```

- [ ] **Step 3: Create `MentorService.cpp` (empty method bodies)**

```cpp
#include "MentorService.h"

namespace samplar {

MentorService::MentorService(EventBus& bus) : eventBus_(bus) {}
MentorService::~MentorService() = default;

void MentorService::initialise() {
    juce::Logger::writeToLog(juce::String("?? ") + LOG + " === initialise START ===");
    loadFromBinaryData();
    loadCompletedFromPrefs();
    juce::Logger::writeToLog(juce::String("?? ") + LOG + " sections loaded: " + juce::String(sections_.size()));
    juce::Logger::writeToLog(juce::String("?? ") + LOG + " === initialise END ===");
}

void MentorService::shutdown() {
    juce::Logger::writeToLog(juce::String("?? ") + LOG + " === shutdown ===");
    saveCompletedToPrefs();
}

const MentorSection* MentorService::getSection(const juce::String& id) const {
    for (const auto& s : sections_) if (s.id == id) return &s;
    return nullptr;
}

const MentorLesson* MentorService::getLesson(const juce::String& sectionId, const juce::String& lessonId) const {
    const auto* sec = getSection(sectionId);
    if (sec == nullptr) return nullptr;
    for (const auto& l : sec->lessons) if (l.id == lessonId) return &l;
    return nullptr;
}

void MentorService::openLesson(const juce::String& sectionId, const juce::String& lessonId) {
    juce::Logger::writeToLog(juce::String("?? ") + LOG + " openLesson " + sectionId + "/" + lessonId);
    const auto* lesson = getLesson(sectionId, lessonId);
    if (lesson == nullptr || lesson->steps.isEmpty()) return;
    currentSectionId_ = sectionId;
    currentLessonId_  = lessonId;
    currentStepIndex_ = 0;
    juce::var payload(new juce::DynamicObject());
    payload.getDynamicObject()->setProperty("sectionId", sectionId);
    payload.getDynamicObject()->setProperty("lessonId",  lessonId);
    eventBus_.publish("mentor.lessonOpened", payload);
    publishStepChanged();
}

void MentorService::nextStep() {
    const auto* lesson = getLesson(currentSectionId_, currentLessonId_);
    if (lesson == nullptr) return;
    if (currentStepIndex_ + 1 >= lesson->steps.size()) {
        markCurrentLessonCompleted();
        return;
    }
    ++currentStepIndex_;
    publishStepChanged();
}

void MentorService::prevStep() {
    if (currentStepIndex_ <= 0) return;
    --currentStepIndex_;
    publishStepChanged();
}

const MentorStep* MentorService::getCurrentStep() const {
    const auto* lesson = getLesson(currentSectionId_, currentLessonId_);
    if (lesson == nullptr) return nullptr;
    if (currentStepIndex_ < 0 || currentStepIndex_ >= lesson->steps.size()) return nullptr;
    return &lesson->steps.getReference(currentStepIndex_);
}

bool MentorService::isLessonCompleted(const juce::String& lessonId) const {
    return completedLessons_.count(lessonId) > 0;
}

void MentorService::loadFromBinaryData()     { /* filled in Task 3 */ }
void MentorService::loadCompletedFromPrefs() { /* filled in Task 4 */ }
void MentorService::saveCompletedToPrefs() const { /* filled in Task 4 */ }

void MentorService::publishStepChanged() {
    const auto* lesson = getLesson(currentSectionId_, currentLessonId_);
    if (lesson == nullptr) return;
    juce::var payload(new juce::DynamicObject());
    payload.getDynamicObject()->setProperty("sectionId",  currentSectionId_);
    payload.getDynamicObject()->setProperty("lessonId",   currentLessonId_);
    payload.getDynamicObject()->setProperty("stepIndex",  currentStepIndex_);
    payload.getDynamicObject()->setProperty("totalSteps", lesson->steps.size());
    eventBus_.publish("mentor.stepChanged", payload);
}

void MentorService::markCurrentLessonCompleted() {
    if (currentLessonId_.isEmpty()) return;
    completedLessons_.insert(currentLessonId_);
    saveCompletedToPrefs();
    juce::var payload(new juce::DynamicObject());
    payload.getDynamicObject()->setProperty("lessonId", currentLessonId_);
    eventBus_.publish("mentor.lessonCompleted", payload);
}

} // namespace samplar
```

- [ ] **Step 4: Create `MentorRegistration.h` / `.cpp`**

`MentorRegistration.h`:
```cpp
#pragma once
namespace samplar { class EventBus; class ServiceRegistry; }
namespace samplar { void registerMentorModule(ServiceRegistry& registry, EventBus& bus); }
```

`MentorRegistration.cpp`:
```cpp
#include "MentorRegistration.h"
#include "MentorService.h"
#include "../app/ServiceRegistry.h"
#include <juce_core/juce_core.h>

namespace samplar {
void registerMentorModule(ServiceRegistry& registry, EventBus& bus) {
    juce::Logger::writeToLog("?? [MentorRegistration] registering MentorService");
    registry.registerService("mentor", std::make_unique<MentorService>(bus));
}
}
```

- [ ] **Step 5: Add new sources to `CMakeLists.txt`**

Locate the `target_sources(...)` block (search for `src/services/ChatService.cpp`). Add these lines in the same style as neighbouring entries, keeping alphabetical grouping where used:

```cmake
    src/mentor/MentorService.cpp
    src/mentor/MentorRegistration.cpp
```

- [ ] **Step 6: Build**

Run: `cmake --build build --config Release`
Expected: success. No behavioural change yet — `MentorService` is not registered.

- [ ] **Step 7: Commit**

```bash
git add src/mentor/ CMakeLists.txt
git commit -m "feat(mentor): add MentorService skeleton and types"
```

---

## Task 2: Seed lesson JSON + embed via BinaryData

**Files:**
- Create: `resources/mentor/mentor_lessons.json`
- Modify: `CMakeLists.txt` (add a `juce_add_binary_data` target for mentor resources, link it to the main target)

- [ ] **Step 1: Author a minimal but real `mentor_lessons.json`**

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
            { "title": "Open the Full Song page", "body": "Click 'Full Song' in the left nav to open the workflow." },
            { "title": "Drop an audio file", "body": "Drag any WAV/MP3 into the drop zone." },
            { "title": "Start separation", "body": "Click 'Separate' and wait for the stems to appear." }
          ]
        }
      ]
    },
    {
      "id": "drumstem",
      "title": "Drum Stem",
      "description": "Work with drum stems.",
      "icon": "drumstem",
      "lessons": [
        {
          "id": "drumstem_intro",
          "title": "Drum Stem Basics",
          "estimatedMinutes": 2,
          "steps": [
            { "title": "Load a drum stem", "body": "Drop a drum stem onto the Drum Stem page." },
            { "title": "Slice it", "body": "Use the slice controls to detect transients." }
          ]
        }
      ]
    },
    {
      "id": "smode",
      "title": "S-Mode",
      "description": "Sampler mode.",
      "icon": "smode",
      "lessons": [
        {
          "id": "smode_intro",
          "title": "S-Mode Basics",
          "estimatedMinutes": 2,
          "steps": [
            { "title": "Load samples into pads", "body": "Drop samples onto the S-Mode pads." },
            { "title": "Play with your keyboard", "body": "Your computer keyboard now triggers the pads." }
          ]
        }
      ]
    },
    {
      "id": "drumlib",
      "title": "Drum Library",
      "description": "Browse and manage the drum library.",
      "icon": "drumlib",
      "lessons": [
        {
          "id": "drumlib_intro",
          "title": "Browsing the Drum Library",
          "estimatedMinutes": 2,
          "steps": [
            { "title": "Open the Drum Library", "body": "Select 'Drum Library' from the nav." },
            { "title": "Preview a sample", "body": "Click any sample to audition it." }
          ]
        }
      ]
    }
  ]
}
```

- [ ] **Step 2: Add BinaryData target in `CMakeLists.txt`**

Add after existing binary data targets (search for `juce_add_binary_data` to find the pattern used). If none exists, insert before the main `add_executable`/plugin target:

```cmake
juce_add_binary_data(MentorResources
    SOURCES
        resources/mentor/mentor_lessons.json
    HEADER_NAME MentorBinaryData.h
    NAMESPACE   MentorBinaryData
)
```

Then, in the block that already links libraries to the main target (search `target_link_libraries` + `SM5App` or the main target name), add:

```cmake
target_link_libraries(<MAIN_TARGET> PRIVATE MentorResources)
```

Use the exact main target name already present in the file; do not invent one.

- [ ] **Step 3: Build**

Run: `cmake -B build -G "Visual Studio 17 2022" -A x64`
Then: `cmake --build build --config Release`
Expected: success; the generated `MentorBinaryData.h` appears under `build/.../JuceLibraryCode/` (exact path varies by generator).

- [ ] **Step 4: Commit**

```bash
git add resources/mentor/ CMakeLists.txt
git commit -m "feat(mentor): embed mentor_lessons.json via BinaryData"
```

---

## Task 3: Implement JSON loading (TDD)

**Files:**
- Modify: `src/mentor/MentorService.cpp` (implement `loadFromBinaryData`)
- Create: `tests/mentor/MentorServiceTests.cpp`
- Modify: `CMakeLists.txt` (add test source if the project has a tests target; otherwise gate under `BUILD_TESTS`)

- [ ] **Step 1: Write failing test `Section_ids_all_present`**

Create `tests/mentor/MentorServiceTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../../src/app/EventBus.h"
#include "../../src/mentor/MentorService.h"

class MentorServiceTests : public juce::UnitTest {
public:
    MentorServiceTests() : juce::UnitTest("MentorServiceTests") {}

    void runTest() override {
        beginTest("Section_ids_all_present");
        samplar::EventBus bus;
        samplar::MentorService svc(bus);
        svc.initialise();
        const auto& sections = svc.getSections();
        expectEquals(sections.size(), 4);
        expect(svc.getSection("fullsong") != nullptr);
        expect(svc.getSection("drumstem") != nullptr);
        expect(svc.getSection("smode")    != nullptr);
        expect(svc.getSection("drumlib")  != nullptr);
        svc.shutdown();
    }
};

static MentorServiceTests mentorServiceTests;
```

Wire this source file into whatever test runner the project already uses (search `CMakeLists.txt` for `UnitTestRunner` or `tests` target). If there is no tests target, run the JUCE unit tests via a small wrapper `int main(){ juce::UnitTestRunner r; r.runAllTests(); }` in a new `tests/main.cpp`, gated under a `BUILD_TESTS` option that is OFF by default; then build with `-DBUILD_TESTS=ON` locally.

- [ ] **Step 2: Build tests and run — expect FAIL**

Expected: test fails (either build error "undefined symbol" for BinaryData, or runtime `sections.size() == 0`).

- [ ] **Step 3: Implement `loadFromBinaryData` in `MentorService.cpp`**

```cpp
#include "MentorBinaryData.h"

void MentorService::loadFromBinaryData() {
    sections_.clearQuick();

    int dataSize = 0;
    const char* data = MentorBinaryData::getNamedResource("mentor_lessons_json", dataSize);
    if (data == nullptr || dataSize <= 0) {
        juce::Logger::writeToLog(juce::String("?? ") + LOG + " ? mentor_lessons.json not found in BinaryData");
        return;
    }

    juce::var root = juce::JSON::parse(juce::String::fromUTF8(data, dataSize));
    if (! root.isObject()) {
        juce::Logger::writeToLog(juce::String("?? ") + LOG + " ? failed to parse mentor_lessons.json");
        return;
    }

    const juce::var& sectionsVar = root.getProperty("sections", {});
    if (! sectionsVar.isArray()) return;

    for (int i = 0; i < sectionsVar.size(); ++i) {
        const juce::var& s = sectionsVar[i];
        MentorSection sec;
        sec.id          = s.getProperty("id", "").toString();
        sec.title       = s.getProperty("title", "").toString();
        sec.description = s.getProperty("description", "").toString();
        sec.icon        = s.getProperty("icon", "").toString();

        const juce::var& lessons = s.getProperty("lessons", {});
        if (lessons.isArray()) {
            for (int j = 0; j < lessons.size(); ++j) {
                const juce::var& l = lessons[j];
                MentorLesson lesson;
                lesson.id               = l.getProperty("id", "").toString();
                lesson.title            = l.getProperty("title", "").toString();
                lesson.estimatedMinutes = (int) l.getProperty("estimatedMinutes", 0);

                const juce::var& steps = l.getProperty("steps", {});
                if (steps.isArray()) {
                    for (int k = 0; k < steps.size(); ++k) {
                        const juce::var& st = steps[k];
                        MentorStep step;
                        step.title     = st.getProperty("title", "").toString();
                        step.body      = st.getProperty("body", "").toString();
                        step.imageName = st.getProperty("image", "").toString();
                        lesson.steps.add(step);
                    }
                }
                sec.lessons.add(lesson);
            }
        }
        sections_.add(sec);
    }
}
```

Note: the BinaryData symbol name is derived from the file name. `mentor_lessons.json` ? `MentorBinaryData::mentor_lessons_json` or accessible by `getNamedResource("mentor_lessons_json", size)`. If the generated header uses a different casing, adjust the string to match — inspect `MentorBinaryData.h` to confirm the exact symbol.

- [ ] **Step 4: Build and run tests — expect PASS**

- [ ] **Step 5: Add tests for navigation**

Append to `MentorServiceTests::runTest()`:

```cpp
beginTest("nextStep_prevStep_bounds");
{
    samplar::EventBus bus;
    samplar::MentorService svc(bus);
    svc.initialise();
    svc.openLesson("fullsong", "fullsong_intro");
    expect(svc.getCurrentStep() != nullptr);
    expectEquals(svc.currentStepIndex(), 0);
    svc.prevStep();
    expectEquals(svc.currentStepIndex(), 0); // no underflow
    svc.nextStep(); svc.nextStep();
    expectEquals(svc.currentStepIndex(), 2);
    svc.nextStep(); // past last step -> marks completed, index stays
    expect(svc.isLessonCompleted("fullsong_intro"));
    svc.shutdown();
}
```

- [ ] **Step 6: Build and run tests — expect PASS**

- [ ] **Step 7: Commit**

```bash
git add src/mentor/MentorService.cpp tests/mentor/MentorServiceTests.cpp CMakeLists.txt
git commit -m "feat(mentor): load lessons from BinaryData; add unit tests"
```

---

## Task 4: Persistence via PropertiesFile

**Files:**
- Modify: `src/mentor/MentorService.cpp` (implement `loadCompletedFromPrefs` + `saveCompletedToPrefs`)
- Modify: `tests/mentor/MentorServiceTests.cpp` (add round-trip test)

- [ ] **Step 1: Write failing round-trip test**

Append:

```cpp
beginTest("completed_lessons_persist_round_trip");
{
    samplar::EventBus bus;
    {
        samplar::MentorService a(bus);
        a.initialise();
        a.openLesson("smode", "smode_intro");
        a.nextStep(); a.nextStep(); // past last -> completes
        expect(a.isLessonCompleted("smode_intro"));
        a.shutdown();
    }
    {
        samplar::MentorService b(bus);
        b.initialise();
        expect(b.isLessonCompleted("smode_intro"));
        b.shutdown();
    }
}
```

Run — expect FAIL (second service does not load state yet).

- [ ] **Step 2: Implement helpers**

```cpp
#include <juce_data_structures/juce_data_structures.h>

static juce::PropertiesFile::Options prefsOptions() {
    juce::PropertiesFile::Options o;
    o.applicationName     = "Samplar";
    o.filenameSuffix      = ".settings";
    o.folderName          = "Samplar";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

void MentorService::loadCompletedFromPrefs() {
    juce::PropertiesFile props(prefsOptions());
    const auto s = props.getValue("mentor.completedLessons", "");
    completedLessons_.clear();
    if (s.isEmpty()) return;
    juce::StringArray parts;
    parts.addTokens(s, " ", "");
    for (auto& p : parts) if (p.isNotEmpty()) completedLessons_.insert(p);
}

void MentorService::saveCompletedToPrefs() const {
    juce::PropertiesFile props(prefsOptions());
    juce::String joined;
    for (const auto& id : completedLessons_) {
        if (joined.isNotEmpty()) joined += " ";
        joined += id;
    }
    props.setValue("mentor.completedLessons", joined);
    props.saveIfNeeded();
}
```

- [ ] **Step 3: Build and run tests — expect PASS**

- [ ] **Step 4: Commit**

```bash
git add src/mentor/MentorService.cpp tests/mentor/MentorServiceTests.cpp
git commit -m "feat(mentor): persist completedLessons across sessions"
```

---

## Task 5: MentorPage UI (Browse, Lesson, Complete states)

**Files:**
- Create: `src/mentor/ui/MentorPage.h` / `.cpp`
- Create: `src/mentor/ui/MentorSectionList.h` / `.cpp`
- Create: `src/mentor/ui/MentorStepView.h` / `.cpp`
- Modify: `CMakeLists.txt` (add the three `.cpp` files)

- [ ] **Step 1: Create `MentorSectionList.h` / `.cpp`**

`MentorSectionList.h`:
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "../MentorTypes.h"

namespace samplar {

class MentorService;

class MentorSectionList : public juce::Component {
public:
    using LessonClickFn = std::function<void(const juce::String& sectionId, const juce::String& lessonId)>;

    MentorSectionList(MentorService& svc);
    void setLessonClickCallback(LessonClickFn fn) { onLessonClick_ = std::move(fn); }
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Row { juce::String sectionId, lessonId, label; bool isSectionHeader{false}; bool completed{false}; };
    void rebuildRows();
    void mouseUp(const juce::MouseEvent&) override;

    MentorService& service_;
    juce::Array<Row> rows_;
    int hoveredRow_ { -1 };
    LessonClickFn onLessonClick_;
};

} // namespace samplar
```

`MentorSectionList.cpp`:
```cpp
#include "MentorSectionList.h"
#include "../MentorService.h"

namespace samplar {

MentorSectionList::MentorSectionList(MentorService& s) : service_(s) { rebuildRows(); }

void MentorSectionList::refresh() { rebuildRows(); repaint(); }

void MentorSectionList::rebuildRows() {
    rows_.clearQuick();
    for (const auto& sec : service_.getSections()) {
        Row h; h.sectionId = sec.id; h.label = sec.title; h.isSectionHeader = true;
        rows_.add(h);
        for (const auto& l : sec.lessons) {
            Row r;
            r.sectionId = sec.id;
            r.lessonId  = l.id;
            r.label     = juce::String("  • ") + l.title;
            r.completed = service_.isLessonCompleted(l.id);
            rows_.add(r);
        }
    }
}

void MentorSectionList::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1a));
    const int rowH = 24;
    for (int i = 0; i < rows_.size(); ++i) {
        auto r = juce::Rectangle<int>(0, i * rowH, getWidth(), rowH);
        if (i == hoveredRow_ && ! rows_[i].isSectionHeader)
            g.setColour(juce::Colour(0xff2a2a2a)), g.fillRect(r);
        g.setColour(rows_[i].isSectionHeader ? juce::Colours::white : juce::Colour(0xffcfcfcf));
        g.setFont(rows_[i].isSectionHeader ? 14.0f : 12.0f);
        juce::String text = rows_[i].label + (rows_[i].completed ? juce::String("  ?") : juce::String());
        g.drawFittedText(text, r.reduced(8, 0), juce::Justification::centredLeft, 1);
    }
}

void MentorSectionList::resized() {}

void MentorSectionList::mouseUp(const juce::MouseEvent& e) {
    const int rowH = 24;
    const int idx = e.y / rowH;
    if (idx < 0 || idx >= rows_.size()) return;
    const auto& r = rows_[idx];
    if (r.isSectionHeader || onLessonClick_ == nullptr) return;
    onLessonClick_(r.sectionId, r.lessonId);
}

} // namespace samplar
```

- [ ] **Step 2: Create `MentorStepView.h` / `.cpp`**

`MentorStepView.h`:
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace samplar {

class MentorService;

class MentorStepView : public juce::Component {
public:
    MentorStepView(MentorService& svc);

    void refreshFromService();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MentorService& service_;
    juce::TextButton backBtn_ { "Back" };
    juce::TextButton nextBtn_ { "Next" };
};

} // namespace samplar
```

`MentorStepView.cpp`:
```cpp
#include "MentorStepView.h"
#include "../MentorService.h"

namespace samplar {

MentorStepView::MentorStepView(MentorService& s) : service_(s) {
    addAndMakeVisible(backBtn_);
    addAndMakeVisible(nextBtn_);
    backBtn_.onClick = [this]{ service_.prevStep(); refreshFromService(); };
    nextBtn_.onClick = [this]{ service_.nextStep(); refreshFromService(); };
}

void MentorStepView::refreshFromService() { repaint(); }

void MentorStepView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff202020));
    const auto* step = service_.getCurrentStep();
    auto area = getLocalBounds().reduced(16);
    if (step == nullptr) {
        g.setColour(juce::Colours::white);
        g.setFont(16.0f);
        g.drawFittedText("Select a lesson to begin.", area, juce::Justification::centred, 3);
        return;
    }
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
    auto titleArea = area.removeFromTop(28);
    g.drawFittedText(step->title, titleArea, juce::Justification::centredLeft, 1);
    area.removeFromTop(8);
    g.setFont(14.0f);
    g.drawFittedText(step->body, area.removeFromTop(area.getHeight() - 48), juce::Justification::topLeft, 20);
}

void MentorStepView::resized() {
    auto area = getLocalBounds().reduced(16);
    auto buttons = area.removeFromBottom(32);
    backBtn_.setBounds(buttons.removeFromLeft(80));
    buttons.removeFromLeft(8);
    nextBtn_.setBounds(buttons.removeFromLeft(80));
}

} // namespace samplar
```

- [ ] **Step 3: Create `MentorPage.h` / `.cpp`**

`MentorPage.h`:
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../app/EventBus.h"
#include "MentorSectionList.h"
#include "MentorStepView.h"

namespace samplar {

class MentorService;

class MentorPage : public juce::Component {
public:
    MentorPage(MentorService& svc, EventBus& bus);
    ~MentorPage() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MentorService& service_;
    EventBus& bus_;
    std::vector<std::pair<std::string, EventBus::HandlerId>> subs_;

    MentorSectionList sections_;
    MentorStepView    stepView_;
};

} // namespace samplar
```

`MentorPage.cpp`:
```cpp
#include "MentorPage.h"
#include "../MentorService.h"

namespace samplar {

MentorPage::MentorPage(MentorService& s, EventBus& b)
    : service_(s), bus_(b), sections_(s), stepView_(s) {
    addAndMakeVisible(sections_);
    addAndMakeVisible(stepView_);

    sections_.setLessonClickCallback([this](const juce::String& sid, const juce::String& lid) {
        service_.openLesson(sid, lid);
    });

    auto resubscribe = [this](const std::string& topic, std::function<void(const juce::var&)> fn) {
        subs_.emplace_back(topic, bus_.subscribe(topic, std::move(fn)));
    };
    resubscribe("mentor.stepChanged",     [this](const juce::var&){ stepView_.refreshFromService(); });
    resubscribe("mentor.lessonOpened",    [this](const juce::var&){ stepView_.refreshFromService(); sections_.refresh(); });
    resubscribe("mentor.lessonCompleted", [this](const juce::var&){ stepView_.refreshFromService(); sections_.refresh(); });
    resubscribe("mentor.openLesson",      [this](const juce::var& p){
        if (auto* o = p.getDynamicObject()) {
            service_.openLesson(o->getProperty("sectionId").toString(), o->getProperty("lessonId").toString());
        }
    });
}

MentorPage::~MentorPage() {
    for (auto& s : subs_) bus_.unsubscribe(s.first, s.second);
}

void MentorPage::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff181818)); }

void MentorPage::resized() {
    auto a = getLocalBounds();
    sections_.setBounds(a.removeFromLeft(220));
    stepView_.setBounds(a);
}

} // namespace samplar
```

- [ ] **Step 4: Add the three new `.cpp` files to `CMakeLists.txt`**

```cmake
    src/mentor/ui/MentorPage.cpp
    src/mentor/ui/MentorSectionList.cpp
    src/mentor/ui/MentorStepView.cpp
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config Release` — expect success. Page is not yet wired into nav.

- [ ] **Step 6: Commit**

```bash
git add src/mentor/ui/ CMakeLists.txt
git commit -m "feat(mentor): MentorPage with section list and step view"
```

---

## Task 6: MentorHelperButton (floating "?" per workflow page)

**Files:**
- Create: `src/mentor/ui/MentorHelperButton.h` / `.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `MentorHelperButton.h`**

```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../app/EventBus.h"

namespace samplar {

class MentorService;

class MentorHelperButton : public juce::Component {
public:
    MentorHelperButton(MentorService& svc, EventBus& bus, const juce::String& sectionId);

    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static bool globallyVisible();
    static void setGloballyVisible(bool v);

private:
    void showPopup();

    MentorService& service_;
    EventBus& bus_;
    juce::String sectionId_;
};

} // namespace samplar
```

- [ ] **Step 2: Create `MentorHelperButton.cpp`**

```cpp
#include "MentorHelperButton.h"
#include "../MentorService.h"
#include <juce_data_structures/juce_data_structures.h>

namespace samplar {

static juce::PropertiesFile::Options helperPrefs() {
    juce::PropertiesFile::Options o;
    o.applicationName = "Samplar";
    o.filenameSuffix  = ".settings";
    o.folderName      = "Samplar";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

bool MentorHelperButton::globallyVisible() {
    juce::PropertiesFile p(helperPrefs());
    return p.getBoolValue("mentor.helperVisible", true);
}

void MentorHelperButton::setGloballyVisible(bool v) {
    juce::PropertiesFile p(helperPrefs());
    p.setValue("mentor.helperVisible", v);
    p.saveIfNeeded();
}

MentorHelperButton::MentorHelperButton(MentorService& s, EventBus& b, const juce::String& sectionId)
    : service_(s), bus_(b), sectionId_(sectionId) {
    setInterceptsMouseClicks(true, false);
    setVisible(globallyVisible());
}

void MentorHelperButton::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colour(0xff3a86ff));
    g.fillEllipse(r);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(16.0f).withStyle("Bold")));
    g.drawFittedText("?", getLocalBounds(), juce::Justification::centred, 1);
}

void MentorHelperButton::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) {
        juce::PopupMenu m;
        m.addItem(1, "Hide mentor helper");
        m.showMenuAsync({}, [this](int choice){
            if (choice == 1) { setGloballyVisible(false); if (auto* p = getParentComponent()) p->repaint(); setVisible(false); }
        });
        return;
    }
    showPopup();
}

void MentorHelperButton::showPopup() {
    const auto* sec = service_.getSection(sectionId_);
    if (sec == nullptr) return;
    juce::PopupMenu menu;
    int id = 1;
    juce::Array<juce::String> lessonIds;
    for (const auto& l : sec->lessons) {
        juce::String label = l.title + (service_.isLessonCompleted(l.id) ? juce::String("  ?") : juce::String());
        menu.addItem(id++, label);
        lessonIds.add(l.id);
    }
    auto secId = sectionId_;
    menu.showMenuAsync({}, [this, secId, lessonIds](int choice){
        if (choice <= 0 || choice > lessonIds.size()) return;
        juce::var payload(new juce::DynamicObject());
        payload.getDynamicObject()->setProperty("sectionId", secId);
        payload.getDynamicObject()->setProperty("lessonId",  lessonIds[choice - 1]);
        bus_.publish("mentor.openLesson", payload);
        bus_.publish("ui.navigate", juce::String("mentor"));
    });
}

} // namespace samplar
```

- [ ] **Step 3: Add `src/mentor/ui/MentorHelperButton.cpp` to CMakeLists.txt**

- [ ] **Step 4: Build — expect success**

- [ ] **Step 5: Commit**

```bash
git add src/mentor/ui/MentorHelperButton.h src/mentor/ui/MentorHelperButton.cpp CMakeLists.txt
git commit -m "feat(mentor): floating MentorHelperButton with per-section popup"
```

---

## Task 7: Wire mentor into App bootstrap + navigation (replace Chat)

**Files:**
- Modify: `src/app/App.cpp` (remove ChatService registration; add `registerMentorModule` call)
- Modify: `src/plugin/SamplarAudioProcessor.cpp` (same — ChatService ? Mentor)
- Modify: `src/ui/MainWindow.cpp` (replace `ChatPage chat_;` member and its nav entry with `MentorPage mentor_;` wired via `ServiceRegistry.get("mentor")`)

- [ ] **Step 1: In `src/app/App.cpp`**, replace:

```cpp
#include "../services/ChatService.h"
// ...
juce::Logger::writeToLog("???? [App] Registering ChatService");
serviceRegistry->registerService("chat", std::make_unique<samplar::ChatService>(eventBus_));
```

with:

```cpp
#include "../mentor/MentorRegistration.h"
// ...
juce::Logger::writeToLog("?? [App] Registering MentorService");
samplar::registerMentorModule(*serviceRegistry, eventBus_);
```

- [ ] **Step 2: Same substitution in `src/plugin/SamplarAudioProcessor.cpp`** (lines 247–248 per grep at plan time — re-verify).

- [ ] **Step 3: In `src/ui/MainWindow.cpp`**, replace `ChatPage chat_;` member (line ~1760) with:

```cpp
#include "../mentor/ui/MentorPage.h"
// ...
std::unique_ptr<samplar::MentorPage> mentor_;
```

and where the page is constructed and added to the nav (look for where `chat_` is added as a tab/page), replace with:

```cpp
if (auto* svc = dynamic_cast<samplar::MentorService*>(serviceRegistry_->get("mentor"))) {
    mentor_ = std::make_unique<samplar::MentorPage>(*svc, eventBus_);
    // add mentor_.get() to the nav in the same slot chat_ occupied, with label "Mentor"
}
```

The exact nav-add API depends on MainWindow's existing pattern — copy the `chat_` add pattern verbatim and substitute. Remove `#include "Pages/ChatPage.h"` and any `chat_.*` references.

Ensure `ui.navigate` events with payload `"mentor"` navigate to this page; copy whatever dispatch pattern the other pages use.

- [ ] **Step 4: Build — expect success; app starts; "Mentor" appears in nav where Chat used to be**

- [ ] **Step 5: Smoke test**

Launch the standalone app:
```bash
"build/SM5App_artefacts/Release/SAMPLAR.exe"
```
Navigate to Mentor ? click "Getting Started with Full Song" ? step through with Next ? completion advances back to step view with ? on the lesson.

- [ ] **Step 6: Commit**

```bash
git add src/app/App.cpp src/plugin/SamplarAudioProcessor.cpp src/ui/MainWindow.cpp
git commit -m "feat(mentor): register MentorService; replace Chat nav entry with Mentor"
```

---

## Task 8: Attach MentorHelperButton to the four workflow pages

**Files:**
- Modify: each of the four host Component files identified in the Pre-Task notes.

For each host Component (call it `HostPage`), apply this pattern (one line of includes + one member + one `addAndMakeVisible` + one `resized` placement):

- [ ] **Step 1: Add include + member to `HostPage.h`**

```cpp
#include "../../mentor/ui/MentorHelperButton.h"
// ...
private:
    std::unique_ptr<samplar::MentorHelperButton> mentorHelper_;
```

- [ ] **Step 2: In `HostPage.cpp` constructor**, after the page has a pointer/ref to `MentorService` and `EventBus` (retrieve via the same mechanism other components use — typically `serviceRegistry_` on MainWindow, accessible via parent traversal or a passed reference):

```cpp
if (auto* svc = dynamic_cast<samplar::MentorService*>(registry.get("mentor"))) {
    mentorHelper_ = std::make_unique<samplar::MentorHelperButton>(*svc, bus, /*sectionId*/ "<fullsong|drumstem|smode|drumlib>");
    addAndMakeVisible(*mentorHelper_);
}
```

Use the section id corresponding to that host page (from the Pre-Task notes).

- [ ] **Step 3: In `HostPage::resized()`**, after existing layout logic:

```cpp
if (mentorHelper_ != nullptr) {
    const int sz = 32, pad = 12;
    mentorHelper_->setBounds(getWidth() - sz - pad, getHeight() - sz - pad, sz, sz);
    mentorHelper_->toFront(false);
}
```

- [ ] **Step 4: Repeat Steps 1–3 for all four host Components**

- [ ] **Step 5: Build — expect success**

- [ ] **Step 6: Smoke test**

For each of the four workflows: open the page ? "?" appears bottom-right ? click it ? popup lists that section's lessons ? select a lesson ? Mentor page opens on step 1 of that lesson.

- [ ] **Step 7: Right-click hide test**

Right-click the "?" on any page ? "Hide mentor helper" ? all four helpers vanish immediately (this page) and on next app launch remain hidden across all four pages. Re-enable by flipping the pref manually for now (Task 9 adds the proper toggle).

- [ ] **Step 8: Commit**

```bash
git add src/ui/Pages/
git commit -m "feat(mentor): attach floating helper button to the four workflow pages"
```

---

## Task 9: "Show mentor helper" toggle in MentorPage header

**Files:**
- Modify: `src/mentor/ui/MentorPage.h` / `.cpp`

- [ ] **Step 1: Add header with toggle**

In `MentorPage.h`:
```cpp
#include "MentorHelperButton.h"
// ...
private:
    juce::ToggleButton helperToggle_ { "Show mentor helper on pages" };
```

- [ ] **Step 2: Wire toggle in `MentorPage.cpp` constructor**

```cpp
addAndMakeVisible(helperToggle_);
helperToggle_.setToggleState(MentorHelperButton::globallyVisible(), juce::dontSendNotification);
helperToggle_.onClick = [this] {
    MentorHelperButton::setGloballyVisible(helperToggle_.getToggleState());
    if (auto* top = getTopLevelComponent()) top->repaint();
};
```

And in `resized()`, reserve 28px at the top for the header and place the toggle there; the rest is split between `sections_` and `stepView_` as before:

```cpp
auto a = getLocalBounds();
auto header = a.removeFromTop(28);
helperToggle_.setBounds(header.reduced(8, 2));
sections_.setBounds(a.removeFromLeft(220));
stepView_.setBounds(a);
```

- [ ] **Step 3: Build — expect success**

- [ ] **Step 4: Smoke test**

Right-click helper ? hide ? open MentorPage ? toggle shows OFF. Click toggle ON ? return to a workflow page ? helper visible again.

- [ ] **Step 5: Commit**

```bash
git add src/mentor/ui/MentorPage.h src/mentor/ui/MentorPage.cpp
git commit -m "feat(mentor): MentorPage header toggle to show/hide helper"
```

---

## Task 10: Deprecate old Chat files

**Files:**
- Move: `src/services/ChatService.{h,cpp}` ? `src/deprecated/ChatService.{h,cpp}`
- Move: `src/ui/Pages/ChatPage.{h,cpp}` ? `src/deprecated/ChatPage.{h,cpp}`
- Create: `src/deprecated/README.md`
- Modify: `CMakeLists.txt` (remove the four moved files from `target_sources`)

- [ ] **Step 1: Move the files**

```bash
mkdir -p src/deprecated
git mv src/services/ChatService.h   src/deprecated/ChatService.h
git mv src/services/ChatService.cpp src/deprecated/ChatService.cpp
git mv src/ui/Pages/ChatPage.h      src/deprecated/ChatPage.h
git mv src/ui/Pages/ChatPage.cpp    src/deprecated/ChatPage.cpp
```

- [ ] **Step 2: Write `src/deprecated/README.md`**

```markdown
# Deprecated

Files here are retained for one release cycle and excluded from the build.

- `ChatService.{h,cpp}` / `ChatPage.{h,cpp}` — replaced by `src/mentor/` in the release dated 2026-04-21. Delete after the next release.
```

- [ ] **Step 3: Remove the four moved files from `CMakeLists.txt` `target_sources`**

Search for `ChatService.cpp` and `ChatPage.cpp` in `CMakeLists.txt` and delete those lines.

- [ ] **Step 4: Build — expect success; no references to `ChatService` or `ChatPage` remain in compiled sources**

If the build fails with an unresolved include/symbol for `ChatService`/`ChatPage`, grep for the leftover reference and remove it.

- [ ] **Step 5: Commit**

```bash
git add -A src/deprecated/ src/services/ src/ui/Pages/ CMakeLists.txt
git commit -m "refactor(mentor): move deprecated Chat files to src/deprecated/"
```

---

## Task 11: Final verification

- [ ] **Step 1: Full clean build**

```bash
cmake --build build --config Release --clean-first
```
Expected: success, new `SAMPLAR.exe` produced.

- [ ] **Step 2: Run unit tests** — all pass.

- [ ] **Step 3: Acceptance walkthrough (spec §13)**

- [ ] No Chat page in nav.
- [ ] Mentor page present; four sections render.
- [ ] Each of the four workflow pages shows the "?" when `mentor.helperVisible=true`.
- [ ] Clicking "?" opens a scoped popup; selecting a lesson navigates to MentorPage on step 1.
- [ ] Next/Back works within bounds; completion screen (step view with lesson marked ?) appears; ? persists after restart.
- [ ] Right-click "?" hides helpers globally; MentorPage toggle re-enables them.

- [ ] **Step 4: Tag and commit final state**

```bash
git commit --allow-empty -m "chore(mentor): verified full mentor module behaviour"
```

---

## Self-Review Notes

**Spec coverage check:**
- Module isolation (spec §4.1) ? Tasks 1, 5, 6, 9.
- JSON schema + types (§5) ? Tasks 1, 2, 3.
- UI layout (§6) ? Tasks 5, 6, 9.
- State & persistence (§7) ? Task 4.
- Deprecation (§8) ? Tasks 7 and 10 (registration removal + file moves).
- Testing (§9) ? Tasks 3, 4, 8, 11.
- Logging (§10) ? emitted in MentorService (Task 1) and helper (Task 6); UI logs intentionally minimal to avoid noise.
- Authoring workflow (§11) ? covered implicitly by Tasks 2 and 3.
- Acceptance criteria (§13) ? Task 11 walkthrough.

**Known fuzzy area:** The exact host Component classes for `fullsong`, `drumstem`, `drumlib` are determined during the Pre-Task. All downstream tasks that touch "the four workflow pages" depend on that table.

**Known environment-dependent detail:** The BinaryData symbol name generated for `mentor_lessons.json` (`mentor_lessons_json` vs camelCase) must be verified against `MentorBinaryData.h` after the first build in Task 2/3.
