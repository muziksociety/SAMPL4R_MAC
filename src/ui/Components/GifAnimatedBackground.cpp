#include "GifAnimatedBackground.h"
#include "../../utils/DataPaths.h"
#include <random>
#include <chrono>
#include <thread>

namespace samplar {

//==============================================================================
// Simple GIF Decoder
//==============================================================================

namespace {

struct GifDecoder {
    const uint8_t* data;
    int dataSize;
    int pos = 0;

    uint8_t readByte() {
        if (pos >= dataSize) return 0;
        return data[pos++];
    }

    uint16_t readWord() {
        uint8_t lo = readByte();
        uint8_t hi = readByte();
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    void skip(int bytes) {
        pos += bytes;
        if (pos > dataSize) pos = dataSize;
    }

    bool isValid() const { return pos < dataSize; }
};

// LZW decompression for GIF
class LzwDecoder {
public:
    LzwDecoder(int minCodeSize) : minCodeSize_(minCodeSize) {
        clearCode_ = 1 << minCodeSize_;
        endCode_ = clearCode_ + 1;
        reset();
    }

    void reset() {
        codeSize_ = minCodeSize_ + 1;
        nextCode_ = endCode_ + 1;
        table_.clear();
        table_.resize(4096);
        for (int i = 0; i < clearCode_; ++i) {
            table_[i] = { static_cast<uint8_t>(i) };
        }
    }

    std::vector<uint8_t> decode(const std::vector<uint8_t>& compressed) {
        std::vector<uint8_t> output;
        bitBuffer_ = 0;
        bitsInBuffer_ = 0;
        dataPos_ = 0;
        compressedData_ = &compressed;

        int prevCode = -1;
        while (true) {
            int code = readCode();
            if (code == -1 || code == endCode_) break;

            if (code == clearCode_) {
                reset();
                prevCode = -1;
                continue;
            }

            std::vector<uint8_t> entry;
            if (code < nextCode_) {
                entry = table_[code];
            } else if (code == nextCode_ && prevCode >= 0) {
                entry = table_[prevCode];
                entry.push_back(entry[0]);
            } else {
                break;
            }

            output.insert(output.end(), entry.begin(), entry.end());

            if (prevCode >= 0 && nextCode_ < 4096) {
                std::vector<uint8_t> newEntry = table_[prevCode];
                newEntry.push_back(entry[0]);
                table_[nextCode_++] = newEntry;

                if (nextCode_ >= (1 << codeSize_) && codeSize_ < 12) {
                    codeSize_++;
                }
            }

            prevCode = code;
        }

        return output;
    }

private:
    int readCode() {
        while (bitsInBuffer_ < codeSize_) {
            if (dataPos_ >= compressedData_->size()) return -1;
            bitBuffer_ |= static_cast<uint32_t>((*compressedData_)[dataPos_++]) << bitsInBuffer_;
            bitsInBuffer_ += 8;
        }
        int code = bitBuffer_ & ((1 << codeSize_) - 1);
        bitBuffer_ >>= codeSize_;
        bitsInBuffer_ -= codeSize_;
        return code;
    }

    int minCodeSize_;
    int codeSize_;
    int clearCode_;
    int endCode_;
    int nextCode_;
    std::vector<std::vector<uint8_t>> table_;
    uint32_t bitBuffer_ = 0;
    int bitsInBuffer_ = 0;
    size_t dataPos_ = 0;
    const std::vector<uint8_t>* compressedData_ = nullptr;
};

} // anonymous namespace

//==============================================================================
// GifAnimatedBackground Implementation
//==============================================================================

GifAnimatedBackground::GifAnimatedBackground()
{
    juce::Logger::writeToLog("[GifBackground] Created");
}

GifAnimatedBackground::~GifAnimatedBackground()
{
    stopAnimation();
}

bool GifAnimatedBackground::loadFromBinaryData(const char* data, int dataSize)
{
    if (!data || dataSize < 13) {
        juce::Logger::writeToLog("[GifBackground] Invalid data");
        return false;
    }

    frames_.clear();
    currentFrame_ = 0;

    bool success = decodeGif(data, dataSize);

    if (success && !frames_.empty()) {
        juce::Logger::writeToLog("[GifBackground] Loaded " + juce::String(frames_.size()) + " frames");
        startAnimation();
        return true;
    }

    juce::Logger::writeToLog("[GifBackground] Failed to decode GIF");
    return false;
}

bool GifAnimatedBackground::loadPreset(BackgroundType type)
{
    // GIFs are loaded from the BACKGROUNDS folder on the filesystem.
    // Embedding them in BinaryData produces 80 MB+ C++ source files that
    // overflow MSVC's compiler heap (error C1060).
    const juce::String filename = (type == BackgroundType::Campo)
                                  ? "CAMPO_optimized.gif"
                                  : "LAVA_optimized.gif";

    juce::Logger::writeToLog("[GifBackground] Loading " + filename + " from filesystem");

    // Search strategy: app directory → up the tree → AppData
    auto tryLoad = [&](const juce::File& f) -> bool {
        if (!f.existsAsFile()) return false;
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) return false;
        return loadFromBinaryData(static_cast<const char*>(mb.getData()),
                                  static_cast<int>(mb.getSize()));
    };

