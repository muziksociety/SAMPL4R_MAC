// EngineTests - console test of the realtime playback engine WITHOUT the plugin.
//
//   cmake --build build_engine --config Release --target EngineTests --parallel
//   build_engine/EngineTests_artefacts/Release/EngineTests.exe
//
// Checks (each prints PASS/FAIL + numbers):
//   1. zero heap allocations / frees inside process() across 10k random
//      notes / CC / pitch bend / transport / sequencer / commands, no NaN/Inf
//   2. sample-accurate note start (unpitched: exact; pitched: reported)
//   3. note-off releases GATE and looped voices
//   4. CC123 / CC120 silence
//   5. anti-click fades: no step above a threshold on a full-scale DC stem
//      (start, stop, mono retrigger, steal) + loop crossfade continuity
//   6. offline render == realtime render, bit for bit
//   7. sequencer step timing vs host PPQ (exact), host stop stops everything
//   8. transport with every stem muted: no NaN (A14)
//   9. bypass, RCU snapshot reclamation, stem resampler quality
//  10. UI round: mid-block snapshot switch never frees a snapshot in use (A1),
//      stale UI triggers dropped (A3), per-chop gain / pan / reverse / pitch /
//      attack, sequencer x16 lanes (16 steps per bar, B18), global transpose

#include "../PlaybackEngine.h"
#include "../OfflineRender.h"
#include "../../MicroFade.h"
#include "../../../services/smode/SModeSegmentModel.h"
#include "../SegmentFx.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

#if defined(_MSC_VER)
 #include <malloc.h>
 #include <crtdbg.h>
#endif

// ============================================================================
// Global allocation counter (operator new/delete; plus the CRT hook in Debug,
// which also sees malloc/free from JUCE HeapBlocks).
// ============================================================================
static std::atomic<bool>      gCount  { false };
static std::atomic<long long> gAllocs { 0 };
static std::atomic<long long> gFrees  { 0 };

