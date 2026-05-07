#include "ProcessingPage.h"
#include "../../utils/Tracing.h"
#include <BinaryData.h>
#include "../../utils/DataPaths.h"
#include "../../app/ServiceLocator.h"
#include "../../app/ServiceRegistry.h"
// SAMPLAR: removed - MentorService deleted

using namespace juce;

namespace samplar {

// Design constants
namespace {
    constexpr int PROGRESS_BAR_HEIGHT = 24;
    constexpr int STATUS_HEIGHT = 30;
    constexpr int PROGRESS_BAR_WIDTH = 400;

    // Colors
    const Colour BG_COLOR(0xFF000000);            // Pure black
    const Colour ACCENT_COLOR(0xFF00d4aa);        // Cyan/teal accent
    const Colour PROGRESS_BG(0xFF2a2a2a);         // Progress bar background
    const Colour PROGRESS_FILL_START(0xFF00d4aa); // Cyan start
    const Colour PROGRESS_FILL_END(0xFF00ff88);   // Green end
    const Colour TEXT_DIM(0xFFaaaaaa);            // Dimmed text
    const Colour TEXT_BRIGHT(0xFFffffff);         // Bright text
}

ProcessingPage::ProcessingPage()
{
    SAMPLAR_TRACE_SCOPE("page-ctor", "ProcessingPage");
    juce::Logger::writeToLog("[ProcessingPage] Creating enhanced processing page");

    // ── Async resource loading — prior code did ~1 second of disk I/O + image
    // decode on the message thread (loadBackdrop + 22 MB TILESET.png), blocking
    // every editor open. Now we fire a detached thread that does all decode,
    // and on completion posts results back via MessageManager::callAsync.
    // paint() already guards on backdrop_.isValid() / borderTiles_.empty(), so
    // the brief uncoloured first frames are visually acceptable.
    juce::Component::SafePointer<ProcessingPage> safeThis(this);
    std::thread([safeThis]()
    {
        SAMPLAR_TRACE_SCOPE("page-ctor", "ProcessingPage::asyncBackdropLoad");

        // Find TILESET.png across the three possible install locations.
        juce::File tilesetFile;
        auto appDir = samplar::DataPaths::getAppDirectory();
        auto candidate = appDir.getChildFile("BACKGROUNDS").getChildFile("TILESET.png");
        if (candidate.existsAsFile())
            tilesetFile = candidate;

        if (!tilesetFile.existsAsFile())
        {
            auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getParentDirectory();
            for (int i = 0; i < 10 && !tilesetFile.existsAsFile(); ++i)
            {
                auto f = dir.getChildFile("BACKGROUNDS").getChildFile("TILESET.png");
                if (f.existsAsFile()) tilesetFile = f;
                dir = dir.getParentDirectory();
            }
        }

        if (!tilesetFile.existsAsFile())
            tilesetFile = samplar::DataPaths::getUserDataDirectory()
                              .getChildFile("BACKGROUNDS").getChildFile("TILESET.png");

        juce::Image fullTileset;
        if (tilesetFile.existsAsFile())
            fullTileset = juce::ImageFileFormat::loadFrom(tilesetFile);

        // Swap results onto the message thread when ready.
        juce::MessageManager::callAsync([safeThis, fullTileset]()
        {
            if (safeThis == nullptr) return;
            safeThis->loadBackdrop();  // (cheap synchronous call; lives in helper)
            if (fullTileset.isValid())
            {
                juce::Logger::writeToLog("[ProcessingPage] Tileset async-loaded: "
                    + juce::String(fullTileset.getWidth()) + "x"
                    + juce::String(fullTileset.getHeight()));
                safeThis->extractBorderTiles(fullTileset);
            }
            else
            {
                juce::Logger::writeToLog("[ProcessingPage] WARNING: TILESET.png not found — border tiles disabled");
            }
            safeThis->repaint();
        });
    }).detach();

    // Create animated GIF background (added first so it's behind other components)
    gifBackground_ = std::make_unique<GifAnimatedBackground>();
    gifBackground_->setDarkOverlay(0.3f);  // Slight darken for readability
    gifBackground_->setScaleMode(GifAnimatedBackground::ScaleMode::ScaleToFill);
    addAndMakeVisible(gifBackground_.get());

    // Create loading animation component
    loadingAnimation_ = std::make_unique<LoadingAnimation>();
    addAndMakeVisible(loadingAnimation_.get());

    // Back button with custom styling
    addAndMakeVisible(backButton_);
    backButton_.setColour(TextButton::buttonColourId, Colour(0xFF2a2a2a));
    backButton_.setColour(TextButton::textColourOffId, ACCENT_COLOR);
    backButton_.onClick = [this] {
        isNavigatingAway_ = true;

        if (loadingAnimation_)
            loadingAnimation_->stopAnimation();

        if (gifBackground_)
            gifBackground_->stopAnimation();

        stopTimer();

        if (onBack)
            onBack();
    };

    progressValue_ = 0.0;
    statusMessage_ = "Initializing...";

    // Start backdrop animation timer
    startTimerHz(60);
}

void ProcessingPage::extractBorderTiles(const juce::Image& fullTileset)
{
    // Tileset is a 5x5 grid of stone tiles with gaps between them
    int imgW = fullTileset.getWidth();
    int imgH = fullTileset.getHeight();

    int cellW = imgW / TILESET_GRID;
    int cellH = imgH / TILESET_GRID;

    // The gaps are around the edges of each cell (light background between tiles)
    // Calculate offset to extract just the stone part, avoiding the gaps
    int gapX = cellW / 8;   // ~12.5% gap on each side
    int gapY = cellH / 8;
    int tileW = cellW - (gapX * 2);
    int tileH = cellH - (gapY * 2);

    juce::Logger::writeToLog("[ProcessingPage] Extracting " + String(TILESET_GRID * TILESET_GRID) +
                             " tiles, cell: " + String(cellW) + "x" + String(cellH) +
                             ", tile content: " + String(tileW) + "x" + String(tileH));

    borderTiles_.clear();
    borderTiles_.reserve(TILESET_GRID * TILESET_GRID);

    // Corner rounding radius (subtle rounding for stone tiles)
    const float cornerRadius = 3.0f;

    // Extract all 25 tiles from the 5x5 grid
    for (int row = 0; row < TILESET_GRID; ++row)
    {
        for (int col = 0; col < TILESET_GRID; ++col)
        {
            // Calculate position of this cell
            int cellX = col * cellW;
            int cellY = row * cellH;

            // Extract the tile content (avoiding gaps)
            juce::Rectangle<int> tileRect(cellX + gapX, cellY + gapY, tileW, tileH);
            juce::Image extractedTile = fullTileset.getClippedImage(tileRect);

            // Scale to border size
            juce::Image scaledTile = extractedTile.rescaled(TILE_SIZE, TILE_SIZE,
                                                             juce::Graphics::mediumResamplingQuality);

            // Apply rounded corners by drawing into a new ARGB image with clipping
            juce::Image roundedTile(juce::Image::ARGB, TILE_SIZE, TILE_SIZE, true);
            {
                juce::Graphics tileG(roundedTile);

                // Create rounded rectangle clip path
                juce::Path clipPath;
                clipPath.addRoundedRectangle(0.0f, 0.0f, (float)TILE_SIZE, (float)TILE_SIZE, cornerRadius);
                tileG.reduceClipRegion(clipPath);

                // Draw the scaled tile into the clipped area
                tileG.drawImageAt(scaledTile, 0, 0);
            }

            borderTiles_.push_back(roundedTile);
        }
    }

    juce::Logger::writeToLog("[ProcessingPage] Extracted " + String(borderTiles_.size()) + " border tiles with rounded corners");
}

void ProcessingPage::preloadResources()
{
    // Preload GIF background to avoid lag when navigating to this page
    if (!gifLoaded_ && gifBackground_)
    {
        juce::Logger::writeToLog("[ProcessingPage] Preloading GIF background...");
        gifBackground_->loadRandomBackground();
        gifLoaded_ = true;
        juce::Logger::writeToLog("[ProcessingPage] GIF background preloaded");
    }
}

ProcessingPage::~ProcessingPage()
{
    if (loadingAnimation_)
        loadingAnimation_->stopAnimation();
    if (gifBackground_)
        gifBackground_->stopAnimation();
}

void ProcessingPage::paint(Graphics& g)
{
    SAMPLAR_TRACE_SCOPE("paint", "ProcessingPage::paint");
    // Animated tiled backdrop - solid tiles without transparency overlay
    if (backdropScaled_.isValid())
    {
        g.setTiledImageFill(backdropScaled_, (int)bgOffsetX_, (int)bgOffsetY_, 1.0f);
        g.fillAll();
        // No gradient overlay - tiles are drawn solid
    }
    else
    {
        // Fallback to solid black
        g.fillAll(BG_COLOR);
    }
}

void ProcessingPage::drawStoneBorder(Graphics& g, Rectangle<int> outerBounds)
{
    const int borderThickness = TILE_SIZE * BORDER_TILES;
    Rectangle<int> innerBounds = outerBounds.reduced(borderThickness);

    // Use extracted tiles if available
    if (!borderTiles_.empty())
    {
        int numTiles = static_cast<int>(borderTiles_.size());
        int tileIndex = 0;

        // Draw top border (left to right)
        for (int x = outerBounds.getX(); x < outerBounds.getRight(); x += TILE_SIZE)
        {
            int drawW = jmin(TILE_SIZE, outerBounds.getRight() - x);
            const auto& tile = borderTiles_[tileIndex % numTiles];
            g.drawImage(tile, x, outerBounds.getY(), drawW, TILE_SIZE,
                        0, 0, drawW, TILE_SIZE);
            tileIndex++;
        }

        // Draw bottom border (left to right)
        for (int x = outerBounds.getX(); x < outerBounds.getRight(); x += TILE_SIZE)
        {
            int drawW = jmin(TILE_SIZE, outerBounds.getRight() - x);
            const auto& tile = borderTiles_[tileIndex % numTiles];
            g.drawImage(tile, x, innerBounds.getBottom(), drawW, TILE_SIZE,
                        0, 0, drawW, TILE_SIZE);
            tileIndex++;
        }

        // Draw left border (top to bottom, between corners)
        for (int y = innerBounds.getY(); y < innerBounds.getBottom(); y += TILE_SIZE)
        {
            int drawH = jmin(TILE_SIZE, innerBounds.getBottom() - y);
            const auto& tile = borderTiles_[tileIndex % numTiles];
            g.drawImage(tile, outerBounds.getX(), y, TILE_SIZE, drawH,
                        0, 0, TILE_SIZE, drawH);
            tileIndex++;
        }

        // Draw right border (top to bottom, between corners)
        for (int y = innerBounds.getY(); y < innerBounds.getBottom(); y += TILE_SIZE)
        {
            int drawH = jmin(TILE_SIZE, innerBounds.getBottom() - y);
            const auto& tile = borderTiles_[tileIndex % numTiles];
            g.drawImage(tile, innerBounds.getRight(), y, TILE_SIZE, drawH,
                        0, 0, TILE_SIZE, drawH);
            tileIndex++;
        }
    }
    else
    {
        // Fallback: Simple pixel-art style border
        // Outer dark edge
        g.setColour(Colour(0xFF1a1a2e));
        g.drawRect(outerBounds, 2);

        // Mid tone
        g.setColour(Colour(0xFF2d2d44));
        g.drawRect(outerBounds.reduced(2), 2);

        // Inner highlight
        g.setColour(Colour(0xFF3d3d5c));
        g.drawRect(outerBounds.reduced(4), 2);

        // Inner dark edge
        g.setColour(Colour(0xFF0f0f1a));
        g.drawRect(innerBounds.expanded(2), 2);
    }
}

void ProcessingPage::paintOverChildren(Graphics& g)
{
    auto bounds = getLocalBounds();
    int contentWidth = bounds.getWidth();
    int centerX = bounds.getCentreX();

    // === TITLE AREA ===
    int titleY = 25;

    // Draw "PROCESSING" title
    g.setColour(TEXT_BRIGHT);
    g.setFont(Font(28.0f, Font::bold));
    g.drawText("P R O C E S S I N G", bounds.getX(), titleY, contentWidth, 36, Justification::centred);

    // Draw underline
    int underlineWidth = 200;
    int underlineY = titleY + 38;
    g.setColour(ACCENT_COLOR.withAlpha(0.6f));
    g.drawLine(static_cast<float>(centerX - underlineWidth / 2), static_cast<float>(underlineY),
               static_cast<float>(centerX + underlineWidth / 2), static_cast<float>(underlineY), 2.0f);

    // === STONE BORDER around viewport ===
    // Calculate outer bounds (viewport + border)
    int borderThickness = TILE_SIZE * BORDER_TILES;
    Rectangle<int> outerBounds = viewportBounds_.expanded(borderThickness);
    drawStoneBorder(g, outerBounds);

    // === BATCH PROGRESS INDICATOR (if in batch mode) ===
    int filenameY = outerBounds.getBottom() + 15;

    if (batchTotalFiles_ > 1)
    {
        // Show "PROCESSING FILE X OF Y" header
        // Mode 2 = FullSong (separation), Mode 3/4 = Slicing
        String action = (processMode_ == 2) ? "SEPARATING" : "SLICING";
        String batchText = action + " FILE " + String(batchCurrentFile_) + " OF " + String(batchTotalFiles_);
        g.setColour(Colour(0xFF00CCFF));  // Cyan for emphasis
        g.setFont(Font(14.0f, Font::bold));
        g.drawText(batchText, bounds.getX(), filenameY - 22, contentWidth, 20, Justification::centred);
    }

    // === FILE NAME DISPLAY ===
    if (inputFile_.existsAsFile())
    {
        // Draw filename in a subtle box
        String filename = inputFile_.getFileName();
        g.setFont(Font(16.0f));
        int textWidth = g.getCurrentFont().getStringWidth(filename) + 40;
        int boxWidth = jmin(textWidth, 400);
        Rectangle<int> filenameBox(centerX - boxWidth / 2, filenameY, boxWidth, 32);

        // Box background
        g.setColour(Colour(0xFF252525));
        g.fillRoundedRectangle(filenameBox.toFloat(), 4.0f);

        // Box border
        g.setColour(ACCENT_COLOR.withAlpha(0.3f));
        g.drawRoundedRectangle(filenameBox.toFloat(), 4.0f, 1.0f);

        // Filename text
        g.setColour(TEXT_BRIGHT);
        g.drawText(filename, filenameBox, Justification::centred);
    }

    // === PROGRESS BAR ===
    int progressY = filenameY + 45;
    Rectangle<int> progressBounds(centerX - PROGRESS_BAR_WIDTH / 2, progressY, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT);

    // Progress bar background
    g.setColour(PROGRESS_BG);
    g.fillRoundedRectangle(progressBounds.toFloat(), 6.0f);

    // Progress bar fill with gradient
    if (progressValue_ > 0.0)
    {
        int fillWidth = static_cast<int>(progressBounds.getWidth() * progressValue_);
        Rectangle<int> fillBounds(progressBounds.getX(), progressBounds.getY(), fillWidth, PROGRESS_BAR_HEIGHT);

        ColourGradient progressGradient(PROGRESS_FILL_START, static_cast<float>(fillBounds.getX()), 0,
                                        PROGRESS_FILL_END, static_cast<float>(fillBounds.getRight()), 0, false);
        g.setGradientFill(progressGradient);
        g.fillRoundedRectangle(fillBounds.toFloat(), 6.0f);
    }

    // Progress bar border
    g.setColour(ACCENT_COLOR.withAlpha(0.4f));
    g.drawRoundedRectangle(progressBounds.toFloat(), 6.0f, 1.0f);

    // Progress percentage text
    g.setColour(TEXT_BRIGHT);
    g.setFont(Font(18.0f, Font::bold));
    String progressText = String(progressValue_ * 100.0, 0) + "%";
    g.drawText(progressText, progressBounds.getRight() + 15, progressY, 60, PROGRESS_BAR_HEIGHT, Justification::centredLeft);

    // === STATUS MESSAGE ===
    int statusY = progressY + 35;
    g.setColour(TEXT_DIM);
    g.setFont(Font(15.0f));
    g.drawText(statusMessage_, bounds.getX(), statusY, contentWidth, STATUS_HEIGHT, Justification::centred);

    // === TIME WARNING (for Full Song mode / batch) ===
    if (processMode_ == 2 || batchTotalFiles_ > 1)  // Full Song mode or batch
    {
        int warningY = statusY + 25;
        g.setColour(Colour(0xFFFF9900).withAlpha(0.8f));  // Orange warning
        g.setFont(Font(13.0f, Font::italic));

        String warningText;
        if (processMode_ == 2)
        {
            // Full Song separation mode
            if (batchTotalFiles_ > 1)
                warningText = "AI drum separation in progress... This may take 5-15 minutes per song.";
            else
                warningText = "AI drum separation in progress... This may take several minutes.";
        }
        else if (batchTotalFiles_ > 1)
        {
            // Batch slicing mode (Mode 3/4)
            warningText = "Processing " + String(batchTotalFiles_) + " songs... Please wait.";
        }
        g.drawText(warningText, bounds.getX(), warningY, contentWidth, 20, Justification::centred);
    }
}

void ProcessingPage::resized()
{
    SAMPLAR_TRACE_SCOPE("paint", "ProcessingPage::resized");
    auto bounds = getLocalBounds();
    int centerX = bounds.getCentreX();
    int centerY = bounds.getCentreY();

    // Calculate viewport bounds (contained area for GIF + monster)
    // Viewport is ~65% width, ~45% height, positioned above center
    int viewportWidth = static_cast<int>(bounds.getWidth() * 0.65);
    int viewportHeight = static_cast<int>(bounds.getHeight() * 0.45);

    // Ensure minimum size
    viewportWidth = jmax(viewportWidth, 400);
    viewportHeight = jmax(viewportHeight, 250);

    // Position viewport below title (moved down 60px to avoid overlap with PROCESSING text)
    int viewportY = 140; // Below title with clearance
    viewportBounds_ = Rectangle<int>(
        centerX - viewportWidth / 2,
        viewportY,
        viewportWidth,
        viewportHeight
    );

    // GIF background fills the viewport area (inside the border)
    if (gifBackground_)
    {
        gifBackground_->setBounds(viewportBounds_);
    }

    // Monster animation centered inside viewport - keep it small!
    // Frame aspect ratio is 400:172 ≈ 2.33:1
    // Target: ~100-140px wide, proportionally tall
    int maxAnimWidth = jmin(140, viewportWidth / 4);  // Max 140px or 25% of viewport
    int animWidth = jmax(80, maxAnimWidth);           // Min 80px
    int animHeight = static_cast<int>(animWidth / 2.33f);  // Maintain aspect ratio

    if (loadingAnimation_)
    {
        loadingAnimation_->setBounds(
            viewportBounds_.getCentreX() - animWidth / 2,
            viewportBounds_.getCentreY() - animHeight / 2,
            animWidth,
            animHeight
        );
        juce::Logger::writeToLog("[ProcessingPage] Monster size: " +
            juce::String(animWidth) + "x" + juce::String(animHeight));
    }

    // Back button bottom-left corner
    backButton_.setBounds(20, bounds.getHeight() - 45, 100, 32);

    juce::Logger::writeToLog("[ProcessingPage] Resized - viewport: " + viewportBounds_.toString());
}

void ProcessingPage::visibilityChanged()
{
    Component::visibilityChanged();

    if (isVisible())
    {
        // Lazy load the GIF background on first visibility
        if (!gifLoaded_ && gifBackground_)
        {
            juce::Logger::writeToLog("[ProcessingPage] First visibility - loading GIF background...");
            gifBackground_->loadRandomBackground();
            gifLoaded_ = true;
            juce::Logger::writeToLog("[ProcessingPage] GIF background loaded");
        }

        // Start animations when page becomes visible
        if (gifBackground_)
        {
            gifBackground_->startAnimation();
            juce::Logger::writeToLog("[ProcessingPage] Page visible - starting GIF background");
        }
        if (loadingAnimation_)
        {
            loadingAnimation_->startAnimation();
            juce::Logger::writeToLog("[ProcessingPage] Page visible - starting loading animation");
        }

        // Start backdrop animation timer
        startTimerHz(60);
    }
    else
    {
        // Stop animations when page is hidden
        if (gifBackground_)
        {
            gifBackground_->stopAnimation();
            juce::Logger::writeToLog("[ProcessingPage] Page hidden - stopping GIF background");
        }
        if (loadingAnimation_)
        {
            loadingAnimation_->stopAnimation();
            juce::Logger::writeToLog("[ProcessingPage] Page hidden - stopping loading animation");
        }

        // Stop backdrop animation timer
        stopTimer();
    }
}

void ProcessingPage::resetForNewJob()
{
    isResetting_ = true;
    isNavigatingAway_ = false;

    progressValue_ = 0.0;
    statusMessage_ = "Initializing...";
    batchCurrentFile_ = 1;
    batchTotalFiles_ = 1;

    if (loadingAnimation_)
        loadingAnimation_->setProgress(0.0f);

    isResetting_ = false;
    repaint();

    juce::Logger::writeToLog("[ProcessingPage] Reset for new job");
}

void ProcessingPage::setProgress(double pct)
{
    if (isResetting_.load() || isNavigatingAway_.load())
        return;

    progressValue_ = juce::jlimit(0.0, 100.0, pct) / 100.0;

    if (loadingAnimation_)
        loadingAnimation_->setProgress(static_cast<float>(progressValue_));

    repaint();
}

void ProcessingPage::loadBackdrop()
{
    // Load from BinaryData (same backdrop as WavePreviewPage)
    if (BinaryData::backdrop_pngSize > 0)
    {
        backdrop_ = juce::ImageFileFormat::loadFrom(BinaryData::backdrop_png, BinaryData::backdrop_pngSize);
    }

    if (backdrop_.isValid())
    {
        // Scale to 50% to reveal more of the pattern
        int w = juce::jmax(1, backdrop_.getWidth() / 2);
        int h = juce::jmax(1, backdrop_.getHeight() / 2);
        backdropScaled_ = backdrop_.rescaled(w, h, juce::Graphics::highResamplingQuality);
        juce::Logger::writeToLog("[ProcessingPage] Backdrop loaded and scaled to " +
                                 juce::String(w) + "x" + juce::String(h));
    }
    else
    {
        juce::Logger::writeToLog("[ProcessingPage] WARNING: Failed to load backdrop");
    }
}

void ProcessingPage::timerCallback()
{
    // Animate backdrop: drift towards lower-left (same as WavePreviewPage)
    if (backdropScaled_.isValid())
    {
        bgOffsetX_ -= 0.5f;  // left
        bgOffsetY_ += 0.3f;  // down

        // Wrap offsets to avoid overflow
        const int w = juce::jmax(1, backdropScaled_.getWidth());
        const int h = juce::jmax(1, backdropScaled_.getHeight());
        if (bgOffsetX_ <= -w) bgOffsetX_ += w;
        if (bgOffsetX_ >= w)  bgOffsetX_ -= w;
        if (bgOffsetY_ <= -h) bgOffsetY_ += h;
        if (bgOffsetY_ >= h)  bgOffsetY_ -= h;

        repaint();
    }
}

} // namespace samplar
