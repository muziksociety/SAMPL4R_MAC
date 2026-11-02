// VST3SmokeHost - loads the SAMPLAR VST3 like a DAW would, without a DAW.
// (Ported from Sample Monsta 5 tools/vst3_smoke_host.)
//
// Steps (each one timed): load + instantiate, optional setStateInformation from
// a previous run, prepareToPlay(44100, 512), a burst of ~5 s silence + ~5 s test
// tone (with MIDI notes) through processBlock on an audio thread (NaN/Inf check),
// open the editor in a DocumentWindow and pump the message loop while a paced
// audio thread keeps calling processBlock (like a host), close the editor,
// reopen it (persistent root: must be fast), close again, getStateInformation
// (saved for the next run), releaseResources, delete the instance.
//
// Exit code 0 = every step passed. A watchdog kills the process (exit 10) if a
// step hangs. Usage:
//   VST3SmokeHost [--plugin <path.vst3>] [--state <file>] [--show-ms N]
//                 [--reopen-budget-ms N] [--unload-budget-ms N] [--two-instances]
//                 [--ui-sweep <outdir>] [--session-roundtrip <song.wav>] [--stacks]
// --ui-sweep: after the first editor open, resize the editor to 850x500,
// 1060x724 and 1810x1130 and, in each UI state (empty, synthetic stems with 1
// and 4 sequencer lanes, sequencer collapsed, after eject), ask the plugin's
// debug self-test (SModeUiSelfTest, enabled by SAMPLAR_UI_SWEEP_DIR) to run a
// hit-test sweep and save <outdir>/<state>_<WxH>.png/.txt. Any FAIL -> exit 1.
// The host cannot reach the plugin's components (VST3 = separate DLL + its own
// HWND), so the sweep runs inside the plugin; the host drives it through the
// process environment (SAMPLAR_UI_CMD) and waits for <outdir>/ack_<n>.txt.
// --session-roundtrip: DAW project round trip (see runSessionRoundtrip below).
// --two-instances: a second instance (B) opens its editor, then the first one (A)
// reopens + closes its editor and is unloaded WHILE B's editor stays open; B keeps
// running and is closed last (process-global ServiceLocator + queue owner lock).
// --stacks (diagnostic, Windows x64): sampling profile of the main thread during
// each instance delete, printed when the delete took >= 200 ms ("[Prof]" lines,
// see StackSampler.h).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>

#include "StackSampler.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace {

void log (const juce::String& s)
{
    std::printf ("[Smoke] %s\n", s.toRawUTF8());
    std::fflush (stdout);
}

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

StackSampler* gSampler = nullptr;   // set in main() when --stacks
struct SampledStep
{
    explicit SampledStep (const char* name) { if (gSampler) gSampler->begin (name); }
    ~SampledStep()                         { if (gSampler) gSampler->end(); }
};

struct Args
{
    juce::File plugin { juce::String (VST3_SMOKE_DEFAULT_PLUGIN) };
    juce::File stateFile;
    int showMs = 8000;
    double reopenBudgetMs = 500.0;
    double unloadBudgetMs = 1500.0;
    bool twoInstances = false;
    bool stacks = false;           // --stacks: sample the main thread's stack when an unload step is slow
    juce::File uiSweepDir;
    juce::File roundtripWav;
    juce::File bpmUnloadDir;       // --bpm-unload <dir>: unload while BPM analysis runs
    double bpmUnloadSongSec = 600.0;
};

Args parseArgs (const juce::StringArray& a)
{
    Args r;
    for (int i = 0; i < a.size(); ++i)
    {
        const bool hasNext = i + 1 < a.size();
        if (a[i] == "--plugin" && hasNext)                r.plugin = juce::File (a[++i]);
        else if (a[i] == "--state" && hasNext)            r.stateFile = juce::File (a[++i]);
        else if (a[i] == "--show-ms" && hasNext)          r.showMs = a[++i].getIntValue();
        else if (a[i] == "--reopen-budget-ms" && hasNext) r.reopenBudgetMs = a[++i].getDoubleValue();
        else if (a[i] == "--unload-budget-ms" && hasNext) r.unloadBudgetMs = a[++i].getDoubleValue();
        else if (a[i] == "--two-instances")               r.twoInstances = true;
        else if (a[i] == "--stacks")                      r.stacks = true;
        else if (a[i] == "--ui-sweep" && hasNext)         r.uiSweepDir = juce::File (a[++i]);
        else if (a[i] == "--session-roundtrip" && hasNext) r.roundtripWav = juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (a[++i]));
        else if (a[i] == "--bpm-unload" && hasNext)       r.bpmUnloadDir = juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (a[++i]));
        else if (a[i] == "--bpm-song-sec" && hasNext)     r.bpmUnloadSongSec = a[++i].getDoubleValue();
        else log ("WARNING: unknown argument '" + a[i] + "'");
    }
    return r;
}

//==============================================================================
// Kills the process if one step runs longer than its limit (a hang is a fail).
class Watchdog
{
public:
    Watchdog() : thread_ ([this] { run(); }) {}
    ~Watchdog()
    {
        stop_ = true;
        thread_.join();
    }

    void arm (const char* step, int limitMs)
    {
        step_ = step;
        deadline_ = nowMs() + limitMs;
    }

private:
    void run()
    {
        while (! stop_)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            const double d = deadline_.load();
            if (d > 0.0 && nowMs() > d)
            {
                std::printf ("[Smoke] FAIL: HANG - step '%s' exceeded its time limit -> exit 10\n", step_.load());
                std::fflush (stdout);
                std::_Exit (10);
            }
        }
    }

    std::atomic<bool> stop_ { false };
    std::atomic<const char*> step_ { "" };
    std::atomic<double> deadline_ { 0.0 };
    std::thread thread_;
};

