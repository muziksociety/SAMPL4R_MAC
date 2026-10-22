#include "StemWaveformComponent.h"

namespace samplar {

StemWaveformComponent::StemWaveformComponent(SModeSegmentModel& model, int stemIndex, juce::Colour stemColour)
    : model_(model), stemIndex_(stemIndex), stemColour_(stemColour)
{
    juce::Logger::writeToLog("🎵 [StemWaveform] constructor stemIndex=" + juce::String(stemIndex));

    // Solo button
    addAndMakeVisible(soloBtn_);
    soloBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    soloBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    soloBtn_.setTooltip("Solo this stem (mute all others)");
    soloBtn_.onClick = [this]()
    {
        auto* stem = model_.getStem(stemIndex_);
        if (!stem) return;
        model_.pushUndoState("Stem solo");
        model_.setSolo(stemIndex_, !stem->solo);
        refreshStemButtons();
        repaint();
    };

    // Mute button
    addAndMakeVisible(muteBtn_);
    muteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    muteBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    muteBtn_.setTooltip("Mute this stem");
    muteBtn_.onClick = [this]()
    {
        auto* stem = model_.getStem(stemIndex_);
        if (!stem) return;
        model_.pushUndoState("Stem mute");
        model_.setMute(stemIndex_, !stem->mute);
        refreshStemButtons();
        repaint();
    };

    // Link button
    addAndMakeVisible(linkBtn_);
    linkBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    linkBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    linkBtn_.setTooltip("Link this stem to others for synchronized editing");
    linkBtn_.onClick = [this]()
    {
        // Delegate the actual link-state change to the parent page. The parent
        // owns the mutual-exclusion logic with LINK ALL and the authoritative
        // per-stem state; we just fire the intent.
        if (onLinkToggled) onLinkToggled(stemIndex_);
    };

    // Focus (eye) button — expand this stem, hide others
    {
        // Eye icon: outer almond as a hollow ring + filled pupil + tiny highlight.
        // Uses even-odd winding so the inner almond carves out the outline.
        juce::Path eyePath;
        eyePath.setUsingNonZeroWinding(false);

        // Outer almond (eye outline)
        eyePath.startNewSubPath(2.0f, 10.0f);
        eyePath.quadraticTo(10.0f, 2.5f, 18.0f, 10.0f);
        eyePath.quadraticTo(10.0f, 17.5f, 2.0f, 10.0f);
        eyePath.closeSubPath();

        // Inner almond (carves the outline into a ring under even-odd)
        eyePath.startNewSubPath(3.6f, 10.0f);
        eyePath.quadraticTo(10.0f, 4.2f, 16.4f, 10.0f);
        eyePath.quadraticTo(10.0f, 15.8f, 3.6f, 10.0f);
        eyePath.closeSubPath();

        // Pupil (filled solid circle in the centre)
        eyePath.addEllipse(7.6f, 7.6f, 4.8f, 4.8f);

        // Highlight (small filled dot — also carved out by even-odd, leaving white inside pupil)
        eyePath.addEllipse(8.4f, 8.4f, 1.6f, 1.6f);

        auto icon = std::make_unique<juce::DrawablePath>();
        icon->setPath(eyePath);
        icon->setFill(juce::Colours::grey);
        eyeIcon_ = std::move(icon);

        auto iconOver = std::make_unique<juce::DrawablePath>();
        iconOver->setPath(eyePath);
        iconOver->setFill(juce::Colours::white);
        eyeIconOver_ = std::move(iconOver);
    }
    addAndMakeVisible(focusBtn_);
    focusBtn_.setImages(eyeIcon_.get(), eyeIconOver_.get());
    focusBtn_.setColour(juce::DrawableButton::backgroundColourId, juce::Colour(0xff2d2d3a));
    focusBtn_.setTooltip("Focus: expand this stem, hide others");
    focusBtn_.onClick = [this]()
    {
        if (onFocusRequested)
            onFocusRequested(stemIndex_);
    };

    // ── Per-stem pitch knob (time-preserving SoundTouch) ─────────────────────
    addAndMakeVisible(pitchKnob_);
    pitchKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pitchKnob_.setRange(-12.0, 12.0, 1.0);
    pitchKnob_.setValue(0.0, juce::dontSendNotification);
    pitchKnob_.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 36, 14);
    pitchKnob_.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    pitchKnob_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    pitchKnob_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    pitchKnob_.setNumDecimalPlacesToDisplay(0);
    pitchKnob_.textFromValueFunction = [](double v)
    {
        const int s = (int) std::lround(v);
        return (s > 0 ? juce::String("+") + juce::String(s) : juce::String(s)) + " st";
    };
    pitchKnob_.valueFromTextFunction = [](const juce::String& t)
    {
        return t.retainCharacters("-0123456789").getDoubleValue();
    };
    pitchKnob_.setDoubleClickReturnValue(true, 0.0);
    pitchKnob_.setBaseTooltip(
        "Pitch: shift this stem up or down by -12 to +12 semitones.\n"
        "Time is preserved (does not affect playback speed).\n"
        "Double-click to reset to 0.");
    pitchKnob_.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff7aa6ff));
    pitchKnob_.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2d2d3a));
    pitchKnob_.onValueChange = [this]()
    {
        if (onPitchChanged)
            onPitchChanged(stemIndex_, (float) pitchKnob_.getValue());
    };
    pitchKnob_.onContextMenu = [this]()
    {
        if (onPitchMidiMenu) onPitchMidiMenu(stemIndex_);
    };

    // ── Control panel redesign widgets ───────────────────────────────────────
    addChildComponent(chevronBtn_);   // D-10: no action is wired to it -> never shown
    chevronBtn_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    chevronBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);

    addAndMakeVisible(stemIconBtn_);
    stemIconBtn_.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(stemNameLabel_);
    stemNameLabel_.setText("Stem", juce::dontSendNotification);
    stemNameLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    stemNameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    stemNameLabel_.setJustificationType(juce::Justification::centredLeft);

    // Style S/M/L buttons with proper colours
    soloBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    soloBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow.withAlpha(0.8f));
    soloBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    soloBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);

    muteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    muteBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.withAlpha(0.8f));
    muteBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    muteBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    linkBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d3a));
    linkBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::limegreen.withAlpha(0.8f));
    linkBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    linkBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);

    // (No lane hover-tracking — only the sequencer dots trigger cross-lane dim.)

    setOpaque(false);
    setWantsKeyboardFocus(true);

    // Initialize view range from stem audio
    auto* stem = model_.getStem(stemIndex_);
    if (stem && stem->audioBuffer.getNumSamples() > 0)
        viewEndTime_ = static_cast<double>(stem->audioBuffer.getNumSamples()) / model_.getSampleRate();
}

// ── Stem Kind / Icon ─────────────────────────────────────────────────────────

void StemWaveformComponent::setStemKind(StemKind kind)
{
    stemKind_ = kind;

    juce::String name;
    juce::Path iconPath;

    switch (kind)
    {
        case StemKind::Vocals:
        {
            name = "Vocals";
            // Microphone: vertical capsule body + small circular base + stand
            iconPath.addRoundedRectangle(8.0f, 2.0f, 8.0f, 12.0f, 4.0f);  // capsule
            iconPath.addEllipse(9.0f, 15.0f, 6.0f, 4.0f);                  // base
            iconPath.startNewSubPath(12.0f, 14.0f);
            iconPath.lineTo(12.0f, 19.0f);                                   // stand pole
            break;
        }
        case StemKind::Bass:
        {
            name = "Bass";
            // Eighth note: filled notehead + stem + flag
            iconPath.addEllipse(2.0f, 14.0f, 8.0f, 6.0f);   // notehead
            iconPath.startNewSubPath(10.0f, 17.0f);
            iconPath.lineTo(10.0f, 2.0f);                      // stem up
            iconPath.quadraticTo(18.0f, 4.0f, 14.0f, 8.0f);  // flag curve
            break;
        }
        case StemKind::Drums:
        {
            name = "Drums";
            // Drum shell (ellipse) + two crossed sticks
            iconPath.addEllipse(4.0f, 8.0f, 16.0f, 10.0f);   // drum shell
            iconPath.startNewSubPath(2.0f, 2.0f);
            iconPath.lineTo(10.0f, 8.0f);                      // stick left
            iconPath.startNewSubPath(22.0f, 2.0f);
            iconPath.lineTo(14.0f, 8.0f);                      // stick right
            break;
        }
        case StemKind::Melody:
        {
            name = "Melody";
            // Two eighth notes beamed: two noteheads + stems + beam
            iconPath.addEllipse(2.0f, 14.0f, 7.0f, 5.0f);    // notehead 1
            iconPath.startNewSubPath(9.0f, 16.0f);
            iconPath.lineTo(9.0f, 4.0f);                       // stem 1
            iconPath.addEllipse(13.0f, 16.0f, 7.0f, 5.0f);   // notehead 2
            iconPath.startNewSubPath(20.0f, 18.0f);
            iconPath.lineTo(20.0f, 6.0f);                      // stem 2
            iconPath.startNewSubPath(9.0f, 4.0f);
            iconPath.lineTo(20.0f, 6.0f);                      // beam
            break;
        }
        default:
        {
            name = "Stem";
            iconPath.addEllipse(8.0f, 8.0f, 8.0f, 8.0f);     // simple dot
            break;
        }
    }

    stemNameLabel_.setText(name, juce::dontSendNotification);

    auto drawable = std::make_unique<juce::DrawablePath>();
    drawable->setPath(iconPath);
    drawable->setFill(juce::Colours::lightgrey);
    drawable->setStrokeFill(juce::Colours::lightgrey);
    drawable->setStrokeThickness(1.5f);
    stemIconDrawable_ = std::move(drawable);
    stemIconBtn_.setImages(stemIconDrawable_.get());

    juce::Logger::writeToLog("[StemWaveform] setStemKind: stem=" + juce::String(stemIndex_)
                             + " name=" + name);
    repaint();
}

