#include "FullSongService.h"
#include "separation/OrtRuntime.h"
#include "separation/StemCacheLock.h"
#include "../utils/DataPaths.h"
#include "../utils/SamplarSettings.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

namespace samplar {

// Persistent stem cache layout (see header). Bump kStemCacheFormat whenever the stem files
// change meaning (sample format, naming, model export) so old entries simply miss.
// v2: song name in the stem file names cut to 60 chars, entry.json records stem byte sizes.
static constexpr const char* kStemCacheDirName   = "stem_cache_onnx";
static constexpr const char* kStemCacheFormat    = "f32wav-v2";
static constexpr const char* kStemCacheEntryFile = "entry.json";
static constexpr const char* kSeparationLockFile = ".separation.lock";
static constexpr const char* kCacheLockFile      = ".cache.lock";
static constexpr juce::int64 kStemCacheDefaultMaxBytes = (juce::int64) 10 * 1024 * 1024 * 1024;   // 10 GB
static constexpr juce::int64 kPartialMinAgeMs    = (juce::int64) 10 * 60 * 1000;             // 10 min (dead writer)
static constexpr juce::int64 kEvictMinIdleMs     = (juce::int64) 30 * 60 * 1000;             // 30 min since last use
static constexpr int         kStemBaseMaxChars   = 60;
static constexpr int         kCacheLockWaitMs    = 15000;                                    // holders keep it for ms

// Cache size cap; env SAMPLAR_STEM_CACHE_MAX_MB overrides it (tests exercise eviction with it).
static juce::int64 stemCacheMaxBytes()
{
    const auto env = juce::SystemStats::getEnvironmentVariable("SAMPLAR_STEM_CACHE_MAX_MB", {}).trim();
    if (env.isNotEmpty() && env.containsOnly("0123456789"))
        return env.getLargeIntValue() * 1024 * 1024;
    return kStemCacheDefaultMaxBytes;
}

// ----------------------------------------------------------------------------
// Cross-process lock helpers
// ----------------------------------------------------------------------------
namespace {

std::wstring wpath(const juce::File& f) { return f.getFullPathName().toWideCharPointer(); }

enum class LockOutcome { Held, Unlockable, Cancelled, TimedOut };

// Polls `lock` every 50 ms. Unlockable = the lock file cannot be opened (read-only profile,
// AV): the caller proceeds WITHOUT the lock rather than refusing to separate.
LockOutcome acquireLock(CrossProcessFileLock& lock, const juce::String& what, int timeoutMs,
                        const std::function<bool()>& cancel, const std::function<void()>& onFirstWait = {})
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    bool announced = false;
    for (;;)
    {
        const auto r = lock.tryLock();
        if (r == CrossProcessFileLock::Try::Acquired)
        {
            if (announced)
                juce::Logger::writeToLog("[FullSongService] " + what + " acquired after "
                    + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
            return LockOutcome::Held;
        }
        if (r == CrossProcessFileLock::Try::Error)
        {
            juce::Logger::writeToLog("[FullSongService] " + what + ": lock file unusable (Win32 error "
                + juce::String((juce::int64) lock.lastError()) + ") -> continuing WITHOUT the cross-process lock");
            return LockOutcome::Unlockable;
        }
        if (cancel && cancel())
        {
            juce::Logger::writeToLog("[FullSongService] " + what + ": cancelled while waiting");
            return LockOutcome::Cancelled;
        }
        if (timeoutMs >= 0 && juce::Time::getMillisecondCounterHiRes() - t0 > timeoutMs)
        {
            juce::Logger::writeToLog("[FullSongService] " + what + ": timed out after " + juce::String(timeoutMs) + " ms");
            return LockOutcome::TimedOut;
        }
        if (! announced)
        {
            announced = true;
            juce::Logger::writeToLog("[FullSongService] " + what + " busy (another process/instance) -> waiting");
            if (onFirstWait) onFirstWait();
        }
        juce::Thread::sleep(50);
    }
}

// Renames an entry away (atomic disappearance for readers). The slow recursive delete of the
// graveyard happens AFTER the cache lock is released (review N-22: deleting GBs under the lock
// made other processes' validate / lookup / commit time out). A dir with a file open in another
// process cannot be renamed on Windows: then it is left alone (still in use).
bool evictDir(const juce::File& dir, const juce::String& why, std::vector<juce::File>& toDelete)
{
    const auto graveyard = dir.getSiblingFile(dir.getFileName() + ".evict-" + juce::String(juce::Time::currentTimeMillis()));
    if (! dir.moveFileTo(graveyard))
    {
        juce::Logger::writeToLog("[FullSongService]   evict " + dir.getFileName() + " (" + why
            + ") SKIPPED: cannot rename (in use by another process?)");
        return false;
    }
    toDelete.push_back(graveyard);
    juce::Logger::writeToLog("[FullSongService]   evicted " + dir.getFileName() + " (" + why + ") -> "
        + graveyard.getFileName() + " (deleted after unlocking)");
    return true;
}

// Runs after the cache lock is released. A failed delete leaves an .evict-* dir that the next
// prune collects again.
void deleteEvicted(const std::vector<juce::File>& dirs)
{
    for (const auto& d : dirs)
    {
        const bool ok = d.deleteRecursively();
        juce::Logger::writeToLog("[FullSongService]   deleted " + d.getFileName()
            + (ok ? juce::String() : juce::String(" - FAILED, kept for the next prune")));
    }
}

juce::File leaseFileFor(const juce::File& partialDir)
{
    return partialDir.getSiblingFile(partialDir.getFileName() + ".lease");
}

} // namespace

// ----------------------------------------------------------------------------
std::mutex& FullSongService::separationGate()
{
    static std::mutex gate;
    return gate;
}

juce::File FullSongService::stemCacheRoot()
{
    const auto overrideDir = juce::SystemStats::getEnvironmentVariable("SAMPLAR_STEM_CACHE_DIR", {}).trim();
    if (overrideDir.isNotEmpty() && juce::File::isAbsolutePath(overrideDir))
        return juce::File(overrideDir);
    return DataPaths::getUserDataDirectory().getChildFile(kStemCacheDirName);
}

bool FullSongService::isSeparationRuntimeAvailable()
{
    return ensureOrtApi();
}

bool FullSongService::isReinstallError(const juce::String& errorText)
{
    // Both service messages end in "...Please reinstall SAMPLAR." (runtime / model missing).
    return errorText == ortUnavailableUserMessage() || errorText.containsIgnoreCase("reinstall");
}

// ----------------------------------------------------------------------------
FullSongService::FullSongService(EventBus& bus) : bus_(bus) {}

// Out-of-line dtor: the provider is held via unique_ptr.
FullSongService::~FullSongService() = default;

void FullSongService::initialise() {
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Initializing (ONNX backend)");
}

void FullSongService::shutdown() {
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Shutting down...");
    provider_.reset();
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
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Input: " + input.getFileName()
        + " drumsOnly=" + juce::String((int) cfg.drumsOnly)
        + " device=" + (cfg.deviceOverride.isEmpty() ? juce::String("setting") : cfg.deviceOverride)
        + " cache=" + juce::String((int) cfg.useStemCache));

