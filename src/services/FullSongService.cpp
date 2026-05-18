#include "FullSongService.h"
#include "../utils/DataPaths.h"
#include "separation/OnnxSeparationProvider.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace samplar {

// ----------------------------------------------------------------------------
FullSongService::FullSongService(EventBus& bus) : bus_(bus) {}

// Out-of-line dtor: required because the header forward-declares
// OnnxSeparationProvider and holds it via unique_ptr; the destructor must
// see the full type to delete it.
FullSongService::~FullSongService() = default;

void FullSongService::initialise() {
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Initializing (ONNX backend)");
}

void FullSongService::shutdown() {
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Shutting down...");
    provider_.reset();
}

// ----------------------------------------------------------------------------
FullSongService::Result FullSongService::process(const juce::File& input,
                                                 const Config& cfg,
                                                 const juce::String& tempPrefix)
{
    Result res;
    if (!input.existsAsFile()) {
        res.error = "Input file not found";
        juce::Logger::writeToLog("[FAILED] [FullSongService] Input not found: " + input.getFullPathName());
        return res;
    }

    juce::Logger::writeToLog("[AUDIO] [FullSongService] Starting full song processing: " + input.getFileName());

    publishProgress(0, "Queued", cfg);
    publishProgress(5, "Loading ONNX model...", cfg);

    res = separateAudioOnnx(input, cfg, tempPrefix);
    if (res.error.isNotEmpty()) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] ONNX separation failed: " + res.error);
        return res;
    }
    juce::Logger::writeToLog("[OK] [FullSongService] ONNX separation completed successfully");

    publishProgress(80, "Detecting drum samples...", cfg);
    res = processDrumStem(res.drumStemFile, cfg, tempPrefix);
    if (res.error.isNotEmpty()) {
        return res;
    }

    publishProgress(100, "Complete", cfg);
    juce::Logger::writeToLog("[OK] [FullSongService] Full song processing complete: " + res.outputDir.getFullPathName());
    return res;
}

// ----------------------------------------------------------------------------
FullSongService::Result FullSongService::separateDrums(const juce::File& input,
                                                       const Config& cfg,
                                                       const juce::String& tempPrefix)
{
    Result res;
    if (!input.existsAsFile()) {
        res.error = "Input file not found";
        juce::Logger::writeToLog("[FAILED] [FullSongService] Input not found: " + input.getFullPathName());
        return res;
    }

    juce::Logger::writeToLog("[AUDIO] [FullSongService] === SEPARATION ONLY ===");
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Input: " + input.getFileName());

    publishProgress(0, "Queued", cfg);
    publishProgress(5, "Loading ONNX model...", cfg);

    res = separateAudioOnnx(input, cfg, tempPrefix);
    if (res.error.isNotEmpty()) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] ONNX separation failed: " + res.error);
        return res;
    }
    juce::Logger::writeToLog("[OK] [FullSongService] ONNX separation completed");
    publishProgress(100, "Complete", cfg);
    return res;
}

// ----------------------------------------------------------------------------
FullSongService::Result FullSongService::processDrumStem(const juce::File& drumStemFile,
                                                          const Config& cfg,
                                                          const juce::String& tempPrefix)
{
    Result res;
    juce::Logger::writeToLog("[DRUM] [FullSongService] Processing drum stem: " + drumStemFile.getFileName());

    // SAMPLAR fork: DrumSliceService removed. Stems-to-pads workflow returns the
    // drum stem file directly; S-mode places it in a slot rather than slicing.
    juce::ignoreUnused(tempPrefix, cfg);

    res.drumStemFile = drumStemFile;
    res.outputDir    = drumStemFile.getParentDirectory();
    res.sampleFiles.add(drumStemFile);
    return res;
}

// ----------------------------------------------------------------------------
void FullSongService::publishProgress(int pct, const juce::String& msg, const Config& cfg) {
    if (cfg.onProgress) cfg.onProgress(pct, msg);
    juce::Logger::writeToLog(juce::String::formatted("[FullSongService] %d%% - %s", pct, msg.toRawUTF8()));
}

void FullSongService::publishError(const juce::String& error, const Config& /*cfg*/) {
    juce::Logger::writeToLog("[FAILED] [FullSongService] Error: " + error);
}