    // 1. BACKGROUNDS/ next to the executable
    juce::File appDir = samplar::DataPaths::getAppDirectory();
    if (tryLoad(appDir.getChildFile("BACKGROUNDS").getChildFile(filename)))
        return true;

    // 2. Walk up from exe (dev tree: build/ → Sample_monsta5/ → BACKGROUNDS/)
    juce::File dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getParentDirectory();
    for (int i = 0; i < 10; ++i)
    {
        if (tryLoad(dir.getChildFile("BACKGROUNDS").getChildFile(filename)))
            return true;
        dir = dir.getParentDirectory();
    }

    // 3. AppData (for VST3 / installed mode where assets are copied there)
    if (tryLoad(samplar::DataPaths::getUserDataDirectory()
                    .getChildFile("BACKGROUNDS").getChildFile(filename)))
        return true;

    juce::Logger::writeToLog("[GifBackground] WARNING: " + filename + " not found");
    return false;
}

bool GifAnimatedBackground::loadRandomBackground()
{
    // True 50% random chance using random_device for seed
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 1);

    int selection = dis(gen);
    BackgroundType type = (selection == 0) ? BackgroundType::Campo : BackgroundType::Lava;

    juce::Logger::writeToLog("[GifBackground] Random selection: " + juce::String(selection) +
                             " -> " + (type == BackgroundType::Campo ? "CAMPO" : "LAVA"));

    bool success = loadPreset(type);

    juce::Logger::writeToLog("[GifBackground] Load result: " + juce::String(success ? "SUCCESS" : "FAILED") +
                             ", frames loaded: " + juce::String(static_cast<int>(frames_.size())));

    // If selected one failed, try the other
    if (!success) {
        type = (selection == 0) ? BackgroundType::Lava : BackgroundType::Campo;
        juce::Logger::writeToLog("[GifBackground] Falling back to " +
                                 juce::String(type == BackgroundType::Campo ? "CAMPO" : "LAVA"));
        success = loadPreset(type);
    }

    return success;
}