static void* countedAlloc(std::size_t n)
{
    if (gCount.load(std::memory_order_relaxed)) gAllocs.fetch_add(1);
    if (void* p = std::malloc(n != 0 ? n : 1)) return p;
    throw std::bad_alloc();
}
static void countedFree(void* p) noexcept
{
    if (p != nullptr && gCount.load(std::memory_order_relaxed)) gFrees.fetch_add(1);
    std::free(p);
}
static void* countedAlignedAlloc(std::size_t n, std::align_val_t a)
{
    if (gCount.load(std::memory_order_relaxed)) gAllocs.fetch_add(1);
#if defined(_MSC_VER)
    if (void* p = _aligned_malloc(n != 0 ? n : 1, (std::size_t) a)) return p;
#else
    // std::aligned_alloc needs a non-zero size that is a multiple of the alignment.
    const auto align = (std::size_t) a;
    const auto size  = ((n != 0 ? n : 1) + align - 1) / align * align;
    if (void* p = std::aligned_alloc (align, size)) return p;
#endif
    throw std::bad_alloc();
}
static void countedAlignedFree(void* p) noexcept
{
    if (p != nullptr && gCount.load(std::memory_order_relaxed)) gFrees.fetch_add(1);
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* operator new(std::size_t n)                                   { return countedAlloc(n); }
void* operator new[](std::size_t n)                                 { return countedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept   { try { return countedAlloc(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc(n); } catch (...) { return nullptr; } }
void  operator delete(void* p) noexcept                             { countedFree(p); }
void  operator delete[](void* p) noexcept                           { countedFree(p); }
void  operator delete(void* p, std::size_t) noexcept                { countedFree(p); }
void  operator delete[](void* p, std::size_t) noexcept              { countedFree(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept      { countedFree(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept    { countedFree(p); }
void* operator new(std::size_t n, std::align_val_t a)               { return countedAlignedAlloc(n, a); }
void* operator new[](std::size_t n, std::align_val_t a)             { return countedAlignedAlloc(n, a); }
void  operator delete(void* p, std::align_val_t) noexcept           { countedAlignedFree(p); }
void  operator delete[](void* p, std::align_val_t) noexcept         { countedAlignedFree(p); }
void  operator delete(void* p, std::size_t, std::align_val_t) noexcept   { countedAlignedFree(p); }
void  operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree(p); }

#if defined(_MSC_VER) && defined(_DEBUG)
static int crtAllocHook(int type, void*, size_t, int, long, const unsigned char*, int)
{
    if (gCount.load(std::memory_order_relaxed) && (type == _HOOK_ALLOC || type == _HOOK_REALLOC))
        gAllocs.fetch_add(1);
    return 1;
}
#endif

using namespace samplar;
using namespace samplar::engine;

// ============================================================================
// Tiny harness
// ============================================================================
static int gFailures = 0;
static void report(bool ok, const char* name, const juce::String& detail)
{
    std::printf("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
    std::fflush(stdout);
    if (! ok) ++gFailures;
}

struct Rendered { std::vector<float> L, R; };

static juce::AudioBuffer<float> makeStem(int numSamples, int kind, double sr, uint32_t seed = 1)
{
    juce::AudioBuffer<float> b(2, numSamples);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(-0.5f, 0.5f);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            switch (kind)
            {
                case 0:  d[i] = 1.0f; break;                                                       // DC (worst case for clicks)
                case 1:  d[i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr); break;
                default: d[i] = uni(rng); break;                                                     // noise
            }
        }
    }
    return b;
}

/** Builds a model with `stems` stems of `seconds` at `sr`, all of `kind`. */
static void fillModel(SModeSegmentModel& m, int stems, double seconds, double sr, int kind)
{
    m.clear();
    m.setStemCount(stems);
    m.setSampleRate(sr);
    for (int s = 0; s < stems; ++s)
    {
        m.getStem(s)->name = "Stem" + juce::String(s);
        m.setStemAudio(s, makeStem((int) (seconds * sr), kind, sr, (uint32_t) (s + 7)));
    }
}

static Rendered renderBlocks(PlaybackEngine& eng, int numBlocks, int block,
                             const std::function<void(int, juce::MidiBuffer&, HostInfo&)>& perBlock = {})
{
    Rendered r;
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    for (int b = 0; b < numBlocks; ++b)
    {
        midi.clear();
        HostInfo host;
        if (perBlock) perBlock(b, midi, host);
        buf.clear();
        eng.process(buf, 0, block, &midi, host);
        r.L.insert(r.L.end(), buf.getReadPointer(0), buf.getReadPointer(0) + block);
        r.R.insert(r.R.end(), buf.getReadPointer(1), buf.getReadPointer(1) + block);
    }
    return r;
}

static int firstNonZero(const std::vector<float>& v, int from = 0, float thr = 0.0f)
{
    for (int i = from; i < (int) v.size(); ++i)
        if (std::abs(v[(size_t) i]) > thr) return i;
    return -1;
}

static float maxStep(const std::vector<float>& v)
{
    float m = 0.0f;
    for (size_t i = 1; i < v.size(); ++i) m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

static bool allZeroFrom(const std::vector<float>& v, int from)
{
    for (int i = juce::jmax(0, from); i < (int) v.size(); ++i)
        if (v[(size_t) i] != 0.0f) return false;
    return true;
}

static Command cmd(CmdType t, int i0 = 0, int i1 = 0) { Command c; c.type = t; c.i0 = i0; c.i1 = i1; c.f0 = 1.0f; c.f4 = 1.0f; c.f1 = 1.0f; c.f2 = 1.0f; return c; }

static std::vector<LaneDesc> makeLanes(int stems, int segsPerStem)
{
    std::vector<LaneDesc> lanes;
    for (int l = 0; l < 2; ++l)
    {
        LaneDesc d;
        d.numCells = 8;
        for (int c = 0; c < 8; c += 2)
        {
            SlotDesc s;
            s.empty = false;
            s.stem = (l + c) % stems;
            s.segIndex = (c / 2) % segsPerStem;
            s.gridCell = c;
            s.cellSpan = 1 + (c % 3 == 0 ? 1 : 0);
            s.velocity = 0.8f;
            s.pitchSemis = (c == 4) ? 2.0f : 0.0f;
            s.speed = (c == 6) ? 1.5f : 1.0f;
            s.reverse = (c == 2);
            s.trimStart = 0.1f;
            s.trimEnd = 0.9f;
            d.slots.push_back(s);
        }
        lanes.push_back(d);
    }
    return lanes;
}

// ============================================================================
// 1. Allocation-free render under 10k random events
// ============================================================================
static void testNoAllocations()
{
    const double modelSr = 44100.0, hostSr = 48000.0;
    const int maxBlock = 512;
    SModeSegmentModel model;
    fillModel(model, 4, 12.0, modelSr, 2);
    const int segsPerStem = 8;
    for (int s = 0; s < 4; ++s)
        for (int k = 0; k < segsPerStem; ++k)
        {
            const int idx = model.addSegment(s, (int64_t) (k * 1.3 * modelSr), (int64_t) ((k * 1.3 + 0.2 + 0.1 * k) * modelSr));
            model.assignMidiNote(s, idx, 36 + (s % 2) * 8 + k);   // stems 0/2 and 1/3 share notes (LINK-ALL style)
            if (k % 3 == 2) model.getSegment(s, idx)->effects.loop = true;
        }

    EngineParams params;
    params.stems[1].pitchSemis.store(3.0f);    // pitched stems -> pitch shifter path
    params.stems[3].pitchSemis.store(-5.0f);
    params.seqBpm.store(132.0);
    auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
    auto& eng = *engHolder;
    eng.prepare(hostSr, maxBlock);
    SnapshotManager mgr(eng);
    auto lanes = makeLanes(4, segsPerStem);
    mgr.publish(model.buildPlaybackSnapshot(lanes));

    std::mt19937 rng(1234);
    auto rnd = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    juce::AudioBuffer<float> buf(2, 4096);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    double ppq = 0.0;
    bool hostPlaying = false;
    long long events = 0, blocks = 0, nonFinite = 0;
    float peak = 0.0f;
    long long allocs = 0, frees = 0;
    int publishes = 0;

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(crtAllocHook);
#endif

    while (events < 10000)
    {
        // Block size: mostly <= maxBlock, sometimes larger than prepared (A22).
        const int n = (rnd(0, 20) == 0) ? rnd(maxBlock + 1, 4096) : rnd(1, maxBlock);

        midi.clear();
        const int midiEvents = rnd(0, 6);
        for (int e = 0; e < midiEvents; ++e)
        {
            const int pos = rnd(0, n - 1);
            const int ch  = rnd(1, 4);
            switch (rnd(0, 9))
            {
                case 0: case 1: case 2: midi.addEvent(juce::MidiMessage::noteOn(ch, rnd(34, 70), (juce::uint8) rnd(1, 127)), pos); break;
                case 3: case 4:         midi.addEvent(juce::MidiMessage::noteOff(ch, rnd(34, 70)), pos); break;
                case 5: midi.addEvent(juce::MidiMessage::controllerEvent(ch, 1, rnd(0, 127)), pos); break;
                case 6: midi.addEvent(juce::MidiMessage::pitchWheel(ch, rnd(0, 16383)), pos); break;
                case 7: midi.addEvent(juce::MidiMessage::channelPressureChange(ch, rnd(0, 127)), pos); break;
                case 8: midi.addEvent(juce::MidiMessage::controllerEvent(ch, rnd(0, 1) ? 123 : 120, 0), pos); break;
                default: midi.addEvent(juce::MidiMessage::controllerEvent(ch, 74, rnd(0, 127)), pos); break;
            }
            ++events;
        }

        // UI commands (pushed outside the counted region, like the message thread does).
        if (rnd(0, 6) == 0)
        {
            switch (rnd(0, 9))
            {
                case 0: eng.pushCommand(cmd(CmdType::StopVoices)); break;
                case 1: eng.pushCommand(cmd(CmdType::PlaySegment, rnd(0, 3), rnd(0, segsPerStem - 1))); break;
                case 2: { auto c = cmd(CmdType::PlaySlot, rnd(0, 3), rnd(0, segsPerStem - 1)); c.f0 = 0.2f; c.f1 = 0.7f; c.f2 = 0.75f; c.f3 = 4.0f; c.b0 = true; eng.pushCommand(c); break; }
                case 3: { auto c = cmd(CmdType::TransportStart, rnd(1, 99)); c.d0 = rnd(0, 11); eng.pushCommand(c); break; }
                case 4: eng.pushCommand(cmd(CmdType::TransportStop)); break;
                case 5: eng.pushCommand(cmd(CmdType::SeqStart, 3, rnd(1, 99))); break;
                case 6: eng.pushCommand(cmd(CmdType::SeqStop)); break;
                case 7: eng.pushCommand(cmd(CmdType::AllSoundOff)); break;
                case 8: params.mono.store(rnd(0, 1) == 1); params.gate.store(rnd(0, 1) == 1); params.transportLoop.store(rnd(0, 1) == 1); break;
                default:
                    // Model edit -> new snapshot published while voices play the old one.
                    model.resizeSegment(rnd(0, 3), rnd(0, segsPerStem - 1), (int64_t) rnd(0, 100000), (int64_t) rnd(100001, 400000));
                    model.setMute(rnd(0, 3), rnd(0, 4) == 0);
                    mgr.publish(model.buildPlaybackSnapshot(lanes));
                    ++publishes;
                    break;
            }
            ++events;
        }

        // Host transport changes: play/stop toggles and locate jumps.
        HostInfo host;
        if (rnd(0, 40) == 0) { hostPlaying = ! hostPlaying; ++events; }
        if (rnd(0, 60) == 0) { ppq = rnd(0, 64) * 0.5; ++events; }
        host.valid = true; host.playing = hostPlaying; host.ppqValid = true; host.ppq = ppq; host.bpm = 128.0;
        if (hostPlaying) ppq += n * (128.0 / 60.0) / hostSr;

        buf.clear();
        gCount.store(true);
        const long long a0 = gAllocs.load(), f0 = gFrees.load();
        eng.process(buf, 0, n, &midi, host);
        allocs += gAllocs.load() - a0;
        frees  += gFrees.load() - f0;
        gCount.store(false);
        ++blocks;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite(d[i])) ++nonFinite;
                peak = std::max(peak, std::abs(d[i]));
            }
        }

        Notification note;
        while (eng.popNotification(note)) {}
        mgr.collectGarbage();
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(nullptr);
#endif

    report(allocs == 0 && frees == 0, "RT: zero heap allocations / frees inside process()",
           "events=" + juce::String(events) + " blocks=" + juce::String(blocks)
           + " snapshotsPublished=" + juce::String(publishes)
           + " allocs=" + juce::String(allocs) + " frees=" + juce::String(frees)
#if defined(_DEBUG)
           + " (operator new + CRT malloc hook)"
#else
           + " (operator new/delete; build Debug for the malloc hook too)"
#endif
           );
    report(nonFinite == 0 && peak < 64.0f, "RT: no NaN / Inf in 10k-event render",
           "nonFinite=" + juce::String(nonFinite) + " peak=" + juce::String(peak, 3));
}

// ============================================================================
// 2-5. Onset, note-off, CC123, fades, loop crossfade
// ============================================================================
static void testOnsetAndRelease()
{
    const double sr = 44100.0;
    const int block = 512;
    SModeSegmentModel model;
    fillModel(model, 2, 4.0, sr, 0);   // DC stems
    const int s0 = model.addSegment(0, 1000, 60000);
    model.assignMidiNote(0, s0, 60);
    const int s1 = model.addSegment(1, 2000, 9000);
    model.assignMidiNote(1, s1, 62);
    model.getSegment(1, s1)->effects.loop = true;
    const int s2 = model.addSegment(0, 70000, 150000);
    model.assignMidiNote(0, s2, 64);

    EngineParams params;
    const int releaseLen = (int) std::lround(kReleaseFadeMs * 0.001 * sr);

    // -- 2a. unpitched onset, micro-fade OFF: exact -----------------------------
    {
        microFadeEnabledFlag().store(false);
        auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        const int onBlock = 3, onOff = 137;
        auto r = renderBlocks(eng, 8, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == onBlock) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), onOff);
        });
        const int expected = onBlock * block + onOff;
        const int got = firstNonZero(r.L);
        report(got == expected, "Onset: unpitched note starts on its sample (fade off)",
               "expected=" + juce::String(expected) + " got=" + juce::String(got) + " (+-0)");
    }

    // -- 2b. fade ON: first sample is the 0-gain start of the 2 ms ramp ----------
    {
        microFadeEnabledFlag().store(true);
        auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 4, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 1) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 200);
        });
        const int expected = block + 200;
        const int got = firstNonZero(r.L);
        report(got == expected + 1 && r.L[(size_t) expected] == 0.0f, "Onset: with micro-fade the ramp starts on the event sample",
               "event=" + juce::String(expected) + " firstNonZero=" + juce::String(got));
    }

    // -- 2c. pitched onset (pre-rolled pitch shifter) - measured --------------------
    {
        microFadeEnabledFlag().store(false);
        EngineParams pp;
        pp.stems[0].pitchSemis.store(3.0f);
        auto engHolder = std::make_unique<PlaybackEngine>(pp);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 12, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 2) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 50);
        });
        const int expected = 2 * block + 50;
        const int got = firstNonZero(r.L, 0, 0.05f);
        const int delta = got - expected;
        report(got >= 0 && std::abs(delta) <= 256, "Onset: pitched voice aligned with unpitched (outputSeek pre-roll)",
               "expected=" + juce::String(expected) + " firstAbove0.05=" + juce::String(got)
               + " delta=" + juce::String(delta) + " samples (old engine: +30..80 ms late)");
        microFadeEnabledFlag().store(true);
    }

    // -- 3. GATE + loop release on note-off ------------------------------------------
    {
        EngineParams gp;
        gp.gate.store(true);
        auto engHolder = std::make_unique<PlaybackEngine>(gp);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        const int offPos = 4 * block + 100;
        auto r = renderBlocks(eng, 10, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
            if (b == 4) m.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
        });
        const bool soundingBefore = std::abs(r.L[(size_t) offPos - 1]) > 0.5f;
        const bool silentAfter = allZeroFrom(r.L, offPos + releaseLen + 2);
        report(soundingBefore && silentAfter, "Note-off: GATE one-shot releases (3 ms fade)",
               "sounding@off-1=" + juce::String(r.L[(size_t) offPos - 1], 3)
               + " silentFrom=off+" + juce::String(releaseLen + 2));
    }
    {
        auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;   // 1SH mode: loops must still stop on note-off (A11)
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        const int offPos = 40 * block + 7;   // after several loop wraps (loop = 7000 samples)
        auto r = renderBlocks(eng, 46, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 0)  m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 127), 0);
            if (b == 40) m.addEvent(juce::MidiMessage::noteOff(1, 62), 7);
        });
        const bool looped = std::abs(r.L[(size_t) offPos - 1]) > 0.5f;
        report(looped && allZeroFrom(r.L, offPos + releaseLen + 2), "Note-off: looped chop stops in one-shot mode",
               "loopedPastEnd=" + juce::String(looped ? "yes" : "no"));
    }

    // -- 4. CC123 / CC120 ----------------------------------------------------------------
    {
        auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 8, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 0) { m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
                          m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 127), 0); }
            if (b == 3) m.addEvent(juce::MidiMessage::controllerEvent(1, 123, 0), 10);
        });
        report(std::abs(r.L[(size_t) 3 * block + 9]) > 0.5f && allZeroFrom(r.L, 3 * block + 10 + releaseLen + 2),
               "CC123 all-notes-off silences one-shots and loops", "");

        eng.pushCommand(cmd(CmdType::TransportStart, 1));
        auto r2 = renderBlocks(eng, 8, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 127), 0);
            if (b == 4) m.addEvent(juce::MidiMessage::controllerEvent(1, 120, 0), 0);
        });
        report(std::abs(r2.L[(size_t) 4 * block - 1]) > 0.5f && allZeroFrom(r2.L, 4 * block + releaseLen + 2)
                   && ! eng.isTransportActive(),
               "CC120 all-sound-off silences voices and the stem transport", "");
    }

    // -- 5. fades: no step above threshold on a full-scale DC stem ----------------------
    {
        EngineParams fp;
        fp.mono.store(true);
        auto engHolder = std::make_unique<PlaybackEngine>(fp);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 40, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 1)  m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 33);    // start
            if (b == 5)  m.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 127), 100);   // MONO retrigger
            if (b == 9)  m.addEvent(juce::MidiMessage::controllerEvent(1, 123, 0), 300);          // stop
            if (b == 12) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
        });
        eng.pushCommand(cmd(CmdType::StopVoices));
        auto r2 = renderBlocks(eng, 3, block);
        r.L.insert(r.L.end(), r2.L.begin(), r2.L.end());
        const float step = maxStep(r.L);
        report(step < 0.03f, "Fades: start / mono retrigger / stop / UI stop, max step on DC=1.0",
               "maxStep=" + juce::String(step, 4) + " (hard cut = 1.0, threshold 0.03)");
    }
    {
        // Steal: 50 overlapping one-shots (> 32 voices), one per block.
        SModeSegmentModel big;
        fillModel(big, 1, 30.0, sr, 0);
        for (int k = 0; k < 50; ++k)
        {
            const int idx = big.addSegment(0, k * 10000, k * 10000 + 400000 > 30 * 44100 ? 30 * 44100 - 1 : k * 10000 + 400000);
            big.assignMidiNote(0, idx, 20 + k);
        }
        auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = big.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 70, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b < 50) m.addEvent(juce::MidiMessage::noteOn(1, 20 + b, (juce::uint8) 32), 17);
        });
        // velocity 32/127 ~ 0.25 per voice; one start + one steal per block
        const float step = maxStep(r.L);
        report(step < 0.03f && eng.getActiveVoiceCountUi() <= kMidiVoiceSlots, "Fades: voice stealing past 32 voices fades the stolen voice",
               "maxStep=" + juce::String(step, 4) + " activeVoices=" + juce::String(eng.getActiveVoiceCountUi()));
    }
    {
        // Loop crossfade continuity: sine loop whose length is NOT a whole number of periods.
        SModeSegmentModel sm;
        fillModel(sm, 1, 4.0, sr, 1);
        const int idx = sm.addSegment(0, 10000, 10000 + 4321);
        sm.assignMidiNote(0, idx, 60);
        sm.getSegment(0, idx)->effects.loop = true;
        auto engHolder = std::make_unique<PlaybackEngine>(params);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        auto snap = sm.buildPlaybackSnapshot({});
        snap->seq = 1;
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 60, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
        });
        const float natural = (float) (0.5 * 2.0 * juce::MathConstants<double>::pi * 440.0 / sr);
        const float step = maxStep(r.L);
        report(step < natural * 1.5f, "Loop: crossfade at the wrap point (no click)",
               "maxStep=" + juce::String(step, 4) + " sineMaxStep=" + juce::String(natural, 4)
               + " wraps=" + juce::String((60 * block) / 4321));
    }
}

