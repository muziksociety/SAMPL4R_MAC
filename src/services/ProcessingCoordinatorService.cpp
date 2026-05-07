#include "ProcessingCoordinatorService.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>
#include <algorithm>
#include <vector>
#include <cmath>
// SAMPLAR: removed - DrumSliceService and DrumAnalyzer deleted
#include "../../third_party/minibpm/MiniBpm.h"
#include "analysis/LoopDetector.h"

namespace samplar {

static constexpr const char* kEvtEnqueue  = "processing.enqueue";
static constexpr const char* kEvtProgress = "processing.progress";
static constexpr const char* kEvtResult   = "processing.result";
static constexpr const char* kEvtError    = "processing.error";
static constexpr const char* kEvtViz      = "processing.viz";

void ProcessingCoordinatorService::initialise()
{
    stopping_ = false;

    // Subscribe to enqueue event
    subId_ = bus_.subscribe(kEvtEnqueue, [this](const juce::var& v){ handleEnqueue(v); });

    // Initialize FullSongService
    fullSongService_ = std::make_unique<FullSongService>(bus_);
    fullSongService_->initialise();

    // Initialize audio format manager for tempo-first pipeline
    formatManager_.registerBasicFormats();
    juce::Logger::writeToLog("[ProcessingCoordinator] Tempo-First pipeline initialized");
}

void ProcessingCoordinatorService::shutdown()
{
    juce::Logger::writeToLog("[ProcessingCoordinator] === SHUTDOWN ===");

    // Signal that we're stopping - no new jobs will be accepted
    stopping_ = true;

    // Wait for all active threads to complete
    {
        std::lock_guard<std::mutex> lock(threadsMutex_);
        juce::Logger::writeToLog("[ProcessingCoordinator] Waiting for " + juce::String(activeThreads_.size()) + " processing threads...");
        for (auto& t : activeThreads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        activeThreads_.clear();
    }
    juce::Logger::writeToLog("[ProcessingCoordinator] All processing threads joined");

    // Shutdown FullSongService
    if (fullSongService_) {
        fullSongService_->shutdown();
        fullSongService_.reset();
    }

    if (subId_ != 0) {
        bus_.unsubscribe(kEvtEnqueue, subId_);
        subId_ = 0;
    }

    juce::Logger::writeToLog("[ProcessingCoordinator] Shutdown complete");
}

void ProcessingCoordinatorService::cleanupFinishedThreads()
{
    // Remove threads that have finished (joinable threads that we can join immediately)
    // Note: This is called periodically to prevent unbounded thread accumulation
    std::lock_guard<std::mutex> lock(threadsMutex_);
    // We can't easily detect finished threads without a flag, so we just keep them until shutdown
    // In a production system, you might want to use std::async with futures or a proper thread pool
}

void ProcessingCoordinatorService::enqueue(const juce::File& input, int mode)
{
    // Clear any pending beat grid since this is called without one
    pendingBeatGrid_.reset();

    juce::Logger::writeToLog("[TRACE] [TRACE] ProcessingCoordinatorService::enqueue called");
    juce::Logger::writeToLog("[TRACE] [TRACE] Input file: " + input.getFullPathName());
    juce::Logger::writeToLog("[TRACE] [TRACE] Mode: " + juce::String(mode));
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("file", input.getFullPathName());
    obj->setProperty("mode", mode);
    juce::Logger::writeToLog("[TRACE] [TRACE] Publishing enqueue event to event bus");
    bus_.publish(kEvtEnqueue, juce::var(obj.get()));
    juce::Logger::writeToLog("[TRACE] [TRACE] ProcessingCoordinatorService::enqueue COMPLETED");
}

void ProcessingCoordinatorService::enqueue(const juce::File& input, int mode, const BeatGridInfo& beatGrid)
{
    // Store beat grid from WavePreviewPage - this is the source of truth
    pendingBeatGrid_ = beatGrid;
    pendingGridSlicePoints_.clear();  // Clear any previous grid slice points

    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] enqueue with BeatGridInfo");
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] BPM: " + juce::String(beatGrid.bpm.bpm, 2) +
        ", Downbeat: " + juce::String(beatGrid.downbeatTimeSeconds, 3) + "s" +
        ", Loops: " + juce::String(static_cast<int>(beatGrid.detectedLoops.size())));

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("file", input.getFullPathName());
    obj->setProperty("mode", mode);
    juce::Logger::writeToLog("[TRACE] [TRACE] Publishing enqueue event to event bus (with beat grid)");
    bus_.publish(kEvtEnqueue, juce::var(obj.get()));
    juce::Logger::writeToLog("[TRACE] [TRACE] ProcessingCoordinatorService::enqueue with BeatGrid COMPLETED");
}

void ProcessingCoordinatorService::enqueue(const juce::File& input, int mode, const BeatGridInfo& beatGrid,
                                           const std::vector<double>& gridSlicePoints)
{
    // Store beat grid and user-defined slice points from WavePreviewPage Step 3
    pendingBeatGrid_ = beatGrid;
    pendingGridSlicePoints_ = gridSlicePoints;

    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] enqueue with GridSlicePoints (Mode 4)");
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] BPM: " + juce::String(beatGrid.bpm.bpm, 2) +
        ", Slice points: " + juce::String(static_cast<int>(gridSlicePoints.size())) +
        ", Detected Loops: " + juce::String(static_cast<int>(beatGrid.detectedLoops.size())));

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("file", input.getFullPathName());
    obj->setProperty("mode", mode);  // Mode 4 = GridSlice
    juce::Logger::writeToLog("[TRACE] Publishing enqueue event for GridSlice mode");
    bus_.publish(kEvtEnqueue, juce::var(obj.get()));
    juce::Logger::writeToLog("[TRACE] ProcessingCoordinatorService::enqueue with GridSlicePoints COMPLETED");
}

