#pragma once

// SModeUiSelfTest - debug-only UI layout self-test, driven by the VST3SmokeHost
// (`--ui-sweep <dir>`). INERT unless the environment variable
// SAMPLAR_UI_SWEEP_DIR is set when SModePage is constructed: createIfEnabled()
// then returns nullptr and nothing else in this file runs.
//
// Protocol (process-wide Win32 environment, so host and plugin DLL share it):
//   SAMPLAR_UI_SWEEP_DIR = output folder (PNG snapshots + result text files)
//   SAMPLAR_UI_CMD       = "<seq>|<verb>|<arg>"  (polled every 50 ms)
// Every command writes "<dir>/ack_<seq>.txt" when it is done. Verbs:
//   sweep|<tag>   hit-test sweep of the editor tree + snapshot -> <tag>.png/.txt
//   load|         build 4 synthetic sine "stems" into the model (page callback)
//   loadlong|<s>  same, with a <s>-second drum stem (BPM analysis runs long)
//   notice|<k>    in-page notice: missing / unavailable / off
//   unload|       eject (back to the empty state)
//   lanes|<n>     set the number of sequencer lanes (1..4)
//   collapse|0/1  collapse / expand the sequencer
//   scroll|top/bottom  scroll the sequencer viewport
//   perf|<tag>    90 playhead frames, each flushed with performAnyPendingRepaintsNow
//   midi|<step>   MIDI CC learn UI: menu_pitch / menu_insp (open the right-click
//                 menu; the ack lists its items), learn_pitch / learn_insp
//                 (learn + injected CC, PASS/FAIL), clear, close (dismiss menus)
// Open popup menus (separate desktop windows) are drawn into the sweep PNG at
// their screen position, so the menu can be inspected in the snapshot.
// Every sweep also runs the page's extraChecks (key label fits the top bar,
// chop inspector present + usable, LINK ALL off by default ...).
//
// Sweep rule (mistakes.md Mistake 4): every visible + enabled interactive
// component must be >= 16x16, inside its parent, not covered by a later
// sibling, and the editor's getComponentAt() at a 3x3 grid of its visible
// points must return it or a descendant. Parts clipped by a Viewport are
// reported as INFO (scroll), never silently skipped.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../WaveDragArea.h"
#include "SequencerSlotComponent.h"
#include "StemWaveformComponent.h"
#include <functional>
#include <memory>
#include <typeinfo>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace samplar {

class SModeUiSelfTest : private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void()>         loadSynthetic;
        std::function<void(double)>   loadLong;         // drum stem length, seconds
        std::function<void(const juce::String&)> notice; // "missing" / "unavailable" / "off"
        std::function<void()>         unload;
        std::function<void(int)>      setSeqLanes;
        std::function<void(bool)>     setCollapsed;
        std::function<void(bool)>     scrollSeq;        // true = bottom
        std::function<void(double)>   playheadTick;     // seconds
        std::function<juce::String(const juce::String&)> midi;   // MIDI learn UI steps
        // Page-specific assertions run with every sweep (key label, chop
        // inspector, LINK ALL default...). Each returned line is one FAIL.
        std::function<juce::StringArray()> extraChecks;
    };

    static juce::String readEnv(const char* name)
    {
       #if JUCE_WINDOWS
        wchar_t buf[2048] = {};
        const auto len = ::GetEnvironmentVariableW(juce::String(name).toWideCharPointer(), buf, 2047);
        if (len == 0 || len >= 2047) return {};
        return juce::String(buf);
       #else
        return juce::SystemStats::getEnvironmentVariable(name, {});
       #endif
    }

    static std::unique_ptr<SModeUiSelfTest> createIfEnabled(juce::Component& page, Callbacks cb)
    {
        const auto dir = readEnv("SAMPLAR_UI_SWEEP_DIR");
        if (dir.isEmpty()) return nullptr;
        juce::Logger::writeToLog("[UiSelfTest] ENABLED, dir=" + dir);
        return std::unique_ptr<SModeUiSelfTest>(new SModeUiSelfTest(page, std::move(cb), juce::File(dir)));
    }

    ~SModeUiSelfTest() override { stopTimer(); }