// ============================================================================
// 6. offline == realtime
// ============================================================================
static void testOfflineEqualsRealtime()
{
    const double sr = 44100.0;
    SModeSegmentModel model;
    fillModel(model, 3, 8.0, sr, 2);
    for (int s = 0; s < 3; ++s)
        for (int k = 0; k < 4; ++k)
            model.addSegment(s, (int64_t) ((k * 1.7 + 0.1 * s) * sr), (int64_t) ((k * 1.7 + 0.1 * s + 0.6) * sr));
    auto lanes = makeLanes(3, 4);
    auto snap = model.buildPlaybackSnapshot(lanes);
    snap->seq = 1;
    const double bpm = 118.0;
    const int cells = 8;

    auto realtime = [&](EngineParams& p, const std::vector<int>& blockPattern)
    {
        auto engHolder = std::make_unique<PlaybackEngine>(p);  // ~130 KB: keep off the stack
        auto& eng = *engHolder;
        eng.prepare(sr, 512);
        eng.publishSnapshot(snap.get());
        Command c = cmd(CmdType::SeqStart, 3, 1);
        c.d0 = bpm;
        eng.pushCommand(c);
        const int total = (int) std::llround(cells * 60.0 / bpm * sr);
        juce::AudioBuffer<float> out(2, total);
        out.clear();
        const HostInfo noHost;
        int pos = 0, k = 0;
        while (pos < total)
        {
            const int n = juce::jmin(blockPattern[(size_t) (k++ % (int) blockPattern.size())], total - pos);
            eng.process(out, pos, n, nullptr, noHost);
            pos += n;
        }
        return out;
    };
    auto compare = [](const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double& maxDiff)
    {
        maxDiff = 0.0;
        if (a.getNumSamples() != b.getNumSamples()) { maxDiff = 1e9; return false; }
        bool same = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const float x = a.getSample(ch, i), y = b.getSample(ch, i);
                if (x != y) { same = false; maxDiff = std::max(maxDiff, (double) std::abs(x - y)); }
            }
        return same;
    };

    {
        EngineParams p;
        p.stems[1].pitchSemis.store(-2.0f);   // pitched stem in the mix
        auto rt  = realtime(p, { 512 });
        auto off = renderSequence(p, *snap, sr, 3, cells, bpm, {}, 512);
        double d = 0.0;
        const bool same = off != nullptr && compare(rt, *off, d);
        report(same, "Export: offline render == realtime render, bit for bit (incl. pitched stem)",
               "samples=" + juce::String(rt.getNumSamples()) + " maxDiff=" + juce::String(d, 9)
               + " peak=" + juce::String(rt.getMagnitude(0, rt.getNumSamples()), 3));
    }
    {
        // Unpitched (no stem pitch, no slot pitch): block partition must not matter at all.
        auto flatLanes = lanes;
        for (auto& l : flatLanes) for (auto& sl : l.slots) sl.pitchSemis = 0.0f;
        auto flat = model.buildPlaybackSnapshot(flatLanes);
        flat->seq = 1;
        snap = flat;
        EngineParams p;
        auto off = renderSequence(p, *snap, sr, 3, cells, bpm, {}, 512);
        auto rt  = realtime(p, { 64, 333, 17, 512, 129 });
        double d = 0.0;
        const bool same = off != nullptr && compare(rt, *off, d);
        report(same, "Export: unpitched render is identical for any host block partition",
               "maxDiff=" + juce::String(d, 9));
    }
}

