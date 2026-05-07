#include "TempoCache.h"
#include "../../utils/DataPaths.h"
#include <juce_cryptography/juce_cryptography.h>

namespace samplar {

juce::File TempoCache::getCacheDirectory()
{
    auto dir = DataPaths::getUserDataDirectory().getChildFile("tempo_cache");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

juce::File TempoCache::keyToFile(const juce::String& key)
{
    return getCacheDirectory().getChildFile(key + ".json");
}

juce::String TempoCache::computeKey(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();
    if (numSamples <= 0 || numCh <= 0)
        return {};

    // Hash metadata first.
    juce::MemoryBlock payload;
    {
        juce::MemoryOutputStream mos(payload, false);
        mos.writeInt(numSamples);
        mos.writeInt(numCh);
        mos.writeDouble(sampleRate);

        // Sample chunk size: 64 KB of floats at most, from channel 0.
        const int chunkFloats = juce::jmin(numSamples, 16384);  // 16384 * 4 bytes = 64 KB
        const float* ch0 = buffer.getReadPointer(0);

        // First chunk
        mos.write(ch0, static_cast<size_t>(chunkFloats) * sizeof(float));

        // Last chunk (skip if the buffer is small — head already covers it)
        if (numSamples > chunkFloats * 2)
        {
            const float* tail = ch0 + (numSamples - chunkFloats);
            mos.write(tail, static_cast<size_t>(chunkFloats) * sizeof(float));
        }
    }

    juce::SHA256 sha(payload.getData(), payload.getSize());
    return sha.toHexString();
}

juce::var TempoCache::resultToVar(const BPMResult& r)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("bpm",               r.bpm);
    obj->setProperty("confidence",        r.confidence);
    obj->setProperty("fftBPM",            r.fftBPM);
    obj->setProperty("fftConfidence",     r.fftConfidence);
    obj->setProperty("dwtBPM",            r.dwtBPM);
    obj->setProperty("dwtConfidence",     r.dwtConfidence);
    obj->setProperty("detectorSource",    r.detectorSource);
    obj->setProperty("lteBPM",            r.lteBPM);
    obj->setProperty("lteScore",          r.lteScore);
    obj->setProperty("lteDetectedAsLoop", r.lteDetectedAsLoop);
    obj->setProperty("lteTooLong",        r.lteTooLong);
    obj->setProperty("correctedBPM",      r.correctedBPM);
    obj->setProperty("octaveCorrected",   r.octaveCorrected);
    obj->setProperty("cachedAtMs",        (juce::int64) juce::Time::currentTimeMillis());
    obj->setProperty("schemaVersion",     1);
    return juce::var(obj);
}

BPMResult TempoCache::varToResult(const juce::var& v)
{
    BPMResult r;
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return r;

    r.bpm               = (float)  (double) obj->getProperty("bpm");
    r.confidence        = (float)  (double) obj->getProperty("confidence");
    r.fftBPM            = (float)  (double) obj->getProperty("fftBPM");
    r.fftConfidence     = (float)  (double) obj->getProperty("fftConfidence");
    r.dwtBPM            = (float)  (double) obj->getProperty("dwtBPM");
    r.dwtConfidence     = (float)  (double) obj->getProperty("dwtConfidence");
    r.detectorSource    =          obj->getProperty("detectorSource").toString();
    r.lteBPM            = (float)  (double) obj->getProperty("lteBPM");
    r.lteScore          = (float)  (double) obj->getProperty("lteScore");
    r.lteDetectedAsLoop = (bool)   obj->getProperty("lteDetectedAsLoop");
    r.lteTooLong        = (bool)   obj->getProperty("lteTooLong");
    r.correctedBPM      = (float)  (double) obj->getProperty("correctedBPM");
    r.octaveCorrected   = (bool)   obj->getProperty("octaveCorrected");
    // odf vector intentionally not restored — downstream loop detection recomputes.
    return r;
}

std::optional<BPMResult> TempoCache::get(const juce::String& key)
{
    if (key.isEmpty()) return std::nullopt;

    auto file = keyToFile(key);
    if (! file.existsAsFile()) return std::nullopt;

    auto parsed = juce::JSON::parse(file);
    if (! parsed.isObject()) return std::nullopt;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return std::nullopt;

    const int schema = (int) obj->getProperty("schemaVersion");
    if (schema != 1)
    {
        juce::Logger::writeToLog("[TempoCache] schema mismatch (" + juce::String(schema)
                                 + "); ignoring " + file.getFileName());
        return std::nullopt;
    }

    return varToResult(parsed);
}

void TempoCache::put(const juce::String& key, const BPMResult& result)
{
    if (key.isEmpty()) return;
    if (result.isManualOverride)
        return;  // Don't cache user-typed BPM values.

    auto file = keyToFile(key);
    file.replaceWithText(juce::JSON::toString(resultToVar(result), /*allOnOneLine*/ false));
}

void TempoCache::invalidate(const juce::String& key)
{
    if (key.isEmpty()) return;
    auto file = keyToFile(key);
    if (file.existsAsFile()) file.deleteFile();
}

} // namespace samplar