void GifAnimatedBackground::loadRandomBackgroundAsync()
{
    // Re-entry guard: only one async decode at a time. Subsequent calls are no-ops.
    bool expected = false;
    if (!asyncLoadInFlight_.compare_exchange_strong(expected, true))
    {
        juce::Logger::writeToLog("[GifBackground] Async load already in flight, skipping");
        return;
    }

    juce::Logger::writeToLog("[GifBackground] Async load STARTING (background thread)");

    // SafePointer keeps it safe if the component is destroyed mid-decode.
    juce::Component::SafePointer<GifAnimatedBackground> safeThis(this);

    std::thread([safeThis]()
    {
        // Pick CAMPO or LAVA randomly.
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 1);
        int selection = dis(gen);
        BackgroundType type = (selection == 0) ? BackgroundType::Campo : BackgroundType::Lava;

        // Resolve the file path (off-thread; getAppDirectory/getUserDataDirectory are read-only).
        auto resolveFile = [](BackgroundType t) -> juce::File
        {
            const juce::String filename = (t == BackgroundType::Campo)
                                          ? "CAMPO_optimized.gif"
                                          : "LAVA_optimized.gif";

            juce::File appDir = samplar::DataPaths::getAppDirectory();
            juce::File f = appDir.getChildFile("BACKGROUNDS").getChildFile(filename);
            if (f.existsAsFile()) return f;

            juce::File dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                 .getParentDirectory();
            for (int i = 0; i < 10; ++i)
            {
                juce::File c = dir.getChildFile("BACKGROUNDS").getChildFile(filename);
                if (c.existsAsFile()) return c;
                dir = dir.getParentDirectory();
            }

            juce::File user = samplar::DataPaths::getUserDataDirectory()
                                  .getChildFile("BACKGROUNDS").getChildFile(filename);
            if (user.existsAsFile()) return user;

            return {};
        };

        juce::File file = resolveFile(type);
        if (!file.existsAsFile())
        {
            BackgroundType alt = (type == BackgroundType::Campo)
                                 ? BackgroundType::Lava : BackgroundType::Campo;
            file = resolveFile(alt);
            if (file.existsAsFile()) type = alt;
        }

        // Decode into a LOCAL frames vector so we never touch frames_ from this thread.
        std::vector<GifFrame> localFrames;
        if (file.existsAsFile())
        {
            juce::MemoryBlock mb;
            if (file.loadFileAsData(mb))
            {
                // Reuse decodeGif by temporarily decoding into a throwaway instance.
                // Cheaper: replicate the parse loop minimally — but decodeGif writes to
                // frames_, so we use a stack-allocated temporary GifAnimatedBackground.
                // That's heavy on the stack; instead, we decode in-place here via a helper.
                GifAnimatedBackground tmp;
                if (tmp.decodeGif(static_cast<const char*>(mb.getData()),
                                  static_cast<int>(mb.getSize())))
                {
                    localFrames = std::move(tmp.frames_);
                }
            }
        }

        const int frameCount = static_cast<int>(localFrames.size());
        juce::Logger::writeToLog("[GifBackground] Async decode finished: "
                                 + juce::String(frameCount) + " frames"
                                 + " (" + (type == BackgroundType::Campo ? "CAMPO" : "LAVA") + ")");

        // Hand frames back to the message thread. We need to move them into the
        // lambda capture; std::function requires copyable, so wrap in shared_ptr.
        auto framesPtr = std::make_shared<std::vector<GifFrame>>(std::move(localFrames));

        juce::MessageManager::callAsync([safeThis, framesPtr]()
        {
            if (safeThis == nullptr)
            {
                juce::Logger::writeToLog("[GifBackground] Component gone before async swap");
                return;
            }

            safeThis->frames_ = std::move(*framesPtr);
            safeThis->currentFrame_ = 0;
            safeThis->asyncLoadInFlight_.store(false);

            if (!safeThis->frames_.empty())
            {
                juce::Logger::writeToLog("[GifBackground] Async swap done, "
                                         + juce::String((int)safeThis->frames_.size())
                                         + " frames live; starting animation");
                safeThis->startAnimation();
                safeThis->repaint();
            }
            else
            {
                juce::Logger::writeToLog("[GifBackground] Async swap: no frames decoded");
            }
        });
    }).detach();
}