private:
    SModeUiSelfTest(juce::Component& page, Callbacks cb, juce::File dir)
        : page_(page), cb_(std::move(cb)), dir_(std::move(dir))
    {
        dir_.createDirectory();
        startTimer(50);
    }

    void timerCallback() override
    {
        const auto cmd = readEnv("SAMPLAR_UI_CMD");
        if (cmd.isEmpty() || cmd == lastCmd_) return;
        lastCmd_ = cmd;
        juce::StringArray parts;
        parts.addTokens(cmd, "|", {});
        while (parts.size() < 3) parts.add({});
        const auto seq = parts[0], verb = parts[1], arg = parts[2];
        juce::Logger::writeToLog("[UiSelfTest] cmd seq=" + seq + " verb=" + verb + " arg=" + arg);

        juce::String ack = "ok";
        if      (verb == "sweep")    ack = runSweep(arg);
        else if (verb == "load"     && cb_.loadSynthetic) cb_.loadSynthetic();
        else if (verb == "loadlong" && cb_.loadLong)      cb_.loadLong(arg.getDoubleValue());
        else if (verb == "notice"   && cb_.notice)        cb_.notice(arg);
        else if (verb == "unload"   && cb_.unload)        cb_.unload();
        else if (verb == "lanes"    && cb_.setSeqLanes)   cb_.setSeqLanes(arg.getIntValue());
        else if (verb == "collapse" && cb_.setCollapsed)  cb_.setCollapsed(arg.getIntValue() != 0);
        else if (verb == "scroll"   && cb_.scrollSeq)     cb_.scrollSeq(arg == "bottom");
        else if (verb == "perf")     ack = runPerf(arg);
        else if (verb == "midi"     && cb_.midi)          ack = cb_.midi(arg);
        else ack = "unknown verb";

        dir_.getChildFile("ack_" + seq + ".txt").replaceWithText(ack);
    }

    // The sweep root is the RootComponent (the page's parent), i.e. the whole
    // SAMPLAR UI without the editor-level licence panel. While unlicensed the
    // editor disables the root; the sweep re-enables it only for the duration
    // of the synchronous check + snapshot (no events are pumped in between) and
    // restores it before returning, so this never unlocks anything.
    juce::Component& root() const
    {
        if (auto* p = page_.getParentComponent())
            return *p;
        return page_;
    }

    struct ScopedEnable
    {
        explicit ScopedEnable(juce::Component& c) : comp(c), was(c.isEnabled()) { if (! was) comp.setEnabled(true); }
        ~ScopedEnable() { if (! was) comp.setEnabled(false); }
        juce::Component& comp;
        bool was;
    };

    //==========================================================================
    static juce::String describe(const juce::Component& c)
    {
        juce::String type = typeid(c).name();
        type = type.fromLastOccurrenceOf("::", false, false).fromLastOccurrenceOf(" ", false, false);
        juce::String name = c.getName();
        if (auto* b = dynamic_cast<const juce::Button*>(&c))
            if (b->getButtonText().isNotEmpty()) name = b->getButtonText();
        if (name.isEmpty()) name = c.getComponentID();
        // ASCII-only log: replace non-ASCII glyph button texts with their code point
        juce::String ascii;
        for (auto ch : name)
            ascii << ((ch >= 32 && ch < 127) ? juce::String::charToString(ch) : "U+" + juce::String::toHexString((int) ch));
        return type + " '" + ascii + "'";
    }

    static bool isInteractive(const juce::Component& c)
    {
        return dynamic_cast<const juce::Button*>(&c)      != nullptr
            || dynamic_cast<const juce::Slider*>(&c)      != nullptr
            || dynamic_cast<const juce::ComboBox*>(&c)    != nullptr
            || dynamic_cast<const juce::TextEditor*>(&c)  != nullptr
            || dynamic_cast<const juce::ListBox*>(&c)     != nullptr
            || dynamic_cast<const WaveDragArea*>(&c)      != nullptr
            || dynamic_cast<const SequencerSlotComponent*>(&c) != nullptr
            || dynamic_cast<const StemWaveformComponent*>(&c)  != nullptr;
    }

    // Internals of these are JUCE-owned sub-widgets (labels, rows, scrollbars).
    static bool isLeafForSweep(const juce::Component& c)
    {
        return dynamic_cast<const juce::Slider*>(&c)     != nullptr
            || dynamic_cast<const juce::ComboBox*>(&c)   != nullptr
            || dynamic_cast<const juce::TextEditor*>(&c) != nullptr
            || dynamic_cast<const juce::ListBox*>(&c)    != nullptr
            || dynamic_cast<const juce::ScrollBar*>(&c)  != nullptr;
    }

    struct Tally { int checked = 0, fails = 0, infos = 0; juce::StringArray lines; };

    void walk(juce::Component& r, juce::Component& comp, Tally& t)
    {
        for (int i = 0; i < comp.getNumChildComponents(); ++i)
        {
            auto* c = comp.getChildComponent(i);
            if (c == nullptr || ! c->isVisible()) continue;
            if (isInteractive(*c) && c->isEnabled())
                check(r, *c, t);
            if (! isLeafForSweep(*c))
                walk(r, *c, t);
        }
    }

    void check(juce::Component& r, juce::Component& c, Tally& t)
    {
        ++t.checked;
        auto* parent = c.getParentComponent();
        const auto inRoot = r.getLocalArea(parent, c.getBounds());
        juce::StringArray problems;

        if (c.getWidth() < 16 || c.getHeight() < 16)
            problems.add("too small " + juce::String(c.getWidth()) + "x" + juce::String(c.getHeight()));

        if (! parent->getLocalBounds().contains(c.getBounds()))
            problems.add("outside parent " + parent->getLocalBounds().toString());

        // Later siblings that would swallow clicks on any part of c.
        const int idx = parent->getIndexOfChildComponent(&c);
        for (int j = idx + 1; j < parent->getNumChildComponents(); ++j)
        {
            auto* s = parent->getChildComponent(j);
            if (s == nullptr || ! s->isVisible() || ! s->getBounds().intersects(c.getBounds())) continue;
            bool selfClicks = false, childClicks = false;
            s->getInterceptsMouseClicks(selfClicks, childClicks);
            bool covers = selfClicks;
            if (! covers && childClicks)
                for (int k = 0; k < s->getNumChildComponents() && ! covers; ++k)
                    if (auto* sc = s->getChildComponent(k); sc != nullptr && sc->isVisible()
                        && sc->getBounds().translated(s->getX(), s->getY()).intersects(c.getBounds()))
                        covers = true;
            if (covers)
                problems.add("overlapped by sibling " + describe(*s) + " " + s->getBounds().toString());
        }

        // Visible part (clipped by every ancestor). Clipping by a Viewport = scroll (INFO).
        auto visible = inRoot;
        bool clippedByViewport = false;
        for (auto* a = parent; a != nullptr && a != &r; a = a->getParentComponent())
        {
            const auto aBounds = r.getLocalArea(a, a->getLocalBounds());
            const auto clipped = visible.getIntersection(aBounds);
            if (clipped != visible && dynamic_cast<juce::Viewport*>(a->getParentComponent()) != nullptr)
                clippedByViewport = true;
            visible = clipped;
        }
        visible = visible.getIntersection(r.getLocalBounds());

        juce::String info;
        if (visible.getWidth() < 16 || visible.getHeight() < 16)
        {
            if (clippedByViewport) info = "scrolled out of view";
            else if (problems.isEmpty()) problems.add("not visible on screen, visible=" + visible.toString());
        }
        else
        {
            if (clippedByViewport && visible != inRoot) info = "partly scrolled, visible=" + visible.toString();
            static const float fr[] = { 0.2f, 0.5f, 0.8f };
            bool bad = false;
            for (float fx : fr)
                for (float fy : fr)
                {
                    if (bad) break;
                    const juce::Point<int> p(visible.getX() + juce::roundToInt(fx * (float) (visible.getWidth()  - 1)),
                                             visible.getY() + juce::roundToInt(fy * (float) (visible.getHeight() - 1)));
                    auto* hit = r.getComponentAt(p);
                    if (hit != &c && ! c.isParentOf(hit))
                    {
                        problems.add("point " + p.toString() + " hits " + (hit ? describe(*hit) : juce::String("nothing")));
                        bad = true;   // one bad point is enough evidence
                    }
                }
        }

        const auto head = describe(c) + " bounds=" + inRoot.toString();
        if (! problems.isEmpty())
        {
            ++t.fails;
            t.lines.add("FAIL " + head + " :: " + problems.joinIntoString("; "));
        }
        else if (info.isNotEmpty())
        {
            ++t.infos;
            t.lines.add("INFO " + head + " :: " + info);
        }
        else
            t.lines.add("OK   " + head);
    }

    juce::String runSweep(const juce::String& tag)
    {
        auto& r = root();
        ScopedEnable enable(r);
        Tally t;
        walk(r, r, t);
        if (cb_.extraChecks)
        {
            for (const auto& problem : cb_.extraChecks())
            {
                ++t.fails;
                t.lines.add("FAIL page-check :: " + problem);
            }
            t.lines.add("INFO page-checks ran");
        }
        const auto summary = "SUMMARY tag=" + tag + " editor=" + juce::String(r.getWidth()) + "x" + juce::String(r.getHeight())
                           + " checked=" + juce::String(t.checked) + " fail=" + juce::String(t.fails)
                           + " info=" + juce::String(t.infos);
        t.lines.add(summary);
        dir_.getChildFile(tag + ".txt").replaceWithText(t.lines.joinIntoString("\n") + "\n");

        auto snap = r.createComponentSnapshot(r.getLocalBounds(), true, 1.0f);
        // Popup menus are their own desktop windows: draw every visible one
        // over the snapshot where it sits on screen (MIDI-learn menu checks).
        {
            juce::Graphics g(snap);
            auto& desk = juce::Desktop::getInstance();
            for (int i = 0; i < desk.getNumComponents(); ++i)
            {
                auto* w = desk.getComponent(i);
                if (w == nullptr || ! w->isVisible() || w == r.getTopLevelComponent()) continue;
                if (! juce::String(typeid(*w).name()).contains("MenuWindow")) continue;
                const auto pos = r.getLocalPoint(nullptr, w->getScreenPosition());
                g.drawImageAt(w->createComponentSnapshot(w->getLocalBounds(), true, 1.0f), pos.x, pos.y);
                juce::Logger::writeToLog("[UiSelfTest] popup menu drawn at " + pos.toString() + " size " + juce::String(w->getWidth())
                            + "x" + juce::String(w->getHeight()));
            }
        }
        auto png = dir_.getChildFile(tag + ".png");
        png.deleteFile();
        if (juce::FileOutputStream out(png); out.openedOk())
            juce::PNGImageFormat().writeImageToStream(snap, out);

        juce::Logger::writeToLog("[UiSelfTest] " + summary);
        return summary;
    }

    juce::String runPerf(const juce::String& tag)
    {
        if (! cb_.playheadTick) return "no playhead callback";
        auto& r = root();
        auto* peer = r.getPeer();
        StemWaveformComponent::debugWaveCacheBuilds_ = 0;
        constexpr int kFrames = 90;
        StemWaveformComponent::debugPaintMs_ = 0.0;
        StemWaveformComponent::debugPaintCount_ = 0;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < kFrames; ++i)
        {
            cb_.playheadTick(1.0 + i / 30.0);
            if (peer != nullptr) peer->performAnyPendingRepaintsNow();
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;

        // Full-lane paint cost (independent of what is on screen): render every
        // stem lane completely, kFrames times, with the playhead moving.
        std::vector<juce::Component*> lanes;
        std::function<void(juce::Component&)> find = [&](juce::Component& c)
        {
            for (auto* ch : c.getChildren())
            {
                if (dynamic_cast<StemWaveformComponent*>(ch) != nullptr && ch->isVisible()) lanes.push_back(ch);
                else find(*ch);
            }
        };
        find(r);
        StemWaveformComponent::debugPaintMs_ = 0.0;
        StemWaveformComponent::debugPaintCount_ = 0;
        const double f0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < kFrames; ++i)
        {
            cb_.playheadTick(4.0 + i / 30.0);
            for (auto* l : lanes)
                juce::ignoreUnused(l->createComponentSnapshot(l->getLocalBounds(), false, 1.0f));
        }
        const double fullMs = juce::Time::getMillisecondCounterHiRes() - f0;
        const auto full = " full_lane_paint: lanes=" + juce::String((int) lanes.size())
            + " ms_per_frame=" + juce::String(fullMs / kFrames, 3)
            + " paint_ms_per_frame=" + juce::String(StemWaveformComponent::debugPaintMs_ / kFrames, 3);

        const auto res = "PERF tag=" + tag + full + " | screen: frames=" + juce::String(kFrames)
            + " wall_ms_per_frame=" + juce::String(ms / kFrames, 3)
            + " lane_paint_calls=" + juce::String(StemWaveformComponent::debugPaintCount_)
            + " lane_paint_ms_per_frame=" + juce::String(StemWaveformComponent::debugPaintMs_ / kFrames, 3)
            + " wave_cache_builds=" + juce::String(StemWaveformComponent::debugWaveCacheBuilds_)
            + " peer=" + juce::String(peer != nullptr ? 1 : 0);
        dir_.getChildFile(tag + ".txt").replaceWithText(res + "\n");
        juce::Logger::writeToLog("[UiSelfTest] " + res);
        return res;
    }

    juce::Component& page_;
    Callbacks cb_;
    juce::File dir_;
    juce::String lastCmd_;
};

} // namespace samplar
