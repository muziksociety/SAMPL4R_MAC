#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include "FullSongService.h"
#include "analysis/TempoAnalyzer.h"
#include "analysis/LoopDivider.h"
#include "analysis/BeatAlignedSlicer.h"
#include "BeatAnalysisTypes.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <optional>

namespace samplar {

// Processing Modes:
//   1 = DrumStem (legacy onset slicing)
//   2 = FullSong (demucs separation + slicing)
//   3 = TempoFirst (tempo detection -> loop division -> beat-aligned slicing)
class ProcessingCoordinatorService : public IService {
public:
    explicit ProcessingCoordinatorService(EventBus& bus) : bus_(bus) {}
    ~ProcessingCoordinatorService() override = default;

    void initialise() override;
    void shutdown() override;

    // API to enqueue processing jobs directly (until EventBus producers are wired)
    void enqueue(const juce::File& input, int mode);

    // Overload with beat grid info from WavePreviewPage (source of truth for slicing)
    void enqueue(const juce::File& input, int mode, const BeatGridInfo& beatGrid);

    // Overload with grid slice points from WavePreviewPage Step 3 (for GridSlice mode)
    // Mode 4 = GridSlice: directly use user-defined slice points instead of auto-detection
    void enqueue(const juce::File& input, int mode, const BeatGridInfo& beatGrid,
                 const std::vector<double>& gridSlicePoints);

private:
    // Beat grid info passed from WavePreviewPage (if available)
    std::optional<BeatGridInfo> pendingBeatGrid_;
    // User-defined grid slice points from WavePreviewPage Step 3 (in seconds)
    std::vector<double> pendingGridSlicePoints_;
    EventBus& bus_;
    EventBus::HandlerId subId_ { 0 };

    // Service instances
    std::unique_ptr<FullSongService> fullSongService_;

    // Tempo-First Pipeline components
    TempoAnalyzer tempoAnalyzer_;
    LoopDivider loopDivider_;
    BeatAlignedSlicer beatAlignedSlicer_;
    juce::AudioFormatManager formatManager_;

    // Thread management for safe shutdown
    std::vector<std::thread> activeThreads_;
    std::mutex threadsMutex_;
    std::atomic<bool> stopping_ { false };
    std::atomic<bool> isProcessing_ { false };  // Prevent concurrent jobs

    void cleanupFinishedThreads();
    void handleEnqueue(const juce::var& payload);
    void publishProgress(int pct, const juce::String& msg);
    void publishResult(const juce::Array<juce::File>& outputs);
    void publishError(const juce::String& code, const juce::String& msg);

    // Tempo-First Pipeline processing
    void processTempoFirst(const juce::File& input);

    // GridSlice Pipeline processing (user-defined slice points from WavePreviewPage Step 3)
    void processGridSlice(const juce::File& input);
};

} // namespace samplar

