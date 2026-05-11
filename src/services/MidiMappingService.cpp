#include "MidiMappingService.h"
#include "../audio/MicroFade.h"

// Realtime pitch shifter — replaces SoundTouch in the audio path.
// MIT-licensed, header-only, designed for realtime music. Internal framing
// is handled by the library (no FIFO/prime/drain plumbing needed).
#include "signalsmith-stretch.h"

#include <thread>
#include <chrono>
#include <cmath>

namespace samplar {

// ────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ────────────────────────────────────────────────────────────────────────────

MidiMappingService::MidiMappingService(EventBus& bus)
    : bus_(bus)
{
    juce::Logger::writeToLog("[MidiMappingService] Constructed");
}

MidiMappingService::~MidiMappingService()
{
    juce::Logger::writeToLog("[MidiMappingService] Destroyed");
}

// ────────────────────────────────────────────────────────────────────────────
// IService
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::initialise()
{
    juce::Logger::writeToLog("[MidiMappingService] === initialise START ===");

    // Subscribe: SampleBrowserPage publishes this when the visible page changes
    browserPageHandler_ = bus_.subscribe("browser.page.changed",
        [this](const juce::var& payload)
        {
            if (!payload.isArray()) return;

            const auto* arr = payload.getArray();
            if (arr == nullptr) return;

            std::vector<juce::File> files;
            files.reserve(static_cast<size_t>(arr->size()));
            for (const auto& v : *arr)
                files.push_back(juce::File(v.toString()));

            juce::Logger::writeToLog("[MidiMappingService] Auto-mapping "
                + juce::String(files.size()) + " browser samples");
            autoMapBrowserPage(files);
        });

    juce::Logger::writeToLog("[MidiMappingService] === initialise END ===");
}

void MidiMappingService::shutdown()
{
    juce::Logger::writeToLog("[MidiMappingService] shutdown");

    // Mark as destroyed FIRST so any in-flight callAsync lambdas (MIDI learn)
    // will see the flag and bail out before touching `this`.
    destroyed_.store(true, std::memory_order_seq_cst);
    midiLearnActive_.store(false, std::memory_order_seq_cst);

    // Disconnect from standalone MIDI input
    disconnectFromDeviceManager();

    if (browserPageHandler_ != 0)
    {
        bus_.unsubscribe("browser.page.changed", browserPageHandler_);
        browserPageHandler_ = 0;
    }

    // Unregister cache listener to prevent dangling pointer
    if (cache_ != nullptr)
    {
        cache_->removeListener(this);
        cache_ = nullptr;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Standalone MIDI device input
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::connectToDeviceManager(juce::AudioDeviceManager* dm)
{
    if (dm == nullptr) return;

    // Disconnect from any previous manager
    disconnectFromDeviceManager();

    connectedDeviceManager_ = dm;

    // Register for ALL MIDI devices (empty string = wildcard)
    dm->addMidiInputDeviceCallback({}, this);

    // Audio output: bridge this AudioSource → AudioIODeviceCallback → device
    audioPlayer_.setSource(this);
    dm->addAudioCallback(&audioPlayer_);

    juce::Logger::writeToLog("[MidiMappingService] Connected to AudioDeviceManager for MIDI input + audio output");
}

void MidiMappingService::disconnectFromDeviceManager()
{
    if (connectedDeviceManager_ != nullptr) {
        connectedDeviceManager_->removeAudioCallback(&audioPlayer_);
        audioPlayer_.setSource(nullptr);
        connectedDeviceManager_->removeMidiInputDeviceCallback({}, this);
        connectedDeviceManager_ = nullptr;
        juce::Logger::writeToLog("[MidiMappingService] Disconnected from AudioDeviceManager");
    }
}

void MidiMappingService::setMidiChannelFilter(int channel)
{
    int clamped = juce::jlimit(0, 16, channel);
    channelFilter_.store(clamped, std::memory_order_relaxed);
    juce::Logger::writeToLog("[MidiMappingService] Channel filter set to: " +
        (clamped == 0 ? juce::String("ALL") : juce::String(clamped)));
}

void MidiMappingService::handleIncomingMidiMessage(juce::MidiInput* source,
                                                    const juce::MidiMessage& message)
{
    if (destroyed_.load(std::memory_order_acquire)) return;

    // Apply channel filter (0 = accept all, 1-16 = specific channel)
    int filter = channelFilter_.load(std::memory_order_relaxed);
    if (filter > 0 && message.getChannel() != filter)
        return;

    if (message.isNoteOn()) {
        handleNoteOn(message.getChannel(),
                     message.getNoteNumber(),
                     message.getFloatVelocity());
    }
    else if (message.isNoteOff()) {
        handleNoteOff(message.getChannel(),
                      message.getNoteNumber());
    }
}

// ────────────────────────────────────────────────────────────────────────────
// AudioSource
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::Logger::writeToLog("[MidiMappingService] === prepareToPlay START === sr="
        + juce::String(sampleRate) + " blockSize=" + juce::String(samplesPerBlockExpected));

    currentSampleRate_ = sampleRate;
    blockSize_         = samplesPerBlockExpected;

    // Silence all voices on reconfigure
    for (auto& v : voices_)
    {
        v.active = false;
        v.stemIdx = -1;
    }

    // ── Allocate realtime signalsmith-stretch instances (NEVER allocate on audio thread) ──
    // We configure every stretcher at the DEVICE sample rate exactly ONCE
    // here, and NEVER reconfigure it on the audio thread. The previous
    // design configured per-stretcher at the input rate (modelSr / voice
    // bufferSr) and called process() with unequal frame counts — that
    // makes signalsmith interpret inputSamples/outputSamples as a time-
    // stretch ratio, coupling pitch shift with an unwanted time stretch
    // (low pitch → slow, high pitch → fast). The fix: pre-resample input
    // from modelSr/bufferSr → deviceSr via linear interp, then call
    // process(in, N, out, N) with EQUAL frame counts so the stretcher
    // only does pitch shifting and never time stretching.
    juce::Logger::writeToLog("[MidiMappingService] prepareToPlay: allocating signalsmith-stretch instances");
    // IMPORTANT: signalsmith::stretch::SignalsmithStretch::reset() and other
    // methods access internal STFT buffers allocated by presetDefault(). An
    // un-presetDefault'd instance has null/empty internals — calling reset()
    // on one CRASHES (observed in stopAllVoices → DynamicSTFT::moveOutput
    // access violation). We presetDefault EVERY instance here at the device
    // rate. No audio-thread reconfigure is ever performed.
    for (int s = 0; s < MAX_STEMS; ++s)
    {
        if (!stemStretch_[(size_t) s])
            stemStretch_[(size_t) s] = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
        // presetCheaper (vs presetDefault): noticeably lower STFT framing
        // latency at slightly reduced quality. Stems now ALL route through
        // signalsmith (no pitch=0 bypass) so latency floor matters more
        // than micro-quality gains — keeping every stem on the same low
        // latency floor is what keeps them locked in sync. Music stems
        // tolerate the cheaper preset well; the alternative (presetDefault)
        // adds ~30-80 ms latency with no audible benefit for stem playback.
        stemStretch_[(size_t) s]->presetCheaper(2, (float) sampleRate, /*splitComputation*/ true);
        stemLastSemi_[(size_t) s] = 0.0f;
        stemLastConfiguredSr_[(size_t) s] = (juce::uint32) sampleRate;
    }

    for (auto& v : voices_)
    {
        if (!v.stretch)
            v.stretch = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
        v.stretch->presetDefault(2, (float) sampleRate, /*splitComputation*/ true);
        v.lastSemi          = 0.0f;
        v.lastConfiguredSr  = (juce::uint32) sampleRate;
        v.bufferSampleRate  = sampleRate;
        v.eosFedSilence     = false;
    }

    // Scratch buffers (planar). signalsmith-stretch's process() takes
    // pointer-of-pointers — same layout as juce::AudioBuffer. We size
    // these big enough to handle any single block + slack for the input
    // count (which can be larger than the output count when shifting up).
    const int maxFrames = juce::jmax(samplesPerBlockExpected, 4096);
    stemOutScratch_.setSize  (2, maxFrames, false, false, true);
    voiceOutScratch_.setSize (2, maxFrames, false, false, true);
    // Input scratch (stem path): holds deviceSr-rate pre-resampled input,
    // numSamples frames. Allocate 4x for safety / future-proofing.
    stretchInScratch_.setSize(2, maxFrames * 4, false, false, true);
    // Input scratch (voice path): same purpose as stretchInScratch_ but kept
    // separate so a voice and a stem can both run in the same block.
    voiceInScratch_.setSize (2, maxFrames, false, false, true);
    stemOutScratch_.clear();
    voiceOutScratch_.clear();
    stretchInScratch_.clear();
    voiceInScratch_.clear();

    juce::Logger::writeToLog("[MidiMappingService] prepareToPlay: signalsmith-stretch instances allocated, "
        "stems=" + juce::String(MAX_STEMS) + " voices=" + juce::String(MAX_VOICES));
    juce::Logger::writeToLog("[MidiMappingService] === prepareToPlay END ===");
}

void MidiMappingService::releaseResources()
{
    juce::Logger::writeToLog("[MidiMappingService] releaseResources");

    for (auto& v : voices_)
    {
        v.active = false;
        v.buffer.reset();
        v.stemIdx = -1;
        v.eosFedSilence = false;
        if (v.stretch) v.stretch->reset();
    }

    for (int s = 0; s < MAX_STEMS; ++s)
    {
        if (stemStretch_[(size_t) s]) stemStretch_[(size_t) s]->reset();
    }
}

void MidiMappingService::applyPitchIfChanged(signalsmith::stretch::SignalsmithStretch<float>& s,
                                             float& lastSemi, float targetSemi)
{
    if (std::abs(lastSemi - targetSemi) > 1.0e-4f)
    {
        s.setTransposeSemitones(targetSemi);
        lastSemi = targetSemi;
    }
}

void MidiMappingService::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Called on AUDIO THREAD.
    // Mix all active voices into the output buffer.

    auto* destL = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto* destR = bufferToFill.buffer->getNumChannels() > 1
                  ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
                  : nullptr;

    const int numSamples = bufferToFill.numSamples;

    for (auto& voice : voices_)
    {
        if (!voice.active || voice.buffer == nullptr)
            continue;

        const auto& srcBuf = *voice.buffer;
        const int srcChannels = srcBuf.getNumChannels();
        const int srcSamples  = srcBuf.getNumSamples();

        const float* srcL = srcBuf.getReadPointer(0);
        const float* srcR = srcChannels > 1 ? srcBuf.getReadPointer(1) : srcL;

        // ── Realtime signalsmith-stretch path: voice has stemIdx >= 0 ──
        // Pitch from atomic stem-pitch. The library handles framing
        // internally — we just feed inN input frames and request outN
        // output frames per block. inN/outN = inputSr/deviceSr keeps tempo
        // at 1.0 in output time-domain while pitch is shifted by
        // setTransposeSemitones.
        //
        // signalsmith-stretch consumes/produces planar (non-interleaved)
        // audio, so we pass pointer-of-pointers directly from juce::AudioBuffer
        // — no interleave step.
        //
        // Position advances by INPUT frames consumed (not output produced).
        // Read current stem pitch up-front so we can bypass the stretcher
        // when it's zero. This must match the diagnostic bypass in the
        // stem-transport path below — same threshold (1e-3 semitone).
        const float voiceTargetSemi =
            (voice.stemIdx >= 0 && voice.stemIdx < MAX_STEMS)
                ? stemPitch_[(size_t) voice.stemIdx]
                      .targetSemi.load(std::memory_order_relaxed)
                : 0.0f;
        const bool voiceUseRealtimeStretch =
            (voice.stemIdx >= 0 && voice.stemIdx < MAX_STEMS
             && voice.stretch != nullptr
             && std::abs(voiceTargetSemi) >= 1.0e-3f);

        // If a stretch instance exists but we're bypassing for pitch=0,
        // reset it so we don't re-emit stale tail when pitch changes next.
        if (voice.stemIdx >= 0 && voice.stemIdx < MAX_STEMS
            && voice.stretch != nullptr && !voiceUseRealtimeStretch)
        {
            voice.stretch->reset();
            voice.lastConfiguredSr = 0;
            voice.lastSemi = 0.0f;
            voice.eosFedSilence = false;
        }

        if (voiceUseRealtimeStretch)
        {
            // The stretcher was configured at deviceSr in prepareToPlay and
            // is NEVER reconfigured on the audio thread. We pre-resample
            // input from voice.bufferSampleRate → deviceSr via linear
            // interpolation, then call process() with EQUAL in/out frame
            // counts so signalsmith only does pitch shift (no time stretch).
            // NOTE: voice.lastConfiguredSr is now DEAD STATE — preserved
            // only for binary-compat with any external readers; never
            // consulted here. Can be removed in a future cleanup.

            // Update pitch from atomic. RT-safe — just sets internal field.
            const float targetSemi = stemPitch_[(size_t) voice.stemIdx]
                                        .targetSemi.load(std::memory_order_relaxed);
            applyPitchIfChanged(*voice.stretch, voice.lastSemi, targetSemi);

            const int endLimit  = (voice.endSample > 0) ? voice.endSample : srcSamples;
            const int blockOut  = numSamples;
            // Linear-interp resample voice.buffer at voice.position
            // (input-sample-space, modelSr) into voiceInScratch_ at
            // deviceSr, producing exactly numSamples output frames.
            const double ratio = (voice.resampleRatio > 0.0)
                                 ? voice.resampleRatio
                                 : 1.0;  // == bufferSr / deviceSr

            const int inCap = voiceInScratch_.getNumSamples();
            const int inFrames = juce::jmin(blockOut, inCap);
            float* inL = voiceInScratch_.getWritePointer(0);
            float* inR = voiceInScratch_.getWritePointer(1);

            bool reachedEnd = false;
            for (int i = 0; i < inFrames; ++i)
            {
                // Wrap on loop boundary BEFORE reading (looping samples
                // wrap to startSample when we reach endLimit).
                double readPos = voice.position;
                if (voice.looping)
                {
                    while (readPos >= (double) endLimit)
                    {
                        const double span = (double) (endLimit - voice.startSample);
                        if (span <= 0.0) break;
                        readPos -= span;
                    }
                    voice.position = readPos;
                }

                const int idx0 = (int) readPos;
                if (idx0 < voice.startSample || idx0 >= endLimit)
                {
                    // EOS — zero-fill the rest of the input window so the
                    // stretcher's internal tail flushes cleanly on the
                    // next block.
                    std::memset(inL + i, 0, sizeof(float) * (size_t) (inFrames - i));
                    std::memset(inR + i, 0, sizeof(float) * (size_t) (inFrames - i));
                    reachedEnd = true;
                    break;
                }
                const int idx1 = juce::jmin(idx0 + 1, endLimit - 1);
                const float frac = (float) (readPos - (double) idx0);

                inL[i] = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                inR[i] = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);

                voice.position = readPos + ratio;
            }

            // Process: EQUAL in/out frame counts → pure pitch shift, no
            // time stretch. THIS IS THE FIX.
            float* outBuf[2] = {
                voiceOutScratch_.getWritePointer(0),
                voiceOutScratch_.getWritePointer(1)
            };
            float* inBuf[2] = { inL, inR };
            voice.stretch->process(inBuf, inFrames, outBuf, blockOut);

            // Mix into dest at velocity. signalsmith-stretch does NOT add
            // overlap-add gain, so no compensation needed (full unity).
            const float gain = voice.velocity;
            for (int i = 0; i < blockOut; ++i)
            {
                destL[i] += outBuf[0][i] * gain;
                if (destR != nullptr)
                    destR[i] += outBuf[1][i] * gain;
            }

            if (reachedEnd && !voice.looping)
            {
                if (voice.eosFedSilence)
                {
                    // Tail flushed in the previous block — done.
                    voice.active = false;
                }
                else
                {
                    voice.eosFedSilence = true;  // one more block of zeros next time
                }
            }

            continue; // next voice
        }

        // ── Legacy direct-playback path (no realtime pitch) ──
        for (int i = 0; i < numSamples; ++i)
        {
            int pos = static_cast<int>(voice.position);
            if (pos >= srcSamples)
            {
                if (voice.looping)
                {
                    voice.position = 0.0;
                    pos = 0;
                    static std::atomic<int> wrapCount{0};
                    if (wrapCount.fetch_add(1) < 3)
                        juce::Logger::writeToLog("[MidiMapping] LOOP WRAP at sample " + juce::String(srcSamples));
                }
                else
                {
                    voice.active = false;
                    break;
                }
            }

            const int  pos1  = juce::jmin(pos + 1, srcSamples - 1);
            const float frac = static_cast<float>(voice.position - pos);

            const float sampleL = srcL[pos] + frac * (srcL[pos1] - srcL[pos]);
            const float sampleR = srcR[pos] + frac * (srcR[pos1] - srcR[pos]);

            const float fadeGain = voice.looping
                ? 1.0f
                : samplar::computeVoiceFadeGain(voice.position, srcSamples, currentSampleRate_);

            destL[i] += sampleL * voice.velocity * fadeGain;
            if (destR != nullptr)
                destR[i] += sampleR * voice.velocity * fadeGain;

            voice.position += voice.resampleRatio;
        }
    }

