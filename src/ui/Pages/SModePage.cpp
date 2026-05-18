#include "SModePage.h"
#include "../../utils/Tracing.h"
#include "../../utils/RecentSongs.h"
#include "../../utils/SamplarSettings.h"
#include "../../app/ServiceLocator.h"
#include "../../app/ServiceRegistry.h"
#include "../../services/MidiMappingService.h"
// SAMPLAR: removed - MentorService deleted
#include "../../services/smode/SpectralFluxDetector.h"
#include "../../services/analysis/HybridTempoDetector.h"
#include "../../audio/MicroFade.h"

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

SModePage::SModePage(EventBus& bus)
    : bus_(bus), midiLearnOverlay_(bus)
{
    SAMPLAR_TRACE_SCOPE("page-ctor", "SModePage");
    juce::Logger::writeToLog("🎵 [SModePage] === constructor ===");

    // AppState binding — persist sequencer lanes (shuffled patterns, stem dots,
    // bars, bpm). Uses SequencerLaneComponent::captureState / restoreState which
    // already existed for the internal undo stack, so no new lane API needed.
    if (auto* app = samplar::ServiceLocator::getAppState())
    {
        stateBinding_ = std::make_unique<samplar::ScopedStateBinding>(
            *app,
            samplar::StateBinding {
                juce::Identifier("smodePage"), 1,
                // save
                [this](juce::ValueTree& t)
                {
                    t.removeAllChildren(nullptr);
                    t.setProperty("laneCount", (int) sequencerLanes_.size(), nullptr);
                    for (int i = 0; i < (int) sequencerLanes_.size(); ++i)
                    {
                        if (! sequencerLanes_[i]) continue;
                        juce::ValueTree lane("lane");
                        lane.setProperty("index", i, nullptr);
                        // captureState returns juce::var (DynamicObject). Store it
                        // directly — ValueTree properties are juce::var-typed and
                        // writeToStream round-trips DynamicObjects + arrays intact,
                        // so there's no need (and no safety) in going through JSON.
                        auto snap = sequencerLanes_[i]->captureState();
                        lane.setProperty("state", snap, nullptr);

                        // Diagnostic: how many slots did we actually capture?
                        int slotCount = 0;
                        int bars = 0; int subdiv = 0;
                        if (auto* obj = snap.getDynamicObject())
                        {
                            if (auto* arr = obj->getProperty("slots").getArray())
                                slotCount = arr->size();
                            bars   = (int) obj->getProperty("numBars");
                            subdiv = (int) obj->getProperty("slotsPerBar");
                        }
                        juce::Logger::writeToLog("[smodePage-binding]   lane " + juce::String(i)
                            + " captured: slots=" + juce::String(slotCount)
                            + " bars=" + juce::String(bars)
                            + " subdiv=" + juce::String(subdiv));
                        t.appendChild(lane, nullptr);
                    }
                    juce::Logger::writeToLog("[smodePage-binding] saved "
                        + juce::String((int) sequencerLanes_.size()) + " lanes");
                },
                // restore
                [this](const juce::ValueTree& t)
                {
                    struct LaneSnap { int index; juce::var state; };
                    std::vector<LaneSnap> snaps;
                    for (int i = 0; i < t.getNumChildren(); ++i)
                    {
                        auto c = t.getChild(i);
                        if (! c.hasType(juce::Identifier("lane"))) continue;
                        LaneSnap s;
                        s.index = (int) c.getProperty("index", i);
                        s.state = c.getProperty("state");

                        // Diagnostic: how many slots survived into the restore side?
                        int slotCount = 0;
                        if (auto* obj = s.state.getDynamicObject())
                            if (auto* arr = obj->getProperty("slots").getArray())
                                slotCount = arr->size();
                        juce::Logger::writeToLog("[smodePage-binding]   lane " + juce::String(s.index)
                            + " to-restore: slots=" + juce::String(slotCount));
                        snaps.push_back(std::move(s));
                    }

                    sequencerLanes_.clear();

                    // Two-pass restore:
                    //   Pass 1 — add every lane as a child component.
                    //   ---- resized() here gives each lane valid bounds so the
                    //        lane's internal slotArea_ is populated. Without this,
                    //        buildSlotComponents() inside restoreState() computes
                    //        cellW from a 0-width slotArea_ and falls back to 20px,
                    //        which renders all slots clustered at the left edge
                    //        ("shuffle not saving" bug from 2026-04-22).
                    //   Pass 2 — apply the saved state (this is when slot components
                    //        get built with the correct per-cell width).
                    for (size_t i = 0; i < snaps.size(); ++i)
                        addSequencerLane();

                    resized();

                    for (size_t i = 0; i < snaps.size() && i < sequencerLanes_.size(); ++i)
                        if (sequencerLanes_[i])
                            sequencerLanes_[i]->restoreState(snaps[i].state);

                    resized();
                    repaint();

                    juce::Logger::writeToLog("[smodePage-binding] restored "
                        + juce::String((int) snaps.size()) + " lanes (2-pass layout)");
                }
            });

        // ── Per-stem pitch persistence ──────────────────────────────────────
        // Saved as a flat array of semitone values, one per stem index.
        stemPitchBinding_ = std::make_unique<samplar::ScopedStateBinding>(
            *app,
            samplar::StateBinding {
                juce::Identifier("smodeStemPitch"), 1,
                // save: pull from MidiMappingService (it's the source of truth)
                [this](juce::ValueTree& t)
                {
                    t.removeAllProperties(nullptr);
                    samplar::MidiMappingService* midiSvc = nullptr;
                    if (auto* reg = samplar::ServiceLocator::getRegistry())
                        midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
                    for (int i = 0; i < samplar::MidiMappingService::MAX_STEMS; ++i)
                    {
                        const float v = midiSvc ? midiSvc->getStemPitchSemitones(i) : 0.0f;
                        t.setProperty(juce::Identifier("p" + juce::String(i)), v, nullptr);
                    }
                    juce::Logger::writeToLog("[smodeStemPitch-binding] saved");
                },
                // restore: push into MidiMappingService AND each visible lane's knob
                [this](const juce::ValueTree& t)
                {
                    samplar::MidiMappingService* midiSvc = nullptr;
                    if (auto* reg = samplar::ServiceLocator::getRegistry())
                        midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
                    for (int i = 0; i < samplar::MidiMappingService::MAX_STEMS; ++i)
                    {
                        const float v = (float) (double) t.getProperty(
                            juce::Identifier("p" + juce::String(i)), 0.0);
                        if (midiSvc) midiSvc->setStemPitchSemitones(i, v);
                        if (i < (int) stemWaveforms_.size() && stemWaveforms_[(size_t) i])
                            stemWaveforms_[(size_t) i]->setPitchSemitones(v);
                    }
                    juce::Logger::writeToLog("[smodeStemPitch-binding] restored");
                }
            });

        // ── Per-stem MIDI-mod state persistence ─────────────────────────────
        // Persists per-stem velocity curve + pitchBend/modWheel/aftertouch
        // destinations and ranges. CC bindings are NOT persisted (user
        // re-learns next session). TODO: persist ccBindings_ once a stable
        // serialization of std::function callbacks is in place.
        stemModBinding_ = std::make_unique<samplar::ScopedStateBinding>(
            *app,
            samplar::StateBinding {
                juce::Identifier("smodeStemMod"), 1,
                // save
                [this](juce::ValueTree& t)
                {
                    t.removeAllProperties(nullptr);
                    samplar::MidiMappingService* midiSvc = nullptr;
                    if (auto* reg = samplar::ServiceLocator::getRegistry())
                        midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
                    if (!midiSvc) return;
                    using MM = samplar::MidiMappingService;
                    for (int i = 0; i < MM::MAX_STEMS; ++i)
                    {
                        const juce::String suf = juce::String(i);
                        t.setProperty("vc"  + suf,
                            (int) midiSvc->getStemVelocityCurve(i), nullptr);
                        t.setProperty("pbd" + suf,
                            (int) midiSvc->getStemPitchBendDest(i),  nullptr);
                        t.setProperty("mwd" + suf,
                            (int) midiSvc->getStemModWheelDest(i),   nullptr);
                        t.setProperty("atd" + suf,
                            (int) midiSvc->getStemAftertouchDest(i), nullptr);
                        t.setProperty("pbr" + suf,
                            (double) midiSvc->getStemPitchBendRange(i),  nullptr);
                        t.setProperty("mwr" + suf,
                            (double) midiSvc->getStemModWheelRange(i),   nullptr);
                        t.setProperty("atr" + suf,
                            (double) midiSvc->getStemAftertouchRange(i), nullptr);
                    }
                    juce::Logger::writeToLog("[smodeStemMod-binding] saved");
                },
                // restore
                [this](const juce::ValueTree& t)
                {
                    samplar::MidiMappingService* midiSvc = nullptr;
                    if (auto* reg = samplar::ServiceLocator::getRegistry())
                        midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
                    if (!midiSvc) return;
                    using MM = samplar::MidiMappingService;
                    for (int i = 0; i < MM::MAX_STEMS; ++i)
                    {
                        const juce::String suf = juce::String(i);
                        const int vc  = (int) t.getProperty("vc"  + suf, (int) MM::VelocityCurve::Linear);
                        const int pbd = (int) t.getProperty("pbd" + suf, (int) MM::ModDestination::Pitch);
                        const int mwd = (int) t.getProperty("mwd" + suf, (int) MM::ModDestination::None);
                        const int atd = (int) t.getProperty("atd" + suf, (int) MM::ModDestination::None);
                        const float pbr = (float) (double) t.getProperty("pbr" + suf, 2.0);
                        const float mwr = (float) (double) t.getProperty("mwr" + suf, 12.0);
                        const float atr = (float) (double) t.getProperty("atr" + suf, 12.0);
                        midiSvc->setStemVelocityCurve(i, (MM::VelocityCurve)  vc);
                        midiSvc->setStemPitchBendDest (i, (MM::ModDestination) pbd);
                        midiSvc->setStemModWheelDest  (i, (MM::ModDestination) mwd);
                        midiSvc->setStemAftertouchDest(i, (MM::ModDestination) atd);
                        midiSvc->setStemPitchBendRange (i, pbr);
                        midiSvc->setStemModWheelRange  (i, mwr);
                        midiSvc->setStemAftertouchRange(i, atr);
                    }
                    juce::Logger::writeToLog("[smodeStemMod-binding] restored");
                }
            });
    }


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
        if (auto* reg = samplar::ServiceLocator::getRegistry())
        {
            if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
                midiSvc->setSModeTransportLooping(loopOn);
        }

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
        bool synced = dawSyncBtn_.getToggleState();
        juce::Logger::writeToLog("[SModePage] DAW tempo sync: " + juce::String(synced ? "ON" : "OFF"));
        // Use the singleton-bus pattern that SampleBrowserPage uses — proven to work.
        auto* bus = samplar::ServiceLocator::getBus();
        if (bus == nullptr)
        {
            juce::Logger::writeToLog("[SModePage] DAW sync: ServiceLocator::getBus() returned null!");
            return;
        }
        if (synced)
        {
            // Subscribe FIRST so the immediate re-publish below is captured.
            hostTempoSub_ = bus->subscribe("host.tempo", [this](const juce::var& v) {
                double hostBpm = static_cast<double>(v);
                juce::Logger::writeToLog("[SModePage] host.tempo received: " + juce::String(hostBpm, 3));
                if (hostBpm <= 0.0) return;
                // Route through the canonical applyBpm() — same path the BPM input uses,
                // updates display, model, grid, waveforms, and pattern engine.
                applyBpm(hostBpm);
            });
            // Ask the audio thread to (re-)publish the current host BPM right now.
            bus->publish("host.tempo.request");
            juce::Logger::writeToLog("[SModePage] DAW sync: subscribed to host.tempo, requested re-publish");
        }
        else
        {
            if (hostTempoSub_ != 0)
            {
                bus->unsubscribe("host.tempo", hostTempoSub_);
                hostTempoSub_ = 0;
            }
        }
    };

    // Segmentation controls
    for (auto* btn : { &autoBeatBtn_, &autoBarBtn_, &auto2BarBtn_, &autoTransientBtn_,
                        &quickAssignBtn_, &exportBtn_, &undoBtn_, &redoBtn_ })
    {
        addAndMakeVisible(btn);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    }
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
            // Post-undo UI refresh MUST NOT push a new undo entry — suppress push while we run it.
            model.beginSuppressPush();
            applyBpm(model.getDetectedTempo());
            for (auto& wf : stemWaveforms_) wf->repaint();
            model.endSuppressPush();

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
            model.beginSuppressPush();
            applyBpm(model.getDetectedTempo());
            for (auto& wf : stemWaveforms_) wf->repaint();
            model.endSuppressPush();

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

    // Auto segment — single combo button, right-click to switch mode
    autoBeatBtn_.setButtonText("Auto: Beat");
    autoBeatBtn_.setTooltip("Left-click: auto-segment  |  Right-click to switch mode: Beat / Bar / 2-Bar / Transient");
    autoBeatBtn_.onClick = [this]()
    {
        auto text = autoBeatBtn_.getButtonText();
        if (text == "Auto: Beat")         autoSegmentByDivision(1.0);
        else if (text == "Auto: Bar")     autoSegmentByDivision(4.0);
        else if (text == "Auto: 2-Bar")   autoSegmentByDivision(8.0);
        else if (text == "Auto: Transient") autoSegmentByTransient();
    };
    autoBeatBtn_.onStateChange = [this]()
    {
        if (autoBeatBtn_.isDown() && juce::ModifierKeys::currentModifiers.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Auto: Beat",      true, autoBeatBtn_.getButtonText() == "Auto: Beat");
            menu.addItem(2, "Auto: Bar",       true, autoBeatBtn_.getButtonText() == "Auto: Bar");
            menu.addItem(3, "Auto: 2-Bar",     true, autoBeatBtn_.getButtonText() == "Auto: 2-Bar");
            menu.addItem(4, "Auto: Transient", true, autoBeatBtn_.getButtonText() == "Auto: Transient");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&autoBeatBtn_),
                [this](int result)
            {
                if (result == 1) autoBeatBtn_.setButtonText("Auto: Beat");
                else if (result == 2) autoBeatBtn_.setButtonText("Auto: Bar");
                else if (result == 3) autoBeatBtn_.setButtonText("Auto: 2-Bar");
                else if (result == 4) autoBeatBtn_.setButtonText("Auto: Transient");
            });
        }
    };
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

        // Build popup menu: All Stems + each individual stem
        juce::PopupMenu menu;
        menu.addItem(1, "All Stems");
        menu.addSeparator();
        for (int s = 0; s < stemCount; ++s)
        {
            auto* stem = model.getStem(s);
            juce::String name = stem ? stem->name : ("Stem " + juce::String(s));
            menu.addItem(s + 10, name);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&quickAssignBtn_),
            [this](int result)
        {
            if (result == 0) return;  // User cancelled
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();

            // Determine which stems to assign
            std::set<int> stemsToAssign;
            if (result == 1)
            {
                // All stems (respecting exclusion)
                for (int s = 0; s < model.getStemCount(); ++s)
                {
                    if (s < static_cast<int>(stemWaveforms_.size()) &&
                        stemWaveforms_[static_cast<size_t>(s)]->excludedFromQuickAssign_)
                        continue;
                    stemsToAssign.insert(s);
                }
            }
            else
            {
                stemsToAssign.insert(result - 10);
            }

            // Snapshot segment audio data on message thread
            struct SegSnap { int segIdx; int64_t start, end; std::vector<std::vector<float>> ch; };
            struct StemSnap { int stemIdx; std::vector<SegSnap> segs; };
            std::vector<StemSnap> snapshots;

            for (int s : stemsToAssign)
            {
                auto* stem = model.getStem(s);
                if (!stem) continue;
                StemSnap ss; ss.stemIdx = s;
                for (int i = 0; i < static_cast<int>(stem->segments.size()); ++i)
                {
                    const auto& seg = stem->segments[static_cast<size_t>(i)];
                    int64_t st = juce::jlimit((int64_t)0, (int64_t)stem->audioBuffer.getNumSamples(), seg.startSample);
                    int64_t en = juce::jlimit(st, (int64_t)stem->audioBuffer.getNumSamples(), seg.endSample);
                    int len = static_cast<int>(en - st);
                    SegSnap seg2; seg2.segIdx = i; seg2.start = st; seg2.end = en;
                    for (int ch = 0; ch < stem->audioBuffer.getNumChannels(); ++ch)
                    {
                        const float* p = stem->audioBuffer.getReadPointer(ch) + st;
                        seg2.ch.emplace_back(p, p + len);
                    }
                    ss.segs.push_back(std::move(seg2));
                }
                snapshots.push_back(std::move(ss));
            }

            quickAssignBtn_.setEnabled(false);
            quickAssignBtn_.setButtonText("Analyzing...");

            std::thread([this, snapshots = std::move(snapshots)]() mutable
            {
                // Compute RMS per segment off message thread.
                //   toAssign — segments above the silence threshold (get MIDI notes)
                //   toDelete — segments below the threshold (get removed from the
                //              model so the stem lane is visually clean)
                std::vector<std::pair<int,int>> toAssign;
                std::vector<std::pair<int,int>> toDelete;
                for (auto& ss : snapshots)
                {
                    for (auto& seg : ss.segs)
                    {
                        double sumSq = 0.0;
                        int total = 0;
                        for (auto& ch : seg.ch)
                        {
                            for (float v : ch) sumSq += v * v;
                            total += static_cast<int>(ch.size());
                        }
                        float rms = total > 0 ? static_cast<float>(std::sqrt(sumSq / total)) : 0.0f;
                        float db = rms > 0.0f ? 20.0f * std::log10(rms) : -100.0f;
                        if (db > -40.0f)
                            toAssign.push_back({ ss.stemIdx, seg.segIdx });
                        else
                            toDelete.push_back({ ss.stemIdx, seg.segIdx });
                    }
                }

                juce::MessageManager::callAsync([this,
                    toAssign = std::move(toAssign),
                    toDelete = std::move(toDelete)]()
                {
                    if (!smodeService_) return;
                    auto& model = smodeService_->getModel();
                    model.pushUndoState("Quick-assign MIDI notes");

                    // Assign first (uses current indices, before any deletion).
                    int note = 36;
                    for (const auto& [si, segi] : toAssign)
                    {
                        if (note > 127) break;
                        model.assignMidiNote(si, segi, note++);
                    }

                    // Delete silent segments in DESCENDING index order per-stem
                    // so removal doesn't invalidate the indices of later deletions.
                    auto sortedToDelete = toDelete;
                    std::sort(sortedToDelete.begin(), sortedToDelete.end(),
                        [](const auto& a, const auto& b) {
                            return a.first == b.first ? a.second > b.second : a.first < b.first;
                        });
                    for (const auto& [si, segi] : sortedToDelete)
                        model.removeSegment(si, segi);

                    quickAssignBtn_.setEnabled(true);
                    quickAssignBtn_.setButtonText("Quick Assign");
                    juce::Logger::writeToLog("✅ [SModePage] Quick Assign: "
                        + juce::String(toAssign.size()) + " segments assigned, "
                        + juce::String(toDelete.size()) + " silent segments removed");
                    for (auto& wf : stemWaveforms_) wf->repaint();
                });
            }).detach();
        });
    };

    clearAllBtn_.onClick = [this]()
    {
        if (!smodeService_) return;
        auto& model = smodeService_->getModel();
        model.pushUndoState("Clear all segments");
        for (int i = 0; i < model.getStemCount(); ++i)
            model.clearSegments(i);
        juce::Logger::writeToLog("🎵 [SModePage] Clear All: all segments removed");
        for (auto& wf : stemWaveforms_) wf->repaint();
    };

    // MIDI controls — single combo button: cycles POLY/1SH → POLY/GATE → MONO/1SH → MONO/GATE
    addAndMakeVisible(monoPolyBtn_);
    monoPolyBtn_.setButtonText("POLY 1SH");
    monoPolyBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    monoPolyBtn_.setTooltip("Click to cycle: POLY/MONO + ONE-SHOT/GATE");
    monoPolyBtn_.onClick = [this]()
    {
        // Cycle through 4 modes
        static int midiMode = 0;
        midiMode = (midiMode + 1) % 4;
        bool mono = (midiMode >= 2);
        bool gate = (midiMode == 1 || midiMode == 3);

        juce::String label = juce::String(mono ? "MONO" : "POLY") + " " + juce::String(gate ? "GATE" : "1SH");
        monoPolyBtn_.setButtonText(label);

        if (auto* reg = samplar::ServiceLocator::getRegistry())
        {
            if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
            {
                midiSvc->setMonophonic(mono);
                midiSvc->setSampleGateMode(gate);
                juce::Logger::writeToLog("[SModePage] MIDI mode: " + label);
            }
        }
    };

    // trigModeBtn_ hidden — merged into monoPolyBtn_
    trigModeBtn_.setVisible(false);

    // LINK ALL button — syncs zoom/scroll/segments across all stems
    addAndMakeVisible(linkAllBtn_);
    linkAllBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
    linkAllBtn_.setClickingTogglesState(true);
    linkAllBtn_.setToggleState(true, juce::dontSendNotification);
    linkAllBtn_.onClick = [this]()
    {
        if (smodeService_) smodeService_->getModel().pushUndoState("Link all stems");
        linkAllActive_ = linkAllBtn_.getToggleState();
        linkAllBtn_.setColour(juce::TextButton::buttonColourId,
                              linkAllActive_ ? juce::Colour(0xff2d6d3a) : juce::Colour(0xff2d2d3a));

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
        double val = bpmInput_.getText().getDoubleValue();
        if (val >= 30.0 && val <= 300.0) applyBpm(val);
    };

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
        gridLockBtn_.setButtonText(gridLocked_ ? "LOCKED" : "LOCK");
        gridLockBtn_.setColour(juce::TextButton::buttonColourId,
                               gridLocked_ ? juce::Colour(0xffb35900) : juce::Colour::fromString("ff21262D"));
        if (gridLocked_)
            lockedBpm_ = detectedBpm_;
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
        juce::Timer::callAfterDelay(100, [this]()
        {
            tapTempoBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff21262D"));
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
    setGridDivision(currentGridDivision_);  // Initialize combo selection + propagate

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

        // When LINK ALL is active, assign the same note to the same segment index
        // on ALL stems — so one key triggers all stems' corresponding segment together
        if (linkAllActive_)
        {
            for (int s = 0; s < model.getStemCount(); ++s)
            {
                auto* stem = model.getStem(s);
                if (stem && segIdx < static_cast<int>(stem->segments.size()))
                    model.assignMidiNote(s, segIdx, noteNumber);
            }
            juce::Logger::writeToLog("✅ [SModePage] MIDI Learn (LINK ALL): note=" + juce::String(noteNumber)
                                     + " assigned to seg " + juce::String(segIdx) + " on ALL stems");
        }
        else
        {
            model.assignMidiNote(stemIdx, segIdx, noteNumber);
            juce::Logger::writeToLog("✅ [SModePage] MIDI Learn assigned: stem=" + juce::String(stemIdx)
                                     + " seg=" + juce::String(segIdx) + " note=" + juce::String(noteNumber));
        }
        for (auto& wf : stemWaveforms_) wf->repaint();
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

    // Chain button kept in code but hidden from UI
    playChainBtn_.setVisible(false);

    // Stop button hidden — Play All toggles play/stop
    stopAllSeqBtn_.setVisible(false);

    addAndMakeVisible(dragSeqWavArea_);
    dragSeqWavArea_.setHasContent(true);
    dragSeqWavArea_.onCanRender = [this]() { return !sequencerLanes_.empty(); };
    dragSeqWavArea_.onRenderPattern = [this](double& outSampleRate,
        std::function<void(float)> progressCb) -> std::unique_ptr<juce::AudioBuffer<float>>
    {
        if (!smodeService_) return nullptr;
        auto& model = smodeService_->getModel();
        double modelSr = model.getSampleRate();
        if (modelSr <= 0.0) modelSr = 44100.0;
        outSampleRate = modelSr;
        double bpm = detectedBpm_ > 0.0 ? detectedBpm_ : 120.0;
        double samplesPerBeat = (60.0 / bpm) * modelSr;

        int maxCells = 0;
        int numCh = 2;
        for (auto& lane : sequencerLanes_)
        {
            if (lane->isMuted()) continue;
            maxCells = juce::jmax(maxCells, lane->getNumBars() * 4);
            for (const auto& slot : lane->getSlots())
            {
                if (slot.empty) continue;
                auto* stem = model.getStem(slot.stemIndex);
                if (stem) { numCh = stem->audioBuffer.getNumChannels(); break; }
            }
        }
        if (maxCells <= 0) return nullptr;

        int64_t totalSmp = static_cast<int64_t>(maxCells * samplesPerBeat);
        auto buf = std::make_unique<juce::AudioBuffer<float>>(numCh, static_cast<int>(totalSmp));
        buf->clear();

        int totalSlots = 0;
        for (auto& lane : sequencerLanes_)
            if (!lane->isMuted()) totalSlots += static_cast<int>(lane->getSlots().size());

        int processed = 0;
        for (auto& lane : sequencerLanes_)
        {
            if (lane->isMuted()) continue;
            for (const auto& slot : lane->getSlots())
            {
                if (slot.empty) { ++processed; continue; }
                auto* stem = model.getStem(slot.stemIndex);
                auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
                if (!stem || !seg || seg->getLengthSamples() <= 0) { ++processed; continue; }

                const auto& srcBuf = stem->audioBuffer;
                int64_t segStart = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), seg->startSample);
                int64_t segEnd = juce::jlimit(segStart, (int64_t)srcBuf.getNumSamples(), seg->endSample);
                int64_t segLen = segEnd - segStart;

                int64_t writePos = static_cast<int64_t>(slot.gridCell * samplesPerBeat);
                int64_t cellDur = static_cast<int64_t>(slot.cellSpan * samplesPerBeat);
                int64_t trimStart = segStart + static_cast<int64_t>(slot.trimStart * segLen);
                int64_t trimEnd = segStart + static_cast<int64_t>(slot.trimEnd * segLen);
                trimStart = juce::jlimit(segStart, segEnd, trimStart);
                trimEnd = juce::jlimit(trimStart + 1, segEnd, trimEnd);
                int copyLen = static_cast<int>(juce::jmin(trimEnd - trimStart, cellDur));

                if (copyLen > 0 && writePos + copyLen <= totalSmp)
                {
                    int chCopy = juce::jmin(numCh, srcBuf.getNumChannels());
                    for (int ch = 0; ch < chCopy; ++ch)
                        buf->addFrom(ch, static_cast<int>(writePos), srcBuf, ch,
                                      static_cast<int>(trimStart), copyLen, slot.velocity);
                }
                ++processed;
                if (progressCb) progressCb(static_cast<float>(processed) / juce::jmax(1, totalSlots));
            }
        }
        return buf;
    };

    // Tooltips for every button
    transportBtn_.setTooltip("Play / Stop transport (Spacebar)");
    autoBeatBtn_.setTooltip("Left-click: auto-segment  |  Right-click to switch mode: Beat / Bar / 2-Bar / Transient");
    autoBarBtn_.setTooltip("Auto-segment by bar (4 beats per segment)");
    auto2BarBtn_.setTooltip("Auto-segment by 2 bars (8 beats per segment)");
    autoTransientBtn_.setTooltip("Auto-segment by transient detection");
    snapToggle_.setTooltip("Snap segment edges to grid lines");
    freeformToggle_.setTooltip("Draw segments freely without snapping");
    quickAssignBtn_.setTooltip("Auto-assign MIDI notes (C1+) to all segments");
    clearAllBtn_.setTooltip("Remove all segments from all stems");
    exportBtn_.setTooltip("Export all segments as individual WAV files");
    monoPolyBtn_.setTooltip("Toggle monophonic (one voice) / polyphonic (multiple voices)");
    trigModeBtn_.setTooltip("Toggle One-Shot (play full sample) / Gate (play while held)");
    linkAllBtn_.setTooltip("Link all stems: zoom, scroll, and segment together");
    bpmInput_.setTooltip("Type BPM value and press Enter");
    gridLockBtn_.setTooltip("Lock grid: BPM changes transpose pitch instead of re-segmenting");
    tapTempoBtn_.setTooltip("Tap repeatedly to set tempo by rhythm");
    bpmRoundBtn_.setTooltip("Round BPM to nearest whole number");
    halfTempoBtn_.setTooltip("Halve the tempo (wider grid)");
    doubleTempoBtn_.setTooltip("Double the tempo (tighter grid)");
    addSeqBtn_.setTooltip("Add a new sequencer lane");
    smartFillBtn_.setTooltip("Analyze segment audio and auto-fill 3 lanes (Drums / Bass / Melodic)");
    undoBtn_.setTooltip("Undo last action (Ctrl+Z)");
    redoBtn_.setTooltip("Redo last undone action (Ctrl+Y)");

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

    // Empty-state overlay — sits on top of all other children
    addAndMakeVisible(loadAudioOverlay_);
    loadAudioOverlay_.onLoad = [this]() { openLoadAudioChooser(); };
    loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
    loadAudioOverlay_.onLoadRecent = [this](const juce::File& f) {
        if (!f.existsAsFile()) return;  // overlay handles missing entries itself
        juce::Logger::writeToLog("[SModePage] " + juce::String(juce::CharPointer_UTF8("\xf0\x9f\x93\x81"))
                                 + " recent click: " + f.getFullPathName());
        samplar::RecentSongs::push(f);  // bumps it back to the top
        loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
        if (onRequestSeparation)
            onRequestSeparation(f);
    };
    loadAudioOverlay_.onLoadFile = [this](const juce::File& f) {
        if (!f.existsAsFile()) {
            juce::Logger::writeToLog("[SModePage] drag-drop: file does not exist: "
                                     + f.getFullPathName());
            return;
        }
        juce::Logger::writeToLog("[SModePage] "
            + juce::String(juce::CharPointer_UTF8("\xf0\x9f\x93\x81"))
            + " drag-drop load: " + f.getFullPathName());
        samplar::RecentSongs::push(f);
        loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
        if (onRequestSeparation)
            onRequestSeparation(f);
    };
    loadAudioOverlay_.setVisible(true);   // start visible (no audio loaded yet)

    // Register as KeyListener so spacebar/shortcuts work regardless of which child has focus
    addKeyListener(this);
    setWantsKeyboardFocus(true);
}