// ============================================================================
// 7. Sequencer timing vs host PPQ
// ============================================================================
static void testSequencerHostSync()
{
    microFadeEnabledFlag().store(false);   // exact onsets
    const double sr = 48000.0;
    const double bpm = 120.0;              // 24000 samples per beat
    const int block = 480;
    SModeSegmentModel model;
    fillModel(model, 1, 10.0, sr, 0);
    model.addSegment(0, 0, (int64_t) (2 * sr));
    std::vector<LaneDesc> lanes(1);
    lanes[0].numCells = 4;
    for (int c : { 0, 2 })
    {
        SlotDesc s; s.empty = false; s.stem = 0; s.segIndex = 0; s.gridCell = c; s.cellSpan = 1;
        lanes[0].slots.push_back(s);
    }
    auto snap = model.buildPlaybackSnapshot(lanes);
    snap->seq = 1;

    EngineParams p;
    auto engHolder = std::make_unique<PlaybackEngine>(p);  // ~130 KB: keep off the stack
    auto& eng = *engHolder;
    eng.prepare(sr, block);
    eng.publishSnapshot(snap.get());
    eng.pushCommand(cmd(CmdType::SeqStart, 1, 1));

    const double ppq0 = 0.25;
    const int playBlocks = 400;   // 4 s
    auto r = renderBlocks(eng, playBlocks + 20, block, [&](int b, juce::MidiBuffer&, HostInfo& h)
    {
        h.valid = true; h.ppqValid = true; h.bpm = bpm;
        h.playing = b < playBlocks;
        h.ppq = ppq0 + (double) b * block * bpm / 60.0 / sr;
    });

    std::vector<int> onsets;
    for (int i = 1; i < playBlocks * block; ++i)
        if (r.L[(size_t) i] != 0.0f && r.L[(size_t) i - 1] == 0.0f) onsets.push_back(i);
    // cells with slots: 0 and 2 -> beats 2, 4, 6, ... ; sample = (beat - ppq0) * 24000
    std::vector<int> expected;
    for (int beat = 2; (beat - ppq0) * 24000.0 < playBlocks * block; beat += 2)
        expected.push_back((int) std::lround((beat - ppq0) * 24000.0));
    bool exact = onsets.size() == expected.size();
    int worst = 0;
    for (size_t i = 0; i < std::min(onsets.size(), expected.size()); ++i)
    {
        worst = std::max(worst, std::abs(onsets[i] - expected[i]));
        if (onsets[i] != expected[i]) exact = false;
    }
    report(exact, "Sequencer: steps land on host PPQ beats (sample-exact)",
           "steps=" + juce::String((int) onsets.size()) + "/" + juce::String((int) expected.size())
           + " worstError=" + juce::String(worst) + " samples, first=" + juce::String(onsets.empty() ? -1 : onsets[0])
           + " expected=" + juce::String(expected.empty() ? -1 : expected[0]));

    bool stoppedNote = false;
    Notification n;
    while (eng.popNotification(n))
        if (n.type == NoteType::SequencerStopped && n.i0 == 0) stoppedNote = true;
    const int releaseLen = (int) std::lround(kReleaseFadeMs * 0.001 * sr);
    report(stoppedNote && ! eng.isSequencerRunning() && allZeroFrom(r.L, playBlocks * block + releaseLen + 2),
           "Sequencer: host transport stop stops the host-driven sequencer + voices (A11)", "");
    microFadeEnabledFlag().store(true);
}

// ============================================================================
// 8-9. Transport with muted stems, bypass, RCU, resampler
// ============================================================================
static void testMisc()
{
    const double sr = 44100.0;
    SModeSegmentModel model;
    fillModel(model, 2, 3.0, sr, 0);
    model.setMute(0, true);
    model.setMute(1, true);
    const int idx = model.addSegment(0, 1000, 100000);
    model.assignMidiNote(0, idx, 60);
    model.getSegment(0, idx)->effects.loop = true;

    EngineParams p;
    p.transportLoop.store(true);
    auto engHolder = std::make_unique<PlaybackEngine>(p);  // ~130 KB: keep off the stack
    auto& eng = *engHolder;
    eng.prepare(sr, 512);
    SnapshotManager mgr(eng);
    mgr.publish(model.buildPlaybackSnapshot({}));

    // A14: all stems muted + looping transport -> position stays finite, output silent.
    auto c = cmd(CmdType::TransportStart, 1);
    c.d0 = 1.0;
    eng.pushCommand(c);
    auto r = renderBlocks(eng, 600, 512);   // ~7 s: wraps the 3 s song twice
    const double pos = eng.getTransportPositionSeconds();
    report(std::isfinite(pos) && pos >= 0.0 && pos <= 3.0 && eng.isTransportActive() && r.L.back() == 0.0f,
           "Transport: all stems muted -> no NaN, keeps looping silently (A14)",
           "pos=" + juce::String(pos, 3) + "s");

    // unmute -> audible again
    model.setMute(0, false);
    mgr.publish(model.buildPlaybackSnapshot({}));
    auto r2 = renderBlocks(eng, 20, 512);
    report(std::abs(r2.L.back()) > 0.5f, "Transport: unmuting resumes audio (gain ramp)", "");
    eng.pushCommand(cmd(CmdType::TransportStop));

    // Bypass: fade once, then silence; voices gone afterwards.
    {
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
        buf.clear();
        eng.process(buf, 0, 512, &midi, HostInfo {});
        for (int i = 0; i < 4; ++i)
        {
            for (int ch = 0; ch < 2; ++ch) buf.getWritePointer(ch)[0] = 0.77f;   // host input must not leak
            eng.processBypassed(buf, 0, 512);
        }
        report(buf.getMagnitude(0, 512) == 0.0f && ! eng.hasActiveVoices(), "Bypass: silence, voices released (A26)", "");
    }

    // RCU: a snapshot used by a playing voice survives newer publishes, then is freed.
    {
        std::weak_ptr<const Snapshot> first = mgr.current();
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);   // looping voice
        eng.process(buf, 0, 512, &midi, HostInfo {});
        for (int i = 0; i < 3; ++i)
        {
            mgr.publish(model.buildPlaybackSnapshot({}));
            midi.clear();
            buf.clear();
            eng.process(buf, 0, 512, &midi, HostInfo {});
            mgr.collectGarbage();
        }
        const bool keptWhilePlaying = ! first.expired();
        eng.pushCommand(cmd(CmdType::StopVoices));
        for (int i = 0; i < 4; ++i) { buf.clear(); eng.process(buf, 0, 512, &midi, HostInfo {}); }
        mgr.collectGarbage();
        report(keptWhilePlaying && first.expired() && mgr.retiredCount() == 0,
               "RCU: old snapshot kept while a voice plays it, freed on the message side afterwards",
               "retired=" + juce::String(mgr.retiredCount()));
    }

    // Resampler (A23): 1 kHz sine 44.1k -> 48k vs ideal.
    {
        const int n = 44100;
        juce::AudioBuffer<float> src(1, n);
        for (int i = 0; i < n; ++i) src.setSample(0, i, (float) std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 44100.0));
        auto out = resampleStem(src, 44100.0, 48000.0);
        double err = 0.0, ref = 0.0;
        for (int i = 2000; out != nullptr && i < out->getNumSamples() - 2000; ++i)
        {
            const double ideal = std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0);
            err += std::pow(out->getSample(0, i) - ideal, 2.0);
            ref += ideal * ideal;
        }
        const double snr = (err > 0.0 && ref > 0.0) ? 10.0 * std::log10(ref / err) : 200.0;
        report(out != nullptr && snr > 60.0, "Resample: windowed-sinc stem resample to the host rate",
               "SNR=" + juce::String(snr, 1) + " dB (Kaiser sinc; JUCE WindowedSinc measured 40 dB)");
    }
}


// ============================================================================
// 10. UI round (review A1 / A3, per-chop settings, x16 lanes, LOCK transpose)
// ============================================================================
namespace samplar { namespace engine {
struct PlaybackEngineTestAccess
{
    static void beginBlock(PlaybackEngine& e) { e.inProcess_.store(true); e.reloadSnapshot(); }
    static void drain(PlaybackEngine& e)      { e.drainCommands(); }
    static void endBlock(PlaybackEngine& e)   { e.inProcess_.store(false); }