//==============================================================================
// processBlock caller. burst(): renders as fast as possible. startPaced():
// one block every block duration (a host's audio thread) until stopPaced().
class AudioDriver
{
public:
    AudioDriver (juce::AudioPluginInstance& p, double sr, int block)
        : plugin_ (p), sampleRate_ (sr), blockSize_ (block),
          buffer_ (juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels())), block)
    {
    }

    ~AudioDriver() { stopPaced(); }

    // seconds of silence, then seconds of a 440 Hz tone on the inputs + a MIDI
    // note every 0.5 s. Returns false on NaN/Inf or an exception.
    bool burst (double silenceSec, double toneSec)
    {
        const int silenceBlocks = (int) std::ceil (silenceSec * sampleRate_ / blockSize_);
        const int toneBlocks    = (int) std::ceil (toneSec * sampleRate_ / blockSize_);
        std::atomic<bool> ok { true }, done { false };
        std::thread t ([&]
        {
            for (int b = 0; b < silenceBlocks + toneBlocks && ok; ++b)
                ok = renderBlock (b >= silenceBlocks);
            done = true;
        });
        // The message thread keeps pumping meanwhile (the plugin posts callAsync hops).
        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        t.join();
        log ("  burst: " + juce::String (silenceBlocks) + " silence + " + juce::String (toneBlocks)
             + " tone blocks, peak=" + juce::String (peak_, 4) + " nonFinite=" + juce::String (nonFinite_)
             + " exceptions=" + juce::String (exceptions_));
        return ok;
    }

    void startPaced()
    {
        running_ = true;
        paced_ = std::thread ([this]
        {
            const auto blockMs = 1000.0 * blockSize_ / sampleRate_;
            double next = nowMs();
            while (running_)
            {
                if (! renderBlock (true)) failed_ = true;
                ++pacedBlocks_;
                next += blockMs;
                const double wait = next - nowMs();
                if (wait > 0) std::this_thread::sleep_for (std::chrono::microseconds ((int) (wait * 1000.0)));
                else          next = nowMs();   // fell behind: do not try to catch up
            }
        });
    }

    void stopPaced()
    {
        running_ = false;
        if (paced_.joinable()) paced_.join();
    }

    bool failed() const       { return failed_ || nonFinite_ > 0 || exceptions_ > 0; }
    long pacedBlocks() const  { return pacedBlocks_; }
    long nonFinite() const    { return nonFinite_; }
    double maxBlockMs() const { return maxBlockMs_; }

private:
    bool renderBlock (bool tone)
    {
        juce::MidiBuffer midi;
        buffer_.clear();
        if (tone)
        {
            for (int ch = 0; ch < juce::jmin (2, buffer_.getNumChannels()); ++ch)
            {
                auto* d = buffer_.getWritePointer (ch);
                for (int i = 0; i < blockSize_; ++i)
                    d[i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) (phase_ + i) / sampleRate_);
            }
            const auto every = (juce::int64) (0.5 * sampleRate_);
            if ((phase_ / every) != ((phase_ + blockSize_) / every))
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (int) ((phase_ / every) % 12), (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOff (1, 36 + (int) ((phase_ / every) % 12)), blockSize_ - 1);
            }
        }
        phase_ += blockSize_;

        const double t0 = nowMs();
        try
        {
            plugin_.processBlock (buffer_, midi);
        }
        catch (...)
        {
            ++exceptions_;
            return false;
        }
        maxBlockMs_ = juce::jmax (maxBlockMs_.load(), nowMs() - t0);
        ++rendered_;

        for (int ch = 0; ch < buffer_.getNumChannels(); ++ch)
        {
            const auto* d = buffer_.getReadPointer (ch);
            for (int i = 0; i < blockSize_; ++i)
            {
                if (! std::isfinite (d[i])) { ++nonFinite_; return false; }
                peak_ = juce::jmax (peak_.load(), std::abs (d[i]));
            }
        }
        return true;
    }

    juce::AudioPluginInstance& plugin_;
    const double sampleRate_;
    const int blockSize_;
    juce::AudioBuffer<float> buffer_;
    juce::int64 phase_ = 0;
    std::atomic<long> rendered_ { 0 }, nonFinite_ { 0 }, exceptions_ { 0 }, pacedBlocks_ { 0 };
    std::atomic<float> peak_ { 0.0f };
    std::atomic<double> maxBlockMs_ { 0.0 };
    std::atomic<bool> running_ { false }, failed_ { false };
    std::thread paced_;
};

//==============================================================================
class HostWindow : public juce::DocumentWindow
{
public:
    HostWindow() : DocumentWindow ("VST3SmokeHost - SAMPLAR", juce::Colours::black, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
    }
    void closeButtonPressed() override { log ("  (window close button ignored - the script closes the editor)"); }
};

struct Result
{
    juce::StringArray lines;
    bool ok = true;

    void step (const juce::String& name, double ms, bool pass, const juce::String& extra = {})
    {
        const auto line = juce::String (pass ? "PASS " : "FAIL ") + name.paddedRight (' ', 28)
                          + juce::String (ms, 1).paddedLeft (' ', 9) + " ms" + (extra.isNotEmpty() ? "  " + extra : juce::String());
        lines.add (line);
        log (line);
        ok = ok && pass;
    }
};

