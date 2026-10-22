#include "SModePage.h"
#include "../../utils/Tracing.h"
#include "../../utils/RecentSongs.h"
#include "../../utils/SamplarSettings.h"
#include "../../app/ServiceLocator.h"
#include "../../app/ServiceRegistry.h"
#include "../../services/MidiMappingService.h"
#include "../../services/FullSongService.h"
#include "../../services/LicenseSystemService.h"
#include "../licensing/LicenseActivationDialog.h"
// SAMPLAR: removed - MentorService deleted
#include "../../services/smode/SpectralFluxDetector.h"
#include "../../services/analysis/HybridTempoDetector.h"
#include "../../services/analysis/KeyDetector.h"
#include "../../audio/engine/SegmentFx.h"
#include "../../audio/MicroFade.h"
#include "../../utils/DataPaths.h"

namespace samplar {

// Returns true if segment has meaningful audio content (RMS above threshold)
static bool segmentHasAudio(const StemData& stem,
                            const Segment& seg,
                            float thresholdDb = -40.0f)
{
    const auto& buf = stem.audioBuffer;
    int numCh = buf.getNumChannels();
    if (numCh == 0 || seg.getLengthSamples() <= 0) return false;

    int64_t start = juce::jlimit((int64_t)0, (int64_t)buf.getNumSamples(), seg.startSample);
    int64_t end   = juce::jlimit(start, (int64_t)buf.getNumSamples(), seg.endSample);
    int len = static_cast<int>(end - start);
    if (len <= 0) return false;

    double sumSq = 0.0;
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* data = buf.getReadPointer(ch) + start;
        for (int i = 0; i < len; ++i)
            sumSq += static_cast<double>(data[i]) * data[i];
    }
    float rms = static_cast<float>(std::sqrt(sumSq / (len * numCh)));
    float rmsDb = (rms > 0.0f) ? 20.0f * std::log10(rms) : -100.0f;
    return rmsDb > thresholdDb;
}

namespace
{
    // Drag-out WAVs must outlive the drag: most DAWs reference the dropped file
    // in place. Write them to a persistent folder with never-reused names.
    // (Old behaviour: fixed names in %TEMP% - collided across songs, got purged
    // by Windows, and FileOutputStream APPENDED to the previous file.)
    juce::File makeUniqueExportFile(const juce::String& baseName)
    {
        auto dir = samplar::DataPaths::getUserDataDirectory().getChildFile("exports");
        if (!dir.isDirectory())
            dir.createDirectory();
        auto f = dir.getNonexistentChildFile(juce::File::createLegalFileName(baseName), ".wav", false);
        juce::Logger::writeToLog("[SModePage] export file: " + f.getFullPathName());
        return f;
    }

    // Runner handed to child components so their heavy jobs go through the
    // processor-owned SModeService pool (drained on shutdown) instead of
    // detached std::threads.
    std::function<bool(std::function<void()>)> makeBackgroundRunner(samplar::SModeService* svc)
    {
        return [svc](std::function<void()> job) { return svc != nullptr && svc->runBackground(std::move(job)); };
    }
}

namespace
{
    // Responsive layout constants (see plan_layout.md section 4).
    constexpr int   kPagePad      = 8,   kBarPadX     = 8;
    constexpr int   kBarHTall     = 64,  kBarHNormal  = 52,  kBarHShort = 44;
    constexpr int   kPageHTall    = 900, kPageHNormal = 600;
    constexpr int   kStripH       = 14,  kRowH        = 24,  kGap = 4, kStemGap = 4;
    constexpr int   kStemLaneMinH = 64;
    constexpr float kStemShare    = 0.60f;   // stems keep >= 60% of stems+sequencer
    constexpr int   kSeqLaneGap   = 3,   kSeqLaneMinH = 64,  kSeqLaneMaxH = 112, kSeqScrollBarW = 8;
    constexpr int   kBpmLabelW    = 36,  kRoundW      = 28,  kDawW = 64;

    struct TopBarSpec { int logo, play, loop, undo, grid, bpm, key, half, dbl, lock, tap, more,
                        loadMin, loadMax, eject, g, G; };
    constexpr TopBarSpec kFull    { 110, 52, 60, 64, 88, 68, 66, 50, 44, 60, 50,  0, 110, 160, 32, 6, 14 };
    constexpr TopBarSpec kMedium  {  84, 46, 52, 56, 76, 64, 58, 40, 36, 52, 44, 28,  96, 160, 30, 6, 14 };
    constexpr TopBarSpec kCompact {  64, 40, 44, 48, 64, 60, 50,  0,  0,  0, 40, 28,  80, 120, 28, 4, 10 };

    int topBarMinWidth(const TopBarSpec& s, bool full, bool withLogo, int sysW)
    {
        const int a = (withLogo ? s.logo + s.G : 0) + s.play + s.g + s.loop;
        const int b = s.undo + s.g + s.undo;
        int c = s.grid + s.g + s.bpm + s.g + s.key;
        if (full)       c += kBpmLabelW + 4 + kRoundW + s.g + kDawW;
        if (s.half > 0) c += s.g + s.half + s.g + s.dbl + s.g + s.lock;
        c += s.g + s.tap;
        if (s.more > 0) c += s.g + s.more;
        const int d = s.loadMin + s.g + s.eject;
        return a + s.G + b + s.G + c + s.G + d + s.G + sysW;
    }

    const char* topBarModeName(int m) { return m == 0 ? "FULL" : (m == 1 ? "MEDIUM" : "COMPACT"); }

    // ---- auto-chop helpers (run on the SModeService worker pool) ----------
    using Range = std::pair<int64_t, int64_t>;

    double rangeRmsDb(const juce::AudioBuffer<float>& b, Range r)
    {
        const int64_t s0 = juce::jlimit((int64_t) 0, (int64_t) b.getNumSamples(), r.first);
        const int64_t s1 = juce::jlimit(s0, (int64_t) b.getNumSamples(), r.second);
        const int len = (int) (s1 - s0);
        if (len <= 0 || b.getNumChannels() <= 0) return -120.0;
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer(ch) + s0;
            for (int i = 0; i < len; ++i) sum += (double) d[i] * d[i];
        }
        const double rms = std::sqrt(sum / ((double) len * b.getNumChannels()));
        return rms > 0.0 ? 20.0 * std::log10(rms) : -120.0;
    }

    /** Keep at most `cap` non-silent ranges, spread over the whole song: the
     *  candidates (time order) are split into `cap` consecutive groups and the
     *  loudest range of each group is kept ("first N" would only cover the
     *  intro). */
    std::vector<Range> pickChops(const juce::AudioBuffer<float>& b, const std::vector<Range>& cands, int cap)
    {
        std::vector<Range> live;
        std::vector<double> db;
        for (const auto& r : cands)
        {
            const double v = rangeRmsDb(b, r);
            if (v > -40.0) { live.push_back(r); db.push_back(v); }
        }
        if ((int) live.size() <= cap) return live;
        std::vector<Range> out;
        const int n = (int) live.size();
        for (int gI = 0; gI < cap; ++gI)
        {
            const int a = (int) ((int64_t) gI * n / cap);
            const int z = (int) ((int64_t) (gI + 1) * n / cap);
            int best = a;
            for (int i = a; i < z; ++i) if (db[(size_t) i] > db[(size_t) best]) best = i;
            out.push_back(live[(size_t) best]);
        }
        return out;
    }

    std::vector<Range> gridCandidates(int64_t total, int64_t offset, int64_t len)
    {
        std::vector<Range> c;
        if (len <= 0) return c;
        int64_t pos = juce::jlimit((int64_t) 0, total, offset);
        // Grid aligned to the downbeat: the intro before beat 1 is left unchopped.
        while (pos + len <= total) { c.push_back({ pos, pos + len }); pos += len; }
        if (total - pos > len / 4) c.push_back({ pos, total });
        return c;
    }

    std::vector<Range> transientCandidates(const juce::AudioBuffer<float>& b, double sr, double bpm, bool phrases)
    {
        const int n = b.getNumSamples();
        if (n <= 0 || b.getNumChannels() <= 0) return {};
        std::vector<float> monoStore;
        const float* mono = b.getReadPointer(0);
        if (b.getNumChannels() > 1)
        {
            monoStore.resize((size_t) n);
            const float* L = b.getReadPointer(0);
            const float* Rr = b.getReadPointer(1);
            for (int i = 0; i < n; ++i) monoStore[(size_t) i] = 0.5f * (L[i] + Rr[i]);
            mono = monoStore.data();
        }
        samplar::SpectralFluxDetector::Settings st;
        const double beatSec = bpm > 0.0 ? 60.0 / bpm : 0.5;
        if (phrases) st.minSpacingSec = beatSec;          // vocals: phrase / word onsets, not syllables
        samplar::SpectralFluxDetector det(st);
        auto onsets = det.detect(mono, n, sr);
        const int64_t maxLen = (int64_t) (4.0 * beatSec * sr);   // a chop never runs past one bar
        const int64_t minLen = (int64_t) (0.05 * sr);
        std::vector<Range> c;
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const int64_t s0 = onsets[i];
            int64_t s1 = i + 1 < onsets.size() ? onsets[i + 1] : (int64_t) n;
            s1 = juce::jmin(s1, s0 + maxLen);
            if (s1 - s0 >= minLen) c.push_back({ s0, s1 });
        }
        return c;
    }

    juce::String bankRangeName(int stem)
    {
        const int first = samplar::MidiMappingService::getStemBankFirstNote(stem);
        if (first < 0) return {};
        return samplar::MidiMappingService::noteNumberToName(first) + "-"
             + samplar::MidiMappingService::noteNumberToName(first + samplar::MidiMappingService::kBankSize - 1);
    }
}

SModePage::SModePage(EventBus& bus)
    : SModePage(bus, samplar::ServiceLocator::capture())
{
}

