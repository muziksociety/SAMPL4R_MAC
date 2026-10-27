#include "SamplePreviewCache.h"

namespace samplar {

//==============================================================================
// Constructor / Destructor
//==============================================================================

// Delegating ctor — provides the implicit-defaults overload that used to be
// `= Config{}` in the header (rejected by Apple Clang as a class-body default arg).
SamplePreviewCache::SamplePreviewCache(juce::AudioFormatManager& formatManager)
    : SamplePreviewCache(formatManager, Config{})
{
}

SamplePreviewCache::SamplePreviewCache(juce::AudioFormatManager& formatManager, const Config& config)
    : juce::Thread("SamplePreviewCacheLoader")
    , config_(config)
    , formatManager_(formatManager)
{
    juce::Logger::writeToLog("[SamplePreviewCache] === INITIALIZED ===");
    juce::Logger::writeToLog("[SamplePreviewCache] Max cache size: " +
        juce::String(config_.maxCacheSizeBytes / (1024 * 1024)) + " MB");

    // Start the background loading thread
    startThread(juce::Thread::Priority::low);
}

SamplePreviewCache::~SamplePreviewCache()
{
    juce::Logger::writeToLog("[SamplePreviewCache] === SHUTTING DOWN ===");

    // Signal thread to stop
    signalThreadShouldExit();
    queueEvent_.signal();

    // Wait for thread to finish
    stopThread(5000);

    // Clear cache
    clearAll();

    juce::Logger::writeToLog("[SamplePreviewCache] Shutdown complete");
}

//==============================================================================
// Cache Operations
//==============================================================================

bool SamplePreviewCache::isCached(const juce::File& file) const
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.find(makeKey(file)) != cache_.end();
}

std::unique_ptr<SamplePreviewCache::CachedSample> SamplePreviewCache::getCachedSample(const juce::File& file)
{
    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto key = makeKey(file);
    auto it = cache_.find(key);

    if (it == cache_.end()) {
        cacheMisses_.fetch_add(1);
        return nullptr;
    }

    // Update LRU order - move to front
    lruOrder_.erase(it->second.lruPosition);
    lruOrder_.push_front(key);
    it->second.lruPosition = lruOrder_.begin();

    cacheHits_.fetch_add(1);

    // Return a COPY of the cached sample for thread safety
    auto result = std::make_unique<CachedSample>();
    result->buffer.makeCopyOf(it->second.sample.buffer);
    result->sampleRate = it->second.sample.sampleRate;
    result->fileSizeBytes = it->second.sample.fileSizeBytes;

    juce::Logger::writeToLog("[SamplePreviewCache] CACHE HIT: " + file.getFileName());
    return result;
}

SamplePreviewCache::SharedBufferRef SamplePreviewCache::getSharedBuffer(const juce::File& file)
{
    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto key = makeKey(file);
    auto it = cache_.find(key);

    if (it == cache_.end()) {
        cacheMisses_.fetch_add(1);
        return {};  // Empty SharedBufferRef
    }

    // Update LRU order - move to front
    lruOrder_.erase(it->second.lruPosition);
    lruOrder_.push_front(key);
    it->second.lruPosition = lruOrder_.begin();

    cacheHits_.fetch_add(1);

    // Return shared reference - NO COPY!
    SharedBufferRef ref;
    ref.buffer = it->second.sharedBuffer;
    ref.sampleRate = it->second.sample.sampleRate;

    return ref;
}

void SamplePreviewCache::requestPreload(const juce::Array<juce::File>& files, bool priority)
{
    if (files.isEmpty())
        return;

    int queuedCount = 0;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        for (const auto& file : files) {
            // Skip if already cached
            if (isCached(file))
                continue;

            // Skip if already in queue
            auto key = makeKey(file);
            bool alreadyQueued = false;

            for (const auto& queued : loadQueue_) {
                if (makeKey(queued) == key) {
                    alreadyQueued = true;
                    break;
                }
            }

            if (!alreadyQueued) {
                if (priority)
                    loadQueue_.push_front(file);
                else
                    loadQueue_.push_back(file);
                queuedCount++;
            }
        }
    }

    if (queuedCount > 0) {
        juce::Logger::writeToLog("[SamplePreviewCache] Queued " + juce::String(queuedCount) +
            " files for preload (priority=" + (priority ? "true" : "false") + ")");
        queueEvent_.signal();
    }
}

void SamplePreviewCache::clearAll()
{
    std::lock_guard<std::mutex> lock(cacheMutex_);

    int count = static_cast<int>(cache_.size());
    cache_.clear();
    lruOrder_.clear();
    currentCacheSizeBytes_ = 0;

    if (count > 0) {
        juce::Logger::writeToLog("[SamplePreviewCache] Cleared all (" + juce::String(count) + " samples)");
    }
}

