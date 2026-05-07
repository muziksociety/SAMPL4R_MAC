#pragma once

#ifdef WITH_SOUNDTOUCH

#include <juce_audio_basics/juce_audio_basics.h>
#include <SoundTouch.h>

namespace samplar {

// A JUCE AudioSource that time-stretches / pitch-shifts using SoundTouch,
// wrapping an underlying input AudioSource.
class SoundTouchSource : public juce::AudioSource
{
public:
    SoundTouchSource(juce::AudioSource* input, bool deleteInputWhenDeleted = false)
        : input_(input), deleteInput_(deleteInputWhenDeleted)
    {
        st_.setChannels(2);
        // NOTE: Sample rate will be set correctly in prepareToPlay()
        // Using 48000 as default since it's more common for audio devices
        // and safer than 44100 if prepareToPlay isn't called immediately
        st_.setSampleRate(48000);
        st_.setSetting(SETTING_USE_QUICKSEEK, 1);
        st_.setSetting(SETTING_SEQUENCE_MS, 40);
        st_.setSetting(SETTING_SEEKWINDOW_MS, 20);
        st_.setSetting(SETTING_OVERLAP_MS, 8);
    }

    ~SoundTouchSource() override
    {
        if (deleteInput_) delete input_;
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        juce::ignoreUnused(samplesPerBlockExpected);
        juce::Logger::writeToLog("[SoundTouchSource] prepareToPlay called - sampleRate: " + juce::String(sampleRate) + " Hz");
        sampleRate_ = sampleRate;
        st_.setSampleRate((uint) sampleRate_);
        if (input_) input_->prepareToPlay(samplesPerBlockExpected, sampleRate);
        inputFifo_.setSize(2, juce::jmax(4096, samplesPerBlockExpected * 3));
        tmpBlock_.setSize(2, 2048);
        st_.clear();
    }

    void releaseResources() override
    {
        if (input_) input_->releaseResources();
        inputFifo_.setSize(0, 0);
        tmpBlock_.setSize(0, 0);
        st_.clear();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        auto* outL = info.buffer->getWritePointer(0, info.startSample);
        auto* outR = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
        auto toProduce = info.numSamples;
        int produced = 0;

        while (produced < toProduce)
        {
            // First, try to receive from SoundTouch buffer
            int outAvail = (int) st_.numSamples();
            if (outAvail > 0)
            {
                int want = juce::jmin(outAvail, toProduce - produced);
                ensureInterleavedOutSize(want);
                st_.receiveSamples(interleavedOut_.data(), (uint) want);
                deinterleaveToOutput(interleavedOut_.data(), want, outL + produced, outR ? outR + produced : nullptr);
                produced += want;
                continue;
            }

            // If SoundTouch has nothing, feed more from input
            if (input_ != nullptr)
            {
                juce::AudioSourceChannelInfo inInfo(&tmpBlock_, 0, tmpBlock_.getNumSamples());
                tmpBlock_.clear();
                input_->getNextAudioBlock(inInfo);
                if (inInfo.numSamples > 0)
                {
                    feedToSoundTouch(tmpBlock_, inInfo.numSamples);
                    continue;
                }
            }

            // If input is exhausted, flush SoundTouch and fill rest with zeros
            st_.flush();
            int want = toProduce - produced;
            if (outR) { juce::FloatVectorOperations::clear(outR, want); }
            juce::FloatVectorOperations::clear(outL, want);
            produced += want;
            break;
        }
    }

    // tempo in ratio (1.0 = original), pitch in semitones
    void setTempoRatio(double ratio)
    {
        tempo_ = ratio;
        st_.setTempo((float) tempo_);
    }

    void setPitchSemitones(double semi)
    {
        pitchSemi_ = semi;
        st_.setPitchSemiTones((float) pitchSemi_);
    }

private:
    void ensureTmpBlockSize(int samples)
    {
        if (tmpBlock_.getNumSamples() < samples)
            tmpBlock_.setSize(2, samples, true, true, true);
    }

    void feedToSoundTouch(const juce::AudioBuffer<float>& buf, int numSamples)
    {
        // SoundTouch expects interleaved samples
        ensureInterleavedInSize(numSamples);
        interleaveFromBuffer(buf, numSamples, interleavedIn_.data());
        st_.putSamples(interleavedIn_.data(), (uint) numSamples);
    }

    static void deinterleaveToOutput(const float* interleaved, int numSamples, float* dstL, float* dstR)
    {
        if (dstR == nullptr)
        {
            // mono out: mix down
            for (int i = 0; i < numSamples; ++i)
            {
                float l = interleaved[2 * i];
                float r = interleaved[2 * i + 1];
                dstL[i] = 0.5f * (l + r);
            }
            return;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            dstL[i] = interleaved[2 * i];
            dstR[i] = interleaved[2 * i + 1];
        }
    }

    static void interleaveFromBuffer(const juce::AudioBuffer<float>& src, int numSamples, float* dstInterleaved)
    {
        auto* l = src.getReadPointer(0);
        auto* r = src.getNumChannels() > 1 ? src.getReadPointer(1) : nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            dstInterleaved[2 * i]     = l[i];
            dstInterleaved[2 * i + 1] = r ? r[i] : l[i];
        }
    }

    void ensureInterleavedInSize(int samples)
    {
        if ((int)interleavedIn_.size() < samples * 2)
            interleavedIn_.resize(samples * 2);
    }

    void ensureInterleavedOutSize(int samples)
    {
        if ((int)interleavedOut_.size() < samples * 2)
            interleavedOut_.resize(samples * 2);
    }

    juce::AudioSource* input_ { nullptr };
    bool deleteInput_ { false };
    double sampleRate_ { 44100.0 };
    double tempo_ { 1.0 };
    double pitchSemi_ { 0.0 };

    soundtouch::SoundTouch st_;
    juce::AudioBuffer<float> inputFifo_;
    juce::AudioBuffer<float> tmpBlock_;
    std::vector<float> interleavedIn_;
    std::vector<float> interleavedOut_;
};

} // namespace samplar

#endif // WITH_SOUNDTOUCH