SModePage::SModePage(EventBus& bus, const ServiceLocator::Context& ctx)
    : bus_(bus), ctx_(ctx), midiLearnOverlay_(bus, ctx.getRegistry())
{
    SAMPLAR_TRACE_SCOPE("page-ctor", "SModePage");
    juce::Logger::writeToLog("🎵 [SModePage] === constructor ===");

    // This instance's services, from the Context the editor captured right
    // after claiming the locator (review A2). Every later lookup uses these.
    registry_ = ctx_.getRegistry();
    appState_ = ctx_.getAppState();
    juce::Logger::writeToLog(juce::String("[SModePage] services captured: registry=")
                             + (registry_ != nullptr ? "ok" : "null") + " appState=" + (appState_ != nullptr ? "ok" : "null"));

    // Persistence (review N-2): sequencer lanes, per-stem pitch and the per-stem
    // MIDI mod state are saved ONLY in the processor's "smode" session node
    // (<play>, SModeSession.h), which works with the editor closed. The old
    // page bindings "smodePage", "smodeStemPitch" and "smodeStemMod" are gone:
    // they late-drained stale values over <play> on every editor open. Old
    // projects that only have the legacy nodes are migrated (clamped) by the
    // processor restore. The knobs mirror the service (timerCallback).
    juce::Logger::writeToLog("[SModePage] persistence: session node only (no page bindings)");


    // ── Top-bar additions (Phase 1 visual build-out) ────────────────────
    // Logo label
    addAndMakeVisible(logoLabel_);
    // SamplarLogo paints itself (gradient fill + cyan glow); no text/font setters.

    // Default spacebar action: top transport toggle. Re-bound when the user
    // clicks a per-lane / play-all sequencer button (see those onClick handlers).
    spaceKeyAction_ = [this]()
    {
        if (!smodeService_) return;
        stopLoop();
        if (transportPlaying_) stopTransport();
        else startTransport(playheadTimeSec_);
    };

    // Play/Stop toggle button — BigPlayButton paints triangle (stopped) or square (playing)
    addAndMakeVisible(playBtn_);
    playBtn_.onClick = [this]()
    {
        if (!smodeService_) return;
        if (transportPlaying_) stopTransport();
        else startTransport(playheadTimeSec_);

        // Lock spacebar back to top transport.
        spaceKeyAction_ = [this]()
        {
            if (!smodeService_) return;
            stopLoop();
            if (transportPlaying_) stopTransport();
            else startTransport(playheadTimeSec_);
        };
        juce::Logger::writeToLog("[SModePage] spaceKeyAction_ -> transport (top play button)");
    };

    // (Stem status indicators removed — MainWindow already has these ball buttons at top.)

    // (presetCombo_ removed — divisionCombo_ is the real measure/grid selector.)
    // ── End top-bar additions ────────────────────────────────────────────

    // Header
    addAndMakeVisible(titleLabel_);
    titleLabel_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(trackNameLabel_);
    trackNameLabel_.setFont(juce::FontOptions(12.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(bpmLabel_);
    bpmLabel_.setFont(juce::FontOptions(12.0f));
    bpmLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(transportBtn_);
    transportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
    transportBtn_.onClick = [this]()
    {
        if (!smodeService_) return;

        if (transportPlaying_)
            stopTransport();
        else
            startTransport(playheadTimeSec_);
    };

    // Loop toggle (affects transport + segment triggers)
    addAndMakeVisible(loopBtn_);
    loopBtn_.setTooltip("Loop: transport wraps at end, segments repeat when triggered");
    loopBtn_.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    loopBtn_.onClick = [this]()
    {
        if (smodeService_) smodeService_->getModel().pushUndoState("Loop toggle");
        bool loopOn = loopBtn_.getToggleState();
        juce::Logger::writeToLog("[SModePage] Loop toggled: " + juce::String(loopOn ? "ON" : "OFF"));

        // Set transport looping
        if (auto* midiSvc = midiService())
            midiSvc->setSModeTransportLooping(loopOn);

        // Set segment effects loop on ALL segments
        if (smodeService_)
        {
            auto& model = smodeService_->getModel();
            for (int s = 0; s < model.getStemCount(); ++s)
            {
                if (auto* stem = model.getStem(s))
                {
                    for (auto& seg : stem->segments)
                        seg.effects.loop = loopOn;
                }
            }
        }
    };

    // DAW tempo sync toggle (next to BPM label)
    addAndMakeVisible(dawSyncBtn_);
    dawSyncBtn_.setTooltip("Sync tempo to DAW host BPM (VST3 only)");
    dawSyncBtn_.setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
    dawSyncBtn_.onClick = [this]()
    {
        const bool synced = dawSyncBtn_.getToggleState();
        juce::Logger::writeToLog("[SModePage] DAW tempo sync clicked: " + juce::String(synced ? "ON" : "OFF"));
        setDawSync(synced);
    };

    // Segmentation controls
    for (auto* btn : { &autoBeatBtn_, &autoBarBtn_, &auto2BarBtn_, &autoTransientBtn_,
                        &quickAssignBtn_, &undoBtn_, &redoBtn_ })
    {
        addAndMakeVisible(btn);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    }
    // Auto: Bar / 2-Bar / Transient live in autoBeatBtn_'s right-click menu;
    // these three were never laid out (0x0 but "visible") -> hide them.
    autoBarBtn_.setVisible(false);
    auto2BarBtn_.setVisible(false);
    autoTransientBtn_.setVisible(false);
    addAndMakeVisible(&clearAllBtn_);
    clearAllBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffaa1f1f));

    // Undo status flash label (hidden unless showing a brief confirmation)
    addChildComponent(undoStatusLabel_);
    undoStatusLabel_.setFont(juce::FontOptions(11.0f));
    undoStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff00d4b8));
    undoStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    undoStatusLabel_.setInterceptsMouseClicks(false, false);
    clearAllBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffdd3333));
    quickAssignBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3d4a));  // Deep teal
    quickAssignBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00e5cc)); // Cyan text

    undoBtn_.onClick = [this]()
    {
        if (!smodeService_) return;
        auto& model = smodeService_->getModel();
        juce::String label = model.peekUndoLabel();
        if (model.undo())
        {
            refreshAfterUndoRedo();

            flashUndoStatus("Undid: " + (label.isEmpty() ? juce::String("change") : label));
            bus_.publish("smode.undo.applied", juce::var(label));
        }
    };
    redoBtn_.onClick = [this]()
    {
        if (!smodeService_) return;
        auto& model = smodeService_->getModel();
        juce::String label = model.peekRedoLabel();
        if (model.redo())
        {
            refreshAfterUndoRedo();

            flashUndoStatus("Redid: " + (label.isEmpty() ? juce::String("change") : label));
            bus_.publish("smode.redo.applied", juce::var(label));
        }
    };

    // Tooltip toggle — on by default
    addAndMakeVisible(tooltipToggle_);
    tooltipToggle_.setToggleState(true, juce::dontSendNotification);
    tooltipToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::grey);
    tooltipToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    tooltipToggle_.setTooltip("Show/hide hover tooltips on waveforms");
    tooltipToggle_.onClick = [this]()
    {
        bool enabled = tooltipToggle_.getToggleState();
        for (auto& wf : stemWaveforms_)
            wf->setTooltipsEnabled(enabled);
        juce::Logger::writeToLog("[SModePage] Tooltips " + juce::String(enabled ? "ON" : "OFF"));
    };

    addAndMakeVisible(snapToggle_);
    addAndMakeVisible(freeformToggle_);
    addAndMakeVisible(fadeToggle_);
    freeformToggle_.setButtonText("Free");
    snapToggle_.setToggleState(true, juce::dontSendNotification);

    // Anti-click micro-fade toggle (2 ms fade-in/out on voice start/end) — default ON.
    // Drives both playback and export so the exported WAV matches what the user heard.
    fadeToggle_.setTooltip("Anti-click micro-fade on sample start/end (playback and export)");
    fadeToggle_.setToggleState(microFadeEnabledFlag().load(std::memory_order_relaxed),
                               juce::dontSendNotification);
    fadeToggle_.onClick = [this]()
    {
        const bool on = fadeToggle_.getToggleState();
        microFadeEnabledFlag().store(on, std::memory_order_relaxed);
        juce::Logger::writeToLog(juce::String("[SModePage] Micro-fade ")
                                 + (on ? "ON" : "OFF"));
    };

    snapToggle_.onClick = [this]()
    {
        if (snapToggle_.getToggleState())
            freeformToggle_.setToggleState(false, juce::dontSendNotification);
        updateSnapState();
    };
    freeformToggle_.onClick = [this]()
    {
        if (freeformToggle_.getToggleState())
            snapToggle_.setToggleState(false, juce::dontSendNotification);
        updateSnapState();
    };

    // Auto-chop - single combo button, right-click to switch mode. Default
    // "Smart": transients on Drums + Vocals, bars on Bass + Melody, at most
    // kMaxChopsPerStem chops per stem, grid aligned to the detected downbeat.
    autoBeatBtn_.onClick = [this]() { runAutoChop(autoMode_, true, "button"); };
    autoBeatBtn_.onStateChange = [this]()
    {
        if (autoBeatBtn_.isDown() && juce::ModifierKeys::currentModifiers.isPopupMenu() && ! autoChopBusy_)
        {
            juce::PopupMenu menu;
            for (int m = 0; m <= (int) AutoMode::Transient; ++m)
                menu.addItem(m + 1, autoModeName((AutoMode) m), true, (int) autoMode_ == m);
            juce::Component::SafePointer<SModePage> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&autoBeatBtn_).withDeletionCheck(*this),
                [safe](int result)
            {
                if (safe == nullptr || result <= 0) return;
                safe->autoMode_ = (AutoMode) (result - 1);
                safe->updateAutoButton();
                juce::Logger::writeToLog("[SModePage] auto-chop mode -> " + autoModeName(safe->autoMode_));
            });
        }
    };
    updateAutoButton();
    // Hide the other 3 auto buttons — merged into autoBeatBtn_
    autoBarBtn_.setVisible(false);
    auto2BarBtn_.setVisible(false);
    autoTransientBtn_.setVisible(false);

    quickAssignBtn_.onClick = [this]()
    {
        if (!smodeService_) return;
        auto& model = smodeService_->getModel();
        int stemCount = model.getStemCount();
        if (stemCount == 0) return;

        if (quickAssignBusy_) return;

        // Build popup menu: All Stems + each individual stem with its note bank
        juce::PopupMenu menu;
        menu.addItem(1, "All Stems (each to its own bank)");
        menu.addSeparator();
        for (int s = 0; s < stemCount; ++s)
        {
            auto* stem = model.getStem(s);
            juce::String name = stem ? stem->name : ("Stem " + juce::String(s));
            const auto bank = bankRangeName(s);
            menu.addItem(s + 10, name + (bank.isNotEmpty() ? "  (" + bank + ")" : juce::String()), bank.isNotEmpty());
        }

        juce::Component::SafePointer<SModePage> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&quickAssignBtn_).withDeletionCheck(*this),
            [safe](int result)
        {
            if (safe == nullptr || result == 0) return;  // page gone / user cancelled
            auto* self = safe.getComponent();
            if (! self->smodeService_) return;
            auto& model = self->smodeService_->getModel();

            // Determine which stems to assign
            std::set<int> stemsToAssign;
            if (result == 1)
            {
                // All stems (respecting exclusion)
                for (int st = 0; st < model.getStemCount(); ++st)
                {
                    if (st < static_cast<int>(self->stemWaveforms_.size()) &&
                        self->stemWaveforms_[static_cast<size_t>(st)]->excludedFromQuickAssign_)
                        continue;
                    stemsToAssign.insert(st);
                }
            }
            else
            {
                stemsToAssign.insert(result - 10);
            }
            self->runQuickAssign(stemsToAssign);
        });
    };

    clearAllBtn_.onClick = [this]()
    {
        if (!smodeService_) return;
        auto& model = smodeService_->getModel();
        model.pushUndoState("Clear all segments");
        for (int i = 0; i < model.getStemCount(); ++i)
            model.clearSegments(i);
        lastAutoChopValid_ = false;
        juce::Logger::writeToLog("🎵 [SModePage] Clear All: all segments removed");
        for (auto& wf : stemWaveforms_) wf->repaint();
        refreshInspector();
    };

    // MIDI controls — single combo button: cycles POLY/1SH → POLY/GATE → MONO/1SH → MONO/GATE
    addAndMakeVisible(monoPolyBtn_);
    monoPolyBtn_.setButtonText("POLY 1SH");
    monoPolyBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    monoPolyBtn_.setTooltip("Click to cycle: POLY/MONO + ONE-SHOT/GATE");
    monoPolyBtn_.onClick = [this]()
    {
        // Cycle POLY/1SH -> POLY/GATE -> MONO/1SH -> MONO/GATE. B25: the mode
        // lives in this instance's engine (no function-local static shared
        // by every plugin instance); the label always mirrors the engine.
        auto* midiSvc = midiService();
        if (midiSvc == nullptr) return;
        const int cur  = (midiSvc->isMonophonic() ? 2 : 0) + (midiSvc->isSampleGateMode() ? 1 : 0);
        const int next = (cur + 1) % 4;
        midiSvc->setMonophonic(next >= 2);
        midiSvc->setSampleGateMode(next == 1 || next == 3);
        updateMonoPolyLabel();
        juce::Logger::writeToLog("[SModePage] MIDI mode: " + monoPolyBtn_.getButtonText());
    };

    // trigModeBtn_ hidden — merged into monoPolyBtn_
    trigModeBtn_.setVisible(false);

    // LINK ALL button — syncs zoom/scroll/segments across all stems
    addAndMakeVisible(linkAllBtn_);
    linkAllBtn_.setClickingTogglesState(true);
    updateLinkAllButton();   // B16: OFF by default - every stem is its own sampler
    linkAllBtn_.onClick = [this]()
    {
        if (smodeService_) smodeService_->getModel().pushUndoState("Link all stems");
        linkAllActive_ = linkAllBtn_.getToggleState();
        updateLinkAllButton();

        // When Link All is ON, set all stems as linked in the model.
        // When OFF, clear all stems so individual L buttons start from a clean slate.
        if (smodeService_)
        {
            auto& model = smodeService_->getModel();
            for (int i = 0; i < model.getStemCount(); ++i)
                model.setLinked(i, linkAllActive_);
        }

        // Sync each lane's L button visual to reflect the new state.
        for (auto& w : stemWaveforms_)
            if (w) w->refreshLinkButton();

        juce::Logger::writeToLog("[SModePage] Link All: " + juce::String(linkAllActive_ ? "ON" : "OFF"));
    };

    // BPM controls
    addAndMakeVisible(bpmTitleLabel_);
    bpmTitleLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    bpmTitleLabel_.setColour(juce::Label::textColourId, juce::Colour::fromString("ff00FFFF"));

    addAndMakeVisible(bpmInput_);
    bpmInput_.setMultiLine(false);
    bpmInput_.setReturnKeyStartsNewLine(false);
    bpmInput_.setInputRestrictions(7, "0123456789.");
    bpmInput_.setText("...", juce::dontSendNotification);
    bpmInput_.setJustification(juce::Justification::centred);
    bpmInput_.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromString("ff21262D"));
    bpmInput_.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    bpmInput_.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromString("ff00FFFF"));
    bpmInput_.onReturnKey = [this]()
    {
        double val = bpmInput_.getText().getDoubleValue();
        if (val >= 30.0 && val <= 300.0) applyBpm(val);
    };
    bpmInput_.onFocusLost = [this]()
    {
        // B24: focus changes alone must not push "BPM change" entries.
        double val = bpmInput_.getText().getDoubleValue();
        if (val >= 30.0 && val <= 300.0 && std::abs(val - detectedBpm_) > 0.005) applyBpm(val);
    };

    addAndMakeVisible(keyLabel_);
    keyLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    keyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffd54f));
    keyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour::fromString("ff21262D"));
    keyLabel_.setColour(juce::Label::outlineColourId, juce::Colour::fromString("ff30363D"));
    keyLabel_.setJustificationType(juce::Justification::centred);
    keyLabel_.setMinimumHorizontalScale(0.6f);
    keyLabel_.setInterceptsMouseClicks(true, false);   // tooltip
    setKeyText("--", "Musical key (detected on the Melody + Bass stems after a song loads)");

    addAndMakeVisible(bpmRoundBtn_);
    bpmRoundBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff30363D"));
    bpmRoundBtn_.setTooltip("Round BPM to nearest integer");
    bpmRoundBtn_.onClick = [this]()
    {
        double rounded = std::round(detectedBpm_);
        if (rounded >= 30.0) applyBpm(rounded);
    };

    addAndMakeVisible(halfTempoBtn_);
    halfTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
    halfTempoBtn_.setTooltip("Halve the tempo (grid becomes wider)");
    halfTempoBtn_.onClick = [this]()
    {
        double half = detectedBpm_ / 2.0;
        if (half >= 30.0) applyBpm(half);
    };

    addAndMakeVisible(doubleTempoBtn_);
    doubleTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
    doubleTempoBtn_.setTooltip("Double the tempo (grid becomes tighter)");
    doubleTempoBtn_.onClick = [this]()
    {
        double dbl = detectedBpm_ * 2.0;
        if (dbl <= 300.0) applyBpm(dbl);
    };

    // Grid lock button — locks grid positions, BPM changes become transpose
    addAndMakeVisible(gridLockBtn_);
    gridLockBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
    gridLockBtn_.setTooltip("Lock grid: BPM changes transpose instead of re-segmenting");
    gridLockBtn_.onClick = [this]()
    {
        if (smodeService_) smodeService_->getModel().pushUndoState("Grid lock");
        gridLocked_ = !gridLocked_;
        if (gridLocked_)
            lockedBpm_ = detectedBpm_;
        else
            lockSemis_ = 0;   // unlocking drops the transpose
        applyLockTranspose();
        updateGridLockButton();
        juce::Logger::writeToLog("[SModePage] Grid lock: " + juce::String(gridLocked_ ? "LOCKED" : "UNLOCKED")
                                 + " at " + juce::String(lockedBpm_, 2) + " BPM");
    };

    // Tap tempo button
    addAndMakeVisible(tapTempoBtn_);
    tapTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
    tapTempoBtn_.setTooltip("Tap repeatedly to set tempo");
    tapTempoBtn_.onClick = [this]()
    {
        double now = juce::Time::getMillisecondCounterHiRes();

        // Reset if too long since last tap
        if (!tapTimes_.empty() && (now - tapTimes_.back()) > kTapTimeoutMs)
            tapTimes_.clear();

        tapTimes_.push_back(now);

        // Keep max taps
        if (static_cast<int>(tapTimes_.size()) > kMaxTaps)
            tapTimes_.erase(tapTimes_.begin());

        // Need at least 2 taps to calculate BPM
        if (tapTimes_.size() >= 2)
        {
            double totalInterval = tapTimes_.back() - tapTimes_.front();
            double avgInterval = totalInterval / static_cast<double>(tapTimes_.size() - 1);
            double tappedBpm = 60000.0 / avgInterval;

            // Clamp to reasonable range
            tappedBpm = juce::jlimit(30.0, 300.0, tappedBpm);

            applyBpm(tappedBpm);
            juce::Logger::writeToLog("[SModePage] Tap tempo: " + juce::String(tappedBpm, 1)
                                     + " BPM (" + juce::String(static_cast<int>(tapTimes_.size())) + " taps)");
        }

        // Flash the button color briefly
        tapTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a9d4a));
        // Local first: MSVC mis-resolves `this` inside a nested lambda's init-capture.
        juce::Component::SafePointer<SModePage> safeTap(this);
        juce::Timer::callAfterDelay(100, [safeTap]()
        {
            if (safeTap == nullptr) return;
            safeTap->tapTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
        });
    };

    // Grid division selector (combo box) — drives both grid rendering and snap.
    // Item IDs ARE the division values so setSelectedId(N) == division N.
    addAndMakeVisible(divisionCombo_);
    divisionCombo_.setTooltip("Grid subdivision — controls grid line density and snap");
    divisionCombo_.addItem("1/1",  1);
    divisionCombo_.addItem("1/2",  2);
    divisionCombo_.addItem("1/4",  4);
    divisionCombo_.addItem("1/8",  8);
    divisionCombo_.addItem("1/16", 16);
    divisionCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromString("ff21262D"));
    divisionCombo_.setColour(juce::ComboBox::outlineColourId,    juce::Colour::fromString("ff30363D"));
    divisionCombo_.onChange = [this]()
    {
        int picked = divisionCombo_.getSelectedId();
        if (picked > 0) setGridDivision(picked);
    };
    // Show the current division right away (setGridDivision() returns early
    // when the value is unchanged, so the combo used to stay blank).
    divisionCombo_.setSelectedId(currentGridDivision_, juce::dontSendNotification);

    // Controller area label (placeholder)
    addAndMakeVisible(controllerAreaLabel_);
    controllerAreaLabel_.setFont(juce::FontOptions(14.0f));
    controllerAreaLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    controllerAreaLabel_.setJustificationType(juce::Justification::centred);

    // MIDI Learn overlay (hidden by default, covers entire page)
    addChildComponent(midiLearnOverlay_);
    midiLearnOverlay_.onNoteAssigned = [this](int stemIdx, int segIdx, int noteNumber)
    {
        if (!smodeService_) return;
        auto& model = smodeService_->getModel();
        model.pushUndoState("MIDI learn");

        if (model.getSegment(stemIdx, segIdx) == nullptr) return;   // chop gone meanwhile
        model.assignMidiNote(stemIdx, segIdx, noteNumber);
        juce::Logger::writeToLog("[SModePage] MIDI Learn assigned: stem=" + juce::String(stemIdx)
                                 + " seg=" + juce::String(segIdx) + " note=" + juce::String(noteNumber)
                                 + " (" + samplar::MidiMappingService::noteNumberToName(noteNumber) + ")");
        // Explicitly linked stems layer: the chop at the SAME TIME on each
        // linked stem gets the note too (B16: link by time, never by index).
        for (const auto& [ls, lseg] : linkedSegmentsAt(stemIdx, segIdx))
        {
            model.assignMidiNote(ls, lseg, noteNumber);
            juce::Logger::writeToLog("[SModePage]   linked layer: stem=" + juce::String(ls) + " seg=" + juce::String(lseg));
        }
        for (auto& wf : stemWaveforms_) wf->repaint();
        refreshInspector();
    };

    // Sequencer: + SEQ and Smart Fill buttons
    addAndMakeVisible(addSeqBtn_);
    addSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d6a));
    addSeqBtn_.onClick = [this]() {
        if (smodeService_) smodeService_->getModel().pushUndoState("Add lane");
        addSequencerLane();
    };

    addAndMakeVisible(smartFillBtn_);
    smartFillBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d5a3d));
    smartFillBtn_.onClick = [this]() {
        if (smodeService_) smodeService_->getModel().pushUndoState("Smart fill");
        smartFill();
    };

    // Play All (simultaneous) and Chain (sequential) buttons
    addAndMakeVisible(playAllSeqBtn_);
    playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
    playAllSeqBtn_.setTooltip("Play/Stop all sequencer lanes simultaneously");
    playAllSeqBtn_.onClick = [this]()
    {
        // Toggle: if playing, stop. If stopped, play.
        if (seqPlayingFlag_ && seqPlayingFlag_->load())
        {
            stopAllSequencerPlayback();
            playAllSeqBtn_.setButtonText("Play All");
            playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
        }
        else
        {
            playAllLanesSimultaneous();
            playAllSeqBtn_.setButtonText("Stop");
            playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
        }

        // Lock spacebar to Play-All sequencer toggle.
        spaceKeyAction_ = [this]()
        {
            if (seqPlayingFlag_ && seqPlayingFlag_->load())
            {
                stopAllSequencerPlayback();
                playAllSeqBtn_.setButtonText("Play All");
                playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
            }
            else
            {
                playAllLanesSimultaneous();
                playAllSeqBtn_.setButtonText("Stop");
                playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
            }
        };
        juce::Logger::writeToLog("[SModePage] spaceKeyAction_ -> sequencer-all (Play All button)");
    };

    addAndMakeVisible(dragSeqWavArea_);
    dragSeqWavArea_.setHasContent(true);
    dragSeqWavArea_.onCanRender = [this]()
    {
        if (auto* m = midiService()) pushLanesToEngine(*m);   // message thread: latest lanes
        return !sequencerLanes_.empty();
    };
    dragSeqWavArea_.onRenderPattern = [target = renderTarget_](double& outSampleRate,
        std::function<void(float)> progressCb) -> std::unique_ptr<juce::AudioBuffer<float>>
    {
        // WaveDragArea worker thread: never touches the page. Every lane is
        // rendered by the realtime engine itself (pitch, speed, reverse,
        // fades, velocity identical to playback - B26).
        auto* svc = target->svc.load();
        const uint32_t mask = target->allMask.load();
        if (svc == nullptr || mask == 0) return nullptr;
        return svc->renderSequencerOffline(mask, outSampleRate, std::move(progressCb));
    };

    // 30 Hz UI timer: transport playhead, sequencer playhead / playing slot,
    // lane -> engine sync. The sequencer clock itself is on the audio thread.
    startTimerHz(30);

    // Tooltips for every button
    transportBtn_.setTooltip("Play / Stop transport (Spacebar)");
    autoBarBtn_.setTooltip("Auto-segment by bar (4 beats per segment)");
    auto2BarBtn_.setTooltip("Auto-segment by 2 bars (8 beats per segment)");
    autoTransientBtn_.setTooltip("Auto-segment by transient detection");
    snapToggle_.setTooltip("Snap segment edges to grid lines");
    freeformToggle_.setTooltip("Draw segments freely without snapping");
    quickAssignBtn_.setTooltip("Map each stem's chops to its own 16-note bank: Vocals " + bankRangeName(0)
                               + ", Bass " + bankRangeName(1) + ", Drums " + bankRangeName(2)
                               + ", Melody " + bankRangeName(3) + " (C3 = middle C = MIDI 60). "
                               "Computer keyboard: A..; plays, Z / X shifts the octave.");
    clearAllBtn_.setTooltip("Remove all segments from all stems");
    monoPolyBtn_.setTooltip("Click to cycle POLY/MONO + 1SH (play to the end) / GATE (stop on note-off). "
                            "A chop's own trigger setting in the inspector overrides 1SH/GATE.");
    updateMonoPolyLabel();   // B25: mirror this instance's engine (it outlives the editor)
    trigModeBtn_.setTooltip("Toggle One-Shot (play full sample) / Gate (play while held)");
    linkAllBtn_.setTooltip("LINK ALL (layer): stems share zoom/scroll and chop edits, and a chop plays "
                           "together with the chops at the same time on the other stems. Off = every stem is its own sampler.");
    bpmInput_.setTooltip("Type BPM value and press Enter");
    gridLockBtn_.setTooltip("Lock grid: BPM changes transpose every stem (12 x log2 of the tempo ratio) "
                            "instead of moving the grid");
    tapTempoBtn_.setTooltip("Tap repeatedly to set tempo by rhythm");
    bpmRoundBtn_.setTooltip("Round BPM to nearest whole number");
    halfTempoBtn_.setTooltip("Halve the tempo (wider grid)");
    doubleTempoBtn_.setTooltip("Double the tempo (tighter grid)");
    addSeqBtn_.setTooltip("Add a new sequencer lane");
    smartFillBtn_.setTooltip("Analyze segment audio and auto-fill 3 lanes (Drums / Bass / Melodic)");
    undoBtn_.setTooltip("Undo last action (Ctrl+Z)");
    redoBtn_.setTooltip("Redo last undone action (Ctrl+Y)");

    // Tempo overflow menu ("...") - holds the tempo controls the bar hides at MEDIUM / COMPACT width.
    addAndMakeVisible(tempoMoreBtn_);
    tempoMoreBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
    tempoMoreBtn_.setTooltip("More tempo options");
    tempoMoreBtn_.onClick = [this]() { showTempoOverflowMenu(); };

    // Sequencer collapse toggle - stems get the space back when collapsed.
    addAndMakeVisible(seqCollapseBtn_);
    seqCollapseBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    seqCollapseBtn_.setTooltip("Show / hide the sequencer lanes");
    seqCollapseBtn_.onClick = [this]()
    {
        seqCollapsed_ = ! seqCollapsed_;
        updateSeqCollapseButton();
        juce::Logger::writeToLog("[SModePage] sequencer " + juce::String(seqCollapsed_ ? "collapsed" : "expanded"));
        resized();
    };
    updateSeqCollapseButton();

    // Sequencer lanes live in a scrollable viewport (stems never scroll).
    addChildComponent(seqViewport_);
    seqViewport_.setViewedComponent(&seqContent_, false);
    seqViewport_.setScrollBarsShown(true, false);
    seqViewport_.setScrollBarThickness(kSeqScrollBarW);

    // Legacy header widgets replaced by logo / big play button.
    titleLabel_.setVisible(false);
    trackNameLabel_.setVisible(false);
    bpmLabel_.setVisible(false);
    transportBtn_.setVisible(false);
    controllerAreaLabel_.setVisible(false);

    // LOAD SONG button — primary entry point for S-Mode separation
    // (also accepts drag-drop via FileDragAndDropTarget)
    addAndMakeVisible(loadSongBtn_);
    loadSongBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(116, 82, 255));  // purple/blue
    loadSongBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadSongBtn_.setTooltip("Drop a song here or click to choose a file for S-Mode separation");
    loadSongBtn_.onClick = [this]() { openLoadAudioChooser(); };

    // Eject button — sits right next to LOAD SONG, removes current audio.
    addAndMakeVisible(ejectBtn_);
    ejectBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(180, 60, 60));
    ejectBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    ejectBtn_.setTooltip("Unload current audio and return to empty state");
    ejectBtn_.setVisible(false);  // hidden until stems load
    ejectBtn_.onClick = [this]() { unloadCurrentAudio(); };

    // Per-chop inspector strip (lower tool area). Disabled until a chop is selected.
    addAndMakeVisible(chopInspector_);
    chopInspector_.onEditBegin = [this]()
    {
        if (smodeService_ != nullptr && findInspectorSegment() >= 0)
            smodeService_->getModel().pushUndoState("Chop settings");
    };
    chopInspector_.onValuesChanged = [this](const ChopInspectorStrip::Values& v)
    {
        if (smodeService_ == nullptr) return;
        const int seg = findInspectorSegment();
        if (seg < 0) { refreshInspector(); return; }
        auto& model = smodeService_->getModel();
        auto fx = model.getSegmentEffects(inspStem_, seg);
        fx.pitchSemitones = v.pitchSemis;
        fx.volume  = ChopInspectorStrip::gainDbToLinear(v.gainDb);
        fx.pan     = v.pan;
        fx.reverse = v.reverse;
        samplar::engine::setTriggerMode(fx, v.trigger);
        samplar::engine::setAttackMs(fx, v.attackMs);
        samplar::engine::setReleaseMs(fx, v.releaseMs);
        model.setSegmentEffects(inspStem_, seg, fx);
        if (auto* m = midiService()) m->syncSnapshotNow();   // next trigger already uses it
        if (inspStem_ >= 0 && inspStem_ < (int) stemWaveforms_.size())
            stemWaveforms_[(size_t) inspStem_]->repaint();
        if (auto* app = appState_) app->markDirty();
    };
    chopInspector_.onMidiMenu = [this](const juce::String& param, juce::Component& anchor)
    {
        showMidiLearnMenu(inspectorCcTarget(param), "Selected chop " + param, anchor);
    };
    if (auto* midiSvc = midiService())
    {
        // The inspector controls are CC targets while this page exists; a bound
        // CC with the editor closed is kept (inert) and saved with the project.
        juce::Component::SafePointer<SModePage> safeInsp(this);
        for (const auto& param : ChopInspectorStrip::midiParams())
            midiSvc->registerCcTarget(inspectorCcTarget(param), [safeInsp, param](float v)
            {
                if (safeInsp != nullptr) safeInsp->applyInspectorCc(param, v);
            });
        juce::Logger::writeToLog("[SModePage] chop inspector CC targets registered: "
                                 + juce::String(ChopInspectorStrip::midiParams().size()));
    }

    // Empty-state overlay — sits on top of all other children
    addAndMakeVisible(loadAudioOverlay_);
    loadAudioOverlay_.onLoad = [this]() { openLoadAudioChooser(); };
    loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
    loadAudioOverlay_.onLoadRecent = [this](const juce::File& f) {
        if (!f.existsAsFile()) return;  // overlay handles missing entries itself
        juce::Logger::writeToLog("[SModePage] recent click: " + f.getFullPathName());
        requestLoadFile(f, "recent");
    };
    loadAudioOverlay_.onLoadFile = [this](const juce::File& f) {
        if (!f.existsAsFile()) {
            juce::Logger::writeToLog("[SModePage] drag-drop: file does not exist: "
                                     + f.getFullPathName());
            return;
        }
        juce::Logger::writeToLog("[SModePage] drag-drop load: " + f.getFullPathName());
        requestLoadFile(f, "overlay drop");
    };
    loadAudioOverlay_.setVisible(true);   // start visible (no audio loaded yet)
    updateAudioDependentControls();

    // Register as KeyListener so spacebar/shortcuts work regardless of which child has focus
    addKeyListener(this);
    setWantsKeyboardFocus(true);

    // Debug-only layout self-test (inert unless SAMPLAR_UI_SWEEP_DIR is set).
    {
        SModeUiSelfTest::Callbacks cb;
        juce::Component::SafePointer<SModePage> safe(this);
        cb.loadSynthetic = [safe]() { if (safe != nullptr) safe->debugLoadSyntheticStems(); };
        cb.loadLong      = [safe](double seconds) { if (safe != nullptr) safe->debugLoadSyntheticStems(seconds); };
        cb.notice        = [safe](const juce::String& kind)
        {
            if (safe == nullptr) return;
            if (kind == "missing")
                safe->showMissingAudioNotice("Audio missing: the song 'My Song (final mix 3) - extended version.wav' and its saved "
                                             "stems were not found. Your chops are kept; load the song again to relink them.");
            else if (kind == "unavailable")
                safe->showSeparationUnavailableNotice();
            else if (kind == "signin")
                safe->showSignInRequiredNotice();
            else
                safe->hideNotice();
        };
        cb.unload        = [safe]() { if (safe != nullptr && safe->audioLoaded_) safe->unloadCurrentAudio(); };
        cb.setSeqLanes   = [safe](int n)
        {
            if (safe == nullptr) return;
            while ((int) safe->sequencerLanes_.size() < n)
            {
                const auto before = safe->sequencerLanes_.size();
                safe->addSequencerLane();
                if (safe->sequencerLanes_.size() == before) break;
            }
            while ((int) safe->sequencerLanes_.size() > juce::jmax(0, n))
                safe->removeSequencerLane((int) safe->sequencerLanes_.size() - 1);
        };
        cb.setCollapsed  = [safe](bool c)
        {
            // Same effect as clicking seqCollapseBtn_ (a click would be ignored
            // while the root is disabled behind the licence panel).
            if (safe == nullptr || safe->seqCollapsed_ == c) return;
            safe->seqCollapsed_ = c;
            safe->updateSeqCollapseButton();
            safe->resized();
        };
        cb.scrollSeq     = [safe](bool bottom)
        {
            if (safe == nullptr) return;
            safe->seqViewport_.setViewPosition(0, bottom ? safe->seqContent_.getHeight() : 0);
        };
        cb.playheadTick  = [safe](double t)
        {
            if (safe == nullptr) return;
            safe->playheadTimeSec_ = t;
            safe->updatePlayheadDisplay();
        };
        cb.midi          = [safe](const juce::String& step) -> juce::String
        {
            return safe != nullptr ? safe->debugMidiSelfTest(step) : juce::String("FAIL page gone");
        };
        cb.extraChecks   = [safe]() -> juce::StringArray
        {
            juce::StringArray fails;
            if (safe == nullptr) return fails;
            auto& p = *safe;
            const auto pageBounds = p.getLocalBounds();
            // Key label: visible in the top bar at every size, text fits, no overlap.
            if (! p.keyLabel_.isVisible() || p.keyLabel_.getWidth() < 40 || p.keyLabel_.getHeight() < 16)
                fails.add("key label hidden or too small: " + p.keyLabel_.getBounds().toString());
            else
            {
                if (! pageBounds.contains(p.keyLabel_.getBounds()))
                    fails.add("key label outside the page: " + p.keyLabel_.getBounds().toString());
                for (auto* c : p.getChildren())
                    if (c != &p.keyLabel_ && c->isVisible() && c != &p.loadAudioOverlay_ && c != &p.midiLearnOverlay_
                        && c->getBounds().intersects(p.keyLabel_.getBounds()))
                        fails.add("key label overlaps " + juce::String(typeid(*c).name()) + " " + c->getBounds().toString());
                const auto text = p.keyLabel_.getText();
                const float need = juce::GlyphArrangement::getStringWidth(p.keyLabel_.getFont(), text);
                const float room = (float) (p.keyLabel_.getWidth() - 4) / p.keyLabel_.getMinimumHorizontalScale();
                if (text.isEmpty() || need > room)
                    fails.add("key label text '" + text + "' does not fit (" + juce::String(need, 1) + " > " + juce::String(room, 1) + ")");
            }
            if (p.audioLoaded_)
            {
                // B16: stems are independent by default.
                if (p.linkAllActive_ || p.linkAllBtn_.getToggleState())
                    fails.add("LINK ALL is ON after a fresh load (B16 default must be OFF)");
                if (p.smodeService_ != nullptr)
                    for (int i = 0; i < p.smodeService_->getModel().getStemCount(); ++i)
                        if (p.smodeService_->getModel().isLinked(i))
                            fails.add("stem " + juce::String(i) + " linked after a fresh load");
                // Chop inspector: in the lower tool area, enabled for the selected chop.
                if (! p.chopInspector_.isVisible() || p.chopInspector_.getHeight() < 18
                    || ! pageBounds.contains(p.chopInspector_.getBounds()))
                    fails.add("chop inspector not laid out: " + p.chopInspector_.getBounds().toString());
                else if (! p.chopInspector_.hasTarget())
                    fails.add("chop inspector has no target although a chop is selected");
            }
            else if (p.chopInspector_.isVisible())
                fails.add("chop inspector visible in the empty state");

            // In-page notice: under the top bar, inside the page, text readable,
            // overlaps nothing (the overlay / lanes are laid out below it).
            if (p.noticeBanner_.isVisible())
            {
                const auto nb = p.noticeBanner_.getBounds();
                if (! pageBounds.contains(nb) || nb.getHeight() < 30)
                    fails.add("notice banner badly placed: " + nb.toString());
                for (auto* c : p.getChildren())
                    if (c != &p.noticeBanner_ && c->isVisible() && c != &p.midiLearnOverlay_
                        && c->getWidth() > 0 && c->getHeight() > 0 && c->getBounds().intersects(nb)
                        && c->getY() >= nb.getY())
                        fails.add("notice banner overlaps " + juce::String(typeid(*c).name()) + " " + c->getBounds().toString());
                const auto& lbl = p.noticeBanner_.getLabel();
                const auto font = lbl.getFont();
                const int lines = juce::jmax(1, (int) ((float) lbl.getHeight() / font.getHeight()));
                const float need = juce::GlyphArrangement::getStringWidth(font, lbl.getText());
                const float room = (float) (lbl.getWidth() - 4) * (float) lines / lbl.getMinimumHorizontalScale();
                if (lbl.getWidth() < 120 || need > room)
                    fails.add("notice text does not fit (" + juce::String(need, 0) + " > " + juce::String(room, 0)
                              + ", label " + lbl.getBounds().toString() + ")");
                if (p.noticeBanner_.getKind() == SModeNoticeBanner::Kind::MissingAudio
                    && ! p.noticeBanner_.getActionButton().isVisible())
                    fails.add("missing-audio notice has no Locate button");
                if (p.noticeBanner_.getKind() == SModeNoticeBanner::Kind::SignInRequired)
                {
                    auto& btn = p.noticeBanner_.getActionButton();
                    if (! btn.isVisible() || btn.getButtonText() != "Sign in again" || btn.getWidth() < 100)
                        fails.add("sign-in notice has no usable 'Sign in again' button " + btn.getBounds().toString());
                }
            }
            return fails;
        };
        uiSelfTest_ = SModeUiSelfTest::createIfEnabled(*this, std::move(cb));
    }

    // In-page notice strip (replaces modal "audio missing" alerts).
    addChildComponent(noticeBanner_);
    noticeBanner_.onAction = [this](SModeNoticeBanner::Kind k)
    {
        juce::Logger::writeToLog("[SModePage] notice action clicked (kind=" + juce::String((int) k) + ")");
        if (k == SModeNoticeBanner::Kind::MissingAudio)
            locateMissingSong();
        else if (k == SModeNoticeBanner::Kind::SignInRequired)
            openSignInPanel();
    };
    noticeBanner_.onDismiss = [this]()
    {
        if (noticeBanner_.getKind() == SModeNoticeBanner::Kind::SignInRequired)
            signInNoticeDismissed_ = true;   // back on the next sign-in requirement
        hideNotice();
    };

    // Licence kept but a fresh sign-in is required (token_* reply): non-blocking
    // strip. license.status may arrive on any thread -> hop to the message thread.
    {
        juce::Component::SafePointer<SModePage> safe(this);
        licenseStatusSub_ = bus_.subscribe("license.status", [safe](const juce::var&)
        {
            juce::MessageManager::callAsync([safe]() { if (safe != nullptr) safe->updateSignInNotice(); });
        });
    }

    // Controls mirror this instance's engine state (project restore / editor
    // reopen): POLY/MONO + 1SH/GATE label, DAW tempo sync.
    updateMonoPolyLabel();
    if (auto* midiSvc = midiService(); midiSvc != nullptr && midiSvc->isDawTempoSync())
    {
        juce::Logger::writeToLog("[SModePage] ctor: DAW tempo sync was ON in this instance -> re-subscribing");
        setDawSync(true);
    }
    if (auto* svc = serviceFromContext();
        svc != nullptr && svc->getSessionStatus() == SModeService::SessionStatus::MissingAudio)
        showMissingAudioNotice(svc->getSessionMessage());
    updateSignInNotice();
}

