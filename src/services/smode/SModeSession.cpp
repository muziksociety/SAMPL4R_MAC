#include "SModeSession.h"
#include <cmath>
#include <limits>

namespace samplar {
namespace session {

namespace
{
    const juce::Identifier kSession     { "session" };
    const juce::Identifier kStem        { "stem" };
    const juce::Identifier kSeg         { "seg" };
    const juce::Identifier kPlay        { "play" };
    const juce::Identifier kStemParam   { "stemParam" };
    const juce::Identifier kCc          { "cc" };

    void log (const juce::String& s) { juce::Logger::writeToLog ("[SModeSession] " + s); }

    double getD (const juce::ValueTree& t, const juce::Identifier& id, double def)
    {
        const auto& v = t.getProperty (id);
        if (v.isVoid() || v.isUndefined()) return def;
        const double d = static_cast<double> (v);
        return std::isfinite (d) ? d : def;
    }

    // Integers saved as doubles (or hand-edited projects) can be NaN / inf /
    // 1e999: casting those to int is UB, so they read as the default.
    int getI (const juce::ValueTree& t, const juce::Identifier& id, int def)
    {
        const auto& v = t.getProperty (id);
        if (v.isVoid() || v.isUndefined()) return def;
        if (v.isDouble())
        {
            const double d = static_cast<double> (v);
            if (! std::isfinite (d) || d < (double) std::numeric_limits<int>::min()
                || d > (double) std::numeric_limits<int>::max())
                return def;
            return static_cast<int> (d);
        }
        return static_cast<int> (v);
    }

    juce::int64 getI64 (const juce::ValueTree& t, const juce::Identifier& id, juce::int64 def)
    {
        const auto& v = t.getProperty (id);
        if (v.isVoid() || v.isUndefined()) return def;
        if (v.isDouble())
        {
            const double d = static_cast<double> (v);
            if (! std::isfinite (d) || std::abs (d) > 9.0e18) return def;
            return static_cast<juce::int64> (d);
        }
        return static_cast<juce::int64> (v);
    }

    // Enum stored as int: out of range -> default (never cast onto another value).
    int getEnum (const juce::ValueTree& t, const juce::Identifier& id, int maxValue, int def)
    {
        const int v = getI (t, id, def);
        return (v >= 0 && v <= maxValue) ? v : def;
    }

    constexpr int kMaxVelCurve = 3;   // MidiMappingService::VelocityCurve::SoftKnee
    constexpr int kMaxModDest  = 4;   // MidiMappingService::ModDestination::Pan

    bool getB (const juce::ValueTree& t, const juce::Identifier& id, bool def)
    {
        const auto& v = t.getProperty (id);
        return (v.isVoid() || v.isUndefined()) ? def : static_cast<bool> (v);
    }