    // round 2 (N-3 / N-18): voice-state inspection
    static void laneVoices(const PlaybackEngine& e, int lane, int& sounding, int& fading)
    {
        sounding = fading = 0;
        for (const auto& v : e.voices_)
            if (v.active && v.pool == PlaybackEngine::PoolSeq && v.lane == lane)
                ++(v.releasing ? fading : sounding);
    }
    static int poolActive(const PlaybackEngine& e, int pool)
    {
        int n = 0;
        for (const auto& v : e.voices_) if (v.active && v.pool == pool) ++n;
        return n;
    }
    static int stretchVoices(const PlaybackEngine& e, int& activeTotal)
    {
        int n = 0; activeTotal = 0;
        for (const auto& v : e.voices_) if (v.active) { ++activeTotal; if (v.stretch) ++n; }
        return n;
    }
};
}}

static void testUiRound()
{
    const double sr = 44100.0;
    const int block = 512;

    // ---- A1: a snapshot switch in the middle of the command drain ---------
    {
        SModeSegmentModel model;
        fillModel(model, 1, 4.0, sr, 2);
        model.addSegment(0, 0, (int64_t) (2.0 * sr));
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        SnapshotManager mgr(eng);
        mgr.publish(model.buildPlaybackSnapshot({}));   // seq 1
        const uint64_t seqA = mgr.currentSeq();

        PlaybackEngineTestAccess::beginBlock(eng);       // block starts on A
        Command c1 = cmd(CmdType::PlaySegment, 0, 0); c1.snapSeq = seqA;
        eng.pushCommand(c1);
        mgr.publish(model.buildPlaybackSnapshot({}));   // seq 2: A retired
        Command c2 = cmd(CmdType::PlaySegment, 0, 0); c2.snapSeq = mgr.currentSeq();
        eng.pushCommand(c2);
        PlaybackEngineTestAccess::drain(eng);            // voice on A, then switch to B
        const int freedDuringBlock = mgr.collectGarbage();   // the 30 Hz timer, mid-block
        const int retiredDuringBlock = mgr.retiredCount();
        PlaybackEngineTestAccess::endBlock(eng);

        juce::AudioBuffer<float> buf(2, block);
        buf.clear();
        eng.process(buf, 0, block, nullptr, HostInfo {});   // publishes the voices' seqs
        const int freedWhilePlaying = mgr.collectGarbage();
        report(freedDuringBlock == 0 && retiredDuringBlock == 1 && freedWhilePlaying == 0,
               "A1: mid-block snapshot switch keeps the older snapshot alive",
               "freedDuringBlock=" + juce::String(freedDuringBlock) + " retired=" + juce::String(retiredDuringBlock)
               + " freedWhileVoicePlays=" + juce::String(freedWhilePlaying));
        eng.pushCommand(cmd(CmdType::AllSoundOff));
        for (int i = 0; i < 20; ++i) { buf.clear(); eng.process(buf, 0, block, nullptr, HostInfo {}); }
        const int freedAfter = mgr.collectGarbage();
        report(freedAfter == 1 && mgr.retiredCount() == 0, "A1: old snapshot freed once no voice uses it",
               "freed=" + juce::String(freedAfter));
    }

    // ---- A3: stale trigger dropped, stop still applied ----------------------
    {
        SModeSegmentModel model;
        fillModel(model, 1, 2.0, sr, 0);
        model.addSegment(0, 0, (int64_t) sr);
        auto snap = model.buildPlaybackSnapshot({});
        snap->seq = 1;
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        eng.publishSnapshot(snap.get());
        Command stale = cmd(CmdType::PlaySegment, 0, 0);
        stale.stampMs = (juce::Time::getMillisecondCounter() - 1000u) | 1u;
        eng.pushCommand(stale);
        auto r = renderBlocks(eng, 4, block);
        const bool silent = firstNonZero(r.L) < 0;
        Command fresh = cmd(CmdType::PlaySegment, 0, 0);
        fresh.stampMs = juce::Time::getMillisecondCounter() | 1u;
        eng.pushCommand(fresh);
        auto r2 = renderBlocks(eng, 4, block);
        report(silent && firstNonZero(r2.L) >= 0, "A3: trigger queued > 150 ms ago is dropped, a fresh one plays",
               juce::String("stale silent=") + (silent ? "yes" : "no"));
        eng.publishSnapshot(nullptr);
    }

    // ---- per-chop gain / pan / reverse / pitch / attack ----------------------
    {
        microFadeEnabledFlag().store(false);
        SModeSegmentModel model;
        model.clear();
        model.setStemCount(1);
        model.setSampleRate(sr);
        const int n = (int) sr;
        juce::AudioBuffer<float> ramp(2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) ramp.setSample(ch, i, (float) i / (float) n);
        model.setStemAudio(0, std::move(ramp));
        model.addSegment(0, 1000, 11000);
        SegmentEffects fx;
        fx.volume = 0.5f;
        fx.pan = -1.0f;
        fx.reverse = true;
        model.setSegmentEffects(0, 0, fx);
        auto snap = model.buildPlaybackSnapshot({});
        fillSegmentFx(*snap, model, {});
        snap->seq = 1;
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        eng.publishSnapshot(snap.get());
        eng.pushCommand(cmd(CmdType::PlaySegment, 0, 0));
        auto r = renderBlocks(eng, 30, block);
        // reversed: output k = 0.5 * stem[10999 - k] on L, nothing on R
        float maxErr = 0.0f, maxR = 0.0f;
        for (int k = 0; k < 10000; ++k)
        {
            maxErr = std::max(maxErr, std::abs(r.L[(size_t) k] - 0.5f * (float) (10999 - k) / (float) n));
            maxR = std::max(maxR, std::abs(r.R[(size_t) k]));
        }
        report(maxErr < 1.0e-5f && maxR == 0.0f, "Per-chop gain 0.5 + hard-left pan + reverse",
               "maxErr=" + juce::String(maxErr, 7) + " maxRight=" + juce::String(maxR, 7));

        // attack: 10 ms linear fade-in, measured on a DC chop
        SModeSegmentModel m2;
        fillModel(m2, 1, 2.0, sr, 0);
        m2.addSegment(0, 0, (int64_t) sr);
        auto s2 = m2.buildPlaybackSnapshot({});
        fillSegmentFx(*s2, m2, {});
        s2->segs[0].attackMs = 10.0f;
        s2->segs[0].pitchSemis = 0.0f;
        s2->seq = 2;
        eng.pushCommand(cmd(CmdType::AllSoundOff));
        renderBlocks(eng, 4, block);
        eng.publishSnapshot(s2.get());
        eng.pushCommand(cmd(CmdType::PlaySegment, 0, 0));
        auto ra = renderBlocks(eng, 4, block);
        const int atk = (int) std::lround(0.010 * sr);
        const bool ramped = std::abs(ra.L[0]) < 1.0e-6f && std::abs(ra.L[(size_t) atk / 2] - 0.5f) < 0.01f
                         && std::abs(ra.L[(size_t) atk + 10] - 1.0f) < 1.0e-5f;
        report(ramped, "Per-chop attack 10 ms fades in linearly",
               "v[0]=" + juce::String(ra.L[0], 4) + " v[atk/2]=" + juce::String(ra.L[(size_t) atk / 2], 4)
               + " v[atk+10]=" + juce::String(ra.L[(size_t) atk + 10], 4));

        // pitch: +12 st per chop or via the global LOCK transpose must change the output
        s2->segs[0].attackMs = 0.0f;
        eng.pushCommand(cmd(CmdType::AllSoundOff));
        renderBlocks(eng, 4, block);
        p.globalSemis.store(12.0f);
        SModeSegmentModel m3;
        fillModel(m3, 1, 2.0, sr, 1);   // 440 Hz sine
        m3.addSegment(0, 0, (int64_t) sr);
        auto s3 = m3.buildPlaybackSnapshot({});
        s3->seq = 3;
        eng.publishSnapshot(s3.get());
        eng.pushCommand(cmd(CmdType::PlaySegment, 0, 0));
        auto rp = renderBlocks(eng, 40, block);
        int zc = 0;
        for (size_t i = 8000; i < 16000; ++i)
            if ((rp.L[i - 1] < 0.0f) != (rp.L[i] < 0.0f)) ++zc;
        const double hz = zc / 2.0 / (8000.0 / sr);
        p.globalSemis.store(0.0f);
        report(std::abs(hz - 880.0) < 25.0, "Global LOCK transpose +12 st doubles the pitch (B17)",
               "measured=" + juce::String(hz, 1) + " Hz (source 440 Hz)");
        eng.publishSnapshot(nullptr);
        microFadeEnabledFlag().store(true);
    }

    // ---- B18: x16 lane = 16 steps per bar, 1/16 note apart --------------------
    {
        microFadeEnabledFlag().store(false);
        const double sr48 = 48000.0;
        SModeSegmentModel model;
        fillModel(model, 1, 4.0, sr48, 0);
        model.addSegment(0, 0, 1000);
        std::vector<LaneDesc> lanes(1);
        lanes[0].cellsPerBeat = 4;
        lanes[0].numCells = 16;   // 1 bar of 16ths
        for (int c = 0; c < 16; ++c)
        {
            SlotDesc sd; sd.empty = false; sd.stem = 0; sd.segIndex = 0; sd.gridCell = c; sd.cellSpan = 1;
            lanes[0].slots.push_back(sd);
        }
        auto snap = model.buildPlaybackSnapshot(lanes);
        fillSegmentFx(*snap, model, lanes);
        snap->seq = 1;
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr48, 480);
        eng.publishSnapshot(snap.get());
        Command st = cmd(CmdType::SeqStart, 1, 1); st.d0 = 120.0;   // 24000 samples / beat
        eng.pushCommand(st);
        auto r = renderBlocks(eng, 200, 480);   // exactly 1 bar = 96000 samples
        std::vector<int> onsets;
        if (r.L[0] != 0.0f) onsets.push_back(0);
        for (size_t i = 1; i < r.L.size(); ++i)
            if (r.L[i] != 0.0f && r.L[i - 1] == 0.0f) onsets.push_back((int) i);
        bool exact = onsets.size() == 16;
        for (size_t i = 0; i < onsets.size() && exact; ++i)
            if (onsets[i] != (int) i * 6000) exact = false;
        report(exact, "Sequencer x16 lane: 16 steps per bar, 6000 samples apart at 120 BPM (B18)",
               "steps=" + juce::String((int) onsets.size()) + " second=" + juce::String(onsets.size() > 1 ? onsets[1] : -1));
        eng.publishSnapshot(nullptr);
        microFadeEnabledFlag().store(true);
    }
}