void SModePage::debugLoadSyntheticStems(double drumSeconds)
{
    juce::Logger::writeToLog("[SModePage] === debugLoadSyntheticStems (UI self-test) drumSeconds="
                             + juce::String(drumSeconds, 1) + " ===");
    auto* reg = registry_;
    auto* svc = reg != nullptr ? dynamic_cast<samplar::SModeService*>(reg->get("smode")) : nullptr;
    if (svc == nullptr)
    {
        juce::Logger::writeToLog("[SModePage] self-test: smode service not found");
        return;
    }
    constexpr double kSr = 44100.0;
    constexpr int kLen = (int) (kSr * 20.0);
    // --bpm-unload: one LONG mono drum stem (clicks at 120 BPM) so the BPM job
    // (BeatThis) runs for tens of seconds; the other stems stay 20 s.
    const int drumLen = drumSeconds > 20.0 ? (int) (kSr * juce::jmin(drumSeconds, 1200.0)) : kLen;
    static const char* names[] = { "Vocals", "Bass", "Drums", "Melody" };
    static const double freqs[] = { 440.0, 55.0, 110.0, 330.0 };
    auto& m = svc->getModel();
    m.clear();   // a new "song": fresh undo history + stem link state
    m.setStemCount(4);
    for (int i = 0; i < 4; ++i)
    {
        auto* stem = m.getStem(i);
        if (stem == nullptr) continue;
        stem->name = names[i];
        if (i == 2 && drumLen != kLen)
        {
            juce::AudioBuffer<float> drums(1, drumLen);
            drums.clear();
            auto* d = drums.getWritePointer(0);
            const int beat = (int) (kSr * 0.5);
            for (int pos = 0; pos < drumLen; pos += beat)
                for (int n = 0; n < 2000 && pos + n < drumLen; ++n)
                    d[pos + n] = (float) (0.9 * std::exp(-n / 300.0) * std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * n / kSr));
            m.setStemAudio(i, std::move(drums));
            continue;
        }
        stem->audioBuffer.setSize(2, kLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = stem->audioBuffer.getWritePointer(ch);
            for (int n = 0; n < kLen; ++n)
            {
                const double t = n / kSr;
                const double env = 0.5 + 0.45 * std::sin(2.0 * juce::MathConstants<double>::pi * (0.5 + i * 0.3) * t);
                d[n] = (float) (0.8 * env * std::sin(2.0 * juce::MathConstants<double>::pi * freqs[i] * t));
            }
        }
    }
    m.setSampleRate(kSr);
    svc->setSourceFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ui_selftest.wav"));
    onStemsReady(*svc);

    // Deterministic chops for the sweep: 4 x 2 s per stem on its note bank,
    // and chop 1 of the Vocals stem selected (shows the inspector).
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
        {
            const int idx = m.addSegment(i, (int64_t) (k * 2.0 * kSr), (int64_t) ((k + 1) * 2.0 * kSr));
            if (idx >= 0) m.assignMidiNote(i, idx, samplar::MidiMappingService::getStemBankFirstNote(i) + k);
        }
    if (auto* st = m.getStem(0); st != nullptr && st->segments.size() > 1)
        st->segments[1].selected = true;
    selectChopForInspector(0, 1);
    for (auto& wf : stemWaveforms_) wf->repaint();
}

SModePage::~SModePage()
{
    // NOTE: Do NOT disconnect S-Mode model from MIDI here.
    // SModeService (and its model) is owned by the processor and survives
    // editor close/reopen. MIDI triggering must continue without the UI.

    juce::Logger::writeToLog("[SModePage] === destructor START ===");
    stopTimer();

    // MIDI CC learn: the inspector targets point into this page. Bindings stay
    // (saved with the project, inert until an editor registers them again).
    if (auto* midiSvc = midiService())
    {
        for (const auto& param : ChopInspectorStrip::midiParams())
            midiSvc->unregisterCcTarget(inspectorCcTarget(param));
        if (pendingLearnTarget_.isNotEmpty() && midiSvc->getCcLearnTarget() == pendingLearnTarget_)
            midiSvc->endCcLearn();   // never leave a learn armed with no UI to show it
        juce::Logger::writeToLog("[SModePage]   CC targets unregistered");
    }

    // B2/B36: the sequencer clock runs on the audio thread now, but closing the
    // editor still stops it (unchanged UX; the lanes live in this page).
    if (seqPlayingFlag_ && seqPlayingFlag_->load())
    {
        juce::Logger::writeToLog("[SModePage]   stopping sequencer playback before teardown");
        stopAllSequencerPlayback();
    }
    if (seqPlayingFlag_) seqPlayingFlag_->store(false);

    // B4: DAW tempo sync subscription lives on the processor bus, which
    // outlives this page.
    if (hostTempoSub_ != 0)
    {
        // Same bus it was subscribed on (U-H2), not whatever the locator holds now.
        bus_.unsubscribe("host.tempo", hostTempoSub_);
        hostTempoSub_ = 0;
        juce::Logger::writeToLog("[SModePage]   unsubscribed host.tempo");
    }

    // B1: the model is processor-lifetime; detach every callback that points
    // back into this page (undo UI + extra-state hooks), otherwise
    // SModeService::shutdown() -> model.clear() calls into a dead page.
    if (smodeService_ != nullptr)
    {
        auto& model = smodeService_->getModel();
        model.onUndoStackChanged = nullptr;
        model.setExtraStateHooks(nullptr, nullptr);
        juce::Logger::writeToLog("[SModePage]   detached model callbacks");
    }

    if (segmentTriggeredSub_ != 0)
        bus_.unsubscribe("smode.segment.triggered", segmentTriggeredSub_);
    if (segmentStoppedSub_ != 0)
        bus_.unsubscribe("smode.segment.stopped", segmentStoppedSub_);
    if (transportStoppedSub_ != 0)
        bus_.unsubscribe("smode.transport.stopped", transportStoppedSub_);
    if (downbeatDetectedSub_ != 0)
        bus_.unsubscribe("smode.downbeat.detected", downbeatDetectedSub_);
    if (licenseStatusSub_ != 0)
        bus_.unsubscribe("license.status", licenseStatusSub_);
    juce::Logger::writeToLog("[SModePage] === destructor END ===");
}

void SModePage::openLoadAudioChooser()
{
    juce::Logger::writeToLog("[SModePage] openLoadAudioChooser - opening file chooser");
    auto chooser = std::make_shared<juce::FileChooser>(
        "Choose audio file for S-Mode separation",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg");
    juce::Component::SafePointer<SModePage> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
        [safe, chooser](const juce::FileChooser& fc) {
            auto results = fc.getResults();
            if (safe == nullptr || results.isEmpty()) return;
            juce::File chosen = results[0];
            juce::Logger::writeToLog("[SModePage] File chosen: " + chosen.getFullPathName());
            safe->requestLoadFile(chosen, "file chooser");
        });
}

void SModePage::requestLoadFile(const juce::File& f, const juce::String& source)
{
    juce::Logger::writeToLog("[SModePage] requestLoadFile (" + source + "): " + f.getFullPathName()
                             + " sessionLoaded=" + juce::String(audioLoaded_ ? 1 : 0));
    if (! f.existsAsFile()) return;
    if (! audioLoaded_)
    {
        startLoad(f);
        return;
    }
    // B28: a loaded session (chops, MIDI map, lanes) is never discarded silently.
    juce::Component::SafePointer<SModePage> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
        "Replace current session?",
        "Load \"" + f.getFileName() + "\"?\n\nThe current song's chops, MIDI mapping and sequencer lanes "
        "will be replaced (Ctrl+Z cannot bring them back).",
        "Replace", "Cancel", this,
        juce::ModalCallbackFunction::create([safe, f](int ok)
        {
            if (safe == nullptr) return;
            juce::Logger::writeToLog("[SModePage] replace-session prompt: " + juce::String(ok != 0 ? "REPLACE" : "cancel"));
            if (ok != 0) safe->startLoad(f);
        }));
}

void SModePage::startLoad(const juce::File& f)
{
    // N-21: no up-front refusal when the ONNX Runtime is missing - a song whose
    // stems are cached loads without it (FullSongService serves cache hits
    // first). A real separation then fails with runtimeUnavailable and the
    // "reinstall" error, which MainWindow turns into the in-page notice.
    juce::Logger::writeToLog("[SModePage] startLoad: " + f.getFileName() + " (separation runtime "
                             + (samplar::FullSongService::isSeparationRuntimeAvailable() ? "available" : "MISSING - cache only")
                             + ")");
    samplar::RecentSongs::push(f);
    loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
    if (onRequestSeparation)
        onRequestSeparation(f);
    else
        juce::Logger::writeToLog("[SModePage] WARNING: onRequestSeparation not set");
}

void SModePage::beginSeparationVisuals(int stemCount)
{
    juce::Logger::writeToLog("[SModePage] === beginSeparationVisuals stemCount=" + juce::String(stemCount) + " ===");
    hideNotice();   // a new song / relink is loading

    // B28: a new song starts clean - sequencer lanes pointed at the old song's
    // chops, the inspector at an old chop, the key / BPM at the old song.
    if (seqPlayingFlag_ && seqPlayingFlag_->load()) stopAllSequencerPlayback();
    for (auto& l : sequencerLanes_)
        if (l) seqContent_.removeChildComponent(l.get());
    sequencerLanes_.clear();
    selectChopForInspector(-1, -1);
    lastAutoChopValid_ = false;
    setKeyText("--", "Musical key: detecting after the stems are ready");
    bpmDotsAnim_.reset();
    bpmInput_.setText("...", juce::dontSendNotification);
    ++bpmEditCounter_;

    stemWaveforms_.clear();

    // Reset the placeholder model so it carries no stale data
    placeholderModel_.clear();
    placeholderModel_.setStemCount(stemCount);

    // Helper: returns (bgA, bgB) gradient pair for a stem index.
    // Order matches SModeSeparationService 4-stem output:
    // 0=Vocals(blue), 1=Bass(red), 2=Drums(teal), 3+=Melody/other(green)
    auto stemTheme = [](int idx) -> std::pair<juce::Colour, juce::Colour>
    {
        switch (idx)
        {
            case 0:  return { juce::Colour::fromRGB(8,  21, 34), juce::Colour::fromRGB(15, 35, 54) };  // vocals: blue
            case 1:  return { juce::Colour::fromRGB(30,  9, 12), juce::Colour::fromRGB(54, 18, 22) };  // bass: red
            case 2:  return { juce::Colour::fromRGB(5,  22, 28), juce::Colour::fromRGB(9,  42, 50) };  // drums: teal
            default: return { juce::Colour::fromRGB(5,  24, 16), juce::Colour::fromRGB(11, 46, 28) };  // melody/other: green
        }
    };

    auto kindFor = [](int idx) {
        // Match SModeSeparationService 4-stem order: Vocals/Bass/Drums/Melody.
        switch (idx) {
            case 0: return StemWaveformComponent::StemKind::Vocals;
            case 1: return StemWaveformComponent::StemKind::Bass;
            case 2: return StemWaveformComponent::StemKind::Drums;
            default: return StemWaveformComponent::StemKind::Melody;
        }
    };

    for (int i = 0; i < stemCount; ++i)
    {
        auto colour = getStemColour(i);
        auto wf = std::make_unique<StemWaveformComponent>(placeholderModel_, i, colour);
        auto [bgA, bgB] = stemTheme(i);
        wf->setBackgroundGradient(bgA, bgB);
        wf->setStemKind(kindFor(i));
        wf->onStemHovered = [this](int stemIdx)
        {
            for (int j = 0; j < static_cast<int>(stemWaveforms_.size()); ++j)
            {
                const float a = (stemIdx < 0 || j == stemIdx) ? 1.0f : 0.3f;
                if (stemWaveforms_[static_cast<size_t>(j)])
                    stemWaveforms_[static_cast<size_t>(j)]->setAlpha(a);
            }
        };
        wf->setLoadingState(true);
        addAndMakeVisible(*wf);
        stemWaveforms_.push_back(std::move(wf));
    }

    // Hide the empty-state overlay while separation is running
    loadAudioOverlay_.setVisible(false);

    juce::Logger::writeToLog("[SModePage] beginSeparationVisuals: created " + juce::String(stemWaveforms_.size()) + " placeholder lanes");
    resized();
    repaint();
}