    float clampF (double v, double lo, double hi, double def)
    {
        if (! std::isfinite (v)) v = def;
        return static_cast<float> (juce::jlimit (lo, hi, v));
    }
}

//==============================================================================
bool StemPlayParams::operator== (const StemPlayParams& o) const
{
    return pitch == o.pitch && velCurve == o.velCurve && bendDest == o.bendDest
        && wheelDest == o.wheelDest && atDest == o.atDest && bendRange == o.bendRange
        && wheelRange == o.wheelRange && atRange == o.atRange;
}

bool PlaySettings::operator== (const PlaySettings& o) const
{
    return mono == o.mono && gate == o.gate && loopGate == o.loopGate
        && transportLoop == o.transportLoop && dawSync == o.dawSync && stems == o.stems
        && ccBindings == o.ccBindings;
}

juce::ValueTree writePlay (const PlaySettings& p)
{
    juce::ValueTree t (kPlay);
    t.setProperty ("mono", p.mono, nullptr);
    t.setProperty ("gate", p.gate, nullptr);
    t.setProperty ("loopGate", p.loopGate, nullptr);
    t.setProperty ("transportLoop", p.transportLoop, nullptr);
    t.setProperty ("dawSync", p.dawSync, nullptr);
    for (int i = 0; i < (int) p.stems.size(); ++i)
    {
        const auto& s = p.stems[(size_t) i];
        juce::ValueTree c (kStemParam);
        c.setProperty ("index", i, nullptr);
        c.setProperty ("pitch", (double) s.pitch, nullptr);
        c.setProperty ("vc",  s.velCurve, nullptr);
        c.setProperty ("pbd", s.bendDest, nullptr);
        c.setProperty ("mwd", s.wheelDest, nullptr);
        c.setProperty ("atd", s.atDest, nullptr);
        c.setProperty ("pbr", (double) s.bendRange, nullptr);
        c.setProperty ("mwr", (double) s.wheelRange, nullptr);
        c.setProperty ("atr", (double) s.atRange, nullptr);
        t.appendChild (c, nullptr);
    }
    for (const auto& b : p.ccBindings)
    {
        juce::ValueTree c (kCc);
        c.setProperty ("channel", b.channel, nullptr);
        c.setProperty ("cc", b.cc, nullptr);
        c.setProperty ("target", b.target, nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}

PlaySettings readPlay (const juce::ValueTree& t)
{
    PlaySettings p;
    if (! t.isValid())
        return p;
    p.mono          = getB (t, "mono", false);
    p.gate          = getB (t, "gate", false);
    p.loopGate      = getB (t, "loopGate", false);
    p.transportLoop = getB (t, "transportLoop", false);
    p.dawSync       = getB (t, "dawSync", false);          // schema 1: absent -> off
    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        const auto c = t.getChild (i);
        if (c.hasType (kCc))
        {
            // schema 2. Invalid entries are dropped, never clamped onto another CC.
            CcBinding b;
            b.channel = getI (c, "channel", -1);
            b.cc      = getI (c, "cc", -1);
            b.target  = c.getProperty ("target").toString().trim();
            if (b.channel >= 0 && b.channel <= 16 && b.cc >= 0 && b.cc <= 127 && b.target.isNotEmpty()
                && (int) p.ccBindings.size() < 512)
                p.ccBindings.push_back (b);
            else
                log ("readPlay: invalid <cc> binding dropped");
            continue;
        }
        if (! c.hasType (kStemParam)) continue;
        const int idx = getI (c, "index", -1);
        if (idx < 0 || idx >= (int) p.stems.size()) continue;
        auto& s = p.stems[(size_t) idx];
        s.pitch      = clampF (getD (c, "pitch", 0.0), -24.0, 24.0, 0.0);
        s.velCurve   = getEnum (c, "vc",  kMaxVelCurve, 0);
        s.bendDest   = getEnum (c, "pbd", kMaxModDest, 1);
        s.wheelDest  = getEnum (c, "mwd", kMaxModDest, 0);
        s.atDest     = getEnum (c, "atd", kMaxModDest, 0);
        s.bendRange  = clampF (getD (c, "pbr", 2.0),  0.0, 48.0, 2.0);
        s.wheelRange = clampF (getD (c, "mwr", 12.0), 0.0, 48.0, 12.0);
        s.atRange    = clampF (getD (c, "atr", 12.0), 0.0, 48.0, 12.0);
    }
    return p;
}

PlaySettings readLegacyPlay (const juce::ValueTree& stemPitch, const juce::ValueTree& stemMod)
{
    // Pre-schema-2 page bindings (review N-2/N-18): <smodeStemPitch p0..p7/> and
    // <smodeStemMod vc0 pbd0 mwd0 atd0 pbr0 mwr0 atr0 .../>. Same clamps as readPlay.
    PlaySettings p;
    for (int i = 0; i < (int) p.stems.size(); ++i)
    {
        auto& s = p.stems[(size_t) i];
        const juce::String suf (i);
        if (stemPitch.isValid())
            s.pitch = clampF (getD (stemPitch, "p" + suf, 0.0), -24.0, 24.0, 0.0);
        if (stemMod.isValid())
        {
            s.velCurve   = getEnum (stemMod, "vc"  + suf, kMaxVelCurve, 0);
            s.bendDest   = getEnum (stemMod, "pbd" + suf, kMaxModDest, 1);
            s.wheelDest  = getEnum (stemMod, "mwd" + suf, kMaxModDest, 0);
            s.atDest     = getEnum (stemMod, "atd" + suf, kMaxModDest, 0);
            s.bendRange  = clampF (getD (stemMod, "pbr" + suf, 2.0),  0.0, 48.0, 2.0);
            s.wheelRange = clampF (getD (stemMod, "mwr" + suf, 12.0), 0.0, 48.0, 12.0);
            s.atRange    = clampF (getD (stemMod, "atr" + suf, 12.0), 0.0, 48.0, 12.0);
        }
    }
    return p;
}

PlaySettings selectPlaySettings (const juce::ValueTree& smodeNode,
                                 const juce::ValueTree& legacyStemPitch,
                                 const juce::ValueTree& legacyStemMod,
                                 juce::String* sourceOut)
{
    juce::String src;
    PlaySettings p;
    const auto play = smodeNode.getChildWithName (kPlay);
    if (play.isValid())
    {
        p = readPlay (play);
        src = (legacyStemPitch.isValid() || legacyStemMod.isValid()) ? "play (stale legacy nodes ignored)" : "play";
    }
    else if (legacyStemPitch.isValid() || legacyStemMod.isValid())
    {
        p = readLegacyPlay (legacyStemPitch, legacyStemMod);
        src = juce::String ("legacy") + (legacyStemPitch.isValid() ? " smodeStemPitch" : "")
                                      + (legacyStemMod.isValid() ? " smodeStemMod" : "");
    }
    else
    {
        src = "defaults (no play settings saved)";
    }
    log ("selectPlaySettings: " + src);
    if (sourceOut != nullptr) *sourceOut = src;
    return p;
}

//==============================================================================
juce::String stemCacheKeyFor (const std::vector<juce::File>& stemFiles)
{
    if (stemFiles.empty()) return {};
    const auto dir = stemFiles.front().getParentDirectory();
    if (dir.getParentDirectory().getFileName().equalsIgnoreCase ("stem_cache_onnx"))
        return dir.getFileName();
    return {};
}

juce::String describeModel (const SModeSegmentModel& model)
{
    juce::String d;
    d << "stems=" << model.getStemCount()
      << " bpm=" << juce::String (model.getDetectedTempo(), 3)
      << " grid=" << juce::String (model.getGridOffsetSeconds(), 4);
    for (int s = 0; s < model.getStemCount(); ++s)
    {
        const auto* st = model.getStem (s);
        if (st == nullptr) continue;
        d << " | s" << s << (st->linked ? " L" : "") << (st->solo ? " S" : "") << (st->mute ? " M" : "")
          << " v" << juce::String (st->volume, 3) << " n=" << (int) st->segments.size();
        for (const auto& g : st->segments)
            d << " [" << (juce::int64) g.startSample << "-" << (juce::int64) g.endSample << " #" << g.midiNote
              << " p" << g.effects.pitchSemitones << (g.effects.loop ? " loop" : "")
              << (g.effects.attackMs > 0.0f ? " a" + juce::String (g.effects.attackMs, 1) : juce::String())
              << (g.effects.releaseMs > 0.0f ? " r" + juce::String (g.effects.releaseMs, 1) : juce::String())
              << (g.effects.triggerMode != 0 ? " t" + juce::String (g.effects.triggerMode) : juce::String()) << "]";
    }
    return d;
}

juce::ValueTree writeSession (const SModeSegmentModel& model, const SessionSource& src)
{
    juce::ValueTree t (kSession);
    t.setProperty ("schema", kSchema, nullptr);
    const auto& f = src.sourceFile;
    t.setProperty ("sourceFile", f.getFullPathName(), nullptr);
    if (f.existsAsFile())
    {
        t.setProperty ("sourceSize",  (juce::int64) f.getSize(), nullptr);
        t.setProperty ("sourceMtime", f.getLastModificationTime().toMilliseconds(), nullptr);
    }
    if (src.sourceFingerprint.isNotEmpty())
        t.setProperty ("sourceFp", src.sourceFingerprint, nullptr);   // optional (relink check, N-10)
    t.setProperty ("stemCacheKey", stemCacheKeyFor (src.stemFiles), nullptr);
    t.setProperty ("stemCount",  model.getStemCount(), nullptr);
    t.setProperty ("sampleRate", model.getSampleRate(), nullptr);
    t.setProperty ("bpm",        model.getDetectedTempo(), nullptr);
    t.setProperty ("gridOffset", model.getGridOffsetSeconds(), nullptr);
    t.setProperty ("uiExtra",    src.uiExtraJson, nullptr);

    int segs = 0;
    for (int s = 0; s < model.getStemCount(); ++s)
    {
        const auto* stem = model.getStem (s);
        if (stem == nullptr) continue;
        juce::ValueTree st (kStem);
        st.setProperty ("index",  s, nullptr);
        st.setProperty ("name",   stem->name, nullptr);
        st.setProperty ("file",   s < (int) src.stemFiles.size() ? src.stemFiles[(size_t) s].getFullPathName() : juce::String(), nullptr);
        st.setProperty ("linked", stem->linked, nullptr);
        st.setProperty ("solo",   stem->solo, nullptr);
        st.setProperty ("mute",   stem->mute, nullptr);
        st.setProperty ("volume", (double) stem->volume, nullptr);
        for (const auto& seg : stem->segments)
        {
            juce::ValueTree g (kSeg);
            g.setProperty ("start",  (juce::int64) seg.startSample, nullptr);
            g.setProperty ("end",    (juce::int64) seg.endSample, nullptr);
            g.setProperty ("note",   seg.midiNote, nullptr);
            g.setProperty ("pitch",  seg.effects.pitchSemitones, nullptr);
            g.setProperty ("vol",    (double) seg.effects.volume, nullptr);
            g.setProperty ("pan",    (double) seg.effects.pan, nullptr);
            g.setProperty ("cutoff", (double) seg.effects.filterCutoff, nullptr);
            g.setProperty ("hp",     seg.effects.filterIsHighPass, nullptr);
            g.setProperty ("rev",    seg.effects.reverse, nullptr);
            g.setProperty ("loop",   seg.effects.loop, nullptr);
            g.setProperty ("atk",    (double) seg.effects.attackMs, nullptr);
            g.setProperty ("rel",    (double) seg.effects.releaseMs, nullptr);
            g.setProperty ("trig",   seg.effects.triggerMode, nullptr);
            st.appendChild (g, nullptr);
            ++segs;
        }
        t.appendChild (st, nullptr);
    }
    log ("writeSession: stems=" + juce::String (model.getStemCount()) + " segments=" + juce::String (segs)
         + " bpm=" + juce::String (model.getDetectedTempo(), 3) + " gridOffset=" + juce::String (model.getGridOffsetSeconds(), 4)
         + " uiExtra=" + juce::String (src.uiExtraJson.length()) + " chars");
    return t;
}

SessionSource readSessionSource (const juce::ValueTree& t)
{
    SessionSource src;
    if (! t.isValid())
        return src;
    const auto path = t.getProperty ("sourceFile").toString();
    if (juce::File::isAbsolutePath (path))
        src.sourceFile = juce::File (path);
    src.uiExtraJson = t.getProperty ("uiExtra").toString();
    src.stemCount   = juce::jlimit (0, engine::kMaxStems, getI (t, "stemCount", 0));

    // Stem files in stem-index order (a missing / non-absolute path -> empty File).
    std::vector<juce::File> files;
    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        const auto c = t.getChild (i);
        if (! c.hasType (kStem)) continue;
        const int idx = getI (c, "index", (int) files.size());
        if (idx < 0 || idx >= engine::kMaxStems) continue;
        if ((int) files.size() <= idx) files.resize ((size_t) idx + 1);
        const auto p = c.getProperty ("file").toString();
        if (juce::File::isAbsolutePath (p))
            files[(size_t) idx] = juce::File (p);
    }
    src.stemFiles = std::move (files);
    if (src.stemCount == 0)
        src.stemCount = (int) src.stemFiles.size();
    return src;
}

int countSegments (const juce::ValueTree& t)
{
    int n = 0;
    for (int i = 0; i < t.getNumChildren(); ++i)
        if (t.getChild (i).hasType (kStem))
            for (int j = 0; j < t.getChild (i).getNumChildren(); ++j)
                if (t.getChild (i).getChild (j).hasType (kSeg)) ++n;
    return n;
}

int applySessionToModel (const juce::ValueTree& t, SModeSegmentModel& model, ApplyReport* report)
{
    if (report != nullptr) *report = {};
    if (! t.isValid())
        return 0;
    const int schema = getI (t, "schema", kSchema);
    if (schema > kSchema)
        log ("applySessionToModel: schema " + juce::String (schema) + " is newer than " + juce::String (kSchema) + " - reading best-effort");

    const double savedRate = getD (t, "sampleRate", 0.0);
    const double modelRate = model.getSampleRate();
    const double scale = (savedRate > 0.0 && modelRate > 0.0) ? modelRate / savedRate : 1.0;
    if (std::abs (scale - 1.0) > 1e-9)
        log ("applySessionToModel: rescaling positions " + juce::String (savedRate, 1) + " -> " + juce::String (modelRate, 1) + " Hz");

    const double bpm = getD (t, "bpm", 0.0);
    model.setDetectedTempo (bpm > 0.0 && bpm < 1000.0 ? bpm : 0.0);
    model.setGridOffsetSeconds (juce::jmax (0.0, getD (t, "gridOffset", 0.0)));

    int applied = 0, dropped = 0, clamped = 0, skipped = 0;
    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        const auto st = t.getChild (i);
        if (! st.hasType (kStem)) continue;
        const int idx = getI (st, "index", -1);
        auto* stem = model.getStem (idx);
        if (stem == nullptr)
        {
            int n = 0;
            for (int j = 0; j < st.getNumChildren(); ++j)
                if (st.getChild (j).hasType (kSeg)) ++n;
            skipped += n;
            log ("applySessionToModel: stem " + juce::String (idx) + " not loaded - its " + juce::String (n)
                 + " segments are skipped");
            continue;
        }
        stem->linked = getB (st, "linked", false);
        stem->solo   = getB (st, "solo", false);
        stem->mute   = getB (st, "mute", false);
        stem->volume = clampF (getD (st, "volume", 1.0), 0.0, 4.0, 1.0);

        const juce::int64 len = stem->audioBuffer.getNumSamples();
        stem->segments.clear();
        for (int j = 0; j < st.getNumChildren(); ++j)
        {
            const auto g = st.getChild (j);
            if (! g.hasType (kSeg)) continue;
            Segment seg;
            seg.startSample = (int64_t) std::llround ((double) getI64 (g, "start", 0) * scale);
            seg.endSample   = (int64_t) std::llround ((double) getI64 (g, "end", 0) * scale);
            const auto rawStart = seg.startSample, rawEnd = seg.endSample;
            if (len > 0)
            {
                seg.startSample = juce::jlimit ((int64_t) 0, (int64_t) len, seg.startSample);
                seg.endSample   = juce::jlimit ((int64_t) 0, (int64_t) len, seg.endSample);
            }
            if (seg.startSample < 0 || seg.endSample <= seg.startSample) { ++dropped; continue; }
            if (seg.startSample != rawStart || seg.endSample != rawEnd) ++clamped;
            const int note = getI (g, "note", -1);
            seg.midiNote = (note >= 0 && note < 128) ? note : -1;
            seg.effects.pitchSemitones   = juce::jlimit (-48, 48, getI (g, "pitch", 0));
            seg.effects.volume           = clampF (getD (g, "vol", 1.0), 0.0, 4.0, 1.0);
            seg.effects.pan              = clampF (getD (g, "pan", 0.0), -1.0, 1.0, 0.0);
            seg.effects.filterCutoff     = clampF (getD (g, "cutoff", 1.0), 0.0, 1.0, 1.0);
            seg.effects.filterIsHighPass = getB (g, "hp", false);
            seg.effects.reverse          = getB (g, "rev", false);
            seg.effects.loop             = getB (g, "loop", false);
            seg.effects.attackMs         = clampF (getD (g, "atk", 0.0), 0.0, 2000.0, 0.0);
            seg.effects.releaseMs        = clampF (getD (g, "rel", 0.0), 0.0, 5000.0, 0.0);
            seg.effects.triggerMode      = juce::jlimit (0, 2, getI (g, "trig", 0));
            stem->segments.push_back (seg);
            ++applied;
        }
    }
    log ("applySessionToModel: applied " + juce::String (applied) + " segments (shortened " + juce::String (clamped)
         + ", dropped " + juce::String (dropped) + ", on unloaded stems " + juce::String (skipped)
         + "), bpm=" + juce::String (model.getDetectedTempo(), 3) + " gridOffset=" + juce::String (model.getGridOffsetSeconds(), 4));
    if (report != nullptr)
    {
        report->applied = applied;
        report->clamped = clamped;
        report->dropped = dropped;
        report->skipped = skipped;
    }
    return applied;
}

//==============================================================================
juce::String computeSourceFingerprint (const juce::File& f)
{
    // FNV-1a 64 over (length, first 64 KB, last 64 KB): cheap (128 KB read)
    // and catches a re-rendered / different file with the same name.
    juce::FileInputStream in (f);
    if (! in.openedOk())
        return {};
    const juce::int64 len = in.getTotalLength();
    if (len <= 0)
        return {};
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h] (const void* data, size_t n)
    {
        const auto* p = static_cast<const juce::uint8*> (data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    };
    mix (&len, sizeof (len));
    constexpr int kBlock = 64 * 1024;
    juce::HeapBlock<char> buf ((size_t) kBlock);
    const int first = (int) juce::jmin ((juce::int64) kBlock, len);
    if (in.read (buf.get(), first) != first)
        return {};
    mix (buf.get(), (size_t) first);
    if (len > kBlock)
    {
        const juce::int64 tailStart = juce::jmax ((juce::int64) kBlock, len - kBlock);
        const int tail = (int) (len - tailStart);
        if (! in.setPosition (tailStart) || in.read (buf.get(), tail) != tail)
            return {};
        mix (buf.get(), (size_t) tail);
    }
    return "v1:" + juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

double lastChopEndSeconds (const juce::ValueTree& t)
{
    const double rate = getD (t, "sampleRate", 0.0);
    if (rate <= 0.0)
        return 0.0;
    juce::int64 maxEnd = 0;
    for (int i = 0; i < t.getNumChildren(); ++i)
        if (t.getChild (i).hasType (kStem))
            for (int j = 0; j < t.getChild (i).getNumChildren(); ++j)
            {
                const auto g = t.getChild (i).getChild (j);
                if (g.hasType (kSeg)) maxEnd = juce::jmax (maxEnd, getI64 (g, "end", 0));
            }
    return (double) maxEnd / rate;
}

RelinkCheck checkRelinkCandidate (const juce::ValueTree& t, const juce::File& candidate, double candidateSeconds)
{
    RelinkCheck r;
    const juce::String savedFp = t.getProperty ("sourceFp").toString();
    const juce::int64 savedSize = getI64 (t, "sourceSize", -1);
    if (savedFp.isNotEmpty())
    {
        const auto fp = computeSourceFingerprint (candidate);
        r.contentMismatch = fp != savedFp;
        r.reason = r.contentMismatch ? "content fingerprint differs (" + fp + " vs saved " + savedFp + ")"
                                     : juce::String ("content fingerprint matches");
    }
    else if (savedSize > 0)
    {
        const juce::int64 size = candidate.getSize();
        r.contentMismatch = size != savedSize;
        r.reason = r.contentMismatch ? "file size differs (" + juce::String (size) + " vs saved " + juce::String (savedSize) + " bytes)"
                                     : juce::String ("file size matches (no saved fingerprint)");
    }
    else
    {
        r.identityUnknown = true;
        r.reason = "no saved size / fingerprint (old project)";
    }

    // Duration: the new audio must cover every saved chop.
    const double rate = getD (t, "sampleRate", 0.0);
    r.lastChopEndSeconds = lastChopEndSeconds (t);
    if (candidateSeconds > 0.0 && rate > 0.0)
    {
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            if (! t.getChild (i).hasType (kStem)) continue;
            for (int j = 0; j < t.getChild (i).getNumChildren(); ++j)
            {
                const auto g = t.getChild (i).getChild (j);
                if (! g.hasType (kSeg)) continue;
                const double s = (double) getI64 (g, "start", 0) / rate;
                const double e = (double) getI64 (g, "end", 0) / rate;
                if (s >= candidateSeconds)           ++r.segsDropped;
                else if (e > candidateSeconds + 0.001) ++r.segsClamped;
            }
        }
        r.tooShort = (r.segsDropped + r.segsClamped) > 0;
        if (r.tooShort)
            r.reason << "; audio " << juce::String (candidateSeconds, 2) << " s is shorter than the last chop end "
                     << juce::String (r.lastChopEndSeconds, 2) << " s (" << r.segsClamped << " chops shortened, "
                     << r.segsDropped << " removed)";
    }
    r.accept = ! r.contentMismatch && ! r.tooShort;
    log ("checkRelinkCandidate " + candidate.getFileName() + ": " + juce::String (r.accept ? "ACCEPT" : "REJECT")
         + " - " + r.reason);
    return r;
}

//==============================================================================
const char* SessionState::toString (Status s)
{
    switch (s)
    {
        case Status::Empty:        return "Empty";
        case Status::Restoring:    return "Restoring";
        case Status::Active:       return "Active";
        case Status::MissingAudio: return "MissingAudio";
    }
    return "?";
}

void SessionState::beginRestore (const juce::ValueTree& session)
{
    pending_ = session.createCopy();
    status_  = Status::Restoring;
    message_ = {};
    log ("state -> Restoring (" + juce::String (countSegments (pending_)) + " segments pending)");
}

void SessionState::markMissing (const juce::String& reason)
{
    status_  = Status::MissingAudio;
    message_ = reason;
    log ("state -> MissingAudio (pending kept, " + juce::String (countSegments (pending_)) + " segments): " + reason);
}

void SessionState::completeRestore()
{
    pending_ = {};
    status_  = Status::Active;
    message_ = {};
    log ("state -> Active (restore complete)");
}

void SessionState::setActive()
{
    pending_ = {};
    status_  = Status::Active;
    message_ = {};
    log ("state -> Active (new session)");
}

void SessionState::clear()
{
    pending_ = {};
    status_  = Status::Empty;
    message_ = {};
    log ("state -> Empty");
}

juce::ValueTree SessionState::treeForSave (const std::function<juce::ValueTree()>& buildLive) const
{
    if (pending_.isValid())
    {
        log ("treeForSave: " + juce::String (toString (status_)) + " -> writing the PENDING session back unchanged");
        return pending_.createCopy();
    }
    if (status_ == Status::Active && buildLive)
        return buildLive();
    return {};
}

} // namespace session
} // namespace samplar
