#include "OnnxSeparationProvider.h"

#include <onnxruntime_cxx_api.h>

#include <cmath>
#include <cstring>
#include <algorithm>
#include <random>

// ============================================================================
// OnnxSeparationProvider — mirror of sevagh/demucs.onnx reference inference
// (src/model_apply.cpp + src/demucs.hpp on commit ~main, 2024-11).
//
// Why this rewrite (2026-05-07):
//   The previous code targeted the ModernMube htdemucs ONNX export, which
//   exposes the hybrid two-branch contract  inputs=(mix, spec),
//   outputs=(spec_stems, wave_stems). That export was produced with a broken
//   PyTorch (issue #135343) and outputs garbage stems regardless of how
//   correctly the C++ glue STFT/iSTFT/normalises around it.
//
//   We now use smank/htdemucs-onnx, which exports the FULL pipeline
//   (STFT inside graph -> hybrid network -> iSTFT inside graph) as a single
//   ONNX op chain. Contract is the same as sevagh's apply.py "core model
//   wrapper", reduced to one tensor in / one tensor out:
//
//     input  mix     : [1, 2, samples] f32
//     output sources : [1, 4, 2, samples] f32   (drums, bass, other, vocals)
//
//   With STFT baked in we no longer need our own STFT/iSTFT — we just feed
//   raw waveform chunks at SEGMENT_LEN samples and OLA the waveform output
//   with the same triangular weights sevagh uses.
//
// Pipeline constants — sevagh/demucs.onnx src/demucs.hpp:
//   SUPPORTED_SAMPLE_RATE = 44100
//   SEGMENT_LEN_SECS      = 7.8     -> 343 980 samples per chunk
//   MAX_SHIFT_SECS        = 0.5     -> 22 050 samples
//   OVERLAP               = 0.25    -> stride = (1-0.25)*segment = 257 985
//   TRANSITION_POWER      = 1.0     (triangular weights, not squared)
//
// Reference is sevagh src/model_apply.cpp::demucs_inference(). Differences
// from a generic Demucs Python apply.py:
//   - sevagh uses ref_std with ddof=1 (sample stddev, n-1), NOT n. We match.
//   - sevagh symmetric-zero-pads by MAX_SHIFT on BOTH sides, picks one
//     random shift in [0, MAX_SHIFT), and operates on shifted_audio of
//     length length + max_shift - shift_offset. After OLA he crops back
//     using offset = max_shift - shift_offset, extent = length.
//   - Triangular weights are computed on full segment, normalised by max,
//     then **= TRANSITION_POWER (=1.0 -> noop).
//   - Stride is (1-OVERLAP)*segment but the loop ALWAYS reads
//     min(segment, remaining), allowing the last chunk to be shorter.
//   - Inference is per chunk, output is accumulated as
//       out[seg_off : seg_off + chunk_len] += weight[:chunk_len] * chunk_out
//       sum_weight[seg_off : seg_off + chunk_len] += weight[:chunk_len]
//   - After loop: out /= sum_weight, then out = out*ref_std + ref_mean.
// ============================================================================

namespace samplar {

// ----------------------------------------------------------------------------
// Constants from sevagh/demucs.onnx src/demucs.hpp
// ----------------------------------------------------------------------------
static constexpr int   kSampleRate       = 44100;
static constexpr float kSegmentLenSecs   = 7.8f;
static constexpr float kMaxShiftSecs     = 0.5f;
static constexpr float kOverlap          = 0.25f;
static constexpr float kTransitionPower  = 1.0f;

static constexpr int   kSegmentSamples   = (int) (kSegmentLenSecs * (float) kSampleRate); // 343980
static constexpr int   kMaxShiftSamples  = (int) (kMaxShiftSecs   * (float) kSampleRate); // 22050
static constexpr int   kStrideSamples    = (int) ((1.0f - kOverlap) * (float) kSegmentSamples); // 257985

static constexpr int   kNumStems         = 4;
static constexpr int   kNumChannels      = 2;

// ----------------------------------------------------------------------------
OnnxSeparationProvider::OnnxSeparationProvider (const juce::File& modelFile)
{
    juce::Logger::writeToLog ("[ONNX] === OnnxSeparationProvider ctor START ===");
    juce::Logger::writeToLog ("[ONNX]   model: " + modelFile.getFullPathName());

    if (! modelFile.existsAsFile())
    {
        lastError_ = "ONNX model not found: " + modelFile.getFullPathName();
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        return;
    }

    try
    {
        env_ = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "samplar-onnx");

        Ort::SessionOptions so;
        const int cores = juce::jmax (1, (int) juce::SystemStats::getNumPhysicalCpus());
        so.SetIntraOpNumThreads (cores);
        so.SetInterOpNumThreads (1);
        so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        so.SetExecutionMode (ORT_SEQUENTIAL);

        juce::Logger::writeToLog ("[ONNX]   intra=" + juce::String (cores)
                                  + " inter=1 mode=SEQUENTIAL gopt=ALL");

        activeEP_ = "CPU";

        const auto modelPathW = modelFile.getFullPathName().toWideCharPointer();
        session_ = std::make_unique<Ort::Session> (*env_, modelPathW, so);

        memInfo_ = std::make_unique<Ort::MemoryInfo> (
            Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault));