void SModePage::onStemsReady(SModeService& service)
{
    juce::Logger::writeToLog("🎵 [SModePage] === onStemsReady ===");

    smodeService_ = &service;
    auto& model = service.getModel();
    int stemCount = model.getStemCount();
    hideNotice();

    // ── Undo/Redo wiring ─────────────────────────────────────────────
    // 1) Register hooks so the model's snapshot can save/restore lane patterns + UI flags.
    model.setExtraStateHooks(
        [this]() { return captureUiExtraState(); },
        [this](const juce::var& v) { restoreUiExtraState(v); });

    // 2) UI updates when the stack changes.
    model.onUndoStackChanged = [this]() { updateUndoUi(); };

    // 3) B11: do NOT clear the history here. A new song clears the model
    //    (SModeService::startWorkflow / reset -> model.clear() drops both
    //    stacks); reopening the editor reconnects to the same session and
    //    keeps its undo history.
    updateUndoUi();

    // Wire S-Mode model to THIS instance's MidiMappingService (review A2).
    if (auto* midiSvc = midiService())
    {
        midiSvc->setSModeModel(&model);
        juce::Logger::writeToLog("[SModePage] Wired SModeSegmentModel to MidiMappingService");
    }

    // Subscribe to segment trigger events for visual animation
    if (segmentTriggeredSub_ != 0)
        bus_.unsubscribe("smode.segment.triggered", segmentTriggeredSub_);

    segmentTriggeredSub_ = bus_.subscribe("smode.segment.triggered", [this](const juce::var& v)
    {
        int stemIdx = -1;
        int segIdx  = -1;
        double duration = 0.0;
        if (auto* obj = v.getDynamicObject())
        {
            stemIdx  = static_cast<int>(obj->getProperty("stemIndex"));
            segIdx   = static_cast<int>(obj->getProperty("segmentIndex"));
            duration = static_cast<double>(obj->getProperty("duration"));
        }

        if (stemIdx >= 0 && stemIdx < static_cast<int>(stemWaveforms_.size()))
        {
            stemWaveforms_[static_cast<size_t>(stemIdx)]->startSegmentAnimation(segIdx, duration);
        }
    });

    // Subscribe to segment stop events (note-off in gate mode) for animation stop
    if (segmentStoppedSub_ != 0)
        bus_.unsubscribe("smode.segment.stopped", segmentStoppedSub_);

    segmentStoppedSub_ = bus_.subscribe("smode.segment.stopped", [this](const juce::var& v)
    {
        int stemIdx = -1, segIdx = -1;
        if (auto* obj = v.getDynamicObject())
        {
            stemIdx = static_cast<int>(obj->getProperty("stemIndex"));
            segIdx  = static_cast<int>(obj->getProperty("segmentIndex"));
        }
        if (stemIdx >= 0 && stemIdx < static_cast<int>(stemWaveforms_.size()))
            stemWaveforms_[static_cast<size_t>(stemIdx)]->stopSegmentAnimation(segIdx);
    });

    trackNameLabel_.setText(service.getSourceFile().getFileName(), juce::dontSendNotification);

    // Show cached BPM immediately if available (editor reconnect), otherwise "detecting..."
    double cachedBpm = model.getDetectedTempo();
    if (cachedBpm > 0.0)
        bpmLabel_.setText("BPM: " + juce::String(cachedBpm, 2), juce::dontSendNotification);
    else
        bpmLabel_.setText("BPM: detecting...", juce::dontSendNotification);

    stemWaveforms_.clear();

    // Helper: returns (bgA, bgB) gradient pair for a stem index.
    // Order matches SModeSeparationService 4-stem output:
    // 0=Vocals(blue), 1=Bass(red), 2=Drums(teal), 3+=Melody/other(green)
    auto stemTheme = [](int idx) -> std::pair<juce::Colour, juce::Colour>
    {
        switch (idx)
        {
            case 0:  return { juce::Colour::fromRGB(8,  21, 34), juce::Colour::fromRGB(15, 35, 54) };  // vocals: blue
            case 1:  return { juce::Colour::fromRGB(30,  9, 12), juce::Colour::fromRGB(54, 18, 22) };  // bass: red
            case 2:  return { juce::Colour::fromRGB(5,  22, 28), juce::Colour::fromRGB(9,  42, 50) };  // drums: teal
            default: return { juce::Colour::fromRGB(5,  24, 16), juce::Colour::fromRGB(11, 46, 28) };  // melody/other: green
        }
    };

    auto kindFor2 = [](int idx) {
        // Match SModeSeparationService 4-stem order: Vocals/Bass/Drums/Melody.
        switch (idx) {
            case 0: return StemWaveformComponent::StemKind::Vocals;
            case 1: return StemWaveformComponent::StemKind::Bass;
            case 2: return StemWaveformComponent::StemKind::Drums;
            default: return StemWaveformComponent::StemKind::Melody;
        }
    };

    for (int i = 0; i < stemCount; ++i)
    {
        auto colour = getStemColour(i);
        auto wf = std::make_unique<StemWaveformComponent>(model, i, colour);
        auto [bgA, bgB] = stemTheme(i);
        wf->setBackgroundGradient(bgA, bgB);
        wf->setStemKind(kindFor2(i));
        wf->setGridDivision(currentGridDivision_);  // Apply currently-selected grid division
        wf->onStemHovered = [this](int stemIdx)
        {
            for (int j = 0; j < static_cast<int>(stemWaveforms_.size()); ++j)
            {
                const float a = (stemIdx < 0 || j == stemIdx) ? 1.0f : 0.3f;
                if (stemWaveforms_[static_cast<size_t>(j)])
                    stemWaveforms_[static_cast<size_t>(j)]->setAlpha(a);
            }
        };

        // L button: delegate mutual-exclusion with LINK ALL to the page.
        wf->onLinkToggled = [this](int stemIdx)
        {
            if (!smodeService_) return;
            auto& mdl = smodeService_->getModel();
            mdl.pushUndoState("Link stem");

            if (linkAllActive_)
            {
                // Transitioning from LINK ALL mode: turn off the master toggle,
                // clear every stem's link state, then set only the clicked one.
                linkAllActive_ = false;
                linkAllBtn_.setToggleState(false, juce::dontSendNotification);
                linkAllBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));

                for (int i = 0; i < mdl.getStemCount(); ++i)
                    mdl.setLinked(i, false);
                mdl.setLinked(stemIdx, true);

                juce::Logger::writeToLog("[SModePage] L button pressed — LINK ALL off, individual linking: stem "
                                         + juce::String(stemIdx));
            }
            else
            {
                // Normal toggle: flip this stem's link state only.
                mdl.setLinked(stemIdx, !mdl.isLinked(stemIdx));
                juce::Logger::writeToLog("[SModePage] L button toggled stem " + juce::String(stemIdx)
                                         + " -> " + (mdl.isLinked(stemIdx) ? "linked" : "unlinked"));
            }

            // Sync visuals on every lane so L buttons reflect the new model state.
            for (auto& w : stemWaveforms_)
                if (w) w->refreshLinkButton();
        };

        wf->onViewRangeChanged = [this, i](double startTime, double endTime) {
            syncViewRange(i, startTime, endTime);
        };
        wf->onSegmentsChanged = [this, i]()
        {
            // If link-all or this stem's L is active, replicate segments to linked stems
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();
            bool thisLinked = linkAllActive_ || model.isLinked(i);
            if (!thisLinked) return;

            auto* srcStem = model.getStem(i);
            if (!srcStem) return;

            for (int j = 0; j < static_cast<int>(stemWaveforms_.size()); ++j)
            {
                if (j == i) continue;
                bool destLinked = linkAllActive_ || model.isLinked(j);
                if (!destLinked) continue;

                // Explicit layer (LINK): copy the chops, notes and per-chop settings.
                model.clearSegments(j);
                for (const auto& seg : srcStem->segments)
                {
                    int newSegIdx = model.addSegment(j, seg.startSample, seg.endSample);
                    if (newSegIdx < 0) continue;
                    if (seg.midiNote >= 0)
                        model.assignMidiNote(j, newSegIdx, seg.midiNote);
                    model.setSegmentEffects(j, newSegIdx, seg.effects);
                }

                stemWaveforms_[static_cast<size_t>(j)]->repaint();
            }
        };
        wf->onSegmentDeleted = [this, i](int segIdx, int64_t delStart, int64_t delEnd)
        {
            // Surgical delete propagation for LINK ALL / linked stems.
            // The generic onSegmentsChanged path uses clearSegments+rebuild, which
            // destroys per-destination state (MIDI assignments, selection, any
            // divergent segments). Here we mirror exactly the one delete on every
            // linked sister stem so ONLY the targeted segment index is removed.
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();
            bool thisLinked = linkAllActive_ || model.isLinked(i);
            if (!thisLinked)
            {
                stemWaveforms_[static_cast<size_t>(i)]->repaint();
                refreshInspector();
                return;
            }

            // B16: mirror the delete on linked stems BY TIME - the chop with
            // the same start/end there - never by index (stems diverge).
            juce::ignoreUnused(segIdx);
            for (int j = 0; j < static_cast<int>(stemWaveforms_.size()); ++j)
            {
                if (j == i) continue;
                bool destLinked = linkAllActive_ || model.isLinked(j);
                if (!destLinked) continue;
                auto* destStem = model.getStem(j);
                if (!destStem) continue;
                for (int k = static_cast<int>(destStem->segments.size()) - 1; k >= 0; --k)
                {
                    const auto& d = destStem->segments[(size_t) k];
                    if (d.startSample == delStart && d.endSample == delEnd)
                    {
                        juce::Logger::writeToLog("[SMode] deleteSegment-linked stem=" + juce::String(j)
                                                 + " seg=" + juce::String(k) + " (same time range)");
                        model.removeSegment(j, k);
                        break;
                    }
                }
                stemWaveforms_[static_cast<size_t>(j)]->repaint();
            }
            refreshInspector();
        };
        wf->onSegmentSelected = [this](int stemIdx, int segIdx) {
            juce::Logger::writeToLog("[SModePage] Segment selected: stem=" + juce::String(stemIdx)
                                     + " seg=" + juce::String(segIdx));
            selectChopForInspector(stemIdx, segIdx);
        };
        wf->onSegmentClicked = [this](int stemIdx, int segIdx) {
            // B16: a click plays THIS stem's chop (directly, not through its
            // MIDI note, which other stems may share). Explicitly linked
            // stems layer the chop at the same time position.
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();

            auto* midiSvc = midiService();
            if (!midiSvc) return;

            // -- Stop any currently playing voices (retrigger, no layering) --
            midiSvc->stopAllVoices();

            // -- Interrupt transport if playing --
            if (transportPlaying_)
            {
                juce::Logger::writeToLog("[SModePage] Click-play: interrupting transport");
                stopTransport();
            }

            const bool isGateMode = midiSvc->isSampleGateMode();
            std::vector<std::pair<int, int>> toPlay { { stemIdx, segIdx } };
            for (const auto& ls : linkedSegmentsAt(stemIdx, segIdx))
                toPlay.push_back(ls);

            double modelSr = model.getSampleRate();
            if (modelSr <= 0.0) modelSr = 44100.0;
            const bool isLooping = loopBtn_.getToggleState();

            for (const auto& [si, sgi] : toPlay)
            {
                auto* stem = model.getStem(si);
                auto* s = model.getSegment(si, sgi);
                if (!stem || !s || s->getLengthSamples() <= 0 || stem->mute) continue;

                // Same engine path as a MIDI note: a window of the shared stem
                // buffer + stem pitch knob + fades. No copy on the UI thread (B26).
                midiSvc->playSegment(si, sgi, isLooping);

                const double durSec = static_cast<double>(s->getLengthSamples()) / modelSr;
                stemWaveforms_[static_cast<size_t>(si)]->startSegmentAnimation(sgi, durSec, isLooping);
            }

            juce::Logger::writeToLog("[SModePage] Click-play: stem=" + juce::String(stemIdx)
                                     + " seg=" + juce::String(segIdx)
                                     + " gate=" + juce::String(isGateMode ? "YES" : "NO"));
        };
        // Alt+click = BPM-aware loop
        wf->onSegmentAltClicked = [this](int stemIdx, int segIdx) {
            if (looping_ && loopStemIdx_ == stemIdx && loopSegIdx_ == segIdx)
            {
                stopLoop();  // Second alt+click cancels loop
            }
            else
            {
                stopLoop();
                looping_ = true;
                loopStemIdx_ = stemIdx;
                loopSegIdx_ = segIdx;
                playLoopedSegment(stemIdx, segIdx);
                juce::Logger::writeToLog("[SModePage] Loop START stem=" + juce::String(stemIdx)
                                         + " seg=" + juce::String(segIdx));
            }
        };

        // Drag segment out as WAV file — copy audio on message thread, write on background
        wf->onSegmentDragOut = [this](int stemIdx, int segIdx) {
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();
            auto* stem = model.getStem(stemIdx);
            auto* seg  = model.getSegment(stemIdx, segIdx);
            if (!stem || !seg || seg->getLengthSamples() <= 0) return;
            auto* midiSvc = midiService();
            if (!midiSvc) return;
            midiSvc->syncSnapshotNow();   // export the chop exactly as it is now

            juce::String stemName = stem->name;
            juce::String noteName = (seg->midiNote >= 0)
                ? StemWaveformComponent::midiNoteToName(seg->midiNote) : juce::String(segIdx);

            // Render through the realtime engine (stem pitch, fades, pitched
            // tail = what the user hears, B26) and write the WAV on the worker.
            auto safeThis = juce::Component::SafePointer<SModePage>(this);
            smodeService_->runBackground([safeThis, midiSvc, stemIdx, segIdx, stemName, noteName]()
            {
                double sr = 44100.0;
                auto segBuf = midiSvc->renderSegmentOffline(stemIdx, segIdx, sr);
                if (segBuf == nullptr || segBuf->getNumSamples() == 0)
                {
                    juce::Logger::writeToLog("[SModePage] Drag-out: render produced no audio (muted / empty chop)");
                    return;
                }
                const int numCh = segBuf->getNumChannels();
                auto tmpFile = makeUniqueExportFile(stemName + "_" + noteName + "_seg" + juce::String(segIdx));

                juce::WavAudioFormat wavFmt;
                if (auto os = std::unique_ptr<juce::FileOutputStream>(tmpFile.createOutputStream()))
                {
                    auto* writer = wavFmt.createWriterFor(os.get(), sr,
                                                          static_cast<unsigned int>(numCh), 24, {}, 0);
                    if (writer)
                    {
                        os.release();
                        writer->writeFromAudioSampleBuffer(*segBuf, 0, segBuf->getNumSamples());
                        delete writer;

                        juce::String path = tmpFile.getFullPathName();
                        juce::MessageManager::callAsync([safeThis, path]()
                        {
                            if (safeThis == nullptr) return;   // page destroyed
                            // B30: an OS drag can only start while the button is
                            // still held; a late start would silently do nothing.
                            if (! juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown())
                            {
                                juce::Logger::writeToLog("[SModePage] Drag-out: mouse released before the WAV was ready - saved to " + path);
                                safeThis->flashUndoStatus("Chop saved to Exports (drag again)");
                                return;
                            }
                            juce::StringArray files;
                            files.add(path);
                            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, true, safeThis.getComponent());
                            juce::Logger::writeToLog("[SModePage] Drag-out WAV ready: " + path);
                        });
                    }
                }
            });
        };

        wf->onMultiSegmentDragOut = [this](int /*triggerStemIdx*/, int /*triggerSegIdx*/)
        {
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();
            auto* midiSvc = midiService();
            if (!midiSvc) return;

            // Collect every selected segment across every stem.
            struct Job { int stemIdx; int segIdx; juce::String fileName; };
            auto jobs = std::make_shared<std::vector<Job>>();
            for (int s = 0; s < model.getStemCount(); ++s)
            {
                auto* stem = model.getStem(s);
                if (!stem) continue;
                for (int i = 0; i < static_cast<int>(stem->segments.size()); ++i)
                {
                    const auto& seg = stem->segments[(size_t) i];
                    if (!seg.selected || seg.getLengthSamples() <= 0) continue;
                    juce::String noteName = (seg.midiNote >= 0)
                        ? StemWaveformComponent::midiNoteToName(seg.midiNote) : juce::String(i);
                    jobs->push_back({ s, i, stem->name + "_" + noteName + "_seg" + juce::String(i) });
                }
            }

            if (jobs->empty())
            {
                juce::Logger::writeToLog("[SModePage] Multi-drag: no selected segments - nothing to drag");
                return;
            }
            juce::Logger::writeToLog("[SModePage] Multi-drag: rendering "
                                     + juce::String((int) jobs->size()) + " segment(s) through the engine");
            midiSvc->syncSnapshotNow();

            auto safeThis = juce::Component::SafePointer<SModePage>(this);
            smodeService_->runBackground([safeThis, midiSvc, jobs]()
            {
                juce::WavAudioFormat wavFmt;
                juce::StringArray paths;

                for (auto& j : *jobs)
                {
                    double sr = 44100.0;
                    auto buf = midiSvc->renderSegmentOffline(j.stemIdx, j.segIdx, sr);
                    if (buf == nullptr || buf->getNumSamples() == 0) continue;
                    auto tmpFile = makeUniqueExportFile(j.fileName);
                    if (auto os = std::unique_ptr<juce::FileOutputStream>(tmpFile.createOutputStream()))
                    {
                        auto* writer = wavFmt.createWriterFor(os.get(), sr,
                                                              static_cast<unsigned int>(buf->getNumChannels()), 24, {}, 0);
                        if (writer)
                        {
                            os.release();
                            writer->writeFromAudioSampleBuffer(*buf, 0, buf->getNumSamples());
                            delete writer;
                            paths.add(tmpFile.getFullPathName());
                        }
                    }
                }

                if (paths.isEmpty())
                {
                    juce::Logger::writeToLog("[SModePage] Multi-drag: no files written");
                    return;
                }

                juce::MessageManager::callAsync([safeThis, paths]()
                {
                    if (safeThis == nullptr) return;   // page destroyed
                    if (! juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown())   // B30
                    {
                        juce::Logger::writeToLog("[SModePage] Multi-drag: mouse released before the WAVs were ready - kept in Exports");
                        safeThis->flashUndoStatus(juce::String(paths.size()) + " chops saved to Exports");
                        return;
                    }
                    juce::DragAndDropContainer::performExternalDragDropOfFiles(paths, true, safeThis.getComponent());
                    juce::Logger::writeToLog("[SModePage] Multi-drag-out: " + juce::String(paths.size()) + " file(s)");
                });
            });
        };

        wf->onGridQuickAssign = [this](int stemIdx, double beatsPerSegment) {
            gridQuickAssignStem(stemIdx, beatsPerSegment);
        };
        wf->onGridDragStarted = [this](int stemIdx) {
            // B19: grid drags are undoable (grid offset is in the UI undo state).
            if (smodeService_) smodeService_->getModel().pushUndoState("Move grid");
            juce::Logger::writeToLog("[SModePage] grid drag started on stem " + juce::String(stemIdx));
        };
        wf->onGridOffsetChanged = [this](int /*stemIdx*/, double gridOffset) {
            // B19: ONE beat grid for the song (model + every lane), linked or not.
            setGlobalGridOffset(gridOffset);
        };
        wf->onMidiAssignRequested = [this](int stemIdx, int segIdx) {
            // Show MIDI learn overlay
            juce::String stemName = "Stem";
            if (smodeService_)
            {
                auto* stem = smodeService_->getModel().getStem(stemIdx);
                if (stem) stemName = stem->name;
            }
            midiLearnOverlay_.show(stemIdx, segIdx, stemName);
        };
        wf->onPitchMidiMenu = [this](int stemIdx)
        {
            if (stemIdx < 0 || stemIdx >= (int) stemWaveforms_.size() || ! stemWaveforms_[(size_t) stemIdx]) return;
            juce::String stemName = "Stem " + juce::String(stemIdx + 1);
            if (smodeService_ != nullptr)
                if (auto* st = smodeService_->getModel().getStem(stemIdx); st != nullptr && st->name.isNotEmpty())
                    stemName = st->name;
            showMidiLearnMenu(stemPitchCcTarget(stemIdx), stemName + " pitch", stemWaveforms_[(size_t) stemIdx]->getPitchKnob());
        };
        if (auto* midiSvc = midiService())
            wf->setPitchSemitones(midiSvc->getStemPitchSemitones(i));   // restored / CC-moved value
        wf->onPitchChanged = [this](int stemIdx, float semi)
        {
            juce::Logger::writeToLog("[SModePage] stem pitch changed stem="
                + juce::String(stemIdx) + " semi=" + juce::String(semi, 2));
            if (auto* midiSvc = midiService())
                midiSvc->setStemPitchSemitones(stemIdx, semi);
            // Mark the project dirty (pitch is saved in the session <play> node).
            if (auto* app = appState_)
                app->markDirty();
        };
        wf->onFocusRequested = [this](int stemIdx) {
            // Toggle focus: if already focused on this stem, unfocus (show all non-removed)
            if (focusedStem_ == stemIdx)
            {
                // Unfocus — show all stems except removed ones
                focusedStem_ = -1;
                applyRemovedStemVisibility();
                juce::Logger::writeToLog("[SModePage] Unfocused - showing all non-removed stems");
            }
            else
            {
                // Focus on this stem — hide others
                focusedStem_ = stemIdx;
                for (int s = 0; s < static_cast<int>(stemWaveforms_.size()); ++s)
                    stemWaveforms_[static_cast<size_t>(s)]->setVisible(s == stemIdx);
                juce::Logger::writeToLog("[SModePage] Focused on stem " + juce::String(stemIdx));
            }
            resized();
            repaint();
        };
        wf->onRemoveRequested = [this](int stemIdx) {
            // Hide the channel and mute it in the model
            if (stemIdx >= 0 && stemIdx < static_cast<int>(stemWaveforms_.size()))
            {
                // B23: removedStems_ is part of the UI undo state, so Ctrl+Z
                // brings the lane back (the model undoes the mute).
                if (smodeService_)
                    smodeService_->getModel().pushUndoState("Remove channel");
                stemWaveforms_[static_cast<size_t>(stemIdx)]->setVisible(false);
                removedStems_.insert(stemIdx);
                if (inspStem_ == stemIdx) selectChopForInspector(-1, -1);
                flashUndoStatus("Channel removed (Ctrl+Z to restore)");

                // Mute the stem so it doesn't play in transport or MIDI
                if (smodeService_)
                    smodeService_->getModel().setMute(stemIdx, true);

                juce::Logger::writeToLog("[SModePage] Removed stem channel " + juce::String(stemIdx) + " (hidden + muted)");
            }
            resized();
            repaint();
        };

        addAndMakeVisible(wf.get());
        stemWaveforms_.push_back(std::move(wf));
    }

    double sampleRate = model.getSampleRate();
    for (int i = 0; i < stemCount; ++i)
    {
        auto* stem = model.getStem(i);
        if (stem && stem->audioBuffer.getNumSamples() > 0)
        {
            double duration = static_cast<double>(stem->audioBuffer.getNumSamples()) / sampleRate;
            stemWaveforms_[static_cast<size_t>(i)]->setViewRange(0.0, duration);
        }
        stemWaveforms_[static_cast<size_t>(i)]->computeSpectralColorsAsync(makeBackgroundRunner(smodeService_),
                                                                           smodeService_->getJobCancelFlag());
    }

    // B16 / B11: LINK ALL mirrors the model (a fresh song has every stem
    // unlinked -> OFF; reopening the editor keeps the user's choice).
    {
        bool allLinked = stemCount > 0;
        for (int i = 0; i < stemCount; ++i)
            allLinked = allLinked && model.isLinked(i);
        linkAllActive_ = allLinked;
        updateLinkAllButton();
    }
    refreshLaneButtons();
    updateMonoPolyLabel();
    for (auto& w : stemWaveforms_)
        if (w) w->setGridOffsetSeconds(model.getGridOffsetSeconds());   // grid survives editor reopen
    applyRemovedStemVisibility();
    juce::Logger::writeToLog("[SModePage] onStemsReady: linkAll=" + juce::String(linkAllActive_ ? "ON" : "OFF")
                             + " gridOffset=" + juce::String(model.getGridOffsetSeconds(), 4) + "s");

    juce::Logger::writeToLog("✅ [SModePage] Built " + juce::String(stemCount) + " stem waveform components");
    resized();
    repaint();

    juce::MessageManager::callAsync([this, safeThis = juce::Component::SafePointer<SModePage>(this)]() {
        if (safeThis == nullptr) return;
        resized();
        for (auto& wf : stemWaveforms_)
            wf->repaint();
        repaint();
    });

    // Subscribe to transport stop events
    if (transportStoppedSub_ != 0)
        bus_.unsubscribe("smode.transport.stopped", transportStoppedSub_);
    transportStoppedSub_ = bus_.subscribe("smode.transport.stopped", [this](const juce::var&) {
        stopTransport();
    });

    // ── Auto downbeat detection result subscription ─────────────────────
    // Reset one-shot guard for this load session; SModeService publishes the
    // result on this topic when background detection completes.
    downbeatDetectionRequested_ = false;
    if (downbeatDetectedSub_ != 0)
        bus_.unsubscribe("smode.downbeat.detected", downbeatDetectedSub_);
    downbeatDetectedSub_ = bus_.subscribe("smode.downbeat.detected", [this](const juce::var& v)
    {
        double offsetSec = 0.0;
        double conf = 0.0;
        if (auto* obj = v.getDynamicObject())
        {
            offsetSec = static_cast<double>(obj->getProperty("offsetSeconds"));
            conf      = static_cast<double>(obj->getProperty("confidence"));
        }
        juce::Logger::writeToLog("🎵 [SModePage] downbeat received: offset="
                                 + juce::String(offsetSec, 4) + "s conf="
                                 + juce::String(conf, 3));

        // Apply to every lane so the grid snaps to beat 1.
        for (auto& wf : stemWaveforms_)
        {
            if (wf) wf->setGridOffsetSeconds(offsetSec);
        }

        // An auto-chop made before the downbeat arrived used offset 0: redo
        // it on the real bar grid, unless the user edited the chops since
        // (impl_analysis follow-up). Not a new undo step.
        if (smodeService_ != nullptr && lastAutoChopValid_ && ! autoChopBusy_
            && lastAutoChopMode_ != AutoMode::Transient
            && segmentLayoutHash(smodeService_->getModel()) == lastAutoChopLayout_)
        {
            juce::Logger::writeToLog("[SModePage] downbeat arrived after auto-chop - re-chopping bars on the new grid");
            runAutoChop(lastAutoChopMode_, false, "downbeat");
        }
        else if (smodeService_ != nullptr && autoChopBusy_)
        {
            // N-20: the running job uses the old grid offset; re-chop once it lands.
            downbeatRechopPending_ = true;
            juce::Logger::writeToLog("[SModePage] downbeat arrived during auto-chop - re-chop deferred until it finishes");
        }
    });

    // Auto-open with one sequencer lane if none exist
    if (sequencerLanes_.empty())
        addSequencerLane();

    // Run BPM detection on the drum stem (tightest tempo reference)
    runBpmDetectionOnDrumStem();

    // Musical key on Melody + Bass (worker pool), shown next to the BPM.
    runKeyDetection();
    refreshInspector();

    // Audio is loaded — hide the empty-state overlay
    loadAudioOverlay_.setVisible(false);

    // Show eject button now that audio is loaded
    audioLoaded_ = true;
    ejectBtn_.setVisible(true);
    ejectBtn_.toFront(false);
    updateAudioDependentControls();
}

void SModePage::unloadCurrentAudio()
{
    juce::Logger::writeToLog("[SModePage] === unloadCurrentAudio START ===");

    // 1-3. Stop transport, kill voices, detach S-Mode model from MIDI
    if (auto* midiSvc = midiService())
    {
        juce::Logger::writeToLog("[SModePage]   stopping S-Mode transport + voices");
        midiSvc->stopSModeTransport();
        midiSvc->stopAllVoices();
        midiSvc->setSModeModel(nullptr);
        midiSvc->setGlobalTransposeSemitones(0.0f);
    }
    else
    {
        juce::Logger::writeToLog("[SModePage] WARNING: midi.mapping service not found");
    }

    // 4. Reset SModeService session
    if (smodeService_)
    {
        // Detach this page's model hooks first: smodeService_ becomes nullptr
        // below, so ~SModePage can no longer detach them, and the processor-
        // lifetime model would call into a dead page from SModeService::shutdown()
        // -> model.clear() (crash on plugin unload after Eject; found by the
        // UI self-test sweep).
        auto& model = smodeService_->getModel();
        model.onUndoStackChanged = nullptr;
        model.setExtraStateHooks(nullptr, nullptr);
        juce::Logger::writeToLog("[SModePage]   detached model callbacks, resetting SModeService");
        smodeService_->reset();
    }
    smodeService_ = nullptr;
    updateUndoUi();

    // 5. Clear UI state — remove all stem waveforms and sequencer lanes
    juce::Logger::writeToLog("[SModePage]   clearing " + juce::String(stemWaveforms_.size())
                              + " stem waveforms, " + juce::String(sequencerLanes_.size()) + " seq lanes");
    for (auto& w : stemWaveforms_)
        if (w) removeChildComponent(w.get());
    stemWaveforms_.clear();

    for (auto& l : sequencerLanes_)
        if (l) seqContent_.removeChildComponent(l.get());
    sequencerLanes_.clear();

    // Reset transport / playhead / loop state
    transportPlaying_ = false;
    playheadTimeSec_ = 0.0;
    looping_ = false;
    loopStemIdx_ = -1;
    loopSegIdx_ = -1;
    removedStems_.clear();
    focusedStem_ = -1;
    detectedBpm_ = 0.0;
    lockedBpm_ = 0.0;
    gridLocked_ = false;
    lockSemis_ = 0;
    updateGridLockButton();
    downbeatDetectionRequested_ = false;
    // Per-song displays: the BPM field kept the old song's value after Eject.
    bpmDotsAnim_.reset();
    ++bpmEditCounter_;   // a BPM / key job still running for the old song is dropped
    bpmInput_.setText("--", juce::dontSendNotification);
    setKeyText("--", "Musical key (detected on the Melody + Bass stems after a song loads)");
    selectChopForInspector(-1, -1);
    lastAutoChopValid_ = false;
    ++autoChopTicket_;       // N-20: results of jobs started for the old song are dropped
    downbeatRechopPending_ = false;
    ++quickAssignTicket_;
    autoChopBusy_ = false;
    updateAutoButton();
    resetQuickAssignButton();

    // Reflect "stopped" state on the big play button
    playBtn_.setPlaying(false);

    // Show empty-state overlay again
    audioLoaded_ = false;
    ejectBtn_.setVisible(false);
    updateAudioDependentControls();
    loadAudioOverlay_.setVisible(true);
    loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
    loadAudioOverlay_.toFront(true);

    resized();
    repaint();

    juce::Logger::writeToLog("[SModePage] === unloadCurrentAudio END ===");
}

void SModePage::paint(juce::Graphics& g)
{
    // Background gradient matching SampleBrowserPage
    auto bgBounds = getLocalBounds().toFloat();
    juce::Colour top = juce::Colour::fromString("ff3a3a3a");
    juce::Colour bottom = juce::Colour::fromString("ff0a0a0a");
    juce::ColourGradient bgGrad(top, bgBounds.getX(), bgBounds.getY(),
                                 bottom, bgBounds.getX(), bgBounds.getBottom(), false);
    bgGrad.addColour(0.5, juce::Colour::fromString("ff1c1c1c"));
    g.setGradientFill(bgGrad);
    g.fillRect(bgBounds);

    // (Empty state: LoadAudioOverlay shows LOAD AUDIO + recents; the old page-level
    //  "Drop a song here" text sat behind the overlay's button.)

    // Drag-over highlight
    if (dragHighlight_)
    {
        g.setColour(juce::Colour(0x443a86ff));
        g.fillRect(getLocalBounds());
        g.setColour(juce::Colour(0xff3a86ff));
        g.drawRect(getLocalBounds(), 2);
    }

    // Controller area background
    if (!controllerArea_.isEmpty())
    {
        g.setColour(juce::Colour(0xff14142a));
        g.fillRoundedRectangle(controllerArea_.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff2a2a4a));
        g.drawRoundedRectangle(controllerArea_.toFloat(), 4.0f, 1.0f);
    }

    // ── Playhead strip (above waveforms) with triangle marker ────────────
    if (!playheadStripArea_.isEmpty() && !stemWaveforms_.empty())
    {
        // Strip background
        g.setColour(juce::Colour(0xff1a1a2e));
        g.fillRect(playheadStripArea_);

        // Playhead marker, mapped onto the first visible lane's wave area.
        // (No page-level vertical line: every lane draws its own playhead.)
        const int phX = playheadStripX(playheadTimeSec_);
        if (phX >= 0)
        {
            juce::Path triangle;
            float tx = static_cast<float>(phX);
            float ty = static_cast<float>(playheadStripArea_.getY());
            float th = static_cast<float>(playheadStripArea_.getHeight());
            triangle.addTriangle(tx - 5.0f, ty, tx + 5.0f, ty, tx, ty + th);

            g.setColour(transportPlaying_ ? juce::Colours::lime : juce::Colours::white);
            g.fillPath(triangle);
        }
    }
}

void SModePage::resized()
{
    auto area = getLocalBounds().reduced(kPagePad);
    trackNameLabel_.setVisible(false);
    bpmLabel_.setVisible(false);
    titleLabel_.setVisible(false);   // replaced by logoLabel_
    transportBtn_.setVisible(false); // replaced by playBtn_

    const int barH = getHeight() >= kPageHTall ? kBarHTall
                   : getHeight() >= kPageHNormal ? kBarHNormal : kBarHShort;
    layoutTopBar(area.removeFromTop(barH).reduced(kBarPadX, 0));
    area.removeFromTop(kGap);
    int overlayTop = kPagePad + barH;
    if (noticeBanner_.isVisible())
    {
        noticeBanner_.setBounds(area.removeFromTop(kNoticeH));
        area.removeFromTop(kGap);
        overlayTop = area.getY();
    }
    else
    {
        noticeBanner_.setBounds({});
    }

    const bool hasLanes = ! stemWaveforms_.empty();
    // Empty state: no stale, click-through buttons under the overlay (D-9).
    for (auto* c : std::initializer_list<juce::Component*> { &autoBeatBtn_, &quickAssignBtn_, &monoPolyBtn_,
                       &linkAllBtn_, &snapToggle_, &freeformToggle_, &fadeToggle_, &tooltipToggle_, &clearAllBtn_,
                       &seqCollapseBtn_, &addSeqBtn_, &playAllSeqBtn_, &smartFillBtn_, &dragSeqWavArea_,
                       &chopInspector_ })
        c->setVisible(hasLanes);
    playheadStripArea_ = {};
    controllerArea_    = {};
    int stemH = 0, seqH = 0;

    if (hasLanes)
    {
        playheadStripArea_ = area.removeFromTop(kStripH);
        area.removeFromTop(2);

        int visibleCount = 0;
        for (auto& w : stemWaveforms_)
            if (w->isVisible()) ++visibleCount;
        visibleCount = juce::jmax(1, visibleCount);

        const int numSeq   = static_cast<int>(sequencerLanes_.size());
        const bool seqOpen = ! seqCollapsed_ && numSeq > 0;
        const int avail     = area.getHeight() - (kGap + kRowH + kGap + kRowH + kGap + kRowH);
        const int stemsMinH = visibleCount * kStemLaneMinH + (visibleCount - 1) * kStemGap;
        const int stemsPref = juce::jmax(stemsMinH, juce::roundToInt((float) avail * kStemShare));
        if (seqOpen)
        {
            const int wanted = numSeq * kSeqLaneMaxH + (numSeq - 1) * kSeqLaneGap;
            const int room   = juce::jmax(kSeqLaneMinH, avail - stemsPref - kSeqLaneGap);
            seqH = juce::jmin(wanted, room);
        }
        auto waveArea = area.removeFromTop(juce::jmax(0, avail - (seqOpen ? seqH + kSeqLaneGap : 0)));
        area.removeFromTop(kGap);
        layoutToolRow(area.removeFromTop(kRowH));
        area.removeFromTop(kGap);
        layoutInspectorRow(area.removeFromTop(kRowH));
        area.removeFromTop(kGap);
        layoutSeqHeader(area.removeFromTop(kRowH));
        area.removeFromTop(kSeqLaneGap);
        // Lanes are laid out even when collapsed so each lane's slotArea_ stays
        // valid for restoreState() (protects the 2026-04-22 "slots clustered left" fix).
        layoutSeqLanes(seqOpen ? area.removeFromTop(seqH) : area.withHeight(0), seqOpen);

        const int totalGaps = (visibleCount - 1) * kStemGap;
        stemH = juce::jmax(0, (waveArea.getHeight() - totalGaps) / visibleCount);
        for (auto& w : stemWaveforms_)
        {
            if (! w->isVisible()) continue;
            w->setBounds(waveArea.removeFromTop(stemH));
            waveArea.removeFromTop(kStemGap);
        }
        controllerAreaLabel_.setBounds({});
    }
    else
    {
        seqViewport_.setVisible(false);
        undoStatusLabel_.setBounds({});
    }

    // MIDI learn overlay covers the entire page
    midiLearnOverlay_.setBounds(getLocalBounds());

    // Empty-state overlay starts BELOW the top bar (D-9): LOAD SONG / settings stay reachable.
    loadAudioOverlay_.setBounds(getLocalBounds().withTrimmedTop(overlayTop));
    loadAudioOverlay_.toFront(false);
    if (noticeBanner_.isVisible())
        noticeBanner_.toFront(false);

    const juce::String log = juce::String("[SModePage] layout: bar=") + topBarModeName((int) topBarMode_) + "/" + juce::String(barH)
        + " lanes=" + juce::String((int) stemWaveforms_.size()) + "x" + juce::String(stemH)
        + " seq=" + juce::String((int) sequencerLanes_.size()) + (seqCollapsed_ ? " collapsed" : " open")
        + " seqH=" + juce::String(seqH) + " sys=" + juce::String(systemZoneW_);
    if (log != lastLayoutLog_)
    {
        lastLayoutLog_ = log;
        juce::Logger::writeToLog(log);
    }
}