// ── Spectral Color Computation ───────────────────────────────────────────────

void StemWaveformComponent::computeSpectralColors()
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem || stem->audioBuffer.getNumSamples() == 0)
    {
        spectralReady_ = false;
        return;
    }

    const auto& buffer = stem->audioBuffer;
    const double sampleRate = model_.getSampleRate();
    const int64_t totalSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    spectralDuration_ = static_cast<double>(totalSamples) / sampleRate;

    constexpr int fftOrder = 11;
    constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);

    const double sliceWidth = 0.01;
    const int samplesPerSlice = static_cast<int>(sampleRate * sliceWidth);
    const int numSlices = static_cast<int>(spectralDuration_ / sliceWidth) + 1;

    const float lowCutoff = 150.0f, midLowCutoff = 400.0f;
    const float midCutoff = 1500.0f, midHighCutoff = 4000.0f;
    const juce::Colour colLow(0xffff2222);
    const juce::Colour colMidLow(0xffff6600);
    const juce::Colour colMid(0xffffee00);
    const juce::Colour colMidHigh(0xff44ff66);
    const juce::Colour colHigh(0xff00ddff);

    spectralColors_.clear();
    spectralColors_.reserve(static_cast<size_t>(numSlices));

    std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);

    for (int slice = 0; slice < numSlices; ++slice)
    {
        int64_t sliceStart = static_cast<int64_t>(slice) * samplesPerSlice;
        int samplesToRead = juce::jmin(fftSize, static_cast<int>(totalSamples - sliceStart));
        if (samplesToRead <= 0)
        {
            spectralColors_.push_back(juce::Colours::darkgrey);
            continue;
        }

        std::fill(fftData.begin(), fftData.end(), 0.0f);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* channelData = buffer.getReadPointer(ch);
            for (int s = 0; s < samplesToRead; ++s)
                fftData[static_cast<size_t>(s)] += channelData[sliceStart + s];
        }
        float scale = 1.0f / static_cast<float>(numChannels);
        for (int s = 0; s < samplesToRead; ++s)
            fftData[static_cast<size_t>(s)] *= scale;

        for (int s = 0; s < samplesToRead; ++s)
        {
            float w = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                                * static_cast<float>(s) / static_cast<float>(samplesToRead)));
            fftData[static_cast<size_t>(s)] *= w;
        }

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        float bandEnergy[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftSize);

        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            float freq = static_cast<float>(bin) * binWidth;
            float mag = fftData[static_cast<size_t>(bin)];
            float energy = mag * mag;

            if (freq < lowCutoff)           bandEnergy[0] += energy;
            else if (freq < midLowCutoff)   bandEnergy[1] += energy;
            else if (freq < midCutoff)       bandEnergy[2] += energy;
            else if (freq < midHighCutoff)   bandEnergy[3] += energy;
            else                             bandEnergy[4] += energy;
        }

        float total = bandEnergy[0] + bandEnergy[1] + bandEnergy[2] + bandEnergy[3] + bandEnergy[4];
        juce::Colour sliceColour = juce::Colours::darkgrey;

        if (total > 1e-10f)
        {
            float inv = 1.0f / total;
            float w0 = bandEnergy[0] * inv, w1 = bandEnergy[1] * inv;
            float w2 = bandEnergy[2] * inv, w3 = bandEnergy[3] * inv;
            float w4 = bandEnergy[4] * inv;

            float r = w0 * colLow.getFloatRed()     + w1 * colMidLow.getFloatRed()
                    + w2 * colMid.getFloatRed()      + w3 * colMidHigh.getFloatRed()
                    + w4 * colHigh.getFloatRed();
            float gn = w0 * colLow.getFloatGreen()   + w1 * colMidLow.getFloatGreen()
                     + w2 * colMid.getFloatGreen()    + w3 * colMidHigh.getFloatGreen()
                     + w4 * colHigh.getFloatGreen();
            float b = w0 * colLow.getFloatBlue()    + w1 * colMidLow.getFloatBlue()
                    + w2 * colMid.getFloatBlue()     + w3 * colMidHigh.getFloatBlue()
                    + w4 * colHigh.getFloatBlue();

            sliceColour = juce::Colour::fromFloatRGBA(
                juce::jlimit(0.0f, 1.0f, r),
                juce::jlimit(0.0f, 1.0f, gn),
                juce::jlimit(0.0f, 1.0f, b), 1.0f);
        }

        spectralColors_.push_back(sliceColour);
    }

    spectralReady_ = true;
    waveCacheValid_ = false;
    juce::Logger::writeToLog("✅ [StemWaveform] Spectral colors: " + juce::String(spectralColors_.size()) + " slices");
    repaint();
}

void StemWaveformComponent::computeSpectralColorsAsync(const std::function<bool(std::function<void()>)>& runBackground,
                                                       const std::atomic<bool>* cancel)
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem || stem->audioBuffer.getNumSamples() == 0) return;

    // ── Fast path: reuse cached colors from the model (survives editor close/reopen) ──
    if (stem->spectralReady && !stem->spectralColors.empty())
    {
        spectralColors_ = stem->spectralColors;
        spectralDuration_ = stem->spectralDuration;
        spectralReady_ = true;
        waveCacheValid_ = false;
        repaint();
        juce::Logger::writeToLog("[StemWaveform] Spectral colors restored from cache ("
                                 + juce::String(spectralColors_.size()) + " slices)");
        return;
    }

    // ── Slow path: compute on the processor-owned worker pool ──
    // Copy the audio HERE (message thread, a memcpy): a new song load clears
    // the model while the job runs, so the job must never hold raw pointers
    // into it. The job is drained by SModeService::shutdown(), so it never
    // outlives the plugin DLL.
    if (!runBackground)
    {
        juce::Logger::writeToLog("[StemWaveform] no background runner - computing spectral colours synchronously");
        computeSpectralColors();
        return;
    }

    // B33: share the model's immutable stem buffer (refcounted) instead of
    // copying the whole stem per lane on the message thread.
    auto shared = model_.getSharedStemAudio(stemIndex_);
    if (shared == nullptr || shared->getNumSamples() == 0) return;
    int numCh = shared->getNumChannels();
    int totalSmp = shared->getNumSamples();
    double sr = model_.getSampleRate();
    int stemIdx = stemIndex_;
    const uint64_t generation = model_.getContentGeneration();
    juce::Logger::writeToLog("[StemWaveform] spectral job: stem=" + juce::String(stemIdx)
                             + " samples=" + juce::String(totalSmp) + " (shared buffer, no copy)");

    auto safeThis = juce::Component::SafePointer<StemWaveformComponent>(this);
    const bool queued = runBackground([safeThis, shared, totalSmp, sr, numCh, stemIdx, generation, cancel]()
    {
        constexpr int fftOrder = 11;
        constexpr int fftSize = 1 << fftOrder;
        juce::dsp::FFT fft(fftOrder);

        const double sliceWidth = 0.01;
        const int samplesPerSlice = static_cast<int>(sr * sliceWidth);
        double duration = static_cast<double>(totalSmp) / sr;
        const int numSlices = static_cast<int>(duration / sliceWidth) + 1;

        const float lowCutoff = 150.0f, midLowCutoff = 400.0f;
        const float midCutoff = 1500.0f, midHighCutoff = 4000.0f;
        const juce::Colour colLow(0xffff2222), colMidLow(0xffff6600);
        const juce::Colour colMid(0xffffee00), colMidHigh(0xff44ff66), colHigh(0xff00ddff);

        std::vector<juce::Colour> colors;
        colors.reserve(static_cast<size_t>(numSlices));
        std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);

        for (int slice = 0; slice < numSlices; ++slice)
        {
            if ((slice & 255) == 0 && cancel != nullptr && cancel->load())
                return;   // plugin unloading: drop the half-done colours (worker: no logging)
            int64_t sliceStart = static_cast<int64_t>(slice) * samplesPerSlice;
            int samplesToRead = juce::jmin(fftSize, static_cast<int>(totalSmp - sliceStart));
            if (samplesToRead <= 0) { colors.push_back(juce::Colours::darkgrey); continue; }

            std::fill(fftData.begin(), fftData.end(), 0.0f);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* data = shared->getReadPointer(ch);
                for (int s = 0; s < samplesToRead; ++s)
                    fftData[static_cast<size_t>(s)] += data[sliceStart + s];
            }
            float scale = 1.0f / static_cast<float>(numCh);
            for (int s = 0; s < samplesToRead; ++s)
            {
                float w = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                                    * static_cast<float>(s) / static_cast<float>(samplesToRead)));
                fftData[static_cast<size_t>(s)] *= w * scale;
            }
            fft.performFrequencyOnlyForwardTransform(fftData.data());

            float bandEnergy[5] = {};
            const float binWidth = static_cast<float>(sr) / static_cast<float>(fftSize);
            for (int bin = 1; bin < fftSize / 2; ++bin)
            {
                float freq = static_cast<float>(bin) * binWidth;
                float energy = fftData[static_cast<size_t>(bin)] * fftData[static_cast<size_t>(bin)];
                if      (freq < lowCutoff)     bandEnergy[0] += energy;
                else if (freq < midLowCutoff)  bandEnergy[1] += energy;
                else if (freq < midCutoff)     bandEnergy[2] += energy;
                else if (freq < midHighCutoff) bandEnergy[3] += energy;
                else                           bandEnergy[4] += energy;
            }
            float total = bandEnergy[0]+bandEnergy[1]+bandEnergy[2]+bandEnergy[3]+bandEnergy[4];
            juce::Colour col = juce::Colours::darkgrey;
            if (total > 1e-10f)
            {
                float inv = 1.0f / total;
                float w0=bandEnergy[0]*inv, w1=bandEnergy[1]*inv, w2=bandEnergy[2]*inv,
                      w3=bandEnergy[3]*inv, w4=bandEnergy[4]*inv;
                float r = w0*colLow.getFloatRed()    + w1*colMidLow.getFloatRed()
                        + w2*colMid.getFloatRed()     + w3*colMidHigh.getFloatRed()    + w4*colHigh.getFloatRed();
                float g = w0*colLow.getFloatGreen()  + w1*colMidLow.getFloatGreen()
                        + w2*colMid.getFloatGreen()   + w3*colMidHigh.getFloatGreen()  + w4*colHigh.getFloatGreen();
                float b = w0*colLow.getFloatBlue()   + w1*colMidLow.getFloatBlue()
                        + w2*colMid.getFloatBlue()    + w3*colMidHigh.getFloatBlue()   + w4*colHigh.getFloatBlue();
                col = juce::Colour::fromFloatRGBA(r, g, b, 1.0f);
            }
            colors.push_back(col);
        }

        // Apply on message thread + cache in model for reconnect
        juce::MessageManager::callAsync([safeThis, colors = std::move(colors), duration, stemIdx, generation]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;   // lane destroyed (editor closed / stems reloaded)
            if (generation != self->model_.getContentGeneration())
            {
                juce::Logger::writeToLog("[StemWaveform] spectral result dropped (song changed)");
                return;
            }
            auto& spectralColors_ = self->spectralColors_;
            auto& spectralDuration_ = self->spectralDuration_;
            auto& spectralReady_ = self->spectralReady_;
            auto& model_ = self->model_;
            spectralColors_ = colors;
            spectralDuration_ = duration;
            spectralReady_ = true;

            // Cache in the processor-lifetime model so colors survive editor close/reopen
            auto* s = model_.getStem(stemIdx);
            if (s)
            {
                s->spectralColors = colors;
                s->spectralDuration = duration;
                s->spectralReady = true;
            }

            self->waveCacheValid_ = false;
            self->repaint();
            juce::Logger::writeToLog("✅ [StemWaveform] Spectral async done: " + juce::String(spectralColors_.size()) + " slices (cached in model)");
        });
    });

    if (!queued)
    {
        juce::Logger::writeToLog("[StemWaveform] background runner refused job - computing synchronously");
        computeSpectralColors();
    }
}