// Opens the editor in `window`; returns its time (create + show + first paint pump).
juce::AudioProcessorEditor* openEditor (juce::AudioPluginInstance& p, HostWindow& window, double& ms)
{
    const double t0 = nowMs();
    auto* ed = p.createEditorIfNeeded();
    if (ed == nullptr) { ms = nowMs() - t0; return nullptr; }
    window.setContentNonOwned (ed, true);
    window.centreWithSize (window.getWidth(), window.getHeight());
    window.setVisible (true);
    window.toFront (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // attach + first paint
    ms = nowMs() - t0;
    return ed;
}

double closeEditor (juce::AudioPluginInstance& p, HostWindow& window, juce::AudioProcessorEditor*& ed)
{
    const double t0 = nowMs();
    window.setVisible (false);
    window.clearContentComponent();
    p.editorBeingDeleted (ed);
    delete ed;
    ed = nullptr;
    return nowMs() - t0;
}

void setEnv (const char* name, const juce::String& value)
{
   #if JUCE_WINDOWS
    ::SetEnvironmentVariableW (juce::String (name).toWideCharPointer(), value.toWideCharPointer());
   #else
    setenv (name, value.toRawUTF8(), 1);
   #endif
}

// Drives the plugin-side UI self-test (see header comment). Returns the number
// of failed sweeps (or -1 if the plugin never answered).
int runUiSweep (juce::AudioProcessorEditor& ed, const juce::File& dir, Result& res)
{
    int seq = 0;
    auto cmd = [&] (const juce::String& verb, const juce::String& arg, int timeoutMs = 20000) -> juce::String
    {
        ++seq;
        auto ack = dir.getChildFile ("ack_" + juce::String (seq) + ".txt");
        ack.deleteFile();
        setEnv ("SAMPLAR_UI_CMD", juce::String (seq) + "|" + verb + "|" + arg);
        const double t0 = nowMs();
        while (! ack.existsAsFile() && nowMs() - t0 < timeoutMs)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        return ack.existsAsFile() ? ack.loadFileAsString().trim() : juce::String ("TIMEOUT");
    };

    struct Size { int w, h; };
    const Size sizes[] = { { 850, 500 }, { 1060, 724 }, { 1810, 1130 } };
    int failedSweeps = 0;

    auto sweepAllSizes = [&] (const juce::String& state, std::function<void()> afterResize)
    {
        for (auto s : sizes)
        {
            ed.setSize (s.w, s.h);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (700);   // resize + async relayout
            if (afterResize) afterResize();
            const auto tag = state + "_" + juce::String (s.w) + "x" + juce::String (s.h);
            const double t0 = nowMs();
            const auto summary = cmd ("sweep", tag);
            const bool pass = summary.contains ("fail=0");
            if (! pass) ++failedSweeps;
            res.step ("ui-sweep " + tag, nowMs() - t0, pass, summary.fromFirstOccurrenceOf ("editor=", true, false));
            if (! pass)
            {
                const auto lines = juce::StringArray::fromLines (dir.getChildFile (tag + ".txt").loadFileAsString());
                for (auto& l : lines)
                    if (l.startsWith ("FAIL")) log ("    " + l);
            }
        }
    };

    log ("ui-sweep: output -> " + dir.getFullPathName());
    if (cmd ("sweep", "probe", 5000) == "TIMEOUT")
    {
        res.step ("ui-sweep handshake", 0.0, false, "plugin self-test did not answer (SAMPLAR_UI_SWEEP_DIR not seen?)");
        return -1;
    }

    sweepAllSizes ("empty", {});

    cmd ("load", {});
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
    cmd ("lanes", "1");
    sweepAllSizes ("loaded1", {});

    ed.setSize (1060, 724);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    log ("  " + cmd ("perf", "perf_loaded1_1060x724"));

    // MIDI CC learn UI: the right-click menus on the stem pitch knob and the
    // chop inspector (snapshots include the open menu), learn via an injected
    // CC, tooltips show the CC, Clear MIDI removes it.
    {
        auto midiStep = [&] (const juce::String& step, const juce::String& mustContain, const juce::String& name)
        {
            const double t0 = nowMs();
            const auto ack = cmd ("midi", step);
            const bool pass = ack.contains (mustContain) && ! ack.startsWith ("FAIL");
            if (! pass) ++failedSweeps;
            res.step ("ui-sweep midi " + name, nowMs() - t0, pass, ack);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);   // let the menu window paint
        };
        auto menuSweep = [&] (const juce::String& tag)
        {
            const double t0 = nowMs();
            const auto summary = cmd ("sweep", tag);
            const bool pass = summary.contains ("fail=0");
            if (! pass) ++failedSweeps;
            res.step ("ui-sweep " + tag, nowMs() - t0, pass, summary.fromFirstOccurrenceOf ("editor=", true, false));
        };
        midiStep ("menu_pitch", "MIDI Learn", "pitch menu (unbound)");
        menuSweep ("midimenu_pitch_1060x724");
        midiStep ("close", "ok", "close");
        midiStep ("learn_pitch", "PASS", "pitch learn");
        midiStep ("menu_pitch", "Clear MIDI (CC 74 ch 1)", "pitch menu (bound)");
        menuSweep ("midimenu_pitch_bound_1060x724");
        midiStep ("close", "ok", "close");
        midiStep ("menu_insp", "MIDI Learn", "inspector menu (unbound)");
        menuSweep ("midimenu_insp_1060x724");
        midiStep ("close", "ok", "close");
        midiStep ("learn_insp", "PASS", "inspector learn");
        midiStep ("menu_insp", "Clear MIDI (CC 7 ch 2)", "inspector menu (bound)");
        menuSweep ("midimenu_insp_bound_1060x724");
        midiStep ("close", "ok", "close");
        midiStep ("clear", "PASS", "clear");
    }

    cmd ("lanes", "4");
    sweepAllSizes ("loaded4top", [&] { cmd ("scroll", "top"); });
    sweepAllSizes ("loaded4bottom", [&] { cmd ("scroll", "bottom"); });

    cmd ("lanes", "1");
    cmd ("collapse", "1");
    sweepAllSizes ("collapsed", {});
    cmd ("collapse", "0");

    cmd ("unload", {});
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    sweepAllSizes ("ejected", {});

    // In-page notices (no modal alerts): restored project with missing audio
    // (Locate song... button) and ONNX Runtime unavailable.
    cmd ("notice", "missing");
    sweepAllSizes ("notice_missing", {});
    cmd ("notice", "unavailable");
    sweepAllSizes ("notice_unavailable", {});
    // Licence kept but a fresh sign-in is required: "Sign in again" strip.
    cmd ("notice", "off");
    cmd ("notice", "signin");
    sweepAllSizes ("notice_signin", {});
    cmd ("notice", "off");

    ed.setSize (1060, 724);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    return failedSweeps;
}