void SModePage::layoutTopBar(juce::Rectangle<int> bar)
{
    const int barH  = bar.getHeight();
    const int ctrlH = barH >= kBarHTall ? 36 : (barH >= kBarHNormal ? 32 : 28);
    const int playH = barH >= kBarHTall ? 48 : (barH >= kBarHNormal ? 40 : 34);
    const int W = bar.getWidth();

    topBarMode_ = topBarMinWidth(kFull, true, true, systemZoneW_)    <= W ? TopBarMode::Full
                : topBarMinWidth(kMedium, false, true, systemZoneW_) <= W ? TopBarMode::Medium
                                                                         : TopBarMode::Compact;
    const auto& s      = topBarMode_ == TopBarMode::Full ? kFull : topBarMode_ == TopBarMode::Medium ? kMedium : kCompact;
    const bool full    = topBarMode_ == TopBarMode::Full;
    const bool compact = topBarMode_ == TopBarMode::Compact;
    const bool showLogo = ! compact || topBarMinWidth(kCompact, false, true, systemZoneW_) <= W;

    bpmTitleLabel_.setVisible(full);
    bpmRoundBtn_.setVisible(full);
    dawSyncBtn_.setVisible(full);
    halfTempoBtn_.setVisible(! compact);
    doubleTempoBtn_.setVisible(! compact);
    gridLockBtn_.setVisible(! compact);
    tempoMoreBtn_.setVisible(! full);
    logoLabel_.setVisible(showLogo);
    for (auto* c : std::initializer_list<juce::Component*> { &bpmTitleLabel_, &bpmRoundBtn_, &dawSyncBtn_,
                       &halfTempoBtn_, &doubleTempoBtn_, &gridLockBtn_, &tempoMoreBtn_, &logoLabel_ })
        if (! c->isVisible()) c->setBounds({});

    auto left = [&bar](juce::Component& c, int w, int h)
    {
        c.setBounds(bar.removeFromLeft(w).withSizeKeepingCentre(w, h));
    };

    // Right end: system zone (RootComponent's window buttons), then eject.
    systemZone_ = bar.removeFromRight(systemZoneW_);
    bar.removeFromRight(s.G);
    ejectBtn_.setBounds(bar.removeFromRight(s.eject).withSizeKeepingCentre(s.eject, s.eject));
    bar.removeFromRight(s.g);

    // Left groups: [logo] | play loop | undo redo | grid [BPM:] bpm [R][DAW] [half dbl lock] tap [...]
    if (showLogo) { left(logoLabel_, s.logo, barH - 8); bar.removeFromLeft(s.G); }
    left(playBtn_, s.play, playH);          bar.removeFromLeft(s.g);
    left(loopBtn_, s.loop, ctrlH);          bar.removeFromLeft(s.G);
    left(undoBtn_, s.undo, ctrlH);          bar.removeFromLeft(s.g);
    left(redoBtn_, s.undo, ctrlH);          bar.removeFromLeft(s.G);
    left(divisionCombo_, s.grid, ctrlH);    bar.removeFromLeft(s.g);
    divisionCombo_.toFront(false);
    if (full) left(bpmTitleLabel_, kBpmLabelW, ctrlH);
    left(bpmInput_, s.bpm, ctrlH);
    bar.removeFromLeft(s.g); left(keyLabel_, s.key, ctrlH);
    keyLabel_.setFont(juce::FontOptions(full ? 13.0f : (compact ? 11.0f : 12.0f), juce::Font::bold));
    if (full)
    {
        bar.removeFromLeft(4);   left(bpmRoundBtn_, kRoundW, kRoundW);
        bar.removeFromLeft(s.g); left(dawSyncBtn_, kDawW, ctrlH);
    }
    if (! compact)
    {
        bar.removeFromLeft(s.g); left(halfTempoBtn_,   s.half, ctrlH);
        bar.removeFromLeft(s.g); left(doubleTempoBtn_, s.dbl,  ctrlH);
        bar.removeFromLeft(s.g); left(gridLockBtn_,    s.lock, ctrlH);
    }
    bar.removeFromLeft(s.g); left(tapTempoBtn_, s.tap, ctrlH);
    if (! full) { bar.removeFromLeft(s.g); left(tempoMoreBtn_, s.more, ctrlH); }

    // Whatever is left is flex; LOAD SONG takes up to loadMax from its right end.
    bar.removeFromLeft(s.G);
    const int loadW = juce::jlimit(s.loadMin, s.loadMax, bar.getWidth());
    loadSongBtn_.setBounds(bar.removeFromRight(loadW).withSizeKeepingCentre(loadW, ctrlH));
    jassert(bar.getWidth() >= 0);   // fails only if the mode table and topBarMinWidth disagree
}

void SModePage::layoutToolRow(juce::Rectangle<int> row)
{
    clearAllBtn_.setBounds(row.removeFromRight(58));      row.removeFromRight(8);
    autoBeatBtn_.setBounds(row.removeFromLeft(95));       row.removeFromLeft(4);
    quickAssignBtn_.setBounds(row.removeFromLeft(80));    row.removeFromLeft(3);
    monoPolyBtn_.setBounds(row.removeFromLeft(75));       row.removeFromLeft(3);
    linkAllBtn_.setBounds(row.removeFromLeft(65));        row.removeFromLeft(6);
    snapToggle_.setBounds(row.removeFromLeft(62));        row.removeFromLeft(2);
    freeformToggle_.setBounds(row.removeFromLeft(52));    row.removeFromLeft(4);
    fadeToggle_.setBounds(row.removeFromLeft(52));        row.removeFromLeft(4);
    tooltipToggle_.setBounds(row.removeFromLeft(52));     row.removeFromLeft(8);
    // B15: the undo toast sits in the free space of this row, never over a button.
    undoStatusLabel_.setBounds(row.withWidth(juce::jmax(0, juce::jmin(280, row.getWidth()))));
}

void SModePage::layoutInspectorRow(juce::Rectangle<int> row)
{
    chopInspector_.setBounds(row);
}

void SModePage::layoutSeqHeader(juce::Rectangle<int> row)
{
    dragSeqWavArea_.setBounds(row.removeFromRight(110));
    seqCollapseBtn_.setBounds(row.removeFromLeft(58));    row.removeFromLeft(4);
    addSeqBtn_.setBounds(row.removeFromLeft(55));         row.removeFromLeft(3);
    playAllSeqBtn_.setBounds(row.removeFromLeft(55));     row.removeFromLeft(5);
    smartFillBtn_.setBounds(row.removeFromLeft(80));
}

void SModePage::layoutSeqLanes(juce::Rectangle<int> r, bool show)
{
    seqViewport_.setVisible(show);
    seqViewport_.setBounds(r);
    const int n = static_cast<int>(sequencerLanes_.size());
    if (n == 0)
    {
        seqContent_.setSize(r.getWidth(), 0);
        return;
    }
    const int fitH     = (r.getHeight() - (n - 1) * kSeqLaneGap) / n;
    const int laneH    = juce::jlimit(kSeqLaneMinH, kSeqLaneMaxH, fitH);
    const int contentH = n * laneH + (n - 1) * kSeqLaneGap;
    const bool scroll  = show && contentH > r.getHeight();
    const int contentW = juce::jmax(0, r.getWidth() - (scroll ? kSeqScrollBarW : 0));
    seqContent_.setSize(contentW, contentH);
    for (int i = 0; i < n; ++i)
        sequencerLanes_[(size_t) i]->setBounds(0, i * (laneH + kSeqLaneGap), contentW, laneH);
}

void SModePage::setSystemZoneWidth(int w)
{
    if (w == systemZoneW_) return;
    systemZoneW_ = w;
    if (! getLocalBounds().isEmpty()) resized();
}

void SModePage::updateSeqCollapseButton()
{
    seqCollapseBtn_.setButtonText(juce::String::charToString(seqCollapsed_ ? (juce::juce_wchar) 0x25B8
                                                                           : (juce::juce_wchar) 0x25BE) + " SEQ");
}

void SModePage::updateAudioDependentControls()
{
    // D-9: controls that do nothing without a song are disabled until one loads.
    for (auto* c : std::initializer_list<juce::Component*> { &playBtn_, &loopBtn_, &bpmRoundBtn_, &halfTempoBtn_,
                       &doubleTempoBtn_, &gridLockBtn_, &tapTempoBtn_ })
        c->setEnabled(audioLoaded_);
    juce::Logger::writeToLog("[SModePage] audio-dependent controls " + juce::String(audioLoaded_ ? "enabled" : "disabled"));
}

void SModePage::showTempoOverflowMenu()
{
    const bool compact = topBarMode_ == TopBarMode::Compact;
    juce::PopupMenu m;
    m.addItem(1, "Round BPM to integer", audioLoaded_ && detectedBpm_ >= 30.0);
    if (compact)
    {
        m.addItem(2, "Half tempo",   audioLoaded_ && detectedBpm_ / 2.0 >= 30.0);
        m.addItem(3, "Double tempo", audioLoaded_ && detectedBpm_ > 0.0 && detectedBpm_ * 2.0 <= 300.0);
        m.addItem(4, "Lock grid", audioLoaded_, gridLocked_);
    }
    m.addItem(5, "Sync tempo to DAW", true, dawSyncBtn_.getToggleState());
    juce::Logger::writeToLog("[SModePage] tempo overflow menu opened (" + juce::String(topBarModeName((int) topBarMode_)) + ")");
    juce::Component::SafePointer<SModePage> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tempoMoreBtn_),
        [safe](int r)
        {
            if (safe == nullptr || r == 0) return;
            juce::Logger::writeToLog("[SModePage] tempo menu item " + juce::String(r));
            switch (r)
            {
                case 1: safe->bpmRoundBtn_.triggerClick();    break;
                case 2: safe->halfTempoBtn_.triggerClick();   break;
                case 3: safe->doubleTempoBtn_.triggerClick(); break;
                case 4: safe->gridLockBtn_.triggerClick();    break;
                case 5: safe->dawSyncBtn_.triggerClick();     break;   // toggles, then onClick
                default: break;
            }
        });
}

StemWaveformComponent* SModePage::firstVisibleLane() const
{
    for (auto& w : stemWaveforms_)
        if (w != nullptr && w->isVisible())
            return w.get();
    return nullptr;
}

juce::Rectangle<int> SModePage::getWaveColumnInPage() const
{
    if (auto* w = firstVisibleLane())
        return getLocalArea(w, w->getWaveArea());
    return {};
}

int SModePage::playheadStripX(double timeSec) const
{
    auto* w = firstVisibleLane();
    if (w == nullptr || playheadStripArea_.isEmpty()) return -1;
    const double viewStart = w->getViewStartTime();
    const double viewEnd   = w->getViewEndTime();
    const double viewDur   = viewEnd - viewStart;
    if (viewDur <= 0.0 || timeSec < viewStart || timeSec > viewEnd) return -1;
    const auto col = getWaveColumnInPage();
    if (col.getWidth() <= 0) return -1;
    return col.getX() + static_cast<int>((timeSec - viewStart) / viewDur * col.getWidth());
}

juce::Colour SModePage::getStemColour(int index)
{
    static const juce::Colour colours[] = {
        juce::Colour(0xffb388ff),  // Vocals — Purple
        juce::Colour(0xffff7043),  // Bass — Orange/Red
        juce::Colour(0xff42a5f5),  // Drums — Blue
        juce::Colour(0xff66bb6a),  // Other — Green
        juce::Colour(0xffffd54f),  // Piano — Yellow (6-stem)
        juce::Colour(0xff26c6da),  // Guitar — Teal (6-stem)
    };
    constexpr int numColours = 6;
    return colours[index % numColours];
}

// -- Auto-chop -----------------------------------------------------------------

juce::String SModePage::autoModeName(AutoMode m)
{
    switch (m)
    {
        case AutoMode::Smart:     return "Auto Chop";
        case AutoMode::Beat:      return "Auto: Beat";
        case AutoMode::Bar:       return "Auto: Bar";
        case AutoMode::TwoBar:    return "Auto: 2-Bar";
        case AutoMode::Transient: return "Auto: Transient";
    }
    return "Auto Chop";
}

uint64_t SModePage::segmentLayoutHash(const SModeSegmentModel& m)
{
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFFu; h *= 1099511628211ULL; } };
    mix((uint64_t) m.getStemCount());
    for (const auto& st : m.getStems())
    {
        mix((uint64_t) st.segments.size());
        for (const auto& sg : st.segments)
        {
            mix((uint64_t) sg.startSample);
            mix((uint64_t) sg.endSample);
            mix((uint64_t) (int64_t) sg.midiNote);
        }
    }
    return h;
}

void SModePage::updateAutoButton()
{
    autoBeatBtn_.setEnabled(! autoChopBusy_);
    autoBeatBtn_.setButtonText(autoChopBusy_ ? juce::String("Analyzing...") : autoModeName(autoMode_));
    autoBeatBtn_.setTooltip(autoMode_ == AutoMode::Smart
        ? "Chop every stem: transients on Drums + Vocals, bars on Bass + Melody, max "
              + juce::String(kMaxChopsPerStem) + " chops per stem spread over the song, grid from the downbeat. "
              "Right-click: Beat / Bar / 2-Bar / Transient"
        : autoModeName(autoMode_) + " on every stem (max " + juce::String(kMaxChopsPerStem)
              + " chops per stem). Right-click to change the mode");
}

void SModePage::runAutoChop(AutoMode mode, bool pushUndo, const juce::String& reason)
{
    juce::Logger::writeToLog("[SModePage] === runAutoChop mode=" + autoModeName(mode) + " reason=" + reason
                             + " pushUndo=" + juce::String(pushUndo ? 1 : 0) + " ===");
    if (smodeService_ == nullptr) return;
    if (autoChopBusy_)
    {
        // Review U-H4: never two analyses at once (they used to stack chops).
        juce::Logger::writeToLog("[SModePage] runAutoChop ignored: an analysis is already running");
        return;
    }
    auto& model = smodeService_->getModel();
    double bpm = model.getDetectedTempo();
    if (bpm <= 0.0)
    {
        bpm = detectedBpm_ > 0.0 ? detectedBpm_ : 120.0;
        juce::Logger::writeToLog("[SModePage] runAutoChop: no detected tempo yet, using " + juce::String(bpm, 2));
    }
    const double sr = model.getSampleRate() > 0.0 ? model.getSampleRate() : 44100.0;
    const double offsetSec = model.getGridOffsetSeconds();

    struct Job { int stem; int kind; std::shared_ptr<const juce::AudioBuffer<float>> audio; };   // kind: 0 grid, 1 transient, 2 phrase
    std::vector<Job> jobs;
    double gridBeats = 4.0;
    if (mode == AutoMode::Beat)   gridBeats = 1.0;
    if (mode == AutoMode::TwoBar) gridBeats = 8.0;
    for (int si = 0; si < model.getStemCount(); ++si)
    {
        auto* st = model.getStem(si);
        if (st == nullptr || st->audioBuffer.getNumSamples() == 0) continue;
        if (si < (int) stemWaveforms_.size() && removedStems_.count(si) > 0) continue;   // removed channel
        int kind = 0;
        if (mode == AutoMode::Transient) kind = 1;
        else if (mode == AutoMode::Smart)
        {
            const auto n = st->name.toLowerCase();
            if (n.contains("drum"))       kind = 1;
            else if (n.contains("vocal")) kind = 2;
        }
        jobs.push_back({ si, kind, model.getSharedStemAudio(si) });   // B33: shared, no copy
    }
    if (jobs.empty()) return;

    autoChopBusy_ = true;
    const uint64_t ticket = ++autoChopTicket_;
    updateAutoButton();
    const uint64_t generation = model.getContentGeneration();
    const uint64_t layoutAtStart = segmentLayoutHash(model);
    auto safeThis = juce::Component::SafePointer<SModePage>(this);
    const bool queued = smodeService_->runBackground([safeThis, jobs, bpm, sr, offsetSec, gridBeats, generation,
                                                      layoutAtStart, mode, pushUndo, ticket]()
    {
        std::vector<std::pair<int, std::vector<Range>>> plans;
        const int64_t gridLen = (int64_t) std::llround(60.0 / bpm * gridBeats * sr);
        const int64_t offset  = (int64_t) std::llround(juce::jmax(0.0, offsetSec) * sr);
        for (const auto& j : jobs)
        {
            if (j.audio == nullptr) continue;
            auto cands = j.kind == 0 ? gridCandidates(j.audio->getNumSamples(), offset, gridLen)
                                     : transientCandidates(*j.audio, sr, bpm, j.kind == 2);
            const int nCand = (int) cands.size();
            auto picked = pickChops(*j.audio, cands, kMaxChopsPerStem);
            juce::Logger::writeToLog("[SModePage] auto-chop stem " + juce::String(j.stem) + ": "
                                     + juce::String(j.kind == 0 ? "grid" : (j.kind == 1 ? "transient" : "phrase"))
                                     + " candidates=" + juce::String(nCand) + " kept=" + juce::String((int) picked.size()));
            plans.push_back({ j.stem, std::move(picked) });
        }
        juce::MessageManager::callAsync([safeThis, plans = std::move(plans), generation, layoutAtStart, mode, pushUndo, ticket]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;   // page destroyed (editor closed)
            if (ticket != self->autoChopTicket_)
            {
                // N-20: a newer job (or a new song) owns the busy flag now.
                juce::Logger::writeToLog("[SModePage] auto-chop result dropped (stale ticket " + juce::String((juce::int64) ticket)
                                         + ", current " + juce::String((juce::int64) self->autoChopTicket_) + ")");
                return;
            }
            self->autoChopBusy_ = false;
            self->updateAutoButton();      // U-M8: the button never stays on "Analyzing..."
            const bool rechopPending = std::exchange(self->downbeatRechopPending_, false);   // dropped results never re-chop
            if (self->smodeService_ == nullptr) return;
            auto& model = self->smodeService_->getModel();
            if (generation != model.getContentGeneration())
            {
                juce::Logger::writeToLog("[SModePage] auto-chop result dropped (song changed)");
                return;
            }
            if (segmentLayoutHash(model) != layoutAtStart)
            {
                juce::Logger::writeToLog("[SModePage] auto-chop result dropped (chops edited / undone during the analysis)");
                self->flashUndoStatus("Auto chop cancelled: chops changed meanwhile");
                return;
            }
            if (pushUndo) model.pushUndoState("Auto chop");
            int total = 0;
            for (const auto& [si, ranges] : plans)
            {
                model.clearSegments(si);
                for (const auto& r : ranges) { model.addSegment(si, r.first, r.second); ++total; }
            }
            self->lastAutoChopValid_  = true;
            self->lastAutoChopMode_   = mode;
            self->lastAutoChopLayout_ = segmentLayoutHash(model);
            for (auto& wf : self->stemWaveforms_) { wf->setGridBPM(model.getDetectedTempo() > 0.0 ? model.getDetectedTempo() : 120.0); wf->repaint(); }
            self->refreshInspector();
            juce::Logger::writeToLog("[SModePage] auto-chop applied: " + juce::String(total) + " chops on "
                                     + juce::String((int) plans.size()) + " stems");
            if (rechopPending)
            {
                if (mode != AutoMode::Transient)
                {
                    juce::Logger::writeToLog("[SModePage] running the deferred downbeat re-chop");
                    self->runAutoChop(mode, false, "downbeat (deferred)");
                }
            }
        });
    });
    if (! queued && ticket == autoChopTicket_)
    {
        autoChopBusy_ = false;
        updateAutoButton();
        juce::Logger::writeToLog("[SModePage] auto-chop job NOT queued (service shutting down)");
    }
}

// -- Quick Assign (B21) ------------------------------------------------------

void SModePage::resetQuickAssignButton()
{
    quickAssignBusy_ = false;
    quickAssignBtn_.setEnabled(true);
    quickAssignBtn_.setButtonText("Quick Assign");
}

void SModePage::runQuickAssign(const std::set<int>& stems)
{
    juce::Logger::writeToLog("[SModePage] === runQuickAssign stems=" + juce::String((int) stems.size()) + " ===");
    if (smodeService_ == nullptr || quickAssignBusy_ || stems.empty()) return;
    auto& model = smodeService_->getModel();

    // Chops are identified by their sample range (indices go stale if the
    // user edits during the analysis - review U-M5). Audio is shared (B33).
    struct StemJob { int stem; std::shared_ptr<const juce::AudioBuffer<float>> audio; std::vector<Range> chops; };
    std::vector<StemJob> jobs;
    for (int si : stems)
    {
        auto* st = model.getStem(si);
        if (st == nullptr || samplar::MidiMappingService::getStemBankFirstNote(si) < 0) continue;
        StemJob j { si, model.getSharedStemAudio(si), {} };
        for (const auto& sg : st->segments) j.chops.push_back({ sg.startSample, sg.endSample });
        jobs.push_back(std::move(j));
    }
    if (jobs.empty()) return;

    quickAssignBusy_ = true;
    const uint64_t ticket = ++quickAssignTicket_;
    quickAssignBtn_.setEnabled(false);
    quickAssignBtn_.setButtonText("Analyzing...");
    const uint64_t generation = model.getContentGeneration();
    auto safeThis = juce::Component::SafePointer<SModePage>(this);
    const bool queued = smodeService_->runBackground([safeThis, jobs, generation, ticket]()
    {
        // Audible chops in time order, per stem.
        std::vector<std::pair<int, std::vector<Range>>> audible;
        for (const auto& j : jobs)
        {
            std::vector<Range> keep;
            for (const auto& r : j.chops)
                if (j.audio != nullptr && rangeRmsDb(*j.audio, r) > -40.0) keep.push_back(r);
            std::sort(keep.begin(), keep.end());
            audible.push_back({ j.stem, std::move(keep) });
        }
        juce::MessageManager::callAsync([safeThis, audible = std::move(audible), generation, ticket]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            if (ticket != self->quickAssignTicket_)
            {
                juce::Logger::writeToLog("[SModePage] Quick Assign result dropped (stale ticket)");
                return;
            }
            self->resetQuickAssignButton();
            if (self->smodeService_ == nullptr) return;
            auto& model = self->smodeService_->getModel();
            if (generation != model.getContentGeneration())
            {
                juce::Logger::writeToLog("[SModePage] Quick Assign result dropped (song changed)");
                return;
            }
            model.pushUndoState("Quick assign");
            int mapped = 0, skipped = 0;
            juce::StringArray summary;
            for (const auto& [si, ranges] : audible)
            {
                auto* st = model.getStem(si);
                const int first = samplar::MidiMappingService::getStemBankFirstNote(si);
                if (st == nullptr || first < 0) continue;
                for (int k = 0; k < (int) st->segments.size(); ++k)
                    if (st->segments[(size_t) k].midiNote >= 0) model.clearMidiNote(si, k);
                int note = first;
                for (const auto& r : ranges)
                {
                    if (note >= first + samplar::MidiMappingService::kBankSize) { ++skipped; continue; }
                    for (int k = 0; k < (int) st->segments.size(); ++k)
                        if (st->segments[(size_t) k].startSample == r.first && st->segments[(size_t) k].endSample == r.second)
                        {
                            model.assignMidiNote(si, k, note++);
                            ++mapped;
                            break;
                        }
                }
                if (note > first)
                    summary.add(st->name + " " + samplar::MidiMappingService::noteNumberToName(first) + "-"
                                + samplar::MidiMappingService::noteNumberToName(note - 1));
            }
            juce::Logger::writeToLog("[SModePage] Quick Assign: " + juce::String(mapped) + " chops mapped, "
                                     + juce::String(skipped) + " beyond the 16-note bank (no chop deleted): "
                                     + summary.joinIntoString(", "));
            self->flashUndoStatus(summary.isEmpty() ? juce::String("Quick Assign: no audible chops")
                                                    : "Mapped " + summary.joinIntoString(", ")
                                                      + (skipped > 0 ? " (" + juce::String(skipped) + " over 16 not mapped)" : juce::String()));
            for (auto& wf : self->stemWaveforms_) wf->repaint();
            self->refreshInspector();
        });
    });
    if (! queued && ticket == quickAssignTicket_) resetQuickAssignButton();
}