    publishProgress(0, "Queued", cfg);
    publishProgress(5, "Loading ONNX model...", cfg);

    // No exception may escape into the caller's std::thread (std::terminate kills the DAW).
    try {
        res = separateAudioOnnx(input, cfg, tempPrefix);
    }
    catch (const std::bad_alloc&) {
        res = {};
        res.error = "Not enough memory to separate this song. Close other programs or use a shorter file.";
        juce::Logger::writeToLog("[FAILED] [FullSongService] std::bad_alloc: " + res.error);
    }
    catch (const std::exception& e) {
        res = {};
        res.error = juce::String("Stem separation failed: ") + e.what();
        juce::Logger::writeToLog("[FAILED] [FullSongService] exception: " + res.error);
    }

    if (res.error.isNotEmpty()) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] ONNX separation failed: " + res.error
            + (res.cancelled ? " (cancelled)" : "") + (res.runtimeUnavailable ? " (runtime unavailable)" : ""));
        return res;
    }
    juce::Logger::writeToLog("[OK] [FullSongService] ONNX separation completed (backend=" + res.backend
        + " fromCache=" + juce::String((int) res.fromCache) + ")");
    publishProgress(100, "Complete", cfg);
    return res;
}

// ----------------------------------------------------------------------------
void FullSongService::publishProgress(int pct, const juce::String& msg, const Config& cfg) {
    if (cfg.onProgress) cfg.onProgress(pct, msg);
    // Plain concatenation: String::formatted("%s") uses wide printf on Windows
    // and misreads UTF-8 bytes as UTF-16 (garbled CJK-looking log + over-read).
    juce::Logger::writeToLog("[FullSongService] " + juce::String(pct) + "% - " + msg);
}

// =====================================================================
// ONNX backend (4-stem native) - ported from SM5 2026-05-17, GPU 2026-10
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