//==============================================================================
// --session-roundtrip <wav>: DAW project round trip without a DAW.
//   A  seeds a known session through the plugin's debug hook
//      (SAMPLAR_DEBUG_SESSION_SEED/ACK): loads the song, waits for the stems,
//      chops every stem into 2 MIDI-mapped segments, mutes stem 3, links stem 1,
//      sets BPM / grid offset / GATE / stem pitch / 2 sequencer lanes; renders
//      every note; saves the state; is unloaded.
//   B  (editor NEVER opened) setStateInformation; an immediate getState must
//      still hold every segment (A4); waits for the processor-side restore,
//      renders the same notes (same chops, notes, pitch, mute) and compares;
//      then opens the editor and checks what the page got (lanes, grid
//      division, model) through the plugin's editor ack.
//   C  the saved stem paths are gone -> the restore re-separates the song
//      (stem cache) and must end with the same session.
//   D  song and stems gone -> "missing audio": the session is kept and the
//      next save still contains every segment.
namespace rt {

// A hosted VST3's state is JUCE's "VST3PluginState" XML (binary) holding the
// plugin's IComponent stream in base64. Inside that stream: the SMAS AppState
// blob (magic, version, ValueTree), then the JUCE wrapper's own private data.
struct WrappedState
{
    std::unique_ptr<juce::XmlElement> xml;
    juce::MemoryBlock component;
    size_t treeStart = 0, treeEnd = 0;   // the ValueTree bytes inside `component`
    juce::ValueTree root;
};

WrappedState unwrap (const juce::MemoryBlock& mb)
{
    WrappedState w;
    w.xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (w.xml == nullptr) return w;
    auto* comp = w.xml->getChildByName ("IComponent");
    if (comp == nullptr) return w;
    w.component.fromBase64Encoding (comp->getAllSubText().trim());
    const auto* d = static_cast<const char*> (w.component.getData());
    for (size_t i = 0; i + 6 <= w.component.getSize(); ++i)
    {
        if (std::memcmp (d + i, "SMAS", 4) != 0) continue;
        w.treeStart = i + 6;
        juce::MemoryInputStream mis (d + w.treeStart, w.component.getSize() - w.treeStart, false);
        w.root = juce::ValueTree::readFromStream (mis);
        w.treeEnd = w.treeStart + (size_t) mis.getPosition();
        break;
    }
    return w;
}

juce::ValueTree parseState (const juce::MemoryBlock& mb) { return unwrap (mb).root; }

// Same wrapper, AppState tree replaced by `root` (private data kept).
juce::MemoryBlock rewrap (const juce::MemoryBlock& original, const juce::ValueTree& root)
{
    auto w = unwrap (original);
    juce::MemoryOutputStream tree;
    root.writeToStream (tree);
    juce::MemoryBlock comp;
    comp.append (w.component.getData(), w.treeStart);
    comp.append (tree.getData(), tree.getDataSize());
    comp.append (static_cast<const char*> (w.component.getData()) + w.treeEnd, w.component.getSize() - w.treeEnd);
    auto* c = w.xml->getChildByName ("IComponent");
    c->deleteAllTextElements();
    c->addTextElement (comp.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*w.xml, out);
    return out;
}

juce::ValueTree sessionOf (const juce::ValueTree& root)
{
    return root.getChildWithName ("smode").getChildWithName ("session");
}

int countSegs (const juce::ValueTree& session)
{
    int n = 0;
    for (int i = 0; i < session.getNumChildren(); ++i)
        if (session.getChild (i).hasType ("stem"))
            for (int j = 0; j < session.getChild (i).getNumChildren(); ++j)
                n += session.getChild (i).getChild (j).hasType ("seg") ? 1 : 0;
    return n;
}

struct NoteProbe { int note; int stem; bool muted; };

std::vector<NoteProbe> notesOf (const juce::ValueTree& session)
{
    std::vector<NoteProbe> v;
    for (int i = 0; i < session.getNumChildren(); ++i)
    {
        const auto st = session.getChild (i);
        if (! st.hasType ("stem")) continue;
        for (int j = 0; j < st.getNumChildren(); ++j)
        {
            const int note = (int) st.getChild (j).getProperty ("note", -1);
            if (note >= 0) v.push_back ({ note, (int) st.getProperty ("index"), (bool) st.getProperty ("mute") });
        }
    }
    v.push_back ({ 100, -1, false });   // unassigned note: must be silent
    return v;
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// Waits (pumping the message loop) for a file matching `pattern` in `dir`.
juce::File waitForAck (const juce::File& dir, const juce::String& pattern, int timeoutMs)
{
    const double t0 = nowMs();
    while (nowMs() - t0 < timeoutMs)
    {
        auto found = dir.findChildFiles (juce::File::findFiles, false, pattern);
        if (! found.isEmpty())
        {
            pump (50);   // let the writer finish
            std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getLastModificationTime() < b.getLastModificationTime(); });
            return found.getLast();
        }
        pump (50);
    }
    return {};
}

void clearAcks (const juce::File& dir, const juce::String& pattern)
{
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, pattern))
        f.deleteFile();
}

// Renders each note (on 0.25 s, off, 0.35 s tail) through processBlock on this
// thread and returns the RMS of each window. Messages are pumped between notes.
std::vector<double> renderNotes (juce::AudioPluginInstance& p, const std::vector<NoteProbe>& notes)
{
    constexpr int block = 512;
    juce::AudioBuffer<float> buf (juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels())), block);
    std::vector<double> out;
    for (const auto& n : notes)
    {
        double sum = 0.0; long count = 0;
        const int onBlocks = (int) (0.25 * 44100 / block), tailBlocks = (int) (0.35 * 44100 / block);
        for (int b = 0; b < onBlocks + tailBlocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)        midi.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) 110), 0);
            if (b == onBlocks) midi.addEvent (juce::MidiMessage::noteOff (1, n.note), 0);
            buf.clear();
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i) { const double s = buf.getSample (ch, i); sum += s * s; ++count; }
        }
        out.push_back (std::sqrt (sum / (double) juce::jmax (1L, count)));
        pump (30);
    }
    return out;
}

juce::String firstLineAfterHeader (const juce::String& ackText)
{
    // ready_N.txt: "restores=.. pendingExtra=..\n<model+play>", seeded.txt: "<model+play>"
    return ackText.contains ("\n") ? ackText.fromFirstOccurrenceOf ("\n", false, false).trim() : ackText.trim();
}

} // namespace rt

