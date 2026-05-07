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
        soloBtn_.setColour(juce::TextButton::textColourOffId,
                           stem->solo ? juce::Colours::yellow : juce::Colours::grey);
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
        muteBtn_.setColour(juce::TextButton::textColourOffId,
                           stem->mute ? juce::Colours::red : juce::Colours::grey);
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
        juce::Path eyePath;
        // Almond eye outline
        eyePath.startNewSubPath(1.0f, 10.0f);
        eyePath.quadraticTo(10.0f, 2.0f, 19.0f, 10.0f);
        eyePath.quadraticTo(10.0f, 18.0f, 1.0f, 10.0f);
        eyePath.closeSubPath();
        // Pupil
        eyePath.addEllipse(7.0f, 7.0f, 6.0f, 6.0f);

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

    setOpaque(false);
    setWantsKeyboardFocus(true);

    // Initialize view range from stem audio
    auto* stem = model_.getStem(stemIndex_);
    if (stem && stem->audioBuffer.getNumSamples() > 0)
        viewEndTime_ = static_cast<double>(stem->audioBuffer.getNumSamples()) / model_.getSampleRate();
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
    juce::Logger::writeToLog("✅ [StemWaveform] Spectral colors: " + juce::String(spectralColors_.size()) + " slices");
    repaint();
}

void StemWaveformComponent::computeSpectralColorsAsync()
{
    auto* stem = model_.getStem(stemIndex_);
    if (!stem || stem->audioBuffer.getNumSamples() == 0) return;

    // ── Fast path: reuse cached colors from the model (survives editor close/reopen) ──
    if (stem->spectralReady && !stem->spectralColors.empty())
    {
        spectralColors_ = stem->spectralColors;
        spectralDuration_ = stem->spectralDuration;
        spectralReady_ = true;
        repaint();
        juce::Logger::writeToLog("[StemWaveform] Spectral colors restored from cache ("
                                 + juce::String(spectralColors_.size()) + " slices)");
        return;
    }

    // ── Slow path: compute on background thread ──
    // Get raw pointers + metadata for the thread. The audio buffers are owned
    // by the processor-lifetime SModeSegmentModel — they won't be deleted
    // while we read them.
    const float* const* readPtrs = stem->audioBuffer.getArrayOfReadPointers();
    int numCh = stem->audioBuffer.getNumChannels();
    int totalSmp = stem->audioBuffer.getNumSamples();
    double sr = model_.getSampleRate();
    int stemIdx = stemIndex_;

    // Move the buffer COPY into the background thread instead of blocking
    // the message thread. Raw pointers are safe because the model outlives
    // any editor component.
    std::thread([this, readPtrs, totalSmp, sr, numCh, stemIdx]() mutable
    {
        // Copy audio data HERE (background thread, not message thread)
        std::vector<std::vector<float>> channelData;
        channelData.reserve(static_cast<size_t>(numCh));
        for (int ch = 0; ch < numCh; ++ch)
            channelData.emplace_back(readPtrs[ch], readPtrs[ch] + totalSmp);
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
            int64_t sliceStart = static_cast<int64_t>(slice) * samplesPerSlice;
            int samplesToRead = juce::jmin(fftSize, static_cast<int>(totalSmp - sliceStart));
            if (samplesToRead <= 0) { colors.push_back(juce::Colours::darkgrey); continue; }

            std::fill(fftData.begin(), fftData.end(), 0.0f);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* data = channelData[static_cast<size_t>(ch)].data();
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
        juce::MessageManager::callAsync([this, colors = std::move(colors), duration, stemIdx]()
        {
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

            repaint();
            juce::Logger::writeToLog("✅ [StemWaveform] Spectral async done: " + juce::String(spectralColors_.size()) + " slices (cached in model)");
        });
    }).detach();
}

// ── View Range ───────────────────────────────────────────────────────────────

void StemWaveformComponent::setViewRange(double startTime, double endTime)
{
    viewStartTime_ = startTime;
    viewEndTime_ = endTime;
    repaint();
}

// ── Layout ───────────────────────────────────────────────────────────────────