// ── View Range ───────────────────────────────────────────────────────────────

void StemWaveformComponent::setViewRange(double startTime, double endTime)
{
    if (startTime == viewStartTime_ && endTime == viewEndTime_) return;
    viewStartTime_ = startTime;
    viewEndTime_ = endTime;
    waveCacheValid_ = false;
    repaint();
}

int StemWaveformComponent::playheadColumn(double timeSeconds) const
{
    if (timeSeconds < 0.0 || waveArea_.isEmpty()) return -1;
    if (timeSeconds < viewStartTime_ || timeSeconds > viewEndTime_) return -1;
    return timeToPixel(timeSeconds);
}

void StemWaveformComponent::setPlayheadTime(double timeSeconds)
{
    if (timeSeconds == playheadTime_) return;
    const int oldX = playheadColumn(playheadTime_);
    playheadTime_ = timeSeconds;
    const int newX = playheadColumn(playheadTime_);
    if (oldX == newX) return;
    // drawPlayhead() paints x-1 .. x+1: repaint a 5 px column at each position.
    if (oldX >= 0) repaint(oldX - 2, waveArea_.getY(), 5, waveArea_.getHeight());
    if (newX >= 0) repaint(newX - 2, waveArea_.getY(), 5, waveArea_.getHeight());
}

// ── Layout ───────────────────────────────────────────────────────────────────

void StemWaveformComponent::resized()
{
    auto area = getLocalBounds();
    controlArea_ = area.removeFromLeft(kControlWidth);
    if (area.getWidth() != waveArea_.getWidth() || area.getHeight() != waveArea_.getHeight())
        waveCacheValid_ = false;
    waveArea_ = area;

    // Card interior: accent strip 4, card inset 6 (all sides), card padding 6 x 4.
    auto panel = controlArea_;
    panel.removeFromLeft(4);
    panel = panel.withTrimmedLeft(kPanelInset).withTrimmedRight(kPanelInset).reduced(0, kPanelInset);
    panel = panel.reduced(6, 4);   // 132 x (h - 20)

    const auto mode = getHeight() >= kTallLaneH    ? HeaderMode::Tall
                    : getHeight() >= kCompactLaneH ? HeaderMode::Compact
                                                   : HeaderMode::Mini;
    applyHeaderMode(mode);

    auto placeSmlEye = [this](juce::Rectangle<int> row, int w, int gap, bool withEye)
    {
        for (auto* b : std::initializer_list<juce::Component*> { &soloBtn_, &muteBtn_, &linkBtn_ })
        {
            b->setBounds(row.removeFromLeft(w));
            row.removeFromLeft(gap);
        }
        if (withEye) focusBtn_.setBounds(row.removeFromLeft(w));
    };

    if (mode == HeaderMode::Tall)
    {
        auto row1 = panel.removeFromTop(26);
        stemIconBtn_.setBounds(row1.removeFromLeft(22));
        row1.removeFromLeft(6);
        focusBtn_.setBounds(row1.removeFromRight(22).withSizeKeepingCentre(22, 20));
        row1.removeFromRight(4);
        stemNameLabel_.setBounds(row1);
        panel.removeFromTop(6);
        auto row2 = panel.removeFromTop(24);
        placeSmlEye(row2, (row2.getWidth() - 12) / 3, 6, false);
        panel.removeFromTop(6);
        const int d = juce::jmin(64, juce::jmin(panel.getWidth(), panel.getHeight()));
        pitchKnob_.setBounds(panel.withSizeKeepingCentre(d, d));
    }
    else if (mode == HeaderMode::Compact)
    {
        auto knobCol = panel.removeFromRight(40);
        panel.removeFromRight(6);
        auto rows = panel.withSizeKeepingCentre(panel.getWidth(), juce::jmin(panel.getHeight(), 42));
        auto row1 = rows.removeFromTop(18);
        stemIconBtn_.setBounds(row1.removeFromLeft(16));
        row1.removeFromLeft(4);
        stemNameLabel_.setBounds(row1);
        rows.removeFromTop(4);
        placeSmlEye(rows.removeFromTop(20), 20, 2, true);   // 4 x 20 + 3 x 2 = 86
        const int d = juce::jmin(40, knobCol.getHeight());
        pitchKnob_.setBounds(knobCol.withSizeKeepingCentre(d, d));
    }
    else // Mini
    {
        auto row = panel.withSizeKeepingCentre(panel.getWidth(), juce::jmin(20, panel.getHeight()));
        pitchKnob_.setBounds(row.removeFromRight(row.getHeight()));
        row.removeFromRight(4);
        placeSmlEye(row, 20, 2, true);
    }
}

void StemWaveformComponent::applyHeaderMode(HeaderMode m)
{
    if (m == headerMode_ && headerModeApplied_) return;
    headerMode_ = m;
    headerModeApplied_ = true;
    const bool tall = (m == HeaderMode::Tall);
    pitchKnob_.setTextBoxStyle(tall ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, true, 36, 14);
    auto* top = getTopLevelComponent();
    pitchKnob_.setPopupDisplayEnabled(! tall, ! tall, top != this ? top : nullptr, 1500);
    stemNameLabel_.setFont(juce::FontOptions(tall ? 14.0f : 13.0f, juce::Font::bold));
    stemIconBtn_.setVisible(m != HeaderMode::Mini);
    stemNameLabel_.setVisible(m != HeaderMode::Mini);
    chevronBtn_.setVisible(false);   // D-10: dead control
    juce::Logger::writeToLog("[StemWaveform] stem=" + juce::String(stemIndex_) + " header mode -> "
        + juce::String(tall ? "Tall" : (m == HeaderMode::Compact ? "Compact" : "Mini"))
        + " (h=" + juce::String(getHeight()) + ")");
}

// ── Visibility ───────────────────────────────────────────────────────────────

void StemWaveformComponent::visibilityChanged()
{
    if (isVisible())
        repaint();
}

// ── Paint ────────────────────────────────────────────────────────────────────