// ============================================================================
// 11. Review round 2: N-3, N-4, N-11, N-12, N-13, N-18, N-24
// ============================================================================

// N-3: chops with a 5 s release on a 16th-note lane.
static void testLongReleaseLane()
{
    const double sr = 44100.0;
    const int block = 512;
    microFadeEnabledFlag().store(true);
    SModeSegmentModel model;
    fillModel(model, 1, 6.0, sr, 0);   // DC
    model.addSegment(0, 0, (int64_t) (2.0 * sr));   // a 2 s chop: outlives every 16th cell
    std::vector<LaneDesc> lanes(1);
    lanes[0].cellsPerBeat = 4;
    lanes[0].numCells = 16;
    for (int c = 0; c < 16; ++c)
    {
        SlotDesc sd; sd.empty = false; sd.stem = 0; sd.segIndex = 0; sd.gridCell = c; sd.cellSpan = 1;
        lanes[0].slots.push_back(sd);
    }
    auto snap = model.buildPlaybackSnapshot(lanes);
    fillSegmentFx(*snap, model, lanes);
    snap->segs[0].releaseMs = 5000.0f;
    snap->seq = 1;

    EngineParams p;
    auto engHolder = std::make_unique<PlaybackEngine>(p);
    auto& eng = *engHolder;
    eng.prepare(sr, block);
    eng.publishSnapshot(snap.get());
    Command st = cmd(CmdType::SeqStart, 1, 1); st.d0 = 140.0;
    eng.pushCommand(st);

    int maxSounding = 0, maxFading = 0, maxPool = 0;
    Rendered r;
    juce::AudioBuffer<float> buf(2, block);
    for (int b = 0; b < 400; ++b)   // ~4.6 s = ~43 steps
    {
        buf.clear();
        eng.process(buf, 0, block, nullptr, HostInfo {});
        r.L.insert(r.L.end(), buf.getReadPointer(0), buf.getReadPointer(0) + block);
        int s = 0, f = 0;
        PlaybackEngineTestAccess::laneVoices(eng, 0, s, f);
        maxSounding = std::max(maxSounding, s);
        maxFading = std::max(maxFading, f);
        maxPool = std::max(maxPool, PlaybackEngineTestAccess::poolActive(eng, 1));
    }
    const float step = maxStep(r.L);
    report(maxSounding <= 1 && maxFading <= 1 && eng.getForcedKillCount() == 0 && step < 0.03f,
           "N-3: 16th lane, chop release 5 s -> lane stays monophonic, no forced cuts, no clicks",
           "maxSounding=" + juce::String(maxSounding) + " maxFading=" + juce::String(maxFading)
           + " maxSeqPool=" + juce::String(maxPool) + " forcedKills=" + juce::String(eng.getForcedKillCount())
           + " maxStep=" + juce::String(step, 4) + " (threshold 0.03, DC=1.0)");
    eng.publishSnapshot(nullptr);
}

// N-3: MIDI drum rolls with a 5 s chop release (gate mode: every note-off starts it).
static void testDrumRollNoStarvation()
{
    const double sr = 44100.0;
    const int block = 512;
    microFadeEnabledFlag().store(true);
    SModeSegmentModel model;
    fillModel(model, 1, 30.0, sr, 0);   // DC
    const int numNotes = 48;
    for (int k = 0; k < numNotes; ++k)
    {
        const int idx = model.addSegment(0, k * 20000, k * 20000 + 44100);   // 1 s chops
        model.assignMidiNote(0, idx, 30 + k);
    }
    auto snap = model.buildPlaybackSnapshot({});
    fillSegmentFx(*snap, model, {});
    for (auto& sg : snap->segs) sg.releaseMs = 5000.0f;
    snap->seq = 1;

    // a) roll: one note every 8 ms, note-off 30 ms later (GATE) -> 5 s releases pile up
    {
        EngineParams p;
        p.gate.store(true);
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        eng.publishSnapshot(snap.get());
        const int spacing = (int) std::lround(0.008 * sr), gateLen = (int) std::lround(0.030 * sr);
        int triggered = 0, maxPool = 0;
        auto r = renderBlocks(eng, 200, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            const int b0 = b * block, b1 = b0 + block;
            for (int k = 0; k < numNotes; ++k)
            {
                const int on = 1000 + k * spacing, off = on + gateLen;
                if (on >= b0 && on < b1)  m.addEvent(juce::MidiMessage::noteOn(1, 30 + k, (juce::uint8) 32), on - b0);
                if (off >= b0 && off < b1) m.addEvent(juce::MidiMessage::noteOff(1, 30 + k), off - b0);
            }
        });
        Notification n;
        while (eng.popNotification(n)) if (n.type == NoteType::SegmentTriggered) ++triggered;
        maxPool = PlaybackEngineTestAccess::poolActive(eng, 0);
        const float step = maxStep(r.L);
        report(triggered == numNotes && eng.getForcedKillCount() == 0 && step < 0.03f,
               "N-3: 48-note MIDI roll (8 ms apart, 5 s release, GATE) -> every note plays, no forced cuts, no clicks",
               "triggered=" + juce::String(triggered) + "/" + juce::String(numNotes)
               + " forcedKills=" + juce::String(eng.getForcedKillCount()) + " maxStep=" + juce::String(step, 4)
               + " midiPoolAtEnd=" + juce::String(maxPool));
    }

    // b) two dense chords in consecutive blocks: the pool really runs out, the
    //    forced cut must not step the output (declick ramp).
    {
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        eng.publishSnapshot(snap.get());
        auto r = renderBlocks(eng, 12, block, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 2) for (int k = 0; k < 40; ++k) m.addEvent(juce::MidiMessage::noteOn(1, 30 + k, (juce::uint8) 32), 100);
            if (b == 3) for (int k = 40; k < 48; ++k) m.addEvent(juce::MidiMessage::noteOn(1, 30 + k, (juce::uint8) 32), 0);
            if (b == 3) m.addEvent(juce::MidiMessage::noteOn(1, 30, (juce::uint8) 32), 1);   // + one more: pool full
        });
        // Only look at the dense region, after the first chord's fade-in.
        std::vector<float> region(r.L.begin() + 3 * block - 50, r.L.begin() + 6 * block);
        const float step = maxStep(region);
        const bool finite = std::all_of(r.L.begin(), r.L.end(), [](float x) { return std::isfinite(x); });
        report(eng.getForcedKillCount() > 0 && step < 0.05f && finite,
               "N-3: pool exhausted -> forced cut ramps out (1.5 ms declick), no step",
               "forcedKills=" + juce::String(eng.getForcedKillCount()) + " maxStep=" + juce::String(step, 4)
               + " (a hard cut of one voice = ~0.25)");
    }
}

