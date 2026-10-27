#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <map>
#include <list>
#include <mutex>
#include <atomic>

namespace samplar {

/**
 * Thread-safe LRU cache for preloaded audio sample buffers.
 * Designed for instant playback of visible samples in SampleBrowserPage.
 *
 * Usage:
 * 1. Call requestPreload() with visible files to queue background loading
 * 2. Call getCachedBuffer() before playback to check cache
 * 3. If cached, use buffer directly (instant); otherwise fallback to disk load
 */
class SamplePreviewCache : private juce::Thread
{
public:
    //==========================================================================
    // Configuration
    //==========================================================================

    struct Config {
        size_t maxCacheSizeBytes = 256 * 1024 * 1024;  // 256 MB default
        int maxSampleDurationMs = 10000;                // 10 seconds max per sample
        int numLoaderThreads = 2;                       // Parallel loaders
    };

    //==========================================================================
    // Cached Sample Data
    //==========================================================================

    struct CachedSample {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        juce::int64 fileSizeBytes = 0;

        size_t getMemoryUsage() const {
            return static_cast<size_t>(buffer.getNumSamples()) *
                   static_cast<size_t>(buffer.getNumChannels()) * sizeof(float);
        }
    };

    // Shared buffer reference - avoids copying large audio buffers
    struct SharedBufferRef {
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double sampleRate = 0.0;
    };

    //==========================================================================
    // Lifecycle
    //==========================================================================

    // Apple Clang rejects `= Config{}` as a default arg here because it requires
    // the enclosing class to be complete when the default member initializers
    // of the nested Config are referenced. MSVC accepts it; we remove the
    // default and provide a delegating overload for callers that want defaults.
    explicit SamplePreviewCache(juce::AudioFormatManager& formatManager, const Config& config);
    explicit SamplePreviewCache(juce::AudioFormatManager& formatManager);
    ~SamplePreviewCache() override;

    // Non-copyable
    SamplePreviewCache(const SamplePreviewCache&) = delete;
    SamplePreviewCache& operator=(const SamplePreviewCache&) = delete;

    //==========================================================================
    // Cache Operations (Thread-Safe)
    //==========================================================================

    /**
     * Check if a sample is cached and ready for playback.
     * Safe to call from any thread.
     */
    bool isCached(const juce::File& file) const;

    /**
     * Get a cached sample buffer. Returns nullptr if not cached.
     * Updates LRU order. Safe to call from any thread.
     * IMPORTANT: Returns a COPY of the buffer for thread safety.
     */
    std::unique_ptr<CachedSample> getCachedSample(const juce::File& file);

    /**
     * Get a SHARED reference to a cached buffer (NO COPY - instant).
     * Returns empty SharedBufferRef if not cached.
     * PERFORMANCE: Use this for step sequencer playback to avoid buffer copies.
     */
    SharedBufferRef getSharedBuffer(const juce::File& file);

    /**
     * Request loading of samples. Non-blocking.
     * Samples not already cached will be queued for background loading.
     * @param files List of files to preload
     * @param priority If true, load these before other queued samples
     */
    void requestPreload(const juce::Array<juce::File>& files, bool priority = false);

    /**
     * Clear all cached samples.
     */
    void clearAll();

    /**
     * Invalidate (remove) a specific file from the cache.
     * Use this when a file's contents have been replaced on disk.
     */
    void invalidateFile(const juce::File& file);

    /**
     * Directly cache an audio buffer without loading from disk.
     * Useful for caching processed/edited audio immediately for instant playback.
     * @param file The file path to use as the cache key
     * @param buffer The audio buffer to cache (will be copied)
     * @param sampleRate The sample rate of the buffer
     */
    void cacheBuffer(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate);

    /**
     * Stop all background loading.
     */
    void stopLoading();

    //==========================================================================
    // Status & Statistics
    //==========================================================================

    struct Stats {
        size_t currentCacheSizeBytes = 0;
        size_t maxCacheSizeBytes = 0;
        int cachedSampleCount = 0;
        int pendingLoadCount = 0;
        int cacheHits = 0;
        int cacheMisses = 0;
    };

    Stats getStats() const;

    //==========================================================================
    // Listener for cache events
    //==========================================================================

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void sampleLoaded(const juce::File& file) {}
        virtual void loadingComplete(int samplesLoaded) {}
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    //==========================================================================
    // Internal Types
    //==========================================================================

    // LRU list: front = most recently used, back = least recently used
    using LruList = std::list<juce::String>;
    using LruIterator = LruList::iterator;

    struct CacheEntry {
        CachedSample sample;
        std::shared_ptr<const juce::AudioBuffer<float>> sharedBuffer;  // Shared reference for no-copy access
        LruIterator lruPosition;
    };

    //==========================================================================
    // Thread Implementation
    //==========================================================================

    void run() override;

    //==========================================================================
    // Internal Methods
    //==========================================================================

    void loadSample(const juce::File& file);
    void evictLeastRecentlyUsed(size_t bytesNeeded);
    void notifyListeners(const juce::File& file);

    static juce::String makeKey(const juce::File& file);

    //==========================================================================
    // Members
    //==========================================================================

    Config config_;
    juce::AudioFormatManager& formatManager_;

    // Cache storage (protected by mutex)
    mutable std::mutex cacheMutex_;
    std::map<juce::String, CacheEntry> cache_;
    LruList lruOrder_;
    size_t currentCacheSizeBytes_ = 0;

    // Load queue (protected by mutex)
    mutable std::mutex queueMutex_;
    std::list<juce::File> loadQueue_;
    juce::WaitableEvent queueEvent_;

    // Statistics (atomic for thread-safe reads)
    mutable std::atomic<int> cacheHits_{0};
    mutable std::atomic<int> cacheMisses_{0};

    // Listeners
    juce::ListenerList<Listener> listeners_;

    JUCE_LEAK_DETECTOR(SamplePreviewCache)
};

} // namespace samplar