void StemWaveformComponent::resized()
{
    auto area = getLocalBounds();
    controlArea_ = area.removeFromLeft(kControlWidth);
    waveArea_ = area;

    // Stack S/M/L/V buttons vertically in controlArea_
    auto ctrlBounds = controlArea_.reduced(2);
    int btnH = juce::jmin(ctrlBounds.getHeight() / 4, 16);
    soloBtn_.setBounds(ctrlBounds.removeFromTop(btnH));
    ctrlBounds.removeFromTop(1);
    muteBtn_.setBounds(ctrlBounds.removeFromTop(btnH));
    ctrlBounds.removeFromTop(1);
    linkBtn_.setBounds(ctrlBounds.removeFromTop(btnH));
    ctrlBounds.removeFromTop(1);
    focusBtn_.setBounds(ctrlBounds.removeFromTop(btnH));
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
    // Background
    g.setColour(stemColour_.withAlpha(0.12f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);

    // Border
    g.setColour(stemColour_.withAlpha(0.5f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 3.0f, 1.5f);

    if (waveArea_.isEmpty()) return;

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
    drawWaveform(g, waveArea_);
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
    juce::String text = "Ctrl + Click = Drag";
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

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(juce::Rectangle<int>(e.x, e.y, 1, 1)).toFloat().toNearestInt()),
            [this](int result)
            {
                if (result == 1)
                {
                    excludedFromQuickAssign_ = !excludedFromQuickAssign_;
                    juce::Logger::writeToLog("[StemWaveform] Stem " + juce::String(stemIndex_)
                                             + " quick assign: " + juce::String(excludedFromQuickAssign_ ? "EXCLUDED" : "INCLUDED"));
                    if (onQuickAssignExcludeChanged)
                        onQuickAssignExcludeChanged(stemIndex_, excludedFromQuickAssign_);
                }
                else if (result == 2)
                {
                    if (onRemoveRequested)
                        onRemoveRequested(stemIndex_);
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

    // Grid drag: Ctrl+Click anywhere (not just near grid lines)
    if (gridBpm_ > 0.0 && e.mods.isCtrlDown())
    {
        dragMode_ = DragMode::DraggingGrid;
        dragStartX_ = e.x;
        gridDragStartOffset_ = gridOffsetSeconds_;
        juce::Logger::writeToLog("🎵 [StemWaveform] Grid drag (Ctrl+Click) at x=" + juce::String(e.x));
        return;
    }

    // Check for segment edge (resize)
    int edgeSeg = -1;
    auto edge = findSegmentEdge(e.x, edgeSeg);
    if (edge != EdgeHit::None)
    {
        dragMode_ = (edge == EdgeHit::Left) ? DragMode::ResizingLeft : DragMode::ResizingRight;
        activeSegIdx_ = edgeSeg;
        model_.pushUndoState("Resize segment");
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

    // Start creating a new segment
    dragMode_ = DragMode::Creating;
    dragStartX_ = e.x;
    model_.pushUndoState("Create segment");
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
        && !dragOutStarted_ && e.getDistanceFromDragStart() > 8)
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

        if (dragMode_ == DragMode::ResizingLeft)
        {
            if (sample < seg->endSample)
                model_.resizeSegment(stemIndex_, activeSegIdx_, sample, seg->endSample);
        }
        else
        {
            if (sample > seg->startSample)
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
                model_.addSegment(stemIndex_, startSample, endSample);
                if (onSegmentsChanged) onSegmentsChanged();
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
        if (onSegmentsChanged) onSegmentsChanged();
    }

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
    // Grab keyboard focus on mouse enter so Ctrl modifier is reliably detected
    // for Ctrl+Wheel zoom on Windows
    if (!hasKeyboardFocus(false))
        grabKeyboardFocus();
}

void StemWaveformComponent::mouseExit(const juce::MouseEvent&)
{
    bool needRepaint = false;
    if (gridTooltipVisible_) { gridTooltipVisible_ = false; needRepaint = true; }
    if (segTooltipVisible_) { segTooltipVisible_ = false; needRepaint = true; }
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
            segTooltipText_ = "Click = Play  |  Alt+Click = Loop  |  Right-Click = Menu";
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

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        localAreaToGlobal(juce::Rectangle<int>(pos.x, pos.y, 1, 1)).toFloat().toNearestInt()),
        [this, segIdx](int result)
        {
            if (result == 0) return;

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
                if (onSegmentDeleted) onSegmentDeleted(segIdx);
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
        });
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

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        localAreaToGlobal(juce::Rectangle<int>(pos.x, pos.y, 1, 1)).toFloat().toNearestInt()),
        [this](int result)
        {
            if (result == 0) return;
            double beats = 1.0;
            switch (result)
            {
                case 101: beats = 2.0; break;
                case 102: beats = 1.0; break;
                case 103: beats = 0.5; break;
                case 104: beats = 0.25; break;
                default: return;
            }
            if (onGridQuickAssign) onGridQuickAssign(stemIndex_, beats);
        });
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
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (note / 12) - 1;
    int nameIdx = note % 12;
    return juce::String(noteNames[nameIdx]) + juce::String(octave);
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

    repaint();

    if (!anyActive)
        stopTimer();
}

} // namespace samplar