void ProcessingCoordinatorService::handleEnqueue(const juce::var& payload)
{
    juce::Logger::writeToLog("[Processing] handleEnqueue called");

    // Reject new jobs if we're shutting down
    if (stopping_) {
        juce::Logger::writeToLog("[Processing] Rejected - service is stopping");
        publishError("shutting_down", "Service is shutting down");
        return;
    }

    // Reject if already processing (prevents concurrent job crashes)
    if (isProcessing_.exchange(true)) {
        juce::Logger::writeToLog("[Processing] Rejected - already processing a job");
        publishError("busy", "Already processing - please wait");
        return;
    }

    if (!payload.isObject()) {
        juce::Logger::writeToLog("[Processing] ERROR: Payload is not an object");
        isProcessing_ = false;  // Reset flag on error
        publishError("bad_payload", "Enqueue payload must be object");
        return;
    }

    auto* o = payload.getDynamicObject();
    juce::File input = juce::File(o->getProperty("file").toString());
    int mode = static_cast<int>(o->getProperty("mode"));
    juce::Logger::writeToLog("[Processing] Input file: " + input.getFullPathName());
    juce::Logger::writeToLog("[Processing] Mode: " + juce::String(mode));

    if (!input.existsAsFile()) {
        juce::Logger::writeToLog("[Processing] ERROR: Input file does not exist!");
        isProcessing_ = false;  // Reset flag on error
        publishError("no_input", "Input file not found");
        return;
    }

    // Dispatch based on mode: 1=DrumStem (C++), 2=FullSong (Python ZMQ then C++)
    publishProgress(5, "Queued");
    juce::Logger::writeToLog("[Processing] Enqueued: " + input.getFileName());

    // Create thread and store it for safe shutdown
    std::thread processingThread([this, input, mode]{
        // RAII guard to always reset isProcessing_ when thread exits
        struct ProcessingGuard {
            std::atomic<bool>& flag;
            ~ProcessingGuard() { flag = false; }
        } guard{isProcessing_};

        if (stopping_) return;  // Early exit if shutdown started

        if (mode == 1) {
            // SAMPLAR: removed - DrumSliceService deleted; mode 1 not used in SAMPLAR
            publishError("unsupported_mode", "Mode 1 (DrumSlice) is not available in SAMPLAR");
            juce::Logger::writeToLog("[Processing] Mode 1 DrumSlice not available - SAMPLAR build");

        } else if (mode == 2) {
            publishProgress(0, "Queued");
            juce::Logger::writeToLog("[Processing] FullSong begin");

            FullSongService::Config cfg;
            cfg.onProgress = [this](int pct, const juce::String& msg) {
                if (!stopping_) publishProgress(pct, msg);
            };

            fullSongService_->onVizProgress = [this](const juce::var& v){
                if (!stopping_) bus_.publish(kEvtViz, v);
            };

            auto res = fullSongService_->process(input, cfg, "fullSong_");
            if (stopping_) return;

            if (res.error.isNotEmpty()) {
                publishError("full_song_failed", res.error);
                juce::Logger::writeToLog("[Processing] FullSong Error: " + res.error);
                return;
            }

            juce::Array<juce::var> arr;
            for (auto& f : res.sampleFiles) arr.add(f.getFullPathName());
            juce::DynamicObject::Ptr r = new juce::DynamicObject();
            r->setProperty("outputs", juce::var(arr));
            r->setProperty("outputDir", res.outputDir.getFullPathName());
            r->setProperty("drumStemFile", res.drumStemFile.getFullPathName());

            if (!stopping_) {
                publishProgress(100, "Done");
                bus_.publish(kEvtResult, juce::var(r.get()));
            }
            juce::Logger::writeToLog("[Processing] FullSong done: " + res.outputDir.getFullPathName());

        } else if (mode == 3) {
            // Mode 3: Tempo-First Pipeline (intelligent beat-aligned slicing)
            processTempoFirst(input);

        } else if (mode == 4) {
            // Mode 4: GridSlice (user-defined slice points from WavePreviewPage Step 3)
            processGridSlice(input);

        } else {
            publishError("invalid_mode", "Unknown processing mode: " + juce::String(mode));
        }
    });

    // Store thread for cleanup during shutdown
    {
        std::lock_guard<std::mutex> lock(threadsMutex_);
        activeThreads_.push_back(std::move(processingThread));
    }
}

void ProcessingCoordinatorService::publishProgress(int pct, const juce::String& msg)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("pct", pct);
    obj->setProperty("msg", msg);
    // Add step classification for SM4 parity
    if (pct < 80) obj->setProperty("step", "separate");
    else if (pct < 100) obj->setProperty("step", "detect");
    else obj->setProperty("step", "done");
    bus_.publish(kEvtProgress, juce::var(obj.get()));
}

void ProcessingCoordinatorService::publishResult(const juce::Array<juce::File>& outputs)
{
    juce::Array<juce::var> arr;
    for (auto& f : outputs) arr.add(f.getFullPathName());
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("outputs", juce::var(arr));
    bus_.publish(kEvtResult, juce::var(obj.get()));
}

void ProcessingCoordinatorService::publishError(const juce::String& code, const juce::String& msg)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("code", code);
    obj->setProperty("msg", msg);
    bus_.publish(kEvtError, juce::var(obj.get()));
}

//==============================================================================
// Tempo-First Pipeline Processing
// Uses DrumSliceService (for working animation) + calculates BPM from slice intervals
//==============================================================================

