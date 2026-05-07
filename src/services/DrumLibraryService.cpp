#include "DrumLibraryService.h"
#include <algorithm>

namespace samplar {

DrumLibraryService::DrumLibraryService(EventBus& bus)
    : bus_(bus)
{
    juce::Logger::writeToLog("[DrumLibraryService] === CONSTRUCTOR ===");
}

DrumLibraryService::~DrumLibraryService()
{
    juce::Logger::writeToLog("[DrumLibraryService] === DESTRUCTOR ===");
}

void DrumLibraryService::initialise()
{
    juce::Logger::writeToLog("[DrumLibraryService] === INITIALISE START ===");

    // Try to load from disk
    if (!load())
    {
        juce::Logger::writeToLog("[DrumLibraryService] No saved config, adding defaults");
        addDefaultFolders();
        save();
    }

    juce::Logger::writeToLog("[DrumLibraryService] Initialized with " +
                             juce::String(folders_.size()) + " library folders");
    juce::Logger::writeToLog("[DrumLibraryService] === INITIALISE END ===");
}

void DrumLibraryService::shutdown()
{
    juce::Logger::writeToLog("[DrumLibraryService] === SHUTDOWN START ===");
    save();
    juce::Logger::writeToLog("[DrumLibraryService] === SHUTDOWN END ===");
}

bool DrumLibraryService::addLibraryFolder(const juce::File& folder, const juce::String& name)
{
    juce::Logger::writeToLog("[DrumLibraryService] addLibraryFolder: " + folder.getFullPathName());

    if (!folder.isDirectory())
    {
        juce::Logger::writeToLog("[DrumLibraryService] ERROR: Not a directory");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already exists
    for (const auto& f : folders_)
    {
        if (f.path == folder)
        {
            juce::Logger::writeToLog("[DrumLibraryService] Folder already in library");
            return false;
        }
    }

    // Add new folder
    LibraryFolder newFolder;
    newFolder.path = folder;
    newFolder.name = name.isEmpty() ? folder.getFileName() : name;
    newFolder.isBuiltIn = false;
    newFolder.sampleCount = scanFolderForSamples(folder);

    folders_.push_back(newFolder);

    juce::Logger::writeToLog("[DrumLibraryService] Added folder: " + newFolder.name +
                             " (" + juce::String(newFolder.sampleCount) + " samples)");

    notifyLibraryChanged();
    return true;
}

bool DrumLibraryService::removeLibraryFolder(const juce::File& path)
{
    juce::Logger::writeToLog("[DrumLibraryService] removeLibraryFolder: " + path.getFullPathName());

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(folders_.begin(), folders_.end(),
                           [&path](const LibraryFolder& f) { return f.path == path; });

    if (it == folders_.end())
    {
        juce::Logger::writeToLog("[DrumLibraryService] Folder not found");
        return false;
    }

    // Don't allow removing built-in folders
    if (it->isBuiltIn)
    {
        juce::Logger::writeToLog("[DrumLibraryService] Cannot remove built-in folder");
        return false;
    }

    folders_.erase(it);
    juce::Logger::writeToLog("[DrumLibraryService] Folder removed");

    notifyLibraryChanged();
    return true;
}

const std::vector<LibraryFolder>& DrumLibraryService::getLibraryFolders() const
{
    return folders_;
}

std::vector<juce::File> DrumLibraryService::getRootFolders() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<juce::File> roots;
    for (const auto& f : folders_)
    {
        if (f.path.isDirectory())
            roots.push_back(f.path);
    }
    return roots;
}

int DrumLibraryService::scanFolderForSamples(const juce::File& folder) const
{
    if (!folder.isDirectory())
        return 0;

    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false,
                          "*.wav;*.mp3;*.aiff;*.flac;*.ogg");
    return audioFiles.size();
}

juce::Array<juce::File> DrumLibraryService::getAudioFilesInFolder(const juce::File& folder, bool recursive) const
{
    juce::Array<juce::File> audioFiles;

    if (!folder.isDirectory())
        return audioFiles;

    folder.findChildFiles(audioFiles, juce::File::findFiles, recursive,
                          "*.wav;*.mp3;*.aiff;*.flac;*.ogg");

    // Sort by name
    audioFiles.sort();

    return audioFiles;
}