int runSessionRoundtrip (const Args& args)
{
    Result res;
    Watchdog dog;
    const auto wav = args.roundtripWav;
    log ("session round trip: song " + wav.getFullPathName() + "  plugin " + args.plugin.getFullPathName());
    if (! wav.existsAsFile() || ! args.plugin.exists())
    {
        log ("FAIL: song or plugin missing");
        return 2;
    }

    const auto ack = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplar_session_rt");
    ack.deleteRecursively();
    ack.createDirectory();
    setEnv ("SAMPLAR_DEBUG_SESSION_ACK", ack.getFullPathName());
    setEnv ("SAMPLAR_DEBUG_SESSION_SEED", wav.getFullPathName());

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> types;
    formats.getFormat (0)->findAllTypesForFile (types, args.plugin.getFullPathName());
    if (types.isEmpty()) { log ("FAIL: no plugin type"); return 2; }

    auto make = [&] (const char* tag) -> std::unique_ptr<juce::AudioPluginInstance>
    {
        juce::String error;
        auto p = formats.createPluginInstance (*types[0], 44100.0, 512, error);
        res.step (juce::String (tag) + " instantiate", 0.0, p != nullptr, error);
        if (p != nullptr)
        {
            p->enableAllBuses();
            p->setRateAndBufferSizeDetails (44100.0, 512);
            p->prepareToPlay (44100.0, 512);
        }
        return p;
    };

    // ---------------- A: seed, render, save ----------------
    dog.arm ("A seed", 300000);
    double t0 = nowMs();
    auto a = make ("A");
    if (a == nullptr) return 3;
    const auto seededFile = rt::waitForAck (ack, "seeded.txt", 280000);
    res.step ("A load song + seed session", nowMs() - t0, seededFile.existsAsFile());
    if (! seededFile.existsAsFile()) return 3;
    const auto seeded = seededFile.loadFileAsString().trim();
    // Unlicensed machine: processBlock output is muted by the licence gate
    // (review A5), so audio can only be compared on a licensed machine. Then the
    // check becomes "every note of every instance is exactly silent".
    const bool audioAllowed = ack.getChildFile ("licence.txt").loadFileAsString().contains ("audioAllowed=1");
    log (juce::String ("  licence gate: ") + (audioAllowed ? "open (audio compared)" : "CLOSED - unlicensed: renders must be silent"));
    log ("  seeded: " + seeded.substring (0, 300) + (seeded.length() > 300 ? " ..." : ""));
    rt::pump (600);   // engine snapshot rebuild (30 Hz timer)

    juce::MemoryBlock stateA;
    a->getStateInformation (stateA);
    const auto rootA = rt::parseState (stateA);
    const auto sessA = rt::sessionOf (rootA);
    const int segsA = rt::countSegs (sessA);
    res.step ("A getState (session + segments)", 0.0, sessA.isValid() && segsA >= 6,
              juce::String ((int) stateA.getSize()) + " bytes, segments=" + juce::String (segsA)
              + " schema=" + rootA.getChildWithName ("smode").getProperty ("schema").toString());
    const auto notes = rt::notesOf (sessA);
    dog.arm ("A render", 120000);
    const auto rmsA = rt::renderNotes (*a, notes);
    a->releaseResources();
    a.reset();
    rt::pump (300);
    setEnv ("SAMPLAR_DEBUG_SESSION_SEED", "");

    auto compareNotes = [&] (const juce::String& tag, const std::vector<double>& rms)
    {
        bool ok = rms.size() == rmsA.size();
        juce::String detail;
        int audible = 0, silent = 0;
        for (size_t i = 0; ok && i < rms.size(); ++i)
        {
            const auto& n = notes[i];
            const bool expectSilent = n.stem < 0 || n.muted || ! audioAllowed;
            bool pass;
            if (expectSilent)
                pass = rms[i] < 1e-9 && rmsA[i] < 1e-9;
            else
                pass = rms[i] > 1e-7 && rmsA[i] > 1e-7
                       && std::abs (20.0 * std::log10 (rms[i] / rmsA[i])) < 1.0;
            (expectSilent ? silent : audible) += pass ? 1 : 0;
            if (! pass || detail.length() < 400)
                detail << " #" << n.note << (expectSilent ? "(silent)" : "") << "=" << juce::String (rms[i], 7)
                       << "/A" << juce::String (rmsA[i], 7) << (pass ? "" : " FAIL");
            ok = ok && pass;
        }
        res.step (tag + (audioAllowed ? juce::String (" MIDI notes == A (") : juce::String (" licence gate mutes every note ("))
                      + juce::String (audible) + " audible, " + juce::String (silent) + " silent)",
                  0.0, ok, detail);
    };

    // ---------------- B: restore with the editor closed ----------------
    dog.arm ("B restore", 120000);
    rt::clearAcks (ack, "ready_*.txt");
    rt::clearAcks (ack, "editor_*.txt");
    auto b = make ("B");
    if (b == nullptr) return 4;
    t0 = nowMs();
    b->setStateInformation (stateA.getData(), (int) stateA.getSize());
    juce::MemoryBlock early;
    b->getStateInformation (early);   // before the stems are decoded
    const int segsEarly = rt::countSegs (rt::sessionOf (rt::parseState (early)));
    res.step ("B save BEFORE restore keeps session", 0.0, segsEarly == segsA,
              "segments " + juce::String (segsEarly) + " (A " + juce::String (segsA) + ")");
    const auto readyB = rt::waitForAck (ack, "ready_*.txt", 110000);
    res.step ("B restore (editor closed)", nowMs() - t0, readyB.existsAsFile());
    if (! readyB.existsAsFile()) return 4;
    const auto descB = rt::firstLineAfterHeader (readyB.loadFileAsString());
    res.step ("B model + play settings == A", 0.0, descB == seeded,
              descB == seeded ? juce::String ("identical") : "B: " + descB.substring (0, 400));
    rt::pump (600);
    dog.arm ("B render", 120000);
    compareNotes ("B", rt::renderNotes (*b, notes));

    // editor opened AFTER the restore: the page must get lanes / grid / model
    dog.arm ("B editor", 60000);
    {
        HostWindow window;
        double ms = 0.0;
        auto* ed = openEditor (*b, window, ms);
        res.step ("B editor open", ms, ed != nullptr);
        rt::pump (1500);
        const auto edAck = rt::waitForAck (ack, "editor_*.txt", 5000);
        const auto txt = edAck.loadFileAsString();
        const auto head = txt.upToFirstOccurrenceOf ("\n", false, false);
        const auto model = txt.fromFirstOccurrenceOf ("\n", false, false).trim();
        const bool lanesOk = head.contains ("pendingExtra=0") && head.contains ("lanes=2") && head.contains ("slots=3")
                          && head.contains ("division=8");
        res.step ("B editor: lanes + grid division restored", 0.0, edAck.existsAsFile() && lanesOk, head);
        res.step ("B editor: model unchanged by the page", 0.0, seeded.startsWith (model) && model.isNotEmpty(),
                  model == seeded.substring (0, model.length()) ? juce::String ("identical") : model.substring (0, 300));
        if (ed != nullptr) closeEditor (*b, window, ed);
        rt::pump (300);
    }
    juce::MemoryBlock stateB;
    b->getStateInformation (stateB);
    const auto sessB = rt::sessionOf (rt::parseState (stateB));
    res.step ("B save after editor close", 0.0, rt::countSegs (sessB) == segsA
                                                && sessB.getProperty ("uiExtra").toString().contains ("\"lanes\""),
              "segments " + juce::String (rt::countSegs (sessB)) + ", uiExtra " + juce::String (sessB.getProperty ("uiExtra").toString().length()) + " chars");
    b->releaseResources();
    b.reset();
    rt::pump (300);

    // ---------------- C: stem files gone -> re-separation (stem cache) ----------------
    dog.arm ("C reseparate", 300000);
    {
        auto root = rt::parseState (stateA).createCopy();
        auto sess = rt::sessionOf (root);
        for (int i = 0; i < sess.getNumChildren(); ++i)
            if (sess.getChild (i).hasType ("stem"))
                sess.getChild (i).setProperty ("file", ack.getChildFile ("gone_" + juce::String (i) + ".wav").getFullPathName(), nullptr);
        const auto stateC = rt::rewrap (stateA, root);
        rt::clearAcks (ack, "ready_*.txt");
        auto c = make ("C");
        if (c == nullptr) return 5;
        t0 = nowMs();
        c->setStateInformation (stateC.getData(), (int) stateC.getSize());
        const auto readyC = rt::waitForAck (ack, "ready_*.txt", 280000);
        res.step ("C restore with stems missing (re-separate)", nowMs() - t0, readyC.existsAsFile());
        if (readyC.existsAsFile())
        {
            const auto descC = rt::firstLineAfterHeader (readyC.loadFileAsString());
            res.step ("C model + play settings == A", 0.0, descC == seeded,
                      descC == seeded ? juce::String ("identical") : "C: " + descC.substring (0, 400));
            rt::pump (600);
            compareNotes ("C", rt::renderNotes (*c, notes));
        }
        c->releaseResources();
        c.reset();
        rt::pump (300);
    }

    // ---------------- D: song + stems gone -> missing audio, session kept ----------------
    dog.arm ("D missing", 60000);
    {
        auto root = rt::parseState (stateA).createCopy();
        auto sess = rt::sessionOf (root);
        sess.setProperty ("sourceFile", ack.getChildFile ("gone_song.wav").getFullPathName(), nullptr);
        for (int i = 0; i < sess.getNumChildren(); ++i)
            if (sess.getChild (i).hasType ("stem"))
                sess.getChild (i).setProperty ("file", ack.getChildFile ("gone_" + juce::String (i) + ".wav").getFullPathName(), nullptr);
        const auto stateD = rt::rewrap (stateA, root);
        rt::clearAcks (ack, "missing_*.txt");
        auto d = make ("D");
        if (d == nullptr) return 6;
        d->setStateInformation (stateD.getData(), (int) stateD.getSize());
        const auto missing = rt::waitForAck (ack, "missing_*.txt", 20000);
        res.step ("D missing audio reported", 0.0, missing.existsAsFile(), missing.loadFileAsString().substring (0, 160));
        juce::MemoryBlock stateD2;
        d->getStateInformation (stateD2);
        const auto sessD = rt::sessionOf (rt::parseState (stateD2));
        res.step ("D save keeps the session (never wiped)", 0.0, rt::countSegs (sessD) == segsA
                                                               && sessD.getProperty ("uiExtra").toString() == sessA.getProperty ("uiExtra").toString(),
                  "segments " + juce::String (rt::countSegs (sessD)));
        d->releaseResources();
        d.reset();
        rt::pump (300);
    }

    setEnv ("SAMPLAR_DEBUG_SESSION_ACK", "");
    dog.arm ("exit", 30000);
    log ("==== SUMMARY (session round trip) ====");
    for (auto& l : res.lines) log (l);
    log (res.ok ? "RESULT: ALL PASS (exit 0)" : "RESULT: FAILURES (exit 1)");
    return res.ok ? 0 : 1;
}