void StemWaveformComponent::paint(juce::Graphics& g)
{
    struct PaintTimer
    {
        double t0 { juce::Time::getMillisecondCounterHiRes() };
        ~PaintTimer() { debugPaintMs_ += juce::Time::getMillisecondCounterHiRes() - t0; ++debugPaintCount_; }
    } paintTimer;

    // Lane corner radius — clip the whole paint pass to a rounded rect so the
    // waveform, accent strip, and control panel card all respect the curves.
    const float kLaneRadius = 12.0f;
    {
        juce::Path lanePath;
        lanePath.addRoundedRectangle(getLocalBounds().toFloat(), kLaneRadius);
        g.reduceClipRegion(lanePath);
    }

    // Themed diagonal gradient background
    {
        juce::ColourGradient bg(bgA_, 0.0f, 0.0f,
                                bgB_, (float)getWidth(), (float)getHeight(),
                                false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), kLaneRadius);
    }

    // Accent tint overlay (keeps stem colour visible at low alpha)
    g.setColour(stemColour_.withAlpha(0.07f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), kLaneRadius);

    // Left-edge accent: full-height bar with a vertical alpha gradient so the
    // colour fades to nothing at the top and bottom — visually merging into the
    // lane's rounded corners. Horizontal halo also fades into the corner curve.
    {
        const float h = static_cast<float>(getHeight());

        // Soft horizontal halo (fades to transparent rightward)
        {
            juce::Rectangle<float> halo(0.0f, 0.0f, 10.0f, h);
            juce::ColourGradient haloX(stemColour_.withAlpha(0.55f), 0.0f, 0.0f,
                                       stemColour_.withAlpha(0.0f), 10.0f, 0.0f,
                                       false);
            g.setGradientFill(haloX);
            g.fillRect(halo);

            // Top/bottom fade on the halo via alpha-only vertical overlay
            juce::ColourGradient haloY(juce::Colours::black.withAlpha(0.0f), 0.0f, h * 0.18f,
                                       juce::Colours::black.withAlpha(0.0f), 0.0f, h * 0.82f,
                                       false);
            haloY.addColour(0.0, juce::Colours::black);  // opaque black at very top of stop range
            // Actually simpler approach below: mask via fill-on-top of corners
        }

        // Core slim bar (3px wide, full height) with vertical alpha fade
        juce::Rectangle<float> bar(1.0f, 0.0f, 3.0f, h);
        juce::ColourGradient barGrad(stemColour_.withAlpha(0.0f), 0.0f, 0.0f,
                                     stemColour_.withAlpha(0.0f), 0.0f, h,
                                     false);
        barGrad.addColour(0.15, stemColour_.withAlpha(0.95f));
        barGrad.addColour(0.50, stemColour_);
        barGrad.addColour(0.85, stemColour_.withAlpha(0.95f));
        g.setGradientFill(barGrad);
        g.fillRoundedRectangle(bar, 1.5f);

        // Inner highlight running down the centre for a glassy edge
        juce::ColourGradient hiGrad(juce::Colours::white.withAlpha(0.0f),  0.0f, 0.0f,
                                    juce::Colours::white.withAlpha(0.0f),  0.0f, h,
                                    false);
        hiGrad.addColour(0.30, juce::Colours::white.withAlpha(0.30f));
        hiGrad.addColour(0.70, juce::Colours::white.withAlpha(0.30f));
        g.setGradientFill(hiGrad);
        g.fillRect(juce::Rectangle<float>(2.0f, 0.0f, 1.0f, h));
    }

    // ── Control panel card ────────────────────────────────────────────────
    // Drawn as a visually distinct rounded box inset from the lane edges,
    // sitting to the right of the 4px accent strip.
    {
        juce::Rectangle<float> card(
            4.0f + (float)kPanelInset,                      // left: after accent strip + inset
            (float)kPanelInset,                              // top inset
            (float)(kControlWidth - 4 - kPanelInset * 2),   // width: panel minus strip & margins
            (float)(getHeight() - kPanelInset * 2));         // height: lane minus top+bottom margin
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(card, 8.0f);
        g.setColour(stemColour_.withAlpha(0.25f));
        g.drawRoundedRectangle(card, 8.0f, 1.0f);
    }

    // Border
    g.setColour(stemColour_.withAlpha(0.5f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 12.0f, 1.5f);

    // Old self-highlight overlay disabled — cross-lane dim handles the focus visual.
    // (isHighlighted_ still tracked for the onStemHovered fire/clear logic.)

    if (waveArea_.isEmpty()) return;

    // ── Loading state: full-lane progress fill (entire waveform area) ──
    if (loadingState_)
    {
        const auto waveF = waveArea_.toFloat();

        // Dim base
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.fillRect(waveF);

        // Filled portion of the lane (full height, width scales with progress)
        const float fillW = juce::jmax(0.0f, waveF.getWidth() * loadingProgress_);
        if (fillW > 0.0f)
        {
            juce::Rectangle<float> fill(waveF.getX(), waveF.getY(), fillW, waveF.getHeight());

            // Gradient: dark stem at left edge -> bright stem at leading edge
            juce::ColourGradient fillGrad(stemColour_.withAlpha(0.35f), fill.getX(), 0.0f,
                                          stemColour_.withAlpha(0.85f), fill.getRight(), 0.0f,
                                          false);
            g.setGradientFill(fillGrad);
            g.fillRect(fill);

            // Ocean-wave overlay: multiple soft crests of brighter colour scrolling across
            // the filled portion. Each crest is a horizontal gradient that fades in/out so
            // the result reads as continuous flowing colour instead of a bouncing band.
            const int   kNumCrests = 5;
            const float spacing    = fill.getWidth() / static_cast<float>(kNumCrests);
            const float crestW     = juce::jmax(40.0f, spacing * 1.4f);
            // Continuous left-to-right scroll based on loadingPhase_ (1 cycle / sec at 30 Hz).
            const float scrollPx   = std::fmod(loadingPhase_ * spacing, spacing);

            for (int i = 0; i < kNumCrests + 1; ++i)
            {
                const float cx = fill.getX() + (static_cast<float>(i) * spacing) + scrollPx;
                juce::Rectangle<float> crest(cx - crestW * 0.5f, fill.getY(),
                                             crestW, fill.getHeight());
                crest = crest.getIntersection(fill);
                if (crest.isEmpty()) continue;

                juce::ColourGradient cg(stemColour_.withAlpha(0.0f),       crest.getX(),       0.0f,
                                        stemColour_.withAlpha(0.0f),       crest.getRight(),   0.0f,
                                        false);
                cg.addColour(0.5, stemColour_.brighter(0.55f).withAlpha(0.30f));
                g.setGradientFill(cg);
                g.fillRect(crest);
            }

            // Soft bright leading edge (no hard line — just a fade)
            const float edgeW = 6.0f;
            juce::Rectangle<float> edge(fill.getRight() - edgeW, fill.getY(),
                                        edgeW, fill.getHeight());
            juce::ColourGradient edgeGrad(stemColour_.withAlpha(0.0f),       edge.getX(),     0.0f,
                                          stemColour_.brighter(0.5f).withAlpha(0.55f), edge.getRight(), 0.0f,
                                          false);
            g.setGradientFill(edgeGrad);
            g.fillRect(edge);
        }

        // Percentage label, large, centered over the entire lane
        const int pct = juce::roundToInt(loadingProgress_ * 100.0f);
        juce::String label = "SEPARATING - " + juce::String(pct) + "%";
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.setFont(juce::FontOptions(juce::jlimit(14.0f, 28.0f, waveF.getHeight() * 0.28f), juce::Font::bold));
        g.drawText(label, waveArea_, juce::Justification::centred);

        return;  // skip normal waveform drawing while loading
    }
    // ── End loading state ─────────────────────────────────────────────────────

    // Stem name label (top-left of wave area)
    auto* stem = model_.getStem(stemIndex_);
    if (stem)
    {
        if (stem->mute)
        {
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.fillRect(waveArea_);
        }

        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(stem->name, waveArea_.getX() + 4, waveArea_.getY() + 1, 60, 12,
                   juce::Justification::centredLeft);

        // Segment count badge
        int segCount = static_cast<int>(stem->segments.size());
        if (segCount > 0)
        {
            juce::String badge = juce::String(segCount) + " segs";
            g.setFont(juce::FontOptions(8.5f));
            int bw = 46;
            juce::Rectangle<float> pill(static_cast<float>(waveArea_.getX() + 66),
                                        static_cast<float>(waveArea_.getY() + 2),
                                        static_cast<float>(bw), 11.0f);
            g.setColour(stemColour_.withAlpha(0.35f));
            g.fillRoundedRectangle(pill, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawText(badge, static_cast<int>(pill.getX()), static_cast<int>(pill.getY()),
                       bw, 11, juce::Justification::centred);
        }
    }

    drawGrid(g, waveArea_);
    drawWaveformCached(g, waveArea_);
    drawSegments(g, waveArea_);
    drawPlayhead(g, waveArea_);

    if (gridTooltipVisible_)
        drawGridTooltip(g);
    if (segTooltipVisible_)
        drawSegTooltip(g);
}

// ── Waveform Drawing ─────────────────────────────────────────────────────────

void StemWaveformComponent::drawWaveform(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem || stem->audioBuffer.getNumSamples() == 0) return;

    const auto& buffer = stem->audioBuffer;
    const double sampleRate = model_.getSampleRate();
    const int width = area.getWidth();
    const int height = area.getHeight();
    const float centreY = static_cast<float>(area.getCentreY());
    // Use ~40% of height for waveform amplitude (compact display)
    const float halfH = static_cast<float>(height) * 0.40f;

    const double duration = viewEndTime_ - viewStartTime_;
    if (duration <= 0.0) return;

    const double samplesPerPixel = (duration * sampleRate) / width;
    const int numChannels = buffer.getNumChannels();
    const int totalSamples = buffer.getNumSamples();

    for (int x = 0; x < width; ++x)
    {
        int64_t sampleStart = static_cast<int64_t>((viewStartTime_ + (static_cast<double>(x) / width) * duration) * sampleRate);
        int64_t sampleEnd = sampleStart + static_cast<int64_t>(samplesPerPixel);
        sampleStart = juce::jlimit(static_cast<int64_t>(0), static_cast<int64_t>(totalSamples - 1), sampleStart);
        sampleEnd = juce::jlimit(sampleStart + 1, static_cast<int64_t>(totalSamples), sampleEnd);

        float minVal = 0.0f, maxVal = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto range = buffer.findMinMax(ch, static_cast<int>(sampleStart),
                                            static_cast<int>(sampleEnd - sampleStart));
            minVal = juce::jmin(minVal, range.getStart());
            maxVal = juce::jmax(maxVal, range.getEnd());
        }

        // Get spectral color for this pixel
        juce::Colour col = stemColour_;
        if (spectralReady_ && !spectralColors_.empty())
        {
            double pixelTime = viewStartTime_ + (static_cast<double>(x) / width) * duration;
            int sliceIdx = static_cast<int>(pixelTime / 0.01);
            sliceIdx = juce::jlimit(0, static_cast<int>(spectralColors_.size()) - 1, sliceIdx);
            col = spectralColors_[static_cast<size_t>(sliceIdx)];
        }

        float y1 = centreY - maxVal * halfH;
        float y2 = centreY - minVal * halfH;
        float lineHeight = juce::jmax(1.0f, y2 - y1);

        g.setColour(col.withAlpha(0.85f));
        g.fillRect(static_cast<float>(area.getX() + x), y1, 1.0f, lineHeight);
    }
}

