#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <map>

namespace samplar {

/**
 * MidiFileGenerator - Converts step sequencer patterns to MIDI files
 *
 * Creates standard MIDI files (Type 0) with GM drum mapping.
 * Uses 480 ticks per quarter note for precise timing.
 */
class MidiFileGenerator
{
public:
    static constexpr int kTicksPerQuarterNote = 480;
    static constexpr int kDrumChannel = 10;  // GM drum channel
    static constexpr int kDefaultVelocity = 100;

    /**
     * Generate a MIDI file from a step pattern grid
     *
     * @param grid Step pattern (rows x steps), non-zero = note on
     * @param rowNames Names for each row (used for GM drum mapping)
     * @param tempo Tempo in BPM
     * @param beatsPerBar Beats per bar for timing calculation
     * @param outputPath Path to write the MIDI file
     * @return true if file was successfully written
     */
    static bool generateMidiFile(
        const std::vector<std::vector<uint8_t>>& grid,
        const juce::StringArray& rowNames,
        double tempo,
        int beatsPerBar,
        const juce::String& outputPath
    );

    /**
     * Same as above but with per-row note overrides. For each row, if
     * noteOverrides[row] >= 0 it is used verbatim; otherwise the code falls
     * back to the GM mapping derived from rowNames. Pass an empty vector to
     * use GM mapping for every row (equivalent to the 5-arg overload).
     */
    static bool generateMidiFile(
        const std::vector<std::vector<uint8_t>>& grid,
        const juce::StringArray& rowNames,
        const std::vector<int>& noteOverrides,
        double tempo,
        int beatsPerBar,
        const juce::String& outputPath
    );

    /**
     * Check if grid has any active steps
     */
    static bool hasActiveSteps(const std::vector<std::vector<uint8_t>>& grid);

    /**
     * Generate a unique filename for the MIDI file
     * Format: SAMPLAR_pattern_HHMMSS_XXX.mid
     */
    static juce::String generateUniqueFilename();

    /**
     * Get GM drum note for a row name
     */
    static int getDrumNoteForName(const juce::String& name);

private:
    MidiFileGenerator() = delete;  // Static-only class

    // GM Drum note mappings
    static const std::map<juce::String, int> kDrumNoteMap;
};

} // namespace samplar