SModePage::~SModePage()
{
    // NOTE: Do NOT disconnect S-Mode model from MIDI here.
    // SModeService (and its model) is owned by the processor and survives
    // editor close/reopen. MIDI triggering must continue without the UI.

    stopTimer();

    if (segmentTriggeredSub_ != 0)
        bus_.unsubscribe("smode.segment.triggered", segmentTriggeredSub_);
    if (segmentStoppedSub_ != 0)
        bus_.unsubscribe("smode.segment.stopped", segmentStoppedSub_);
    if (transportStoppedSub_ != 0)
        bus_.unsubscribe("smode.transport.stopped", transportStoppedSub_);
    if (downbeatDetectedSub_ != 0)
        bus_.unsubscribe("smode.downbeat.detected", downbeatDetectedSub_);
}

void SModePage::openLoadAudioChooser()
{
    juce::Logger::writeToLog("[SModePage] openLoadAudioChooser - opening file chooser");
    auto chooser = std::make_shared<juce::FileChooser>(
        "Choose audio file for S-Mode separation",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) {
            auto results = fc.getResults();
            if (results.isEmpty()) return;
            juce::File chosen = results[0];
            juce::Logger::writeToLog("[SModePage] File chosen: " + chosen.getFullPathName());
            samplar::RecentSongs::push(chosen);
            loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
            if (onRequestSeparation)
                onRequestSeparation(chosen);
        });
}