void StemWaveformComponent::drawWaveformCached(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem || stem->audioBuffer.getNumSamples() == 0 || area.isEmpty()) return;

    const float scale = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    WaveCacheKey key;
    key.w = area.getWidth();
    key.h = area.getHeight();
    key.scale = scale;
    key.viewStart = viewStartTime_;
    key.viewEnd = viewEndTime_;
    key.generation = model_.getContentGeneration();
    key.numSamples = stem->audioBuffer.getNumSamples();
    key.data = stem->audioBuffer.getNumChannels() > 0 ? stem->audioBuffer.getReadPointer(0) : nullptr;
    key.spectral = spectralReady_;
    key.spectralCount = spectralColors_.size();

    if (!waveCacheValid_ || !(key == waveCacheKey_) || !waveCache_.isValid())
    {
        const int iw = juce::jmax(1, juce::roundToInt((float) area.getWidth()  * scale));
        const int ih = juce::jmax(1, juce::roundToInt((float) area.getHeight() * scale));
        waveCache_ = juce::Image(juce::Image::ARGB, iw, ih, true);
        {
            juce::Graphics ig(waveCache_);
            ig.addTransform(juce::AffineTransform::scale(scale));
            drawWaveform(ig, area.withPosition(0, 0));
        }
        waveCacheKey_ = key;
        waveCacheValid_ = true;
        ++debugWaveCacheBuilds_;
    }
    // drawImage() is modulated by the current colour's opacity (drawGrid leaves
    // a 13% white behind) -> draw the cached image fully opaque.
    g.setOpacity(1.0f);
    g.drawImage(waveCache_, area.toFloat());
}

// ── Segment Drawing ──────────────────────────────────────────────────────────

void StemWaveformComponent::drawSegments(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem) return;

    for (size_t i = 0; i < stem->segments.size(); ++i)
    {
        const auto& seg = stem->segments[i];
        int x1 = sampleToPixel(seg.startSample);
        int x2 = sampleToPixel(seg.endSample);

        if (x2 <= area.getX() || x1 >= area.getRight()) continue;

        x1 = juce::jmax(x1, area.getX());
        x2 = juce::jmin(x2, area.getRight());

        juce::Rectangle<int> segRect(x1, area.getY(), x2 - x1, area.getHeight());

        // Segment fill
        auto segColour = seg.selected ? juce::Colours::white.withAlpha(0.2f)
                                      : stemColour_.withAlpha(0.15f);
        g.setColour(segColour);
        g.fillRect(segRect);

        // Playback animation: gradient sweep (same style as SampleBrowserPage tiles)
        auto animIt = segmentAnims_.find(static_cast<int>(i));
        if (animIt != segmentAnims_.end() && animIt->second.active)
        {
            const float progress = juce::jlimit(0.0f, 1.0f, animIt->second.progress);
            float highlightW = segRect.getWidth() * juce::jmax(progress, 0.01f);
            highlightW = juce::jmax(highlightW, 1.0f);
            highlightW = juce::jmin(highlightW, static_cast<float>(segRect.getWidth()));

            juce::Rectangle<float> highlight(static_cast<float>(segRect.getX()),
                                              static_cast<float>(segRect.getY()),
                                              highlightW,
                                              static_cast<float>(segRect.getHeight()));

            auto startCol = stemColour_.withAlpha(0.50f);
            auto endCol   = stemColour_.withAlpha(0.06f);
            juce::ColourGradient grad(startCol, highlight.getX(), highlight.getY(),
                                       endCol, highlight.getRight(), highlight.getBottom(), false);
            grad.addColour(0.35, stemColour_.withAlpha(0.35f));
            grad.addColour(0.8, stemColour_.withAlpha(0.15f));
            g.setGradientFill(grad);
            g.fillRect(highlight);

            // Glow border while playing
            g.setColour(stemColour_.withAlpha(0.6f));
            g.drawRect(segRect, 2);
        }

        // Segment borders (left and right edges)
        g.setColour(stemColour_.withAlpha(0.8f));
        g.fillRect(x1, area.getY(), 2, area.getHeight());
        g.fillRect(x2 - 2, area.getY(), 2, area.getHeight());

        // MIDI note badge — pill background + note name
        if (seg.midiNote >= 0 && segRect.getWidth() >= 18)
        {
            auto noteName = midiNoteToName(seg.midiNote);
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            int labelW = juce::jmin(segRect.getWidth() - 2, 34);
            int labelX = segRect.getCentreX() - labelW / 2;
            int labelY = segRect.getBottom() - 14;
            juce::Rectangle<float> pill(static_cast<float>(labelX - 1), static_cast<float>(labelY),
                                        static_cast<float>(labelW + 2), 12.0f);
            g.setColour(stemColour_.withAlpha(0.7f));
            g.fillRoundedRectangle(pill, 3.0f);
            g.setColour(juce::Colours::white);
            g.drawText(noteName, labelX, labelY, labelW, 12, juce::Justification::centred);
        }

        // Effects indicator
        if (seg.effects.pitchSemitones != 0 || seg.effects.reverse || seg.effects.filterCutoff < 0.99f)
        {
            g.setColour(juce::Colours::orange);
            g.fillEllipse(static_cast<float>(segRect.getRight() - 8),
                          static_cast<float>(segRect.getY() + 3), 5.0f, 5.0f);
        }
    }

    // Draw in-progress drag segment
    if (dragMode_ == DragMode::Creating)
    {
        int x1 = juce::jmin(dragStartX_, getMouseXYRelative().x);
        int x2 = juce::jmax(dragStartX_, getMouseXYRelative().x);
        x1 = juce::jmax(x1, area.getX());
        x2 = juce::jmin(x2, area.getRight());

        g.setColour(stemColour_.withAlpha(0.25f));
        g.fillRect(x1, area.getY(), x2 - x1, area.getHeight());
        g.setColour(stemColour_.withAlpha(0.7f));
        g.drawRect(x1, area.getY(), x2 - x1, area.getHeight(), 1);
    }
}

// ── Grid Drawing ─────────────────────────────────────────────────────────────

void StemWaveformComponent::drawGrid(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (gridBpm_ <= 0.0) return;

    double beatDuration = 60.0 / gridBpm_;                              // seconds per quarter note
    double divInterval  = beatDuration * (4.0 / (double) divisionValue_); // seconds per subdivision line
    double duration     = viewEndTime_ - viewStartTime_;
    if (duration <= 0.0 || divInterval <= 0.0) return;

    // Align to the first subdivision line >= viewStartTime_
    double firstLine = gridOffsetSeconds_;
    if (firstLine < viewStartTime_)
    {
        double stepsToSkip = std::floor((viewStartTime_ - firstLine) / divInterval);
        firstLine += stepsToSkip * divInterval;
    }

    // Hierarchy alphas: bar (downbeat) > beat > subdivision
    const float alphaBar  = 0.28f;
    const float alphaBeat = 0.14f;
    const float alphaSub  = 0.06f;

    for (double t = firstLine; t < viewEndTime_; t += divInterval)
    {
        if (t < viewStartTime_) continue;
        double frac = (t - viewStartTime_) / duration;
        int x = area.getX() + static_cast<int>(frac * area.getWidth());

        // Classify: is this line a beat boundary? A bar boundary?
        double beatsSinceOffset = (t - gridOffsetSeconds_) / beatDuration;
        double rounded = std::round(beatsSinceOffset);
        bool   isBeat = std::abs(beatsSinceOffset - rounded) < 1e-4;
        bool   isBar  = isBeat && (static_cast<long long>(rounded) % 4 == 0);

        float alpha = isBar ? alphaBar : (isBeat ? alphaBeat : alphaSub);
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }
}

void StemWaveformComponent::drawGridTooltip(juce::Graphics& g)
{
    juce::String text = "Ctrl + Drag = move grid";
    auto font = juce::Font(11.0f);
    g.setFont(font);
    int tw = (int)font.getStringWidthFloat(text) + 14;
    int th = 20;
    int tx = gridTooltipPos_.x - tw / 2;
    int ty = gridTooltipPos_.y - th - 8;
    tx = juce::jlimit(0, getWidth() - tw, tx);
    ty = juce::jlimit(0, getHeight() - th, ty);

    g.setColour(juce::Colour(0xee000000));
    g.fillRoundedRectangle((float)tx, (float)ty, (float)tw, (float)th, 4.0f);
    g.setColour(juce::Colour(0xffaaaaaa));
    g.drawText(text, tx, ty, tw, th, juce::Justification::centred);
}

void StemWaveformComponent::drawSegTooltip(juce::Graphics& g)
{
    auto font = juce::Font(11.0f);
    g.setFont(font);
    int tw = (int)font.getStringWidthFloat(segTooltipText_) + 14;
    int th = 20;
    int tx = segTooltipPos_.x - tw / 2;
    int ty = segTooltipPos_.y - th - 8;
    tx = juce::jlimit(0, getWidth() - tw, tx);
    ty = juce::jlimit(0, getHeight() - th, ty);

    g.setColour(juce::Colour(0xee000000));
    g.fillRoundedRectangle((float)tx, (float)ty, (float)tw, (float)th, 4.0f);
    g.setColour(juce::Colour(0xffaaaaaa));
    g.drawText(segTooltipText_, tx, ty, tw, th, juce::Justification::centred);
}

// ── Mouse Interaction ────────────────────────────────────────────────────────

void StemWaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    // Right-click on control area (S/M/L/V banner) → channel context menu
    if (controlArea_.contains(e.getPosition()) && e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem(1, excludedFromQuickAssign_ ? "Include in Quick Assign" : "Exclude from Quick Assign");
        menu.addSeparator();
        menu.addItem(2, "Remove Channel");

        // B7: the menu is tied to this lane (closed if the lane is deleted) and
        // the callback re-checks the lane is still alive.
        juce::Component::SafePointer<StemWaveformComponent> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withTargetComponent(this)
                               .withTargetScreenArea(localAreaToGlobal(juce::Rectangle<int>(e.x, e.y, 1, 1)))
                               .withDeletionCheck(*this),
            [safe](int result)
            {
                auto* self = safe.getComponent();
                if (self == nullptr || result == 0) return;
                if (result == 1)
                {
                    self->excludedFromQuickAssign_ = !self->excludedFromQuickAssign_;
                    juce::Logger::writeToLog("[StemWaveform] Stem " + juce::String(self->stemIndex_)
                                             + " quick assign: " + juce::String(self->excludedFromQuickAssign_ ? "EXCLUDED" : "INCLUDED"));
                    if (self->onQuickAssignExcludeChanged)
                        self->onQuickAssignExcludeChanged(self->stemIndex_, self->excludedFromQuickAssign_);
                }
                else if (result == 2)
                {
                    if (self->onRemoveRequested)
                        self->onRemoveRequested(self->stemIndex_);
                }
            });
        return;
    }

    if (!waveArea_.contains(e.getPosition())) return;

    // Middle-click → start panning (drag waveform side to side)
    if (e.mods.isMiddleButtonDown())
    {
        isPanning_ = true;
        panStartX_ = e.x;
        panStartViewStart_ = viewStartTime_;
        panStartViewEnd_ = viewEndTime_;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // Right-click → context menu
    if (e.mods.isPopupMenu())
    {
        int hitSeg = findSegmentAtX(e.x);
        if (hitSeg >= 0)
            showSegmentContextMenu(hitSeg, e.getPosition());
        else
            showEmptyAreaContextMenu(e.getPosition());
        return;
    }

    // Grid drag: Ctrl+drag anywhere. Ctrl+Shift is reserved for the
    // multi-segment drag-out below (B19), so it never moves the grid.
    if (gridBpm_ > 0.0 && e.mods.isCtrlDown() && ! e.mods.isShiftDown())
    {
        dragMode_ = DragMode::DraggingGrid;
        dragStartX_ = e.x;
        gridDragStartOffset_ = gridOffsetSeconds_;
        if (onGridDragStarted) onGridDragStarted(stemIndex_);   // one undo entry per drag
        juce::Logger::writeToLog("[StemWaveform] Grid drag (Ctrl+drag) at x=" + juce::String(e.x));
        return;
    }

    // Check for segment edge (resize). The undo entry is pushed on the first
    // real move (B24: a click on an edge must not add a junk entry).
    int edgeSeg = -1;
    auto edge = findSegmentEdge(e.x, edgeSeg);
    if (edge != EdgeHit::None && ! (e.mods.isCtrlDown() && e.mods.isShiftDown()))
    {
        dragMode_ = (edge == EdgeHit::Left) ? DragMode::ResizingLeft : DragMode::ResizingRight;
        activeSegIdx_ = edgeSeg;
        resizeUndoPushed_ = false;
        return;
    }

    // Check if clicking on existing segment (select/play/loop)
    int hitSeg = findSegmentAtX(e.x);
    if (hitSeg >= 0)
    {
        auto* stem = model_.getStem(stemIndex_);
        if (stem)
        {
            const bool shift   = e.mods.isShiftDown();
            const bool ctrl    = e.mods.isCommandDown();   // Ctrl on Win/Linux, Cmd on Mac
            const bool isMulti = ctrl && shift;

            if (shift && !ctrl)
            {
                // Shift+click: toggle THIS segment, keep every other selection intact
                auto& seg = stem->segments[static_cast<size_t>(hitSeg)];
                seg.selected = !seg.selected;
                if (seg.selected && onSegmentSelected) onSegmentSelected(stemIndex_, hitSeg);
                multiDragIntent_ = false;
            }
            else if (isMulti)
            {
                // Ctrl+Shift+click on a segment. If already selected, preserve selection
                // and arm multi-drag. If not, select only it (fallback single), but still arm multi.
                auto& seg = stem->segments[static_cast<size_t>(hitSeg)];
                if (!seg.selected)
                {
                    for (auto& s : stem->segments) s.selected = false;
                    seg.selected = true;
                }
                if (onSegmentSelected) onSegmentSelected(stemIndex_, hitSeg);
                multiDragIntent_ = true;
            }
            else
            {
                // Plain click: clear SAME-stem selections, select this one.
                // (Cross-stem selections stay — Shift+click across stems composes.)
                for (auto& s : stem->segments) s.selected = false;
                stem->segments[static_cast<size_t>(hitSeg)].selected = true;
                if (onSegmentSelected) onSegmentSelected(stemIndex_, hitSeg);
                multiDragIntent_ = false;
            }

            if (e.mods.isAltDown())
            {
                // Alt+click = loop this segment
                if (onSegmentAltClicked) onSegmentAltClicked(stemIndex_, hitSeg);
            }
            else if (!shift && !ctrl)
            {
                // Normal click = play once
                if (onSegmentClicked) onSegmentClicked(stemIndex_, hitSeg);
            }
        }
        // Track for drag-out
        clickedSegIdx_ = hitSeg;
        dragOutStarted_ = false;
        repaint();
        return;
    }

    clickedSegIdx_ = -1;

    // Start creating a new segment. The undo entry is pushed in mouseUp, only
    // if a segment is actually created (B24).
    dragMode_ = DragMode::Creating;
    dragStartX_ = e.x;
}

void StemWaveformComponent::mouseDrag(const juce::MouseEvent& e)
{
    // Middle-button panning
    if (isPanning_)
    {
        double viewDuration = panStartViewEnd_ - panStartViewStart_;
        if (viewDuration <= 0.0 || waveArea_.getWidth() <= 0) return;
        double pixelsPerSecond = static_cast<double>(waveArea_.getWidth()) / viewDuration;
        double timeDelta = static_cast<double>(panStartX_ - e.x) / pixelsPerSecond;

        auto* stem = model_.getStem(stemIndex_);
        double totalDuration = stem ? static_cast<double>(stem->audioBuffer.getNumSamples()) / model_.getSampleRate() : viewDuration;

        double newStart = panStartViewStart_ + timeDelta;
        double newEnd = panStartViewEnd_ + timeDelta;

        // Clamp to audio bounds
        if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
        if (newEnd > totalDuration) { newStart -= (newEnd - totalDuration); newEnd = totalDuration; }
        newStart = juce::jmax(0.0, newStart);

        setViewRange(newStart, newEnd);
        if (onViewRangeChanged) onViewRangeChanged(newStart, newEnd);
        return;
    }

    // Drag-out: user clicked a segment and dragged > 8px — offer WAV export
    if (dragMode_ == DragMode::None && clickedSegIdx_ >= 0
        && !dragOutStarted_ && e.getDistanceFromDragStart() > 8 && e.mods.isLeftButtonDown())
    {
        dragOutStarted_ = true;
        if (multiDragIntent_ && onMultiSegmentDragOut)
            onMultiSegmentDragOut(stemIndex_, clickedSegIdx_);
        else if (onSegmentDragOut)
            onSegmentDragOut(stemIndex_, clickedSegIdx_);
        return;
    }

    if (dragMode_ == DragMode::Creating)
    {
        repaint();
    }
    else if (dragMode_ == DragMode::DraggingGrid)
    {
        // Convert pixel delta to time delta and apply to grid offset
        double timeDelta = pixelToTime(e.x) - pixelToTime(dragStartX_);
        gridOffsetSeconds_ = gridDragStartOffset_ + timeDelta;
        // Propagate to linked stems in real-time during drag
        if (onGridOffsetChanged) onGridOffsetChanged(stemIndex_, gridOffsetSeconds_);
        repaint();
    }
    else if (dragMode_ == DragMode::ResizingLeft || dragMode_ == DragMode::ResizingRight)
    {
        auto* seg = model_.getSegment(stemIndex_, activeSegIdx_);
        if (!seg) return;

        int64_t sample = pixelToSample(e.x);
        sample = snapToGrid(sample);

        const bool valid = dragMode_ == DragMode::ResizingLeft ? (sample < seg->endSample && sample != seg->startSample)
                                                               : (sample > seg->startSample && sample != seg->endSample);
        if (valid && ! resizeUndoPushed_)
        {
            model_.pushUndoState("Resize segment");   // state BEFORE the first change
            resizeUndoPushed_ = true;
            seg = model_.getSegment(stemIndex_, activeSegIdx_);
            if (! seg) return;
        }
        if (dragMode_ == DragMode::ResizingLeft)
        {
            if (valid)
                model_.resizeSegment(stemIndex_, activeSegIdx_, sample, seg->endSample);
        }
        else
        {
            if (valid)
                model_.resizeSegment(stemIndex_, activeSegIdx_, seg->startSample, sample);
        }
        repaint();
    }
}

void StemWaveformComponent::mouseUp(const juce::MouseEvent& e)
{
    if (isPanning_)
    {
        isPanning_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (dragMode_ == DragMode::Creating)
    {
        int x1 = juce::jmin(dragStartX_, e.x);
        int x2 = juce::jmax(dragStartX_, e.x);

        if (x2 - x1 > 4)
        {
            int64_t startSample = snapToGrid(pixelToSample(x1));
            int64_t endSample = snapToGrid(pixelToSample(x2));

            if (endSample > startSample)
            {
                model_.pushUndoState("Create segment");
                const int newIdx = model_.addSegment(stemIndex_, startSample, endSample);
                if (onSegmentsChanged) onSegmentsChanged();
                if (newIdx >= 0 && onSegmentSelected)
                {
                    if (auto* stem = model_.getStem(stemIndex_))
                    {
                        for (auto& s : stem->segments) s.selected = false;
                        if (newIdx < (int) stem->segments.size()) stem->segments[(size_t) newIdx].selected = true;
                    }
                    onSegmentSelected(stemIndex_, newIdx);
                }
            }
        }
    }
    else if (dragMode_ == DragMode::DraggingGrid)
    {
        juce::Logger::writeToLog("✅ [StemWaveform] Grid offset: " + juce::String(gridOffsetSeconds_, 3) + "s");
        // Fire view range changed so linked stems sync grid offset
        if (onViewRangeChanged) onViewRangeChanged(viewStartTime_, viewEndTime_);
    }
    else if (dragMode_ == DragMode::ResizingLeft || dragMode_ == DragMode::ResizingRight)
    {
        if (resizeUndoPushed_ && onSegmentsChanged) onSegmentsChanged();
    }

    resizeUndoPushed_ = false;
    dragMode_ = DragMode::None;
    activeSegIdx_ = -1;
    clickedSegIdx_ = -1;
    dragOutStarted_ = false;
    repaint();
}

void StemWaveformComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!waveArea_.contains(e.getPosition())) return;

    // Double-click on a segment → play it (delete via right-click context menu)
    int hitSeg = findSegmentAtX(e.x);
    if (hitSeg >= 0)
    {
        juce::Logger::writeToLog("🎵 [StemWaveform] Double-click play: stem=" + juce::String(stemIndex_)
                                 + " seg=" + juce::String(hitSeg));
        if (onSegmentClicked)
            onSegmentClicked(stemIndex_, hitSeg);
    }
}