void SModePage::updateSnapState()
{
    bool snap = snapToggle_.getToggleState();
    bool freeform = freeformToggle_.getToggleState();
    for (auto& wf : stemWaveforms_)
    {
        wf->setSnapEnabled(snap);
        wf->setFreeformEnabled(freeform);
    }
}

// ── View Sync (Link All) ────────────────────────────────────────────────────

void SModePage::syncViewRange(int sourceStemIdx, double startTime, double endTime)
{
    if (!smodeService_) return;
    auto& model = smodeService_->getModel();
    bool srcLinked = linkAllActive_ || model.isLinked(sourceStemIdx);
    if (!srcLinked) return;

    // Also sync grid offset from source
    double gridOffset = stemWaveforms_[static_cast<size_t>(sourceStemIdx)]->getGridOffsetSeconds();

    for (int i = 0; i < static_cast<int>(stemWaveforms_.size()); ++i)
    {
        if (i == sourceStemIdx) continue;
        bool destLinked = linkAllActive_ || model.isLinked(i);
        if (!destLinked) continue;

        stemWaveforms_[static_cast<size_t>(i)]->setViewRange(startTime, endTime);
        stemWaveforms_[static_cast<size_t>(i)]->setGridOffsetSeconds(gridOffset);
    }
}

// ── Grid subdivision ────────────────────────────────────────────────────────

void SModePage::setGridDivision(int division)
{
    // Accept only the 5 supported values; fall back to quarter note otherwise.
    if (division != 1 && division != 2 && division != 4 && division != 8 && division != 16)
        division = 4;

    if (division == currentGridDivision_) return;  // no-op = no undo entry

    if (smodeService_)
        smodeService_->getModel().pushUndoState("Grid division");

    currentGridDivision_ = division;

    // Propagate to every stem lane — this drives both grid rendering AND snap.
    for (auto& wf : stemWaveforms_)
        if (wf) wf->setGridDivision(division);

    // Sync the combo box without re-firing onChange.
    divisionCombo_.setSelectedId(division, juce::dontSendNotification);

    juce::Logger::writeToLog("[SModePage] Grid division set to 1/" + juce::String(division));
}

// ── BPM / Tempo ─────────────────────────────────────────────────────────────

void SModePage::applyBpm(double bpm, bool pushUndo, bool fromDetection)
{
    juce::Logger::writeToLog("[SModePage] applyBpm: " + juce::String(bpm, 4) + " undo=" + juce::String(pushUndo ? 1 : 0)
                             + " detection=" + juce::String(fromDetection ? 1 : 0));
    if (! (bpm > 0.0)) return;
    if (! fromDetection) ++bpmEditCounter_;   // U-M6: a running detection must not overwrite this

    // Stop the "..." dots animation now that we have a real BPM to display.
    bpmDotsAnim_.reset();

    // B24: only a real change is an undo step (focus changes / re-applies are not).
    const bool changed = std::abs(bpm - detectedBpm_) > 1.0e-6;
    if (pushUndo && changed && smodeService_)
        smodeService_->getModel().pushUndoState(
            gridLocked_ && lockedBpm_ > 0.0 ? juce::String("BPM transpose") : juce::String("BPM change"));

    if (gridLocked_ && lockedBpm_ > 0.0)
    {
        // B17: grid locked -> the BPM change becomes a transpose of every
        // voice, applied by the realtime engine (12 * log2(new / locked)).
        const double ratio = bpm / lockedBpm_;
        lockSemis_ = juce::jlimit(-24, 24, (int) std::lround(12.0 * std::log2(ratio)));
        applyLockTranspose();
        updateGridLockButton();
        juce::Logger::writeToLog("[SModePage] Grid LOCKED transpose: ratio=" + juce::String(ratio, 4)
                                 + " semitones=" + juce::String(lockSemis_));

        // Update display but do NOT move the grid
        detectedBpm_ = bpm;
        bpmInput_.setText(juce::String(bpm, 2), juce::dontSendNotification);
        bpmLabel_.setText("BPM: " + juce::String(bpm, 2) + " (T" + juce::String(lockSemis_ > 0 ? "+" : "")
                          + juce::String(lockSemis_) + "st)", juce::dontSendNotification);
        return;
    }

    // Normal (unlocked) - move the grid
    detectedBpm_ = bpm;
    bpmInput_.setText(juce::String(bpm, 2), juce::dontSendNotification);
    bpmLabel_.setText("BPM: " + juce::String(bpm, 2), juce::dontSendNotification);

    if (smodeService_)
        smodeService_->getModel().setDetectedTempo(bpm);

    // Update grid on all waveforms
    for (auto& wf : stemWaveforms_)
    {
        wf->setGridBPM(bpm);
        wf->repaint();
    }

    juce::Logger::writeToLog("[SModePage] BPM applied: " + juce::String(bpm, 2));

    // Auto downbeat (beat 1) detection, one-shot per stem load. The result
    // lands via "smode.downbeat.detected" (onStemsReady subscription).
    if (smodeService_ && !downbeatDetectionRequested_)
    {
        auto& model = smodeService_->getModel();
        if (model.getGridOffsetSeconds() == 0.0 && bpm >= 60.0 && bpm <= 220.0)
        {
            downbeatDetectionRequested_ = true;
            smodeService_->runDownbeatDetectionAsync(bpm);
        }
    }
}

void SModePage::updateGridLockButton()
{
    // Same text + colours from every path (click, overflow menu, undo - B24).
    gridLockBtn_.setButtonText(! gridLocked_ ? juce::String("LOCK")
                               : (lockSemis_ == 0 ? juce::String("LOCKED")
                                                  : (lockSemis_ > 0 ? "LK +" : "LK ") + juce::String(lockSemis_)));
    gridLockBtn_.setColour(juce::TextButton::buttonColourId,
                           gridLocked_ ? juce::Colour(0xffb35900) : juce::Colour::fromString("ff21262D"));
}

void SModePage::applyLockTranspose()
{
    const int semis = gridLocked_ ? lockSemis_ : 0;
    if (auto* m = midiService())
        m->setGlobalTransposeSemitones((float) semis);
    juce::Logger::writeToLog("[SModePage] LOCK transpose -> engine: " + juce::String(semis) + " st");
}

// ── Loop playback (Alt+Click) ─────────────────────────────────────────────

void SModePage::playLoopedSegment(int stemIdx, int segIdx)
{
    if (!looping_ || !smodeService_) return;

    auto& model = smodeService_->getModel();
    auto* stem = model.getStem(stemIdx);
    auto* seg  = model.getSegment(stemIdx, segIdx);
    if (!stem || !seg || seg->getLengthSamples() <= 0) { stopLoop(); return; }

    auto* midiSvc = midiService();
    if (!midiSvc) { stopLoop(); return; }

    midiSvc->stopAllVoices();

    // Voice-level looping in the engine: seamless wrap with a crossfade (A13),
    // stem pitch knob applied, no buffer copy.
    midiSvc->playSegment(stemIdx, segIdx, true);

    double sr = model.getSampleRate();
    if (sr <= 0.0) sr = 44100.0;
    const double durSec = static_cast<double>(seg->getLengthSamples()) / sr;

    // Animate the segment
    if (stemIdx < static_cast<int>(stemWaveforms_.size()))
        stemWaveforms_[static_cast<size_t>(stemIdx)]->startSegmentAnimation(segIdx, durSec);
}

void SModePage::stopLoop()
{
    looping_ = false;
    loopStemIdx_ = -1;
    loopSegIdx_ = -1;

    if (auto* midiSvc = midiService()) midiSvc->stopAllVoices();
    juce::Logger::writeToLog("[SModePage] Loop STOP");
}

// ── Transport ─────────────────────────────────────────────────────────────

void SModePage::startTransport(double startTimeSeconds)
{
    if (auto* midiSvc = midiService())
    {
        // Stop any segment-click / MIDI-triggered voices that are already playing
        // so the global transport is the only audio source (Issue 1 fix).
        midiSvc->stopAllVoices();
        midiSvc->startSModeTransport(startTimeSeconds);
        transportPlaying_ = true;
        playBtn_.setPlaying(true);
        transportBtn_.setButtonText(juce::String::charToString(0x25A0));
        transportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
        juce::Logger::writeToLog("[SModePage] Transport START at " + juce::String(startTimeSeconds, 2) + "s");
    }
}

void SModePage::stopTransport()
{
    if (auto* midiSvc = midiService())
        midiSvc->stopSModeTransport();

    transportPlaying_ = false;
    playBtn_.setPlaying(false);
    transportBtn_.setButtonText(juce::String::charToString(0x25B6));  // ▶ play
    transportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));

    // Update playhead on all waveforms
    for (auto& wf : stemWaveforms_)
        wf->setPlayheadTime(playheadTimeSec_);
    repaint();

    juce::Logger::writeToLog("[SModePage] Transport STOP");
}

void SModePage::timerCallback()
{
    auto* midiSvc = midiService();
    if (midiSvc == nullptr) return;

    syncSequencerUi(*midiSvc);
    if (inspStem_ >= 0) refreshInspector();   // follows undo / delete / resize of the chop
    syncStemPitchKnobs();
    if (midiSvc->getCcBindingsGeneration() != lastCcGeneration_)
        refreshMidiLearnInfo();

    if (!transportPlaying_) return;

    // Poll transport position from the engine (atomics)
    if (!midiSvc->isSModeTransportActive())
    {
        stopTransport();
        return;
    }
    playheadTimeSec_ = midiSvc->getSModeTransportPositionSeconds();
    updatePlayheadDisplay();
}

void SModePage::updatePlayheadDisplay()
{
    // B32: each lane repaints only its old/new playhead column; the page
    // repaints only the strip marker (old and new triangle), never the page.
    for (auto& wf : stemWaveforms_)
        wf->setPlayheadTime(playheadTimeSec_);
    const int x = playheadStripX(playheadTimeSec_);
    if (x != lastStripMarkerX_)
    {
        if (lastStripMarkerX_ >= 0) repaint(playheadStripArea_.withX(lastStripMarkerX_ - 6).withWidth(13));
        if (x >= 0)                 repaint(playheadStripArea_.withX(x - 6).withWidth(13));
        lastStripMarkerX_ = x;
    }
}

void SModePage::setPlayheadFromMouse(int x)
{
    if (stemWaveforms_.empty() || playheadStripArea_.isEmpty()) return;

    // Convert strip X to time using the first visible lane's wave area + view range
    auto* lane = firstVisibleLane();
    const auto col = getWaveColumnInPage();
    if (lane == nullptr || col.getWidth() <= 0) return;

    double frac = static_cast<double>(x - col.getX()) / col.getWidth();
    frac = juce::jlimit(0.0, 1.0, frac);

    double viewStart = lane->getViewStartTime();
    double viewEnd = lane->getViewEndTime();
    playheadTimeSec_ = viewStart + frac * (viewEnd - viewStart);

    updatePlayheadDisplay();
}

void SModePage::mouseDown(const juce::MouseEvent& e)
{
    if (playheadStripArea_.contains(e.getPosition()))
    {
        setPlayheadFromMouse(e.x);

        // If transport is playing, restart from new position
        if (transportPlaying_)
            startTransport(playheadTimeSec_);
    }
}

void SModePage::mouseDrag(const juce::MouseEvent& e)
{
    if (playheadStripArea_.contains(e.getPosition()) || playheadStripArea_.contains(e.getMouseDownPosition()))
    {
        setPlayheadFromMouse(e.x);

        // If transport is playing, scrub to the new position in real-time
        if (transportPlaying_)
        {
            if (auto* midiSvc = midiService())
                midiSvc->startSModeTransport(playheadTimeSec_);
        }
    }
}

void SModePage::mouseUp(const juce::MouseEvent&)
{
    // Nothing special needed
}

bool SModePage::keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/)
{
    // Shift+Space = Play All / Stop All toggle
    if (key == juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::shiftModifier, 0))
    {
        if (seqPlayingFlag_ && seqPlayingFlag_->load())
        {
            stopAllSequencerPlayback();
        }
        else
        {
            playAllLanesSimultaneous();
            playAllSeqBtn_.setButtonText("Stop");
            playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
        }
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        // Context-aware: invoke whichever play action the user most recently
        // triggered (top transport / Play All / per-lane). Default is top
        // transport, set in the SModePage constructor.
        if (spaceKeyAction_)
        {
            spaceKeyAction_();
        }
        else if (smodeService_)
        {
            // Defensive fallback — should never happen.
            stopLoop();
            if (transportPlaying_) stopTransport();
            else startTransport(playheadTimeSec_);
        }
        return true;
    }
    // Ctrl+Shift+Z = redo (match before Ctrl+Z so shifted variant wins)
    if (key == juce::KeyPress('z',
            juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        redoBtn_.triggerClick();
        return true;
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        undoBtn_.triggerClick();
        return true;
    }
    if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))
    {
        redoBtn_.triggerClick();
        return true;
    }

    // Computer keyboard MIDI (A W S E D F T G Y H U J K O L P ; = 17 notes,
    // Z / X = octave). MidiMappingService owns the held-key state: exactly one
    // note-on per physical press, the note-off releases the note that key
    // started even if the octave moved meanwhile.
    if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown())
    {
        if (auto* midiSvc = midiService(); midiSvc != nullptr && midiSvc->isComputerKeyboardEnabled())
        {
            if (samplar::MidiMappingService::isOctaveKey(key))
            {
                midiSvc->handleKeyPress(key);
                flashKeyboardRange(*midiSvc);
                return true;
            }
            if (samplar::MidiMappingService::keyToSemitoneOffset(key) >= 0)
            {
                midiSvc->handleKeyPress(key);   // dedup'd; fires noteOn only on first press
                return true;
            }
        }
    }

    return false;
}

void SModePage::flashKeyboardRange(samplar::MidiMappingService& midiSvc)
{
    const int lo = midiSvc.getComputerKeyboardBaseNote();
    const int hi = midiSvc.getComputerKeyboardTopNote();
    juce::String bankInfo;
    for (int st = 0; st < 8; ++st)
    {
        const int first = samplar::MidiMappingService::getStemBankFirstNote(st);
        if (first < 0) break;
        if (first <= hi && first + samplar::MidiMappingService::kBankSize - 1 >= lo && smodeService_ != nullptr)
            if (auto* stem = smodeService_->getModel().getStem(st))
                bankInfo << (bankInfo.isEmpty() ? " - " : ", ") << stem->name;
    }
    flashUndoStatus("Keys A..; = " + samplar::MidiMappingService::noteNumberToName(lo) + "-"
                    + samplar::MidiMappingService::noteNumberToName(hi) + bankInfo);
}

bool SModePage::keyStateChanged(bool /*isKeyDown*/, juce::Component* /*originatingComponent*/)
{
    // Releases are polled by the service (it knows which note each key started).
    auto* midiSvc = midiService();
    if (midiSvc == nullptr || !midiSvc->isComputerKeyboardEnabled()) return false;
    return midiSvc->releaseUnheldComputerKeys();
}

// ── Grid Quick Assign (per-stem) ─────────────────────────────────────────

void SModePage::gridQuickAssignStem(int stemIndex, double beatsPerSegment)
{
    if (!smodeService_) return;
    auto& model = smodeService_->getModel();
    double bpm = model.getDetectedTempo();
    if (bpm <= 0.0) bpm = 120.0;

    double sampleRate = model.getSampleRate();
    double segDuration = (60.0 / bpm) * beatsPerSegment;
    int64_t segSamples = static_cast<int64_t>(segDuration * sampleRate);

    model.pushUndoState("Grid quick-assign");

    // Determine which stems to process (linked or just this one)
    std::vector<int> stemsToProcess;
    bool thisLinked = linkAllActive_ || model.isLinked(stemIndex);
    if (thisLinked)
    {
        for (int i = 0; i < model.getStemCount(); ++i)
        {
            if (linkAllActive_ || model.isLinked(i))
                stemsToProcess.push_back(i);
        }
    }
    else
    {
        stemsToProcess.push_back(stemIndex);
    }

    // Use grid offset so segments align with visible grid lines
    const double gridOffset = model.getGridOffsetSeconds();
    int64_t offsetSamples = juce::jlimit((int64_t)0,
                                          (int64_t)(sampleRate * 600),
                                          static_cast<int64_t>(gridOffset * sampleRate));

    // B21: each stem maps onto its own 16-note bank (Vocals C1, Bass E2 ...).
    for (int si : stemsToProcess)
    {
        const int bankFirst = samplar::MidiMappingService::getStemBankFirstNote(si);
        int nextNote = bankFirst;
        const int bankEnd = bankFirst < 0 ? -1 : bankFirst + samplar::MidiMappingService::kBankSize;
        model.clearSegments(si);
        auto* stem = model.getStem(si);
        if (!stem || stem->audioBuffer.getNumSamples() == 0) continue;

        int64_t total = stem->audioBuffer.getNumSamples();

        // Pre-grid-offset segment
        if (offsetSamples > 0 && offsetSamples < total)
        {
            int segIdx = model.addSegment(si, 0, offsetSamples);
            if (segIdx >= 0 && nextNote >= 0 && nextNote < bankEnd)
            {
                auto& seg = stem->segments[static_cast<size_t>(segIdx)];
                if (segmentHasAudio(*stem, seg))
                    model.assignMidiNote(si, segIdx, nextNote++);
            }
        }

        int64_t pos = offsetSamples;
        while (pos + segSamples <= total)
        {
            int segIdx = model.addSegment(si, pos, pos + segSamples);
            if (segIdx >= 0 && nextNote >= 0 && nextNote < bankEnd)
            {
                auto& seg = stem->segments[static_cast<size_t>(segIdx)];
                if (segmentHasAudio(*stem, seg))
                    model.assignMidiNote(si, segIdx, nextNote++);
            }
            pos += segSamples;
        }
        if (total - pos > segSamples / 4)
        {
            int segIdx = model.addSegment(si, pos, total);
            if (segIdx >= 0 && nextNote >= 0 && nextNote < bankEnd)
            {
                auto& seg = stem->segments[static_cast<size_t>(segIdx)];
                if (segmentHasAudio(*stem, seg))
                    model.assignMidiNote(si, segIdx, nextNote++);
            }
        }
    }

    juce::Logger::writeToLog("[SModePage] Grid Quick Assign: " + juce::String(beatsPerSegment)
                             + " beats at " + juce::String(bpm, 1) + " BPM on "
                             + juce::String(stemsToProcess.size()) + " stems (notes from each stem's bank, max 16)");

    lastAutoChopValid_ = false;
    for (auto& wf : stemWaveforms_)
    {
        wf->setGridBPM(bpm);
        wf->repaint();
    }
    refreshInspector();
}

void SModePage::runBpmDetectionOnDrumStem()
{
    if (!smodeService_) return;

    auto& model = smodeService_->getModel();
    int stemCount = model.getStemCount();

    // Fast path: BPM already detected from a previous session (survives editor close/reopen)
    double cachedBpm = model.getDetectedTempo();
    if (cachedBpm > 0.0)
    {
        juce::Logger::writeToLog("[SModePage] BPM restored from cache: " + juce::String(cachedBpm, 2));
        applyBpm(cachedBpm, false, true);   // reconnect: not an undo step
        return;
    }

    // Find the drums stem (index 2 in htdemucs 6-stem, or search by name)
    int drumIdx = -1;
    for (int i = 0; i < stemCount; ++i)
    {
        auto* stem = model.getStem(i);
        if (stem && stem->name.containsIgnoreCase("drum"))
        {
            drumIdx = i;
            break;
        }
    }
    // Fallback: use stem index 2 (default drums position in demucs)
    if (drumIdx < 0 && stemCount > 2) drumIdx = 2;
    if (drumIdx < 0) drumIdx = 0;  // Last resort: first stem

    auto* drumStem = model.getStem(drumIdx);
    if (!drumStem || drumStem->audioBuffer.getNumSamples() == 0)
    {
        juce::Logger::writeToLog("[SModePage] No drum stem audio for BPM detection");
        bpmInput_.setText("N/A", juce::dontSendNotification);
        return;
    }

    const bool isDrumStem = drumStem->name.containsIgnoreCase("drum");
    if (isDrumStem)
        juce::Logger::writeToLog("[HybridTempo] 🥁 using drum stem (cleaner ODF)");
    else
        juce::Logger::writeToLog("[HybridTempo] using full mix (no drum stem available)");

    juce::Logger::writeToLog("[SModePage] Running BPM detection on stem: " + drumStem->name
                             + " (index " + juce::String(drumIdx) + ")");
    bpmInput_.setText("...", juce::dontSendNotification);

    // Kick off the "..." animation in the BPM field while detection runs.
    // Stops automatically when applyBpm() commits the real result.
    {
        struct BpmDotsTimer : juce::Timer
        {
            juce::Component::SafePointer<juce::TextEditor> target;
            int phase = 0;
            void timerCallback() override
            {
                if (target == nullptr) { stopTimer(); return; }
                static const char* frames[] = {
                    ".  ", ".. ", "...", " ..", "  .", "  .", " ..", "..."
                };
                target->setText(frames[(phase++) % 8], juce::dontSendNotification);
            }
        };
        auto t = std::make_unique<BpmDotsTimer>();
        t->target = juce::Component::SafePointer<juce::TextEditor>(&bpmInput_);
        t->startTimerHz(6);   // ~167 ms per frame
        bpmDotsAnim_ = std::move(t);
    }

    // B33: share the model's immutable stem buffer (refcounted - a new song
    // load cannot free it under the job) instead of copying the whole stem.
    auto bufCopy = model.getSharedStemAudio(drumIdx);
    if (bufCopy == nullptr) { bpmDotsAnim_.reset(); bpmInput_.setText("N/A", juce::dontSendNotification); return; }
    double sampleRate = model.getSampleRate();
    const uint64_t generation = model.getContentGeneration();
    const int editStamp = bpmEditCounter_;
    auto safeThis = juce::Component::SafePointer<SModePage>(this);

    // Cancel token: set first thing in SModeService::shutdown(), so unloading
    // the plugin mid-analysis returns within ~200 ms (review A4 / U-H1). The
    // flag lives as long as the service, whose pool drains this job.
    const std::atomic<bool>* cancelFlag = smodeService_->getJobCancelFlag();
    // Debug-only evidence for the smoke host's --bpm-unload check (inert
    // unless the UI self-test is enabled). Written by the worker: file I/O, no logging.
    const juce::File debugDir = uiSelfTest_ != nullptr ? juce::File(SModeUiSelfTest::readEnv("SAMPLAR_UI_SWEEP_DIR")) : juce::File();
    const bool roundIt = samplar::SamplarSettings::getInstance().getAutoRoundBpm();
    juce::Logger::writeToLog("[SModePage] BPM job queued: " + juce::String(bufCopy->getNumSamples()) + " samples @ "
                             + juce::String(sampleRate, 0) + " Hz, autoRound=" + juce::String(roundIt ? "ON" : "OFF"));

    const bool queued = smodeService_->runBackground([this, safeThis, generation, editStamp, bufCopy, sampleRate,
                                                      cancelFlag, debugDir, roundIt]()
    {
        // Worker thread: no logging, no `this` (only inside the guarded callAsync).
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        if (debugDir != juce::File())
            debugDir.getChildFile("bpm_started.txt").replaceWithText(juce::String(bufCopy->getNumSamples()));

        samplar::HybridTempoDetector detector;
        samplar::HybridTempoDetector::Config cfg;
        cfg.minBPM = 50.0f;
        cfg.maxBPM = 220.0f;
        const auto result = detector.detect(*bufCopy, sampleRate, cfg, samplar::AnalysisCancel(cancelFlag));
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;

        if (debugDir != juce::File())
            debugDir.getChildFile(result.cancelled ? "bpm_cancelled.txt" : "bpm_done.txt")
                .replaceWithText(juce::String(ms, 1) + " ms");
        if (result.cancelled)
            return;   // unloading: never apply (and never post) a cancelled result

        const double bpm = (result.bpm > 0.0f) ? static_cast<double>(result.bpm) : 120.0;
        // Auto-round the detected BPM to the nearest integer if the user
        // setting is enabled (manual entry / host sync / tap keep full precision).
        const double finalBpm = roundIt ? std::round(bpm) : bpm;
        const juce::String summary = juce::String(bpm, 2) + " BPM (confidence=" + juce::String(result.confidence, 2)
                                   + " source=" + result.getSourceName() + ", " + juce::String(ms, 0) + " ms) -> "
                                   + juce::String(finalBpm, 4);
        juce::MessageManager::callAsync([this, safeThis, generation, editStamp, finalBpm, summary]()
        {
            if (safeThis == nullptr) return;   // page destroyed (editor closed)
            juce::Logger::writeToLog("[SModePage] HybridTempoDetector: " + summary);
            if (!smodeService_) { bpmDotsAnim_.reset(); return; }
            if (generation != smodeService_->getModel().getContentGeneration())
            {
                juce::Logger::writeToLog("[SModePage] BPM result dropped (song changed)");
                bpmDotsAnim_.reset();   // U-M8: never leave the "..." animation running
                return;
            }
            if (editStamp != bpmEditCounter_)
            {
                // U-M6: the user typed / tapped / synced a BPM meanwhile - keep it.
                juce::Logger::writeToLog("[SModePage] BPM result " + juce::String(finalBpm, 2)
                                         + " dropped (BPM edited while detecting)");
                bpmDotsAnim_.reset();
                bpmInput_.setText(juce::String(detectedBpm_, 2), juce::dontSendNotification);
                return;
            }
            // First tempo of a song: not an undo step (Ctrl+Z must not blank the grid).
            applyBpm(finalBpm, false, true);
        });
    });
    if (! queued)
    {
        juce::Logger::writeToLog("[SModePage] BPM job NOT queued (service shutting down)");
        bpmDotsAnim_.reset();
        bpmInput_.setText("N/A", juce::dontSendNotification);
    }
}