bool DrumLibraryService::save()
{
    juce::Logger::writeToLog("[DrumLibraryService] === SAVE START ===");

    auto configFile = getConfigFile();

    // Ensure directory exists
    configFile.getParentDirectory().createDirectory();

    // Build JSON
    auto* root = new juce::DynamicObject();
    root->setProperty("version", CONFIG_VERSION);

    juce::Array<juce::var> foldersArray;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& f : folders_)
        {
            foldersArray.add(f.toJson());
        }
    }
    root->setProperty("folders", foldersArray);

    // Write to file
    juce::var json(root);
    juce::String jsonStr = juce::JSON::toString(json, true);

    if (configFile.replaceWithText(jsonStr))
    {
        juce::Logger::writeToLog("[DrumLibraryService] Saved to: " + configFile.getFullPathName());
        juce::Logger::writeToLog("[DrumLibraryService] === SAVE END (SUCCESS) ===");
        return true;
    }

    juce::Logger::writeToLog("[DrumLibraryService] ERROR: Failed to save config file");
    juce::Logger::writeToLog("[DrumLibraryService] === SAVE END (FAILED) ===");
    return false;
}

bool DrumLibraryService::load()
{
    juce::Logger::writeToLog("[DrumLibraryService] === LOAD START ===");

    auto configFile = getConfigFile();

    if (!configFile.existsAsFile())
    {
        juce::Logger::writeToLog("[DrumLibraryService] Config file does not exist");
        juce::Logger::writeToLog("[DrumLibraryService] === LOAD END (NO FILE) ===");
        return false;
    }

    juce::String jsonStr = configFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonStr);

    if (!json.isObject())
    {
        juce::Logger::writeToLog("[DrumLibraryService] ERROR: Invalid JSON format");
        juce::Logger::writeToLog("[DrumLibraryService] === LOAD END (PARSE ERROR) ===");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    folders_.clear();

    auto foldersVar = json.getProperty("folders", juce::var());
    if (auto* arr = foldersVar.getArray())
    {
        for (const auto& item : *arr)
        {
            auto folder = LibraryFolder::fromJson(item);
            if (folder.path.isDirectory())
            {
                folders_.push_back(folder);
            }
            else
            {
                juce::Logger::writeToLog("[DrumLibraryService] Skipping invalid folder: " +
                                         folder.path.getFullPathName());
            }
        }
    }

    loaded_ = true;
    juce::Logger::writeToLog("[DrumLibraryService] Loaded " + juce::String(folders_.size()) + " folders");
    juce::Logger::writeToLog("[DrumLibraryService] === LOAD END (SUCCESS) ===");
    return true;
}

juce::File DrumLibraryService::getConfigFile() const
{
    // Use application data directory
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("Samplar");

    return appDir.getChildFile(CONFIG_FILENAME);
}

bool DrumLibraryService::hasFolder(const juce::File& folder) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& f : folders_)
    {
        if (f.path == folder)
            return true;
    }
    return false;
}

void DrumLibraryService::refreshSampleCounts()
{
    juce::Logger::writeToLog("[DrumLibraryService] Refreshing sample counts...");

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& f : folders_)
    {
        f.sampleCount = scanFolderForSamples(f.path);
        juce::Logger::writeToLog("[DrumLibraryService] " + f.name + ": " +
                                 juce::String(f.sampleCount) + " samples");
    }

    notifyLibraryChanged();
}

void DrumLibraryService::notifyLibraryChanged()
{
    juce::Logger::writeToLog("[DrumLibraryService] Publishing drumLibrary.changed event");

    // Publish event for UI to update
    bus_.publish("drumLibrary.changed", juce::var());
}

void DrumLibraryService::addDefaultFolders()
{
    juce::Logger::writeToLog("[DrumLibraryService] Adding default folders...");

    // Add some common locations as suggestions
    // Users can add their own folders later

    // Check for common drum sample locations
    auto userHome = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    // Common sample library paths
    std::vector<juce::File> defaultPaths = {
        userHome.getChildFile("Documents").getChildFile("Samples"),
        userHome.getChildFile("Music").getChildFile("Samples"),
        userHome.getChildFile("Documents").getChildFile("Drum Samples"),
    };

    for (const auto& path : defaultPaths)
    {
        if (path.isDirectory())
        {
            LibraryFolder f;
            f.path = path;
            f.name = path.getFileName();
            f.isBuiltIn = true;
            f.sampleCount = scanFolderForSamples(path);

            folders_.push_back(f);
            juce::Logger::writeToLog("[DrumLibraryService] Added default: " + f.name);
        }
    }

    if (folders_.empty())
    {
        juce::Logger::writeToLog("[DrumLibraryService] No default folders found");
    }
}

} // namespace samplar