void StemWaveformComponent::mouseEnter(const juce::MouseEvent&)
{
    // B27: never take keyboard focus on hover - inside a DAW that steals the
    // host's Space / QWERTY shortcuts. Focus is taken on click only (JUCE
    // does that for components with setWantsKeyboardFocus(true)); the wheel
    // zooms without focus.
}

void StemWaveformComponent::mouseExit(const juce::MouseEvent& e)
{
    bool needRepaint = false;
    if (gridTooltipVisible_) { gridTooltipVisible_ = false; needRepaint = true; }
    if (segTooltipVisible_) { segTooltipVisible_ = false; needRepaint = true; }

    // No lane-level hover dim — only the sequencer dots fire the cross-lane dim.
    juce::ignoreUnused(e);
    if (needRepaint) repaint();
}

void StemWaveformComponent::mouseWheelMove(const juce::MouseEvent& e,
                                            const juce::MouseWheelDetails& wheel)
{
    // Accept wheel events anywhere on the component (JUCE hit-tests before delivery)
    // Skip only if waveArea_ has zero size (not yet laid out)
    if (waveArea_.getWidth() <= 0) return;

    double viewDuration = viewEndTime_ - viewStartTime_;
    if (viewDuration <= 0.0) return;

    // Ignore near-zero deltas (touchpad noise)
    if (std::abs(wheel.deltaY) < 0.001f && std::abs(wheel.deltaX) < 0.001f) return;

    auto* stem = model_.getStem(stemIndex_);
    double totalDuration = stem ? static_cast<double>(stem->audioBuffer.getNumSamples()) / model_.getSampleRate() : viewDuration;

    // Alt+Wheel = Pan horizontally
    if (e.mods.isAltDown())
    {
        double scrollAmount = viewDuration * 0.1 * ((wheel.deltaY != 0.0f) ? -wheel.deltaY : wheel.deltaX);

        double newStart = viewStartTime_ + scrollAmount;
        double newEnd = viewEndTime_ + scrollAmount;

        if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
        if (newEnd > totalDuration) { newStart -= (newEnd - totalDuration); newEnd = totalDuration; }
        newStart = juce::jmax(0.0, newStart);

        setViewRange(newStart, newEnd);
        if (onViewRangeChanged) onViewRangeChanged(newStart, newEnd);
    }
    else
    {
        // Plain wheel / Ctrl+Wheel / Shift+Wheel = Zoom at cursor
        double zoomFactor = (wheel.deltaY > 0) ? 0.8 : 1.25;
        double cursorTime = pixelToTime(e.x);

        double cursorFrac = (cursorTime - viewStartTime_) / viewDuration;

        double newDuration = viewDuration * zoomFactor;
        newDuration = juce::jlimit(0.05, totalDuration, newDuration);

        double newStart = cursorTime - cursorFrac * newDuration;
        double newEnd = newStart + newDuration;

        if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
        if (newEnd > totalDuration) { newStart -= (newEnd - totalDuration); newEnd = totalDuration; }
        newStart = juce::jmax(0.0, newStart);

        setViewRange(newStart, newEnd);
        if (onViewRangeChanged) onViewRangeChanged(newStart, newEnd);
    }
}

void StemWaveformComponent::mouseMove(const juce::MouseEvent& e)
{
    if (!waveArea_.contains(e.getPosition()))
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        bool needRepaint = false;
        if (gridTooltipVisible_) { gridTooltipVisible_ = false; needRepaint = true; }
        if (segTooltipVisible_) { segTooltipVisible_ = false; needRepaint = true; }
        if (needRepaint) repaint();
        return;
    }

    // Grid line → horizontal drag cursor + tooltip
    if (gridBpm_ > 0.0 && isNearGridLine(e.x))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        if (segTooltipVisible_) { segTooltipVisible_ = false; }
        if (!gridTooltipVisible_ && tooltipsEnabled_)
        {
            gridTooltipVisible_ = true;
            gridTooltipPos_ = e.getPosition();
        }
        repaint();
        return;
    }

    if (gridTooltipVisible_)
    {
        gridTooltipVisible_ = false;
        repaint();
    }

    // Segment hover → show interaction tooltip
    int hitSeg = findSegmentAtX(e.x);
    if (hitSeg >= 0 && tooltipsEnabled_)
    {
        if (!segTooltipVisible_)
        {
            segTooltipVisible_ = true;
            segTooltipPos_ = e.getPosition();
            segTooltipText_ = "Click = Play  |  Alt+Click = Loop  |  Drag = WAV out  |  Right-Click = Menu";
            repaint();
        }
    }
    else if (segTooltipVisible_)
    {
        segTooltipVisible_ = false;
        repaint();
    }

    // Segment edge → resize cursor
    int edgeSeg = -1;
    auto edge = findSegmentEdge(e.x, edgeSeg);
    if (edge != EdgeHit::None)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

// ── Context Menu ─────────────────────────────────────────────────────────────

void StemWaveformComponent::showSegmentContextMenu(int segIdx, juce::Point<int> pos)
{
    auto* seg = model_.getSegment(stemIndex_, segIdx);
    if (!seg) return;

    juce::PopupMenu menu;

    // MIDI Learn — triggers the overlay dialog (press a key on controller)
    if (seg->midiNote >= 0)
    {
        menu.addItem(1, "Assigned to " + midiNoteToName(seg->midiNote) + " — Reassign...");
        menu.addItem(4, "Clear MIDI Assignment");
    }
    else
    {
        menu.addItem(1, "Assign MIDI Key...");
    }

    menu.addSeparator();
    menu.addItem(2, "Delete Segment");
    menu.addItem(3, "Select");

    // B7: the menu closes if the lane is deleted, and the callback re-finds the
    // segment by its sample range (indices go stale after edits / undo).
    const int64_t wantStart = seg->startSample, wantEnd = seg->endSample;
    juce::Component::SafePointer<StemWaveformComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withTargetScreenArea(localAreaToGlobal(juce::Rectangle<int>(pos.x, pos.y, 1, 1)))
                           .withDeletionCheck(*this),
        [safe, segIdx, wantStart, wantEnd](int menuResult)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || menuResult == 0) return;
            self->handleSegmentMenuResult(menuResult, segIdx, wantStart, wantEnd);
        });
}

void StemWaveformComponent::handleSegmentMenuResult(int result, int segIdxAtOpen, int64_t wantStart, int64_t wantEnd)
{
    int segIdx = -1;
    if (auto* st = model_.getStem(stemIndex_))
    {
        if (segIdxAtOpen >= 0 && segIdxAtOpen < (int) st->segments.size()
            && st->segments[(size_t) segIdxAtOpen].startSample == wantStart
            && st->segments[(size_t) segIdxAtOpen].endSample == wantEnd)
            segIdx = segIdxAtOpen;
        else
            for (int i = 0; i < (int) st->segments.size(); ++i)
                if (st->segments[(size_t) i].startSample == wantStart && st->segments[(size_t) i].endSample == wantEnd)
                { segIdx = i; break; }
    }
    if (segIdx < 0)
    {
        juce::Logger::writeToLog("[StemWaveform] segment menu: chop no longer exists - ignored");
        return;
    }
    {
        {

            if (result == 1)  // Assign MIDI Key via learn overlay
            {
                if (onMidiAssignRequested)
                    onMidiAssignRequested(stemIndex_, segIdx);
            }
            else if (result == 2)  // Delete
            {
                auto* stem = model_.getStem(stemIndex_);
                const int beforeCount = stem ? static_cast<int>(stem->segments.size()) : -1;
                juce::Logger::writeToLog("[SMode] deleteSegment stem=" + juce::String(stemIndex_)
                                         + " segIdx=" + juce::String(segIdx)
                                         + " before=" + juce::String(beforeCount));
                model_.pushUndoState("Delete segment");
                model_.removeSegment(stemIndex_, segIdx);
                const int afterCount = stem ? static_cast<int>(stem->segments.size()) : -1;
                juce::Logger::writeToLog("[SMode] deleteSegment stem=" + juce::String(stemIndex_)
                                         + " after=" + juce::String(afterCount));
                // Use the delete-specific callback so the page mirrors ONLY this
                // segment's removal on linked stems (surgical erase), instead of
                // the clear-and-rebuild path in onSegmentsChanged which used to
                // wipe every segment on every linked stem when one was deleted.
                if (onSegmentDeleted) onSegmentDeleted(segIdx, wantStart, wantEnd);
                else if (onSegmentsChanged) onSegmentsChanged();
                repaint();
            }
            else if (result == 3)  // Select
            {
                auto* stem = model_.getStem(stemIndex_);
                if (stem)
                {
                    for (auto& s : stem->segments) s.selected = false;
                    if (segIdx < static_cast<int>(stem->segments.size()))
                        stem->segments[static_cast<size_t>(segIdx)].selected = true;
                }
                repaint();
            }
            else if (result == 4)  // Clear MIDI
            {
                model_.pushUndoState("Clear MIDI note");
                model_.clearMidiNote(stemIndex_, segIdx);
                repaint();
            }
        }
    }
}

// ── Coordinate Helpers ───────────────────────────────────────────────────────

double StemWaveformComponent::pixelToTime(int x) const
{
    double duration = viewEndTime_ - viewStartTime_;
    if (duration <= 0.0 || waveArea_.getWidth() <= 0) return 0.0;

    double frac = static_cast<double>(x - waveArea_.getX()) / waveArea_.getWidth();
    frac = juce::jlimit(0.0, 1.0, frac);
    return viewStartTime_ + frac * duration;
}

