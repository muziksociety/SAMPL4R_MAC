#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../app/EventBus.h"
#include "../../app/AppState.h"
#include "../../services/smode/SModeService.h"
#include "../Components/smode/StemWaveformComponent.h"
#include "../Components/smode/SModeMidiLearnOverlay.h"
#include "../Components/smode/SequencerLaneComponent.h"
#include <memory>
#include <set>
#include <vector>

namespace samplar {

/** Full-bleed overlay shown when no audio is loaded in S-Mode. */
class LoadAudioOverlay : public juce::Component
{
public:
    LoadAudioOverlay()
    {
        addAndMakeVisible(loadButton_);
        loadButton_.setButtonText("LOAD AUDIO");
        loadButton_.setColour(juce::TextButton::buttonColourId,   juce::Colours::white.withAlpha(0.95f));
        loadButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::white);
        loadButton_.setColour(juce::TextButton::textColourOffId,  juce::Colours::black);
        loadButton_.setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
        loadButton_.onClick = [this]() { if (onLoad) onLoad(); };

        // Don't intercept mouse on the gray panel itself — only the button is clickable
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override
    {
        // Translucent dark-gray fill covering the whole SModePage
        g.fillAll(juce::Colour(0xCC202428));   // ~80% opaque dark grey
    }

    void resized() override
    {
        const int btnW = 240, btnH = 64;
        auto centre = getLocalBounds().getCentre();
        loadButton_.setBounds(juce::Rectangle<int>(0, 0, btnW, btnH).withCentre(centre));
    }

