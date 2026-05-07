#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
// DrumSliceService removed in SAMPLAR fork (stems-to-pads, no drum slicing)
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace samplar {

/**
 * FullSongService - Orchestrates full song processing pipeline
 *
 * Workflow:
 * 1. Load full song audio file
 * 2. Separate drum stem using UVR / audio-separator (single source of truth)
 * 3. Call DrumSliceService on the separated drum track
 * 4. Return final sample outputs
 *
 * Separation backend: UVR / audio-separator only.
 * Supported models: htdemucs_ft.yaml (default, best drum quality) and
 *                   htdemucs_6s.yaml (6-stem variant).
 */
class FullSongService : public IService {
public:
    struct Config {
        // === UVR Backend (audio-separator) ===
        // Model name for audio-separator CLI (auto-downloaded on first use).
        // Supported: htdemucs_ft.yaml (default, 4-stem fine-tuned), htdemucs_6s.yaml (6-stem).
        juce::String uvrModelName = "htdemucs_ft.yaml";

        // Output format for separated stems
        juce::String uvrOutputFormat = "WAV";

        // Extract only drums stem (faster, skips other stems)
        bool uvrDrumsOnly = true;

        // (DrumSliceService::Config removed in SAMPLAR fork - no slicing)

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
    ~FullSongService() override = default;

    void initialise() override;
    void shutdown() override;

    // Main processing function (separation + slicing)
    Result process(const juce::File& input, const Config& cfg, const juce::String& tempPrefix = "fullSong_");

    // Separation-only function - returns drums stem without slicing.
    // Use this to get the drums stem for WavePreviewPage before user edits.
    Result separateDrums(const juce::File& input, const Config& cfg, const juce::String& tempPrefix = "fullSong_");

    // Progress callback for visualization
    std::function<void(const juce::var&)> onVizProgress;

private:
    EventBus& bus_;

    // Audio separation via UVR / audio-separator CLI (the only backend)
    Result separateAudioUVR(const juce::File& input, const Config& cfg, const juce::String& tempPrefix);

    // Check if UVR / audio-separator is available (portable or PATH)
    bool isUvrAvailable();

    // Get path to audio-separator (checks portable location first, then PATH)
    juce::String getAudioSeparatorPath();

    // Set up Python environment for portable mode (PYTHONHOME, PATH)
    void setupPortablePythonEnv();

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