// N-4: garbage host BPM / PPQ must not hang the audio thread.
static void testHostGarbage()
{
    const double sr = 48000.0;
    const int block = 512;
    SModeSegmentModel model;
    fillModel(model, 1, 4.0, sr, 0);
    model.addSegment(0, 0, 4000);
    std::vector<LaneDesc> lanes(1);
    lanes[0].cellsPerBeat = 4;
    lanes[0].numCells = 16;
    for (int c = 0; c < 16; ++c)
    {
        SlotDesc sd; sd.empty = false; sd.stem = 0; sd.segIndex = 0; sd.gridCell = c; sd.cellSpan = 1;
        lanes[0].slots.push_back(sd);
    }
    auto snap = model.buildPlaybackSnapshot(lanes);
    fillSegmentFx(*snap, model, lanes);
    snap->seq = 1;

    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    struct Case { const char* name; double bpm; double ppq; };
    const Case cases[] = {
        { "bpm NaN", nan, 8.0 }, { "bpm +Inf", inf, 8.0 }, { "bpm -Inf", -inf, 8.0 }, { "bpm 1e300", 1.0e300, 8.0 },
        { "bpm 0", 0.0, 8.0 }, { "bpm -120", -120.0, 8.0 }, { "bpm 1e-300", 1.0e-300, 8.0 },
        { "ppq NaN", 120.0, nan }, { "ppq +Inf", 120.0, inf }, { "ppq -Inf", 120.0, -inf }, { "ppq 1e18", 120.0, 1.0e18 },
        { "ppq -1e18", 120.0, -1.0e18 }, { "both NaN", nan, nan }, { "bpm Inf ppq 1e18", inf, 1.0e18 },
    };
    double worstMs = 0.0;
    bool allFinite = true, allOk = true;
    juce::String detail;
    for (const auto& c : cases)
    {
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, block);
        eng.publishSnapshot(snap.get());
        eng.pushCommand(cmd(CmdType::SeqStart, 1, 1));
        juce::AudioBuffer<float> buf(2, block);
        double ppq = 4.0;
        double caseWorst = 0.0;
        for (int b = 0; b < 30; ++b)
        {
            HostInfo h;
            h.valid = true; h.playing = true; h.ppqValid = true;
            // 5 good blocks (host-driven sequencer running), then garbage, then good again.
            const bool bad = b >= 5 && b < 25;
            h.bpm = bad ? c.bpm : 120.0;
            h.ppq = bad ? c.ppq : ppq;
            ppq += block * 2.0 / sr;
            buf.clear();
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            eng.process(buf, 0, block, nullptr, h);
            const double dt = juce::Time::getMillisecondCounterHiRes() - t0;
            caseWorst = std::max(caseWorst, dt);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (! std::isfinite(buf.getSample(ch, i))) allFinite = false;
        }
        worstMs = std::max(worstMs, caseWorst);
        if (caseWorst >= 50.0) { allOk = false; detail << c.name << "=" << juce::String(caseWorst, 1) << "ms "; }
        eng.publishSnapshot(nullptr);
    }
    report(allOk && allFinite, "N-4: NaN / Inf / 1e300 BPM, NaN / Inf / 1e18 PPQ, BPM <= 0 -> every block < 50 ms, finite output",
           "cases=" + juce::String((int) (sizeof(cases) / sizeof(cases[0]))) + " worstBlock=" + juce::String(worstMs, 2)
           + " ms " + detail);

    HostInfo h; h.valid = true; h.playing = true; h.ppqValid = true; h.bpm = inf; h.ppq = nan;
    const bool changed = sanitizeHostInfo(h);
    HostInfo g; g.valid = true; g.ppqValid = true; g.bpm = 5000.0; g.ppq = 12.5;
    sanitizeHostInfo(g);
    report(changed && h.bpm == 0.0 && ! h.ppqValid && g.bpm == kMaxHostBpm && g.ppqValid && g.ppq == 12.5,
           "N-4: sanitizeHostInfo rejects non-finite, clamps BPM to 20..999",
           "inf->" + juce::String(h.bpm) + " 5000->" + juce::String(g.bpm));
}

// N-11: transport mute / solo after the host-rate resampled snapshot lands.
static void testTransportMuteAfterResample()
{
    const double modelSr = 44100.0, hostSr = 48000.0;
    const int block = 512;
    SModeSegmentModel model;
    fillModel(model, 2, 3.0, modelSr, 0);
    EngineParams p;
    auto engHolder = std::make_unique<PlaybackEngine>(p);
    auto& eng = *engHolder;
    eng.prepare(hostSr, block);
    SnapshotManager mgr(eng);
    mgr.publish(model.buildPlaybackSnapshot({}));   // model rate, converted on the fly

    eng.pushCommand(cmd(CmdType::TransportStart, 1));
    auto r0 = renderBlocks(eng, 10, block);
    const bool playing = std::abs(r0.L.back()) > 1.5f;   // 2 DC stems

    // The background resample lands; the user mutes stem 0 at the same time.
    std::array<StemOverride, kMaxStems> ov {};
    for (int s = 0; s < 2; ++s)
        ov[(size_t) s] = { resampleStem(*model.getSharedStemAudio(s), modelSr, hostSr), hostSr };
    model.setMute(0, true);
    auto snapB = model.buildPlaybackSnapshot({}, &ov);
    const bool sameId = snapB->stems[0].sourceId == mgr.current()->stems[0].sourceId
                     && snapB->stems[0].ch[0] != mgr.current()->stems[0].ch[0];
    mgr.publish(snapB);
    auto r1 = renderBlocks(eng, 10, block);
    const float level = std::abs(r1.L.back());   // stem 1 only: ~1.0
    report(playing && sameId && std::abs(level - 1.0f) < 0.02f,
           "N-11: transport mute applies after the resampled snapshot replaced the stem pointers",
           "levelBefore=" + juce::String(r0.L.back(), 3) + " levelAfterMute=" + juce::String(level, 3)
           + " sameSourceId=" + juce::String(sameId ? "yes" : "no"));
    eng.pushCommand(cmd(CmdType::AllSoundOff));
    renderBlocks(eng, 4, block);

    // A new song (clear) must get a different identity.
    const uint64_t oldId = snapB->stems[0].sourceId;
    fillModel(model, 2, 3.0, modelSr, 0);
    auto snapC = model.buildPlaybackSnapshot({});
    report(snapC->stems[0].sourceId != oldId && snapC->stems[0].sourceId != 0,
           "N-11: a new song gets a new stem source identity", "");
}

