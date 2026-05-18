#include "PatternGenerator.h"
#include "SModeSegmentModel.h"
#include <algorithm>
#include <random>
#include <map>
#include <cmath>

namespace samplar {

// ── Pattern expansion ─────────────────────────────────────────────────────

std::vector<char> PatternGenerator::expandPattern(PatternType type, int numBars)
{
    // Base pattern (4-bar unit)
    std::vector<char> base;

    switch (type)
    {
        case PatternType::AABB:     base = { 'A', 'A', 'B', 'B' }; break;
        case PatternType::ABAB:     base = { 'A', 'B', 'A', 'B' }; break;
        case PatternType::AAAB:     base = { 'A', 'A', 'A', 'B' }; break;
        case PatternType::ABAC:     base = { 'A', 'B', 'A', 'C' }; break;
        case PatternType::AABA:     base = { 'A', 'A', 'B', 'A' }; break;
        case PatternType::ABCD:     base = { 'A', 'B', 'C', 'D' }; break;
        case PatternType::Random:   base = {}; break;
        case PatternType::Euclidean: base = {}; break;
    }

    if (type == PatternType::Random || type == PatternType::Euclidean)
    {
        std::vector<char> result(static_cast<size_t>(numBars));
        std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
        std::uniform_int_distribution<int> dist(0, 3);
        for (auto& r : result)
            r = static_cast<char>('A' + dist(rng));
        return result;
    }

    // Tile the base pattern to fill numBars
    std::vector<char> result;
    result.reserve(static_cast<size_t>(numBars));

    while (static_cast<int>(result.size()) < numBars)
    {
        for (char c : base)
        {
            result.push_back(c);
            if (static_cast<int>(result.size()) >= numBars)
                break;
        }
    }

    return result;
}

// ── Segment picking ───────────────────────────────────────────────────────

std::vector<PatternGenerator::SegmentRef> PatternGenerator::pickUniqueSegments(
    const std::vector<SegmentRef>& pool, int count)
{
    if (pool.empty()) return {};

    std::vector<SegmentRef> shuffled = pool;
    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    std::vector<SegmentRef> result;
    int picked = 0;
    for (const auto& seg : shuffled)
    {
        if (picked >= count) break;
        result.push_back(seg);
        ++picked;
    }

    // If not enough unique segments, wrap around
    while (static_cast<int>(result.size()) < count && !pool.empty())
    {
        result.push_back(pool[static_cast<size_t>(result.size() % pool.size())]);
    }

    return result;
}

// ── Main generation ───────────────────────────────────────────────────────

std::vector<PatternSlot> PatternGenerator::generate(
    const std::vector<SegmentRef>& availableSegments,
    PatternType pattern,
    int numBars,
    int slotsPerBar)
{
    juce::Logger::writeToLog("[PatternGenerator] === generate START ===");
    juce::Logger::writeToLog("[PatternGenerator]   pattern=" + getPatternString(pattern)
                             + " bars=" + juce::String(numBars)
                             + " slotsPerBar=" + juce::String(slotsPerBar)
                             + " available=" + juce::String(static_cast<int>(availableSegments.size())));

    int totalSlots = numBars * slotsPerBar;

    // Euclidean gets its own generator
    if (pattern == PatternType::Euclidean)
    {
        // Fill 100% — no empty slots
        return generateEuclidean(availableSegments, totalSlots, totalSlots);
    }

    std::vector<PatternSlot> result(static_cast<size_t>(totalSlots));

    if (availableSegments.empty())
    {
        juce::Logger::writeToLog("[PatternGenerator]   No segments available — returning empty");
        return result;
    }

    // Expand pattern to bar-level roles
    std::vector<char> barRoles = expandPattern(pattern, numBars);

    // Find unique roles used
    std::set<char> uniqueRoles(barRoles.begin(), barRoles.end());
    int rolesNeeded = static_cast<int>(uniqueRoles.size());

    // Assign a random segment to each role
    auto roleSegments = pickUniqueSegments(availableSegments, rolesNeeded);
    std::map<char, SegmentRef> roleMap;
    int roleIdx = 0;
    for (char role : uniqueRoles)
    {
        if (roleIdx < static_cast<int>(roleSegments.size()))
            roleMap[role] = roleSegments[static_cast<size_t>(roleIdx)];
        ++roleIdx;
    }

    // Fill slots
    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis() + 7));

    for (int bar = 0; bar < numBars; ++bar)
    {
        char role = barRoles[static_cast<size_t>(bar)];
        auto it = roleMap.find(role);

        for (int slot = 0; slot < slotsPerBar; ++slot)
        {
            int idx = bar * slotsPerBar + slot;
            if (idx >= totalSlots) break;

            auto& ps = result[static_cast<size_t>(idx)];
            ps.role = role;

            if (it != roleMap.end())
            {
                // For density > 1, optionally vary within the same role
                if (slotsPerBar > 1 && slot > 0 && availableSegments.size() > 1)
                {
                    // 30% chance to substitute a different segment within the bar
                    std::uniform_int_distribution<int> chance(0, 9);
                    if (chance(rng) < 3)
                    {
                        std::uniform_int_distribution<int> segDist(
                            0, static_cast<int>(availableSegments.size()) - 1);
                        const auto& alt = availableSegments[static_cast<size_t>(segDist(rng))];
                        ps.stemIndex = alt.stemIndex;
                        ps.segmentIndex = alt.segmentIndex;
                        ps.empty = false;
                        continue;
                    }
                }

                ps.stemIndex = it->second.stemIndex;
                ps.segmentIndex = it->second.segmentIndex;
                ps.empty = false;
            }
        }
    }

    juce::Logger::writeToLog("[PatternGenerator]   Generated " + juce::String(totalSlots) + " slots");
    return result;
}