        const size_t numIn  = session_->GetInputCount();
        const size_t numOut = session_->GetOutputCount();
        juce::Logger::writeToLog ("[ONNX]   inputs=" + juce::String ((int) numIn)
                                  + " outputs=" + juce::String ((int) numOut));

        Ort::AllocatorWithDefaultOptions discoverAlloc;
        for (size_t i = 0; i < numIn; ++i)
        {
            auto nameHolder = session_->GetInputNameAllocated (i, discoverAlloc);
            auto info  = session_->GetInputTypeInfo (i);
            auto tinfo = info.GetTensorTypeAndShapeInfo();
            auto shape = tinfo.GetShape();
            juce::String s = "[";
            for (size_t k = 0; k < shape.size(); ++k)
            {
                if (k > 0) s << ",";
                s << juce::String ((juce::int64) shape[k]);
            }
            s << "]";
            juce::Logger::writeToLog ("[ONNX]   input[" + juce::String ((int) i) + "] name="
                                      + juce::String (nameHolder.get()) + " shape=" + s);
        }
        for (size_t i = 0; i < numOut; ++i)
        {
            auto nameHolder = session_->GetOutputNameAllocated (i, discoverAlloc);
            auto info  = session_->GetOutputTypeInfo (i);
            auto tinfo = info.GetTensorTypeAndShapeInfo();
            auto shape = tinfo.GetShape();
            juce::String s = "[";
            for (size_t k = 0; k < shape.size(); ++k)
            {
                if (k > 0) s << ",";
                s << juce::String ((juce::int64) shape[k]);
            }
            s << "]";
            juce::Logger::writeToLog ("[ONNX]   output[" + juce::String ((int) i) + "] name="
                                      + juce::String (nameHolder.get()) + " shape=" + s);
        }

        ready_ = true;
        juce::Logger::writeToLog ("[ONNX] === ctor SUCCESS (provider=" + activeEP_
                                  + " segment=" + juce::String (kSegmentSamples)
                                  + " stride=" + juce::String (kStrideSamples) + ") ===");
    }
    catch (const std::exception& e)
    {
        lastError_ = juce::String ("ONNX session init failed: ") + e.what();
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        ready_ = false;
    }
}

OnnxSeparationProvider::~OnnxSeparationProvider() = default;

