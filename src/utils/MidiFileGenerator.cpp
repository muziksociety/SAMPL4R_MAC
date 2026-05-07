#include "MidiFileGenerator.h"

namespace samplar {

// GM Drum note mappings (case-insensitive matching)
const std::map<juce::String, int> MidiFileGenerator::kDrumNoteMap = {
    { "kick",    36 },
    { "snare",   38 },
    { "hat",     42 },
    { "hihat",   42 },
    { "hi-hat",  42 },
    { "clap",    39 },
    { "rim",     37 },
    { "tom",     45 },
    { "crash",   49 },
    { "ride",    51 },
    { "perc",    39 },  // Default percussion to clap
    { "perc 1",  39 },
    { "perc 2",  54 },  // Tambourine
    { "perc 3",  56 },  // Cowbell
    { "perc 4",  75 },  // Claves
    { "perc 5",  76 },  // Hi Wood Block
};

int MidiFileGenerator::getDrumNoteForName(const juce::String& name)
{
    juce::String lower = name.toLowerCase().trim();

    // Try exact match first
    auto it = kDrumNoteMap.find(lower);
    if (it != kDrumNoteMap.end())
        return it->second;

    // Try partial match
    for (const auto& pair : kDrumNoteMap)
    {
        if (lower.contains(pair.first))
            return pair.second;
    }

    // Default to kick
    return 36;
}

bool MidiFileGenerator::generateMidiFile(
    const std::vector<std::vector<uint8_t>>& grid,
    const juce::StringArray& rowNames,
    double tempo,
    int beatsPerBar,
    const juce::String& outputPath)
{
    // Delegate to the overload with an empty override vector → pure GM fallback.
    return generateMidiFile(grid, rowNames, std::vector<int>{}, tempo, beatsPerBar, outputPath);
}

bool MidiFileGenerator::generateMidiFile(
    const std::vector<std::vector<uint8_t>>& grid,
    const juce::StringArray& rowNames,
    const std::vector<int>& noteOverrides,
    double tempo,
    int beatsPerBar,
    const juce::String& outputPath)
{
    juce::Logger::writeToLog("[MidiFileGenerator] === GENERATING MIDI FILE ===");
    juce::Logger::writeToLog("[MidiFileGenerator] Output: " + outputPath);
    juce::Logger::writeToLog("[MidiFileGenerator] Tempo: " + juce::String(tempo) + " BPM");
    juce::Logger::writeToLog("[MidiFileGenerator] Overrides provided: "
        + juce::String((int)noteOverrides.size()));

    if (grid.empty() || grid[0].empty())
    {
        juce::Logger::writeToLog("[MidiFileGenerator] Empty grid - skipping");
        return false;
    }

    const int numRows = (int)grid.size();
    const int numSteps = (int)grid[0].size();

    juce::Logger::writeToLog("[MidiFileGenerator] Grid: " + juce::String(numRows) + " rows x " + juce::String(numSteps) + " steps");

    // Clamp tempo to prevent issues
    tempo = juce::jlimit(20.0, 400.0, tempo);
    beatsPerBar = juce::jmax(1, beatsPerBar);

    // Create MIDI file
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(kTicksPerQuarterNote);

    // Create single track
    juce::MidiMessageSequence track;

    // Add tempo meta event at time 0
    double microsecondsPerQuarterNote = 60000000.0 / tempo;
    auto tempoEvent = juce::MidiMessage::tempoMetaEvent(static_cast<int>(microsecondsPerQuarterNote));
    track.addEvent(tempoEvent, 0.0);

    // Calculate ticks per step
    // Steps per beat = numSteps / beatsPerBar
    // Ticks per step = kTicksPerQuarterNote / stepsPerBeat
    double stepsPerBeat = (double)numSteps / (double)beatsPerBar;
    int ticksPerStep = static_cast<int>(kTicksPerQuarterNote / stepsPerBeat);
    int noteLength = ticksPerStep - 10;  // Slightly shorter for separation

    juce::Logger::writeToLog("[MidiFileGenerator] Ticks per step: " + juce::String(ticksPerStep));

    int noteCount = 0;

    // Add note events for each active step
    for (int row = 0; row < numRows; ++row)
    {
        int midiNote = -1;

        // 1) Prefer explicit override (from live midiMap) when present & valid.
        if (row < (int)noteOverrides.size() && noteOverrides[row] >= 0
            && noteOverrides[row] <= 127)
        {
            midiNote = noteOverrides[row];
        }

        // 2) GM fallback based on row name.
        if (midiNote < 0)
        {
            juce::String rowName = (row < rowNames.size()) ? rowNames[row] : "Kick";
            midiNote = getDrumNoteForName(rowName);
        }

        for (int step = 0; step < numSteps; ++step)
        {
            if (grid[row][step] != 0)
            {
                int startTick = step * ticksPerStep;
                int endTick = startTick + noteLength;

                // Note On (channel 10 for drums, 1-indexed in MIDI but JUCE uses 1-16)
                auto noteOn = juce::MidiMessage::noteOn(kDrumChannel, midiNote, (juce::uint8)kDefaultVelocity);
                track.addEvent(noteOn, startTick);

                // Note Off
                auto noteOff = juce::MidiMessage::noteOff(kDrumChannel, midiNote);
                track.addEvent(noteOff, endTick);

                noteCount++;
            }
        }
    }

    juce::Logger::writeToLog("[MidiFileGenerator] Notes added: " + juce::String(noteCount));

    // Add end-of-track marker
    int endTick = numSteps * ticksPerStep;
    track.addEvent(juce::MidiMessage::endOfTrack(), endTick);

    // Sort and update matched pairs
    track.sort();
    track.updateMatchedPairs();

    // Add track to file
    midiFile.addTrack(track);

    // Write to file
    juce::File outputFile(outputPath);

    // Delete existing file if present
    if (outputFile.existsAsFile())
        outputFile.deleteFile();

    // Create parent directories if needed
    outputFile.getParentDirectory().createDirectory();

    // Write MIDI file
    juce::FileOutputStream outputStream(outputFile);
    if (!outputStream.openedOk())
    {
        juce::Logger::writeToLog("[MidiFileGenerator] Failed to open output file");
        return false;
    }

    bool success = midiFile.writeTo(outputStream);
    outputStream.flush();

    if (success)
    {
        juce::Logger::writeToLog("[MidiFileGenerator] MIDI file written: " + juce::String(outputFile.getSize()) + " bytes");
    }
    else
    {
        juce::Logger::writeToLog("[MidiFileGenerator] Failed to write MIDI file");
    }

    return success;
}

bool MidiFileGenerator::hasActiveSteps(const std::vector<std::vector<uint8_t>>& grid)
{
    for (const auto& row : grid)
    {
        for (uint8_t step : row)
        {
            if (step != 0)
                return true;
        }
    }
    return false;
}

juce::String MidiFileGenerator::generateUniqueFilename()
{
    auto now = juce::Time::getCurrentTime();

    // Format: SAMPLAR_pattern_HHMMSS_XXX.mid
    juce::String filename = "SAMPLAR_pattern_";
    filename += now.formatted("%H%M%S");
    filename += "_";
    filename += juce::String(juce::Random::getSystemRandom().nextInt(999)).paddedLeft('0', 3);
    filename += ".mid";

    return filename;
}

} // namespace samplar