// ── Bjorklund's Algorithm ────────────────────────────────────────────────

std::vector<bool> PatternGenerator::bjorklund(int steps, int hits)
{
    if (steps <= 0) return {};
    hits = juce::jlimit(0, steps, hits);

    // Build using Bjorklund's binary sequence distribution
    std::vector<std::vector<bool>> groups;
    groups.reserve(static_cast<size_t>(steps));

    for (int i = 0; i < steps; ++i)
        groups.push_back({ i < hits });

    // Iteratively merge remainder groups
    while (true)
    {
        // Count leading trues and trailing falses
        int numOnes = 0, numZeros = 0;
        for (auto& g : groups)
        {
            if (g.back()) numOnes++;
            else numZeros++;
        }
        if (numZeros <= 1 || numOnes <= 1) break;

        int mergeCount = std::min(numOnes, numZeros);
        std::vector<std::vector<bool>> newGroups;
        newGroups.reserve(groups.size());

        // Merge front ones with back zeros
        for (int i = 0; i < mergeCount; ++i)
        {
            auto merged = groups[i];
            auto& tail = groups[groups.size() - 1 - i];
            merged.insert(merged.end(), tail.begin(), tail.end());
            newGroups.push_back(std::move(merged));
        }
        // Add remaining unmerged
        for (int i = mergeCount; i < static_cast<int>(groups.size()) - mergeCount; ++i)
            newGroups.push_back(groups[i]);

        groups = std::move(newGroups);
    }

    // Flatten
    std::vector<bool> result;
    result.reserve(static_cast<size_t>(steps));
    for (auto& g : groups)
        for (bool b : g)
            result.push_back(b);

    return result;
}

// ── Euclidean Pattern Generation ─────────────────────────────────────────