void SModePage::beginSeparationVisuals(int stemCount)
{
    juce::Logger::writeToLog("[SModePage] === beginSeparationVisuals stemCount=" + juce::String(stemCount) + " ===");

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

    // ── Undo/Redo wiring ─────────────────────────────────────────────
    // 1) Register hooks so the model's snapshot can save/restore lane patterns + UI flags.
    model.setExtraStateHooks(
        [this]() { return captureUiExtraState(); },
        [this](const juce::var& v) { restoreUiExtraState(v); });

    // 2) UI updates when the stack changes.
    model.onUndoStackChanged = [this]() { updateUndoUi(); };

    // 3) New session: clear any history left from a prior track.
    model.clearUndoHistory();
    updateUndoUi();

    // Wire S-Mode model to MidiMappingService for MIDI-triggered segment playback
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (reg) {
        auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
        if (midiSvc) {
            midiSvc->setSModeModel(&model);
            juce::Logger::writeToLog("[SModePage] Wired SModeSegmentModel to MidiMappingService");
        }
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

                // Copy segments from source stem to destination (preserve MIDI notes)
                model.clearSegments(j);
                for (const auto& seg : srcStem->segments)
                {
                    int newSegIdx = model.addSegment(j, seg.startSample, seg.endSample);
                    if (seg.midiNote >= 0 && newSegIdx >= 0)
                        model.assignMidiNote(j, newSegIdx, seg.midiNote);
                }

                stemWaveforms_[static_cast<size_t>(j)]->repaint();
            }
        };
        wf->onSegmentDeleted = [this, i](int segIdx)
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
                return;
            }

            for (int j = 0; j < static_cast<int>(stemWaveforms_.size()); ++j)
            {
                if (j == i) continue;
                bool destLinked = linkAllActive_ || model.isLinked(j);
                if (!destLinked) continue;

                auto* destStem = model.getStem(j);
                if (!destStem) continue;

                const int beforeCount = static_cast<int>(destStem->segments.size());
                if (segIdx >= 0 && segIdx < beforeCount)
                {
                    juce::Logger::writeToLog("[SMode] deleteSegment-linked stem=" + juce::String(j)
                                             + " segIdx=" + juce::String(segIdx)
                                             + " before=" + juce::String(beforeCount));
                    model.removeSegment(j, segIdx);
                    juce::Logger::writeToLog("[SMode] deleteSegment-linked stem=" + juce::String(j)
                                             + " after=" + juce::String((int) destStem->segments.size()));
                }

                stemWaveforms_[static_cast<size_t>(j)]->repaint();
            }
        };
        wf->onSegmentSelected = [this](int stemIdx, int segIdx) {
            juce::Logger::writeToLog("🎵 [SModePage] Segment selected: stem=" + juce::String(stemIdx)
                                     + " seg=" + juce::String(segIdx));
        };
        wf->onSegmentClicked = [this](int stemIdx, int segIdx) {
            // Single-click plays the segment (and linked segments if LINK ALL)
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();

            auto* reg = samplar::ServiceLocator::getRegistry();
            if (!reg) return;
            auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
            if (!midiSvc) return;

            // ── Stop any currently playing voices (retrigger, no layering) ──
            midiSvc->stopAllVoices();

            // ── Interrupt transport if playing ──
            if (transportPlaying_)
            {
                juce::Logger::writeToLog("[SModePage] Click-play: interrupting transport");
                stopTransport();
            }

            // ── Gate mode: if this segment is already playing, stop it ──
            bool isGateMode = midiSvc->isSampleGateMode();

            // If segment has a MIDI note, trigger via that (plays ALL stems with this note)
            auto* seg = model.getSegment(stemIdx, segIdx);
            if (seg && seg->midiNote >= 0)
            {
                if (isGateMode)
                {
                    // In gate mode, send note-off to stop if already playing
                    midiSvc->handleNoteOff(1, seg->midiNote);
                }
                midiSvc->handleNoteOn(1, seg->midiNote, 1.0f);
                return;
            }

            // No MIDI note — play directly (and linked stems at same segment index)
            std::vector<std::pair<int, int>> toPlay;
            if (linkAllActive_)
            {
                for (int s = 0; s < model.getStemCount(); ++s)
                {
                    auto* stem = model.getStem(s);
                    if (stem && segIdx < static_cast<int>(stem->segments.size()))
                        toPlay.push_back({ s, segIdx });
                }
            }
            else
            {
                toPlay.push_back({ stemIdx, segIdx });
            }

            double modelSr = model.getSampleRate();
            if (modelSr <= 0.0) modelSr = 44100.0;

            for (const auto& [si, sgi] : toPlay)
            {
                auto* stem = model.getStem(si);
                auto* s = model.getSegment(si, sgi);
                if (!stem || !s || s->getLengthSamples() <= 0 || stem->mute) continue;

                // Realtime pitch path: read from raw stem buffer, pass stemIdx
                // through to playBufferDirect so the voice uses SoundTouch.
                const auto& srcBuf = stem->audioBuffer;

                int64_t startSmp = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) s->startSample);
                int64_t endSmp   = juce::jlimit(startSmp,    (int64_t)srcBuf.getNumSamples(), (int64_t) s->endSample);
                int segLen = static_cast<int>(endSmp - startSmp);
                if (segLen <= 0) continue;

                auto segBuf = std::make_shared<juce::AudioBuffer<float>>(srcBuf.getNumChannels(), segLen);
                for (int ch = 0; ch < srcBuf.getNumChannels(); ++ch)
                    segBuf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(startSmp), segLen);

                bool isLooping = loopBtn_.getToggleState();
                const float pitchSemi = midiSvc->getStemPitchSemitones(si);
                juce::Logger::writeToLog("[SModePage] playSegment stem=" + juce::String(si)
                    + " pitch=" + juce::String(pitchSemi, 2));
                midiSvc->playBufferDirect(segBuf, modelSr, 1.0f, isLooping, si);

                double durSec = static_cast<double>(segLen) / modelSr;
                stemWaveforms_[static_cast<size_t>(si)]->startSegmentAnimation(sgi, durSec, isLooping);
            }

            // In gate mode, start a timer-based animation stop isn't needed —
            // the animation already has its own duration and will stop naturally.
            // But if the user clicks again in gate mode, the note-off above handles it.
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

            const auto& srcBuf = stem->audioBuffer;
            double sr = model.getSampleRate();
            int64_t startSmp = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), seg->startSample);
            int64_t endSmp   = juce::jlimit(startSmp,   (int64_t)srcBuf.getNumSamples(), seg->endSample);
            int len = static_cast<int>(endSmp - startSmp);
            if (len <= 0) return;

            // Copy audio into shared buffer (message thread — fast, just a memcpy)
            auto segBuf = std::make_shared<juce::AudioBuffer<float>>(srcBuf.getNumChannels(), len);
            for (int ch = 0; ch < srcBuf.getNumChannels(); ++ch)
                segBuf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(startSmp), len);

            // Match what the user heard during playback: apply the same micro-fade
            // at start and end of the exported segment (no-op if fade is disabled).
            samplar::applyMicroFade(*segBuf, sr);

            int numCh = srcBuf.getNumChannels();
            juce::String stemName = stem->name;
            juce::String noteName = (seg->midiNote >= 0)
                ? StemWaveformComponent::midiNoteToName(seg->midiNote) : juce::String(segIdx);

            // Write WAV on background thread, then trigger drag on message thread
            std::thread([this, segBuf, sr, numCh, stemName, noteName, segIdx]()
            {
                auto tmpFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile(stemName + "_" + noteName + "_seg" + juce::String(segIdx) + ".wav");

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
                        juce::MessageManager::callAsync([this, path]()
                        {
                            juce::StringArray files;
                            files.add(path);
                            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, true, this);
                            juce::Logger::writeToLog("[SModePage] Drag-out WAV ready: " + path);
                        });
                    }
                }
            }).detach();
        };

        wf->onMultiSegmentDragOut = [this](int /*triggerStemIdx*/, int /*triggerSegIdx*/)
        {
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();

            // Collect every selected segment across every stem.
            struct Pick { int stemIdx; int segIdx; };
            std::vector<Pick> picks;
            for (int s = 0; s < model.getStemCount(); ++s)
            {
                auto* stem = model.getStem(s);
                if (!stem) continue;
                for (int i = 0; i < static_cast<int>(stem->segments.size()); ++i)
                    if (stem->segments[(size_t) i].selected)
                        picks.push_back({ s, i });
            }

            if (picks.empty())
            {
                juce::Logger::writeToLog("[SModePage] Multi-drag: no selected segments — nothing to drag");
                return;
            }

            juce::Logger::writeToLog("[SModePage] Multi-drag: rendering "
                                     + juce::String((int) picks.size()) + " segment(s)");

            // Snapshot audio + metadata on the message thread, render on a worker.
            struct Job {
                juce::String fileName;
                int numCh;
                double sr;
                std::shared_ptr<juce::AudioBuffer<float>> buf;
            };
            auto jobs = std::make_shared<std::vector<Job>>();
            const double sr = model.getSampleRate();

            for (const auto& p : picks)
            {
                auto* stem = model.getStem(p.stemIdx);
                auto* seg  = model.getSegment(p.stemIdx, p.segIdx);
                if (!stem || !seg || seg->getLengthSamples() <= 0) continue;

                const auto& srcBuf = stem->audioBuffer;
                int64_t startSmp = juce::jlimit((int64_t) 0, (int64_t) srcBuf.getNumSamples(), seg->startSample);
                int64_t endSmp   = juce::jlimit(startSmp,       (int64_t) srcBuf.getNumSamples(), seg->endSample);
                int len = static_cast<int>(endSmp - startSmp);
                if (len <= 0) continue;

                Job j;
                j.numCh = srcBuf.getNumChannels();
                j.sr    = sr;
                j.buf   = std::make_shared<juce::AudioBuffer<float>>(j.numCh, len);
                for (int ch = 0; ch < j.numCh; ++ch)
                    j.buf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(startSmp), len);
                samplar::applyMicroFade(*j.buf, sr);

                juce::String noteName = (seg->midiNote >= 0)
                    ? StemWaveformComponent::midiNoteToName(seg->midiNote) : juce::String(p.segIdx);
                j.fileName = stem->name + "_" + noteName + "_seg" + juce::String(p.segIdx) + ".wav";

                jobs->push_back(std::move(j));
            }

            if (jobs->empty())
            {
                juce::Logger::writeToLog("[SModePage] Multi-drag: all selected segments empty — abort");
                return;
            }

            std::thread([this, jobs]()
            {
                juce::WavAudioFormat wavFmt;
                juce::StringArray paths;
                auto tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

                for (auto& j : *jobs)
                {
                    auto tmpFile = tmpDir.getChildFile(j.fileName);
                    if (auto os = std::unique_ptr<juce::FileOutputStream>(tmpFile.createOutputStream()))
                    {
                        auto* writer = wavFmt.createWriterFor(os.get(), j.sr,
                                                              static_cast<unsigned int>(j.numCh), 24, {}, 0);
                        if (writer)
                        {
                            os.release();
                            writer->writeFromAudioSampleBuffer(*j.buf, 0, j.buf->getNumSamples());
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

                juce::MessageManager::callAsync([this, paths]()
                {
                    juce::DragAndDropContainer::performExternalDragDropOfFiles(paths, true, this);
                    juce::Logger::writeToLog("[SModePage] Multi-drag-out: " + juce::String(paths.size()) + " file(s)");
                });
            }).detach();
        };

        wf->onGridQuickAssign = [this](int stemIdx, double beatsPerSegment) {
            gridQuickAssignStem(stemIdx, beatsPerSegment);
        };
        wf->onGridOffsetChanged = [this](int stemIdx, double gridOffset) {
            // Propagate grid offset to all linked stems during drag
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();
            bool srcLinked = linkAllActive_ || model.isLinked(stemIdx);
            if (!srcLinked) return;

            for (int j = 0; j < static_cast<int>(stemWaveforms_.size()); ++j)
            {
                if (j == stemIdx) continue;
                bool destLinked = linkAllActive_ || model.isLinked(j);
                if (!destLinked) continue;
                stemWaveforms_[static_cast<size_t>(j)]->setGridOffsetSeconds(gridOffset);
            }
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
        wf->onPitchChanged = [this](int stemIdx, float semi)
        {
            juce::Logger::writeToLog("[SModePage] 🎵 stem pitch changed stem="
                + juce::String(stemIdx) + " semi=" + juce::String(semi, 2));
            if (auto* reg = samplar::ServiceLocator::getRegistry())
            {
                if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
                    midiSvc->setStemPitchSemitones(stemIdx, semi);
            }
            // Mark project dirty so the AppState binding flushes on save.
            if (auto* app = samplar::ServiceLocator::getAppState())
                app->markDirty();
        };
        wf->onFocusRequested = [this](int stemIdx) {
            // Toggle focus: if already focused on this stem, unfocus (show all non-removed)
            if (focusedStem_ == stemIdx)
            {
                // Unfocus — show all stems except removed ones
                focusedStem_ = -1;
                for (int s = 0; s < static_cast<int>(stemWaveforms_.size()); ++s)
                    stemWaveforms_[static_cast<size_t>(s)]->setVisible(
                        removedStems_.find(s) == removedStems_.end());
                juce::Logger::writeToLog("[SModePage] Unfocused — showing all non-removed stems");
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
                if (smodeService_)
                    smodeService_->getModel().pushUndoState("Remove stem");
                stemWaveforms_[static_cast<size_t>(stemIdx)]->setVisible(false);
                removedStems_.insert(stemIdx);

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
        stemWaveforms_[static_cast<size_t>(i)]->computeSpectralColorsAsync();
    }

    // Synchronize model link state with LINK ALL default so L button visuals are accurate.
    for (int i = 0; i < stemCount; ++i)
        model.setLinked(i, linkAllActive_);
    for (auto& w : stemWaveforms_)
        if (w) w->refreshLinkButton();

    juce::Logger::writeToLog("✅ [SModePage] Built " + juce::String(stemCount) + " stem waveform components");
    resized();
    repaint();

    juce::MessageManager::callAsync([this]() {
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
    });

    // Auto-open with one sequencer lane if none exist
    if (sequencerLanes_.empty())
        addSequencerLane();

    // Run BPM detection on the drum stem (tightest tempo reference)
    runBpmDetectionOnDrumStem();

    // Audio is loaded — hide the empty-state overlay
    loadAudioOverlay_.setVisible(false);

    // Show eject button now that audio is loaded
    audioLoaded_ = true;
    ejectBtn_.setVisible(true);
    ejectBtn_.toFront(false);
}

void SModePage::unloadCurrentAudio()
{
    juce::Logger::writeToLog("[SModePage] === unloadCurrentAudio START ===");

    // 1-3. Stop transport, kill voices, detach S-Mode model from MIDI
    if (auto* reg = samplar::ServiceLocator::getRegistry())
    {
        if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
        {
            juce::Logger::writeToLog("[SModePage]   stopping S-Mode transport + voices");
            midiSvc->stopSModeTransport();
            midiSvc->stopAllVoices();
            midiSvc->setSModeModel(nullptr);
        }
        else
        {
            juce::Logger::writeToLog("[SModePage] WARNING: midi.mapping service not found");
        }
    }

    // 4. Reset SModeService session
    if (smodeService_)
    {
        juce::Logger::writeToLog("[SModePage]   resetting SModeService");
        smodeService_->reset();
    }
    smodeService_ = nullptr;

    // 5. Clear UI state — remove all stem waveforms and sequencer lanes
    juce::Logger::writeToLog("[SModePage]   clearing " + juce::String(stemWaveforms_.size())
                              + " stem waveforms, " + juce::String(sequencerLanes_.size()) + " seq lanes");
    for (auto& w : stemWaveforms_)
        if (w) removeChildComponent(w.get());
    stemWaveforms_.clear();

    for (auto& l : sequencerLanes_)
        if (l) removeChildComponent(l.get());
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
    downbeatDetectionRequested_ = false;

    // Reflect "stopped" state on the big play button
    playBtn_.setPlaying(false);

    // Show empty-state overlay again
    audioLoaded_ = false;
    ejectBtn_.setVisible(false);
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

    if (stemWaveforms_.empty())
    {
        // Drop zone prompt — shown when no stems loaded yet
        auto centre = getLocalBounds().withHeight(getHeight() / 2);
        g.setColour(juce::Colours::grey.withAlpha(0.6f));
        g.setFont(juce::FontOptions(16.0f));
        g.drawText("Drop a song here or click LOAD SONG", centre, juce::Justification::centred);
    }

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

        // Calculate playhead X position from time
        // Use first waveform's view range for coordinate mapping
        auto& wf0 = *stemWaveforms_[0];
        double viewStart = wf0.getViewStartTime();
        double viewEnd = wf0.getViewEndTime();
        double viewDur = viewEnd - viewStart;

        if (viewDur > 0.0 && playheadTimeSec_ >= viewStart && playheadTimeSec_ <= viewEnd)
        {
            // Map time to X position in the strip (offset past control panel = 178px)
            double frac = (playheadTimeSec_ - viewStart) / viewDur;
            const int kStemControlW = 178;  // matches StemWaveformComponent's panel width
            int stripWaveX = playheadStripArea_.getX() + kStemControlW;
            int stripWaveW = playheadStripArea_.getWidth() - kStemControlW;
            int phX = stripWaveX + static_cast<int>(frac * stripWaveW);

            // Triangle marker (pointing down)
            juce::Path triangle;
            float tx = static_cast<float>(phX);
            float ty = static_cast<float>(playheadStripArea_.getY());
            float th = static_cast<float>(playheadStripArea_.getHeight());
            triangle.addTriangle(tx - 5.0f, ty, tx + 5.0f, ty, tx, ty + th);

            g.setColour(transportPlaying_ ? juce::Colours::lime : juce::Colours::white);
            g.fillPath(triangle);

            // Vertical line extending through all waveforms
            if (!stemWaveforms_.empty())
            {
                int lineTop = playheadStripArea_.getBottom();
                int lineBot = stemWaveforms_.back()->getBounds().getBottom();
                g.setColour(juce::Colours::white.withAlpha(0.6f));
                g.drawVerticalLine(phX, static_cast<float>(lineTop), static_cast<float>(lineBot));
            }
        }
    }
}

void SModePage::resized()
{
    auto area = getLocalBounds().reduced(8);

    // ── Header row (1500×72 top bar) ──────────────────────────────────
    trackNameLabel_.setVisible(false);
    bpmLabel_.setVisible(false);
    titleLabel_.setVisible(false);   // replaced by logoLabel_
    transportBtn_.setVisible(false); // replaced by playBtn_

    auto topBar = area.removeFromTop(72).reduced(14, 10);

    // Zone 1: Logo (130px)
    logoLabel_.setBounds(topBar.removeFromLeft(130));

    // Zone 2: Transport (260px) — PLAY (56×52) + record circle + snapshot circle
    {
        auto transportArea = topBar.removeFromLeft(152);
        playBtn_.setBounds(transportArea.removeFromLeft(56).withSizeKeepingCentre(56, 52));
        transportArea.removeFromLeft(8);
        loopBtn_.setBounds(transportArea.removeFromLeft(80).withSizeKeepingCentre(80, 36));
    }

    // Zone 3: Utility (230px) — Undo, Redo, DAW toggle
    {
        auto utilityArea = topBar.removeFromLeft(230);
        undoBtn_.setBounds(utilityArea.removeFromLeft(72).withSizeKeepingCentre(72, 36));
        utilityArea.removeFromLeft(6);
        redoBtn_.setBounds(utilityArea.removeFromLeft(72).withSizeKeepingCentre(72, 36));
        utilityArea.removeFromLeft(8);
        dawSyncBtn_.setBounds(utilityArea.removeFromLeft(70));
    }

    // Zone 4: Stem status indicators (180px)
    {
        // (Stem status indicators removed — MainWindow has these in its top strip.)
    }

    // Zone 5: Grid division combo (was presetCombo_ — restored to divisionCombo_)
    divisionCombo_.setBounds(topBar.removeFromLeft(100).withSizeKeepingCentre(96, 36));
    divisionCombo_.toFront(false);  // ensure popup renders above siblings

    // Zone 6: BPM (150px) — label + input + R button
    {
        auto bpmArea = topBar.removeFromLeft(150);
        bpmTitleLabel_.setBounds(bpmArea.removeFromLeft(40));
        bpmInput_.setBounds(bpmArea.removeFromLeft(72).withSizeKeepingCentre(72, 36));
        bpmArea.removeFromLeft(4);
        bpmRoundBtn_.setBounds(bpmArea.removeFromLeft(28).withSizeKeepingCentre(28, 28));
    }

    // Zone 7: Mode buttons (260px) — Half, Dbl, LOCK, TAP
    {
        auto modeArea = topBar.removeFromLeft(260);
        halfTempoBtn_.setBounds(modeArea.removeFromLeft(54).withSizeKeepingCentre(54, 32));
        modeArea.removeFromLeft(4);
        doubleTempoBtn_.setBounds(modeArea.removeFromLeft(48).withSizeKeepingCentre(48, 32));
        modeArea.removeFromLeft(4);
        gridLockBtn_.setBounds(modeArea.removeFromLeft(60).withSizeKeepingCentre(60, 32));
        modeArea.removeFromLeft(4);
        tapTempoBtn_.setBounds(modeArea.removeFromLeft(54).withSizeKeepingCentre(54, 32));
    }

    // Zone 8: LOAD SONG + Eject (~190px remainder)
    {
        auto rightArea = topBar.reduced(0, 6);
        // Eject button on the far right (32×32), same row as LOAD SONG
        auto ejectSlot = rightArea.removeFromRight(32);
        ejectBtn_.setBounds(ejectSlot.withSizeKeepingCentre(32, 32));
        rightArea.removeFromRight(4);  // small gap
        loadSongBtn_.setBounds(rightArea);
    }

    // divisionCombo_ is now visible in Zone 5 above — no off-screen override needed.

    // Flash label: bottom-left corner toast for undo/redo confirmations.
    {
        auto bounds = getLocalBounds();
        undoStatusLabel_.setBounds(bounds.getX() + 8, bounds.getBottom() - 26, 280, 20);
    }

    area.removeFromTop(4);

    // ── Bottom row: Smart Fill + Drag Wave ──────────────────────────
    auto midiRow = area.removeFromBottom(28);
    dragSeqWavArea_.setBounds(midiRow.removeFromRight(100));
    midiRow.removeFromRight(4);
    smartFillBtn_.setBounds(midiRow.removeFromLeft(80));

    exportBtn_.setVisible(false);

    area.removeFromBottom(4);

    // ── Split remaining area: playhead strip + waveforms + controller ──
    if (!stemWaveforms_.empty())
    {
        // Playhead strip (14px above waveforms)
        playheadStripArea_ = area.removeFromTop(14);
        area.removeFromTop(2);

        int stemCount = static_cast<int>(stemWaveforms_.size());
        int numSeqLanes = static_cast<int>(sequencerLanes_.size());
        // Scale lane height: 1 lane = tall (like a channel), more lanes = smaller
        int maxSeqH = area.getHeight() / 2;  // Max half the total area for sequencer
        int seqLaneH = numSeqLanes > 0
            ? juce::jlimit(50, 140, (maxSeqH - 28) / juce::jmax(1, numSeqLanes))
            : 62;
        int seqTotalH = numSeqLanes > 0 ? (numSeqLanes * seqLaneH + (numSeqLanes - 1) * 3 + 28) : 24;

        // No controller area — sequencer lanes use all remaining space below waveforms
        int totalH = area.getHeight();
        int waveformAreaH = totalH - seqTotalH - 8;

        auto waveArea = area.removeFromTop(waveformAreaH);
        area.removeFromTop(4);

        // Sequencer area (uses remaining space)
        auto seqArea = area;
        auto seqBtnRow = seqArea.removeFromTop(24);
        // Clear All at far right (red)
        clearAllBtn_.setBounds(seqBtnRow.removeFromRight(58));
        seqBtnRow.removeFromRight(4);
        // Left section: +SEQ | Play All | Auto | Quick Assign | Poly/1SH | LINK ALL | Snap | Free | Tips
        addSeqBtn_.setBounds(seqBtnRow.removeFromLeft(55));
        seqBtnRow.removeFromLeft(3);
        playAllSeqBtn_.setBounds(seqBtnRow.removeFromLeft(55));
        seqBtnRow.removeFromLeft(5);
        autoBeatBtn_.setBounds(seqBtnRow.removeFromLeft(95));
        seqBtnRow.removeFromLeft(4);
        quickAssignBtn_.setBounds(seqBtnRow.removeFromLeft(80));
        seqBtnRow.removeFromLeft(3);
        monoPolyBtn_.setBounds(seqBtnRow.removeFromLeft(75));
        seqBtnRow.removeFromLeft(3);
        linkAllBtn_.setBounds(seqBtnRow.removeFromLeft(65));
        seqBtnRow.removeFromLeft(6);
        snapToggle_.setBounds(seqBtnRow.removeFromLeft(62));
        seqBtnRow.removeFromLeft(2);
        freeformToggle_.setBounds(seqBtnRow.removeFromLeft(52));
        seqBtnRow.removeFromLeft(4);
        fadeToggle_.setBounds(seqBtnRow.removeFromLeft(52));
        seqBtnRow.removeFromLeft(4);
        tooltipToggle_.setBounds(seqBtnRow.removeFromLeft(52));

        for (int i = 0; i < numSeqLanes; ++i)
        {
            seqArea.removeFromTop(3);
            sequencerLanes_[static_cast<size_t>(i)]->setBounds(seqArea.removeFromTop(seqLaneH));
        }

        controllerArea_ = {};

        // Stack visible stems in the waveform area (focus mode hides others)
        int visibleCount = 0;
        for (auto& w : stemWaveforms_)
            if (w->isVisible()) ++visibleCount;
        if (visibleCount == 0) visibleCount = 1;

        int gap = 4;
        int totalGaps = (visibleCount - 1) * gap;
        int stemH = (waveArea.getHeight() - totalGaps) / visibleCount;

        for (int i = 0; i < stemCount; ++i)
        {
            if (!stemWaveforms_[static_cast<size_t>(i)]->isVisible())
                continue;
            stemWaveforms_[static_cast<size_t>(i)]->setBounds(waveArea.removeFromTop(stemH));
            waveArea.removeFromTop(gap);
        }

        controllerAreaLabel_.setBounds(juce::Rectangle<int>());
    }
    else
    {
        controllerArea_ = {};
    }

    // MIDI learn overlay covers the entire page
    midiLearnOverlay_.setBounds(getLocalBounds());

    // Empty-state overlay covers the entire page (z-order: on top of everything)
    loadAudioOverlay_.setBounds(getLocalBounds());
    loadAudioOverlay_.toFront(false);

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

// ── Auto-Segmentation ────────────────────────────────────────────────────────

void SModePage::autoSegmentByDivision(double beatsPerSegment)
{
    if (!smodeService_) return;
    auto& model = smodeService_->getModel();
    double bpm = model.getDetectedTempo();

    if (bpm <= 0.0)
    {
        bpm = 120.0;
        model.setDetectedTempo(bpm);
        bpmLabel_.setText("BPM: 120 (default)", juce::dontSendNotification);
    }

    double sampleRate = model.getSampleRate();
    double segmentDuration = (60.0 / bpm) * beatsPerSegment;
    int64_t segmentSamples = static_cast<int64_t>(segmentDuration * sampleRate);

    // Use the grid offset so segments align with visible grid lines
    double gridOffset = 0.0;
    if (!stemWaveforms_.empty())
        gridOffset = stemWaveforms_[0]->getGridOffsetSeconds();
    int64_t offsetSamples = juce::jlimit((int64_t)0,
                                          (int64_t)(sampleRate * 600),
                                          static_cast<int64_t>(gridOffset * sampleRate));

    model.pushUndoState("Auto-segment");

    for (int stemIdx = 0; stemIdx < model.getStemCount(); ++stemIdx)
    {
        model.clearSegments(stemIdx);
        auto* stem = model.getStem(stemIdx);
        if (!stem || stem->audioBuffer.getNumSamples() == 0) continue;

        int64_t totalSamples = stem->audioBuffer.getNumSamples();

        // Auto-segment STRICTLY from the grid (first downbeat), not song start.
        // Anything before the grid offset (intro / silence) is intentionally left
        // without segments — it is not musical grid territory.
        int64_t pos = juce::jlimit((int64_t) 0, totalSamples, offsetSamples);
        while (pos + segmentSamples <= totalSamples)
        {
            model.addSegment(stemIdx, pos, pos + segmentSamples);
            pos += segmentSamples;
        }
        // Tail: include any remainder > 25% of a full segment as a partial final segment.
        if (totalSamples - pos > segmentSamples / 4)
            model.addSegment(stemIdx, pos, totalSamples);
    }

    juce::Logger::writeToLog("✅ [SModePage] Auto-segment: " + juce::String(beatsPerSegment)
                             + " beats/seg at " + juce::String(bpm, 1) + " BPM");

    for (auto& wf : stemWaveforms_)
    {
        wf->setGridBPM(bpm);
        wf->repaint();
    }
}

void SModePage::autoSegmentByTransient()
{
    if (!smodeService_) return;
    auto& model = smodeService_->getModel();
    model.pushUndoState("Auto-segment transient");

    // Snapshot stem audio data into thread-safe copies (mono mix per stem)
    struct StemSnapshot {
        int stemIdx;
        std::vector<float> mono;
        int totalSamples;
        double sampleRate;
    };

    double sampleRate = model.getSampleRate();
    std::vector<StemSnapshot> snapshots;

    for (int si = 0; si < model.getStemCount(); ++si)
    {
        auto* stem = model.getStem(si);
        if (!stem || stem->audioBuffer.getNumSamples() == 0) continue;
        const auto& buf = stem->audioBuffer;
        int numSmp = buf.getNumSamples();
        int numCh = buf.getNumChannels();

        // Mix to mono for spectral analysis
        std::vector<float> mono(numSmp, 0.0f);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* ptr = buf.getReadPointer(ch);
            for (int i = 0; i < numSmp; ++i)
                mono[i] += ptr[i];
        }
        if (numCh > 1)
            for (auto& v : mono) v /= static_cast<float>(numCh);

        snapshots.push_back({ si, std::move(mono), numSmp, sampleRate });
        model.clearSegments(si);
    }

    autoTransientBtn_.setEnabled(false);
    autoTransientBtn_.setButtonText("Analyzing...");

    std::thread([this, snapshots = std::move(snapshots)]() mutable
    {
        // Spectral flux transient detection (4-stage pipeline)
        samplar::SpectralFluxDetector detector;
        // Per-stem: map stemIdx → list of {start, end} pairs
        std::vector<std::pair<int, std::vector<std::pair<int64_t, int64_t>>>> results;

        for (auto& snap : snapshots)
        {
            auto onsets = detector.detect(snap.mono.data(), snap.totalSamples, snap.sampleRate);

            // Always include 0 as first onset if not present
            if (onsets.empty() || onsets.front() != 0)
                onsets.insert(onsets.begin(), 0);

            // Convert onsets to segment pairs
            std::vector<std::pair<int64_t, int64_t>> segs;
            for (size_t i = 0; i + 1 < onsets.size(); ++i)
                segs.push_back({ onsets[i], onsets[i + 1] });
            if (!onsets.empty())
                segs.push_back({ onsets.back(), static_cast<int64_t>(snap.totalSamples) });

            results.push_back({ snap.stemIdx, std::move(segs) });
        }

        // Apply results back on message thread
        juce::MessageManager::callAsync([this, results = std::move(results)]()
        {
            if (!smodeService_) return;
            auto& model = smodeService_->getModel();
            for (const auto& [si, segs] : results)
                for (const auto& [s, e] : segs)
                    model.addSegment(si, s, e);

            autoTransientBtn_.setEnabled(true);
            autoTransientBtn_.setButtonText("Auto: Transient");
            juce::Logger::writeToLog("✅ [SModePage] Auto-segment: spectral flux transient detection (async)");
            for (auto& wf : stemWaveforms_) wf->repaint();
        });
    }).detach();
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

void SModePage::applyBpm(double bpm)
{
    juce::Logger::writeToLog("[SModePage] applyBpm: " + juce::String(bpm, 4));

    // Stop the "..." dots animation now that we have a real BPM to display.
    bpmDotsAnim_.reset();

    // Push undo BEFORE any mutation. Guarded against re-entrancy inside the model.
    if (smodeService_)
        smodeService_->getModel().pushUndoState(
            gridLocked_ && lockedBpm_ > 0.0 ? juce::String("BPM transpose") : juce::String("BPM change"));

    if (gridLocked_ && lockedBpm_ > 0.0)
    {
        // Grid is locked — BPM change becomes a transpose
        // Ratio: newBpm / lockedBpm → semitone shift via 12 * log2(ratio)
        double ratio = bpm / lockedBpm_;
        double semitones = 12.0 * std::log2(ratio);
        int semiShift = static_cast<int>(std::round(semitones));

        juce::Logger::writeToLog("[SModePage] Grid LOCKED transpose: ratio=" + juce::String(ratio, 4)
                                 + " semitones=" + juce::String(semiShift));

        // Apply pitch shift to all segments across all stems
        if (smodeService_)
        {
            auto& model = smodeService_->getModel();
            for (int s = 0; s < model.getStemCount(); ++s)
            {
                auto* stem = model.getStem(s);
                if (!stem) continue;
                for (int seg = 0; seg < static_cast<int>(stem->segments.size()); ++seg)
                {
                    auto fx = model.getSegmentEffects(s, seg);
                    fx.pitchSemitones = semiShift;
                    model.setSegmentEffects(s, seg, fx);
                }
            }
        }

        // Update display but do NOT move the grid
        detectedBpm_ = bpm;
        bpmInput_.setText(juce::String(bpm, 2), juce::dontSendNotification);
        bpmLabel_.setText("BPM: " + juce::String(bpm, 2) + " (T" + juce::String(semiShift > 0 ? "+" : "")
                          + juce::String(semiShift) + "st)",
                          juce::dontSendNotification);

        // Repaint waveforms (segments may show pitch badge) but keep grid at locked BPM
        for (auto& wf : stemWaveforms_)
            wf->repaint();

        return;
    }

    // Normal (unlocked) — move the grid
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

    // ── Auto downbeat (beat 1) detection ────────────────────────────────
    // One-shot per stem load: kick off background detection only if we
    // haven't already computed a grid offset for this session and the BPM
    // looks usable. The result lands via the "smode.downbeat.detected"
    // event subscribed in onStemsReady().
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

// ── Loop playback (Alt+Click) ─────────────────────────────────────────────

void SModePage::playLoopedSegment(int stemIdx, int segIdx)
{
    if (!looping_ || !smodeService_) return;

    auto& model = smodeService_->getModel();
    auto* stem = model.getStem(stemIdx);
    auto* seg  = model.getSegment(stemIdx, segIdx);
    if (!stem || !seg || seg->getLengthSamples() <= 0) { stopLoop(); return; }

    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) { stopLoop(); return; }
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (!midiSvc) { stopLoop(); return; }

    midiSvc->stopAllVoices();

    const auto& srcBuf = stem->audioBuffer;

    double sr = model.getSampleRate();
    int64_t startSmp = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
    int64_t endSmp   = juce::jlimit(startSmp,   (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);
    int len = static_cast<int>(endSmp - startSmp);
    if (len <= 0) { stopLoop(); return; }

    auto segBuf = std::make_shared<juce::AudioBuffer<float>>(srcBuf.getNumChannels(), len);
    for (int ch = 0; ch < srcBuf.getNumChannels(); ++ch)
        segBuf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(startSmp), len);

    // Use voice-level looping (seamless wrap in audio engine) instead of
    // timer-based re-triggering. Realtime pitch is applied via stemIdx.
    const float pitchSemi = midiSvc->getStemPitchSemitones(stemIdx);
    juce::Logger::writeToLog("[SModePage] playSegment stem=" + juce::String(stemIdx)
        + " pitch=" + juce::String(pitchSemi, 2));
    midiSvc->playBufferDirect(segBuf, sr, 1.0f, true, stemIdx);

    double durMs = (static_cast<double>(len) / sr) * 1000.0;

    // Animate the segment
    if (stemIdx < static_cast<int>(stemWaveforms_.size()))
        stemWaveforms_[static_cast<size_t>(stemIdx)]->startSegmentAnimation(segIdx, durMs / 1000.0);
}

void SModePage::stopLoop()
{
    looping_ = false;
    loopStemIdx_ = -1;
    loopSegIdx_ = -1;

    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return;
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (midiSvc) midiSvc->stopAllVoices();
    juce::Logger::writeToLog("[SModePage] Loop STOP");
}

// ── Transport ─────────────────────────────────────────────────────────────

void SModePage::startTransport(double startTimeSeconds)
{
    if (auto* reg = samplar::ServiceLocator::getRegistry())
    {
        if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
        {
            // Stop any segment-click / MIDI-triggered voices that are already playing
            // so the global transport is the only audio source (Issue 1 fix).
            midiSvc->stopAllVoices();
            midiSvc->startSModeTransport(startTimeSeconds);
            transportPlaying_ = true;
            playBtn_.setPlaying(true);
            transportBtn_.setButtonText(juce::String::charToString(0x25A0));  // ■ stop
            transportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
            startTimerHz(30);  // 30fps playhead update
            juce::Logger::writeToLog("[SModePage] Transport START at " + juce::String(startTimeSeconds, 2) + "s");
        }
    }
}

void SModePage::stopTransport()
{
    if (auto* reg = samplar::ServiceLocator::getRegistry())
    {
        if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
            midiSvc->stopSModeTransport();
    }

    transportPlaying_ = false;
    playBtn_.setPlaying(false);
    transportBtn_.setButtonText(juce::String::charToString(0x25B6));  // ▶ play
    transportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));
    stopTimer();

    // Update playhead on all waveforms
    for (auto& wf : stemWaveforms_)
        wf->setPlayheadTime(playheadTimeSec_);
    repaint();

    juce::Logger::writeToLog("[SModePage] Transport STOP");
}

void SModePage::timerCallback()
{
    if (!transportPlaying_) return;

    // Poll transport position from MidiMappingService
    if (auto* reg = samplar::ServiceLocator::getRegistry())
    {
        if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
        {
            if (!midiSvc->isSModeTransportActive())
            {
                stopTransport();
                return;
            }
            playheadTimeSec_ = midiSvc->getSModeTransportPositionSeconds();
        }
    }

    // Update playhead on all waveforms
    for (auto& wf : stemWaveforms_)
        wf->setPlayheadTime(playheadTimeSec_);
    repaint();
}

void SModePage::setPlayheadFromMouse(int x)
{
    if (stemWaveforms_.empty() || playheadStripArea_.isEmpty()) return;

    // Convert strip X to time using first waveform's view range
    const int kStemControlW = 178;  // matches StemWaveformComponent's panel width
    int stripWaveX = playheadStripArea_.getX() + kStemControlW;
    int stripWaveW = playheadStripArea_.getWidth() - kStemControlW;
    if (stripWaveW <= 0) return;

    double frac = static_cast<double>(x - stripWaveX) / stripWaveW;
    frac = juce::jlimit(0.0, 1.0, frac);

    double viewStart = stemWaveforms_[0]->getViewStartTime();
    double viewEnd = stemWaveforms_[0]->getViewEndTime();
    playheadTimeSec_ = viewStart + frac * (viewEnd - viewStart);

    for (auto& wf : stemWaveforms_)
        wf->setPlayheadTime(playheadTimeSec_);
    repaint();
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
            if (auto* reg = samplar::ServiceLocator::getRegistry())
            {
                if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
                    midiSvc->startSModeTransport(playheadTimeSec_);
            }
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

    // Computer keyboard MIDI — route unmodified key presses to MidiMappingService.
    // JUCE's keyPressed() fires on OS autorepeat (~30Hz while held); without a
    // dedup guard this would machine-gun handleNoteOn and bypass POLY/MONO/1SH/
    // Gate voice logic (which assumes one noteOn per physical press).
    // Route through MidiMappingService::handleKeyPress which owns the held-set
    // dedup, so the voice state machine sees exactly one noteOn per press and
    // keyStateChanged() below drives exactly one noteOff per release.
    if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown())
    {
        if (auto* reg = samplar::ServiceLocator::getRegistry())
        {
            if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
            {
                int note = samplar::MidiMappingService::keyToMidiNote(key);
                if (note >= 0 && midiSvc->isComputerKeyboardEnabled())
                {
                    // Track locally too so the existing keyStateChanged release
                    // loop continues to work for any keys released before the
                    // service's polled release ticks.
                    heldKeyboardNotes_.insert(note);
                    midiSvc->handleKeyPress(key);  // dedup'd; fires noteOn only on first press
                    return true;
                }
            }
        }
    }

    return false;
}