    // ── S-Mode transport: mix all stems from current position ──────────────
    if (transportActive_.load(std::memory_order_relaxed) && smodeModel_ != nullptr)
    {
        double pos = transportPosSamples_.load(std::memory_order_relaxed);
        const double modelSr = smodeModel_->getSampleRate();
        if (modelSr <= 0.0) return;

        const double ratio = modelSr / currentSampleRate_;
        const int stemCount = smodeModel_->getStemCount();
        int maxStemSamples = 0;

        for (int si = 0; si < stemCount && si < MAX_STEMS; ++si)
        {
            const auto* stem = smodeModel_->getStem(si);
            if (!stem || !smodeModel_->isStemAudible(si) || stem->audioBuffer.getNumSamples() == 0)
                continue;

            const auto& buf = stem->audioBuffer;
            const int totalSmp = buf.getNumSamples();
            if (totalSmp > maxStemSamples) maxStemSamples = totalSmp;

            const float* sL = buf.getReadPointer(0);
            const float* sR = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : sL;
            const float vol = stem->volume;

            // ── Realtime signalsmith-stretch (per-stem) — ALL pitches ──
            // Previously a pitch=0 fast-path bypassed signalsmith and did a
            // raw linear-interp resample. That caused a LATENCY MISMATCH
            // across stems: unpitched stems played immediately, pitched
            // stems lagged by ~30-80 ms (signalsmith's STFT framing
            // latency), so any stem at pitch≠0 audibly drifted out of
            // sync with the others. Fix: route EVERY stem through
            // signalsmith. At semis=0 the stretcher is transparent
            // (modulo framing latency), so all stems share the same
            // latency floor and stay locked together.
            //
            // The voice realtime path keeps its pitch=0 bypass on purpose
            // (one-shot drum hits can't afford a 30-80 ms attack delay).
            // The stretcher was configured at DEVICE rate in prepareToPlay
            // and is NEVER reconfigured on the audio thread. We pre-resample
            // input from modelSr → deviceSr via linear interpolation into
            // stretchInScratch_, then call process() with EQUAL in/out
            // frame counts so signalsmith only does pitch shifting (no
            // time stretching). This fixes the prior bug where unequal
            // frame counts caused signalsmith to interpret inputSamples/
            // outputSamples as a time-stretch ratio, coupling pitch shift
            // with an unwanted tempo change (low pitch → slow, high → fast).
            //
            // NOTE: stemLastConfiguredSr_ is now DEAD STATE — never read.
            // Preserved here only to avoid an ABI/header change in this
            // patch; can be removed in a future cleanup.
            auto* sPtr = stemStretch_[(size_t) si].get();
            if (sPtr == nullptr) continue;
            auto& s = *sPtr;

            const float targetSemi = stemPitch_[(size_t) si]
                                        .targetSemi.load(std::memory_order_relaxed);
            applyPitchIfChanged(s, stemLastSemi_[(size_t) si], targetSemi);

            const int blockOut = numSamples;

            // Pre-resample modelSr → deviceSr via linear interpolation
            // into stretchInScratch_. Stems are processed sequentially in
            // this loop, so reusing this scratch across stems is safe.
            const int inCap = stretchInScratch_.getNumSamples();
            const int inFrames = juce::jmin(blockOut, inCap);
            float* tmpL = stretchInScratch_.getWritePointer(0);
            float* tmpR = stretchInScratch_.getWritePointer(1);
            for (int i = 0; i < inFrames; ++i)
            {
                const double srcPos = pos + (double) i * ratio;
                const int idx0 = (int) srcPos;
                if (idx0 < 0 || idx0 >= totalSmp)
                {
                    tmpL[i] = 0.0f;
                    tmpR[i] = 0.0f;
                    continue;
                }
                const int idx1 = juce::jmin(idx0 + 1, totalSmp - 1);
                const float frac = (float) (srcPos - (double) idx0);
                tmpL[i] = sL[idx0] + frac * (sL[idx1] - sL[idx0]);
                tmpR[i] = sR[idx0] + frac * (sR[idx1] - sR[idx0]);
            }

            float* outBuf[2] = {
                stemOutScratch_.getWritePointer(0),
                stemOutScratch_.getWritePointer(1)
            };
            const float* inBuf[2] = { tmpL, tmpR };
            // EQUAL in/out counts — pure pitch shift, no time stretch.
            s.process(inBuf, inFrames, outBuf, blockOut);

            // Mix into dest at stem volume. Output is already at deviceSr.
            for (int i = 0; i < blockOut; ++i)
            {
                destL[i] += outBuf[0][i] * vol;
                if (destR != nullptr)
                    destR[i] += outBuf[1][i] * vol;
            }
            // Note: transport `pos` advances once after the stem loop using
            // `pos + numSamples * ratio` (in input-sample-space at modelSr).
        }

        double newPos = pos + numSamples * ratio;
        if (newPos >= maxStemSamples)
        {
            if (transportLooping_.load(std::memory_order_relaxed))
            {
                // Wrap to beginning for seamless loop
                newPos = std::fmod(newPos, static_cast<double>(maxStemSamples));
                transportPosSamples_.store(newPos, std::memory_order_relaxed);
                // Reset stem stretchers so end-of-track tail doesn't bleed
                // into the loop start.
                for (int s = 0; s < MAX_STEMS; ++s)
                    if (stemStretch_[(size_t) s]) stemStretch_[(size_t) s]->reset();
            }
            else
            {
                transportActive_.store(false, std::memory_order_relaxed);
                for (int s = 0; s < MAX_STEMS; ++s)
                    if (stemStretch_[(size_t) s]) stemStretch_[(size_t) s]->reset();
                juce::MessageManager::callAsync([this]()
                {
                    if (destroyed_.load(std::memory_order_seq_cst)) return;
                    bus_.publish("smode.transport.stopped", juce::var());
                });
            }
        }
        else
        {
            transportPosSamples_.store(newPos, std::memory_order_relaxed);
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// MIDI Handling — AUDIO THREAD
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::handleNoteOn(int /*channel*/, int noteNumber, float velocity)
{
    if (noteNumber < 0 || noteNumber > 127)
        return;

    // Handle MIDI learn: if active, lock the file to the pressed note
    if (midiLearnActive_.load(std::memory_order_relaxed))
    {
        juce::MessageManager::callAsync([this, noteNumber]()
        {
            if (destroyed_.load(std::memory_order_seq_cst))
                return;

            // S-Mode learn: just publish the note number for the overlay to handle
            if (midiLearnSMode_.load(std::memory_order_relaxed))
            {
                juce::Logger::writeToLog("[MidiMappingService] S-Mode MIDI learn: note " + juce::String(noteNumber)
                    + " (" + noteNumberToName(noteNumber) + ")");
                auto* payload = new juce::DynamicObject();
                payload->setProperty("noteNumber", noteNumber);
                payload->setProperty("noteName", noteNumberToName(noteNumber));
                bus_.publish("smode.midi.learn.assigned", juce::var(payload));
                endMidiLearn();
                return;
            }

            if (midiLearnFile_.existsAsFile())
            {
                lockSampleToNote(noteNumber, midiLearnFile_);
                juce::Logger::writeToLog("[MidiMappingService] MIDI learn locked: "
                    + midiLearnFile_.getFileName() + " -> note " + juce::String(noteNumber)
                    + " (" + noteNumberToName(noteNumber) + ")");

                auto* payload = new juce::DynamicObject();
                payload->setProperty("noteNumber", noteNumber);
                payload->setProperty("noteName", noteNumberToName(noteNumber));
                payload->setProperty("file", midiLearnFile_.getFullPathName());
                bus_.publish("midi.learn.locked", juce::var(payload));
            }
            endMidiLearn();
        });
        return;  // Consume the note — assignment only, no playback during learn
    }

    // ── S-Mode segment triggering ──────────────────────────────────────────
    // Check if this note is assigned to S-Mode stem segments.
    // When LINK ALL assigns the same note to all stems, trigger ALL of them.
    if (smodeModel_ != nullptr)
    {
        auto matches = smodeModel_->findAllSegmentsByMidiNote(noteNumber);
        if (!matches.empty())
        {
            double modelSr = smodeModel_->getSampleRate();
            if (modelSr <= 0.0) modelSr = 44100.0;

            // Monophonic: kill all voices before triggering the group
            if (monophonic_.load(std::memory_order_relaxed))
                for (auto& v : voices_) v.active = false;

            for (const auto& [stemIdx, segIdx] : matches)
            {
                const auto* stem = smodeModel_->getStem(stemIdx);
                const auto* seg  = smodeModel_->getSegment(stemIdx, segIdx);
                if (!stem || !seg || seg->getLengthSamples() <= 0
                    || stem->audioBuffer.getNumSamples() == 0 || !smodeModel_->isStemAudible(stemIdx))
                    continue;

                // Realtime pitch path: copy segment into voice buffer (so the
                // SoundTouch instance has a stable shared_ptr to read from)
                // AND tag voice.stemIdx so the voice loop applies pitch.
                int64_t startSmp = juce::jlimit((int64_t)0, (int64_t)stem->audioBuffer.getNumSamples(),
                                                 (int64_t) seg->startSample);
                int64_t endSmp   = juce::jlimit(startSmp, (int64_t)stem->audioBuffer.getNumSamples(),
                                                 (int64_t) seg->endSample);
                int segLen = static_cast<int>(endSmp - startSmp);
                if (segLen <= 0) continue;

                auto segBuf = std::make_shared<juce::AudioBuffer<float>>(
                    stem->audioBuffer.getNumChannels(), segLen);
                for (int ch = 0; ch < stem->audioBuffer.getNumChannels(); ++ch)
                    segBuf->copyFrom(ch, 0, stem->audioBuffer, ch, static_cast<int>(startSmp), segLen);

                Voice& v        = allocateVoice();
                v.buffer        = segBuf;
                v.position      = 0.0;
                v.startSample   = 0;
                v.endSample     = segLen;
                v.resampleRatio = modelSr / currentSampleRate_;
                v.velocity      = velocity;
                v.noteNumber    = noteNumber;
                v.looping       = seg->effects.loop;
                v.stemIdx       = stemIdx;          // engage realtime pitch
                v.lastSemi      = 0.0f;
                v.eosFedSilence = false;
                // Store the input rate so the audio thread can configure the
                // signalsmith-stretch instance at the correct rate.
                // lastConfiguredSr=0 forces (re)config on first audio block.
                v.bufferSampleRate = modelSr;
                v.lastConfiguredSr = 0;
                if (v.stretch)
                {
                    v.stretch->reset();
                    // Stretcher stays at deviceSr (configured in
                    // prepareToPlay). The audio thread pre-resamples
                    // modelSr → deviceSr before process(). Just clear
                    // lastSemi so pitch is reapplied on the first block.
                    v.lastSemi = -999.0f;
                }
                v.active        = true;

                // Notify UI for each stem's animation
                const double durSec = static_cast<double>(segLen) / modelSr;
                juce::MessageManager::callAsync([this, stemIdx, segIdx, noteNumber, durSec]()
                {
                    if (destroyed_.load(std::memory_order_seq_cst)) return;
                    juce::Logger::writeToLog("[MidiMapping] S-Mode trigger: stem="
                        + juce::String(stemIdx) + " seg=" + juce::String(segIdx)
                        + " note=" + juce::String(noteNumber));
                    auto* payload = new juce::DynamicObject();
                    payload->setProperty("stemIndex", stemIdx);
                    payload->setProperty("segmentIndex", segIdx);
                    payload->setProperty("noteNumber", noteNumber);
                    payload->setProperty("duration", durSec);
                    bus_.publish("smode.segment.triggered", juce::var(payload));
                });
            }

            return;  // Handled by S-Mode — skip browser map
        }
    }

    // ── Browser page sample triggering ──────────────────────────────────────
    // Look up the mapped buffer (try_lock — audio thread must never block)
    std::shared_ptr<const juce::AudioBuffer<float>> buf;
    double slotSampleRate = 44100.0;

    if (mapMutex_.try_lock())
    {
        const auto& slot = midiMap_[noteNumber];
        if (slot.valid)
        {
            buf            = slot.buffer;
            slotSampleRate = slot.sampleRate;
        }
        mapMutex_.unlock();
    }

    if (buf == nullptr)
        return;

    // Monophonic mode: kill all active voices before starting a new one
    if (monophonic_.load(std::memory_order_relaxed))
    {
        for (auto& voice : voices_)
            voice.active = false;
    }

    // Allocate a voice and start playback
    Voice& v        = allocateVoice();
    v.buffer        = buf;
    v.position      = 0.0;
    v.resampleRatio = slotSampleRate / currentSampleRate_;
    v.velocity      = velocity;
    v.noteNumber    = noteNumber;
    v.looping       = loopGateMode_.load(std::memory_order_relaxed);  // Loop while held in loop-gate mode
    v.active        = true;

    // Notify UI of MIDI pad trigger (for tile play animation)
    // Send file path so UI can find the correct tile (handles locked notes too)
    {
        const int padIndex = noteNumber - BROWSER_MAP_START_NOTE;
        const double durationSec = (buf->getNumSamples() > 0 && slotSampleRate > 0)
            ? static_cast<double>(buf->getNumSamples()) / slotSampleRate
            : 1.0;

        // Grab the file path while we still have context
        juce::String filePath;
        if (mapMutex_.try_lock())
        {
            filePath = midiMap_[noteNumber].file.getFullPathName();
            mapMutex_.unlock();
        }

        const bool isMono = monophonic_.load(std::memory_order_relaxed);
        int activeCount = 0;
        for (const auto& voice : voices_)
            if (voice.active) activeCount++;

        juce::MessageManager::callAsync([this, padIndex, durationSec, filePath, noteNumber, isMono, activeCount]()
        {
            if (destroyed_.load(std::memory_order_seq_cst))
                return;
            juce::Logger::writeToLog("[MidiMapping] noteOn=" + juce::String(noteNumber)
                + " mono=" + juce::String(isMono ? 1 : 0)
                + " activeVoices=" + juce::String(activeCount)
                + " file=" + filePath);
            auto* payload = new juce::DynamicObject();
            payload->setProperty("padIndex", padIndex);
            payload->setProperty("duration", durationSec);
            payload->setProperty("filePath", filePath);
            bus_.publish("midi.pad.triggered", juce::var(payload));
        });
    }
}

void MidiMappingService::handleNoteOff(int /*channel*/, int noteNumber)
{
    // In gate mode, note-off immediately stops voices for this note.
    bool sampleGate = sampleGateMode_.load(std::memory_order_relaxed);
    bool loopGate   = loopGateMode_.load(std::memory_order_relaxed);

    if (!sampleGate && !loopGate)
        return;  // Both one-shot — play to completion

    bool killed = false;
    for (auto& voice : voices_)
    {
        if (voice.active && voice.noteNumber == noteNumber)
        {
            if ((voice.looping && loopGate) || (!voice.looping && sampleGate))
            {
                voice.active = false;
                killed = true;
            }
        }
    }

    // Notify UI to stop animation for this note
    if (killed)
    {
        // Check if this note belongs to S-Mode segments (may be on multiple stems)
        if (smodeModel_ != nullptr)
        {
            auto matches = smodeModel_->findAllSegmentsByMidiNote(noteNumber);
            if (!matches.empty())
            {
                for (const auto& [stemIdx, segIdx] : matches)
                {
                    juce::MessageManager::callAsync([this, stemIdx, segIdx, noteNumber]()
                    {
                        if (destroyed_.load(std::memory_order_seq_cst)) return;
                        auto* payload = new juce::DynamicObject();
                        payload->setProperty("stemIndex", stemIdx);
                        payload->setProperty("segmentIndex", segIdx);
                        payload->setProperty("noteNumber", noteNumber);
                        bus_.publish("smode.segment.stopped", juce::var(payload));
                    });
                }
                return;
            }
        }

        // Browser tile note-off
        juce::String filePath;
        if (mapMutex_.try_lock())
        {
            if (noteNumber >= 0 && noteNumber < 128)
                filePath = midiMap_[noteNumber].file.getFullPathName();
            mapMutex_.unlock();
        }

        const int padIndex = noteNumber - BROWSER_MAP_START_NOTE;
        juce::MessageManager::callAsync([this, padIndex, filePath]()
        {
            if (destroyed_.load(std::memory_order_seq_cst))
                return;
            auto* payload = new juce::DynamicObject();
            payload->setProperty("padIndex", padIndex);
            payload->setProperty("filePath", filePath);
            bus_.publish("midi.pad.stopped", juce::var(payload));
        });
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Mapping control — MESSAGE/UI THREAD
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::mapSampleToNote(int noteNumber, const juce::File& sampleFile)
{
    if (noteNumber < 0 || noteNumber > 127)
        return;

    juce::Logger::writeToLog("[MidiMappingService] Map note " + juce::String(noteNumber)
        + " → " + sampleFile.getFileName());

    std::lock_guard<std::mutex> lock(mapMutex_);
    auto& slot = midiMap_[noteNumber];
    slot.file  = sampleFile;
    slot.valid = sampleFile.existsAsFile();
    resolveBuffer(slot);
}

void MidiMappingService::clearMapping(int noteNumber)
{
    if (noteNumber < 0 || noteNumber > 127)
        return;

    std::lock_guard<std::mutex> lock(mapMutex_);
    midiMap_[noteNumber] = MidiSlot{};
}

void MidiMappingService::clearAllMappings()
{
    juce::Logger::writeToLog("[MidiMappingService] clearAllMappings");
    std::lock_guard<std::mutex> lock(mapMutex_);
    midiMap_.fill(MidiSlot{});
}

void MidiMappingService::autoMapBrowserPage(const std::vector<juce::File>& visibleSamples)
{
    juce::Logger::writeToLog("[MidiMappingService] autoMapBrowserPage: "
        + juce::String(visibleSamples.size()) + " samples");

    std::lock_guard<std::mutex> lock(mapMutex_);

    // Build set of files that are locked to ANY note (avoid auto-map duplicates)
    std::set<juce::String> lockedFiles;
    for (int i = 0; i < 128; ++i)
    {
        if (midiMap_[i].locked && midiMap_[i].file.existsAsFile())
            lockedFiles.insert(midiMap_[i].file.getFullPathName());
    }

    // Clear UNLOCKED slots in the browser pad range (locked slots survive page changes)
    for (int i = 0; i < BROWSER_MAP_NUM_PADS; ++i)
    {
        const int note = BROWSER_MAP_START_NOTE + i;
        if (!midiMap_[note].locked)
            midiMap_[note] = MidiSlot{};
    }

    const int count = juce::jmin(static_cast<int>(visibleSamples.size()),
                                 BROWSER_MAP_NUM_PADS);

    for (int i = 0; i < count; ++i)
    {
        const int note = BROWSER_MAP_START_NOTE + i;
        if (midiMap_[note].locked)
            continue;  // Skip locked slots — user-assigned, don't overwrite

        // Skip samples already locked to a different note (no duplicate mapping)
        if (lockedFiles.count(visibleSamples[i].getFullPathName()) > 0)
            continue;

        auto& slot     = midiMap_[note];
        slot.file      = visibleSamples[i];
        slot.valid     = visibleSamples[i].existsAsFile();
        resolveBuffer(slot);
    }
}

void MidiMappingService::mapSequencerChannels(int numChannels)
{
    juce::Logger::writeToLog("[MidiMappingService] mapSequencerChannels: "
        + juce::String(numChannels) + " rows");
    // This sets up WHICH notes trigger which sequencer rows.
    // The actual sample loading is handled by setPattern() in SequencerAudioSource.
    // Here we just ensure the GM map is documented.
    // Row 0 = note 36 (kick), Row 1 = note 37, etc.
    // The mapping is implicit — handleNoteOn triggers autoMapBrowserPage-style slots.
}

void MidiMappingService::setSampleCache(SamplePreviewCache* cache)
{
    // Unregister from old cache
    if (cache_ != nullptr)
        cache_->removeListener(this);

    cache_ = cache;

    // Register as listener so we can resolve buffers when preload completes
    if (cache_ != nullptr)
        cache_->addListener(this);

    juce::Logger::writeToLog("[MidiMappingService] SamplePreviewCache set: "
        + juce::String(cache_ != nullptr ? "yes" : "null"));
}

void MidiMappingService::sampleLoaded(const juce::File& file)
{
    // A sample finished loading into cache — check if any MIDI slot needs it
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (int i = 0; i < 128; ++i)
    {
        auto& slot = midiMap_[i];
        if (slot.valid && slot.buffer == nullptr && slot.file == file)
        {
            resolveBuffer(slot);
            if (slot.buffer != nullptr)
                juce::Logger::writeToLog("[MidiMappingService] Late-resolved buffer for note "
                    + juce::String(i) + ": " + file.getFileName());
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// MIDI Learn
// ────────────────────────────────────────────────────────────────────────────

// ────────────────────────────────────────────────────────────────────────────
// Locked Assignments
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::lockSampleToNote(int noteNumber, const juce::File& sampleFile)
{
    if (noteNumber < 0 || noteNumber > 127)
        return;

    juce::Logger::writeToLog("[MidiMappingService] LOCK note " + juce::String(noteNumber)
        + " (" + noteNumberToName(noteNumber) + ") -> " + sampleFile.getFileName());

    std::lock_guard<std::mutex> lock(mapMutex_);

    // If this file was locked to a different note, unlock the old one first
    for (int i = 0; i < 128; ++i)
    {
        if (midiMap_[i].locked && midiMap_[i].file == sampleFile && i != noteNumber)
        {
            juce::Logger::writeToLog("[MidiMappingService]   Unlocking previous note " + juce::String(i));
            midiMap_[i] = MidiSlot{};
        }
    }

    auto& slot = midiMap_[noteNumber];
    slot.file   = sampleFile;
    slot.valid  = sampleFile.existsAsFile();
    slot.locked = true;
    resolveBuffer(slot);
}

void MidiMappingService::unlockNote(int noteNumber)
{
    if (noteNumber < 0 || noteNumber > 127)
        return;

    juce::Logger::writeToLog("[MidiMappingService] UNLOCK note " + juce::String(noteNumber));

    std::lock_guard<std::mutex> lock(mapMutex_);
    midiMap_[noteNumber] = MidiSlot{};
}

bool MidiMappingService::isNoteLocked(int noteNumber) const
{
    if (noteNumber < 0 || noteNumber > 127)
        return false;

    std::lock_guard<std::mutex> lock(mapMutex_);
    return midiMap_[noteNumber].locked;
}

int MidiMappingService::getLockedNoteForFile(const juce::File& file) const
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (int i = 0; i < 128; ++i)
    {
        if (midiMap_[i].locked && midiMap_[i].file == file)
            return i;
    }
    return -1;
}

int MidiMappingService::noteForSample(const juce::File& file) const
{
    if (file == juce::File()) return -1;
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (int i = 0; i < 128; ++i)
    {
        const auto& slot = midiMap_[i];
        if (slot.valid && slot.file == file)
            return i;
    }
    return -1;
}

int MidiMappingService::registerSampleAtNextFreeNote(const juce::File& file)
{
    if (file == juce::File()) return -1;

    std::lock_guard<std::mutex> lock(mapMutex_);

    // Idempotent: if already mapped anywhere, return existing note.
    for (int i = 0; i < 128; ++i)
    {
        const auto& slot = midiMap_[i];
        if (slot.valid && slot.file == file)
            return i;
    }

    auto tryRange = [&](int lo, int hi) -> int {
        for (int i = lo; i <= hi; ++i)
        {
            auto& slot = midiMap_[i];
            if (!slot.valid && !slot.locked)
            {
                slot.file   = file;
                slot.valid  = file.existsAsFile();
                slot.locked = false;
                resolveBuffer(slot);
                juce::Logger::writeToLog("[MidiMappingService] Auto-registered "
                    + file.getFileName() + " at note " + juce::String(i)
                    + " (" + noteNumberToName(i) + ")");
                return i;
            }
        }
        return -1;
    };

    // Prefer 68..127 so we do NOT clobber the Browser auto-map range (48-67).
    int chosen = tryRange(68, 127);
    if (chosen >= 0) return chosen;

    // Fallback: 0..47 (below the Browser range)
    chosen = tryRange(0, 47);
    if (chosen >= 0) return chosen;

    juce::Logger::writeToLog("[MidiMappingService] registerSampleAtNextFreeNote: all slots full");
    return -1;
}

juce::String MidiMappingService::noteNumberToName(int noteNumber)
{
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = (noteNumber / 12) - 2;  // MIDI 60 = C3
    const int note   = noteNumber % 12;
    return juce::String(noteNames[note]) + juce::String(octave);
}

// ────────────────────────────────────────────────────────────────────────────
// MIDI Learn
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::beginMidiLearn(int tileIndex)
{
    juce::Logger::writeToLog("[MidiMappingService] beginMidiLearn tile=" + juce::String(tileIndex));
    midiLearnTileIndex_.store(tileIndex, std::memory_order_relaxed);
    midiLearnFile_ = juce::File();
    midiLearnActive_.store(true, std::memory_order_relaxed);
}

void MidiMappingService::beginMidiLearnForFile(const juce::File& file)
{
    juce::Logger::writeToLog("[MidiMappingService] beginMidiLearnForFile: " + file.getFileName());
    midiLearnFile_ = file;
    midiLearnTileIndex_.store(-1, std::memory_order_relaxed);
    midiLearnActive_.store(true, std::memory_order_relaxed);
    bus_.publish("midi.learn.waiting", juce::var(file.getFileName()));
}

void MidiMappingService::beginMidiLearnForSMode()
{
    juce::Logger::writeToLog("[MidiMappingService] beginMidiLearnForSMode");
    midiLearnFile_ = juce::File();
    midiLearnTileIndex_.store(-1, std::memory_order_relaxed);
    midiLearnSMode_.store(true, std::memory_order_relaxed);
    midiLearnActive_.store(true, std::memory_order_relaxed);
    bus_.publish("smode.midi.learn.waiting", juce::var());
}

void MidiMappingService::endMidiLearn()
{
    juce::Logger::writeToLog("[MidiMappingService] endMidiLearn");
    midiLearnActive_.store(false, std::memory_order_relaxed);
    midiLearnTileIndex_.store(-1, std::memory_order_relaxed);
    midiLearnSMode_.store(false, std::memory_order_relaxed);
    midiLearnFile_ = juce::File();
}

// ────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::resolveBuffer(MidiSlot& slot)
{
    // Called while mapMutex_ is held — must NOT lock mapMutex_ again.
    if (!slot.valid || cache_ == nullptr)
    {
        slot.buffer.reset();
        return;
    }

    auto ref = cache_->getSharedBuffer(slot.file);
    if (ref.buffer != nullptr)
    {
        slot.buffer     = ref.buffer;
        slot.sampleRate = ref.sampleRate;
        juce::Logger::writeToLog("[MidiMappingService]   Buffer resolved from cache: "
            + slot.file.getFileName());
    }
    else
    {
        // Not in cache yet — request preload AND do synchronous fallback load
        juce::Array<juce::File> arr;
        arr.add(slot.file);
        cache_->requestPreload(arr, /*priority=*/true);
        juce::Logger::writeToLog("[MidiMappingService]   Requested preload: "
            + slot.file.getFileName());

        // Synchronous fallback: load file directly so it's playable immediately
        juce::AudioFormatManager fmgr;
        fmgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmgr.createReaderFor(slot.file));
        if (reader != nullptr)
        {
            auto buf = std::make_shared<juce::AudioBuffer<float>>(
                static_cast<int>(reader->numChannels),
                static_cast<int>(reader->lengthInSamples));
            reader->read(buf.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
            slot.buffer     = buf;
            slot.sampleRate = reader->sampleRate;
            juce::Logger::writeToLog("[MidiMappingService]   Fallback sync load OK: "
                + slot.file.getFileName());
        }
        else
        {
            slot.buffer.reset();
            juce::Logger::writeToLog("[MidiMappingService]   Fallback sync load FAILED: "
                + slot.file.getFileName());
        }
    }
}

MidiMappingService::Voice& MidiMappingService::allocateVoice()
{
    // Find a free slot
    for (auto& v : voices_)
        if (!v.active)
            return v;

    // All voices busy — steal the one furthest into playback (closest to end)
    Voice* oldest = &voices_[0];
    double maxPos = 0.0;
    for (auto& v : voices_)
    {
        if (v.buffer && v.position > maxPos)
        {
            maxPos  = v.position;
            oldest  = &v;
        }
    }
    oldest->active = false;
    return *oldest;
}

// ────────────────────────────────────────────────────────────────────────────
// S-Mode Direct Playback
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::playBufferDirect(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                                           double bufferSampleRate, float velocity, bool looping,
                                           int stemIdx)
{
    if (!buffer || buffer->getNumSamples() == 0) return;

    Voice& v        = allocateVoice();
    v.buffer        = buffer;
    v.position      = 0.0;
    v.startSample   = 0;
    v.endSample     = buffer->getNumSamples();
    v.resampleRatio = bufferSampleRate / currentSampleRate_;
    v.velocity      = velocity;
    v.noteNumber    = -1;  // No MIDI note association
    v.looping       = looping;
    v.stemIdx       = (stemIdx >= 0 && stemIdx < MAX_STEMS) ? stemIdx : -1;
    v.lastSemi      = 0.0f;
    v.eosFedSilence = false;
    // Store the input buffer's native rate so the audio thread configures
    // signalsmith-stretch at the correct rate on first audio block.
    v.bufferSampleRate = bufferSampleRate;
    v.lastConfiguredSr = 0;
    if (v.stretch && v.stemIdx >= 0)
    {
        v.stretch->reset();
        v.lastSemi = -999.0f;  // force pitch (re)apply on first audio block
    }
    v.active        = true;

    juce::Logger::writeToLog("[MidiMapping] playBufferDirect: samples="
        + juce::String(buffer->getNumSamples())
        + " looping=" + juce::String(looping ? "YES" : "NO")
        + " stem=" + juce::String(v.stemIdx));
}

void MidiMappingService::stopAllVoices()
{
    for (auto& v : voices_)
    {
        v.active = false;
        if (v.stretch) v.stretch->reset();
        v.eosFedSilence = false;
    }
    juce::Logger::writeToLog("[MidiMappingService] stopAllVoices: killed all "
        + juce::String(MAX_VOICES) + " voices");
}

// ── Computer Keyboard MIDI ──────────────────────────────────────────────────

int MidiMappingService::keyToMidiNote(const juce::KeyPress& key)
{
    // Standard DAW keyboard layout (Ableton/FL Studio convention)
    // Lower row: Z S X D C V G B H N J M  → C3..B3 (MIDI 48..59)
    // Upper row: Q 2 W 3 E R 5 T 6 Y 7 U  → C4..B4 (MIDI 60..71)
    const int code = key.getKeyCode();

    // Lower octave (C3 = MIDI 48)
    switch (code)
    {
        case 'Z': return 48;  // C3
        case 'S': return 49;  // C#3
        case 'X': return 50;  // D3
        case 'D': return 51;  // D#3
        case 'C': return 52;  // E3
        case 'V': return 53;  // F3
        case 'G': return 54;  // F#3
        case 'B': return 55;  // G3
        case 'H': return 56;  // G#3
        case 'N': return 57;  // A3
        case 'J': return 58;  // A#3
        case 'M': return 59;  // B3

        // Upper octave (C4 = MIDI 60)
        case 'Q': return 60;  // C4
        case '2': return 61;  // C#4
        case 'W': return 62;  // D4
        case '3': return 63;  // D#4
        case 'E': return 64;  // E4
        case 'R': return 65;  // F4
        case '5': return 66;  // F#4
        case 'T': return 67;  // G4
        case '6': return 68;  // G#4
        case 'Y': return 69;  // A4
        case '7': return 70;  // A#4
        case 'U': return 71;  // B4

        default: return -1;
    }
}

bool MidiMappingService::handleKeyPress(const juce::KeyPress& key)
{
    if (!computerKeyboardEnabled_.load(std::memory_order_relaxed))
        return false;

    int note = keyToMidiNote(key);
    if (note < 0) return false;

    // Autorepeat dedup: JUCE's keyPressed() fires on OS auto-repeat while a key
    // is held. Without this guard, each repeat would call handleNoteOn, which
    // machine-guns the sample and bypasses POLY/MONO/1SH/Gate voice logic
    // (that logic assumes one noteOn per physical press). We treat the note as
    // "still held" until handleKeyRelease / releaseUnheldComputerKeys clears it.
    // Even when the key is swallowed, we return true so the Component treats it
    // as handled (prevents fallthrough / beeps).
    const size_t idx = static_cast<size_t>(note);
    bool wasHeld = computerKeyHeld_[idx].exchange(true, std::memory_order_acq_rel);
    if (wasHeld)
    {
        // Silent swallow — no log per repeat (would flood the log at ~30Hz).
        return true;
    }

    juce::Logger::writeToLog("[MidiMappingService] computer-keyboard keyDown note="
        + juce::String(note) + " (dedup'd autorepeat)");
    handleNoteOn(1, note, 0.8f);
    return true;
}

bool MidiMappingService::handleKeyRelease(const juce::KeyPress& key)
{
    if (!computerKeyboardEnabled_.load(std::memory_order_relaxed))
        return false;

    int note = keyToMidiNote(key);
    if (note < 0) return false;

    const size_t idx = static_cast<size_t>(note);
    bool wasHeld = computerKeyHeld_[idx].exchange(false, std::memory_order_acq_rel);
    if (!wasHeld)
        return false;  // nothing to release

    juce::Logger::writeToLog("[MidiMappingService] computer-keyboard keyUp note="
        + juce::String(note));
    handleNoteOff(1, note);
    return true;
}

bool MidiMappingService::releaseUnheldComputerKeys()
{
    if (!computerKeyboardEnabled_.load(std::memory_order_relaxed))
        return false;

    // Walk every QWERTY key that maps to a note; if its physical state is NOT
    // down but we have it flagged as held, fire noteOff. This is the release
    // half of the "one-noteOn-per-press / one-noteOff-per-release" contract
    // and is meant to be driven from a Component::keyStateChanged override.
    static constexpr int allKeys[] = {
        'Z','S','X','D','C','V','G','B','H','N','J','M',
        'Q','2','W','3','E','R','5','T','6','Y','7','U'
    };

    bool anyReleased = false;
    // Build "which notes still have at least one mapped key physically down".
    std::array<bool, 128> physicallyDown{};
    for (int k : allKeys)
    {
        int n = keyToMidiNote(juce::KeyPress(k));
        if (n >= 0 && n < 128 && juce::KeyPress::isKeyCurrentlyDown(k))
            physicallyDown[static_cast<size_t>(n)] = true;
    }

    for (int n = 0; n < 128; ++n)
    {
        if (!computerKeyHeld_[static_cast<size_t>(n)].load(std::memory_order_relaxed))
            continue;
        if (physicallyDown[static_cast<size_t>(n)])
            continue;  // still held, nothing to do

        // Edge: was held, no longer physically down → release.
        computerKeyHeld_[static_cast<size_t>(n)].store(false, std::memory_order_release);
        juce::Logger::writeToLog("[MidiMappingService] computer-keyboard keyUp (polled) note="
            + juce::String(n));
        handleNoteOff(1, n);
        anyReleased = true;
    }

    return anyReleased;
}

void MidiMappingService::resetComputerKeyboardHeld()
{
    for (int n = 0; n < 128; ++n)
    {
        bool wasHeld = computerKeyHeld_[static_cast<size_t>(n)].exchange(false,
                           std::memory_order_acq_rel);
        if (wasHeld)
            handleNoteOff(1, n);
    }
}

void MidiMappingService::setSModeModel(SModeSegmentModel* model)
{
    smodeModel_ = model;

    if (model == nullptr)
    {
        // Hard reset: kill all voices AND release their copied audio buffers.
        // Order matters: set active=false first (audio thread checks active
        // before reading buffer), then null the buffer shared_ptr.
        for (auto& v : voices_)
        {
            v.active = false;
            v.buffer = nullptr;
            v.noteNumber = -1;
        }

        // Stop S-Mode transport
        transportActive_.store(false, std::memory_order_relaxed);
        transportPosSamples_.store(0.0, std::memory_order_relaxed);

        // Reset realtime stem stretchers (drop any in-flight tail)
        for (int s = 0; s < MAX_STEMS; ++s)
        {
            if (stemStretch_[(size_t) s]) stemStretch_[(size_t) s]->reset();
            stemPitch_[(size_t) s].targetSemi.store(0.0f, std::memory_order_relaxed);
            stemLastSemi_[(size_t) s] = 0.0f;
            stemLastConfiguredSr_[(size_t) s] = 0;
        }

        juce::Logger::writeToLog("[MidiMappingService] setSModeModel(nullptr) — hard reset: "
            "voices killed, buffers released, transport stopped, stretchers reset");
    }
    else
    {
        juce::Logger::writeToLog("[MidiMappingService] setSModeModel: wired to model with "
            + juce::String(model->getStemCount()) + " stems");
    }
}

// ── State serialization ──────────────────────────────────────────────────────

std::vector<MidiMappingService::SlotMeta> MidiMappingService::getOccupiedSlotsMeta() const
{
    std::vector<SlotMeta> result;
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (int i = 0; i < 128; ++i)
    {
        const auto& slot = midiMap_[static_cast<size_t>(i)];
        if (slot.valid && slot.file != juce::File{})
        {
            result.push_back({ i, slot.file.getFullPathName(), slot.sampleRate, slot.locked });
        }
    }
    juce::Logger::writeToLog("[MidiMapping] getOccupiedSlotsMeta: " + juce::String(result.size()) + " slots");
    return result;
}

std::vector<MidiMappingService::SlotInfo> MidiMappingService::getOccupiedSlots() const
{
    // First: snapshot the slot data under lock (fast, no I/O)
    struct SlotSnapshot {
        int note;
        juce::File file;
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double sampleRate;
        bool locked;
        bool valid;
    };
    std::vector<SlotSnapshot> snapshots;

    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        int validCount = 0, fileCount = 0, bufCount = 0;
        for (int i = 0; i < 128; ++i)
        {
            const auto& slot = midiMap_[static_cast<size_t>(i)];
            if (slot.valid) ++validCount;
            if (slot.file != juce::File{}) ++fileCount;
            if (slot.buffer != nullptr) ++bufCount;
            if (slot.valid && slot.file != juce::File{})
                snapshots.push_back({ i, slot.file, slot.buffer, slot.sampleRate, slot.locked, slot.valid });
        }
        juce::Logger::writeToLog("[MidiMapping] getOccupiedSlots scan: valid=" + juce::String(validCount)
            + " hasFile=" + juce::String(fileCount) + " hasBuf=" + juce::String(bufCount)
            + " snapshots=" + juce::String(snapshots.size()));
    }

    // Now outside the lock: resolve missing buffers from file
    std::vector<SlotInfo> result;
    for (auto& snap : snapshots)
    {
        auto buf = snap.buffer;
        if (!buf && snap.file.existsAsFile())
        {
            juce::AudioFormatManager fmt;
            fmt.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(snap.file));
            if (reader)
            {
                auto loaded = std::make_shared<juce::AudioBuffer<float>>(
                    static_cast<int>(reader->numChannels),
                    static_cast<int>(reader->lengthInSamples));
                loaded->clear();
                reader->read(loaded.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, reader->numChannels > 1);
                buf = loaded;
                juce::Logger::writeToLog("[MidiMapping] getOccupiedSlots: loaded from file note="
                    + juce::String(snap.note) + " " + snap.file.getFileName());
            }
        }

        if (buf && buf->getNumSamples() > 0)
            result.push_back({ snap.note, snap.file, buf, snap.sampleRate, snap.locked });
    }
    return result;
}

void MidiMappingService::restoreSlot(int noteNumber, const juce::File& file,
                                      std::shared_ptr<juce::AudioBuffer<float>> buffer,
                                      double sampleRate, bool locked)
{
    if (noteNumber < 0 || noteNumber >= 128) return;
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto& slot = midiMap_[static_cast<size_t>(noteNumber)];
    slot.file = file;
    slot.buffer = std::move(buffer);
    slot.sampleRate = sampleRate;
    slot.valid = true;
    slot.locked = locked;
    juce::Logger::writeToLog("[MidiMapping] Restored slot note=" + juce::String(noteNumber)
        + " file=" + file.getFileName() + " locked=" + juce::String(locked ? "YES" : "NO"));
}

// S-Mode Transport
// ────────────────────────────────────────────────────────────────────────────

void MidiMappingService::startSModeTransport(double startTimeSeconds)
{
    if (smodeModel_ == nullptr) return;

    double modelSr = smodeModel_->getSampleRate();
    if (modelSr <= 0.0) modelSr = 44100.0;

    transportPosSamples_.store(startTimeSeconds * modelSr, std::memory_order_relaxed);
    transportActive_.store(true, std::memory_order_relaxed);
    // Reset stem stretchers so the new start position doesn't bleed any
    // previous tail. signalsmith-stretch handles its own framing — no prime
    // step is needed.
    for (int s = 0; s < MAX_STEMS; ++s)
        if (stemStretch_[(size_t) s]) stemStretch_[(size_t) s]->reset();
    juce::Logger::writeToLog("[MidiMapping] Transport START at " + juce::String(startTimeSeconds, 2) + "s");
}

void MidiMappingService::stopSModeTransport()
{
    transportActive_.store(false, std::memory_order_relaxed);
    for (int s = 0; s < MAX_STEMS; ++s)
        if (stemStretch_[(size_t) s]) stemStretch_[(size_t) s]->reset();
    juce::Logger::writeToLog("[MidiMapping] Transport STOP");
}

double MidiMappingService::getSModeTransportPositionSeconds() const
{
    if (smodeModel_ == nullptr) return 0.0;
    double modelSr = smodeModel_->getSampleRate();
    if (modelSr <= 0.0) return 0.0;
    return transportPosSamples_.load(std::memory_order_relaxed) / modelSr;
}

// ────────────────────────────────────────────────────────────────────────────
// Per-stem pitch shifting (REALTIME — applied in audio thread per block)
// ────────────────────────────────────────────────────────────────────────────

float MidiMappingService::getStemPitchSemitones(int stemIdx) const
{
    if (stemIdx < 0 || stemIdx >= MAX_STEMS) return 0.0f;
    return stemPitch_[(size_t) stemIdx].targetSemi.load(std::memory_order_relaxed);
}

void MidiMappingService::setStemPitchSemitones(int stemIdx, float semitones)
{
    if (stemIdx < 0 || stemIdx >= MAX_STEMS)
    {
        juce::Logger::writeToLog("[MidiMappingService] setStemPitchSemitones invalid stemIdx="
            + juce::String(stemIdx));
        return;
    }

    const float clamped = juce::jlimit(-12.0f, 12.0f, semitones);
    auto& slot = stemPitch_[(size_t) stemIdx];

    const float current = slot.targetSemi.load(std::memory_order_relaxed);
    if (std::abs(current - clamped) < 1.0e-4f)
        return;

    // Single atomic store — picked up on the next audio block (~one buffer
    // of latency). No background thread, no debounce, no render kickoff.
    slot.targetSemi.store(clamped, std::memory_order_relaxed);

    juce::Logger::writeToLog("[MidiMappingService] setStemPitchSemitones stem="
        + juce::String(stemIdx) + " semi=" + juce::String(clamped, 2)
        + " (realtime)");

    // Publish event so other UI peers can mirror.
    auto* payload = new juce::DynamicObject();
    payload->setProperty("stemIndex", stemIdx);
    payload->setProperty("semi", clamped);
    bus_.publish("smode.stem.pitch.changed", juce::var(payload));
}

#if 0
// (Removed) Offline pre-render code — replaced by realtime SoundTouch in
// the audio path (stem instances in transport, voice instances on note-on).
void MidiMappingService::renderPitchedStem(int stemIdx, float semi, int generation)
{
#ifdef WITH_SOUNDTOUCH
    juce::Logger::writeToLog("[MidiMappingService] 🔄 renderPitchedStem START stem="
        + juce::String(stemIdx) + " semi=" + juce::String(semi, 2)
        + " gen=" + juce::String(generation));

    if (stemIdx < 0 || stemIdx >= MAX_STEMS) return;
    auto& slot = stemPitch_[(size_t) stemIdx];

    if (slot.rendering.exchange(true, std::memory_order_acq_rel))
    {
        juce::Logger::writeToLog("[MidiMappingService]   render already in flight stem="
            + juce::String(stemIdx) + " — superseding");
    }

    if (destroyed_.load(std::memory_order_acquire) || smodeModel_ == nullptr)
    {
        slot.rendering.store(false, std::memory_order_release);
        juce::Logger::writeToLog("[MidiMappingService] ❌ renderPitchedStem aborted (destroyed or no model)");
        return;
    }

    const auto* stem = smodeModel_->getStem(stemIdx);
    if (stem == nullptr || stem->audioBuffer.getNumSamples() == 0)
    {
        slot.rendering.store(false, std::memory_order_release);
        juce::Logger::writeToLog("[MidiMappingService] ❌ renderPitchedStem stem missing/empty stem="
            + juce::String(stemIdx));
        return;
    }

    // Snapshot source (model is on UI thread; for now we trust it doesn't
    // mutate during render. If this becomes a problem, switch to a copy.)
    const int    srcChannels = stem->audioBuffer.getNumChannels();
    const int    srcSamples  = stem->audioBuffer.getNumSamples();
    const double srcSr       = smodeModel_->getSampleRate() > 0.0
                                 ? smodeModel_->getSampleRate() : 44100.0;

    if (srcChannels <= 0 || srcSamples <= 0)
    {
        slot.rendering.store(false, std::memory_order_release);
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();

    soundtouch::SoundTouch st;
    st.setSampleRate((juce::uint32) srcSr);
    st.setChannels((juce::uint32) srcChannels);
    st.setTempo(1.0f);
    st.setPitchSemiTones(semi);
    st.setSetting(SETTING_USE_QUICKSEEK, 1);
    st.setSetting(SETTING_SEQUENCE_MS, 40);
    st.setSetting(SETTING_SEEKWINDOW_MS, 20);
    st.setSetting(SETTING_OVERLAP_MS, 8);

    // Working interleaved buffers
    constexpr int kChunk = 2048;
    std::vector<float> inInterleaved((size_t) kChunk * (size_t) srcChannels);
    std::vector<float> outInterleaved((size_t) kChunk * (size_t) srcChannels);

    // Destination: tempo=1.0 produces ~same length; over-allocate +2 sec
    // headroom and trim at the end.
    const int destCap = srcSamples + (int) (srcSr * 2.0);
    auto dst = std::make_shared<juce::AudioBuffer<float>>(srcChannels, destCap);
    dst->clear();
    int outWritten = 0;

    // Cache read pointers
    std::vector<const float*> readers((size_t) srcChannels);
    for (int ch = 0; ch < srcChannels; ++ch)
        readers[(size_t) ch] = stem->audioBuffer.getReadPointer(ch);

    int inPos = 0;
    int chunkCounter = 0;
    while (inPos < srcSamples)
    {
        // Supersession check every 8 chunks (~16k samples).
        if ((chunkCounter++ & 0x07) == 0)
        {
            if (slot.generation.load(std::memory_order_acquire) != generation
                || destroyed_.load(std::memory_order_acquire))
            {
                slot.rendering.store(false, std::memory_order_release);
                juce::Logger::writeToLog("[MidiMappingService] ❌ renderPitchedStem stem="
                    + juce::String(stemIdx) + " — newer request superseded, aborting");
                return;
            }
        }

        const int n = juce::jmin(kChunk, srcSamples - inPos);
        // Interleave
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < srcChannels; ++ch)
                inInterleaved[(size_t) i * (size_t) srcChannels + (size_t) ch]
                    = readers[(size_t) ch][inPos + i];

        st.putSamples(inInterleaved.data(), (juce::uint32) n);

        // Drain any output produced
        for (;;)
        {
            const juce::uint32 got = st.receiveSamples(outInterleaved.data(), (juce::uint32) kChunk);
            if (got == 0) break;
            const int gi = (int) got;
            if (outWritten + gi > destCap) break; // safety
            for (int i = 0; i < gi; ++i)
                for (int ch = 0; ch < srcChannels; ++ch)
                    dst->getWritePointer(ch)[outWritten + i]
                        = outInterleaved[(size_t) i * (size_t) srcChannels + (size_t) ch];
            outWritten += gi;
        }

        inPos += n;
    }

    // Flush remaining samples
    st.flush();
    for (;;)
    {
        const juce::uint32 got = st.receiveSamples(outInterleaved.data(), (juce::uint32) kChunk);
        if (got == 0) break;
        const int gi = (int) got;
        if (outWritten + gi > destCap) break;
        for (int i = 0; i < gi; ++i)
            for (int ch = 0; ch < srcChannels; ++ch)
                dst->getWritePointer(ch)[outWritten + i]
                    = outInterleaved[(size_t) i * (size_t) srcChannels + (size_t) ch];
        outWritten += gi;
    }

    // Trim to actual produced length
    if (outWritten < destCap)
    {
        auto trimmed = std::make_shared<juce::AudioBuffer<float>>(srcChannels, juce::jmax(1, outWritten));
        for (int ch = 0; ch < srcChannels; ++ch)
            trimmed->copyFrom(ch, 0, *dst, ch, 0, juce::jmax(1, outWritten));
        dst = trimmed;
    }

    const double ratio = srcSamples > 0
        ? (double) outWritten / (double) srcSamples : 1.0;

    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // Final supersession check before swapping in.
    if (slot.generation.load(std::memory_order_acquire) != generation
        || destroyed_.load(std::memory_order_acquire))
    {
        slot.rendering.store(false, std::memory_order_release);
        juce::Logger::writeToLog("[MidiMappingService] ❌ renderPitchedStem stem="
            + juce::String(stemIdx) + " — superseded post-render, discarding result");
        return;
    }

    // Swap
    {
        juce::SpinLock::ScopedLockType lk(slot.bufferLock);
        slot.pitchedBuffer = dst;
        slot.renderedSemi.store(semi, std::memory_order_release);
        slot.pitchedLengthRatio.store(ratio, std::memory_order_release);
    }
    slot.rendering.store(false, std::memory_order_release);

    juce::Logger::writeToLog("[MidiMappingService] ✅ renderPitchedStem DONE stem="
        + juce::String(stemIdx) + " semi=" + juce::String(semi, 2)
        + " in=" + juce::String(srcSamples) + " out=" + juce::String(outWritten)
        + " ratio=" + juce::String(ratio, 6) + " time=" + juce::String((int) ms) + "ms");

    // Notify UI
    juce::MessageManager::callAsync([this, stemIdx, semi]()
    {
        if (destroyed_.load(std::memory_order_acquire)) return;
        auto* payload = new juce::DynamicObject();
        payload->setProperty("stemIndex", stemIdx);
        payload->setProperty("semi", semi);
        bus_.publish("smode.stem.pitch.rendered", juce::var(payload));
    });
#else
    juce::ignoreUnused(stemIdx, semi, generation);
    juce::Logger::writeToLog("[MidiMappingService] WITH_SOUNDTOUCH not defined — renderPitchedStem skipped");
#endif
}
#endif // 0  end of dead offline render code

} // namespace samplar