bool GifAnimatedBackground::decodeGif(const char* rawData, int dataSize)
{
    GifDecoder gif;
    gif.data = reinterpret_cast<const uint8_t*>(rawData);
    gif.dataSize = dataSize;

    // Check GIF header
    char header[7] = {0};
    for (int i = 0; i < 6; ++i) header[i] = static_cast<char>(gif.readByte());

    if (std::string(header, 6) != "GIF87a" && std::string(header, 6) != "GIF89a") {
        juce::Logger::writeToLog("[GifBackground] Invalid GIF header: " + juce::String(header));
        return false;
    }

    // Logical screen descriptor
    int screenWidth = gif.readWord();
    int screenHeight = gif.readWord();
    uint8_t packed = gif.readByte();
    gif.readByte(); // Background color index
    gif.readByte(); // Pixel aspect ratio

    bool hasGlobalColorTable = (packed & 0x80) != 0;
    int globalColorTableSize = 1 << ((packed & 0x07) + 1);

    // Read global color table
    std::vector<juce::Colour> globalColors;
    if (hasGlobalColorTable) {
        for (int i = 0; i < globalColorTableSize; ++i) {
            uint8_t r = gif.readByte();
            uint8_t g = gif.readByte();
            uint8_t b = gif.readByte();
            globalColors.push_back(juce::Colour(r, g, b));
        }
    }

    // Create base canvas
    juce::Image canvas(juce::Image::ARGB, screenWidth, screenHeight, true);
    int currentDelay = defaultFrameDelay_;
    int transparentIndex = -1;
    int disposalMethod = 0;

    // Parse blocks
    while (gif.isValid()) {
        uint8_t blockType = gif.readByte();

        if (blockType == 0x21) {
            // Extension block
            uint8_t extType = gif.readByte();

            if (extType == 0xF9) {
                // Graphics Control Extension
                gif.readByte(); // Block size
                uint8_t gcPacked = gif.readByte();
                disposalMethod = (gcPacked >> 2) & 0x07;
                bool hasTransparency = (gcPacked & 0x01) != 0;
                currentDelay = gif.readWord() * 10;
                if (currentDelay < 20) currentDelay = 100;
                transparentIndex = hasTransparency ? gif.readByte() : -1;
                if (!hasTransparency) gif.readByte();
                gif.readByte(); // Block terminator
            } else {
                // Skip other extensions
                while (gif.isValid()) {
                    uint8_t subBlockSize = gif.readByte();
                    if (subBlockSize == 0) break;
                    gif.skip(subBlockSize);
                }
            }
        }
        else if (blockType == 0x2C) {
            // Image descriptor
            int left = gif.readWord();
            int top = gif.readWord();
            int width = gif.readWord();
            int height = gif.readWord();
            uint8_t imgPacked = gif.readByte();

            bool hasLocalColorTable = (imgPacked & 0x80) != 0;
            bool interlaced = (imgPacked & 0x40) != 0;
            int localColorTableSize = hasLocalColorTable ? (1 << ((imgPacked & 0x07) + 1)) : 0;

            // Read local color table
            std::vector<juce::Colour> colors = globalColors;
            if (hasLocalColorTable) {
                colors.clear();
                for (int i = 0; i < localColorTableSize; ++i) {
                    uint8_t r = gif.readByte();
                    uint8_t g = gif.readByte();
                    uint8_t b = gif.readByte();
                    colors.push_back(juce::Colour(r, g, b));
                }
            }

            // Read LZW minimum code size
            int lzwMinCodeSize = gif.readByte();

            // Read compressed data sub-blocks
            std::vector<uint8_t> compressedData;
            while (gif.isValid()) {
                uint8_t subBlockSize = gif.readByte();
                if (subBlockSize == 0) break;
                for (int i = 0; i < subBlockSize && gif.isValid(); ++i) {
                    compressedData.push_back(gif.readByte());
                }
            }

            // Decompress
            LzwDecoder lzw(lzwMinCodeSize);
            std::vector<uint8_t> pixels = lzw.decode(compressedData);

            // Handle disposal method
            if (disposalMethod == 2) {
                juce::Graphics canvasG(canvas);
                canvasG.setColour(juce::Colours::transparentBlack);
                canvasG.fillRect(left, top, width, height);
            }

            // Draw pixels to canvas
            juce::Image::BitmapData bitmapData(canvas, juce::Image::BitmapData::readWrite);

            int pixelIndex = 0;
            for (int y = 0; y < height && pixelIndex < static_cast<int>(pixels.size()); ++y) {
                int actualY = interlaced ? getInterlacedY(y, height) : y;
                for (int x = 0; x < width && pixelIndex < static_cast<int>(pixels.size()); ++x) {
                    uint8_t colorIndex = pixels[pixelIndex++];
                    if (colorIndex != transparentIndex && colorIndex < colors.size()) {
                        int px = left + x;
                        int py = top + actualY;
                        if (px >= 0 && px < screenWidth && py >= 0 && py < screenHeight) {
                            bitmapData.setPixelColour(px, py, colors[colorIndex]);
                        }
                    }
                }
            }

            // Store frame
            GifFrame frame;
            frame.image = canvas.createCopy();
            frame.delayMs = currentDelay;
            frames_.push_back(frame);

            // Reset for next frame
            transparentIndex = -1;
            currentDelay = defaultFrameDelay_;
        }
        else if (blockType == 0x3B) {
            break;
        }
        else {
            juce::Logger::writeToLog("[GifBackground] Unknown block type: 0x" +
                                     juce::String::toHexString(blockType));
            break;
        }
    }

    return !frames_.empty();
}