bool SModePage::keyStateChanged(bool /*isKeyDown*/, juce::Component* /*originatingComponent*/)
{
    // Check all held notes — if the key is no longer pressed, send note-off
    if (heldKeyboardNotes_.empty()) return false;

    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return false;
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (!midiSvc || !midiSvc->isComputerKeyboardEnabled()) return false;

    // Build a KeyPress for each held note and check if it's still down
    std::vector<int> released;
    for (int note : heldKeyboardNotes_)
    {
        // Reverse-map note to key code to check if still held
        // We check all possible key codes that map to this note
        bool stillHeld = false;
        static const int allKeys[] = {
            'Z','S','X','D','C','V','G','B','H','N','J','M',
            'Q','2','W','3','E','R','5','T','6','Y','7','U'
        };
        for (int k : allKeys)
        {
            if (samplar::MidiMappingService::keyToMidiNote(juce::KeyPress(k)) == note)
            {
                if (juce::KeyPress::isKeyCurrentlyDown(k))
                {
                    stillHeld = true;
                    break;
                }
            }
        }
        if (!stillHeld)
            released.push_back(note);
    }

    for (int note : released)
    {
        heldKeyboardNotes_.erase(note);
        // Route through the service's polled release so its held-flag stays
        // coherent with ours (prevents a stale flag blocking a later re-press).
        // releaseUnheldComputerKeys will fire handleNoteOff(1, note) for this
        // note since its key is no longer physically down.
        (void)note;
    }
    midiSvc->releaseUnheldComputerKeys();

    return !released.empty();
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
    double gridOffset = 0.0;
    if (stemIndex >= 0 && stemIndex < static_cast<int>(stemWaveforms_.size()))
        gridOffset = stemWaveforms_[static_cast<size_t>(stemIndex)]->getGridOffsetSeconds();
    int64_t offsetSamples = juce::jlimit((int64_t)0,
                                          (int64_t)(sampleRate * 600),
                                          static_cast<int64_t>(gridOffset * sampleRate));

    int nextNote = 36;  // Start MIDI assignment from C1
    for (int si : stemsToProcess)
    {
        model.clearSegments(si);
        auto* stem = model.getStem(si);
        if (!stem || stem->audioBuffer.getNumSamples() == 0) continue;

        int64_t total = stem->audioBuffer.getNumSamples();

        // Pre-grid-offset segment
        if (offsetSamples > 0 && offsetSamples < total)
        {
            int segIdx = model.addSegment(si, 0, offsetSamples);
            if (segIdx >= 0 && nextNote <= 127)
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
            if (segIdx >= 0 && nextNote <= 127)
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
            if (segIdx >= 0 && nextNote <= 127)
            {
                auto& seg = stem->segments[static_cast<size_t>(segIdx)];
                if (segmentHasAudio(*stem, seg))
                    model.assignMidiNote(si, segIdx, nextNote++);
            }
        }
    }

    juce::Logger::writeToLog("✅ [SModePage] Grid Quick Assign: " + juce::String(beatsPerSegment)
                             + " beats at " + juce::String(bpm, 1) + " BPM on "
                             + juce::String(stemsToProcess.size()) + " stems");

    for (auto& wf : stemWaveforms_)
    {
        wf->setGridBPM(bpm);
        wf->repaint();
    }
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
        applyBpm(cachedBpm);
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

    // Pass raw pointers to background thread — model is processor-lifetime,
    // buffers won't be deleted while we read. Avoids blocking the message
    // thread with a multi-MB buffer copy.
    const float* const* readPtrs = drumStem->audioBuffer.getArrayOfReadPointers();
    int numChannels = drumStem->audioBuffer.getNumChannels();
    int numSamples = drumStem->audioBuffer.getNumSamples();
    double sampleRate = model.getSampleRate();

    std::thread([this, readPtrs, numChannels, numSamples, sampleRate]()
    {
        // Build a temporary AudioBuffer from the raw read-pointers (no copy of the
        // sample data — just a referencing wrapper is not available on AudioBuffer,
        // so we do a real copy here; drum stems are typically 2ch / < 10 min).
        juce::AudioBuffer<float> buf(numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            buf.copyFrom(ch, 0, readPtrs[ch], numSamples);

        samplar::HybridTempoDetector detector;
        samplar::HybridTempoDetector::Config cfg;
        cfg.minBPM = 50.0f;
        cfg.maxBPM = 220.0f;
        auto result = detector.detect(buf, sampleRate, cfg);

        double bpm = (result.bpm > 0.0f) ? static_cast<double>(result.bpm) : 120.0;
        juce::Logger::writeToLog("✅ [SModePage] HybridTempoDetector: "
            + juce::String(bpm, 2) + " BPM (confidence="
            + juce::String(result.confidence, 2) + " source=" + result.getSourceName() + ")");

        // Auto-round the detected BPM to the nearest integer if the user
        // setting is enabled. The full pipeline (Beat This! → TempoRefiner)
        // lands within ~0.2% of true, and modern productions almost
        // universally use integer BPM — saves the user a manual round-button
        // press for every load. Manual entry / host sync / tap tempo bypass
        // this entirely and keep full precision (they call applyBpm directly
        // with the user's value). Toggled in Settings → Audio.
        const bool roundIt = samplar::SamplarSettings::getInstance().getAutoRoundBpm();
        const double finalBpm = roundIt ? std::round(bpm) : bpm;
        juce::Logger::writeToLog(juce::String("[SModePage] Auto-round ")
            + (roundIt ? "ON" : "OFF") + ": "
            + juce::String(bpm, 4) + " -> " + juce::String(finalBpm, 4));
        juce::MessageManager::callAsync([this, finalBpm]()
        {
            if (!smodeService_) return;
            applyBpm(finalBpm);
        });
    }).detach();
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
    lane->onLaneChanged = [this, lanePtr](int /*idx*/)
    {
        // If playback is active, restart with updated data
        static bool restarting = false;
        if (restarting) return;
        if (!seqPlayingFlag_ || !seqPlayingFlag_->load()) return;

        restarting = true;

        if (activeSeqLane_ == -2)
        {
            // Play All mode — retrigger simultaneous playback
            playAllLanesSimultaneous();
            playAllSeqBtn_.setButtonText("Stop");
            playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d2d2d));
        }
        else if (activeSeqLane_ >= 0)
        {
            // Single lane mode — retrigger if it's this lane
            for (int i = 0; i < static_cast<int>(sequencerLanes_.size()); ++i)
            {
                if (sequencerLanes_[static_cast<size_t>(i)].get() == lanePtr && i == activeSeqLane_)
                {
                    playSequencerLane(i);
                    break;
                }
            }
        }

        restarting = false;
    };
    // Wire the WaveDragArea render callback for this lane
    lane->getDragWavArea().onRenderPattern = [this, lanePtr](double& outSampleRate,
        std::function<void(float)> progressCb) -> std::unique_ptr<juce::AudioBuffer<float>>
    {
        if (!smodeService_) return nullptr;
        auto& model = smodeService_->getModel();
        double modelSr = model.getSampleRate();
        if (modelSr <= 0.0) modelSr = 44100.0;
        outSampleRate = modelSr;
        double bpm = detectedBpm_ > 0.0 ? detectedBpm_ : 120.0;
        double samplesPerBeat = (60.0 / bpm) * modelSr;

        SequencerLaneComponent* lane2 = nullptr;
        for (auto& l : sequencerLanes_)
            if (l.get() == lanePtr) { lane2 = l.get(); break; }
        if (!lane2) return nullptr;

        int totalCells = lane2->getNumBars() * 4;
        int64_t totalSmp = static_cast<int64_t>(totalCells * samplesPerBeat);
        if (totalSmp <= 0) return nullptr;

        int numCh = 2;
        for (const auto& slot : lane2->getSlots())
        {
            if (slot.empty) continue;
            auto* stem = model.getStem(slot.stemIndex);
            if (stem) { numCh = stem->audioBuffer.getNumChannels(); break; }
        }

        auto buf = std::make_unique<juce::AudioBuffer<float>>(numCh, static_cast<int>(totalSmp));
        buf->clear();

        // (Realtime pitch path: pre-rendered Play-All-mix uses raw stem buffers.
        // The mix path itself is pre-rendered on the message thread so pitch
        // is NOT applied to the offline mix. TODO: pre-mix-path realtime pitch.)
        auto& slots = lane2->getSlots();
        for (int i = 0; i < static_cast<int>(slots.size()); ++i)
        {
            const auto& slot = slots[static_cast<size_t>(i)];
            if (slot.empty) continue;
            auto* stem = model.getStem(slot.stemIndex);
            auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
            if (!stem || !seg || seg->getLengthSamples() <= 0) continue;

            const auto& srcBuf = stem->audioBuffer;

            int64_t segStart = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
            int64_t segEnd   = juce::jlimit(segStart,   (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);
            int64_t segLen = segEnd - segStart;

            int64_t writePos = static_cast<int64_t>(slot.gridCell * samplesPerBeat);
            int64_t cellDur = static_cast<int64_t>(slot.cellSpan * samplesPerBeat);
            int64_t trimStart = segStart + static_cast<int64_t>(slot.trimStart * segLen);
            int64_t trimEnd = segStart + static_cast<int64_t>(slot.trimEnd * segLen);
            trimStart = juce::jlimit(segStart, segEnd, trimStart);
            trimEnd = juce::jlimit(trimStart + 1, segEnd, trimEnd);
            int copyLen = static_cast<int>(juce::jmin(trimEnd - trimStart, cellDur));

            if (copyLen <= 0 || writePos + copyLen > totalSmp) continue;
            int chCopy = juce::jmin(numCh, srcBuf.getNumChannels());
            for (int ch = 0; ch < chCopy; ++ch)
                buf->addFrom(ch, static_cast<int>(writePos), srcBuf, ch,
                              static_cast<int>(trimStart), copyLen, slot.velocity);

            if (progressCb) progressCb(static_cast<float>(i + 1) / static_cast<float>(slots.size()));
        }

        return buf;
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

        auto* reg = samplar::ServiceLocator::getRegistry();
        if (!reg) return;
        auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
        if (!midiSvc) return;

        stopLoop();
        if (transportPlaying_) stopTransport();
        midiSvc->stopAllVoices();

        auto* stem = mdl.getStem(stemIdx);
        auto* seg = mdl.getSegment(stemIdx, segIdx);
        if (!stem || !seg || seg->getLengthSamples() <= 0 || stem->mute) return;

        double modelSr = mdl.getSampleRate();
        if (modelSr <= 0.0) modelSr = 44100.0;

        const auto& srcBuf = stem->audioBuffer;

        int64_t startSmp = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
        int64_t endSmp   = juce::jlimit(startSmp,    (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);
        int segLen = static_cast<int>(endSmp - startSmp);
        if (segLen <= 0) return;

        auto segBuf = std::make_shared<juce::AudioBuffer<float>>(srcBuf.getNumChannels(), segLen);
        for (int ch = 0; ch < srcBuf.getNumChannels(); ++ch)
            segBuf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(startSmp), segLen);

        juce::Logger::writeToLog("[SModePage] sequencer slot play stem=" + juce::String(stemIdx)
            + " (pitch bypass — slot waveform shows untransposed audio)");
        // NO stemIndex passed → sequencer plays at native pitch, matching
        // the slot's displayed waveform. Otherwise realtime signalsmith pitch
        // is applied to playback while the waveform stays untransposed, so
        // "what you see" ≠ "what you hear". The stem transport + segment
        // direct-clicks still apply pitch (they don't have this visual lock).
        midiSvc->playBufferDirect(segBuf, modelSr, 1.0f, false);
    };

    addAndMakeVisible(lane.get());
    sequencerLanes_.push_back(std::move(lane));

    juce::Logger::writeToLog("[SModePage] Added sequencer lane " + juce::String(laneIdx));
    resized();
    repaint();
}