int StemWaveformComponent::timeToPixel(double time) const
{
    double duration = viewEndTime_ - viewStartTime_;
    if (duration <= 0.0 || waveArea_.getWidth() <= 0) return waveArea_.getX();

    double frac = (time - viewStartTime_) / duration;
    return waveArea_.getX() + static_cast<int>(frac * waveArea_.getWidth());
}

int64_t StemWaveformComponent::pixelToSample(int x) const
{
    return static_cast<int64_t>(pixelToTime(x) * model_.getSampleRate());
}

int StemWaveformComponent::sampleToPixel(int64_t sample) const
{
    double time = static_cast<double>(sample) / model_.getSampleRate();
    return timeToPixel(time);
}

int64_t StemWaveformComponent::snapToGrid(int64_t sample) const
{
    if (!snapEnabled_ || gridBpm_ <= 0.0 || freeformEnabled_) return sample;

    double sampleRate = model_.getSampleRate();
    double beatSamples = (60.0 / gridBpm_) * sampleRate;
    // Subdivision samples: 1/divisionValue_ of a whole note.
    // Quarter (div=4) → beatSamples; Eighth (div=8) → beatSamples/2; etc.
    double divSamples = beatSamples * (4.0 / (double) divisionValue_);
    if (divSamples <= 0.0) return sample;

    // Account for grid offset
    double offsetSamples = gridOffsetSeconds_ * sampleRate;
    double adjusted = static_cast<double>(sample) - offsetSamples;
    int64_t lineIndex = static_cast<int64_t>(std::round(adjusted / divSamples));
    return static_cast<int64_t>(lineIndex * divSamples + offsetSamples);
}

// ── Hit Testing ──────────────────────────────────────────────────────────────

int StemWaveformComponent::findSegmentAtX(int x) const
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem) return -1;

    for (int i = static_cast<int>(stem->segments.size()) - 1; i >= 0; --i)
    {
        int sx1 = sampleToPixel(stem->segments[static_cast<size_t>(i)].startSample);
        int sx2 = sampleToPixel(stem->segments[static_cast<size_t>(i)].endSample);
        if (x >= sx1 && x <= sx2)
            return i;
    }
    return -1;
}

StemWaveformComponent::EdgeHit StemWaveformComponent::findSegmentEdge(int x, int& outSegIdx) const
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem) return EdgeHit::None;

    for (int i = static_cast<int>(stem->segments.size()) - 1; i >= 0; --i)
    {
        int sx1 = sampleToPixel(stem->segments[static_cast<size_t>(i)].startSample);
        int sx2 = sampleToPixel(stem->segments[static_cast<size_t>(i)].endSample);

        if (std::abs(x - sx1) <= kEdgeHitZone) { outSegIdx = i; return EdgeHit::Left; }
        if (std::abs(x - sx2) <= kEdgeHitZone) { outSegIdx = i; return EdgeHit::Right; }
    }
    return EdgeHit::None;
}

bool StemWaveformComponent::isNearGridLine(int x) const
{
    if (gridBpm_ <= 0.0) return false;

    double beatDuration = 60.0 / gridBpm_;
    double divInterval  = beatDuration * (4.0 / (double) divisionValue_);
    double duration     = viewEndTime_ - viewStartTime_;
    if (duration <= 0.0 || divInterval <= 0.0) return false;

    double firstLine = gridOffsetSeconds_;
    if (firstLine < viewStartTime_)
    {
        double stepsToSkip = std::floor((viewStartTime_ - firstLine) / divInterval);
        firstLine += stepsToSkip * divInterval;
    }

    for (double t = firstLine; t < viewEndTime_; t += divInterval)
    {
        if (t < viewStartTime_) continue;
        int gx = timeToPixel(t);
        if (std::abs(x - gx) <= kGridHitZone)
            return true;
    }
    return false;
}

// ── Playhead Drawing ─────────────────────────────────────────────────────────

void StemWaveformComponent::drawPlayhead(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (playheadTime_ < 0.0) return;
    if (playheadTime_ < viewStartTime_ || playheadTime_ > viewEndTime_) return;

    int x = timeToPixel(playheadTime_);
    g.setColour(juce::Colours::white);
    g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawVerticalLine(x - 1, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    g.drawVerticalLine(x + 1, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
}

// ── Empty Area Context Menu ─────────────────────────────────────────────────

void StemWaveformComponent::showEmptyAreaContextMenu(juce::Point<int> pos)
{
    juce::PopupMenu gridMenu;
    gridMenu.addItem(101, "1/2 Note");
    gridMenu.addItem(102, "1/4 Note (Beat)");
    gridMenu.addItem(103, "1/8 Note");
    gridMenu.addItem(104, "1/16 Note");

    juce::PopupMenu menu;
    menu.addSubMenu("Quick Assign by Grid", gridMenu);

    juce::Component::SafePointer<StemWaveformComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withTargetScreenArea(localAreaToGlobal(juce::Rectangle<int>(pos.x, pos.y, 1, 1)))
                           .withDeletionCheck(*this),
        [safe](int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || result == 0) return;
            double beats = 1.0;
            switch (result)
            {
                case 101: beats = 2.0; break;
                case 102: beats = 1.0; break;
                case 103: beats = 0.5; break;
                case 104: beats = 0.25; break;
                default: return;
            }
            if (self->onGridQuickAssign) self->onGridQuickAssign(self->stemIndex_, beats);
        });
}

void StemWaveformComponent::refreshStemButtons()
{
    auto* stem = model_.getStem(stemIndex_);
    const bool solo = stem != nullptr && stem->solo;
    const bool mute = stem != nullptr && stem->mute;
    soloBtn_.setColour(juce::TextButton::textColourOffId, solo ? juce::Colours::yellow : juce::Colours::lightgrey);
    soloBtn_.setColour(juce::TextButton::buttonColourId, solo ? juce::Colour(0xff5a5200) : juce::Colour(0xff2d2d3a));
    muteBtn_.setColour(juce::TextButton::textColourOffId, mute ? juce::Colours::red : juce::Colours::lightgrey);
    muteBtn_.setColour(juce::TextButton::buttonColourId, mute ? juce::Colour(0xff5a1a1a) : juce::Colour(0xff2d2d3a));
    soloBtn_.repaint();
    muteBtn_.repaint();
}

void StemWaveformComponent::refreshLinkButton()
{
    const bool linked = model_.isLinked(stemIndex_);
    linkBtn_.setColour(juce::TextButton::textColourOffId,
                       linked ? juce::Colours::cyan : juce::Colours::grey);
    linkBtn_.setColour(juce::TextButton::buttonColourId,
                       linked ? juce::Colour(0xff1e4d2e) : juce::Colour(0xff2d2d3a));
    linkBtn_.repaint();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

juce::String StemWaveformComponent::midiNoteToName(int note)
{
    // B22: same convention as MidiMappingService::noteNumberToName (JUCE
    // default, middle C = 60 = C3).
    return juce::MidiMessage::getMidiNoteName(juce::jlimit(0, 127, note), true, true, 3);
}

void StemWaveformComponent::flashSegment(int segIndex)
{
    // Default flash uses segment duration from model
    auto* seg = model_.getSegment(stemIndex_, segIndex);
    double dur = 0.3;  // fallback
    if (seg)
        dur = seg->getDurationSeconds(model_.getSampleRate());
    startSegmentAnimation(segIndex, dur);
}

void StemWaveformComponent::startSegmentAnimation(int segIndex, double durationSec, bool looping)
{
    SegmentAnim anim;
    anim.startTime = juce::Time::getMillisecondCounterHiRes();
    anim.durationSec = juce::jmax(durationSec, 0.05);
    anim.progress = 0.0f;
    anim.active = true;
    anim.looping = looping;
    segmentAnims_[segIndex] = anim;

    if (!isTimerRunning())
        startTimerHz(30);  // ~30fps animation

    repaint();
}

void StemWaveformComponent::stopSegmentAnimation(int segIndex)
{
    segmentAnims_.erase(segIndex);
    if (segmentAnims_.empty())
        stopTimer();
    repaint();
}

void StemWaveformComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool anyActive = false;

    for (auto it = segmentAnims_.begin(); it != segmentAnims_.end(); )
    {
        auto& anim = it->second;
        double elapsed = (now - anim.startTime) / 1000.0;
        anim.progress = static_cast<float>(juce::jlimit(0.0, 1.0, elapsed / anim.durationSec));

        if (anim.progress >= 1.0f)
        {
            if (anim.looping)
            {
                // Wrap: restart the animation from the beginning
                anim.startTime = now;
                anim.progress = 0.0f;
                anyActive = true;
                ++it;
            }
            else
            {
                it = segmentAnims_.erase(it);
            }
        }
        else
        {
            anyActive = true;
            ++it;
        }
    }

    // Advance loading shimmer phase (wraps 0..1 for sin-based leading-edge glow)
    if (loadingState_)
    {
        loadingPhase_ += 0.033f;  // ~1 shimmer cycle per second at 30 Hz
        if (loadingPhase_ > 1.0f) loadingPhase_ -= 1.0f;
        anyActive = true;
    }

    repaint();

    if (!anyActive)
        stopTimer();
}

// ── Pitch knob value sync (no callback fire) ─────────────────────────────────
void StemWaveformComponent::setPitchSemitones(float semi)
{
    if (! std::isfinite(semi)) return;
    if (pitchKnob_.isMouseButtonDown()) return;   // never fight the user's drag
    const float clamped = juce::jlimit(-12.0f, 12.0f, semi);
    if (std::abs(pitchKnob_.getValue() - (double) clamped) < 1.0e-6) return;
    pitchKnob_.setValue((double) clamped, juce::dontSendNotification);
}

float StemWaveformComponent::getPitchSemitones() const
{
    return (float) pitchKnob_.getValue();
}

} // namespace samplar