std::vector<PatternSlot> PatternGenerator::generateEuclidean(
    const std::vector<SegmentRef>& availableSegments,
    int totalSteps,
    int numHits,
    int rotation)
{
    juce::Logger::writeToLog("[PatternGenerator] === generateEuclidean ===");
    juce::Logger::writeToLog("[PatternGenerator]   steps=" + juce::String(totalSteps)
                             + " hits=" + juce::String(numHits)
                             + " rotation=" + juce::String(rotation));

    auto rhythm = bjorklund(totalSteps, numHits);

    // Apply rotation
    if (rotation > 0 && !rhythm.empty())
    {
        rotation = rotation % static_cast<int>(rhythm.size());
        std::rotate(rhythm.begin(), rhythm.begin() + rotation, rhythm.end());
    }

    std::vector<PatternSlot> result(static_cast<size_t>(totalSteps));

    if (availableSegments.empty()) return result;

    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
    auto picked = pickUniqueSegments(availableSegments,
        std::min(static_cast<int>(availableSegments.size()), 4));

    int segCycle = 0;
    for (int i = 0; i < totalSteps; ++i)
    {
        auto& slot = result[static_cast<size_t>(i)];
        if (rhythm[static_cast<size_t>(i)])
        {
            const auto& seg = picked[static_cast<size_t>(segCycle % picked.size())];
            slot.stemIndex = seg.stemIndex;
            slot.segmentIndex = seg.segmentIndex;
            slot.empty = false;
            slot.role = static_cast<char>('A' + (segCycle % 4));
            ++segCycle;
        }
    }

    juce::Logger::writeToLog("[PatternGenerator]   Euclidean: " + juce::String(numHits)
                             + " hits placed across " + juce::String(totalSteps) + " steps");
    return result;
}

// ── Mutation ─────────────────────────────────────────────────────────────