    std::function<void()> onLoad;

private:
    juce::TextButton loadButton_;
};

/**
 * SModePage — main UI page for S-Mode.
 * Displays compact stacked stem waveforms in upper half,
 * controller/transport area below, and segmentation controls at bottom.
 */
class SModePage : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  private juce::Timer,
                  private juce::KeyListener
{
public:
    explicit SModePage(EventBus& bus);
    ~SModePage() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // KeyListener — intercepts keys from any child component
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;

    /** Called when stems are ready — triggers UI rebuild with waveforms. */
    void onStemsReady(SModeService& service);

    // FileDragAndDropTarget — accept audio files for S-Mode separation
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /** Callback to navigate back (kept for MIDI cleanup; no longer navigates). */
    std::function<void()> onBack;

    /** Callback fired when the user drops a song file and requests S-Mode separation.
     *  MainWindow wires this to show StemCountDialog then start SModeService workflow. */
    std::function<void(const juce::File&)> onRequestSeparation;

private:
    EventBus& bus_;
    SModeService* smodeService_ { nullptr };

    // Load Song button — triggers S-Mode separation flow (also accepts drag-drop)
    juce::TextButton loadSongBtn_ { "LOAD SONG" };
    bool dragHighlight_ { false };  // true while audio file is dragged over window

    // Header
    juce::Label titleLabel_ { {}, "S-MODE" };
    juce::Label trackNameLabel_ { {}, "" };
    juce::Label bpmLabel_ { {}, "" };
    juce::ToggleButton dawSyncBtn_ { "DAW" };  // Sync tempo to DAW host
    EventBus::HandlerId hostTempoSub_ { 0 };
    juce::TextButton transportBtn_ { juce::String::charToString(0x25B6) };  // ▶
    juce::ToggleButton loopBtn_ { juce::String::charToString(0x1F501) };   // 🔁 loop

    // Stem waveform components (one per stem, dynamically created)
    std::vector<std::unique_ptr<StemWaveformComponent>> stemWaveforms_;

    // Segmentation controls
    juce::TextButton autoBeatBtn_ { "Auto: Beat" };
    juce::TextButton autoBarBtn_ { "Auto: Bar" };
    juce::TextButton auto2BarBtn_ { "Auto: 2-Bar" };
    juce::TextButton autoTransientBtn_ { "Auto: Transient" };
    juce::ToggleButton snapToggle_ { "Snap" };
    juce::ToggleButton freeformToggle_ { "Freeform" };
    juce::ToggleButton fadeToggle_ { "Fade" };
    juce::TextButton quickAssignBtn_ { "Quick Assign" };
    juce::TextButton clearAllBtn_ { "Clear All" };
    juce::TextButton exportBtn_ { "Export WAV" };
    juce::TextButton undoBtn_ { "Undo" };
    juce::TextButton redoBtn_ { "Redo" };
    juce::ToggleButton tooltipToggle_ { "Tips" };

    // MIDI controls
    juce::TextButton monoPolyBtn_ { "POLY" };
    juce::TextButton trigModeBtn_ { "1SH" };

    // Link all stems toggle
    juce::TextButton linkAllBtn_ { "LINK ALL" };
    bool linkAllActive_ { true };   // Default: all stems linked

    // BPM / tempo controls
    juce::Label bpmTitleLabel_ { {}, "BPM:" };
    juce::TextEditor bpmInput_;
    juce::TextButton bpmRoundBtn_ { "R" };
    juce::TextButton halfTempoBtn_ { "Half" };
    juce::TextButton doubleTempoBtn_ { "Dbl" };
    juce::TextButton gridLockBtn_ { "LOCK" };
    juce::TextButton tapTempoBtn_ { "TAP" };

    // Grid subdivision selector — combo box
    juce::ComboBox   divisionCombo_;
    int currentGridDivision_ { 4 };            // default = quarter notes
    void setGridDivision(int division);

    bool gridLocked_ { false };
    double detectedBpm_ { 0.0 };
    double lockedBpm_ { 0.0 };  // BPM when grid was locked (for transpose ratio)

    // Undo/Redo UI
    juce::Label undoStatusLabel_ { {}, "" };   // Brief flash label next to Undo/Redo buttons
    void updateUndoUi();
    void flashUndoStatus(const juce::String& text);
    juce::var captureUiExtraState() const;
    void      restoreUiExtraState(const juce::var& v);

    // Tap tempo state
    std::vector<double> tapTimes_;    // Timestamps in milliseconds
    static constexpr int kMaxTaps = 8;
    static constexpr double kTapTimeoutMs = 2000.0;  // Reset after 2s gap

    // Controller area labels
    juce::Label controllerAreaLabel_ { {}, "CONTROLLER / TRANSPORT / MIDI" };

    // MIDI Learn overlay
    SModeMidiLearnOverlay midiLearnOverlay_;

    // Sequencer lanes
    std::vector<std::unique_ptr<SequencerLaneComponent>> sequencerLanes_;

    // AppState binding: persist sequencer lanes (shuffled patterns, active stem
    // dots, bars, bpm). Uses SequencerLaneComponent::captureState/restoreState
    // which already exist for the internal undo stack.
    std::unique_ptr<samplar::ScopedStateBinding> stateBinding_;
    juce::TextButton addSeqBtn_ { "+ SEQ" };
    juce::TextButton smartFillBtn_ { "Smart Fill" };
    juce::TextButton playAllSeqBtn_ { "Play All" };
    juce::TextButton playChainBtn_ { "Chain" };
    juce::TextButton stopAllSeqBtn_ { "Stop" };
    samplar::WaveDragArea dragSeqWavArea_;
    void playAllLanesSimultaneous();
    void playLanesChained();
    void stopAllSequencerPlayback();
    void dragSequencerAsWav();

    // Event subscriptions
    EventBus::HandlerId segmentTriggeredSub_ { 0 };
    EventBus::HandlerId segmentStoppedSub_ { 0 };

    // Stem color palette
    static juce::Colour getStemColour(int index);

    // Auto-segmentation helpers
    void autoSegmentByDivision(double beatsPerSegment);
    void autoSegmentByTransient();
    void updateSnapState();
    void syncViewRange(int sourceStemIdx, double startTime, double endTime);
    void applyBpm(double bpm);
    void runBpmDetectionOnDrumStem();

    // Timer callback (transport playhead animation)
    void timerCallback() override;

    // Transport
    void startTransport(double startTimeSeconds);
    void stopTransport();
    void setPlayheadFromMouse(int x);
    void gridQuickAssignStem(int stemIndex, double beatsPerSegment);

    void addSequencerLane();
    void removeSequencerLane(int laneIndex);
    void playSequencerLane(int laneIndex);
    void smartFill();  // Analyze segments and auto-fill 3 lanes (Drums/Bass/Melodic)

    bool transportPlaying_ { false };
    double playheadTimeSec_ { 0.0 };

    // Computer keyboard MIDI — track held keys for note-off on release
    std::set<int> heldKeyboardNotes_;

    // Sequencer playback cancellation
    std::shared_ptr<std::atomic<bool>> seqPlayingFlag_;
    int activeSeqLane_ { -1 };

    // Alt+click loop state
    bool looping_ { false };
    int loopStemIdx_ { -1 };
    int loopSegIdx_ { -1 };
    void playLoopedSegment(int stemIdx, int segIdx);
    void stopLoop();

    // Layout areas (cached for paint)
    juce::Rectangle<int> controllerArea_;
    juce::Rectangle<int> playheadStripArea_;   // Thin strip above waveforms

    EventBus::HandlerId transportStoppedSub_ { 0 };

    // Tracks which stems have been "removed" (hidden) by the user
    std::set<int> removedStems_;
    int focusedStem_ { -1 };

    // Tooltip window — required for JUCE tooltips to display
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    // Empty-state overlay — visible when no audio is loaded
    LoadAudioOverlay loadAudioOverlay_;

    // Shared helper: opens the file chooser and fires onRequestSeparation
    void openLoadAudioChooser();

};

} // namespace samplar
