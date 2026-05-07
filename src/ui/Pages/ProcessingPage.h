#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "../Components/LoadingAnimation.h"
#include "../Components/GifAnimatedBackground.h"
#include "../../services/BeatAnalysisTypes.h"
#include <atomic>

namespace samplar {

/**
 * ProcessingPage - Shows loading animation during sample processing
 *
 * Simple black screen with centered GIF animation and progress percentage.
 */
class ProcessingPage : public juce::Component, public juce::ListBoxModel, private juce::Timer {
public:
    ProcessingPage();
    ~ProcessingPage() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    // Navigation
    std::function<void()> onBack;

    // ListBoxModel (required interface - minimal implementation)
    int getNumRows() override { return 0; }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    void listBoxItemClicked(int, const juce::MouseEvent&) override {}

    // API
    void setInputFile(const juce::File& f) { inputFile_ = f; repaint(); }
    void setProcessMode(int mode) { processMode_ = mode; }
    void resetForNewJob();
    void setProgress(double pct);
    void setStatusMessage(const juce::String& msg) { statusMessage_ = msg; repaint(); }

    // Batch processing API
    void setBatchInfo(int currentFile, int totalFiles) {
        batchCurrentFile_ = currentFile;
        batchTotalFiles_ = totalFiles;
        repaint();
    }
    bool isBatchMode() const { return batchTotalFiles_ > 1; }

    // Legacy APIs - kept for backward compatibility (no-op implementations)
    void addSlicePosition(int) {}
    void markSliceSupervised(int) {}
    void appendFluxChunk(int, const std::vector<float>&) {}
    void setFluxStats(float, float, float) {}
    void clearVisualizationForNewPhase() {}
    void setPhaseProgress(const juce::String&, float) {}
    void setBPM(float) {}
    void setAnalysisPosition(double) {}
    void enableZoomWindow(bool) {}
    void startFullSongAnimation() {}
    void stopFullSongAnimation() {}
    void updateAnimationProgress(float) {}
    void startDrumStemVisualization() {}
    void enqueueFakeJob() {}
    void toggleAnimation() {}

    // Beat grid info from WavePreviewPage
    void setBeatGridInfo(const BeatGridInfo& beatGrid) {
        if (isResetting_.load() || isNavigatingAway_.load()) return;
        beatGridInfo_ = beatGrid;
    }
    const BeatGridInfo& getBeatGridInfo() const { return beatGridInfo_; }

    // User-defined slice points from WavePreviewPage Step 3
    void setGridSlicePoints(const std::vector<double>& slicePointsInSeconds) {
        if (isResetting_.load() || isNavigatingAway_.load()) return;
        gridSlicePoints_ = slicePointsInSeconds;
    }
    const std::vector<double>& getGridSlicePoints() const { return gridSlicePoints_; }
    bool hasGridSlicePoints() const { return !gridSlicePoints_.empty(); }

    // Back button
    juce::TextButton backButton_ { "HOME" };

    // Preload resources (call on app startup to avoid lag when navigating to this page)
    void preloadResources();

private:
    // Processing state
    double progressValue_ { 0.0 };
    juce::File inputFile_;
    int processMode_ { 1 };
    juce::String statusMessage_ { "Initializing..." };
    std::atomic<bool> isResetting_ { false };
    std::atomic<bool> isNavigatingAway_ { false };

    // Batch processing state
    int batchCurrentFile_ { 1 };
    int batchTotalFiles_ { 1 };

    // Beat grid info received from WavePreviewPage
    BeatGridInfo beatGridInfo_;

    // User-defined grid slice points (in seconds)
    std::vector<double> gridSlicePoints_;

    // Loading animation component
    std::unique_ptr<LoadingAnimation> loadingAnimation_;

    // Animated GIF background
    std::unique_ptr<GifAnimatedBackground> gifBackground_;
    bool gifLoaded_ { false };  // Lazy loading flag - GIFs are 47-53MB

    // Stone tile border - array of extracted tiles from tileset for variety
    std::vector<juce::Image> borderTiles_;  // Multiple tiles for visual variety
    juce::Rectangle<int> viewportBounds_;   // The inner viewport area (GIF + monster)
    static constexpr int TILE_SIZE = 32;    // Each tile is 32x32px
    static constexpr int BORDER_TILES = 1;  // Number of tile rows/columns for border
    static constexpr int TILESET_GRID = 5;  // Tileset is 5x5 grid

    void drawStoneBorder(juce::Graphics& g, juce::Rectangle<int> outerBounds);
    void extractBorderTiles(const juce::Image& fullTileset);
    void timerCallback() override;
    void loadBackdrop();

    // Animated tiled backdrop (same as WavePreviewPage)
    juce::Image backdrop_;
    juce::Image backdropScaled_;
    float bgOffsetX_ { 0.0f };
    float bgOffsetY_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessingPage)
};

} // namespace samplar