void SModePage::removeSequencerLane(int laneIndex)
{
    if (laneIndex < 0 || laneIndex >= static_cast<int>(sequencerLanes_.size())) return;

    removeChildComponent(sequencerLanes_[static_cast<size_t>(laneIndex)].get());
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

    // Cancel any previous sequencer playback chain
    if (seqPlayingFlag_) seqPlayingFlag_->store(false);
    seqPlayingFlag_ = std::make_shared<std::atomic<bool>>(true);
    activeSeqLane_ = laneIndex;

    auto& model = smodeService_->getModel();
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return;
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (!midiSvc) return;

    if (transportPlaying_) stopTransport();
    stopLoop();
    midiSvc->stopAllVoices();

    auto* lane = sequencerLanes_[static_cast<size_t>(laneIndex)].get();
    lane->setPlayingSlot(-1);
    const auto& slots = lane->getSlots();
    if (slots.empty()) return;

    double modelSr = model.getSampleRate();
    if (modelSr <= 0.0) modelSr = 44100.0;
    double bpm = detectedBpm_ > 0.0 ? detectedBpm_ : 120.0;
    double samplesPerBeat = (60.0 / bpm) * modelSr;

    struct SlotPlay {
        int slotIndex;   // -1 = silence gap
        int gridCell;
        std::shared_ptr<juce::AudioBuffer<float>> buffer;  // nullptr for gaps
        int durationMs;
        int stemIndex { -1 };  // For realtime SoundTouch pitch in voice path
    };
    std::vector<SlotPlay> schedule;

    // msPerCell = one beat duration
    double msPerCell = 60000.0 / bpm;
    int totalCells = 0;  // track for gap insertion

    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
    {
        const auto& slot = slots[static_cast<size_t>(i)];
        if (slot.empty) continue;

        auto* stem = model.getStem(slot.stemIndex);
        auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
        if (!stem || !seg || seg->getLengthSamples() <= 0) continue;

        // (Sequencer-schedule mix is pre-rendered on the message thread —
        // realtime pitch via voice path doesn't apply here. The single-segment
        // playback below uses the realtime path via stemIdx.)
        const auto& srcBuf = stem->audioBuffer;

        int64_t segStart = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
        int64_t segEnd   = juce::jlimit(segStart,   (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);

        juce::Logger::writeToLog("[SModePage] playSegment stem=" + juce::String(slot.stemIndex)
            + " (offline mix path, no realtime pitch)");

        // Output duration in samples = cellSpan beats
        int64_t cellDurSamples = static_cast<int64_t>(slot.cellSpan * samplesPerBeat);
        if (cellDurSamples <= 0) continue;

        int numCh = srcBuf.getNumChannels();

        // Apply trim: select portion of segment
        int64_t segLen = segEnd - segStart;
        int64_t trimSmpStart = segStart + static_cast<int64_t>(slot.trimStart * segLen);
        int64_t trimSmpEnd = segStart + static_cast<int64_t>(slot.trimEnd * segLen);
        trimSmpStart = juce::jlimit(segStart, segEnd, trimSmpStart);
        trimSmpEnd = juce::jlimit(trimSmpStart + 1, segEnd, trimSmpEnd);
        int64_t trimmedLen = trimSmpEnd - trimSmpStart;

        // Speed-stretch: resample trimmed audio to fill cell duration
        // speed=1 → trimmedLen as-is, padded/cropped to cellDur
        // speed<1 → stretch (slow), speed>1 → compress (fast)
        int outLen = static_cast<int>(cellDurSamples);
        int inLen = static_cast<int>(trimmedLen);
        auto buf = std::make_shared<juce::AudioBuffer<float>>(numCh, outLen);
        buf->clear();

        if (std::abs(slot.speed - 1.0f) > 0.01f && slot.speed > 0.0f)
        {
            // Resample: stretch or compress trimmed audio to fill cell
            double resampleRatio = static_cast<double>(inLen) / static_cast<double>(outLen);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* rd = srcBuf.getReadPointer(ch, static_cast<int>(trimSmpStart));
                float* wr = buf->getWritePointer(ch);
                for (int j = 0; j < outLen; ++j)
                {
                    double srcPos = j * resampleRatio;
                    int i0 = juce::jmin(static_cast<int>(srcPos), inLen - 1);
                    int i1 = juce::jmin(i0 + 1, inLen - 1);
                    float frac = static_cast<float>(srcPos - i0);
                    wr[j] = rd[i0] * (1.0f - frac) + rd[i1] * frac;
                }
            }
        }
        else
        {
            // No speed change: copy trimmed audio, pad with silence if shorter than cell
            int copyLen = static_cast<int>(juce::jmin(trimmedLen, cellDurSamples));
            for (int ch = 0; ch < numCh; ++ch)
                buf->copyFrom(ch, 0, srcBuf, ch, static_cast<int>(trimSmpStart), copyLen);
        }

        // Apply reverse effect
        if (slot.reverse)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* data = buf->getWritePointer(ch);
                std::reverse(data, data + buf->getNumSamples());
            }
        }

        // Apply pitch shift via resampling
        if (std::abs(slot.pitchSemitones) > 0.1f)
        {
            double ratio = std::pow(2.0, slot.pitchSemitones / 12.0);
            int origLen = buf->getNumSamples();
            int newLen = static_cast<int>(origLen / ratio);
            if (newLen > 0)
            {
                auto pitched = std::make_shared<juce::AudioBuffer<float>>(numCh, newLen);
                pitched->clear();
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float* src = buf->getReadPointer(ch);
                    float* dst = pitched->getWritePointer(ch);
                    for (int j = 0; j < newLen; ++j)
                    {
                        double srcPos = j * ratio;
                        int i0 = static_cast<int>(srcPos);
                        float fr = static_cast<float>(srcPos - i0);
                        int i1 = juce::jmin(i0 + 1, origLen - 1);
                        i0 = juce::jmin(i0, origLen - 1);
                        dst[j] = src[i0] * (1.0f - fr) + src[i1] * fr;
                    }
                }
                buf = pitched;
            }
        }

        // Apply velocity (volume)
        if (std::abs(slot.velocity - 1.0f) > 0.01f)
            buf->applyGain(slot.velocity);

        // Duration: cellSpan * msPerCell (integer cell timing)
        int durMs = static_cast<int>(slot.cellSpan * msPerCell);
        schedule.push_back({ i, slot.gridCell, buf, durMs, slot.stemIndex });
    }

    if (schedule.empty()) return;

    // Sort by grid position so playback matches visual order
    std::sort(schedule.begin(), schedule.end(),
              [](const SlotPlay& a, const SlotPlay& b) { return a.gridCell < b.gridCell; });

    // Insert silence gaps between slots so empty cells play as silence
    {
        std::vector<SlotPlay> withGaps;
        int nextCell = 0;
        for (const auto& sp : schedule)
        {
            if (sp.gridCell > nextCell)
            {
                // Gap: silence from nextCell to sp.gridCell
                int gapCells = sp.gridCell - nextCell;
                int gapMs = static_cast<int>(gapCells * msPerCell);
                withGaps.push_back({ -1, nextCell, nullptr, gapMs });
            }
            withGaps.push_back(sp);
            nextCell = sp.gridCell + static_cast<int>(sp.durationMs / msPerCell + 0.5);
        }
        schedule = std::move(withGaps);
    }

    auto schedulePtr = std::make_shared<std::vector<SlotPlay>>(std::move(schedule));
    auto cancelFlag = seqPlayingFlag_;
    auto playSlot = std::make_shared<std::function<void(int)>>();

    // Progress fill + playhead — ticks every 30ms
    // Signature: (slotIdx, totalMs, startTimeMs, gridCell, cellSpan, totalCells)
    auto progressTimer2 = std::make_shared<std::function<void(int, int, int64_t, int, int, int)>>();
    *progressTimer2 = [this, laneIndex, cancelFlag, progressTimer2](int slotIdx, int totalMs, int64_t startTimeMs, int gridCell, int cellSpan, int totalCells)
    {
        if (!cancelFlag->load())
        {
            if (laneIndex < static_cast<int>(sequencerLanes_.size()))
                sequencerLanes_[static_cast<size_t>(laneIndex)]->setPlayheadPosition(-1.0f);
            return;
        }
        if (laneIndex >= static_cast<int>(sequencerLanes_.size())) return;

        int64_t now = juce::Time::currentTimeMillis();
        int elapsed = static_cast<int>(now - startTimeMs);
        float progress = totalMs > 0 ? juce::jlimit(0.0f, 1.0f, static_cast<float>(elapsed) / totalMs) : 1.0f;

        // Update slot highlight
        if (slotIdx >= 0)
            sequencerLanes_[static_cast<size_t>(laneIndex)]->setPlayingSlot(slotIdx, progress);

        // Update lane-wide playhead: position within the full grid
        if (totalCells > 0)
        {
            float cellProgress = (static_cast<float>(gridCell) + progress * static_cast<float>(cellSpan))
                                  / static_cast<float>(totalCells);
            sequencerLanes_[static_cast<size_t>(laneIndex)]->setPlayheadPosition(cellProgress);
        }

        if (elapsed < totalMs && cancelFlag->load())
        {
            juce::Timer::callAfterDelay(30, [progressTimer2, slotIdx, totalMs, startTimeMs, gridCell, cellSpan, totalCells]()
            {
                (*progressTimer2)(slotIdx, totalMs, startTimeMs, gridCell, cellSpan, totalCells);
            });
        }
    };

    // Slot-by-slot playback with cancellation
    int totalGridCells = lane->getNumBars() * 4;
    *playSlot = [this, laneIndex, schedulePtr, modelSr, cancelFlag, playSlot, progressTimer2, totalGridCells](int idx)
    {
        if (!cancelFlag->load()) return;
        if (idx >= static_cast<int>(schedulePtr->size())) return;
        if (laneIndex >= static_cast<int>(sequencerLanes_.size())) return;

        auto* reg2 = samplar::ServiceLocator::getRegistry();
        if (!reg2) return;
        auto* ms = dynamic_cast<samplar::MidiMappingService*>(reg2->get("midi.mapping"));
        if (!ms) return;

        const auto& sp = (*schedulePtr)[static_cast<size_t>(idx)];
        int cellSpan = juce::jmax(1, static_cast<int>(std::round(sp.durationMs / (60000.0 / (detectedBpm_ > 0 ? detectedBpm_ : 120.0)))));

        if (sp.slotIndex >= 0 && sp.buffer)
        {
            ms->stopAllVoices();
            // Sequencer playback: NO stemIndex passed — keeps audio in sync
            // with the slot's displayed waveform (which is untransposed).
            ms->playBufferDirect(sp.buffer, modelSr, 1.0f, false);
            sequencerLanes_[static_cast<size_t>(laneIndex)]->setPlayingSlot(sp.slotIndex);
        }
        else
        {
            // Silence gap — stop voices, clear highlight
            ms->stopAllVoices();
            sequencerLanes_[static_cast<size_t>(laneIndex)]->setPlayingSlot(-1);
        }

        // Always run playhead timer (for both slots and gaps)
        int64_t startMs2 = juce::Time::currentTimeMillis();
        (*progressTimer2)(sp.slotIndex, sp.durationMs, startMs2, sp.gridCell, cellSpan, totalGridCells);

        // Chain to next slot/gap after this one's duration
        if (idx + 1 < static_cast<int>(schedulePtr->size()))
        {
            juce::Timer::callAfterDelay(sp.durationMs, [playSlot, cancelFlag, nextIdx = idx + 1]()
            {
                if (!cancelFlag->load()) return;
                (*playSlot)(nextIdx);
            });
        }
        else
        {
            // Loop: restart from slot 0
            juce::Timer::callAfterDelay(sp.durationMs, [playSlot, cancelFlag]()
            {
                if (!cancelFlag->load()) return;
                (*playSlot)(0);
            });
        }
    };

    (*playSlot)(0);
    juce::Logger::writeToLog("[SModePage] Playing seq lane " + juce::String(laneIndex)
                             + " (" + juce::String((int)schedulePtr->size()) + " slots)");
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

    auto& model = smodeService_->getModel();
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return;
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (!midiSvc) return;

    if (transportPlaying_) stopTransport();
    stopLoop();
    if (seqPlayingFlag_) seqPlayingFlag_->store(false);
    seqPlayingFlag_ = std::make_shared<std::atomic<bool>>(true);
    activeSeqLane_ = -2;  // -2 = Play All mode
    midiSvc->stopAllVoices();

    double modelSr = model.getSampleRate();
    if (modelSr <= 0.0) modelSr = 44100.0;
    double bpm = detectedBpm_ > 0.0 ? detectedBpm_ : 120.0;
    double samplesPerBeat = (60.0 / bpm) * modelSr;

    // Find max totalCells across all lanes and numChannels
    int maxCells = 0;
    int numCh = 2;
    for (auto& lane : sequencerLanes_)
    {
        int laneCells = lane->getNumBars() * 4;
        maxCells = juce::jmax(maxCells, laneCells);
        for (const auto& slot : lane->getSlots())
        {
            if (slot.empty) continue;
            auto* stem = model.getStem(slot.stemIndex);
            if (stem) { numCh = stem->audioBuffer.getNumChannels(); break; }
        }
    }
    if (maxCells <= 0) return;

    int64_t totalSamples = static_cast<int64_t>(maxCells * samplesPerBeat);
    auto mixBuf = std::make_shared<juce::AudioBuffer<float>>(numCh, static_cast<int>(totalSamples));
    mixBuf->clear();

    // Mix all lanes at their grid positions with trim/speed/velocity
    for (auto& lane : sequencerLanes_)
    {
        if (lane->isMuted()) continue;  // Skip muted lanes
        for (const auto& slot : lane->getSlots())
        {
            if (slot.empty) continue;
            auto* stem = model.getStem(slot.stemIndex);
            auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
            if (!stem || !seg || seg->getLengthSamples() <= 0) continue;

            // Play-All mix is pre-rendered offline → cannot use realtime
            // SoundTouch (mix sums multiple stems into one buffer). TODO:
            // either pre-mix-with-pitch on message thread, or replace this
            // path with a multi-voice playBufferDirect using stemIdx per slot.
            const auto& srcBuf = stem->audioBuffer;

            int64_t segStart = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
            int64_t segEnd   = juce::jlimit(segStart,   (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);
            int64_t segLen = segEnd - segStart;

            juce::Logger::writeToLog("[SModePage] mixAllLanes stem=" + juce::String(slot.stemIndex)
                + " (offline mix — pitch not applied)");

            // Grid position in samples
            int64_t writePos = static_cast<int64_t>(slot.gridCell * samplesPerBeat);
            int64_t cellDur = static_cast<int64_t>(slot.cellSpan * samplesPerBeat);

            // Apply trim
            int64_t trimStart = segStart + static_cast<int64_t>(slot.trimStart * segLen);
            int64_t trimEnd = segStart + static_cast<int64_t>(slot.trimEnd * segLen);
            trimStart = juce::jlimit(segStart, segEnd, trimStart);
            trimEnd = juce::jlimit(trimStart + 1, segEnd, trimEnd);
            int inLen = static_cast<int>(trimEnd - trimStart);
            int outLen = static_cast<int>(cellDur);

            if (outLen <= 0 || writePos + outLen > totalSamples) continue;
            int chCopy = juce::jmin(numCh, srcBuf.getNumChannels());

            if (std::abs(slot.speed - 1.0f) > 0.01f && slot.speed > 0.0f)
            {
                // Speed-stretch: resample trimmed audio to fill cell
                double ratio = static_cast<double>(inLen) / static_cast<double>(outLen);
                for (int ch = 0; ch < chCopy; ++ch)
                {
                    const float* rd = srcBuf.getReadPointer(ch, static_cast<int>(trimStart));
                    float* wr = mixBuf->getWritePointer(ch, static_cast<int>(writePos));
                    for (int j = 0; j < outLen; ++j)
                    {
                        double sp = j * ratio;
                        int i0 = juce::jmin(static_cast<int>(sp), inLen - 1);
                        int i1 = juce::jmin(i0 + 1, inLen - 1);
                        float f = static_cast<float>(sp - i0);
                        wr[j] += (rd[i0] * (1.0f - f) + rd[i1] * f) * slot.velocity;
                    }
                }
            }
            else
            {
                int copyLen = juce::jmin(inLen, outLen);
                for (int ch = 0; ch < chCopy; ++ch)
                    mixBuf->addFrom(ch, static_cast<int>(writePos), srcBuf, ch,
                                     static_cast<int>(trimStart), copyLen, slot.velocity);
            }
        }
    }

    midiSvc->playBufferDirect(mixBuf, modelSr, 1.0f);

    // Playhead animation across all lanes
    auto cancelFlag = seqPlayingFlag_;
    int totalMs = static_cast<int>(maxCells * 60000.0 / bpm);
    int64_t startMs = juce::Time::currentTimeMillis();

    auto playheadTick = std::make_shared<std::function<void()>>();
    auto startMsPtr = std::make_shared<int64_t>(startMs);
    *playheadTick = [this, cancelFlag, startMsPtr, totalMs, modelSr, mixBuf, playheadTick]()
    {
        if (!cancelFlag->load())
        {
            for (auto& lane : sequencerLanes_)
                lane->setPlayheadPosition(-1.0f);
            return;
        }

        int64_t now = juce::Time::currentTimeMillis();
        int elapsed = static_cast<int>(now - *startMsPtr);
        float progress = totalMs > 0 ? juce::jlimit(0.0f, 1.0f, static_cast<float>(elapsed) / totalMs) : 1.0f;

        for (auto& lane : sequencerLanes_)
            lane->setPlayheadPosition(progress);

        if (elapsed < totalMs)
        {
            juce::Timer::callAfterDelay(30, [playheadTick]() { (*playheadTick)(); });
        }
        else
        {
            // Loop: replay the buffer and reset the timer
            auto* reg3 = samplar::ServiceLocator::getRegistry();
            if (reg3)
            {
                auto* ms2 = dynamic_cast<samplar::MidiMappingService*>(reg3->get("midi.mapping"));
                if (ms2)
                {
                    ms2->stopAllVoices();
                    ms2->playBufferDirect(mixBuf, modelSr, 1.0f);
                }
            }
            *startMsPtr = juce::Time::currentTimeMillis();
            juce::Timer::callAfterDelay(30, [playheadTick]() { (*playheadTick)(); });
        }
    };

    juce::Timer::callAfterDelay(30, [playheadTick]() { (*playheadTick)(); });

    juce::Logger::writeToLog("[SModePage] Playing all " + juce::String((int)sequencerLanes_.size())
                             + " lanes simultaneously (" + juce::String(maxCells) + " cells)");
}