void SamplePreviewCache::invalidateFile(const juce::File& file)
{
    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto key = makeKey(file);
    auto it = cache_.find(key);
    if (it != cache_.end())
    {
        // Update memory tracking
        size_t freedBytes = it->second.sample.getMemoryUsage();
        currentCacheSizeBytes_ -= freedBytes;

        // Remove from LRU list
        lruOrder_.erase(it->second.lruPosition);

        // Remove from cache
        cache_.erase(it);

        juce::Logger::writeToLog("[SamplePreviewCache] Invalidated: " + file.getFileName());
    }
}

void SamplePreviewCache::cacheBuffer(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    juce::Logger::writeToLog("[SamplePreviewCache] === cacheBuffer START === file: " + file.getFileName());

    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
    {
        juce::Logger::writeToLog("[SamplePreviewCache] ERROR: Empty buffer, cannot cache");
        return;
    }

    auto numSamples = buffer.getNumSamples();
    auto numChannels = buffer.getNumChannels();
    size_t memoryNeeded = static_cast<size_t>(numSamples) *
                          static_cast<size_t>(numChannels) * sizeof(float);

    juce::Logger::writeToLog("[SamplePreviewCache] Buffer: " + juce::String(numChannels) + " ch, " +
                             juce::String(numSamples) + " samples, " +
                             juce::String(sampleRate, 0) + " Hz, " +
                             juce::String(memoryNeeded / 1024) + " KB");

    // Thread-safe cache update
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        auto key = makeKey(file);

        // If already cached, invalidate first
        auto existing = cache_.find(key);
        if (existing != cache_.end())
        {
            size_t freedBytes = existing->second.sample.getMemoryUsage();
            currentCacheSizeBytes_ -= freedBytes;
            lruOrder_.erase(existing->second.lruPosition);
            cache_.erase(existing);
            juce::Logger::writeToLog("[SamplePreviewCache] Replaced existing cache entry");
        }

        // Ensure we have space (evict if needed)
        if (currentCacheSizeBytes_ + memoryNeeded > config_.maxCacheSizeBytes)
        {
            evictLeastRecentlyUsed(memoryNeeded);
        }

        // Still not enough space after eviction? Skip this sample
        if (currentCacheSizeBytes_ + memoryNeeded > config_.maxCacheSizeBytes)
        {
            juce::Logger::writeToLog("[SamplePreviewCache] Not enough cache space for buffer");
            return;
        }

        // Create cached sample by copying the buffer
        CachedSample sample;
        sample.buffer.makeCopyOf(buffer);
        sample.sampleRate = sampleRate;
        sample.fileSizeBytes = file.existsAsFile() ? file.getSize() : 0;

        // Add to LRU list (front = most recent)
        lruOrder_.push_front(key);

        // Add to cache map
        CacheEntry entry;
        entry.sample = std::move(sample);
        entry.lruPosition = lruOrder_.begin();
        // Create shared buffer reference pointing to the cached buffer (AFTER sample is moved to entry)
        entry.sharedBuffer = std::make_shared<const juce::AudioBuffer<float>>(entry.sample.buffer);

        cache_[key] = std::move(entry);
        currentCacheSizeBytes_ += memoryNeeded;

        juce::Logger::writeToLog("[SamplePreviewCache] ✅ Cached buffer for: " + file.getFileName() +
                                 " (total cache: " + juce::String(currentCacheSizeBytes_ / (1024 * 1024)) + " MB)");
    }

    notifyListeners(file);
}

void SamplePreviewCache::stopLoading()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        loadQueue_.clear();
    }
    juce::Logger::writeToLog("[SamplePreviewCache] Load queue cleared");
}

//==============================================================================
// Statistics
//==============================================================================

SamplePreviewCache::Stats SamplePreviewCache::getStats() const
{
    Stats stats;

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        stats.currentCacheSizeBytes = currentCacheSizeBytes_;
        stats.cachedSampleCount = static_cast<int>(cache_.size());
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stats.pendingLoadCount = static_cast<int>(loadQueue_.size());
    }

    stats.maxCacheSizeBytes = config_.maxCacheSizeBytes;
    stats.cacheHits = cacheHits_.load();
    stats.cacheMisses = cacheMisses_.load();

    return stats;
}

void SamplePreviewCache::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void SamplePreviewCache::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

//==============================================================================
// Background Loading Thread
//==============================================================================

void SamplePreviewCache::run()
{
    juce::Logger::writeToLog("[SamplePreviewCache] Background loader thread started");

    while (!threadShouldExit()) {
        // Wait for work or exit signal
        queueEvent_.wait(-1);

        if (threadShouldExit())
            break;

        int loadedCount = 0;

        while (!threadShouldExit()) {
            juce::File fileToLoad;

            // Get next file from queue
            {
                std::lock_guard<std::mutex> lock(queueMutex_);

                if (loadQueue_.empty()) {
                    break;
                }

                fileToLoad = loadQueue_.front();
                loadQueue_.pop_front();
            }

            // Load the sample
            if (fileToLoad.existsAsFile() && !isCached(fileToLoad)) {
                loadSample(fileToLoad);
                loadedCount++;
            }

            // Yield to other threads periodically
            if (loadedCount % 3 == 0) {
                juce::Thread::yield();
            }
        }

        if (loadedCount > 0) {
            juce::Logger::writeToLog("[SamplePreviewCache] Batch loaded: " + juce::String(loadedCount) + " samples");

            // Notify listeners on message thread
            juce::MessageManager::callAsync([this, loadedCount]() {
                listeners_.call([loadedCount](Listener& l) {
                    l.loadingComplete(loadedCount);
                });
            });
        }
    }

    juce::Logger::writeToLog("[SamplePreviewCache] Background loader thread exiting");
}