int GifAnimatedBackground::getInterlacedY(int y, int height)
{
    static const int startY[] = {0, 4, 2, 1};
    static const int stepY[] = {8, 8, 4, 2};

    int row = 0;
    for (int pass = 0; pass < 4; ++pass) {
        for (int i = startY[pass]; i < height; i += stepY[pass]) {
            if (row == y) return i;
            row++;
        }
    }
    return y;
}

void GifAnimatedBackground::paint(juce::Graphics& g)
{
    if (frames_.empty()) {
        g.fillAll(juce::Colour(0xFF1a1a1a));
        return;
    }

    auto bounds = getLocalBounds();
    const auto& frame = frames_[currentFrame_];

    switch (scaleMode_) {
        case ScaleMode::ScaleToFill: {
            float scaleX = static_cast<float>(bounds.getWidth()) / frame.image.getWidth();
            float scaleY = static_cast<float>(bounds.getHeight()) / frame.image.getHeight();
            float scale = std::max(scaleX, scaleY);

            int scaledW = static_cast<int>(frame.image.getWidth() * scale);
            int scaledH = static_cast<int>(frame.image.getHeight() * scale);
            int offsetX = (bounds.getWidth() - scaledW) / 2;
            int offsetY = (bounds.getHeight() - scaledH) / 2;

            g.setOpacity(opacity_);
            g.drawImage(frame.image, offsetX, offsetY, scaledW, scaledH,
                        0, 0, frame.image.getWidth(), frame.image.getHeight());
            break;
        }

        case ScaleMode::ScaleToFit: {
            float scaleX = static_cast<float>(bounds.getWidth()) / frame.image.getWidth();
            float scaleY = static_cast<float>(bounds.getHeight()) / frame.image.getHeight();
            float scale = std::min(scaleX, scaleY);

            int scaledW = static_cast<int>(frame.image.getWidth() * scale);
            int scaledH = static_cast<int>(frame.image.getHeight() * scale);
            int offsetX = (bounds.getWidth() - scaledW) / 2;
            int offsetY = (bounds.getHeight() - scaledH) / 2;

            g.fillAll(juce::Colours::black);
            g.setOpacity(opacity_);
            g.drawImage(frame.image, offsetX, offsetY, scaledW, scaledH,
                        0, 0, frame.image.getWidth(), frame.image.getHeight());
            break;
        }

        case ScaleMode::Stretch: {
            g.setOpacity(opacity_);
            g.drawImage(frame.image, bounds.toFloat());
            break;
        }

        case ScaleMode::Tile: {
            g.setOpacity(opacity_);
            for (int y = 0; y < bounds.getHeight(); y += frame.image.getHeight()) {
                for (int x = 0; x < bounds.getWidth(); x += frame.image.getWidth()) {
                    g.drawImageAt(frame.image, x, y);
                }
            }
            break;
        }
    }

    if (darkOverlay_ > 0.0f) {
        g.setColour(juce::Colours::black.withAlpha(darkOverlay_));
        g.fillRect(bounds);
    }
}

void GifAnimatedBackground::resized()
{
    cachedScaled_ = juce::Image();
}

void GifAnimatedBackground::startAnimation()
{
    if (frames_.empty()) return;

    int delay = frames_[currentFrame_].delayMs;
    startTimer(delay);
    juce::Logger::writeToLog("[GifBackground] Animation started, " +
                             juce::String(frames_.size()) + " frames");
}

void GifAnimatedBackground::stopAnimation()
{
    stopTimer();
}

void GifAnimatedBackground::timerCallback()
{
    if (frames_.empty()) return;

    currentFrame_ = (currentFrame_ + 1) % static_cast<int>(frames_.size());

    int delay = frames_[currentFrame_].delayMs;
    startTimer(delay);

    repaint();
}

} // namespace samplar
