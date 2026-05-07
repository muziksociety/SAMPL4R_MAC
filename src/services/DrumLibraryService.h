#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <mutex>

namespace samplar {

/**
 * LibraryFolder - Represents a folder in the drum library
 */
struct LibraryFolder {
    juce::String name;           // Display name
    juce::File path;             // Folder path
    bool isBuiltIn = false;      // True for preset library folders
    int sampleCount = 0;         // Cached count of audio files

    // Serialize to JSON
    juce::var toJson() const {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", name);
        obj->setProperty("path", path.getFullPathName());
        obj->setProperty("isBuiltIn", isBuiltIn);
        obj->setProperty("sampleCount", sampleCount);
        return juce::var(obj);
    }

    // Deserialize from JSON
    static LibraryFolder fromJson(const juce::var& json) {
        LibraryFolder f;
        f.name = json.getProperty("name", "").toString();
        f.path = juce::File(json.getProperty("path", "").toString());
        f.isBuiltIn = static_cast<bool>(json.getProperty("isBuiltIn", false));
        f.sampleCount = static_cast<int>(json.getProperty("sampleCount", 0));
        return f;
    }
};

/**
 * DrumLibraryService - Backend service for drum library folder management
 *
 * Features:
 * - Manages user-added library folders
 * - JSON persistence (data/drum_libraries.json)
 * - Folder scanning and sample counting
 * - EventBus integration for UI updates
 */
class DrumLibraryService : public IService
{
public:
    DrumLibraryService(EventBus& bus);
    ~DrumLibraryService() override;

    // IService interface
    void initialise() override;
    void shutdown() override;

    /**
     * Add a folder to the drum library
     * @param folder The folder to add
     * @param name Display name (optional - uses folder name if empty)
     * @return True if added successfully
     */
    bool addLibraryFolder(const juce::File& folder, const juce::String& name = "");

    /**
     * Remove a folder from the drum library
     * @param path Full path of folder to remove
     * @return True if removed
     */
    bool removeLibraryFolder(const juce::File& path);

    /**
     * Get all library folders
     */
    const std::vector<LibraryFolder>& getLibraryFolders() const;

    /**
     * Get root folders (top-level entries for tree view)
     */
    std::vector<juce::File> getRootFolders() const;

    /**
     * Scan a folder for audio files
     * @return Number of audio files found
     */
    int scanFolderForSamples(const juce::File& folder) const;

    /**
     * Get all audio files in a folder (recursive or not)
     */
    juce::Array<juce::File> getAudioFilesInFolder(const juce::File& folder, bool recursive = false) const;

    /**
     * Save library configuration to disk
     */
    bool save();

    /**
     * Load library configuration from disk
     */
    bool load();

    /**
     * Get the config file path
     */
    juce::File getConfigFile() const;

    /**
     * Check if a folder is already in the library
     */
    bool hasFolder(const juce::File& folder) const;

    /**
     * Refresh sample counts for all folders
     */
    void refreshSampleCounts();

private:
    EventBus& bus_;
    std::vector<LibraryFolder> folders_;
    mutable std::mutex mutex_;
    bool loaded_ = false;

    void notifyLibraryChanged();
    void addDefaultFolders();

    static constexpr const char* CONFIG_FILENAME = "drum_libraries.json";
    static constexpr const char* CONFIG_VERSION = "1.0";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumLibraryService)
};

} // namespace samplar