juce::File FullSongService::resolveModelFile(const Config& cfg) const
{
    // Prefer the split body model (GPU-capable, host iSTFT) across ALL candidate folders,
    // then the requested/full export (CPU only) - e.g. dev folders that only have htdemucs.onnx.
    juce::File modelFile;
    if (cfg.onnxModelFileName == "htdemucs.onnx")
        modelFile = locateOnnxModel("htdemucs_body.onnx");
    if (!modelFile.existsAsFile())
        modelFile = locateOnnxModel(cfg.onnxModelFileName);
    juce::Logger::writeToLog("[ONNX] [FullSongService] model lookup: "
        + (modelFile.existsAsFile() ? modelFile.getFullPathName() : juce::String("<not found>")));
    return modelFile;
}

bool FullSongService::resolveAllowGpu(const Config& cfg) const
{
    bool allow = true;
    juce::String source;
    if (cfg.deviceOverride.equalsIgnoreCase("cpu"))      { allow = false; source = "config override"; }
    else if (cfg.deviceOverride.equalsIgnoreCase("gpu")) { allow = true;  source = "config override"; }
    else { allow = SamplarSettings::getInstance().getUseGpuSeparation(); source = "user setting"; }
    juce::Logger::writeToLog("[FullSongService] Decision: allowGpu=" + juce::String((int) allow) + " (" + source + ")");
    return allow;
}

bool FullSongService::ensureOnnxProviderReady(const juce::File& modelFile, bool allowGpu, juce::String& errorOut,
                                              bool& cancelledOut)
{
    cancelledOut = false;
    if (provider_ && provider_->isReady() && providerAllowsGpu_ == allowGpu
        && provider_->modelFile().getParentDirectory() == modelFile.getParentDirectory())
    {
        juce::Logger::writeToLog("[ONNX] [FullSongService] reusing provider (EP=" + provider_->activeExecutionProvider() + ")");
        return true;
    }

    if (!modelFile.existsAsFile()) {
        errorOut = "Stem separation model not found (models/htdemucs_body.onnx). Please reinstall SAMPLAR.";
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + errorOut);
        return false;
    }

    provider_.reset();
    // Session creation (3-5 s) is cancellable: on cancel the provider returns at once and the
    // ORT session is finished + released on a detached, DLL-pinned thread (review S-M3).
    provider_ = std::make_unique<OnnxSeparationProvider>(modelFile, allowGpu, shouldCancel);
    providerAllowsGpu_ = allowGpu;
    if (!provider_->isReady()) {
        cancelledOut = provider_->wasCancelled();
        errorOut = cancelledOut ? juce::String("Cancelled by user")
                 : provider_->isRuntimeUnavailable() ? provider_->lastError()
                                                     : "ONNX session init failed: " + provider_->lastError();
        provider_.reset();
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + errorOut);
        return false;
    }

    juce::Logger::writeToLog("[ONNX] [FullSongService] provider ready (EP="
                            + provider_->activeExecutionProvider() + ")");
    return true;
}

// ----------------------------------------------------------------------------
// Stem cache
// ----------------------------------------------------------------------------
juce::String FullSongService::makeStemCacheKey(const juce::File& input, const juce::File& modelFile)
{
    // Full path (case-folded: NTFS paths are case-insensitive) + size + mtime catch "same
    // file"; the model file name scopes body vs full-model stems; the format tag retires
    // entries when the on-disk stem format changes. The key is only a directory name: a hit
    // is always validated against entry.json (validateEntry), so a hash collision just misses.
    const juce::String key = input.getFullPathName().toLowerCase()
        + "|" + juce::String(input.getSize())
        + "|" + juce::String(input.getLastModificationTime().toMilliseconds())
        + "|" + modelFile.getFileName()
        + "|" + kStemCacheFormat;
    return juce::String::toHexString(key.hashCode64()).paddedLeft('0', 16);
}

juce::String FullSongService::stemBaseName(const juce::File& input)
{
    // Short, valid Windows file-name stem (audit S-M4): the full song name could push
    // <cache>\<key>.partial-<ms>\<song>_Vocals.wav past MAX_PATH.
    juce::String base = input.getFileNameWithoutExtension();
    if (base.length() > kStemBaseMaxChars)
        base = base.substring(0, kStemBaseMaxChars);
    base = base.trimEnd();
    while (base.endsWithChar('.') || base.endsWithChar(' '))
        base = base.dropLastCharacters(1);
    return base.isEmpty() ? juce::String("song") : base;
}