void PatternGenerator::mutate(std::vector<PatternSlot>& slots,
                               const std::vector<SegmentRef>& availableSegments,
                               float intensity)
{
    if (slots.empty()) return;

    // Use a static thread_local RNG so consecutive (spam-clicked) calls
    // each produce a DIFFERENT result. Seeding from juce::Time per call
    // collided within a millisecond — "spam mutate" then barely changed
    // anything because the same seed yielded the same mutation choices.
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    int mutations = 0;

    // Build an index list of NON-EMPTY slots so the "swap two existing slots"
    // mutation can pick partners from the current pattern.
    std::vector<int> nonEmptyIdx;
    nonEmptyIdx.reserve(slots.size());
    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
        if (!slots[(size_t) i].empty)
            nonEmptyIdx.push_back(i);

    if (nonEmptyIdx.empty()) return;

    for (auto& slot : slots)
    {
        if (chance(rng) > intensity) continue;
        if (slot.empty) continue;

        // Mutate is an FX layer applied IN PLACE — it never rearranges
        // the pattern, swaps slots, or changes which segment plays where.
        // The arrangement is set by Shuffle or manual placement. Mutate
        // only varies the PERFORMANCE of existing slots: reverse, pitch,
        // velocity, speed, trim.
        std::uniform_int_distribution<int> mutType(0, 4);
        switch (mutType(rng))
        {
            case 0: // Toggle reverse
                slot.reverse = !slot.reverse;
                break;
            case 1: // Random pitch shift ±3 semitones
            {
                std::uniform_real_distribution<float> pitchDist(-3.0f, 3.0f);
                slot.pitchSemitones = std::round(pitchDist(rng));
                break;
            }
            case 2: // Velocity variation
            {
                std::uniform_real_distribution<float> velDist(0.4f, 1.0f);
                slot.velocity = velDist(rng);
                break;
            }
            case 3: // Speed variation (0.5x – 2.0x, biased toward 1x)
            {
                static const float speeds[] = { 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.25f, 1.5f, 2.0f };
                std::uniform_int_distribution<int> sd(0, 7);
                slot.speed = speeds[sd(rng)];
                break;
            }
            case 4: // Trim window — re-frame the segment without changing source
            {
                std::uniform_real_distribution<float> trimDist(0.0f, 0.3f);
                slot.trimStart = trimDist(rng);
                slot.trimEnd   = 1.0f - trimDist(rng);
                if (slot.trimEnd <= slot.trimStart + 0.1f)
                {
                    slot.trimStart = 0.0f;
                    slot.trimEnd   = 1.0f;
                }
                break;
            }
        }
        ++mutations;
    }

    // Guarantee progress: if intensity-roulette skipped every slot, force
    // ONE random non-empty slot to receive an FX mutation so spam-click
    // always produces a visible change. Same in-place rules — never
    // touches which segment is in which slot.
    if (mutations == 0 && !nonEmptyIdx.empty())
    {
        std::uniform_int_distribution<int> idxDist(
            0, static_cast<int>(nonEmptyIdx.size()) - 1);
        auto& slot = slots[(size_t) nonEmptyIdx[(size_t) idxDist(rng)]];
        std::uniform_int_distribution<int> mutType(0, 4);
        switch (mutType(rng))
        {
            case 0: slot.reverse = !slot.reverse; break;
            case 1: { std::uniform_real_distribution<float> p(-3.0f, 3.0f);
                      slot.pitchSemitones = std::round(p(rng)); } break;
            case 2: { std::uniform_real_distribution<float> v(0.4f, 1.0f);
                      slot.velocity = v(rng); } break;
            case 3: { static const float speeds[] = { 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.25f, 1.5f, 2.0f };
                      std::uniform_int_distribution<int> sd(0, 7);
                      slot.speed = speeds[sd(rng)]; } break;
            case 4: { std::uniform_real_distribution<float> t(0.0f, 0.3f);
                      slot.trimStart = t(rng); slot.trimEnd = 1.0f - t(rng);
                      if (slot.trimEnd <= slot.trimStart + 0.1f)
                      { slot.trimStart = 0.0f; slot.trimEnd = 1.0f; } } break;
        }
        ++mutations;
    }

    juce::Logger::writeToLog("[PatternGenerator] Mutated " + juce::String(mutations)
                             + " of " + juce::String(static_cast<int>(nonEmptyIdx.size())) + " non-empty slots"
                             + " (intensity=" + juce::String(intensity, 2) + ")");
}

// ── Velocity Curves ──────────────────────────────────────────────────────

void PatternGenerator::applyVelocityCurve(std::vector<PatternSlot>& slots,
                                            const juce::String& curveType)
{
    if (slots.empty()) return;
    int n = static_cast<int>(slots.size());

    if (curveType == "crescendo")
    {
        for (int i = 0; i < n; ++i)
            slots[i].velocity = 0.3f + 0.7f * (static_cast<float>(i) / (n - 1));
    }
    else if (curveType == "decrescendo")
    {
        for (int i = 0; i < n; ++i)
            slots[i].velocity = 1.0f - 0.7f * (static_cast<float>(i) / (n - 1));
    }
    else if (curveType == "accent2")
    {
        for (int i = 0; i < n; ++i)
            slots[i].velocity = (i % 2 == 0) ? 1.0f : 0.5f;
    }
    else if (curveType == "accent4")
    {
        for (int i = 0; i < n; ++i)
            slots[i].velocity = (i % 4 == 0) ? 1.0f : 0.55f;
    }
    else if (curveType == "random")
    {
        std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
        std::uniform_real_distribution<float> dist(0.3f, 1.0f);
        for (auto& slot : slots)
            slot.velocity = dist(rng);
    }

    juce::Logger::writeToLog("[PatternGenerator] Applied velocity curve: " + curveType);
}

// ── Random Reverse ───────────────────────────────────────────────────────