// ── Play Lanes Chained (sequential) ──────────────────────────────────────
void SModePage::playLanesChained()
{
    if (sequencerLanes_.empty() || !smodeService_) return;

    auto& model = smodeService_->getModel();
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return;
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (!midiSvc) return;

    if (transportPlaying_) stopTransport();
    midiSvc->stopAllVoices();

    double modelSr = model.getSampleRate();
    if (modelSr <= 0.0) modelSr = 44100.0;

    // Concatenate all lanes sequentially
    int64_t totalSamples = 0;
    int numCh = 2;

    for (auto& lane : sequencerLanes_)
    {
        for (const auto& slot : lane->getSlots())
        {
            if (slot.empty) continue;
            auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
            if (seg && seg->getLengthSamples() > 0)
                totalSamples += seg->getLengthSamples();

            auto* stem = model.getStem(slot.stemIndex);
            if (stem && numCh == 2) numCh = stem->audioBuffer.getNumChannels();
        }
    }

    if (totalSamples <= 0) return;

    auto chainBuf = std::make_shared<juce::AudioBuffer<float>>(numCh, static_cast<int>(totalSamples));
    chainBuf->clear();
    int writePos = 0;

    for (auto& lane : sequencerLanes_)
    {
        for (const auto& slot : lane->getSlots())
        {
            if (slot.empty) continue;
            auto* stem = model.getStem(slot.stemIndex);
            auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
            if (!stem || !seg || seg->getLengthSamples() <= 0) continue;

            // Chained mix is pre-rendered (multi-stem concat) → realtime
            // pitch via voice path doesn't apply. TODO if pitch needed here.
            const auto& srcBuf = stem->audioBuffer;

            int64_t startSmp = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), (int64_t) seg->startSample);
            int64_t endSmp   = juce::jlimit(startSmp,   (int64_t)srcBuf.getNumSamples(), (int64_t) seg->endSample);
            int segLen = static_cast<int>(endSmp - startSmp);
            if (segLen <= 0 || writePos + segLen > chainBuf->getNumSamples()) continue;

            juce::Logger::writeToLog("[SModePage] chainMix stem=" + juce::String(slot.stemIndex)
                + " (offline concat — pitch not applied)");

            float vel = slot.velocity;
            int chToCopy = juce::jmin(numCh, srcBuf.getNumChannels());
            for (int ch = 0; ch < chToCopy; ++ch)
                chainBuf->addFrom(ch, writePos, srcBuf, ch, static_cast<int>(startSmp), segLen, vel);

            writePos += segLen;
        }
    }

    midiSvc->playBufferDirect(chainBuf, modelSr, 1.0f);
    juce::Logger::writeToLog("[SModePage] Playing " + juce::String((int)sequencerLanes_.size())
                             + " lanes chained (" + juce::String((int)totalSamples) + " samples)");
}