// ── Sequencer Lanes ───────────────────────────────────────────────────────

void SModePage::addSequencerLane()
{
    if (!smodeService_) return;
    if (static_cast<int>(sequencerLanes_.size()) >= 4) return;  // Max 4 lanes
    auto& model = smodeService_->getModel();

    int laneIdx = static_cast<int>(sequencerLanes_.size());
    auto lane = std::make_unique<SequencerLaneComponent>(model, laneIdx);

    // Set stem info (colors + count) and BPM
    lane->setStemInfo(model.getStemCount(), [](int idx) { return getStemColour(idx); });
    lane->setServiceRegistry(registry_);
    lane->setBpm(detectedBpm_ > 0.0 ? detectedBpm_ : 120.0);

    // Wire callbacks
    auto* lanePtr = lane.get();
    lane->onRemoveRequested = [this, lanePtr](int /*idx*/)
    {
        // Find lane by pointer (indices go stale after removals)
        for (int i = 0; i < static_cast<int>(sequencerLanes_.size()); ++i)
        {
            if (sequencerLanes_[static_cast<size_t>(i)].get() == lanePtr)
            {
                if (smodeService_) smodeService_->getModel().pushUndoState("Remove lane");
                removeSequencerLane(i);
                return;
            }
        }
    };
    lane->onStatusMessage = [this](const juce::String& text) { flashUndoStatus(text); };
    lane->onLaneChanged = [this](int /*idx*/)
    {
        // The engine plays from the latest lane data at the next cell - no
        // restart, no timer chain (B36).
        if (auto* m = midiService()) pushLanesToEngine(*m);
    };
    // Drag WAV of this lane: rendered by the realtime engine on WaveDragArea's
    // worker thread. The lambda only reads atomics + the export snapshot.
    auto laneIdxCell = std::make_shared<std::atomic<int>>(laneIdx);
    laneIndexCells_[lane.get()] = laneIdxCell;
    lane->getDragWavArea().onCanRender = [this, lanePtr]()
    {
        if (auto* m = midiService()) pushLanesToEngine(*m);
        return !lanePtr->getSlots().empty();
    };
    lane->getDragWavArea().onRenderPattern = [laneIdxCell, target = renderTarget_](double& outSampleRate,
        std::function<void(float)> progressCb) -> std::unique_ptr<juce::AudioBuffer<float>>
    {
        auto* svc = target->svc.load();
        const int idx = laneIdxCell->load();
        if (svc == nullptr || idx < 0 || idx >= samplar::engine::kMaxLanes) return nullptr;
        return svc->renderSequencerOffline(1u << idx, outSampleRate, std::move(progressCb));
    };
    lane->onStemHovered = [this](int stemIdx)
    {
        // Highlight the corresponding stem waveform
        for (int i = 0; i < static_cast<int>(stemWaveforms_.size()); ++i)
        {
            float alpha = (stemIdx < 0 || i == stemIdx) ? 1.0f : 0.3f;
            stemWaveforms_[static_cast<size_t>(i)]->setAlpha(alpha);
        }
    };
    lane->onPlayRequested = [this](int idx)
    {
        playSequencerLane(idx);

        // Lock spacebar to THIS lane: pressing space stops all sequencer
        // playback if anything is playing, otherwise replays this lane.
        spaceKeyAction_ = [this, idx]()
        {
            if (idx < 0 || idx >= (int) sequencerLanes_.size()) return;
            if (seqPlayingFlag_ && seqPlayingFlag_->load())
                stopAllSequencerPlayback();
            else
                playSequencerLane(idx);
        };
        juce::Logger::writeToLog("[SModePage] spaceKeyAction_ -> sequencer-lane " + juce::String(idx));
    };
    lane->onStopRequested = [this](int /*idx*/)
    {
        stopAllSequencerPlayback();
    };
    lane->onSlotClicked = [this](int stemIdx, int segIdx)
    {
        // Click on sequencer slot = stop everything + play this segment
        if (!smodeService_) return;
        auto& mdl = smodeService_->getModel();
        auto* midiSvc = midiService();
        if (!midiSvc) return;

        stopLoop();
        if (transportPlaying_) stopTransport();
        midiSvc->stopAllVoices();

        auto* stem = mdl.getStem(stemIdx);
        auto* seg = mdl.getSegment(stemIdx, segIdx);
        if (!stem || !seg || seg->getLengthSamples() <= 0 || stem->mute) return;

        // Same engine path as every other playback (stem pitch, fades).
        midiSvc->playSegment(stemIdx, segIdx, false);
        juce::Logger::writeToLog("[SModePage] sequencer slot click: play stem=" + juce::String(stemIdx)
                                 + " seg=" + juce::String(segIdx));
    };

    seqContent_.addAndMakeVisible(lane.get());
    sequencerLanes_.push_back(std::move(lane));

    juce::Logger::writeToLog("[SModePage] Added sequencer lane " + juce::String(laneIdx));
    seqCollapsed_ = false;            // a new lane must be visible
    updateSeqCollapseButton();
    resized();
    seqViewport_.setViewPosition(0, seqContent_.getHeight());   // scroll the new lane into view
    repaint();
}

void SModePage::removeSequencerLane(int laneIndex)
{
    if (laneIndex < 0 || laneIndex >= static_cast<int>(sequencerLanes_.size())) return;

    seqContent_.removeChildComponent(sequencerLanes_[static_cast<size_t>(laneIndex)].get());
    sequencerLanes_.erase(sequencerLanes_.begin() + laneIndex);

    // Re-index remaining lanes
    for (int i = 0; i < static_cast<int>(sequencerLanes_.size()); ++i)
    {
        // Lane index is stored internally — we don't expose a setter,
        // but the callbacks capture 'this' so they still work.
    }

    juce::Logger::writeToLog("[SModePage] Removed sequencer lane " + juce::String(laneIndex));
    resized();
    repaint();
}

void SModePage::playSequencerLane(int laneIndex)
{
    if (laneIndex < 0 || laneIndex >= static_cast<int>(sequencerLanes_.size())) return;
    if (!smodeService_) return;
    auto* midiSvc = midiService();
    if (!midiSvc) return;

    if (transportPlaying_) stopTransport();
    stopLoop();
    midiSvc->stopAllVoices();

    auto* lane = sequencerLanes_[static_cast<size_t>(laneIndex)].get();
    lane->setPlayingSlot(-1);
    if (lane->getSlots().empty()) return;

    // B36: the clock runs on the audio thread - sample-accurate, synced to the
    // DAW transport while it plays, free-running at the page BPM otherwise.
    pushLanesToEngine(*midiSvc);
    midiSvc->startSequencer(1u << laneIndex);

    if (seqPlayingFlag_) seqPlayingFlag_->store(false);
    seqPlayingFlag_ = std::make_shared<std::atomic<bool>>(true);
    activeSeqLane_ = laneIndex;

    juce::Logger::writeToLog("[SModePage] Playing seq lane " + juce::String(laneIndex)
                             + " (" + juce::String((int) lane->getSlots().size()) + " slots, audio-thread clock)");
}
// ── Smart Fill ────────────────────────────────────────────────────────────
void SModePage::smartFill()
{
    if (!smodeService_) return;
    auto& model = smodeService_->getModel();
    int stemCount = model.getStemCount();
    if (stemCount == 0) return;

    // Smart Fill: use the dot/ball system.
    // If no lanes exist, create one per stem with only that stem's dot active.
    // If lanes exist, re-fill each lane using its active dots (shuffle).
    bool needCreate = sequencerLanes_.empty();
    seqCollapsed_ = false;   // Smart Fill result must be visible
    updateSeqCollapseButton();

    if (needCreate)
    {
        int lanesToCreate = juce::jmin(stemCount, 4);
        for (int s = 0; s < lanesToCreate; ++s)
        {
            auto* stem = model.getStem(s);
            if (!stem) continue;
            addSequencerLane();
            auto* lane = sequencerLanes_.back().get();
            for (int d = 0; d < stemCount; ++d)
                lane->setStemDotActive(d, d == s);
            lane->shuffle();
        }
    }
    else
    {
        for (auto& lane : sequencerLanes_)
            lane->shuffle();
    }

    resized();
    repaint();
}

// ── Play All Lanes Simultaneous (layered) ────────────────────────────────
void SModePage::playAllLanesSimultaneous()
{
    if (sequencerLanes_.empty() || !smodeService_) return;
    auto* midiSvc = midiService();
    if (!midiSvc) return;

    if (transportPlaying_) stopTransport();
    stopLoop();
    midiSvc->stopAllVoices();

    // Every lane on the audio-thread clock; each lane loops on its own length,
    // muted lanes stay silent.
    pushLanesToEngine(*midiSvc);
    const int n = juce::jmin(static_cast<int>(sequencerLanes_.size()), samplar::engine::kMaxLanes);
    const uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    midiSvc->startSequencer(mask);

    if (seqPlayingFlag_) seqPlayingFlag_->store(false);
    seqPlayingFlag_ = std::make_shared<std::atomic<bool>>(true);
    activeSeqLane_ = -2;  // -2 = Play All mode

    juce::Logger::writeToLog("[SModePage] Playing all " + juce::String(n) + " lanes simultaneously (audio-thread clock)");
}

// ── Stop All Sequencer Playback ──────────────────────────────────────────
void SModePage::stopAllSequencerPlayback()
{
    if (auto* midiSvc = midiService())
        midiSvc->stopSequencer();

    resetSequencerUi();
    stopLoop();   // releases MIDI / audition voices too
    if (transportPlaying_) stopTransport();

    juce::Logger::writeToLog("[SModePage] Stopped all sequencer playback");
}

void SModePage::resetSequencerUi()
{
    if (seqPlayingFlag_) seqPlayingFlag_->store(false);
    seqPlayingFlag_.reset();
    activeSeqLane_ = -1;

    playAllSeqBtn_.setButtonText("Play All");
    playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));

    for (auto& lane : sequencerLanes_)
    {
        lane->setPlayingSlot(-1);
        lane->setPlayheadPosition(-1.0f);
    }
}

samplar::MidiMappingService* SModePage::midiService() const
{
    // This instance's registry (review A2), captured at construction.
    return registry_ != nullptr ? dynamic_cast<samplar::MidiMappingService*>(registry_->get("midi.mapping")) : nullptr;
}

void SModePage::pushLanesToEngine(samplar::MidiMappingService& midiSvc)
{
    std::vector<samplar::MidiMappingService::LaneInput> lanes;
    lanes.reserve(sequencerLanes_.size());
    std::map<const SequencerLaneComponent*, std::shared_ptr<std::atomic<int>>> liveCells;
    for (int i = 0; i < static_cast<int>(sequencerLanes_.size()); ++i)
    {
        const auto* lane = sequencerLanes_[static_cast<size_t>(i)].get();
        if (lane == nullptr) continue;
        samplar::MidiMappingService::LaneInput in;
        in.slots   = lane->getSlots();
        in.numBars = lane->getNumBars();
        in.cellsPerBeat = lane->getCellsPerBeat();   // B18: x8 / x16 lanes
        in.muted   = lane->isMuted();
        lanes.push_back(std::move(in));

        auto it = laneIndexCells_.find(lane);
        if (it != laneIndexCells_.end())
        {
            it->second->store(static_cast<int>(lanes.size()) - 1);
            liveCells.insert(*it);
        }
    }
    laneIndexCells_.swap(liveCells);   // forget removed lanes

    midiSvc.setSequencerBpm(detectedBpm_ > 0.0 ? detectedBpm_ : 120.0);
    midiSvc.setSequencerLanes(lanes);   // no-op when nothing changed

    const int n = juce::jmin(static_cast<int>(lanes.size()), samplar::engine::kMaxLanes);
    renderTarget_->svc.store(&midiSvc);
    renderTarget_->allMask.store(n <= 0 ? 0u : ((1u << n) - 1u));
}

void SModePage::syncSequencerUi(samplar::MidiMappingService& midiSvc)
{
    pushLanesToEngine(midiSvc);

    const bool running = midiSvc.isSequencerRunning();
    if (seqPlayingFlag_ && seqPlayingFlag_->load() && !running)
    {
        // The engine stopped it (DAW transport stopped / bypass / all sound off).
        juce::Logger::writeToLog("[SModePage] sequencer stopped by the engine - resetting lane UI");
        resetSequencerUi();
        return;
    }
    if (!running) return;

    for (int i = 0; i < static_cast<int>(sequencerLanes_.size()); ++i)
    {
        int slot = -1;
        float progress = 0.0f, head = -1.0f;
        midiSvc.getSequencerLaneState(i, slot, progress, head);
        auto& lane = *sequencerLanes_[static_cast<size_t>(i)];
        lane.setPlayingSlot(slot, progress);
        lane.setPlayheadPosition(head);
    }
}

// ── Undo/Redo helpers ──────────────────────────────────────────────────────

void SModePage::updateUndoUi()
{
    if (!smodeService_)
    {
        undoBtn_.setEnabled(false);
        redoBtn_.setEnabled(false);
        undoBtn_.setTooltip("Nothing to undo");
        redoBtn_.setTooltip("Nothing to redo");
        return;
    }
    auto& model = smodeService_->getModel();

    const bool canU = model.canUndo();
    const bool canR = model.canRedo();
    undoBtn_.setEnabled(canU);
    redoBtn_.setEnabled(canR);

    undoBtn_.setTooltip(canU
        ? ("Undo: " + model.peekUndoLabel() + " (Ctrl+Z)")
        : juce::String("Nothing to undo"));
    redoBtn_.setTooltip(canR
        ? ("Redo: " + model.peekRedoLabel() + " (Ctrl+Y)")
        : juce::String("Nothing to redo"));
}

void SModePage::flashUndoStatus(const juce::String& text)
{
    undoStatusLabel_.setText(text, juce::dontSendNotification);
    undoStatusLabel_.setVisible(true);
    // Auto-hide after 1.2s
    juce::Timer::callAfterDelay(1200, [safeThis = juce::Component::SafePointer<SModePage>(this)]()
    {
        if (safeThis != nullptr)
        {
            safeThis->undoStatusLabel_.setVisible(false);
            safeThis->undoStatusLabel_.setText({}, juce::dontSendNotification);
        }
    });
}

juce::var SModePage::captureUiExtraState() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("v", 2);
    obj->setProperty("gridLocked", gridLocked_);
    obj->setProperty("lockedBpm",  lockedBpm_);
    obj->setProperty("lockSemis",  lockSemis_);
    obj->setProperty("displayBpm", detectedBpm_);
    obj->setProperty("gridDivision", currentGridDivision_);
    obj->setProperty("loopEnabled", loopBtn_.getToggleState());
    obj->setProperty("linkAll", linkAllActive_);
    if (smodeService_ != nullptr)
        obj->setProperty("gridOffset", smodeService_->getModel().getGridOffsetSeconds());

    juce::Array<juce::var> removed;   // B23: "Remove Channel" is undoable
    for (int r : removedStems_) removed.add(r);
    obj->setProperty("removed", removed);

    juce::Array<juce::var> laneArr;
    for (const auto& lane : sequencerLanes_)
        if (lane) laneArr.add(lane->captureState());
    obj->setProperty("lanes", laneArr);
    return juce::var(obj);
}

void SModePage::restoreUiExtraState(const juce::var& v)
{
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return;
    juce::Logger::writeToLog("[SModePage] restoreUiExtraState (undo / redo)");

    // Tolerant reader (N-18): the JSON comes from project files; "1e999" parses
    // to inf. Non-finite / out-of-range values fall back instead of reaching
    // the engine or the grid.
    auto bpmOrZero = [](double b) { return std::isfinite(b) && b > 0.0 ? juce::jlimit(20.0, 999.0, b) : 0.0; };
    gridLocked_          = (bool)   obj->getProperty("gridLocked");
    lockedBpm_           = bpmOrZero((double) obj->getProperty("lockedBpm"));
    lockSemis_           = obj->hasProperty("lockSemis") ? juce::jlimit(-24, 24, (int) obj->getProperty("lockSemis")) : 0;
    if (lockedBpm_ <= 0.0) lockSemis_ = 0;
    if (obj->hasProperty("displayBpm")) detectedBpm_ = bpmOrZero((double) obj->getProperty("displayBpm"));
    juce::Logger::writeToLog("[SModePage]   restored gridLocked=" + juce::String(gridLocked_ ? 1 : 0) + " lockedBpm="
                             + juce::String(lockedBpm_, 2) + " lockSemis=" + juce::String(lockSemis_));
    bool loopEnabled     = (bool)   obj->getProperty("loopEnabled");
    linkAllActive_       = (bool)   obj->getProperty("linkAll");

    // B24: every control mirrors the restored state, same look as its click handler.
    loopBtn_.setToggleState(loopEnabled, juce::dontSendNotification);
    if (auto* m = midiService()) m->setSModeTransportLooping(loopEnabled);
    updateGridLockButton();
    applyLockTranspose();
    updateLinkAllButton();

    const int division = (int) obj->getProperty("gridDivision");
    if (division == 1 || division == 2 || division == 4 || division == 8 || division == 16)
    {
        currentGridDivision_ = division;
        divisionCombo_.setSelectedId(division, juce::dontSendNotification);
        for (auto& wf : stemWaveforms_)
            if (wf) wf->setGridDivision(division);
    }
    if (obj->hasProperty("gridOffset"))
    {
        const double off = (double) obj->getProperty("gridOffset");
        if (std::isfinite(off))
            setGlobalGridOffset(juce::jlimit(0.0, 3600.0, off));
        else
            juce::Logger::writeToLog("[SModePage]   gridOffset not finite - ignored");
    }

    removedStems_.clear();
    if (auto* arr = obj->getProperty("removed").getArray())
        for (const auto& r : *arr)
            if (const int s = (int) r; s >= 0 && s < samplar::MidiMappingService::MAX_STEMS)
                removedStems_.insert(s);
    applyRemovedStemVisibility();

    // Restore lanes — size match first, then delegate to each lane
    if (auto* laneArr = obj->getProperty("lanes").getArray())
    {
        const int target = laneArr->size();
        while (static_cast<int>(sequencerLanes_.size()) < target)
        {
            const auto before = sequencerLanes_.size();
            addSequencerLane();
            if (sequencerLanes_.size() == before) break;
        }
        while (static_cast<int>(sequencerLanes_.size()) > target)
            removeSequencerLane(static_cast<int>(sequencerLanes_.size()) - 1);

        for (int i = 0; i < target; ++i)
            if (i < static_cast<int>(sequencerLanes_.size()) && sequencerLanes_[(size_t) i])
                sequencerLanes_[(size_t) i]->restoreState((*laneArr)[i]);
    }

    // Any layout-affecting changes mean a resized pass
    resized();
    repaint();
}

void SModePage::refreshAfterUndoRedo()
{
    if (smodeService_ == nullptr) return;
    auto& model = smodeService_->getModel();
    // The refresh must not push undo entries (that would wipe the redo stack).
    model.beginSuppressPush();
    if (! gridLocked_ && model.getDetectedTempo() > 0.0)
        applyBpm(model.getDetectedTempo(), false);
    else if (detectedBpm_ > 0.0)
        bpmInput_.setText(juce::String(detectedBpm_, 2), juce::dontSendNotification);
    model.endSuppressPush();
    refreshLaneButtons();
    for (auto& wf : stemWaveforms_) wf->repaint();
    lastAutoChopValid_ = false;   // never re-chop over an undone / redone state
    refreshInspector();
    juce::Logger::writeToLog("[SModePage] UI refreshed after undo/redo (linkAll=" + juce::String(linkAllActive_ ? 1 : 0)
                             + " lock=" + juce::String(gridLocked_ ? 1 : 0) + " semis=" + juce::String(lockSemis_)
                             + " removed=" + juce::String((int) removedStems_.size()) + ")");
}

// -- Small UI sync helpers ----------------------------------------------------

void SModePage::updateLinkAllButton()
{
    linkAllBtn_.setToggleState(linkAllActive_, juce::dontSendNotification);
    linkAllBtn_.setColour(juce::TextButton::buttonColourId,
                          linkAllActive_ ? juce::Colour(0xff2d6d3a) : juce::Colour(0xff2d2d3a));
    linkAllBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2d6d3a));
}

void SModePage::updateMonoPolyLabel()
{
    auto* m = midiService();
    const bool mono = m != nullptr && m->isMonophonic();
    const bool gate = m != nullptr && m->isSampleGateMode();
    monoPolyBtn_.setButtonText(juce::String(mono ? "MONO" : "POLY") + " " + juce::String(gate ? "GATE" : "1SH"));
}

void SModePage::refreshLaneButtons()
{
    for (auto& w : stemWaveforms_)
        if (w) { w->refreshStemButtons(); w->refreshLinkButton(); }
}

void SModePage::applyRemovedStemVisibility()
{
    for (int st = 0; st < static_cast<int>(stemWaveforms_.size()); ++st)
    {
        const bool removed = removedStems_.count(st) > 0;
        const bool show = ! removed && (focusedStem_ < 0 || focusedStem_ == st);
        stemWaveforms_[(size_t) st]->setVisible(show);
    }
    if (focusedStem_ >= 0 && removedStems_.count(focusedStem_) > 0)
    {
        focusedStem_ = -1;
        applyRemovedStemVisibility();
        return;
    }
    resized();
}

void SModePage::setGlobalGridOffset(double seconds)
{
    if (smodeService_ != nullptr)
        smodeService_->getModel().setGridOffsetSeconds(seconds);
    for (auto& wf : stemWaveforms_)
        if (wf) wf->setGridOffsetSeconds(seconds);
}

std::vector<std::pair<int, int>> SModePage::linkedSegmentsAt(int stemIdx, int segIdx) const
{
    std::vector<std::pair<int, int>> out;
    if (smodeService_ == nullptr) return out;
    const auto& model = smodeService_->getModel();
    if (! (linkAllActive_ || model.isLinked(stemIdx))) return out;
    const auto* src = model.getSegment(stemIdx, segIdx);
    if (src == nullptr) return out;
    const int64_t mid = (src->startSample + src->endSample) / 2;
    for (int j = 0; j < model.getStemCount(); ++j)
    {
        if (j == stemIdx || ! (linkAllActive_ || model.isLinked(j))) continue;
        if (removedStems_.count(j) > 0) continue;
        const auto* st = model.getStem(j);
        if (st == nullptr) continue;
        for (int k = 0; k < (int) st->segments.size(); ++k)
            if (st->segments[(size_t) k].startSample <= mid && mid < st->segments[(size_t) k].endSample)
            { out.push_back({ j, k }); break; }
    }
    return out;
}

// -- Key detection (melody + bass, worker pool) -----------------------------

void SModePage::setKeyText(const juce::String& text, const juce::String& tooltip)
{
    keyLabel_.setText(text, juce::dontSendNotification);
    keyLabel_.setTooltip(tooltip);
}