void PatternGenerator::randomReverse(std::vector<PatternSlot>& slots, float probability)
{
    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    int count = 0;

    for (auto& slot : slots)
    {
        if (!slot.empty && chance(rng) < probability)
        {
            slot.reverse = true;
            ++count;
        }
    }

    juce::Logger::writeToLog("[PatternGenerator] Random reverse: " + juce::String(count)
                             + " slots reversed (prob=" + juce::String(probability, 2) + ")");
}

// ── Random Pitch ─────────────────────────────────────────────────────────

void PatternGenerator::randomPitch(std::vector<PatternSlot>& slots, float maxSemitones)
{
    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
    std::uniform_real_distribution<float> dist(-maxSemitones, maxSemitones);

    for (auto& slot : slots)
    {
        if (!slot.empty)
            slot.pitchSemitones = std::round(dist(rng));
    }

    juce::Logger::writeToLog("[PatternGenerator] Random pitch: max=" + juce::String(maxSemitones, 1) + " st");
}

// ── Helpers ───────────────────────────────────────────────────────────────

juce::String PatternGenerator::getPatternString(PatternType type)
{
    switch (type)
    {
        case PatternType::AABB:      return "AABB";
        case PatternType::ABAB:      return "ABAB";
        case PatternType::AAAB:      return "AAAB";
        case PatternType::ABAC:      return "ABAC";
        case PatternType::AABA:      return "AABA";
        case PatternType::ABCD:      return "ABCD";
        case PatternType::Random:    return "RAND";
        case PatternType::Euclidean: return "EUCLID";
    }
    return "???";
}

std::vector<std::pair<PatternGenerator::PatternType, juce::String>> PatternGenerator::getAllPatterns()
{
    return {
        { PatternType::AABB,      "AABB" },
        { PatternType::ABAB,      "ABAB" },
        { PatternType::AAAB,      "AAAB" },
        { PatternType::ABAC,      "ABAC" },
        { PatternType::AABA,      "AABA" },
        { PatternType::ABCD,      "ABCD" },
        { PatternType::Random,    "RAND" },
        { PatternType::Euclidean, "EUCLID" },
    };
}

int PatternGenerator::getRequiredRoles(PatternType type)
{
    switch (type)
    {
        case PatternType::AABB:      return 2;
        case PatternType::ABAB:      return 2;
        case PatternType::AAAB:      return 2;
        case PatternType::AABA:      return 2;
        case PatternType::ABAC:      return 3;
        case PatternType::ABCD:      return 4;
        case PatternType::Random:    return 4;
        case PatternType::Euclidean: return 4;
    }
    return 2;
}

// ── Tempo-Grid Sub-Slicing ───────────────────────────────────────────────

void PatternGenerator::applySubSlicing(std::vector<PatternSlot>& slots,
                                        const SModeSegmentModel& /*model*/,
                                        double bpm,
                                        double /*sampleRate*/,
                                        float /*subdivBeats*/)
{
    if (bpm <= 0.0) return;

    // With the integer cell grid, sub-slicing randomizes cellSpan (1-4 cells).
    // gridCell positions are assumed to be already assigned by the caller.
    std::mt19937 rng(static_cast<unsigned>(juce::Time::currentTimeMillis()));
    std::uniform_real_distribution<float> shouldSlice(0.0f, 1.0f);
    const int possibleSpans[] = { 1, 1, 1, 2, 2, 4 };
    const int numSpans = 6;

    int subSliced = 0;

    for (auto& slot : slots)
    {
        if (slot.empty) continue;
        if (shouldSlice(rng) > 0.6f) continue;  // ~60% chance

        std::uniform_int_distribution<int> spanPick(0, numSpans - 1);
        slot.cellSpan = possibleSpans[spanPick(rng)];
        ++subSliced;
    }

    juce::Logger::writeToLog("[PatternGenerator] Sub-sliced " + juce::String(subSliced)
                             + " of " + juce::String(static_cast<int>(slots.size()))
                             + " slots with varied cellSpans (BPM=" + juce::String(bpm, 1) + ")");
}

} // namespace samplar