FullSongService::StemFiles FullSongService::expectedStems(const juce::File& dir, const juce::File& input)
{
    using S = OnnxSeparationProvider::Stem;
    const auto base = stemBaseName(input);
    StemFiles s;
    s.drums  = dir.getChildFile(OnnxSeparationProvider::stemFileName(base, S::Drums));
    s.bass   = dir.getChildFile(OnnxSeparationProvider::stemFileName(base, S::Bass));
    s.other  = dir.getChildFile(OnnxSeparationProvider::stemFileName(base, S::Other));
    s.vocals = dir.getChildFile(OnnxSeparationProvider::stemFileName(base, S::Vocals));
    return s;
}

// entry.json must describe THIS source (path, size, mtime), model and format, and every stem
// must exist with the byte size recorded at commit (catches truncation / partial deletion).
bool FullSongService::validateEntry(const juce::File& entryDir, const juce::File& input, const juce::File& modelFile,
                                    StemFiles& out, juce::String& whyNot)
{
    out = {};
    if (!entryDir.isDirectory()) { whyNot = "no entry"; return false; }
    const auto marker = entryDir.getChildFile(kStemCacheEntryFile);
    if (!marker.existsAsFile()) { whyNot = "no entry.json"; return false; }
    const juce::var json = juce::JSON::parse(marker.loadFileAsString());
    if (!json.isObject()) { whyNot = "entry.json unreadable"; return false; }

    if (json["format"].toString() != kStemCacheFormat) { whyNot = "format " + json["format"].toString(); return false; }
    if (!json["source"].toString().equalsIgnoreCase(input.getFullPathName())) { whyNot = "different source (hash collision)"; return false; }
    if ((juce::int64) json["size"] != input.getSize()) { whyNot = "source size changed"; return false; }
    if ((juce::int64) json["mtime"] != input.getLastModificationTime().toMilliseconds()) { whyNot = "source mtime changed"; return false; }
    if (modelFile != juce::File() && json["model"].toString() != modelFile.getFileName()) { whyNot = "model " + json["model"].toString(); return false; }

    const auto stems = expectedStems(entryDir, input);
    const juce::File files[] = { stems.drums, stems.bass, stems.other, stems.vocals };
    const juce::var sizes = json["stemBytes"];
    if (!sizes.isArray() || sizes.size() != 4) { whyNot = "no stemBytes"; return false; }
    for (int i = 0; i < 4; ++i)
    {
        const juce::int64 expected = (juce::int64) sizes[i];
        if (!files[i].existsAsFile() || expected <= 44 || files[i].getSize() != expected)
        {
            whyNot = files[i].getFileName() + (files[i].existsAsFile() ? " has the wrong size" : " missing");
            return false;
        }
    }
    out = stems;
    return true;
}

bool FullSongService::lookupStemCache(const juce::File& entryDir, const juce::File& input, const juce::File& modelFile,
                                      StemFiles& out) const
{
    juce::Logger::writeToLog("[FullSongService] stem cache lookup: " + entryDir.getFullPathName());
    out = {};
    if (!entryDir.isDirectory()) {
        juce::Logger::writeToLog("[FullSongService] stem cache MISS (no entry)");
        return false;
    }
    // Validate + LRU touch atomically w.r.t. eviction in other processes. Cancellable: the
    // separation thread is joined on unload (review N-9).
    CrossProcessFileLock lock(wpath(stemCacheRoot().getChildFile(kCacheLockFile)));
    const auto lk = acquireLock(lock, "cache lock (lookup)", kCacheLockWaitMs, shouldCancel);
    if (lk == LockOutcome::TimedOut || lk == LockOutcome::Cancelled) {
        juce::Logger::writeToLog(juce::String("[FullSongService] stem cache MISS (cache lock ")
            + (lk == LockOutcome::Cancelled ? "wait cancelled)" : "timeout)"));
        return false;
    }
    juce::String whyNot;
    if (!validateEntry(entryDir, input, modelFile, out, whyNot)) {
        juce::Logger::writeToLog("[FullSongService] stem cache entry invalid (" + whyNot + ") -> MISS, separate again");
        return false;
    }
    entryDir.getChildFile(kStemCacheEntryFile).setLastModificationTime(juce::Time::getCurrentTime());   // LRU touch
    juce::Logger::writeToLog("[FullSongService] stem cache HIT: " + entryDir.getFileName());
    return true;
}