// ----------------------------------------------------------------------------
bool OnnxSeparationProvider::loadAudio441Stereo (const juce::File& input,
                                                 std::vector<float>& outChL,
                                                 std::vector<float>& outChR,
                                                 juce::String& error)
{
    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (input));
    if (reader == nullptr)
    {
        error = "Failed to read input audio (unsupported format?): " + input.getFullPathName();
        return false;
    }

    const auto srcSr   = reader->sampleRate;
    const auto srcLen  = (int) reader->lengthInSamples;
    const auto srcCh   = (int) reader->numChannels;

    if (srcLen <= 0 || srcCh <= 0)
    {
        error = "Input audio is empty";
        return false;
    }

    juce::Logger::writeToLog ("[ONNX] [load] " + input.getFileName()
                              + " sr=" + juce::String (srcSr)
                              + " ch=" + juce::String (srcCh)
                              + " len=" + juce::String (srcLen));

    juce::AudioBuffer<float> srcBuf ((int) juce::jmax (2, srcCh), srcLen);
    srcBuf.clear();
    reader->read (&srcBuf, 0, srcLen, 0, true, srcCh > 1);

    if (srcCh == 1)
        srcBuf.copyFrom (1, 0, srcBuf, 0, 0, srcLen);

    if (std::abs (srcSr - (double) kSampleRate) > 0.5)
    {
        const int newLen = (int) std::round (srcLen * ((double) kSampleRate / srcSr));
        outChL.assign ((size_t) newLen, 0.0f);
        outChR.assign ((size_t) newLen, 0.0f);

        const float* L = srcBuf.getReadPointer (0);
        const float* R = srcBuf.getReadPointer (1);
        const double step = (double) (srcLen - 1) / juce::jmax (1, newLen - 1);

        for (int i = 0; i < newLen; ++i)
        {
            const double srcIdx = i * step;
            const int    idx0   = (int) srcIdx;
            const int    idx1   = juce::jmin (srcLen - 1, idx0 + 1);
            const float  frac   = (float) (srcIdx - idx0);
            outChL[(size_t) i] = L[idx0] + (L[idx1] - L[idx0]) * frac;
            outChR[(size_t) i] = R[idx0] + (R[idx1] - R[idx0]) * frac;
        }

        juce::Logger::writeToLog ("[ONNX] [load] resampled " + juce::String (srcSr)
                                  + " -> 44100 (" + juce::String (srcLen)
                                  + " -> " + juce::String (newLen) + " samples)");
    }
    else
    {
        outChL.assign ((size_t) srcLen, 0.0f);
        outChR.assign ((size_t) srcLen, 0.0f);
        std::memcpy (outChL.data(), srcBuf.getReadPointer (0), sizeof (float) * (size_t) srcLen);
        std::memcpy (outChR.data(), srcBuf.getReadPointer (1), sizeof (float) * (size_t) srcLen);
    }

    return true;
}