//==============================================================================
// --bpm-unload <dir>: proves unloading the plugin while the page's BPM analysis
// (BeatThis inference) runs on a long song is fast (review A4 / U-H1). The
// plugin's UI self-test builds synthetic stems with a <bpm-song-sec> drum stem;
// the BPM job writes bpm_started / bpm_cancelled / bpm_done files into <dir>.
int runBpmUnload (const Args& args)
{
    Result res;
    Watchdog dog;
    const auto dir = args.bpmUnloadDir;
    dir.deleteRecursively();
    dir.createDirectory();
    setEnv ("SAMPLAR_UI_SWEEP_DIR", dir.getFullPathName());
    setEnv ("SAMPLAR_UI_CMD", "");
    log ("bpm-unload: plugin " + args.plugin.getFullPathName() + ", drum stem " + juce::String (args.bpmUnloadSongSec, 0) + " s");

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    dog.arm ("load", 60000);
    juce::OwnedArray<juce::PluginDescription> types;
    formats.getFormat (0)->findAllTypesForFile (types, args.plugin.getFullPathName());
    if (types.isEmpty()) { log ("FAIL: no plugin type"); return 2; }
    juce::String error;
    auto plugin = formats.createPluginInstance (*types[0], 44100.0, 512, error);
    res.step ("instantiate", 0.0, plugin != nullptr, plugin ? juce::String() : error);
    if (plugin == nullptr) return 2;
    plugin->enableAllBuses();
    plugin->setRateAndBufferSizeDetails (44100.0, 512);
    plugin->prepareToPlay (44100.0, 512);

    HostWindow window;
    double ms = 0.0;
    auto* ed = openEditor (*plugin, window, ms);
    res.step ("editor open", ms, ed != nullptr);
    if (ed == nullptr) return 4;

    int seq = 0;
    auto cmd = [&] (const juce::String& verb, const juce::String& arg) -> juce::String
    {
        ++seq;
        auto ack = dir.getChildFile ("ack_" + juce::String (seq) + ".txt");
        setEnv ("SAMPLAR_UI_CMD", juce::String (seq) + "|" + verb + "|" + arg);
        const double t0 = nowMs();
        while (! ack.existsAsFile() && nowMs() - t0 < 30000.0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        return ack.existsAsFile() ? ack.loadFileAsString().trim() : juce::String ("TIMEOUT");
    };

    dog.arm ("self-test handshake", 60000);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
    const bool shaken = cmd ("sweep", "probe") != "TIMEOUT";
    res.step ("self-test handshake", 0.0, shaken);
    if (! shaken) return 3;

    dog.arm ("loadlong", 120000);
    double t0 = nowMs();
    const auto ackLoad = cmd ("loadlong", juce::String (args.bpmUnloadSongSec, 0));
    res.step ("load synthetic stems (drums " + juce::String (args.bpmUnloadSongSec, 0) + " s)", nowMs() - t0, ackLoad != "TIMEOUT");

    // Wait until the BPM job is running, then give BeatThis time to be deep in inference.
    t0 = nowMs();
    const auto started = dir.getChildFile ("bpm_started.txt");
    while (! started.existsAsFile() && nowMs() - t0 < 60000.0)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    res.step ("BPM job started", nowMs() - t0, started.existsAsFile());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    const bool stillRunning = ! dir.getChildFile ("bpm_done.txt").existsAsFile()
                           && ! dir.getChildFile ("bpm_cancelled.txt").existsAsFile();
    res.step ("BPM job still running 2.5 s later", 0.0, stillRunning, "a finished job would not test cancellation");

    dog.arm ("close+unload", 60000);
    t0 = nowMs();
    closeEditor (*plugin, window, ed);
    plugin->releaseResources();
    { SampledStep ss ("delete instance"); plugin.reset(); }
    const double unloadMs = nowMs() - t0;
    if (gSampler) gSampler->report();
    res.step ("editor close + delete instance during BPM analysis", unloadMs, unloadMs < args.unloadBudgetMs,
              "budget " + juce::String (args.unloadBudgetMs, 0) + " ms");

    const auto cancelledFile = dir.getChildFile ("bpm_cancelled.txt");
    res.step ("BPM job reported cancelled", 0.0, cancelledFile.existsAsFile(),
              cancelledFile.existsAsFile() ? "job ran " + cancelledFile.loadFileAsString().trim() + " before returning"
                                           : juce::String ("no bpm_cancelled.txt"));

    setEnv ("SAMPLAR_UI_SWEEP_DIR", "");
    setEnv ("SAMPLAR_UI_CMD", "");
    dog.arm ("final pump", 30000);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    dog.arm ("exit", 30000);
    log ("==== SUMMARY ====");
    for (auto& l : res.lines) log (l);
    log (res.ok ? "RESULT: ALL PASS (exit 0)" : "RESULT: FAILURES (exit 1)");
    return res.ok ? 0 : 1;
}

int runSmoke (const Args& args)
{
    Result res;
    Watchdog dog;
    log ("plugin: " + args.plugin.getFullPathName());
    log ("state : " + (args.stateFile == juce::File() ? juce::String ("(none)") : args.stateFile.getFullPathName()
                         + (args.stateFile.existsAsFile() ? " (exists -> restore)" : " (missing -> fresh)")));
    if (! args.plugin.exists())
    {
        log ("FAIL: plugin bundle not found");
        return 2;
    }

    if (args.uiSweepDir != juce::File())
    {
        args.uiSweepDir.createDirectory();
        setEnv ("SAMPLAR_UI_SWEEP_DIR", args.uiSweepDir.getFullPathName());   // read by the plugin's SModePage ctor
        setEnv ("SAMPLAR_UI_CMD", "");
    }

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());

    // 1. scan + instantiate
    dog.arm ("load", 60000);
    double t0 = nowMs();
    juce::OwnedArray<juce::PluginDescription> types;
    formats.getFormat (0)->findAllTypesForFile (types, args.plugin.getFullPathName());
    res.step ("scan (findAllTypesForFile)", nowMs() - t0, ! types.isEmpty(), juce::String (types.size()) + " type(s)");
    if (types.isEmpty()) return 2;

    t0 = nowMs();
    juce::String error;
    auto plugin = formats.createPluginInstance (*types[0], 44100.0, 512, error);
    res.step ("instantiate", nowMs() - t0, plugin != nullptr, plugin ? plugin->getName() : "error: " + error);
    if (plugin == nullptr) return 2;
    log ("  ins=" + juce::String (plugin->getTotalNumInputChannels()) + " outs=" + juce::String (plugin->getTotalNumOutputChannels())
         + " acceptsMidi=" + juce::String (plugin->acceptsMidi() ? 1 : 0));

    // 2. restore state from a previous run (hosts do this before prepare)
    dog.arm ("setState", 30000);
    if (args.stateFile.existsAsFile())
    {
        juce::MemoryBlock mb;
        args.stateFile.loadFileAsData (mb);
        t0 = nowMs();
        plugin->setStateInformation (mb.getData(), (int) mb.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        res.step ("setStateInformation", nowMs() - t0, true, juce::String ((int) mb.getSize()) + " bytes");
    }

    // 3. prepare + audio burst
    dog.arm ("prepare+burst", 60000);
    t0 = nowMs();
    plugin->enableAllBuses();
    plugin->setRateAndBufferSizeDetails (44100.0, 512);
    plugin->prepareToPlay (44100.0, 512);
    res.step ("prepareToPlay(44100,512)", nowMs() - t0, true);

    AudioDriver audio (*plugin, 44100.0, 512);
    t0 = nowMs();
    const bool burstOk = audio.burst (5.0, 5.0);
    res.step ("processBlock 5s silence+5s tone", nowMs() - t0, burstOk,
              "max block " + juce::String (audio.maxBlockMs(), 2) + " ms");

    // 4. editor open -> pump -> close, with the host's audio thread running
    HostWindow window;
    window.setResizable (true, false);
    audio.startPaced();

    dog.arm ("editor open #1", 60000);
    double ms = 0.0;
    auto* ed = openEditor (*plugin, window, ms);
    res.step ("editor open #1 (first)", ms, ed != nullptr,
              ed ? juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) : juce::String ("no editor"));
    if (ed == nullptr) return 4;

    dog.arm ("pump #1", args.showMs + 30000);
    t0 = nowMs();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (args.showMs);
    const double pumped = nowMs() - t0;
    // A message loop that returns much later than asked = the msg thread was blocked.
    res.step ("message loop pump #1", pumped, pumped < args.showMs + 2000.0,
              "asked " + juce::String (args.showMs) + " ms");

    if (args.uiSweepDir != juce::File())
    {
        dog.arm ("ui-sweep", 600000);
        const int failed = runUiSweep (*ed, args.uiSweepDir, res);
        log ("ui-sweep: failed sweeps = " + juce::String (failed));
    }

    dog.arm ("editor close #1", 30000);
    res.step ("editor close #1", closeEditor (*plugin, window, ed), true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);

    dog.arm ("editor reopen", 30000);
    { SampledStep ss ("editor reopen #2"); ed = openEditor (*plugin, window, ms); }
    res.step ("editor reopen #2 (persistent)", ms, ed != nullptr && ms < args.reopenBudgetMs,
              "budget " + juce::String (args.reopenBudgetMs, 0) + " ms");
    if (gSampler) gSampler->report();
    if (ed == nullptr) return 4;

    dog.arm ("pump #2", 30000);
    t0 = nowMs();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
    res.step ("message loop pump #2", nowMs() - t0, nowMs() - t0 < 5000.0, "asked 3000 ms");

    dog.arm ("editor close #2", 30000);
    res.step ("editor close #2", closeEditor (*plugin, window, ed), true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    audio.stopPaced();
    res.step ("paced audio during editor", 0.0, ! audio.failed(),
              juce::String (audio.pacedBlocks()) + " blocks, nonFinite=" + juce::String (audio.nonFinite())
              + ", max block " + juce::String (audio.maxBlockMs(), 2) + " ms");

    // 5. state save for the next run
    dog.arm ("getState", 30000);
    juce::MemoryBlock state;
    t0 = nowMs();
    plugin->getStateInformation (state);
    res.step ("getStateInformation", nowMs() - t0, state.getSize() > 0, juce::String ((int) state.getSize()) + " bytes");
    if (args.stateFile != juce::File() && state.getSize() > 0)
        args.stateFile.replaceWithData (state.getData(), state.getSize());

    // (Off-thread get/setState cannot be exercised here: JUCE's VST3 host takes
    //  MessageManagerLock inside get/setStateInformation, as VST3 requires the UI
    //  thread. The plugin side is covered by tests/unit/AppStateThreadTests.cpp.)

    // 5b. optional second instance: B's editor stays open while A unloads
    std::unique_ptr<juce::AudioPluginInstance> pluginB;
    std::unique_ptr<HostWindow> windowB;
    juce::AudioProcessorEditor* edB = nullptr;
    if (args.twoInstances)
    {
        dog.arm ("instance B", 90000);
        t0 = nowMs();
        pluginB = formats.createPluginInstance (*types[0], 44100.0, 512, error);
        res.step ("B instantiate", nowMs() - t0, pluginB != nullptr, pluginB ? juce::String() : "error: " + error);
        if (pluginB == nullptr) return 5;
        pluginB->enableAllBuses();
        pluginB->setRateAndBufferSizeDetails (44100.0, 512);
        pluginB->prepareToPlay (44100.0, 512);
        windowB = std::make_unique<HostWindow>();
        edB = openEditor (*pluginB, *windowB, ms);
        res.step ("B editor open", ms, edB != nullptr);
        if (edB == nullptr) return 5;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);

        // A's editor again (A claims the globals), closed while B's stays open
        ed = openEditor (*plugin, window, ms);
        res.step ("A editor open (B open)", ms, ed != nullptr);
        if (ed == nullptr) return 5;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        res.step ("A editor close (B open)", closeEditor (*plugin, window, ed), true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    }

    // 6. release + unload
    dog.arm ("release", 30000);
    t0 = nowMs();
    plugin->releaseResources();
    res.step ("releaseResources", nowMs() - t0, true);

    dog.arm ("delete instance", 60000);
    t0 = nowMs();
    { SampledStep ss ("delete instance"); plugin.reset(); }
    const double unloadMs = nowMs() - t0;
    if (gSampler) gSampler->report();
    res.step ("delete instance (unload)", unloadMs, unloadMs < args.unloadBudgetMs,
              "budget " + juce::String (args.unloadBudgetMs, 0) + " ms");

    if (pluginB != nullptr)
    {
        dog.arm ("B after A unload", 60000);
        t0 = nowMs();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
        res.step ("B pump after A unload", nowMs() - t0, nowMs() - t0 < 5000.0, "asked 3000 ms");
        res.step ("B editor close", closeEditor (*pluginB, *windowB, edB), true);
        pluginB->releaseResources();
        t0 = nowMs();
        { SampledStep ss ("B delete instance"); pluginB.reset(); }
        const double unloadB = nowMs() - t0;
        if (gSampler) gSampler->report();
        res.step ("B delete instance", unloadB, unloadB < args.unloadBudgetMs,
                  "budget " + juce::String (args.unloadBudgetMs, 0) + " ms");
        windowB.reset();
    }

    dog.arm ("final pump", 30000);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // late hops must not crash
    dog.arm ("", 0);
    dog.arm ("exit", 30000);

    log ("==== SUMMARY ====");
    for (auto& l : res.lines) log (l);
    log (res.ok ? "RESULT: ALL PASS (exit 0)" : "RESULT: FAILURES (exit 1)");
    return res.ok ? 0 : 1;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray a;
    for (int i = 1; i < argc; ++i) a.add (juce::String::fromUTF8 (argv[i]));
    const auto args = parseArgs (a);
    StackSampler sampler (args.stacks);
    if (args.stacks) gSampler = &sampler;

    const double t0 = nowMs();
    int rc = 0;
    {
        juce::ScopedJuceInitialiser_GUI gui;
        rc = args.roundtripWav != juce::File() ? runSessionRoundtrip (args)
           : args.bpmUnloadDir != juce::File() ? runBpmUnload (args)
                                                : runSmoke (args);
    }
    log ("process teardown done at +" + juce::String (nowMs() - t0, 0) + " ms, exit " + juce::String (rc));
    return rc;
}
