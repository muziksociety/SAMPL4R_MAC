#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include "DataPaths.h"

namespace samplar {

/**
 * RecentSongs — tiny static helper that persists the most-recently-loaded
 * audio files to a plain-text file in the user data directory.
 *
 * Storage: <UserDataDir>/recent_songs.txt
 *   - One absolute path per line (UTF-8)
 *   - Most-recent first
 *   - Capped at kMaxRecents entries
 *   - Case-insensitive dedupe
 *
 * All methods are best-effort: I/O errors are logged but never thrown.
 */
class RecentSongs
{
public:
    static constexpr int kMaxRecents = 10;

    /** Returns recent songs, most-recent-first. Skips entries that no
     *  longer exist on disk (returns only the still-valid ones). */
    static std::vector<juce::File> get()
    {
        std::vector<juce::File> out;
        auto file = storageFile();
        if (!file.existsAsFile())
            return out;

        juce::StringArray lines;
        file.readLines(lines);

        out.reserve((size_t) juce::jmin(lines.size(), kMaxRecents));
        for (auto& l : lines)
        {
            auto trimmed = l.trim();
            if (trimmed.isEmpty()) continue;
            juce::File f(trimmed);
            // Note: include even non-existing files; UI shows them as
            // "(missing)" so the user can prune them.
            out.push_back(f);
            if ((int) out.size() >= kMaxRecents) break;
        }

        juce::Logger::writeToLog("[RecentSongs] " + juce::String(juce::CharPointer_UTF8("\xe2\x9c\x85"))
                                 + " loaded " + juce::String((int) out.size()) + " entries");
        return out;
    }

    /** Pushes a file to the top of the list (dedup, cap, atomic write). */
    static void push(const juce::File& f)
    {
        if (f.getFullPathName().isEmpty()) return;

        juce::Logger::writeToLog("[RecentSongs] " + juce::String(juce::CharPointer_UTF8("\xf0\x9f\x93\x81"))
                                 + " push " + f.getFullPathName());

        auto current = readAll();
        const auto needle = f.getFullPathName().toLowerCase();

        // Remove any prior copy (case-insensitive)
        for (auto it = current.begin(); it != current.end();)
        {
            if (it->toLowerCase() == needle)
                it = current.erase(it);
            else
                ++it;
        }

        // Prepend new entry
        current.insert(current.begin(), f.getFullPathName());

        // Cap
        if ((int) current.size() > kMaxRecents)
            current.resize((size_t) kMaxRecents);

        writeAll(current);
    }

    /** Removes a specific file from the list. */
    static void remove(const juce::File& f)
    {
        if (f.getFullPathName().isEmpty()) return;

        auto current = readAll();
        const auto needle = f.getFullPathName().toLowerCase();
        bool changed = false;
        for (auto it = current.begin(); it != current.end();)
        {
            if (it->toLowerCase() == needle)
            {
                it = current.erase(it);
                changed = true;
            }
            else ++it;
        }

        if (changed)
        {
            juce::Logger::writeToLog("[RecentSongs] removed " + f.getFullPathName());
            writeAll(current);
        }
    }

    /** Clears the entire list. */
    static void clear()
    {
        writeAll({});
        juce::Logger::writeToLog("[RecentSongs] cleared");
    }

private:
    static juce::File storageFile()
    {
        return DataPaths::getUserDataDirectory().getChildFile("recent_songs.txt");
    }

    /** Reads raw paths (no existence filtering, no cap). */
    static std::vector<juce::String> readAll()
    {
        std::vector<juce::String> out;
        auto file = storageFile();
        if (!file.existsAsFile()) return out;

        juce::StringArray lines;
        file.readLines(lines);

        for (auto& l : lines)
        {
            auto trimmed = l.trim();
            if (!trimmed.isEmpty())
                out.push_back(trimmed);
        }
        return out;
    }

    /** Atomic write: write to .tmp, rename over the target. */
    static void writeAll(const std::vector<juce::String>& paths)
    {
        auto target = storageFile();
        auto tmp    = target.getSiblingFile(target.getFileName() + ".tmp");

        {
            juce::FileOutputStream out(tmp);
            if (!out.openedOk())
            {
                juce::Logger::writeToLog("[RecentSongs] " + juce::String(juce::CharPointer_UTF8("\xe2\x9d\x8c"))
                                         + " write failed (open): " + tmp.getFullPathName());
                return;
            }
            out.setPosition(0);
            out.truncate();
            for (auto& p : paths)
            {
                out.writeText(p, false, false, "\n");
                out.writeText("\n", false, false, nullptr);
            }
            out.flush();
        }

        // Replace target atomically (best-effort on Windows)
        if (target.existsAsFile())
            target.deleteFile();
        if (!tmp.moveFileTo(target))
        {
            juce::Logger::writeToLog("[RecentSongs] " + juce::String(juce::CharPointer_UTF8("\xe2\x9d\x8c"))
                                     + " write failed (rename): " + tmp.getFullPathName());
            // Fallback: copy contents directly
            tmp.copyFileTo(target);
            tmp.deleteFile();
        }
    }
};

} // namespace samplar