bool FullSongService::validateCachedStemFile(const juce::File& anyStemFile, const std::function<bool()>& cancel)
{
    const auto dir = anyStemFile.getParentDirectory();
    const auto marker = dir.getChildFile(kStemCacheEntryFile);
    if (dir.getParentDirectory() == stemCacheRoot()
        && (dir.getFileName().contains(".partial-") || dir.getFileName().contains(".evict-")))
    {
        // In-flight / abandoned / evicted dir: pruned by any process at any time (review N-23).
        juce::Logger::writeToLog("[FullSongService] validateCachedStemFile " + anyStemFile.getFileName()
            + " -> INVALID (stems live in a temporary cache dir " + dir.getFileName() + ")");
        return false;
    }
    if (dir.getParentDirectory() != stemCacheRoot() || !marker.existsAsFile())
        return anyStemFile.existsAsFile();   // not a cache entry: nothing to validate
    const juce::var json = juce::JSON::parse(marker.loadFileAsString());
    const juce::File source(json["source"].toString());
    StemFiles stems;
    juce::String whyNot;
    bool ok = false;
    {
        CrossProcessFileLock lock(wpath(stemCacheRoot().getChildFile(kCacheLockFile)));
        const auto lk = acquireLock(lock, "cache lock (validate)", kCacheLockWaitMs, cancel);
        if (lk == LockOutcome::TimedOut || lk == LockOutcome::Cancelled)
        {
            // Never hand out unvalidated stems (review N-22): the caller re-separates (cache
            // lookup again) or, when cancelled, is unloading anyway.
            juce::Logger::writeToLog("[FullSongService] validateCachedStemFile " + anyStemFile.getFileName()
                + " -> INVALID (cache lock " + (lk == LockOutcome::Cancelled ? "wait cancelled" : "timeout") + ")");
            return false;
        }
        if (source.existsAsFile())
            ok = validateEntry(dir, source, {}, stems, whyNot);
        else
        {
            // Source moved/deleted: the stems are still self-consistent if sizes match.
            const juce::var sizes = json["stemBytes"];
            ok = sizes.isArray() && sizes.size() == 4;
            int found = 0;
            for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.wav"))
            {
                bool match = false;
                for (int i = 0; ok && i < 4; ++i) match = match || f.getSize() == (juce::int64) sizes[i];
                ok = ok && match;
                ++found;
            }
            ok = ok && found == 4;
            if (!ok) whyNot = "stems incomplete (source gone)";
        }
        if (ok) marker.setLastModificationTime(juce::Time::getCurrentTime());
    }
    juce::Logger::writeToLog("[FullSongService] validateCachedStemFile " + anyStemFile.getFileName() + " -> "
        + (ok ? juce::String("valid") : "INVALID (" + whyNot + ")"));
    return ok;
}

bool FullSongService::commitStemCache(const juce::File& partialDir, const juce::File& entryDir,
                                      const juce::File& input, const juce::File& modelFile,
                                      const juce::String& backend) const
{
    juce::Logger::writeToLog("[FullSongService] stem cache commit: " + partialDir.getFileName() + " -> " + entryDir.getFileName());

    const auto stems = expectedStems(partialDir, input);
    juce::Array<juce::var> stemBytes;
    for (const auto& f : { stems.drums, stems.bass, stems.other, stems.vocals })
        stemBytes.add(f.getSize());

    auto* obj = new juce::DynamicObject();
    obj->setProperty("source", input.getFullPathName());
    obj->setProperty("size", input.getSize());
    obj->setProperty("mtime", input.getLastModificationTime().toMilliseconds());
    obj->setProperty("model", modelFile.getFileName());
    obj->setProperty("format", kStemCacheFormat);
    obj->setProperty("backend", backend);
    obj->setProperty("stemBytes", stemBytes);
    obj->setProperty("created", juce::Time::getCurrentTime().toISO8601(true));
    const juce::var json(obj);
    if (!partialDir.getChildFile(kStemCacheEntryFile).replaceWithText(juce::JSON::toString(json))) {
        juce::Logger::writeToLog("[FullSongService] stem cache commit FAILED: cannot write entry.json");
        return false;
    }

    std::vector<juce::File> toDelete;   // deleted after the lock is released (review N-22)
    bool committed = false;
    {
        CrossProcessFileLock lock(wpath(stemCacheRoot().getChildFile(kCacheLockFile)));
        const auto lk = acquireLock(lock, "cache lock (commit)", kCacheLockWaitMs, shouldCancel);
        if (lk == LockOutcome::TimedOut || lk == LockOutcome::Cancelled)
            return false;

        bool replaceable = true;
        if (entryDir.exists()) {
            StemFiles existing;
            juce::String whyNot;
            if (validateEntry(entryDir, input, modelFile, existing, whyNot)) {
                // Another process committed the same song first and may be loading it right now:
                // never replace it (audit S-H1). Ours is identical work; drop it.
                juce::Logger::writeToLog("[FullSongService] stem cache commit: a valid entry already exists -> keeping it, discarding our partial");
                toDelete.push_back(partialDir);   // ours, leased: nobody else touches it
                pruneStemCacheLocked(entryDir, toDelete);
                replaceable = false;
                committed = true;
            } else {
                juce::Logger::writeToLog("[FullSongService] stem cache commit: replacing invalid entry (" + whyNot + ")");
                if (!evictDir(entryDir, "invalid: " + whyNot, toDelete)) {
                    juce::Logger::writeToLog("[FullSongService] stem cache commit FAILED: cannot move the invalid entry away");
                    replaceable = false;
                }
            }
        }
        if (replaceable) {
            if (!partialDir.moveFileTo(entryDir)) {
                juce::Logger::writeToLog("[FullSongService] stem cache commit FAILED: rename " + partialDir.getFullPathName()
                    + " -> " + entryDir.getFullPathName());
            } else {
                juce::Logger::writeToLog("[FullSongService] stem cache SAVE: " + entryDir.getFullPathName());
                pruneStemCacheLocked(entryDir, toDelete);
                committed = true;
            }
        }
    }   // cache lock released here
    deleteEvicted(toDelete);
    return committed;
}

