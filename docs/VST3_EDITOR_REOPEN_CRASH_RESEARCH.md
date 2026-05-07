# VST3 Editor Reopen Crash — Research Findings

**Date:** 2026-03-17
**Problem:** SamplarAudioProcessorEditor crashes in FL Studio when the plugin window is closed and reopened. Log shows `[SamplarAudioProcessorEditor] Destructor` then silence — crash happens during destruction or very early in re-creation.

---

## Root Cause Candidates (Ranked by Likelihood)

### 1. LookAndFeel Dangling Pointer — HIGHEST RISK

**What happens:** If `setDefaultLookAndFeel(&myLookAndFeel)` is called in the editor constructor, and `myLookAndFeel` is a member of the editor (or destroyed before all components that reference it), the destroyed L&F leaves a dangling pointer in `Desktop::getInstance()`. The next editor construction triggers `Component::internalHierarchyChanged()` ? `ComboBox::lookAndFeelChanged()` on any combo/slider that still holds the stale WeakReference — crash.

**Exact crash stack from JUCE forum (2015, thread #14217):**
```
juce::ComboBox::lookAndFeelChanged()
juce::Component::internalHierarchyChanged()
juce::Component::internalHierarchyChanged()
juce::Component::addChildComponent()
MyPluginAudioProcessorEditor::MyPluginAudioProcessorEditor()   ? crash on REOPEN
juce::AudioProcessor::createEditorIfNeeded()
```
The crash happens on the **second open** because the first open set a global default L&F pointer that was then destroyed when the editor closed.

**JUCE assertion in LookAndFeel destructor (verbatim):**
```cpp
// LookAndFeel::~LookAndFeel()
jassert (masterReference.getNumActiveWeakReferences() == 0
          || (masterReference.getNumActiveWeakReferences() == 1
               && this == &getDefaultLookAndFeel()));
// "Deleting a LookAndFeel is unlikely to cause a crash since most things will use a
// safe WeakReference to it, but it could cause some unexpected graphical behaviour"
// -- Note: in practice, on reopen it DOES crash via lookAndFeelChanged callbacks.
```

**Fix — three-part solution:**

**Part A: Never call `setDefaultLookAndFeel()` in a plugin editor.**
```cpp
// BAD — sets a process-wide pointer; destroyed editor leaves dangling pointer
LookAndFeel::setDefaultLookAndFeel(&myLookAndFeel);

// GOOD — scoped to this editor's component tree only
setLookAndFeel(&myLookAndFeel);  // on the editor itself; propagates to children
```
Source: [forum.juce.com/t/lookandfeel-issue-with-multiple-plugin-instances/44548](https://forum.juce.com/t/lookandfeel-issue-with-multiple-plugin-instances/44548) — user HowardAntares confirmed: *"Making that change [to setLookAndFeel] fixes it."*
Also: [forum.juce.com/t/isnt-it-a-bad-practice-to-call-setdefaultlookandfeel/52829](https://forum.juce.com/t/isnt-it-a-bad-practice-to-call-setdefaultlookandfeel/52829) — andrewj (JUCE contributor): *"if you are developing plugins then avoid using setDefaultLookAndFeel()"*

**Part B: Clear L&F references in the destructor before members are destroyed.**
```cpp
SamplarAudioProcessorEditor::~SamplarAudioProcessorEditor()
{
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor");

    // CRITICAL: Clear L&F references on all components before myLookAndFeel is destroyed
    setLookAndFeel(nullptr);          // clears on editor + all children
    // OR explicitly per-component if you used per-component setLookAndFeel():
    // mySlider.setLookAndFeel(nullptr);
    // myButton.setLookAndFeel(nullptr);
}
```
Source: [forum.juce.com/t/how-to-properly-clear-lookandfeel-references/25053](https://forum.juce.com/t/how-to-properly-clear-lookandfeel-references/25053) — correct answer from Toddler-Boy: `mySlider.setLookAndFeel(nullptr);`

**Part C: Declare LookAndFeel member BEFORE component members in the header.**
```cpp
class SamplarAudioProcessorEditor : public juce::AudioProcessorEditor
{
    // LookAndFeel declared FIRST ? destroyed LAST (C++ reverse-order destruction)
    SM5LookAndFeel myLookAndFeel;

    // Components declared after ? destroyed BEFORE myLookAndFeel
    std::unique_ptr<juce::Slider> mySlider;
    std::unique_ptr<juce::ComboBox> myCombo;
};
```
Source: [forum.juce.com/t/how-to-properly-clear-lookandfeel-references/25053](https://forum.juce.com/t/how-to-properly-clear-lookandfeel-references/25053) — andrewj: *"declare the LookAndFeel object before you declare any of the components that use that object. This means the components will be destroyed before the LookAndFeel is destroyed."*

---

### 2. Child Components Not Removed Before Destructor — HIGH RISK

**What happens:** JUCE `Component` does NOT automatically remove children from the parent when a `unique_ptr` child is destroyed mid-sequence. If a child component is destroyed while still registered with the parent (i.e., `addAndMakeVisible` was called), JUCE fires `componentBeingDeleted()` callbacks, which can touch the parent or sibling components that may already be partially destroyed.

**The rule:** JUCE `Component` destructor does call `removeAllChildren()` on itself, BUT this happens at the `Component` base class level, after your subclass destructor has already run. If any child destructor references the parent (e.g., via a raw pointer back-reference), the parent may already be partially torn down.

**Fix:**
```cpp
SamplarAudioProcessorEditor::~SamplarAudioProcessorEditor()
{
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor");

    // Clear L&F FIRST (before any children are destroyed)
    setLookAndFeel(nullptr);

    // Explicitly reset unique_ptr children in controlled order if they have
    // back-references to the editor or to each other:
    myComplexChild.reset();
    myOtherChild.reset();
    // Simple children with no back-references: safe to let destructor order handle them
}
```

**Key insight:** `addAndMakeVisible` with `unique_ptr` children is safe AS LONG AS:
- The child does not hold a raw pointer back to the parent
- The child's destructor does not trigger paint/repaint on siblings

---

### 3. Separate/Floating Windows Left Open — MEDIUM RISK

**What happens:** Any `DocumentWindow`, `DialogWindow`, or floating popup owned by the editor that is NOT explicitly closed in the destructor will leave a dangling native window handle. Some VST3 hosts (Cubase, FL Studio) crash if a child native window outlives the plugin window. Confirmed by JUCE forum thread #40761.

**Quote from HowardAntares ([thread #40761](https://forum.juce.com/t/bug-juce-6-crashes-on-vst3-delete-with-open-windows-juce-5-does-not/40761)):**
> *"If you're truly creating a separate 'window', then you can't leave those open when closing a plugin. Some VST3 hosts (like Cubase) crash if a window is left open, even if it's just a popup menu. All open windows need to be explicitly closed when the editor's destructor is executed."*

**Fix:**
```cpp
SamplarAudioProcessorEditor::~SamplarAudioProcessorEditor()
{
    setLookAndFeel(nullptr);

    // Close any floating windows explicitly
    if (myFloatingWindow != nullptr)
        myFloatingWindow.reset();

    // Close any active popup menus
    juce::PopupMenu::dismissAllActiveMenus();
}
```

---

### 4. `setDefaultLookAndFeel` With Multiple Instances — MEDIUM RISK

**What happens:** `setDefaultLookAndFeel()` calls `Desktop::getInstance().setDefaultLookAndFeel()` — there is ONE default L&F per process. If two instances of SAMPLAR are loaded, and instance B is closed (destroying its L&F while it was set as default), instance A's components all lose their L&F silently. On next paint/interaction ? crash or assertion.

**Fix:** Use `SharedResourcePointer<SM5LookAndFeel>` for a shared L&F across instances, OR call `setLookAndFeel()` on the editor root only (not the global default).

```cpp
// Option A: SharedResourcePointer (safe for multi-instance)
juce::SharedResourcePointer<SM5LookAndFeel> sharedLookAndFeel;
// In constructor:
setLookAndFeel(sharedLookAndFeel.get());
// In destructor:
setLookAndFeel(nullptr);

// Option B: Per-editor (cleanest, zero global state)
SM5LookAndFeel myLookAndFeel; // member, declared before all components
// In constructor:
setLookAndFeel(&myLookAndFeel);
// In destructor:
setLookAndFeel(nullptr);
```

---

## Canonical Safe AudioProcessorEditor Pattern

```cpp
// SamplarAudioProcessorEditor.h
class SamplarAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    SamplarAudioProcessorEditor(SamplarAudioProcessor&);
    ~SamplarAudioProcessorEditor() override;

private:
    // RULE: LookAndFeel FIRST — destroyed LAST
    SM5LookAndFeel lookAndFeel;

    // Components after — destroyed BEFORE lookAndFeel
    std::unique_ptr<juce::Slider>   gainSlider;
    std::unique_ptr<juce::ComboBox> engineCombo;
    // ... other children

    SamplarAudioProcessor& processor;
};

// SamplarAudioProcessorEditor.cpp
SamplarAudioProcessorEditor::SamplarAudioProcessorEditor(SamplarAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Constructor");

    // Apply L&F to THIS editor (propagates to all children automatically)
    // NEVER call LookAndFeel::setDefaultLookAndFeel() here
    setLookAndFeel(&lookAndFeel);

    gainSlider = std::make_unique<juce::Slider>();
    addAndMakeVisible(*gainSlider);

    setSize(800, 600);
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Constructor complete");
}

SamplarAudioProcessorEditor::~SamplarAudioProcessorEditor()
{
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor START");

    // STEP 1: Clear L&F references BEFORE any children are destroyed
    setLookAndFeel(nullptr);

    // STEP 2: Close any floating windows/popups explicitly
    juce::PopupMenu::dismissAllActiveMenus();

    // STEP 3: Explicitly reset children that have back-references (if any)
    // gainSlider.reset(); // only needed if gainSlider holds a raw ptr to editor

    // unique_ptr members auto-destruct in reverse declaration order after this
    juce::Logger::writeToLog("[SamplarAudioProcessorEditor] Destructor END");
}
```

---

## Sources

- [forum.juce.com — LookAndFeel crash when unload and reload](https://forum.juce.com/t/look-and-feel-crash-when-unload-and-reload/14217) — crash stack showing `ComboBox::lookAndFeelChanged` on reopen
- [forum.juce.com — How to Properly Clear LookAndFeel References](https://forum.juce.com/t/how-to-properly-clear-lookandfeel-references/25053) — `setLookAndFeel(nullptr)` in destructor + declaration order fix
- [forum.juce.com — LookAndFeel issue with multiple plugin instances](https://forum.juce.com/t/lookandfeel-issue-with-multiple-plugin-instances/44548) — `setDefaultLookAndFeel` breaks on multi-instance; fix: use `setLookAndFeel` on editor root
- [forum.juce.com — Isn't it bad practice to call setDefaultLookAndFeel?](https://forum.juce.com/t/isnt-it-a-bad-practice-to-call-setdefaultlookandfeel/52829) — JUCE contributor confirms: avoid in plugins
- [forum.juce.com — JUCE 6 crashes on VST3 delete with open windows](https://forum.juce.com/t/bug-juce-6-crashes-on-vst3-delete-with-open-windows-juce-5-does-not/40761) — all floating windows must be closed in destructor
- [forum.juce.com — Linux VST3 with OpenGL crashing on reopen](https://forum.juce.com/t/linux-vst3-plugin-with-opengl-crashing-when-editor-is-reopened/40549) — JUCE team fix: VST3 wrapper must not remove content component from desktop before destroying
- [forum.juce.com — popupMenu and setDefaultLookAndFeel crash](https://forum.juce.com/t/popupmenu-and-setdefaultlookandfeel-crash/16315) — dangling L&F pointer in MenuWindow::paint
- [forum.juce.com — VST3 distortion plugin crashes in FL Studio](https://forum.juce.com/t/vst3-distortion-plugin-crashes-in-fl-studio-but-works-standalone/52606) — FL Studio VST3 crash debugging workflow