void SamplePreviewCache::loadSample(const juce::File& file)
{
    auto startTime = juce::Time::getMillisecondCounterHiRes();

    // Create reader
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file)
    );

    if (reader == nullptr) {
        juce::Logger::writeToLog("[SamplePreviewCache] Failed to create reader: " + file.getFileName());
        return;
    }

    // Check file duration
    auto numSamples = static_cast<int>(reader->lengthInSamples);
    auto numChannels = static_cast<int>(reader->numChannels);
    auto sampleRate = reader->sampleRate;

    double durationMs = (numSamples / sampleRate) * 1000.0;
    if (durationMs > config_.maxSampleDurationMs) {
        juce::Logger::writeToLog("[SamplePreviewCache] Sample too long, skipping: " +
            file.getFileName() + " (" + juce::String(durationMs / 1000.0, 1) + "s)");
        return;
    }

    size_t memoryNeeded = static_cast<size_t>(numSamples) *
                          static_cast<size_t>(numChannels) * sizeof(float);

    // Ensure we have space (evict if needed)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        if (currentCacheSizeBytes_ + memoryNeeded > config_.maxCacheSizeBytes) {
            evictLeastRecentlyUsed(memoryNeeded);
        }

        // Still not enough space after eviction? Skip this sample
        if (currentCacheSizeBytes_ + memoryNeeded > config_.maxCacheSizeBytes) {
            juce::Logger::writeToLog("[SamplePreviewCache] Not enough cache space for: " +
                file.getFileName());
            return;
        }
    }

    // Load audio data into buffer
    CachedSample sample;
    sample.buffer.setSize(numChannels, numSamples);
    sample.buffer.clear();

    if (!reader->read(&sample.buffer, 0, numSamples, 0, true, true)) {
        juce::Logger::writeToLog("[SamplePreviewCache] Failed to read audio: " + file.getFileName());
        return;
    }

    sample.sampleRate = sampleRate;
    sample.fileSizeBytes = file.getSize();

    // Add to cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        auto key = makeKey(file);

        // Check if already cached (race condition protection)
        if (cache_.find(key) != cache_.end()) {
            return;
        }

        // Add to LRU list (front = most recent)
        lruOrder_.push_front(key);

        // Add to cache map
        CacheEntry entry;
        entry.sample = std::move(sample);
        entry.lruPosition = lruOrder_.begin();
        // Create shared buffer reference pointing to the cached buffer (AFTER sample is moved to entry)
        entry.sharedBuffer = std::make_shared<const juce::AudioBuffer<float>>(entry.sample.buffer);

        cache_[key] = std::move(entry);
        currentCacheSizeBytes_ += memoryNeeded;
    }

    auto loadTime = juce::Time::getMillisecondCounterHiRes() - startTime;
    juce::Logger::writeToLog("[SamplePreviewCache] Loaded: " + file.getFileName() +
        " (" + juce::String(memoryNeeded / 1024) + " KB, " +
        juce::String(loadTime, 1) + "ms)");

    notifyListeners(file);
}

void SamplePreviewCache::evictLeastRecentlyUsed(size_t bytesNeeded)
{
    // Must be called with cacheMutex_ held!

    while (currentCacheSizeBytes_ + bytesNeeded > config_.maxCacheSizeBytes &&
           !lruOrder_.empty()) {
        // Get least recently used (back of list)
        auto lruKey = lruOrder_.back();
        auto it = cache_.find(lruKey);

        if (it != cache_.end()) {
            size_t freedBytes = it->second.sample.getMemoryUsage();
            currentCacheSizeBytes_ -= freedBytes;
            cache_.erase(it);

            juce::Logger::writeToLog("[SamplePreviewCache] LRU evicted: " + lruKey +
                " (freed " + juce::String(freedBytes / 1024) + " KB)");
        }

        lruOrder_.pop_back();
    }
}

void SamplePreviewCache::notifyListeners(const juce::File& file)
{
    // Notify on message thread
    juce::MessageManager::callAsync([this, file]() {
        listeners_.call([&file](Listener& l) {
            l.sampleLoaded(file);
        });
    });
}

juce::String SamplePreviewCache::makeKey(const juce::File& file)
{
    // Use full path as key (normalized to lowercase for case-insensitive matching on Windows)
    return file.getFullPathName().toLowerCase();
}

} // namespace samplar
