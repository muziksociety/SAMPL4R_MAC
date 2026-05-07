#include "BandProcessor.h"

namespace samplar {

BandProcessor::BandProcessor()
{
    juce::Logger::writeToLog("[BandProcessor] Initialized");
}

void BandProcessor::process(
    juce::AudioBuffer<float>& buffer,
    double sampleRate,
    float bandLowFreq,
    float bandHighFreq,
    const Settings& settings)
{
    if (buffer.getNumSamples() == 0)
        return;

    // Calculate actual HP/LP cutoff frequencies based on normalized position
    float bandWidth = bandHighFreq - bandLowFreq;

    // HP filter - cuts from low end of band
    if (settings.hpNormalized > 0.001f)
    {
        float hpCutoff = bandLowFreq + (settings.hpNormalized * bandWidth);
        applyHPFilter(buffer, hpCutoff, sampleRate);
    }

    // LP filter - cuts from high end of band
    if (settings.lpNormalized > 0.001f)
    {
        float lpCutoff = bandHighFreq - (settings.lpNormalized * bandWidth);
        applyLPFilter(buffer, lpCutoff, sampleRate);
    }

    // Transient boost (applied before gates so boosted transients can be gated if needed)
    if (settings.transientBoostDb > 0.1f)
    {
        applyTransientBoost(buffer, settings.transientBoostDb, sampleRate);
    }

    // Floor gate - zeros samples below threshold
    if (settings.floorGateEnabled && settings.floorGateThreshold > 0.001f)
    {
        applyFloorGate(buffer, settings.floorGateThreshold);
    }

    // Ceiling gate - zeros samples above threshold
    if (settings.ceilingGateEnabled && settings.ceilingGateThreshold < 0.999f)
    {
        applyCeilingGate(buffer, settings.ceilingGateThreshold);
    }
}

juce::AudioBuffer<float> BandProcessor::computeDisplayBuffer(
    const juce::AudioBuffer<float>& source,
    double sampleRate,
    float bandLowFreq,
    float bandHighFreq,
    const Settings& settings)
{
    // Make a copy and process it
    juce::AudioBuffer<float> result;
    result.makeCopyOf(source);
    process(result, sampleRate, bandLowFreq, bandHighFreq, settings);
    return result;
}

std::vector<float> BandProcessor::computeWaveformPeaks(
    const juce::AudioBuffer<float>& buffer,
    int numColumns)
{
    std::vector<float> peaks(numColumns, 0.0f);

    if (buffer.getNumSamples() == 0 || numColumns <= 0)
        return peaks;

    const int samplesPerColumn = juce::jmax(1, buffer.getNumSamples() / numColumns);

    for (int col = 0; col < numColumns; ++col)
    {
        int startSample = col * samplesPerColumn;
        int endSample = juce::jmin(startSample + samplesPerColumn, buffer.getNumSamples());

        float maxPeak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            for (int i = startSample; i < endSample; ++i)
            {
                maxPeak = juce::jmax(maxPeak, std::abs(data[i]));
            }
        }
        peaks[col] = juce::jmin(1.0f, maxPeak);
    }

    return peaks;
}

void BandProcessor::applyHPFilter(juce::AudioBuffer<float>& buffer, float cutoffFreq, double sampleRate)
{
    // Clamp cutoff to valid range
    cutoffFreq = juce::jlimit(20.0f, static_cast<float>(sampleRate) * 0.45f, cutoffFreq);

    // Create HP filter coefficients
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, cutoffFreq, 0.707f);

    // Process each channel
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        juce::dsp::IIR::Filter<float> filter;
        filter.coefficients = coeffs;
        filter.reset();

        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            data[i] = filter.processSample(data[i]);
        }
    }
}

void BandProcessor::applyLPFilter(juce::AudioBuffer<float>& buffer, float cutoffFreq, double sampleRate)
{
    // Clamp cutoff to valid range
    cutoffFreq = juce::jlimit(20.0f, static_cast<float>(sampleRate) * 0.45f, cutoffFreq);

    // Create LP filter coefficients
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, cutoffFreq, 0.707f);

    // Process each channel
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        juce::dsp::IIR::Filter<float> filter;
        filter.coefficients = coeffs;
        filter.reset();

        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            data[i] = filter.processSample(data[i]);
        }
    }
}

void BandProcessor::applyFloorGate(juce::AudioBuffer<float>& buffer, float threshold)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::abs(data[i]) < threshold)
            {
                data[i] = 0.0f;
            }
        }
    }
}

void BandProcessor::applyCeilingGate(juce::AudioBuffer<float>& buffer, float threshold)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::abs(data[i]) > threshold)
            {
                data[i] = 0.0f;
            }
        }
    }
}

void BandProcessor::applyTransientBoost(juce::AudioBuffer<float>& buffer, float boostDb, double sampleRate)
{
    // Transient boost using envelope follower (same algorithm as LarsDebugDialog)
    // Fast attack (1ms) to catch transients, slow release (100ms)
    const float attackMs = 1.0f;
    const float releaseMs = 100.0f;
    const float attackCoeff = std::exp(-1.0f / (attackMs * 0.001f * static_cast<float>(sampleRate)));
    const float releaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate)));

    float boostLinear = juce::Decibels::decibelsToGain(boostDb);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        float envelope = 0.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float absVal = std::abs(data[i]);

            // Envelope follower
            if (absVal > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * absVal;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * absVal;

            // Transient = input exceeds envelope (fast attack means this catches transients)
            float transient = (envelope > 0.0001f) ? juce::jmax(0.0f, (absVal - envelope) / envelope) : 0.0f;
            transient = juce::jmin(1.0f, transient);

            // Apply progressive boost to transients
            float gain = 1.0f + transient * (boostLinear - 1.0f);
            data[i] *= gain;
        }
    }
}

} // namespace samplar
