#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include "separation/OnnxSeparationProvider.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace samplar {

/**
 * FullSongService - 4-stem separation of a full song (native C++/ONNX Runtime).
 *
 * Backend: OnnxSeparationProvider with models/htdemucs_body.onnx (GPU via DirectML when
 * available and allowed, CPU otherwise); falls back to the full models/htdemucs.onnx
 * (CPU only) when the body model is missing. 4 stems: drums, bass, other, vocals.
 *
 * Persistent stem cache (ported from SM5 a4900a3b / b6ce642a, SAMPLAR layout):
 *   <userData>/stem_cache_onnx/<key>/<song60>_{Drums,Bass,Other,Vocals}.wav + entry.json
 *   (root overridable with env SAMPLAR_STEM_CACHE_DIR for tests/tools). key = hash(full path,
 *   size, mtime, model file name, output format); <song60> = song name cut to 60 chars so the
 *   paths stay far below MAX_PATH. A hit is only served after entry.json is validated against
 *   the source (path, size, mtime), the model and the format, and all 4 stems exist with the
 *   byte sizes recorded at commit. Stems are written into "<key>.partial-<ms>" (guarded by a
 *   live-process lease file) and renamed into place on success.
 *
 * Cross-process safety (audit S-H1), all through LockFileEx lock files in the cache root:
 *   .separation.lock - at most one model load + separation at a time across ALL processes
 *                      (two DAWs / sandboxed hosts) and plugin instances; polled, cancellable.
 *   .cache.lock      - short critical sections: lookup + LRU touch, commit, eviction (renames
 *                      only; evicted dirs are deleted after the lock is released). Every wait is
 *                      cancellable through shouldCancel / the validate cancel hook (unload).
 *   Commit never replaces a VALID entry (another process may be loading it): our partial is
 *   discarded and the existing entry is used. Eviction (10 GB LRU cap) skips entries used in the
 *   last 30 min and partials whose writer is alive; evicted dirs are renamed away first so a
 *   reader never sees half-deleted entries. The legacy <userData>/stem_cache is never touched.
 *   A failed commit is retried once, then the stems are copied to "<key>.copy-<ms>" (a valid,
 *   LRU-managed entry); a ".partial-*" path is never returned to the caller.
 */
class FullSongService : public IService {
public:
    using StemFiles = OnnxSeparationProvider::StemFiles;

    struct Config {
        // Requested model; "htdemucs.onnx" means "prefer htdemucs_body.onnx when present".
        juce::String onnxModelFileName = "htdemucs.onnx";

        // Only the drums stem is needed by the caller. The cache path always writes all 4.
        bool drumsOnly = true;

        // Device: "" = user setting (SamplarSettings "Use GPU"), "cpu" or "gpu" (tools/tests).
        juce::String deviceOverride;

        // false = bypass the persistent cache (tools): stems go to outputDirOverride.
        bool useStemCache = true;
        juce::File outputDirOverride;

        // Progress reporting (0..100, worker thread)
        std::function<void(int, const juce::String&)> onProgress;
    };

    struct Result {
        juce::String error;
        bool cancelled = false;
        bool fromCache = false;
        juce::String backend;        // "CPU", "DirectML (<gpu>)" or "cache"
        juce::File outputDir;
        juce::File drumStemFile;     // == stems.drums
        StemFiles stems;             // exact paths (no filename matching, audit C14)
        bool runtimeUnavailable = false; // onnxruntime.dll missing: show "separation unavailable - reinstall"
    };

    explicit FullSongService(EventBus& bus);
    ~FullSongService() override;

    void initialise() override;
    void shutdown() override;

    // Optional cancel hook: polled while waiting for the gate, between chunks and by the
    // provider's watcher thread (aborts the running ONNX chunk).
    std::function<bool()> shouldCancel;

    // Separate `input` into 4 stems (cache first). drumStemFile/stems are set on success.
    Result separateDrums(const juce::File& input, const Config& cfg, const juce::String& tempPrefix = "fullSong_");

    // Process-wide gate: at most one model load + separation at a time.
    static std::mutex& separationGate();

    // Root of the persistent stem cache.
    static juce::File stemCacheRoot();

    // False when onnxruntime.dll cannot be loaded (UI: "separation unavailable - reinstall").
    // Cheap after the first call; never throws or crashes.
    static bool isSeparationRuntimeAvailable();

    // True for the error texts that mean "reinstall needed": Result::runtimeUnavailable's
    // message (ortUnavailableUserMessage) and the missing-model message. UI: show the
    // "separation unavailable - reinstall" notice only for these (review N-21).
    static bool isReinstallError(const juce::String& errorText);

    // For stem paths restored from saved state: true when `anyStemFile` lies in a valid cache
    // entry whose recorded source (if it still exists) is unchanged (size + mtime) and whose 4
    // stems are intact. Touches the entry (protects it from LRU eviction). Files outside the
    // cache return true when they exist (nothing to validate). False for stems inside a
    // ".partial-*" / ".evict-*" dir (temporary, pruned by any process) and when the cache lock
    // cannot be taken (timeout, or `cancel` returned true while waiting - unload).
    static bool validateCachedStemFile(const juce::File& anyStemFile,
                                       const std::function<bool()>& cancel = {});

private:
    EventBus& bus_;

    std::unique_ptr<OnnxSeparationProvider> provider_;
    bool providerAllowsGpu_ = true;

    juce::File locateOnnxModel(const juce::String& fileName) const;
    juce::File resolveModelFile(const Config& cfg) const;
    bool resolveAllowGpu(const Config& cfg) const;
    // cancelledOut: shouldCancel fired during the (detached) session creation.
    bool ensureOnnxProviderReady(const juce::File& modelFile, bool allowGpu, juce::String& errorOut,
                                 bool& cancelledOut);
    Result separateAudioOnnx(const juce::File& input, const Config& cfg, const juce::String& tempPrefix);

    // Stem cache helpers
    static juce::String makeStemCacheKey(const juce::File& input, const juce::File& modelFile);
    static juce::String stemBaseName(const juce::File& input);
    static StemFiles expectedStems(const juce::File& dir, const juce::File& input);
    static bool validateEntry(const juce::File& entryDir, const juce::File& input, const juce::File& modelFile,
                              StemFiles& out, juce::String& whyNot);
    bool lookupStemCache(const juce::File& entryDir, const juce::File& input, const juce::File& modelFile,
                         StemFiles& out) const;
    // True when entryDir now holds a valid entry for input (ours, or an existing valid one kept).
    bool commitStemCache(const juce::File& partialDir, const juce::File& entryDir,
                         const juce::File& input, const juce::File& modelFile, const juce::String& backend) const;
    // Caller holds the cache lock; renames only, `toDelete` is deleted after unlocking.
    static void pruneStemCacheLocked(const juce::File& keepDir, std::vector<juce::File>& toDelete);

    void publishProgress(int pct, const juce::String& msg, const Config& cfg);
};

} // namespace samplar