// Caller holds the cache lock. Only RENAMES (evicted entries, dead partials) and collects the
// dirs to delete in `toDelete`; the caller deletes them after unlocking (review N-22).
void FullSongService::pruneStemCacheLocked(const juce::File& keepDir, std::vector<juce::File>& toDelete)
{
    const auto root = stemCacheRoot();
    juce::Logger::writeToLog("[FullSongService] === pruneStemCache START === root=" + root.getFullPathName());
    struct Entry { juce::File dir; juce::Time used; juce::int64 bytes; };
    std::vector<Entry> entries;
    juce::int64 total = 0;
    const auto now = juce::Time::getCurrentTime();
    const juce::int64 kStemCacheMaxBytes = stemCacheMaxBytes();

    for (const auto& d : root.findChildFiles(juce::File::findDirectories, false)) {
        const auto name = d.getFileName();
        if (name.contains(".evict-")) {
            toDelete.push_back(d);   // leftover of an eviction whose delete failed earlier
            continue;
        }
        if (name.contains(".partial-")) {
            // In-flight run (writer alive -> lease held, or heartbeat-fresh mtime) or leftover
            // of a crashed run.
            const auto ageMs = (now - d.getLastModificationTime()).inMilliseconds();
            const bool live = LeaseFile::isLeaseLive(wpath(leaseFileFor(d)));
            if (!live && ageMs > kPartialMinAgeMs) {
                juce::Logger::writeToLog("[FullSongService]   removing dead partial: " + name);
                evictDir(d, "dead partial", toDelete);
                leaseFileFor(d).deleteFile();
            }
            continue;
        }
        juce::int64 bytes = 0;
        for (const auto& f : d.findChildFiles(juce::File::findFiles, false))
            bytes += f.getSize();
        const auto marker = d.getChildFile(kStemCacheEntryFile);
        entries.push_back({ d, marker.existsAsFile() ? marker.getLastModificationTime() : d.getLastModificationTime(), bytes });
        total += bytes;
    }

    juce::Logger::writeToLog("[FullSongService]   entries=" + juce::String((int) entries.size())
        + " total=" + juce::String(total / 1048576) + " MB cap=" + juce::String(kStemCacheMaxBytes / 1048576) + " MB");
    if (total <= kStemCacheMaxBytes) {
        juce::Logger::writeToLog("[FullSongService] === pruneStemCache END (under cap) ===");
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
    for (const auto& e : entries) {
        if (total <= kStemCacheMaxBytes) break;
        if (e.dir == keepDir) continue;
        // An entry used recently may be loading in another process / instance right now.
        if ((now - e.used).inMilliseconds() < kEvictMinIdleMs) {
            juce::Logger::writeToLog("[FullSongService]   keeping recently used entry " + e.dir.getFileName());
            continue;
        }
        if (evictDir(e.dir, "LRU, " + juce::String(e.bytes / 1048576) + " MB", toDelete))
            total -= e.bytes;
    }
    juce::Logger::writeToLog("[FullSongService] === pruneStemCache END === total=" + juce::String(total / 1048576) + " MB");
}

// ----------------------------------------------------------------------------
FullSongService::Result FullSongService::separateAudioOnnx(const juce::File& input,
                                                            const Config& cfg,
                                                            const juce::String& tempPrefix)
{
    Result res;
    const juce::File modelFile = resolveModelFile(cfg);
    if (!modelFile.existsAsFile()) {
        res.error = "Stem separation model not found (models/htdemucs_body.onnx). Please reinstall SAMPLAR.";
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);
        return res;
    }

    // ---- Cache HIT (no gate, no model load; works even without the ONNX runtime) ----
    juce::File entryDir;
    if (cfg.useStemCache) {
        stemCacheRoot().createDirectory();
        entryDir = stemCacheRoot().getChildFile(makeStemCacheKey(input, modelFile));
        StemFiles cached;
        if (lookupStemCache(entryDir, input, modelFile, cached)) {
            res.fromCache = true;
            res.backend = "cache";
            res.outputDir = entryDir;
            res.stems = cached;
            res.drumStemFile = cached.drums;
            publishProgress(10, "Stem cache hit", cfg);
            publishProgress(80, "Stem cache hit", cfg);
            return res;
        }
    } else {
        juce::Logger::writeToLog("[FullSongService] Decision: stem cache bypassed (useStemCache=false)");
    }

    // ---- Runtime check before anything heavy (audit S-H2) ----
    if (!ensureOrtApi()) {
        res.runtimeUnavailable = true;
        res.error = ortUnavailableUserMessage();
        juce::Logger::writeToLog("[FAILED] [FullSongService] runtime unavailable: " + ortUnavailableReason());
        return res;
    }

    // ---- Separation gate: one model load + separation at a time, in this process (mutex)
    //      AND across processes (lock file) - two DAWs separating at once would also fight
    //      for VRAM and trip the GPU fallback.
    std::unique_lock<std::mutex> gate(separationGate(), std::defer_lock);
    {
        const double waitStart = juce::Time::getMillisecondCounterHiRes();
        bool announced = false;
        while (!gate.try_lock()) {
            if (shouldCancel && shouldCancel()) {
                res.cancelled = true;
                res.error = "Cancelled by user";
                juce::Logger::writeToLog("[FullSongService] separation gate: cancelled while waiting (" + input.getFileName() + ")");
                return res;
            }
            if (!announced) {
                announced = true;
                juce::Logger::writeToLog("[FullSongService] separation gate busy -> waiting (" + input.getFileName() + ")");
                publishProgress(10, "Waiting for another separation to finish...", cfg);
            }
            juce::Thread::sleep(100);
        }
        juce::Logger::writeToLog("[FullSongService] separation gate acquired after "
            + juce::String(juce::Time::getMillisecondCounterHiRes() - waitStart, 0) + " ms");
    }
    stemCacheRoot().createDirectory();
    CrossProcessFileLock processGate(wpath(stemCacheRoot().getChildFile(kSeparationLockFile)));
    if (acquireLock(processGate, "cross-process separation gate", -1, shouldCancel,
                    [this, &cfg] { publishProgress(10, "Waiting for another separation to finish...", cfg); })
        == LockOutcome::Cancelled)
    {
        res.cancelled = true;
        res.error = "Cancelled by user";
        return res;
    }

    // Another instance / process may have produced this entry while we waited.
    if (cfg.useStemCache) {
        StemFiles cached;
        if (lookupStemCache(entryDir, input, modelFile, cached)) {
            res.fromCache = true;
            res.backend = "cache";
            res.outputDir = entryDir;
            res.stems = cached;
            res.drumStemFile = cached.drums;
            publishProgress(80, "Stem cache hit", cfg);
            return res;
        }
    }

    juce::String err;
    bool providerCancelled = false;
    if (!ensureOnnxProviderReady(modelFile, resolveAllowGpu(cfg), err, providerCancelled)) {
        res.error = err;
        res.cancelled = providerCancelled;
        if (providerCancelled)
            juce::Logger::writeToLog("[FullSongService] Decision: cancelled during model load (session finished off-thread)");
        return res;
    }
    if (shouldCancel && shouldCancel()) {
        res.cancelled = true;
        res.error = "Cancelled by user";
        juce::Logger::writeToLog("[FullSongService] Decision: cancelled after model load, before separation");
        return res;
    }

    // Output: a partial cache dir (renamed on success) or the caller's folder.
    juce::File outDir;
    LeaseFile partialLease;
    if (cfg.useStemCache) {
        outDir = stemCacheRoot().getChildFile(entryDir.getFileName() + ".partial-" + juce::String(juce::Time::currentTimeMillis()));
        // Lease BEFORE the dir exists: a pruner in another process never sees an unleased live partial.
        // Without a lease (review N-24) the partial is protected by its age: the progress callback
        // below refreshes the dir's mtime on every chunk, so a run longer than the 10 min dead-
        // partial age is never pruned by another process while it is alive.
        if (!partialLease.acquire(wpath(leaseFileFor(outDir))))
            juce::Logger::writeToLog("[FullSongService] WARNING: cannot create lease for " + outDir.getFileName()
                + " -> protected by a heartbeat (dir mtime refreshed every chunk)");
    } else if (cfg.outputDirOverride != juce::File()) {
        outDir = cfg.outputDirOverride;
    } else {
        outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile(tempPrefix + juce::String(juce::Time::currentTimeMillis()));
    }
    if (!outDir.createDirectory()) {
        res.error = "Failed to create output dir: " + outDir.getFullPathName();
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);
        return res;
    }
    juce::Logger::writeToLog("[FullSongService] stem output dir: " + outDir.getFullPathName());

    const bool heartbeat = cfg.useStemCache && !partialLease.isHeld();
    auto progress = [this, &cfg, heartbeat, outDir](float p01, const juce::String& msg) {
        if (heartbeat) {
            // Creating + deleting an entry updates the directory's last-write time (NTFS).
            const auto beat = outDir.getChildFile(".alive");
            if (beat.create().wasOk()) beat.deleteFile();
        }
        const int pct = 10 + (int) std::round(p01 * 80.0f);
        publishProgress(juce::jlimit(10, 90, pct), msg, cfg);
    };

    // The cache always needs all 4 stems, whatever the caller asked for.
    const bool writeDrumsOnly = cfg.useStemCache ? false : cfg.drumsOnly;
    const bool ok = provider_->separate(input, outDir, writeDrumsOnly, progress, shouldCancel,
                                        cfg.useStemCache ? stemBaseName(input) : juce::String());
    res.backend = provider_->activeExecutionProvider();
    if (!ok) {
        res.cancelled = provider_->wasCancelled();
        res.error = provider_->lastError().isNotEmpty()
                       ? provider_->lastError()
                       : juce::String("ONNX separation failed (unknown error)");
        if (cfg.useStemCache)
            outDir.deleteRecursively();
        if (!provider_->isReady())
            provider_.reset();   // no usable session left (GPU + CPU rebuild failed)
        return res;
    }

    StemFiles stems = provider_->lastStems();
    if (cfg.useStemCache) {
        // Never hand a ".partial-*" path to the caller (review N-23): the session would save it
        // and any process prunes it 10 min later. Retry once (AV scanners briefly hold new
        // files), then copy into a stable, self-describing cache dir, else fail clearly.
        const auto cancelled = [this] { return shouldCancel && shouldCancel(); };
        bool committed = commitStemCache(outDir, entryDir, input, modelFile, res.backend);
        if (!committed && !cancelled()) {
            juce::Logger::writeToLog("[FullSongService] Decision: cache commit failed -> retrying once in 500 ms");
            juce::Thread::sleep(500);
            committed = commitStemCache(outDir, entryDir, input, modelFile, res.backend);
        }
        if (committed) {
            stems = expectedStems(entryDir, input);
            outDir = entryDir;
        } else if (cancelled()) {
            res.cancelled = true;
            res.error = "Cancelled by user";
            outDir.deleteRecursively();
            juce::Logger::writeToLog("[FullSongService] Decision: cancelled while committing -> partial deleted");
            return res;
        } else {
            const auto stable = stemCacheRoot().getChildFile(entryDir.getFileName() + ".copy-"
                                                             + juce::String(juce::Time::currentTimeMillis()));
            const bool copied = outDir.copyDirectoryTo(stable)
                             && stable.getChildFile(kStemCacheEntryFile).existsAsFile()
                             && expectedStems(stable, input).isComplete();
            outDir.deleteRecursively();
            if (!copied) {
                stable.deleteRecursively();
                res.error = "Could not save the separated stems to the stem cache (" + stemCacheRoot().getFullPathName()
                          + "). Check free disk space / antivirus and try again.";
                juce::Logger::writeToLog("[FAILED] [FullSongService] cache commit + copy fallback failed -> " + res.error);
                return res;
            }
            stems = expectedStems(stable, input);
            outDir = stable;
            juce::Logger::writeToLog("[FullSongService] Decision: cache commit failed twice -> stems copied to stable dir "
                                     + stable.getFullPathName() + " (validated on restore, LRU-managed)");
        }
    }

    res.outputDir = outDir;
    res.stems = stems;
    res.drumStemFile = stems.drums;
    if (!res.drumStemFile.existsAsFile()) {
        res.error = "ONNX completed but drum stem missing: " + res.drumStemFile.getFullPathName();
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);
        return res;
    }
    return res;
}

} // namespace samplar