// ── Stop All Sequencer Playback ──────────────────────────────────────────
void SModePage::stopAllSequencerPlayback()
{
    // Cancel the callAfterDelay chain
    if (seqPlayingFlag_) seqPlayingFlag_->store(false);
    seqPlayingFlag_.reset();
    activeSeqLane_ = -1;

    stopLoop();
    if (transportPlaying_) stopTransport();

    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return;
    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
    if (midiSvc) midiSvc->stopAllVoices();

    // Reset Play All button
    playAllSeqBtn_.setButtonText("Play All");
    playAllSeqBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6d3a));

    // Clear all slot highlights and playhead
    for (auto& lane : sequencerLanes_)
    {
        lane->setPlayingSlot(-1);
        lane->setPlayheadPosition(-1.0f);
    }

    juce::Logger::writeToLog("[SModePage] Stopped all sequencer playback");
}

// ── Drag Sequencer as WAV ────────────────────────────────────────────────
void SModePage::dragSequencerAsWav()
{
    if (sequencerLanes_.empty() || !smodeService_) return;

    auto& model = smodeService_->getModel();
    double modelSr = model.getSampleRate();
    if (modelSr <= 0.0) modelSr = 44100.0;
    double bpm = detectedBpm_ > 0.0 ? detectedBpm_ : 120.0;
    double samplesPerBeat = (60.0 / bpm) * modelSr;

    // Build chained buffer from all lanes
    int64_t totalSamples = 0;
    int numCh = 2;

    for (auto& lane : sequencerLanes_)
    {
        for (const auto& slot : lane->getSlots())
        {
            if (slot.empty) continue;
            auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
            if (!seg || seg->getLengthSamples() <= 0) continue;

            // Cell-based duration: cellSpan beats
            totalSamples += static_cast<int64_t>(slot.cellSpan * samplesPerBeat);

            auto* stem = model.getStem(slot.stemIndex);
            if (stem) numCh = stem->audioBuffer.getNumChannels();
        }
    }

    if (totalSamples <= 0) return;

    // Copy to buffer on message thread
    auto wavBuf = std::make_shared<juce::AudioBuffer<float>>(numCh, static_cast<int>(totalSamples));
    wavBuf->clear();
    int writePos = 0;

    for (auto& lane : sequencerLanes_)
    {
        // Sort by grid position for correct export order
        auto sortedSlots = lane->getSlots();
        std::sort(sortedSlots.begin(), sortedSlots.end(),
                  [](const PatternSlot& a, const PatternSlot& b) { return a.gridCell < b.gridCell; });
        for (const auto& slot : sortedSlots)
        {
            if (slot.empty) continue;
            auto* stem = model.getStem(slot.stemIndex);
            auto* seg = model.getSegment(slot.stemIndex, slot.segmentIndex);
            if (!stem || !seg || seg->getLengthSamples() <= 0) continue;

            const auto& srcBuf = stem->audioBuffer;
            int64_t segStart = juce::jlimit((int64_t)0, (int64_t)srcBuf.getNumSamples(), seg->startSample);
            int64_t segEnd = juce::jlimit(segStart, (int64_t)srcBuf.getNumSamples(), seg->endSample);

            // Cell-based with trim + speed
            int64_t cellDur = static_cast<int64_t>(slot.cellSpan * samplesPerBeat);
            int64_t segLen2 = segEnd - segStart;
            int64_t trimStart2 = segStart + static_cast<int64_t>(slot.trimStart * segLen2);
            int64_t trimEnd2 = segStart + static_cast<int64_t>(slot.trimEnd * segLen2);
            trimStart2 = juce::jlimit(segStart, segEnd, trimStart2);
            trimEnd2 = juce::jlimit(trimStart2 + 1, segEnd, trimEnd2);
            int64_t trimmedLen2 = trimEnd2 - trimStart2;

            int len = static_cast<int>(cellDur);
            if (len <= 0 || writePos + len > wavBuf->getNumSamples()) continue;

            int chCopy = juce::jmin(numCh, srcBuf.getNumChannels());

            if (std::abs(slot.speed - 1.0f) > 0.01f && slot.speed > 0.0f)
            {
                // Speed-stretch: resample trimmed audio to fill cell
                int inLen2 = static_cast<int>(trimmedLen2);
                double ratio2 = static_cast<double>(inLen2) / static_cast<double>(len);
                for (int ch = 0; ch < chCopy; ++ch)
                {
                    const float* rd = srcBuf.getReadPointer(ch, static_cast<int>(trimStart2));
                    float* wr = wavBuf->getWritePointer(ch, writePos);
                    for (int j = 0; j < len; ++j)
                    {
                        double sp = j * ratio2;
                        int i0 = juce::jmin(static_cast<int>(sp), inLen2 - 1);
                        int i1 = juce::jmin(i0 + 1, inLen2 - 1);
                        float f = static_cast<float>(sp - i0);
                        wr[j] += (rd[i0] * (1.0f - f) + rd[i1] * f) * slot.velocity;
                    }
                }
            }
            else
            {
                int copyLen = static_cast<int>(juce::jmin(trimmedLen2, cellDur));
                for (int ch = 0; ch < chCopy; ++ch)
                    wavBuf->addFrom(ch, writePos, srcBuf, ch, static_cast<int>(trimStart2), copyLen, slot.velocity);
            }

            // Match playback: apply the same per-voice micro-fade to each slot
            // so the exported WAV has no click at slot boundaries.
            samplar::applyMicroFadeRange(*wavBuf, writePos, len, modelSr);

            writePos += len;
        }
    }

    double sr = modelSr;
    std::thread([this, wavBuf, sr, numCh]()
    {
        auto tmpFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("SAMPLAR_Sequencer_Export.wav");

        juce::WavAudioFormat wavFmt;
        if (auto os = std::unique_ptr<juce::FileOutputStream>(tmpFile.createOutputStream()))
        {
            auto* writer = wavFmt.createWriterFor(os.get(), sr,
                                                  static_cast<unsigned int>(numCh), 24, {}, 0);
            if (writer)
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> w(writer);
                w->writeFromAudioSampleBuffer(*wavBuf, 0, wavBuf->getNumSamples());
                w.reset();

                juce::String path = tmpFile.getFullPathName();
                juce::MessageManager::callAsync([this, path]()
                {
                    juce::StringArray files;
                    files.add(path);
                    juce::DragAndDropContainer::performExternalDragDropOfFiles(files, true, this);
                    juce::Logger::writeToLog("[SModePage] Sequencer WAV drag ready: " + path);
                });
            }
        }
    }).detach();
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
    obj->setProperty("v", 1);
    obj->setProperty("gridLocked", gridLocked_);
    obj->setProperty("lockedBpm",  lockedBpm_);
    obj->setProperty("gridDivision", currentGridDivision_);
    obj->setProperty("loopEnabled", loopBtn_.getToggleState());
    obj->setProperty("linkAll", linkAllActive_);

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

    gridLocked_          = (bool)   obj->getProperty("gridLocked");
    lockedBpm_           = (double) obj->getProperty("lockedBpm");
    currentGridDivision_ = (int)    obj->getProperty("gridDivision");
    bool loopEnabled     = (bool)   obj->getProperty("loopEnabled");
    linkAllActive_       = (bool)   obj->getProperty("linkAll");

    loopBtn_.setToggleState(loopEnabled, juce::dontSendNotification);
    gridLockBtn_.setColour(juce::TextButton::buttonColourId,
        gridLocked_ ? juce::Colour(0xffd47a00) : juce::Colour(0xff2d3d5d));
    divisionCombo_.setSelectedId(currentGridDivision_, juce::dontSendNotification);
    linkAllBtn_.setColour(juce::TextButton::buttonColourId,
        linkAllActive_ ? juce::Colour(0xff2d6d3a) : juce::Colour(0xff3d3d3d));

    // Restore lanes — size match first, then delegate to each lane
    if (auto* laneArr = obj->getProperty("lanes").getArray())
    {
        const int target = laneArr->size();
        while (static_cast<int>(sequencerLanes_.size()) < target) addSequencerLane();
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
    if (f.existsAsFile())
    {
        samplar::RecentSongs::push(f);
        loadAudioOverlay_.setRecents(samplar::RecentSongs::get());
        if (onRequestSeparation)
            onRequestSeparation(f);
    }
}

} // namespace samplar