void ProcessingCoordinatorService::processTempoFirst(const juce::File& input)
{
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] === TEMPO-FIRST PIPELINE (Slice-Based BPM) ===");
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Input: " + input.getFileName());

    // Check if we have pre-computed beat grid from WavePreviewPage
    const bool hasBeatGrid = pendingBeatGrid_.has_value() && !pendingBeatGrid_->detectedLoops.empty();
    if (hasBeatGrid) {
        juce::Logger::writeToLog("✅ [ProcessingCoordinator] Using beat grid from WavePreviewPage (source of truth)");
        juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Pre-computed BPM: " + juce::String(pendingBeatGrid_->bpm.bpm, 2));
        juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Pre-computed loops: " + juce::String(static_cast<int>(pendingBeatGrid_->detectedLoops.size())));
    } else {
        juce::Logger::writeToLog("🎵 [ProcessingCoordinator] No pre-computed beat grid - will use MiniBPM");
    }

    // SAMPLAR: removed - DrumSliceService deleted; tempo-first pipeline stubs to beat-grid path only
    publishProgress(10, "Analyzing tempo...");

    // Collect onset times placeholder (no DrumSliceService available)
    std::vector<double> onsetTimes;

    // SAMPLAR: stub result (DrumSliceService removed) — sampleFiles populated by beat-grid path below
    struct StubResult { juce::String error; juce::Array<juce::File> sampleFiles; juce::File outputDir; } res;
    res.error = "";
    res.outputDir = input.getParentDirectory();

    if (stopping_) return;

    //==============================================================================
    // BPM DETECTION: Use pre-computed from WavePreviewPage or calculate with MiniBPM
    //==============================================================================
    float calculatedBPM = 0.0f;

    // If we have beat grid from WavePreviewPage, use it directly (source of truth)
    if (hasBeatGrid) {
        calculatedBPM = pendingBeatGrid_->bpm.bpm;
        juce::Logger::writeToLog("✅ [ProcessingCoordinator] Using pre-computed BPM from WavePreviewPage: " +
            juce::String(calculatedBPM, 1) + " BPM");

        // Publish BPM to UI
        juce::DynamicObject::Ptr bpmObj = new juce::DynamicObject();
        bpmObj->setProperty("pct", 85);
        bpmObj->setProperty("msg", "Tempo: " + juce::String(calculatedBPM, 1) + " BPM (from preview)");
        bpmObj->setProperty("bpm", calculatedBPM);
        bpmObj->setProperty("step", "tempo");
        bus_.publish(kEvtProgress, juce::var(bpmObj.get()));

        //==============================================================================
        // LOOP SLICING: Use pre-computed loops from WavePreviewPage (source of truth)
        //==============================================================================
        publishProgress(80, "Slicing loops from preview...");
        juce::Logger::writeToLog("🔄 [LoopSlicer] === SLICING PRE-COMPUTED LOOPS FROM WAVEPREVIEWPAGE ===");
        juce::Logger::writeToLog("🔄 [LoopSlicer] Number of loops: " + juce::String(static_cast<int>(pendingBeatGrid_->detectedLoops.size())));

        // Load the audio file for slicing
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(input));
        if (reader != nullptr) {
            const double sampleRate = reader->sampleRate;
            const int64_t numSamples = static_cast<int64_t>(reader->lengthInSamples);
            const int numChannels = static_cast<int>(reader->numChannels);

            juce::Logger::writeToLog("🔄 [LoopSlicer] Audio: " + juce::String(sampleRate) + " Hz, " +
                juce::String(numSamples / sampleRate, 2) + "s, " + juce::String(numChannels) + " ch");

            // Read audio into buffer
            juce::AudioBuffer<float> audioBuffer(numChannels, static_cast<int>(numSamples));
            audioBuffer.clear();
            reader->read(&audioBuffer, 0, static_cast<int>(numSamples), 0, true, true);

            juce::WavAudioFormat wavFormat;
            int loopCount = 0;

            // Slice each pre-computed loop from WavePreviewPage
            for (size_t i = 0; i < pendingBeatGrid_->detectedLoops.size(); ++i) {
                const auto& loop = pendingBeatGrid_->detectedLoops[i];

                // Convert time to samples
                const int64_t startSample = static_cast<int64_t>(loop.startTimeSeconds * sampleRate);
                const int64_t endSample = static_cast<int64_t>(loop.endTimeSeconds * sampleRate);
                const int loopLength = static_cast<int>(std::min(endSample, numSamples) - startSample);

                if (loopLength <= 0 || startSample >= numSamples) {
                    juce::Logger::writeToLog("⚠️ [LoopSlicer] Skipping loop " + juce::String(i+1) +
                        " - invalid bounds (start=" + juce::String(loop.startTimeSeconds, 3) +
                        "s, end=" + juce::String(loop.endTimeSeconds, 3) + "s)");
                    continue;
                }

                juce::Logger::writeToLog("🔄 [LoopSlicer] Loop " + juce::String(i+1) + ": " +
                    juce::String(loop.startTimeSeconds, 3) + "s - " + juce::String(loop.endTimeSeconds, 3) +
                    "s (" + juce::String(loop.lengthBars) + " bars)");

                // Create loop buffer
                juce::AudioBuffer<float> loopBuffer(numChannels, loopLength);
                loopBuffer.clear();
                for (int ch = 0; ch < numChannels; ++ch) {
                    loopBuffer.copyFrom(ch, 0, audioBuffer, ch, static_cast<int>(startSample), loopLength);
                }

                // ALL detected loops use LOOP_ prefix = WHITE in browser
                juce::String loopName = "LOOP_" + juce::String(i + 1).paddedLeft('0', 3) +
                                        "_" + juce::String(loop.lengthBars) + "bar_" +
                                        juce::String(calculatedBPM, 1) + "bpm.wav";
                juce::File loopFile = res.outputDir.getChildFile(loopName);

                std::unique_ptr<juce::FileOutputStream> outStream(loopFile.createOutputStream());
                if (outStream) {
                    std::unique_ptr<juce::AudioFormatWriter> writer(
                        wavFormat.createWriterFor(outStream.get(), sampleRate,
                                                   static_cast<unsigned int>(numChannels), 24, {}, 0));
                    if (writer) {
                        outStream.release(); // Writer takes ownership
                        writer->writeFromAudioSampleBuffer(loopBuffer, 0, loopLength);
                        res.sampleFiles.add(loopFile);
                        loopCount++;
                        juce::Logger::writeToLog("✅ [LoopSlicer] Saved: " + loopName);
                    }
                }
            }

            juce::Logger::writeToLog("✅ [LoopSlicer] Slicing complete! " + juce::String(loopCount) +
                " loops from WavePreviewPage (source of truth)");
        } else {
            juce::Logger::writeToLog("❌ [LoopSlicer] Failed to load audio file for slicing");
            publishError("audio_load_failed", "Could not load audio file for loop slicing");
            return;
        }
    } else {
        // Fallback: Use MiniBPM for auto-detection

    juce::Logger::writeToLog("🎵 [MiniBPM] === AUTOCORRELATION TEMPO DETECTION ===");
    juce::Logger::writeToLog("🎵 [MiniBPM] Loading audio file: " + input.getFileName());

    // Load the audio file
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(input));

    if (reader != nullptr) {
        const double sampleRate = reader->sampleRate;
        const int64_t numSamples = static_cast<int64_t>(reader->lengthInSamples);
        const int numChannels = static_cast<int>(reader->numChannels);

        juce::Logger::writeToLog("🎵 [MiniBPM] Sample rate: " + juce::String(sampleRate) + " Hz");
        juce::Logger::writeToLog("🎵 [MiniBPM] Duration: " + juce::String(numSamples / sampleRate, 2) + " seconds");
        juce::Logger::writeToLog("🎵 [MiniBPM] Channels: " + juce::String(numChannels));

        // Read audio into buffer
        juce::AudioBuffer<float> audioBuffer(numChannels, static_cast<int>(numSamples));
        audioBuffer.clear();
        reader->read(&audioBuffer, 0, static_cast<int>(numSamples), 0, true, true);

        // Create mono buffer (MiniBPM requires single channel)
        std::vector<float> monoSamples(static_cast<size_t>(numSamples));

        if (numChannels == 1) {
            // Already mono
            std::memcpy(monoSamples.data(), audioBuffer.getReadPointer(0),
                       static_cast<size_t>(numSamples) * sizeof(float));
        } else {
            // Average channels to mono
            const float* left = audioBuffer.getReadPointer(0);
            const float* right = audioBuffer.getReadPointer(1);
            for (int64_t i = 0; i < numSamples; ++i) {
                monoSamples[static_cast<size_t>(i)] = (left[i] + right[i]) * 0.5f;
            }
        }

        juce::Logger::writeToLog("🎵 [MiniBPM] Running MiniBPM tempo estimation...");

        // Create MiniBPM instance and analyze
        breakfastquay::MiniBPM bpmDetector(static_cast<float>(sampleRate));

        // Set BPM range (default is 55-190, but we can customize)
        bpmDetector.setBPMRange(50.0, 200.0);

        // Analyze tempo
        double detectedBPM = bpmDetector.estimateTempoOfSamples(monoSamples.data(),
                                                                static_cast<int>(numSamples));

        // Get all tempo candidates for logging
        std::vector<double> candidates = bpmDetector.getTempoCandidates();
        juce::Logger::writeToLog("🎵 [MiniBPM] Top candidates:");
        for (size_t i = 0; i < std::min(candidates.size(), size_t(5)); ++i) {
            juce::Logger::writeToLog("🎵 [MiniBPM]   #" + juce::String(i+1) + ": " +
                juce::String(candidates[i], 1) + " BPM");
        }

        if (detectedBPM > 0) {
            // Keep full precision - no rounding
            calculatedBPM = static_cast<float>(detectedBPM);

            juce::Logger::writeToLog("✅ [MiniBPM] FINAL BPM: " + juce::String(calculatedBPM, 3));

            // Publish BPM to UI
            juce::DynamicObject::Ptr bpmObj = new juce::DynamicObject();
            bpmObj->setProperty("pct", 85);
            bpmObj->setProperty("msg", "Tempo: " + juce::String(calculatedBPM, 3) + " BPM");
            bpmObj->setProperty("bpm", calculatedBPM);
            bpmObj->setProperty("step", "tempo");
            bus_.publish(kEvtProgress, juce::var(bpmObj.get()));
            //==============================================================================
            // LOOP DETECTION - Find 4-bar loops and individual samples
            //==============================================================================
            publishProgress(75, "Detecting loops...");

            LoopDetector loopDetector;
            loopDetector.setTargetLoopBars(4); // 4-bar loops

            SliceResult loopResult = loopDetector.analyze(
                monoSamples.data(),
                static_cast<int64_t>(numSamples),
                sampleRate,
                static_cast<double>(calculatedBPM)
            );

            juce::Logger::writeToLog("🔄 [LoopSlicer] Found " + juce::String(loopResult.loopBoundaries.size()) +
                                     " loop boundaries, " + juce::String(loopResult.sampleBoundaries.size()) +
                                     " sample onsets");

            //==============================================================================
            // SLICE AUDIO INTO LOOPS (4-bar) AND SAMPLES (individual hits)
            // ALL FILES GO TO SAME FOLDER - loops have "LOOP_" prefix for WHITE color in browser
            //==============================================================================
            publishProgress(80, "Slicing loops and samples...");

            juce::WavAudioFormat wavFormat;
            int loopCount = 0;
            int sampleCount = 0;

            // Slice 4-bar loops - save to SAME folder with LOOP_ prefix
            juce::Logger::writeToLog("🔄 [LoopSlicer] === SLICING 4-BAR LOOPS ===");
            for (size_t i = 0; i < loopResult.loopBoundaries.size(); ++i) {
                const int64_t startSample = loopResult.loopBoundaries[i];
                const int64_t endSample = (i + 1 < loopResult.loopBoundaries.size())
                    ? loopResult.loopBoundaries[i + 1]
                    : static_cast<int64_t>(numSamples);

                const int loopLength = static_cast<int>(endSample - startSample);
                if (loopLength <= 0) continue;

                // Create loop buffer
                juce::AudioBuffer<float> loopBuffer(numChannels, loopLength);
                loopBuffer.clear();
                for (int ch = 0; ch < numChannels; ++ch) {
                    loopBuffer.copyFrom(ch, 0, audioBuffer, ch,
                                        static_cast<int>(startSample), loopLength);
                }

                // Save loop file to SAME folder with LOOP_ prefix (for WHITE color in browser)
                juce::String loopName = "LOOP_" + juce::String(i + 1).paddedLeft('0', 3) +
                                        "_4bar_" + juce::String(calculatedBPM, 1) + "bpm.wav";
                juce::File loopFile = res.outputDir.getChildFile(loopName);

                std::unique_ptr<juce::FileOutputStream> outStream(loopFile.createOutputStream());
                if (outStream) {
                    std::unique_ptr<juce::AudioFormatWriter> writer(
                        wavFormat.createWriterFor(outStream.get(), sampleRate,
                                                   static_cast<unsigned int>(numChannels), 24, {}, 0));
                    if (writer) {
                        outStream.release(); // Writer takes ownership
                        writer->writeFromAudioSampleBuffer(loopBuffer, 0, loopLength);
                        res.sampleFiles.add(loopFile);
                        loopCount++;
                        juce::Logger::writeToLog("✅ [LoopSlicer] Saved LOOP: " + loopName);
                    }
                }
            }

            // Slice individual samples (drum hits)
            juce::Logger::writeToLog("🔄 [LoopSlicer] === SLICING INDIVIDUAL SAMPLES ===");
            for (size_t i = 0; i < loopResult.sampleBoundaries.size(); ++i) {
                const int64_t startSample = loopResult.sampleBoundaries[i];
                const int64_t endSample = (i + 1 < loopResult.sampleBoundaries.size())
                    ? loopResult.sampleBoundaries[i + 1]
                    : static_cast<int64_t>(numSamples);

                const int sampleLength = static_cast<int>(endSample - startSample);

                // Skip only very short samples (< 50ms)
                // Long samples (> 2s) are now allowed and will display as GRAY in browser
                const int minLength = static_cast<int>(sampleRate * 0.05);
                if (sampleLength < minLength) continue;

                // Create sample buffer
                juce::AudioBuffer<float> sampleBuffer(numChannels, sampleLength);
                sampleBuffer.clear();
                for (int ch = 0; ch < numChannels; ++ch) {
                    sampleBuffer.copyFrom(ch, 0, audioBuffer, ch,
                                          static_cast<int>(startSample), sampleLength);
                }

                // Save sample file to SAME folder (drum analyzer will color by type)
                juce::String sampleName = "slice_" + juce::String(i + 1).paddedLeft('0', 4) + ".wav";
                juce::File sampleFile = res.outputDir.getChildFile(sampleName);

                std::unique_ptr<juce::FileOutputStream> outStream(sampleFile.createOutputStream());
                if (outStream) {
                    std::unique_ptr<juce::AudioFormatWriter> writer(
                        wavFormat.createWriterFor(outStream.get(), sampleRate,
                                                   static_cast<unsigned int>(numChannels), 24, {}, 0));
                    if (writer) {
                        outStream.release(); // Writer takes ownership
                        writer->writeFromAudioSampleBuffer(sampleBuffer, 0, sampleLength);
                        res.sampleFiles.add(sampleFile);
                        sampleCount++;
                    }
                }
            }

            juce::Logger::writeToLog("✅ [LoopSlicer] Slicing complete!");
            juce::Logger::writeToLog("✅ [LoopSlicer] LOOPS: " + juce::String(loopCount) + " x 4-bar loops (WHITE in browser)");
            juce::Logger::writeToLog("✅ [LoopSlicer] SAMPLES: " + juce::String(sampleCount) + " drum hits (colored by type)");

        } else {
            juce::Logger::writeToLog("❌ [MiniBPM] Could not detect tempo (clip too short?)");
        }
    } else {
        juce::Logger::writeToLog("❌ [MiniBPM] Failed to load audio file for tempo detection");
    }
    } // End of MiniBPM else block

    //==============================================================================
    // USE PRE-COMPUTED LOOPS FROM WAVEPREVIEWPAGE (when available)
    // This is the SOURCE OF TRUTH - user already set the loop boundaries in preview
    //==============================================================================
    if (hasBeatGrid && calculatedBPM > 0) {
        publishProgress(75, "Using pre-computed loops from preview...");

        // Load the audio file for slicing
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(input));
        if (reader != nullptr) {
            const double sampleRate = reader->sampleRate;
            const int64_t numSamples = static_cast<int64_t>(reader->lengthInSamples);
            const int numChannels = static_cast<int>(reader->numChannels);

            juce::AudioBuffer<float> audioBuffer(numChannels, static_cast<int>(numSamples));
            audioBuffer.clear();
            reader->read(&audioBuffer, 0, static_cast<int>(numSamples), 0, true, true);

            juce::WavAudioFormat wavFormat;
            int loopCount = 0;

            juce::Logger::writeToLog("✅ [BeatGrid] === SLICING FROM PRE-COMPUTED BEAT GRID ===");
            juce::Logger::writeToLog("✅ [BeatGrid] Loops from WavePreviewPage: " +
                juce::String(static_cast<int>(pendingBeatGrid_->detectedLoops.size())));

            publishProgress(80, "Slicing pre-computed loops...");

            // Slice loops based on WavePreviewPage's detectedLoops (source of truth!)
            for (size_t i = 0; i < pendingBeatGrid_->detectedLoops.size(); ++i) {
                const auto& loop = pendingBeatGrid_->detectedLoops[i];
                const int64_t startSample = static_cast<int64_t>(loop.startTimeSeconds * sampleRate);
                const int64_t endSample = static_cast<int64_t>(loop.endTimeSeconds * sampleRate);
                const int loopLength = static_cast<int>(endSample - startSample);

                if (loopLength <= 0) {
                    juce::Logger::writeToLog("⚠️ [BeatGrid] Skipping loop " + juce::String(i+1) + " - invalid length");
                    continue;
                }

                // Ensure we don't read past the end of the buffer
                const int actualLength = std::min(loopLength, static_cast<int>(numSamples) - static_cast<int>(startSample));
                if (actualLength <= 0) continue;

                // Create loop buffer
                juce::AudioBuffer<float> loopBuffer(numChannels, actualLength);
                loopBuffer.clear();
                for (int ch = 0; ch < numChannels; ++ch) {
                    loopBuffer.copyFrom(ch, 0, audioBuffer, ch, static_cast<int>(startSample), actualLength);
                }

                // ALL detected loops use LOOP_ prefix = WHITE in browser
                juce::String barsStr = juce::String(loop.lengthBars) + "bar";
                juce::String loopName = "LOOP_" + juce::String(i + 1).paddedLeft('0', 3) +
                                        "_" + barsStr + "_" + juce::String(calculatedBPM, 1) + "bpm.wav";
                juce::File loopFile = res.outputDir.getChildFile(loopName);

                std::unique_ptr<juce::FileOutputStream> outStream(loopFile.createOutputStream());
                if (outStream) {
                    std::unique_ptr<juce::AudioFormatWriter> writer(
                        wavFormat.createWriterFor(outStream.get(), sampleRate,
                                                   static_cast<unsigned int>(numChannels), 24, {}, 0));
                    if (writer) {
                        outStream.release(); // Writer takes ownership
                        writer->writeFromAudioSampleBuffer(loopBuffer, 0, actualLength);
                        res.sampleFiles.add(loopFile);
                        loopCount++;
                        juce::Logger::writeToLog("✅ [BeatGrid] Saved LOOP: " + loopName +
                            " (start=" + juce::String(loop.startTimeSeconds, 3) + "s, end=" +
                            juce::String(loop.endTimeSeconds, 3) + "s)");
                    }
                }
            }

            juce::Logger::writeToLog("✅ [BeatGrid] Pre-computed loop slicing complete!");
            juce::Logger::writeToLog("✅ [BeatGrid] LOOPS: " + juce::String(loopCount) +
                " (from WavePreviewPage - source of truth)");
        }
    }

    publishProgress(90, "Finalizing...");

    // Build result
    juce::Array<juce::var> arr;
    for (auto& f : res.sampleFiles) arr.add(f.getFullPathName());

    juce::DynamicObject::Ptr r = new juce::DynamicObject();
    r->setProperty("outputs", juce::var(arr));
    r->setProperty("outputDir", res.outputDir.getFullPathName());
    r->setProperty("bpm", calculatedBPM);
    r->setProperty("totalSlices", res.sampleFiles.size());

    if (!stopping_) {
        publishProgress(100, "Done");
        bus_.publish(kEvtResult, juce::var(r.get()));
    }

    juce::Logger::writeToLog("✅ [ProcessingCoordinator] Tempo-First pipeline complete: " +
                            juce::String(res.sampleFiles.size()) + " samples, " +
                            juce::String(calculatedBPM, 1) + " BPM");
}

