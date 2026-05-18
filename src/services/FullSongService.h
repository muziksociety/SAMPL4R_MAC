#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>

namespace samplar {

class OnnxSeparationProvider;  // forward decl - definition in separation/

/**
 * FullSongService - Orchestrates full song processing pipeline
 *
 * Workflow:
 * 1. Load full song audio file
 * 2. Separate stems using native C++/ONNX (htdemucs.onnx)
 * 3. Return separated stems
 *
 * Backend: native C++/ONNX (OnnxSeparationProvider). 4-stem only
 * (vocals / drums / bass / other). The legacy UVR / audio-separator Python
 * subprocess path was removed 2026-05-18 along with the embedded Python.
 */
class FullSongService : public IService {
public:
    struct Config {
        // Model file (resolved via DataPaths::getModelsDirectory()).
        // smank/htdemucs-onnx - Demucs v4, STFT baked into the graph.
        juce::String onnxModelFileName = "htdemucs.onnx";

        // Only write the drums stem (htdemucs computes all 4 - this just
        // skips writing the other 3 WAVs to disk). Set false for S-Mode.
        bool drumsOnly = true;

        // Progress reporting
        std::function<void(int, const juce::String&)> onProgress;
    };

    struct Result {
        juce::String error;
        juce::File outputDir;
        juce::Array<juce::File> sampleFiles;
        juce::File drumStemFile;  // The separated drum stem for reference
    };

    explicit FullSongService(EventBus& bus);
    ~FullSongService() override;

    void initialise() override;
    void shutdown() override;

    // Optional cancel hook (long ORT runs can poll this to abort cleanly).
    std::function<bool()> shouldCancel;

    // Main processing function (separation + slicing)
    Result process(const juce::File& input, const Config& cfg, const juce::String& tempPrefix = "fullSong_");

    // Separation-only function - returns drums stem without slicing.
    // Use this to get the drums stem for WavePreviewPage before user edits.
    Result separateDrums(const juce::File& input, const Config& cfg, const juce::String& tempPrefix = "fullSong_");

    // Progress callback for visualization
    std::function<void(const juce::var&)> onVizProgress;

private:
    EventBus& bus_;

    std::unique_ptr<OnnxSeparationProvider> provider_;
    juce::File locateOnnxModel(const juce::String& fileName) const;
    bool ensureOnnxProviderReady(const Config& cfg, juce::String& errorOut);
    Result separateAudioOnnx(const juce::File& input, const Config& cfg, const juce::String& tempPrefix);

    // Drum processing after separation
    Result processDrumStem(const juce::File& drumStemFile, const Config& cfg, const juce::String& tempPrefix);

    // Utility functions
    void publishProgress(int pct, const juce::String& msg, const Config& cfg);
    void publishError(const juce::String& error, const Config& cfg);

    // Audio file I/O
    bool loadAudioFile(const juce::File& file, juce::AudioBuffer<float>& buffer, double& sampleRate);
    bool saveAudioFile(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate);
};

} // namespace samplar