// ----------------------------------------------------------------------------
bool OnnxSeparationProvider::writeStemWav (const juce::File& path,
                                           const float* chL,
                                           const float* chR,
                                           int numSamples,
                                           juce::String& error)
{
    path.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream> (path.createOutputStream());
    if (! stream || ! stream->openedOk())
    {
        error = "Cannot open stem file for writing: " + path.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), (double) kSampleRate, 2, 16, {}, 0));

    if (writer == nullptr)
    {
        error = "Cannot create WAV writer for: " + path.getFullPathName();
        return false;
    }
    stream.release();

    juce::AudioBuffer<float> buf (2, numSamples);
    std::memcpy (buf.getWritePointer (0), chL, sizeof (float) * (size_t) numSamples);
    std::memcpy (buf.getWritePointer (1), chR, sizeof (float) * (size_t) numSamples);
    if (! writer->writeFromAudioSampleBuffer (buf, 0, numSamples))
    {
        error = "Failed to write WAV samples to: " + path.getFullPathName();
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
bool OnnxSeparationProvider::separate (const juce::File& inputAudio,
                                       const juce::File& outputDir,
                                       bool drumsOnly,
                                       ProgressFn progress,
                                       std::function<bool()> shouldCancel)
{
    juce::Logger::writeToLog ("[ONNX] === separate START ===");
    juce::Logger::writeToLog ("[ONNX]   input: "  + inputAudio.getFullPathName());
    juce::Logger::writeToLog ("[ONNX]   output: " + outputDir.getFullPathName());
    juce::Logger::writeToLog ("[ONNX]   drumsOnly=" + juce::String ((int) drumsOnly));

    lastStems_ = {};

    if (! ready_ || session_ == nullptr)
    {
        if (lastError_.isEmpty()) lastError_ = "ONNX provider not initialised";
        return false;
    }
    if (! inputAudio.existsAsFile())
    {
        lastError_ = "Input audio not found: " + inputAudio.getFullPathName();
        return false;
    }
    outputDir.createDirectory();

    if (progress) progress (0.02f, "Loading audio");

    std::vector<float> chL, chR;
    if (! loadAudio441Stereo (inputAudio, chL, chR, lastError_))
        return false;

    const int length = (int) chL.size();
    if (length < 2)
    {
        lastError_ = "Audio too short";
        return false;
    }
    const double audioDuration = length / (double) kSampleRate;
    juce::Logger::writeToLog ("[ONNX]   loaded " + juce::String (length)
                              + " samples (" + juce::String (audioDuration, 2) + " s)");

    // ------------------------------------------------------------------------
    // STEP 1 — per-track normalization (mirrors sevagh model_apply.cpp:73-77).
    //   ref_mean_0   = audio.colwise().mean();   // mean per sample across channels
    //   ref_mean     = ref_mean_0.mean();
    //   ref_std      = sqrt( sum((ref_mean_0 - ref_mean)^2) / (N - 1) );   // ddof=1
    //   audio'       = (audio - ref_mean) / ref_std
    // ------------------------------------------------------------------------
    std::vector<double> refMean0 ((size_t) length, 0.0);
    for (int i = 0; i < length; ++i)
        refMean0[(size_t) i] = 0.5 * ((double) chL[i] + (double) chR[i]);

    double sum = 0.0;
    for (int i = 0; i < length; ++i)
        sum += refMean0[(size_t) i];
    const double refMean = sum / (double) length;

    double sqSum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double d = refMean0[(size_t) i] - refMean;
        sqSum += d * d;
    }
    const int    denom  = (length > 1) ? (length - 1) : 1; // ddof=1
    double refStd = std::sqrt (sqSum / (double) denom);
    if (refStd < 1.0e-8) refStd = 1.0e-8;  // avoid div-by-zero on silence

    juce::Logger::writeToLog ("[ONNX]   [norm] refMean=" + juce::String (refMean, 8)
                              + " refStd=" + juce::String (refStd, 8));

    // ------------------------------------------------------------------------
    // STEP 2 — symmetric zero pad by max_shift on both sides
    //          (sevagh model_apply.cpp:79-86).
    // padded_mix = zeros[2, length + 2*max_shift]
    // padded_mix[:, max_shift : max_shift+length] = (audio - refMean) / refStd
    // ------------------------------------------------------------------------
    const int paddedLength = length + 2 * kMaxShiftSamples;
    std::vector<float> padL ((size_t) paddedLength, 0.0f);
    std::vector<float> padR ((size_t) paddedLength, 0.0f);

    const float refMeanF = (float) refMean;
    const float invStdF  = (float) (1.0 / refStd);
    for (int i = 0; i < length; ++i)
    {
        padL[(size_t) (kMaxShiftSamples + i)] = (chL[(size_t) i] - refMeanF) * invStdF;
        padR[(size_t) (kMaxShiftSamples + i)] = (chR[(size_t) i] - refMeanF) * invStdF;
    }

    // ------------------------------------------------------------------------
    // STEP 3 — random shift offset in [0, max_shift) for time-equivariance.
    //          (sevagh model_apply.cpp:88-92).
    // ------------------------------------------------------------------------
    static std::random_device sRd;
    static std::mt19937 sGen (sRd());
    std::uniform_int_distribution<> dist (0, kMaxShiftSamples - 1);
    const int shiftOffset = dist (sGen);

    const int shiftedLength = length + kMaxShiftSamples - shiftOffset;
    juce::Logger::writeToLog ("[ONNX]   shift offset = " + juce::String (shiftOffset)
                              + " shifted_length = " + juce::String (shiftedLength));

    // shifted view starts at padL/padR + shiftOffset, spans shiftedLength samples.

    // ------------------------------------------------------------------------
    // STEP 4 — triangular weight (sevagh model_apply.cpp:107-115).
    //   weight = [1..half, half..1] / max  (peak = half = segment/2)
    //   weight = weight ** TRANSITION_POWER  (=1.0 -> noop)
    // ------------------------------------------------------------------------
    std::vector<float> weight ((size_t) kSegmentSamples, 0.0f);
    {
        const int half = kSegmentSamples / 2;
        const float peak = (float) half;
        for (int i = 0; i < half; ++i)
            weight[(size_t) i] = (float) (i + 1) / peak;
        // mirror reverse of the first half into the tail
        for (int i = 0; i < kSegmentSamples - half; ++i)
            weight[(size_t) (half + i)] = weight[(size_t) (half - 1 - i)];
        if (std::abs (kTransitionPower - 1.0f) > 1.0e-6f)
        {
            for (int i = 0; i < kSegmentSamples; ++i)
                weight[(size_t) i] = std::pow (weight[(size_t) i], kTransitionPower);
        }
    }

    // ------------------------------------------------------------------------
    // STEP 5 — output and sum_weight buffers, length = shiftedLength
    //          (sevagh model_apply.cpp:100-105 + 117-118).
    // out[stem, channel, sample] : double accumulator
    // sum_weight[sample]         : double accumulator
    // ------------------------------------------------------------------------
    std::vector<double> outL[kNumStems];
    std::vector<double> outR[kNumStems];
    for (int s = 0; s < kNumStems; ++s)
    {
        outL[s].assign ((size_t) shiftedLength, 0.0);
        outR[s].assign ((size_t) shiftedLength, 0.0);
    }
    std::vector<double> sumWeight ((size_t) shiftedLength, 0.0);

    // ------------------------------------------------------------------------
    // ORT name discovery (smank export: 1 input "mix", 1 output "sources").
    // ------------------------------------------------------------------------
    Ort::AllocatorWithDefaultOptions alloc;
    std::vector<Ort::AllocatedStringPtr> inputNameHolders;
    std::vector<Ort::AllocatedStringPtr> outputNameHolders;
    std::vector<const char*> inputNames, outputNames;
    const size_t numIn  = session_->GetInputCount();
    const size_t numOut = session_->GetOutputCount();
    if (numIn != 1 || numOut != 1)
    {
        lastError_ = "Unexpected model contract: numIn=" + juce::String ((int) numIn)
                   + " numOut=" + juce::String ((int) numOut)
                   + " (expected 1/1 for smank/htdemucs.onnx)";
        juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
        return false;
    }
    for (size_t i = 0; i < numIn; ++i)
    {
        inputNameHolders.emplace_back (session_->GetInputNameAllocated (i, alloc));
        inputNames.push_back (inputNameHolders.back().get());
        juce::Logger::writeToLog ("[ONNX]   input[" + juce::String ((int) i) + "]="
                                  + juce::String (inputNames.back()));
    }
    for (size_t i = 0; i < numOut; ++i)
    {
        outputNameHolders.emplace_back (session_->GetOutputNameAllocated (i, alloc));
        outputNames.push_back (outputNameHolders.back().get());
        juce::Logger::writeToLog ("[ONNX]   output[" + juce::String ((int) i) + "]="
                                  + juce::String (outputNames.back()));
    }

    // ------------------------------------------------------------------------
    // STEP 6 — segment loop.
    // for segment_offset = 0; segment_offset < shifted_length; segment_offset += stride
    //   chunk_length = min(segment_samples, shifted_length - segment_offset)
    //   chunk = shifted_audio[:, segment_offset : segment_offset + chunk_length]
    //   pad chunk to segment_samples with zeros on the right, run model,
    //   accumulate weight[:chunk_length] * chunk_out into out and into sum_weight.
    // ------------------------------------------------------------------------
    int nChunks = 0;
    for (int off = 0; off < shiftedLength; off += kStrideSamples) ++nChunks;
    juce::Logger::writeToLog ("[ONNX]   nChunks=" + juce::String (nChunks)
                              + " segment=" + juce::String (kSegmentSamples)
                              + " stride=" + juce::String (kStrideSamples));

    std::array<int64_t, 3> mixShape { 1, kNumChannels, kSegmentSamples };
    std::vector<float> mixBuf ((size_t) kNumChannels * kSegmentSamples, 0.0f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    int chunkIdx = 0;
    for (int segOff = 0; segOff < shiftedLength; segOff += kStrideSamples, ++chunkIdx)
    {
        if (shouldCancel && shouldCancel())
        {
            lastError_ = "Cancelled by user";
            juce::Logger::writeToLog ("[ONNX] === separate CANCELLED at chunk "
                                      + juce::String (chunkIdx) + " ===");
            return false;
        }

        const int chunkLength = std::min (kSegmentSamples, shiftedLength - segOff);

        // Copy chunk into mixBuf (zero-padded right). Source is the shifted view.
        std::memset (mixBuf.data(), 0, sizeof (float) * mixBuf.size());
        const float* shiftedL = padL.data() + shiftOffset + segOff;
        const float* shiftedR = padR.data() + shiftOffset + segOff;
        std::memcpy (mixBuf.data(),
                     shiftedL,
                     sizeof (float) * (size_t) chunkLength);
        std::memcpy (mixBuf.data() + (size_t) kSegmentSamples,
                     shiftedR,
                     sizeof (float) * (size_t) chunkLength);

        Ort::Value mixTensor = Ort::Value::CreateTensor<float> (
            *memInfo_, mixBuf.data(), mixBuf.size(), mixShape.data(), mixShape.size());

        try
        {
            const char* inNames[]  = { inputNames[0] };
            const char* outNames[] = { outputNames[0] };
            auto outputs = session_->Run (Ort::RunOptions { nullptr },
                                          inNames, &mixTensor, 1,
                                          outNames, 1);

            if (outputs.empty() || ! outputs[0].IsTensor())
            {
                lastError_ = "ONNX Run returned no tensor at chunk "
                           + juce::String (chunkIdx);
                return false;
            }

            // sources is [1, 4, 2, segment_samples] row-major: stem, channel, sample.
            const float* sources = outputs[0].GetTensorData<float>();

            // chunk-0 diagnostic: per-stem RMS so we can sanity-check the model.
            if (chunkIdx == 0)
            {
                juce::String wmsg ("[OnnxSep] chunk 0 sources RMS per stem:");
                for (int s = 0; s < kNumStems; ++s)
                {
                    const float* sL = sources + (size_t) s * (kNumChannels * kSegmentSamples);
                    const float* sR = sL + kSegmentSamples;
                    double ss = 0.0;
                    for (int i = 0; i < chunkLength; ++i)
                        ss += (double) sL[i] * sL[i] + (double) sR[i] * sR[i];
                    const double rms = std::sqrt (ss / (double) juce::jmax (1, 2 * chunkLength));
                    wmsg += " " + juce::String (rms, 3);
                    if (s < kNumStems - 1) wmsg += ",";
                }
                juce::Logger::writeToLog (wmsg);
            }

            // Accumulate weighted output (mirrors sevagh model_apply.cpp:172-186).
            for (int s = 0; s < kNumStems; ++s)
            {
                const float* srcL = sources + (size_t) s * (kNumChannels * kSegmentSamples) + 0 * kSegmentSamples;
                const float* srcR = sources + (size_t) s * (kNumChannels * kSegmentSamples) + 1 * kSegmentSamples;
                double* dstL = outL[s].data() + segOff;
                double* dstR = outR[s].data() + segOff;
                for (int i = 0; i < chunkLength; ++i)
                {
                    const float w = weight[(size_t) i];
                    dstL[i] += (double) (w * srcL[i]);
                    dstR[i] += (double) (w * srcR[i]);
                }
            }

            // Accumulate sum_weight (sevagh model_apply.cpp:189-190).
            double* wAcc = sumWeight.data() + segOff;
            for (int i = 0; i < chunkLength; ++i)
                wAcc[i] += (double) weight[(size_t) i];
        }
        catch (const std::exception& e)
        {
            lastError_ = juce::String ("ONNX Run failed at chunk ")
                       + juce::String (chunkIdx) + ": " + e.what();
            juce::Logger::writeToLog ("[ONNX] FAILED: " + lastError_);
            return false;
        }

        const float p = 0.05f + 0.85f * ((float) (chunkIdx + 1) / (float) nChunks);
        if (progress)
            progress (p, "Separating chunk " + juce::String (chunkIdx + 1)
                       + " / " + juce::String (nChunks));
    }

    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
    juce::Logger::writeToLog ("[ONNX]   inference done in "
                              + juce::String (elapsedMs / 1000.0, 2) + " s (RTF="
                              + juce::String (elapsedMs / 1000.0 / audioDuration, 3) + ")");

    if (progress) progress (0.92f, "Finalising stems");

    // ------------------------------------------------------------------------
    // STEP 7 — normalise by sum_weight then DENORMALIZE
    //          (sevagh model_apply.cpp:193-216).
    //   out /= sum_weight
    //   out = out * ref_std + ref_mean
    // ------------------------------------------------------------------------
    for (int s = 0; s < kNumStems; ++s)
    {
        for (int i = 0; i < shiftedLength; ++i)
        {
            const double w = sumWeight[(size_t) i];
            const double wsafe = (w > 1.0e-12) ? w : 1.0e-12;
            outL[s][(size_t) i] = (outL[s][(size_t) i] / wsafe) * refStd + refMean;
            outR[s][(size_t) i] = (outR[s][(size_t) i] / wsafe) * refStd + refMean;
        }
    }

    // ------------------------------------------------------------------------
    // STEP 8 — crop to original length (sevagh model_apply.cpp:218-225).
    //   trim_start = max_shift - shift_offset
    //   extent     = length
    // ------------------------------------------------------------------------
    const int trimStart = kMaxShiftSamples - shiftOffset;
    juce::Logger::writeToLog ("[ONNX]   crop: trim_start=" + juce::String (trimStart)
                              + " length=" + juce::String (length));

    std::vector<float> finalL[kNumStems];
    std::vector<float> finalR[kNumStems];
    for (int s = 0; s < kNumStems; ++s)
    {
        finalL[s].assign ((size_t) length, 0.0f);
        finalR[s].assign ((size_t) length, 0.0f);
        for (int i = 0; i < length; ++i)
        {
            finalL[s][(size_t) i] = (float) outL[s][(size_t) (trimStart + i)];
            finalR[s][(size_t) i] = (float) outR[s][(size_t) (trimStart + i)];
        }

        // Per-stem RMS / peak — useful when verifying the smank export's
        // stem ordering matches the canonical (drums, bass, other, vocals).
        double ss = 0.0;
        float pk = 0.0f;
        for (int i = 0; i < length; ++i)
        {
            const float l = finalL[s][(size_t) i];
            const float r = finalR[s][(size_t) i];
            ss += (double) l * l + (double) r * r;
            const float al = std::fabs (l);
            const float ar = std::fabs (r);
            if (al > pk) pk = al;
            if (ar > pk) pk = ar;
        }
        const float rms = (float) std::sqrt (ss / (double) juce::jmax (1, 2 * length));
        juce::Logger::writeToLog ("[OnnxSep] stem " + juce::String (s)
                                  + " RMS=" + juce::String (rms, 4)
                                  + " peak=" + juce::String (pk, 4));
    }

    if (progress) progress (0.96f, "Writing stems");

    // ------------------------------------------------------------------------
    // Write WAVs. Stem order per sevagh demucs.onnx README / Demucs v4
    // canonical: drums (0), bass (1), other (2), vocals (3).
    // ------------------------------------------------------------------------
    const juce::String base = inputAudio.getFileNameWithoutExtension();
    const std::array<juce::String, 4> names { "Drums", "Bass", "Other", "Vocals" };

    auto stemFile = [&] (int s) {
        return outputDir.getChildFile (base + "_" + names[(size_t) s] + ".wav");
    };

    {
        const juce::File f = stemFile (0);
        if (! writeStemWav (f, finalL[0].data(), finalR[0].data(), length, lastError_))
            return false;
        lastStems_.drums = f;
    }

    if (! drumsOnly)
    {
        const juce::File fb = stemFile (1);
        const juce::File fo = stemFile (2);
        const juce::File fv = stemFile (3);
        if (! writeStemWav (fb, finalL[1].data(), finalR[1].data(), length, lastError_)) return false;
        if (! writeStemWav (fo, finalL[2].data(), finalR[2].data(), length, lastError_)) return false;
        if (! writeStemWav (fv, finalL[3].data(), finalR[3].data(), length, lastError_)) return false;
        lastStems_.bass   = fb;
        lastStems_.other  = fo;
        lastStems_.vocals = fv;
    }

    if (progress) progress (1.0f, "Done");
    juce::Logger::writeToLog ("[ONNX] === separate SUCCESS ===");
    return true;
}

} // namespace samplar