// ----------------------------------------------------------------------------
bool FullSongService::loadAudioFile(const juce::File& file, juce::AudioBuffer<float>& buffer, double& sampleRate) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] Failed to create reader for: " + file.getFileName());
        return false;
    }

    sampleRate = reader->sampleRate;
    int numChannels = static_cast<int>(reader->numChannels);
    int numSamples  = static_cast<int>(reader->lengthInSamples);

    buffer.setSize(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    juce::Logger::writeToLog(juce::String::formatted(
        "[FOLDER] [FullSongService] Loaded audio: %d channels, %d samples, %.1f Hz",
        numChannels, numSamples, sampleRate));
    return true;
}

bool FullSongService::saveAudioFile(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto* wavFormat = formatManager.findFormatForFileExtension("wav");
    if (!wavFormat) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] WAV format not available");
        return false;
    }

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> fileStream(file.createOutputStream());
    if (!fileStream) return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat->createWriterFor(fileStream.get(), sampleRate,
                                   buffer.getNumChannels(), 16, {}, 0));
    if (!writer) return false;

    fileStream.release(); // writer takes ownership
    bool success = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    return success;
}

// =====================================================================
// ONNX backend (4-stem native) - ported from SM5 2026-05-17
// =====================================================================

juce::File FullSongService::locateOnnxModel(const juce::String& fileName) const
{
    // 1) Canonical: DataPaths::AppDirectory/models/<file>
    auto cand = DataPaths::getModelsDirectory().getChildFile(fileName);
    if (cand.existsAsFile()) return cand;

    // 2) Next to the application/bundle
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto appDir  = appFile.isDirectory() ? appFile : appFile.getParentDirectory();
    cand = appDir.getChildFile("models").getChildFile(fileName);
    if (cand.existsAsFile()) return cand;

    // 3) Inside the VST3 bundle (where the installer drops resources)
    cand = appDir.getChildFile("Contents/x86_64-win/models").getChildFile(fileName);
    if (cand.existsAsFile()) return cand;

    // 4) Next to the actual binary
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    cand = exeFile.getParentDirectory().getChildFile("models").getChildFile(fileName);
    if (cand.existsAsFile()) return cand;

    // 5) User data dir override
    cand = DataPaths::getUserDataDirectory().getChildFile("models").getChildFile(fileName);
    if (cand.existsAsFile()) return cand;

    return {};
}

bool FullSongService::ensureOnnxProviderReady(const Config& cfg, juce::String& errorOut)
{
    if (provider_ && provider_->isReady())
        return true;

    juce::File modelFile = locateOnnxModel(cfg.onnxModelFileName);
    juce::Logger::writeToLog("[ONNX] [FullSongService] model lookup: "
        + (modelFile.existsAsFile() ? modelFile.getFullPathName() : juce::String("<not found>")));

    if (!modelFile.existsAsFile()) {
        errorOut = "ONNX model not found: " + cfg.onnxModelFileName;
        return false;
    }

    provider_ = std::make_unique<OnnxSeparationProvider>(modelFile);
    if (!provider_->isReady()) {
        errorOut = "ONNX session init failed: " + provider_->lastError();
        provider_.reset();
        return false;
    }

    juce::Logger::writeToLog("[ONNX] [FullSongService] provider ready (EP="
                            + provider_->activeExecutionProvider() + ")");
    return true;
}

FullSongService::Result FullSongService::separateAudioOnnx(const juce::File& input,
                                                            const Config& cfg,
                                                            const juce::String& tempPrefix)
{
    Result res;
    juce::String err;
    if (!ensureOnnxProviderReady(cfg, err)) {
        res.error = err;
        return res;
    }

    auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto outDir   = tempRoot.getChildFile(tempPrefix + juce::String(juce::Time::currentTimeMillis()));
    if (!outDir.createDirectory()) {
        res.error = "Failed to create output dir: " + outDir.getFullPathName();
        return res;
    }

    auto progress = [this, &cfg](float p01, const juce::String& msg) {
        const int pct = 10 + (int) std::round(p01 * 70.0f);
        publishProgress(juce::jlimit(10, 80, pct), msg, cfg);
    };

    const bool ok = provider_->separate(input, outDir, cfg.drumsOnly, progress, shouldCancel);
    if (!ok) {
        res.error = provider_->lastError().isNotEmpty()
                       ? provider_->lastError()
                       : juce::String("ONNX separation failed (unknown error)");
        return res;
    }

    res.drumStemFile = provider_->lastStems().drums;
    res.outputDir    = outDir;
    if (!res.drumStemFile.existsAsFile()) {
        res.error = "ONNX completed but drum stem missing: " + res.drumStemFile.getFullPathName();
        return res;
    }
    return res;
}

} // namespace samplar