void SModePage::runKeyDetection()
{
    if (smodeService_ == nullptr) return;
    auto& model = smodeService_->getModel();
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> parts;
    juce::StringArray used;
    for (int i = 0; i < model.getStemCount(); ++i)
    {
        auto* st = model.getStem(i);
        if (st == nullptr || st->audioBuffer.getNumSamples() == 0) continue;
        const auto n = st->name.toLowerCase();
        if (n.contains("melody") || n.contains("other") || n.contains("bass") || n.contains("piano") || n.contains("guitar"))
        {
            parts.push_back(model.getSharedStemAudio(i));   // shared, no copy (B33)
            used.add(st->name);
        }
    }
    if (parts.empty())
    {
        setKeyText("--", "Musical key: no Melody / Bass stem");
        return;
    }
    setKeyText("...", "Musical key: detecting on " + used.joinIntoString(" + "));
    const double sr = model.getSampleRate() > 0.0 ? model.getSampleRate() : 44100.0;
    const uint64_t generation = model.getContentGeneration();
    auto safeThis = juce::Component::SafePointer<SModePage>(this);
    juce::Logger::writeToLog("[SModePage] key detection queued on: " + used.joinIntoString(" + "));
    const std::atomic<bool>* cancelFlag = smodeService_->getJobCancelFlag();   // set on unload (A4)
    const bool queued = smodeService_->runBackground([safeThis, parts, sr, generation, used, cancelFlag]()
    {
        if (cancelFlag->load()) return;
        int len = 0;
        for (const auto& b : parts) if (b != nullptr) len = juce::jmax(len, b->getNumSamples());
        juce::AudioBuffer<float> mono(1, juce::jmax(1, len));
        mono.clear();
        for (const auto& b : parts)
        {
            if (b == nullptr) continue;
            const float g = 1.0f / (float) juce::jmax(1, b->getNumChannels());
            for (int ch = 0; ch < b->getNumChannels(); ++ch)
                mono.addFrom(0, 0, *b, ch, 0, b->getNumSamples(), g);
        }
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const auto res = samplar::KeyDetector::detect(mono, sr, samplar::AnalysisCancel(cancelFlag));
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        if (res.cancelled) return;   // unloading
        juce::MessageManager::callAsync([safeThis, res, generation, used, ms]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr || self->smodeService_ == nullptr) return;
            juce::Logger::writeToLog("[SModePage] KeyDetector: " + res.getName() + " (" + res.getCamelot() + ") conf="
                                     + juce::String(res.confidence, 2) + " in " + juce::String(ms, 0) + " ms");
            if (generation != self->smodeService_->getModel().getContentGeneration())
            {
                juce::Logger::writeToLog("[SModePage] key result dropped (song changed)");
                return;
            }
            if (! res.valid)
            {
                self->setKeyText("--", "Musical key: not detected (no tonal content in " + used.joinIntoString(" + ") + ")");
                return;
            }
            const auto shortName = samplar::KeyDetector::pitchClassName(res.tonic)
                                 + (res.mode == samplar::KeyDetector::Mode::Minor ? "m" : "");
            self->setKeyText(shortName + "  " + res.getCamelot(),
                             "Key: " + res.getName() + " (Camelot " + res.getCamelot() + "), confidence "
                             + juce::String(juce::roundToInt(res.confidence * 100.0)) + "% - from "
                             + used.joinIntoString(" + "));
        });
    });
    if (! queued) setKeyText("--", "Musical key: analysis unavailable");
}

// -- Chop inspector -------------------------------------------------------------

int SModePage::findInspectorSegment() const
{
    if (smodeService_ == nullptr || inspStem_ < 0) return -1;
    const auto* st = smodeService_->getModel().getStem(inspStem_);
    if (st == nullptr) return -1;
    auto same = [this](const Segment& sg) { return sg.startSample == inspStart_ && sg.endSample == inspEnd_; };
    if (inspSeg_ >= 0 && inspSeg_ < (int) st->segments.size() && same(st->segments[(size_t) inspSeg_]))
        return inspSeg_;
    for (int k = 0; k < (int) st->segments.size(); ++k)
        if (same(st->segments[(size_t) k])) return k;
    // Being resized right now: same index, still selected AND still overlapping
    // the range we inspected. A different chop that moved into this index after
    // a delete does not overlap it (N-24: the inspector never jumps to it).
    if (inspSeg_ >= 0 && inspSeg_ < (int) st->segments.size())
    {
        const auto& sg = st->segments[(size_t) inspSeg_];
        if (sg.selected && sg.startSample < inspEnd_ && sg.endSample > inspStart_)
            return inspSeg_;
    }
    return -1;
}

void SModePage::selectChopForInspector(int stemIdx, int segIdx)
{
    const Segment* sg = (smodeService_ != nullptr && stemIdx >= 0) ? smodeService_->getModel().getSegment(stemIdx, segIdx) : nullptr;
    if (sg == nullptr)
    {
        inspStem_ = inspSeg_ = -1;
        if (chopInspector_.hasTarget()) chopInspector_.clearTarget();
        return;
    }
    inspStem_ = stemIdx;
    inspSeg_ = segIdx;
    inspStart_ = sg->startSample;
    inspEnd_ = sg->endSample;
    juce::Logger::writeToLog("[SModePage] inspector -> stem=" + juce::String(stemIdx) + " seg=" + juce::String(segIdx));
    refreshInspector();
}

void SModePage::refreshInspector()
{
    const int seg = findInspectorSegment();
    if (seg < 0)
    {
        if (inspStem_ >= 0)
            juce::Logger::writeToLog("[SModePage] inspector target gone (deleted / undone) - cleared");
        inspStem_ = inspSeg_ = -1;
        if (chopInspector_.hasTarget()) chopInspector_.clearTarget();
        return;
    }
    const auto& model = smodeService_->getModel();
    const auto* sg = model.getSegment(inspStem_, seg);
    const auto* st = model.getStem(inspStem_);
    inspSeg_ = seg;
    inspStart_ = sg->startSample;
    inspEnd_ = sg->endSample;

    const auto& fx = sg->effects;
    ChopInspectorStrip::Values v;
    v.pitchSemis = juce::jlimit(-24, 24, fx.pitchSemitones);
    v.gainDb     = ChopInspectorStrip::linearToGainDb(fx.volume);
    v.pan        = juce::jlimit(-1.0f, 1.0f, fx.pan);
    v.reverse    = fx.reverse;
    v.trigger    = samplar::engine::getTriggerMode(fx);
    v.attackMs   = samplar::engine::getAttackMs(fx);
    v.releaseMs  = samplar::engine::getReleaseMs(fx);
    const juce::String title = (st != nullptr ? st->name : juce::String("Stem")) + " #" + juce::String(seg + 1)
                             + (sg->midiNote >= 0 ? "  " + samplar::MidiMappingService::noteNumberToName(sg->midiNote) : juce::String());

    const auto& cur = chopInspector_.getValues();
    const bool same = chopInspector_.hasTarget() && cur.pitchSemis == v.pitchSemis
                   && std::abs(cur.gainDb - v.gainDb) < 0.05f && std::abs(cur.pan - v.pan) < 0.005f
                   && cur.reverse == v.reverse && cur.trigger == v.trigger
                   && std::abs(cur.attackMs - v.attackMs) < 0.5f && std::abs(cur.releaseMs - v.releaseMs) < 0.5f;
    if (! same || lastInspectorTitle_ != title)
    {
        lastInspectorTitle_ = title;
        chopInspector_.setTarget(title, v, samplar::engine::segmentFxHasEnvelope(),
                                 samplar::engine::segmentFxHasTriggerMode());
    }
}

// ============ FileDragAndDropTarget ============

bool SModePage::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" ||
            ext == ".aiff" || ext == ".aif" || ext == ".ogg")
            return true;
    }
    return false;
}

void SModePage::fileDragEnter(const juce::StringArray& /*files*/, int /*x*/, int /*y*/)
{
    dragHighlight_ = true;
    repaint();
}

void SModePage::fileDragExit(const juce::StringArray& /*files*/)
{
    dragHighlight_ = false;
    repaint();
}

void SModePage::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    dragHighlight_ = false;
    repaint();
    if (files.isEmpty()) return;
    juce::File f(files[0]);
    juce::Logger::writeToLog("[SModePage] File dropped: " + f.getFullPathName());
    if (f.existsAsFile() && LoadAudioOverlay::isAcceptedAudioFile(f.getFullPathName()))
        requestLoadFile(f, "page drop");   // B28: asks before replacing a loaded session
}


// -- Instance services / DAW sync / in-page notices (integration) -------------

SModeService* SModePage::serviceFromContext() const
{
    return registry_ != nullptr ? dynamic_cast<SModeService*>(registry_->get("smode")) : nullptr;
}

void SModePage::setDawSync(bool on)
{
    // U-H2: the page's own bus (this instance's); the destructor unsubscribes
    // from the same bus. The flag lives in this instance's engine front end,
    // so it survives editor close and is saved with the project (schema 2).
    if (auto* midiSvc = midiService())
        midiSvc->setDawTempoSync(on);
    dawSyncBtn_.setToggleState(on, juce::dontSendNotification);

    if (hostTempoSub_ != 0)
    {
        bus_.unsubscribe("host.tempo", hostTempoSub_);
        hostTempoSub_ = 0;
    }
    if (on)
    {
        // Subscribe FIRST so the immediate re-publish below is captured.
        hostTempoSub_ = bus_.subscribe("host.tempo", [this](const juce::var& v) {
            const double hostBpm = static_cast<double>(v);
            juce::Logger::writeToLog("[SModePage] host.tempo received: " + juce::String(hostBpm, 3));
            if (hostBpm <= 0.0) return;
            // Same path as the BPM field, but a tempo change from the host is
            // not an undoable user edit (B24).
            applyBpm(hostBpm, false);
        });
        // Ask the audio thread to (re-)publish the current host BPM right now.
        bus_.publish("host.tempo.request");
    }
    juce::Logger::writeToLog("[SModePage] DAW tempo sync " + juce::String(on ? "ON (host.tempo subscribed, re-publish requested)"
                                                                          : "OFF"));
}

void SModePage::showMissingAudioNotice(const juce::String& message)
{
    juce::Logger::writeToLog("[SModePage] notice: missing audio - " + message);
    // Drop placeholder "loading" lanes of the failed restore: the page shows
    // the empty state (drop / LOAD still work) with the notice above it.
    if (! audioLoaded_)
    {
        for (auto& w : stemWaveforms_)
            if (w) removeChildComponent(w.get());
        stemWaveforms_.clear();
        loadAudioOverlay_.setVisible(true);
        loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
    }
    noticeBanner_.setNotice(SModeNoticeBanner::Kind::MissingAudio,
                            message.isNotEmpty() ? message
                                                 : juce::String("Audio missing: this project's song and stems were not found. Your chops are kept."),
                            "Locate song...");
    noticeBanner_.setVisible(true);
    resized();
    repaint();
}

void SModePage::showSeparationUnavailableNotice(const juce::String& detail)
{
    juce::Logger::writeToLog("[SModePage] notice: separation unavailable" + (detail.isNotEmpty() ? " - " + detail : juce::String()));
    noticeBanner_.setNotice(SModeNoticeBanner::Kind::SeparationUnavailable,
                            "Separation unavailable - please reinstall SAMPLAR."
                                + (detail.isNotEmpty() ? " (" + detail + ")" : juce::String()),
                            {});
    noticeBanner_.setVisible(true);
    resized();
    repaint();
}

void SModePage::showSignInRequiredNotice()
{
    juce::Logger::writeToLog("[SModePage] notice: sign-in required (licence kept, plays offline)");
    noticeBanner_.setNotice(SModeNoticeBanner::Kind::SignInRequired,
                            "Your SAMPLAR sign-in has expired. SAMPLAR keeps working offline for now - "
                            "sign in again to keep your licence verified.",
                            "Sign in again");
    noticeBanner_.setVisible(true);
    resized();
    repaint();
}

void SModePage::updateSignInNotice()
{
    auto* lic = registry_ != nullptr ? dynamic_cast<LicenseSystemService*>(registry_->get("license")) : nullptr;
    // Only while Valid: otherwise the editor's sign-in gate already covers the UI.
    const bool required = lic != nullptr
                          && lic->getCurrentStatus() == LicenseSystemService::LicenseStatus::Valid
                          && lic->isSignInRequired();
    const bool showing = noticeBanner_.isVisible()
                         && noticeBanner_.getKind() == SModeNoticeBanner::Kind::SignInRequired;
    if (! required)
    {
        signInNoticeDismissed_ = false;
        if (showing)
        {
            juce::Logger::writeToLog("[SModePage] sign-in requirement cleared -> notice hidden");
            hideNotice();
        }
        return;
    }
    if (showing || signInNoticeDismissed_)
        return;
    if (noticeBanner_.isVisible())
    {
        juce::Logger::writeToLog("[SModePage] sign-in required, but another notice is showing - kept for later");
        return;
    }
    showSignInRequiredNotice();
}

void SModePage::openSignInPanel()
{
    auto* lic = registry_ != nullptr ? dynamic_cast<LicenseSystemService*>(registry_->get("license")) : nullptr;
    if (lic == nullptr)
    {
        juce::Logger::writeToLog("[SModePage] Sign in again: licence service missing - ignored");
        return;
    }
    juce::Logger::writeToLog("[SModePage] Sign in again clicked -> in-window browser sign-in panel");
    juce::Component::SafePointer<SModePage> safe(this);
    LicenseActivationDialog::showInWindow(this, *lic, bus_, true, [safe](int result)
    {
        juce::Logger::writeToLog("[SModePage] sign-in panel closed (result " + juce::String(result) + ")");
        if (safe != nullptr)
            safe->updateSignInNotice();
    });
}

void SModePage::hideNotice()
{
    if (! noticeBanner_.isVisible()) return;
    juce::Logger::writeToLog("[SModePage] notice hidden");
    noticeBanner_.setVisible(false);
    resized();
    repaint();
    // A pending sign-in requirement comes back once the strip is free (unless
    // the user dismissed it; no recursion: it only hides when not required).
    updateSignInNotice();
}

void SModePage::locateMissingSong()
{
    auto* svc = serviceFromContext();
    juce::File hint = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    juce::String wanted;
    if (svc != nullptr && svc->getSourceFile() != juce::File())
    {
        wanted = svc->getSourceFile().getFileName();
        if (svc->getSourceFile().getParentDirectory().isDirectory())
            hint = svc->getSourceFile().getParentDirectory();
    }
    juce::Logger::writeToLog("[SModePage] locateMissingSong: looking for '" + wanted + "' from " + hint.getFullPathName());
    auto chooser = std::make_shared<juce::FileChooser>(
        wanted.isNotEmpty() ? "Locate \"" + wanted + "\"" : juce::String("Locate the song of this project"),
        hint, "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg");
    juce::Component::SafePointer<SModePage> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, chooser](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto results = fc.getResults();
            if (results.isEmpty())
            {
                juce::Logger::writeToLog("[SModePage] locateMissingSong: cancelled");
                return;
            }
            const auto song = results[0];
            auto* service = safe->serviceFromContext();
            const bool ok = service != nullptr && service->relinkMissingSource(song);
            juce::Logger::writeToLog("[SModePage] locateMissingSong: " + song.getFullPathName()
                                     + (ok ? " -> relinking (re-separating, chops kept)" : " -> relink refused"));
            if (ok)
            {
                samplar::RecentSongs::push(song);
                safe->hideNotice();
            }
        });
}


// ============================================================================
// MIDI CC learn UI (right-click menu on CC-capable controls)
// ============================================================================

void SModePage::showMidiLearnMenu(const juce::String& target, const juce::String& label, juce::Component& anchor)
{
    auto* midiSvc = midiService();
    juce::Logger::writeToLog("[SModePage] MIDI menu: target=" + target + " label=" + label
                             + " service=" + (midiSvc != nullptr ? "ok" : "null"));
    if (midiSvc == nullptr) return;

    const auto bound = midiSvc->describeCcBindings(target);
    const bool armed = midiSvc->getCcLearnTarget() == target;
    const juce::String learnText = armed ? "MIDI Learn (move a control...)" : "MIDI Learn";
    const juce::String clearText = bound.isEmpty() ? juce::String("Clear MIDI") : "Clear MIDI (" + bound + ")";
    juce::PopupMenu m;
    m.addSectionHeader("MIDI: " + label);
    m.addItem(1, learnText, ! armed, armed);
    if (armed)
        m.addItem(3, "Cancel MIDI Learn");
    m.addItem(2, clearText, bound.isNotEmpty());

    lastMidiMenuItems_.clear();
    lastMidiMenuItems_.add(learnText);
    if (armed) lastMidiMenuItems_.add("Cancel MIDI Learn");
    lastMidiMenuItems_.add(clearText + (bound.isEmpty() ? " [disabled]" : ""));

    juce::Component::SafePointer<SModePage> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor).withDeletionCheck(*this),
        [safe, target, label](int result)
        {
            if (safe == nullptr || result == 0) return;
            juce::Logger::writeToLog("[SModePage] MIDI menu result=" + juce::String(result) + " target=" + target);
            if (result == 1) safe->startMidiLearn(target, label);
            else if (result == 2) safe->clearMidiLearn(target, label);
            else if (result == 3)
            {
                if (auto* svc = safe->midiService()) svc->endCcLearn();
                safe->pendingLearnTarget_ = {};
                safe->flashUndoStatus("MIDI Learn cancelled");
            }
        });
}

void SModePage::startMidiLearn(const juce::String& target, const juce::String& label)
{
    auto* midiSvc = midiService();
    if (midiSvc == nullptr) return;
    // Stem pitch is built into the service (no UI callback, works with the
    // editor closed); the inspector targets were registered in the ctor.
    midiSvc->beginCcLearn(target, nullptr);
    pendingLearnTarget_ = target;
    pendingLearnLabel_  = label;
    juce::Logger::writeToLog("[SModePage] MIDI learn armed: " + target);
    flashUndoStatus("MIDI Learn: move a knob or fader on your MIDI controller (" + label + ")");
}

void SModePage::clearMidiLearn(const juce::String& target, const juce::String& label)
{
    auto* midiSvc = midiService();
    if (midiSvc == nullptr) return;
    const int removed = midiSvc->clearCcBindingsForTarget(target);
    if (pendingLearnTarget_ == target) pendingLearnTarget_ = {};
    juce::Logger::writeToLog("[SModePage] MIDI cleared: " + target + " (" + juce::String(removed) + " binding(s))");
    flashUndoStatus("MIDI cleared: " + label);
    if (auto* app = appState_) app->markDirty();
    refreshMidiLearnInfo();
}

void SModePage::refreshMidiLearnInfo()
{
    auto* midiSvc = midiService();
    if (midiSvc == nullptr) return;
    lastCcGeneration_ = midiSvc->getCcBindingsGeneration();
    for (int i = 0; i < (int) stemWaveforms_.size(); ++i)
        if (stemWaveforms_[(size_t) i])
            stemWaveforms_[(size_t) i]->setPitchMidiInfo(midiSvc->describeCcBindings(stemPitchCcTarget(i)));
    for (const auto& param : ChopInspectorStrip::midiParams())
        chopInspector_.setMidiInfo(param, midiSvc->describeCcBindings(inspectorCcTarget(param)));

    // A learn this page armed has completed (bound) or was cancelled elsewhere.
    if (pendingLearnTarget_.isNotEmpty() && midiSvc->getCcLearnTarget() != pendingLearnTarget_)
    {
        const auto bound = midiSvc->describeCcBindings(pendingLearnTarget_);
        juce::Logger::writeToLog("[SModePage] MIDI learn finished: " + pendingLearnTarget_
                                 + " -> " + (bound.isEmpty() ? juce::String("none") : bound));
        if (bound.isNotEmpty())
        {
            flashUndoStatus("MIDI: " + pendingLearnLabel_ + " <- " + bound);
            if (auto* app = appState_) app->markDirty();
        }
        pendingLearnTarget_ = {};
    }
}

void SModePage::syncStemPitchKnobs()
{
    auto* midiSvc = midiService();
    if (midiSvc == nullptr || ! audioLoaded_) return;
    for (int i = 0; i < (int) stemWaveforms_.size(); ++i)
        if (stemWaveforms_[(size_t) i])
            stemWaveforms_[(size_t) i]->setPitchSemitones(midiSvc->getStemPitchSemitones(i));   // no-op when equal
}

void SModePage::applyInspectorCc(const juce::String& param, float value01)
{
    if (smodeService_ == nullptr || ! chopInspector_.hasTarget() || findInspectorSegment() < 0) return;
    // One undo entry per CC gesture: a pause longer than 1 s starts a new one.
    const auto now = juce::Time::getMillisecondCounter();
    const bool newGesture = lastCcUndoMs_ == 0 || now - lastCcUndoMs_ > 1000;
    if (chopInspector_.setFromController(param, value01, newGesture))
    {
        if (newGesture)
            juce::Logger::writeToLog("[SModePage] MIDI CC edits chop " + juce::String(inspStem_) + ":"
                                     + juce::String(inspSeg_) + " " + param + " (undo step pushed)");
        lastCcUndoMs_ = now;
    }
}

juce::String SModePage::debugMidiSelfTest(const juce::String& step)
{
    // UI self-test (VST3SmokeHost --ui-sweep): runs exactly the code the
    // right-click menu items run. Returns "PASS ..." / "FAIL ..." / "MENU ...".
    auto* midiSvc = midiService();
    if (midiSvc == nullptr) return "FAIL no MidiMappingService";
    juce::Logger::writeToLog("[SModePage] MIDI self-test step: " + step);
    if (step == "menu_pitch" || step == "menu_insp")
    {
        juce::PopupMenu::dismissAllActiveMenus();
        if (step == "menu_pitch")
        {
            if (stemWaveforms_.empty() || ! audioLoaded_ || ! stemWaveforms_[0]->onPitchMidiMenu) return "FAIL no stems";
            stemWaveforms_[0]->onPitchMidiMenu(0);
        }
        else
        {
            auto* c = chopInspector_.getControl("gain");
            if (c == nullptr || ! chopInspector_.hasTarget() || ! chopInspector_.onMidiMenu) return "FAIL inspector has no chop";
            chopInspector_.onMidiMenu("gain", *c);
        }
        return "MENU " + lastMidiMenuItems_.joinIntoString(" | ");
    }
    if (step == "close")
    {
        juce::PopupMenu::dismissAllActiveMenus();
        return "ok";
    }
    if (step == "learn_pitch")
    {
        if (stemWaveforms_.empty()) return "FAIL no stems";
        startMidiLearn(stemPitchCcTarget(0), "self-test pitch");
        midiSvc->debugInjectController(1, 74, 0.75f);           // -> +6 st
        refreshMidiLearnInfo();
        syncStemPitchKnobs();
        const float semi = midiSvc->getStemPitchSemitones(0);
        const float knob = stemWaveforms_[0]->getPitchSemitones();
        const auto info = stemWaveforms_[0]->getPitchMidiInfo();
        const bool ok = std::abs(semi - 6.0f) < 0.01f && std::abs(knob - 6.0f) < 0.01f
                     && info == "CC 74 ch 1" && midiSvc->describeCcBindings(stemPitchCcTarget(0)) == "CC 74 ch 1";
        midiSvc->debugInjectController(1, 74, 0.5f);            // back to 0 st
        syncStemPitchKnobs();
        return (ok ? "PASS" : "FAIL") + juce::String(" pitch learn: semi=") + juce::String(semi, 2)
             + " knob=" + juce::String(knob, 2) + " info=" + info;
    }
    if (step == "learn_insp")
    {
        if (! chopInspector_.hasTarget() || findInspectorSegment() < 0) return "FAIL inspector has no chop";
        startMidiLearn(inspectorCcTarget("gain"), "self-test gain");
        midiSvc->debugInjectController(2, 7, 0.0f);              // gain -> -inf
        refreshMidiLearnInfo();
        const auto fx = smodeService_->getModel().getSegmentEffects(inspStem_, findInspectorSegment());
        const auto info = chopInspector_.getMidiInfo("gain");
        const bool ok = fx.volume <= 0.0001f && info == "CC 7 ch 2";
        midiSvc->debugInjectController(2, 7, 1.0f);              // gain -> +6 dB
        const auto fx2 = smodeService_->getModel().getSegmentEffects(inspStem_, findInspectorSegment());
        const bool ok2 = fx2.volume > 1.9f;
        return ((ok && ok2) ? "PASS" : "FAIL") + juce::String(" inspector learn: vol0=") + juce::String(fx.volume, 3)
             + " vol1=" + juce::String(fx2.volume, 3) + " info=" + info;
    }
    if (step == "clear")
    {
        clearMidiLearn(stemPitchCcTarget(0), "self-test pitch");
        clearMidiLearn(inspectorCcTarget("gain"), "self-test gain");
        const bool ok = stemWaveforms_.empty() || stemWaveforms_[0]->getPitchMidiInfo().isEmpty();
        const bool ok2 = chopInspector_.getMidiInfo("gain").isEmpty() && midiSvc->getCcBindings().empty();
        return (ok && ok2) ? juce::String("PASS cleared")
                           : "FAIL clear: bindings=" + juce::String((int) midiSvc->getCcBindings().size());
    }
    return "FAIL unknown step " + step;
}

} // namespace samplar