//==============================================================================
// GridSlice Pipeline Processing - User-defined slice points from WavePreviewPage Step 3
// PRIORITY: Loops are sliced FIRST, then grid-based slices
//==============================================================================

void ProcessingCoordinatorService::processGridSlice(const juce::File& input)
{
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] === GRIDSLICE PIPELINE ===");
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Input: " + input.getFileName());

    // Check if we have detected loops from WavePreviewPage
    const bool hasDetectedLoops = pendingBeatGrid_.has_value() && !pendingBeatGrid_->detectedLoops.empty();
    const bool hasSlicePoints = !pendingGridSlicePoints_.empty();

    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Detected loops: " +
        juce::String(hasDetectedLoops ? static_cast<int>(pendingBeatGrid_->detectedLoops.size()) : 0));
    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Grid slice points: " +
        juce::String(static_cast<int>(pendingGridSlicePoints_.size())));

    // Must have either loops or slice points
    if (!hasDetectedLoops && !hasSlicePoints) {
        juce::Logger::writeToLog("❌ [ProcessingCoordinator] ERROR: No loops or slice points provided");
        publishError("no_slice_data", "No loops or slice points provided for GridSlice mode");
        return;
    }

    // Get BPM from beat grid if available
    float bpm = 0.0f;
    if (pendingBeatGrid_.has_value()) {
        bpm = pendingBeatGrid_->bpm.bpm;
        juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Using BPM from beat grid: " + juce::String(bpm, 1));
    }

    publishProgress(10, "Loading audio file...");

    // Load the audio file
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(input));

    if (reader == nullptr) {
        juce::Logger::writeToLog("❌ [ProcessingCoordinator] Failed to load audio file");
        publishError("audio_load_failed", "Could not load audio file for slicing");
        return;
    }

    const double sampleRate = reader->sampleRate;
    const int64_t totalSamples = static_cast<int64_t>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    const double duration = totalSamples / sampleRate;

    juce::Logger::writeToLog("🎵 [ProcessingCoordinator] Audio: " + juce::String(sampleRate) + " Hz, " +
        juce::String(duration, 2) + "s, " + juce::String(numChannels) + " ch");

    // Validate slice points (warning only if out of bounds)
    if (hasSlicePoints) {
        double maxSlice = *std::max_element(pendingGridSlicePoints_.begin(), pendingGridSlicePoints_.end());
        if (maxSlice > duration + 0.1) {
            juce::Logger::writeToLog("⚠️ [GridSlice] WARNING: Slice points extend beyond file duration");
        }
    }

    // Read audio into buffer
    juce::AudioBuffer<float> audioBuffer(numChannels, static_cast<int>(totalSamples));
    audioBuffer.clear();
    reader->read(&audioBuffer, 0, static_cast<int>(totalSamples), 0, true, true);

    // Create output directory
    juce::File outputDir = input.getParentDirectory().getChildFile(input.getFileNameWithoutExtension() + "_slices");
    if (!outputDir.exists()) {
        outputDir.createDirectory();
    } else {
        // Clear existing files from previous exports
        juce::Logger::writeToLog("🗑️ [GridSlice] Clearing previous export files from: " + outputDir.getFullPathName());
        int deletedCount = 0;
        for (const auto& file : outputDir.findChildFiles(juce::File::findFiles, false, "*.wav")) {
            if (file.deleteFile()) {
                deletedCount++;
            }
        }
        juce::Logger::writeToLog("🗑️ [GridSlice] Deleted " + juce::String(deletedCount) + " old files");
    }

    juce::WavAudioFormat wavFormat;
    juce::Array<juce::File> sampleFiles;
    int loopCount = 0;
    int sliceCount = 0;

    //==============================================================================
    // PHASE 1: SLICE DETECTED LOOPS FIRST (LOOP_XXX.wav - WHITE in browser)
    // This creates the SAME LOOP_ files as Mode 3 (TempoFirst) - loop processing is identical!
    // CRITICAL FIX: Loop boundaries are in ABSOLUTE time but file is CROPPED - apply offset!
    //==============================================================================
    if (hasDetectedLoops) {
        publishProgress(30, "Slicing detected loops...");
        juce::Logger::writeToLog("🔄 [GridSlice] === PHASE 1: SLICING DETECTED LOOPS (WHITE COLOR) ===");
        juce::Logger::writeToLog("🔄 [GridSlice] Number of loops: " +
            juce::String(static_cast<int>(pendingBeatGrid_->detectedLoops.size())));

        // CRITICAL FIX: Get selection offset for coordinate conversion
        // Loop boundaries are in ABSOLUTE time, but the audio file is CROPPED to the selection
        // We must subtract selectionOffset to convert ABSOLUTE → RELATIVE coordinates
        const double selectionOffset = pendingBeatGrid_->selectionOffset;
        juce::Logger::writeToLog("🔧 [GridSlice] Selection offset for loops: " + juce::String(selectionOffset, 4) + "s");

        for (size_t i = 0; i < pendingBeatGrid_->detectedLoops.size(); ++i) {
            if (stopping_) return;

            const auto& loop = pendingBeatGrid_->detectedLoops[i];

            // CRITICAL FIX: Convert ABSOLUTE loop times to RELATIVE (cropped file) coordinates
            const double relativeStartTime = loop.startTimeSeconds - selectionOffset;
            const double relativeEndTime = loop.endTimeSeconds - selectionOffset;

            // Convert RELATIVE time to samples (in the cropped file)
            const int64_t startSample = static_cast<int64_t>(relativeStartTime * sampleRate);
            const int64_t endSample = static_cast<int64_t>(relativeEndTime * sampleRate);
            const int loopLength = static_cast<int>(std::min(endSample, totalSamples) - startSample);

            if (loopLength <= 0 || startSample < 0 || startSample >= totalSamples) {
                juce::Logger::writeToLog("⚠️ [GridSlice] Skipping loop " + juce::String(i+1) +
                    " - invalid bounds (absolute=" + juce::String(loop.startTimeSeconds, 3) +
                    "s, relative=" + juce::String(relativeStartTime, 3) + "s, startSample=" +
                    juce::String(startSample) + ")");
                continue;
            }

            juce::Logger::writeToLog("🔄 [GridSlice] Loop " + juce::String(i+1) + ": ABSOLUTE " +
                juce::String(loop.startTimeSeconds, 3) + "s - " + juce::String(loop.endTimeSeconds, 3) +
                "s → RELATIVE " + juce::String(relativeStartTime, 3) + "s - " +
                juce::String(relativeEndTime, 3) + "s (" + juce::String(loop.lengthBars) + " bars)");

            // Create loop buffer
            juce::AudioBuffer<float> loopBuffer(numChannels, loopLength);
            loopBuffer.clear();
            for (int ch = 0; ch < numChannels; ++ch) {
                loopBuffer.copyFrom(ch, 0, audioBuffer, ch, static_cast<int>(startSample), loopLength);
            }

            // ALL detected loops use LOOP_ prefix = WHITE in browser
            // (Phase 2 slice_ files will get drum analysis colors)
            juce::String loopName = "LOOP_" + juce::String(i + 1).paddedLeft('0', 3) +
                                    "_" + juce::String(loop.lengthBars) + "bar";
            if (bpm > 0) {
                loopName += "_" + juce::String(bpm, 0) + "bpm";
            }
            loopName += ".wav";
            juce::File loopFile = outputDir.getChildFile(loopName);

            std::unique_ptr<juce::FileOutputStream> outStream(loopFile.createOutputStream());
            if (outStream) {
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wavFormat.createWriterFor(outStream.get(), sampleRate,
                                               static_cast<unsigned int>(numChannels), 24, {}, 0));
                if (writer) {
                    outStream.release(); // Writer takes ownership
                    writer->writeFromAudioSampleBuffer(loopBuffer, 0, loopLength);
                    sampleFiles.add(loopFile);
                    loopCount++;
                    // Per-file logging removed for performance - see summary at end
                }
            }

            // Update progress
            int progress = 30 + static_cast<int>(30.0 * (i + 1) / pendingBeatGrid_->detectedLoops.size());
            publishProgress(progress, "Sliced " + juce::String(loopCount) + " loops...");
        }

        juce::Logger::writeToLog("✅ [GridSlice] Phase 1 complete: " + juce::String(loopCount) + " loops sliced");
    }

    //==============================================================================
    // PHASE 2: SLICE GRID-BASED SAMPLES (slice_XXX.wav - if any additional slices)
    // NOTE: Skips slices that overlap with detected loops (to avoid duplicates)
    //==============================================================================
    if (hasSlicePoints) {
        publishProgress(60, "Slicing at grid points...");
        juce::Logger::writeToLog("🔄 [GridSlice] Phase 2: Slicing " +
            juce::String(static_cast<int>(pendingGridSlicePoints_.size())) + " grid points...");

        // Sort slice points to ensure correct order
        std::vector<double> sortedSlicePoints = pendingGridSlicePoints_;
        std::sort(sortedSlicePoints.begin(), sortedSlicePoints.end());

        // Summary logging only (removed verbose per-point diagnostics)
        juce::Logger::writeToLog("🔄 [GridSlice] Slice points: " +
            juce::String(static_cast<int>(sortedSlicePoints.size())) + " points, duration: " +
            juce::String(duration, 2) + "s");

        // Slice audio at each consecutive pair of slice points
        for (size_t i = 0; i < sortedSlicePoints.size(); ++i) {
            if (stopping_) return;

            double startTime = sortedSlicePoints[i];
            double endTime = (i + 1 < sortedSlicePoints.size()) ? sortedSlicePoints[i + 1] : duration;

            // Skip very short slices (< 10ms)
            if ((endTime - startTime) < 0.01) continue;

            // BUG FIX: Skip slices that overlap with detected loops (avoid duplicates)
            // If a slice segment aligns with a loop boundary (within 50ms tolerance), skip it
            // because Phase 1 already created a LOOP_ file for that audio
            // CRITICAL FIX: Loop boundaries are in ABSOLUTE time, but slice points are in RELATIVE time
            // We must convert loop times to RELATIVE before comparing
            bool overlapsLoop = false;
            if (hasDetectedLoops) {
                constexpr double TOLERANCE = 0.05;  // 50ms tolerance
                const double selectionOffset = pendingBeatGrid_->selectionOffset;
                for (const auto& loop : pendingBeatGrid_->detectedLoops) {
                    // Convert loop ABSOLUTE times to RELATIVE for comparison with slice times
                    const double relativeLoopStart = loop.startTimeSeconds - selectionOffset;
                    const double relativeLoopEnd = loop.endTimeSeconds - selectionOffset;
                    // Check if this slice aligns with a loop (same start and end)
                    if (std::abs(startTime - relativeLoopStart) < TOLERANCE &&
                        std::abs(endTime - relativeLoopEnd) < TOLERANCE) {
                        overlapsLoop = true;
                        // Per-skip logging removed - overlapping slices are expected behavior
                        break;
                    }
                }
            }
            if (overlapsLoop) continue;

            // Convert to samples
            const int64_t startSample = static_cast<int64_t>(startTime * sampleRate);
            const int64_t endSample = static_cast<int64_t>(endTime * sampleRate);
            const int sliceLength = static_cast<int>(std::min(endSample, totalSamples) - startSample);

            if (sliceLength <= 0 || startSample >= totalSamples) {
                juce::Logger::writeToLog("⚠️ [GridSlice] Skipping slice " + juce::String(i+1) +
                    " - invalid bounds");
                continue;
            }

            // Create slice buffer
            juce::AudioBuffer<float> sliceBuffer(numChannels, sliceLength);
            sliceBuffer.clear();
            for (int ch = 0; ch < numChannels; ++ch) {
                sliceBuffer.copyFrom(ch, 0, audioBuffer, ch, static_cast<int>(startSample), sliceLength);
            }

            // Create filename - always slice_ for grid-based slices
            juce::String sliceName = "slice_" + juce::String(i + 1).paddedLeft('0', 3);
            if (bpm > 0) {
                sliceName += "_" + juce::String(bpm, 0) + "bpm";
            }
            sliceName += ".wav";
            juce::File sliceFile = outputDir.getChildFile(sliceName);

            // Write slice file
            std::unique_ptr<juce::FileOutputStream> outStream(sliceFile.createOutputStream());
            if (outStream) {
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wavFormat.createWriterFor(outStream.get(), sampleRate,
                                              static_cast<unsigned int>(numChannels), 24, {}, 0));
                if (writer) {
                    outStream.release(); // Writer takes ownership
                    writer->writeFromAudioSampleBuffer(sliceBuffer, 0, sliceLength);
                    sampleFiles.add(sliceFile);
                    sliceCount++;
                    // Per-file logging removed for performance - see summary at end
                }
            }

            // Update progress every 10 slices to reduce overhead
            if (i % 10 == 0 || i == sortedSlicePoints.size() - 1) {
                int progress = 60 + static_cast<int>(30.0 * (i + 1) / sortedSlicePoints.size());
                publishProgress(progress, "Sliced " + juce::String(sliceCount) + " samples...");
            }
        }

        juce::Logger::writeToLog("✅ [GridSlice] Phase 2 complete: " + juce::String(sliceCount) + " slices");
    }

    publishProgress(95, "Finalizing...");

    juce::Logger::writeToLog("✅ [GridSlice] TOTAL: " + juce::String(loopCount) + " loops + " +
        juce::String(sliceCount) + " slices = " + juce::String(sampleFiles.size()) + " files");

    // Build result
    juce::Array<juce::var> arr;
    for (auto& f : sampleFiles) arr.add(f.getFullPathName());

    juce::DynamicObject::Ptr r = new juce::DynamicObject();
    r->setProperty("outputs", juce::var(arr));
    r->setProperty("outputDir", outputDir.getFullPathName());
    r->setProperty("bpm", bpm);
    r->setProperty("totalLoops", loopCount);
    r->setProperty("totalSlices", sliceCount);
    r->setProperty("mode", "GridSlice");

    if (!stopping_) {
        publishProgress(100, "Done");
        bus_.publish(kEvtResult, juce::var(r.get()));
    }

    juce::Logger::writeToLog("✅ [ProcessingCoordinator] GridSlice pipeline complete: " +
                            juce::String(sampleFiles.size()) + " samples");

    // Clear pending slice points after processing
    pendingGridSlicePoints_.clear();
}

} // namespace samplar