// N-12 / N-13 / N-18 / N-24
static void testLifecycleAndCommands()
{
    const double sr = 44100.0;
    SModeSegmentModel model;
    fillModel(model, 1, 4.0, sr, 0);
    model.addSegment(0, 0, (int64_t) (3.0 * sr));
    model.assignMidiNote(0, 0, 60);
    model.getSegment(0, 0)->effects.loop = true;
    auto snap = model.buildPlaybackSnapshot({});
    snap->seq = 1;

    // ---- N-12: prepare / release bump the lifecycle generation ----------------
    {
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        const uint32_t g0 = eng.getLifecycleGeneration();
        eng.prepare(sr, 512);
        eng.publishSnapshot(snap.get());
        eng.pushCommand(cmd(CmdType::TransportStart, 1));
        renderBlocks(eng, 2, 512);
        const bool wasActive = eng.isTransportActive();
        const uint32_t g1 = eng.getLifecycleGeneration();
        eng.release();
        const uint32_t g2 = eng.getLifecycleGeneration();
        eng.prepare(48000.0, 256);
        const uint32_t g3 = eng.getLifecycleGeneration();
        report(wasActive && g1 == g0 + 1 && g2 == g1 + 1 && g3 == g2 + 1 && ! eng.isTransportActive()
                   && ! eng.isSequencerRunning(),
               "N-12: prepare / release bump the engine generation (UI clears Play / sequencer flags)",
               "gen " + juce::String((int) g0) + "->" + juce::String((int) g1) + "->" + juce::String((int) g2)
               + "->" + juce::String((int) g3));
        eng.publishSnapshot(nullptr);
    }

    // ---- N-13: 8192-sample blocks at 44.1 kHz (186 ms per block) --------------
    {
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, 8192);
        eng.publishSnapshot(snap.get());
        renderBlocks(eng, 1, 8192);
        Command c = cmd(CmdType::PlaySegment, 0, 0);
        c.stampMs = (juce::Time::getMillisecondCounter() - 190u) | 1u;   // waited one big block
        eng.pushCommand(c);
        auto r = renderBlocks(eng, 1, 8192);
        const bool played = firstNonZero(r.L) >= 0;
        eng.pushCommand(cmd(CmdType::AllSoundOff));
        renderBlocks(eng, 1, 8192);
        Command stale = cmd(CmdType::PlaySegment, 0, 0);
        stale.stampMs = (juce::Time::getMillisecondCounter() - 2000u) | 1u;   // a real stall
        eng.pushCommand(stale);
        auto r2 = renderBlocks(eng, 1, 8192);
        report(played && firstNonZero(r2.L) < 0 && eng.getStaleThresholdMs() >= 550,
               "N-13: 8192-sample blocks: a 190 ms old trigger plays, a 2 s old one is dropped",
               "threshold=" + juce::String((int) eng.getStaleThresholdMs()) + " ms played=" + juce::String(played ? "yes" : "no"));
        eng.publishSnapshot(nullptr);
    }

    // ---- N-18: triggers during bypass are ignored ------------------------------
    {
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, 512);
        eng.publishSnapshot(snap.get());
        juce::AudioBuffer<float> buf(2, 512);
        renderBlocks(eng, 1, 512);
        eng.processBypassed(buf, 0, 512);
        eng.pushCommand(cmd(CmdType::PlaySegment, 0, 0));
        eng.pushCommand(cmd(CmdType::NoteOn, 60));
        eng.pushCommand(cmd(CmdType::TransportStart, 7));
        eng.pushCommand(cmd(CmdType::SeqStart, 1, 9));
        RawMidi rm; rm.b0 = 0x90; rm.b1 = 60; rm.b2 = 100;
        eng.pushMidi(rm);
        for (int i = 0; i < 4; ++i) eng.processBypassed(buf, 0, 512);
        bool transportStoppedEcho = false, seqStoppedEcho = false;
        Notification n;
        while (eng.popNotification(n))
        {
            if (n.type == NoteType::TransportStopped && n.i0 == 7) transportStoppedEcho = true;
            if (n.type == NoteType::SequencerStopped && n.i1 == 9) seqStoppedEcho = true;
        }
        auto r = renderBlocks(eng, 4, 512);   // un-bypassed
        report(firstNonZero(r.L) < 0 && ! eng.hasActiveVoices() && eng.getBypassDroppedCount() == 5
                   && transportStoppedEcho && seqStoppedEcho && ! eng.isTransportActive(),
               "N-18: triggers queued during bypass do not sound on un-bypass (UI told transport / sequencer did not start)",
               "bypassDropped=" + juce::String(eng.getBypassDroppedCount()));
        eng.publishSnapshot(nullptr);
    }

    // ---- N-24: stall recovery skipped during an offline bounce -----------------
    {
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.setStallRecovery(true);
        eng.prepare(sr, 512);
        eng.publishSnapshot(snap.get());
        HostInfo off; off.offline = true;
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);   // looped chop: holds
        buf.clear(); eng.process(buf, 0, 512, &midi, off);
        midi.clear();
        juce::Thread::sleep(600);   // a slow offline block
        for (int i = 0; i < 4; ++i) { buf.clear(); eng.process(buf, 0, 512, &midi, off); }
        const bool heldOffline = std::abs(buf.getSample(0, 511)) > 0.5f;
        juce::Thread::sleep(600);   // a real-time stall
        for (int i = 0; i < 4; ++i) { buf.clear(); eng.process(buf, 0, 512, &midi, HostInfo {}); }
        const bool releasedRealtime = ! eng.hasActiveVoices();
        report(heldOffline && releasedRealtime, "N-24: a 600 ms offline block keeps held notes; a real-time stall still releases them", "");
        eng.publishSnapshot(nullptr);
    }

    // ---- N-18: PPQ locate tolerance ~10 ms (FL PPQ rounding / tempo ramps) ----
    {
        microFadeEnabledFlag().store(false);
        const double sr48 = 48000.0;
        SModeSegmentModel m2;
        fillModel(m2, 1, 6.0, sr48, 0);
        m2.addSegment(0, 0, (int64_t) (3.0 * sr48));
        std::vector<LaneDesc> lanes(1);
        lanes[0].numCells = 4;
        SlotDesc sd; sd.empty = false; sd.stem = 0; sd.segIndex = 0; sd.gridCell = 0; sd.cellSpan = 4;
        lanes[0].slots.push_back(sd);
        auto s2 = m2.buildPlaybackSnapshot(lanes);
        fillSegmentFx(*s2, m2, lanes);
        s2->seq = 1;
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr48, 480);
        eng.publishSnapshot(s2.get());
        eng.pushCommand(cmd(CmdType::SeqStart, 1, 1));
        auto r = renderBlocks(eng, 300, 480, [&](int b, juce::MidiBuffer&, HostInfo& h)
        {
            h.valid = true; h.playing = true; h.ppqValid = true; h.bpm = 120.0;
            const double jitterBeats = ((b % 2) ? 1.0 : -1.0) * 0.004 * 2.0;   // +-4 ms of beats
            h.ppq = (double) b * 480 * 2.0 / sr48 + (b > 0 ? jitterBeats : 0.0);
        });
        // The slot covers 4 beats = 96000 samples; it must sound continuously.
        int gaps = 0;
        for (int i = 10; i < 90000; ++i) if (std::abs(r.L[(size_t) i]) < 0.5f) ++gaps;
        report(gaps == 0, "N-18: +-4 ms PPQ jitter is not a locate (no sequencer re-trigger / fade every block)",
               "samplesBelowHalf=" + juce::String(gaps));
        eng.publishSnapshot(nullptr);
        microFadeEnabledFlag().store(true);
    }

    // ---- N-18: licence gate flip ramps over 5 ms (no hard cut / hard start) ----
    {
        GateRamp gr;
        gr.prepare(sr, true, 5.0);
        std::vector<float> out;
        juce::AudioBuffer<float> b(2, 256);
        const bool openPattern[] = { true, true, false, false, false, true, true, true };
        for (bool open : openPattern)
        {
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill(b.getWritePointer(ch), 1.0f, 256);   // DC
            gr.apply(b, 0, 256, open);
            out.insert(out.end(), b.getReadPointer(0), b.getReadPointer(0) + 256);
        }
        const float step = maxStep(out);
        const int rampLen = (int) std::lround(0.005 * sr);
        // closed at sample 512: silent from 512 + rampLen; reopened at 1280: full at 1280 + rampLen
        const bool closed = out[(size_t) 512 + rampLen] == 0.0f && out[(size_t) 1279] == 0.0f;
        const bool reopened = out[(size_t) 1280 + rampLen] == 1.0f && out.back() == 1.0f;
        report(step <= 1.0f / rampLen + 1.0e-4f && closed && reopened,
               "N-18: licence gate flip = 5 ms ramp out / in (no hard cut on DC)",
               "maxStep=" + juce::String(step, 5) + " (1/ramp=" + juce::String(1.0f / rampLen, 5) + ", hard cut = 1.0)");
    }

    // ---- N-18: LOCK transpose off 0 with many voices: entries spread over blocks
    {
        SModeSegmentModel m3;
        fillModel(m3, 1, 10.0, sr, 1);
        for (int k = 0; k < 30; ++k)
        {
            const int idx = m3.addSegment(0, k * 1000, k * 1000 + (int64_t) (8.0 * sr));
            m3.assignMidiNote(0, idx, 40 + k);
        }
        auto s3 = m3.buildPlaybackSnapshot({});
        s3->seq = 1;
        EngineParams p;
        auto engHolder = std::make_unique<PlaybackEngine>(p);
        auto& eng = *engHolder;
        eng.prepare(sr, 512);
        eng.publishSnapshot(s3.get());
        renderBlocks(eng, 2, 512, [&](int b, juce::MidiBuffer& m, HostInfo&)
        {
            if (b == 0) for (int k = 0; k < 30; ++k) m.addEvent(juce::MidiMessage::noteOn(1, 40 + k, (juce::uint8) 20), 0);
        });
        p.globalSemis.store(3.0f);
        renderBlocks(eng, 1, 512);
        int total = 0;
        const int afterOne = PlaybackEngineTestAccess::stretchVoices(eng, total);
        renderBlocks(eng, 6, 512);
        int total2 = 0;
        const int afterSeven = PlaybackEngineTestAccess::stretchVoices(eng, total2);
        p.globalSemis.store(0.0f);
        report(afterOne <= PlaybackEngine::kMaxStretchEntriesPerBlock && afterSeven == total2 && total2 == 30,
               "N-18: LOCK transpose with 30 voices: pitch-shifter entries spread over blocks (CPU)",
               "afterBlock1=" + juce::String(afterOne) + "/" + juce::String(total)
               + " afterBlock7=" + juce::String(afterSeven) + "/" + juce::String(total2));
        eng.publishSnapshot(nullptr);
    }
}

struct StdoutLogger : juce::Logger
{
    void logMessage(const juce::String& m) override { std::fprintf(stderr, "%s\n", m.toRawUTF8()); }
};

int main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    for (int t : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(t, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(t, _CRTDBG_FILE_STDERR);
    }
#endif
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    std::printf("SAMPLAR EngineTests\n");
    testNoAllocations();
    testOnsetAndRelease();
    testOfflineEqualsRealtime();
    testSequencerHostSync();
    testMisc();
    testUiRound();
    testLongReleaseLane();
    testDrumRollNoStarvation();
    testHostGarbage();
    testTransportMuteAfterResample();
    testLifecycleAndCommands();
    std::printf("%s (%d failure(s))\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures);
    juce::Logger::setCurrentLogger(nullptr);
    return gFailures == 0 ? 0 : 1;
}
