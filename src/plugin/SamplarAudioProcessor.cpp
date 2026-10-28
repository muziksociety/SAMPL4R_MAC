#include "SamplarAudioProcessor.h"
#include "../app/ServiceLocator.h"
#include "../app/AudioDeviceService.h"
#include "../utils/Tracing.h"
#include "../utils/FontWarmer.h"
#include "../services/LicenseSystemService.h"
#include "../services/UpdateCheckService.h"
#include "../services/MidiMappingService.h"
#include "../services/smode/SModeService.h"
#include "../services/smode/SModeSession.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "../utils/LogPaths.h"
#include "../utils/DataPaths.h"
#include "SamplarAudioProcessorEditor.h"
#include "GpuTeardownOffload.h"
#include "AsyncFileLogger.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace samplar {

// ----------------------------------------------------------
// Live instances (A6)
// ----------------------------------------------------------
// A host can load several SAMPLAR instances; they share the ServiceLocator
// globals. When the instance that owns them unloads, the most recently
// created survivor claims them (SM5 3dcdc3ce) so a still-open editor keeps a
// valid registry / bus / state instead of null or freed pointers.
namespace
{
    std::mutex& liveInstancesMutex()
    {
        static std::mutex m;
        return m;
    }

    std::vector<SamplarAudioProcessor*>& liveInstances()
    {
        static std::vector<SamplarAudioProcessor*> v;
        return v;
    }
}


// ----------------------------------------------------------
// Construction / Destruction
// ----------------------------------------------------------

SamplarAudioProcessor::SamplarAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
          .withInput("Input",   juce::AudioChannelSet::stereo(), true))
{
    SAMPLAR_TRACE_SESSION_START();
    SAMPLAR_TRACE_SCOPE("lifecycle", "SamplarAudioProcessor::ctor");
    // Before anything can create an editor window / JUCE VBlankDispatcher (impl_unload.md).
    samplar::GpuTeardownOffload::registerHolder();
    samplar::FontWarmer::warmInBackground();
    juce::Logger::writeToLog("[SamplarAudioProcessor] === CONSTRUCTOR START ===");
    appState_ = std::make_unique<AppState>(*this);
    initServices();
    registerDefaultBindings();
    startDebugSessionHookIfRequested();
    // A16 / A18: watches the S-Mode session (2 Hz) -> cached tree + dirty flag.
    stateWatcher_.startTimer(500);
    {
        std::scoped_lock lock(liveInstancesMutex());
        liveInstances().push_back(this);
        juce::Logger::writeToLog("[SamplarAudioProcessor]   live instances in this process: "
                                 + juce::String((int) liveInstances().size()));
    }
    juce::Logger::writeToLog("[SamplarAudioProcessor] === CONSTRUCTOR END ===");
}

SamplarAudioProcessor::~SamplarAudioProcessor()
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] === DESTRUCTOR START ===");
    aliveToken_->store(false);
    stateWatcher_.stopTimer();
    {
        std::scoped_lock lock(liveInstancesMutex());
        auto& v = liveInstances();
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
    }
    {
        SAMPLAR_TRACE_SCOPE("lifecycle", "SamplarAudioProcessor::dtor");
        {
            SAMPLAR_TRACE_SCOPE("lifecycle", "shutdownServices");
            shutdownServices();
        }
    }
    {
        // If this instance owned the ServiceLocator, its globals are empty now:
        // let the most recently created surviving instance claim them.
        std::scoped_lock lock(liveInstancesMutex());
        if (ServiceLocator::getRegistry() == nullptr && !liveInstances().empty())
        {
            juce::Logger::writeToLog("[SamplarAudioProcessor] claimServiceLocator by survivor (previous owner unloaded)");
            liveInstances().back()->claimServiceLocator();
        }
        else if (liveInstances().empty())
        {
            juce::Logger::writeToLog("[SamplarAudioProcessor] last instance - ServiceLocator stays empty");
        }
    }
    SAMPLAR_TRACE_SESSION_STOP();
    juce::Logger::writeToLog("[SamplarAudioProcessor] === DESTRUCTOR END ===");
}

// ----------------------------------------------------------
// Shared log file (A21)
// ----------------------------------------------------------
// juce::Logger is process-global. Each instance used to create its own
// FileLogger on the same file (deleting the previous instance's log) and set
// the global logger to nullptr on destruction, so the second instance's
// teardown was never logged. Instead all instances share one logger; the
// first creates it, the last one releases it. It is an AsyncFileLogger (one
// open stream + background writer): juce::FileLogger reopened/closed the file
// per line, which cost 10-200 ms per CloseHandle under CPU load (unload).
namespace
{
    std::mutex& sharedLoggerMutex()
    {
        static std::mutex m;
        return m;
    }

    std::weak_ptr<AsyncFileLogger>& sharedLoggerSlot()
    {
        static std::weak_ptr<AsyncFileLogger> slot;
        return slot;
    }

    std::shared_ptr<AsyncFileLogger> acquireSharedLogger(const juce::File& logFile)
    {
        std::scoped_lock lock(sharedLoggerMutex());
        if (auto existing = sharedLoggerSlot().lock())
            return existing;

        if (logFile.existsAsFile())
            (void) logFile.deleteFile();   // fresh file per host session
        auto logger = std::make_shared<AsyncFileLogger>(logFile, "[SAMPLAR] Startup");
        sharedLoggerSlot() = logger;
        juce::Logger::setCurrentLogger(logger.get());
        return logger;
    }

    // Stopped loggers stay allocated until module unload: a thread that read the
    // global logger pointer just before it was cleared (e.g. a DeferredRelease
    // reaper inside JUCE's ThreadPool teardown) then hits a stopped logger that
    // ignores the line, never freed memory. Module unload waits for every
    // module-pinned thread, so the graveyard is destroyed after them.
    std::vector<std::shared_ptr<AsyncFileLogger>>& stoppedLoggers()
    {
        static std::vector<std::shared_ptr<AsyncFileLogger>> v;
        return v;
    }

    void releaseSharedLogger(std::shared_ptr<AsyncFileLogger>& mine)
    {
        std::scoped_lock lock(sharedLoggerMutex());
        if (mine == nullptr)
            return;
        if (mine.use_count() == 1)   // last instance: nothing else may log into it
        {
            juce::Logger::setCurrentLogger(nullptr);
            mine->shutdown();   // flushes the queue, joins the writer, closes the file
            stoppedLoggers().push_back(std::move(mine));
        }
        mine.reset();
    }
}

// ----------------------------------------------------------
// Service lifecycle (mirrors App::initialise / App::shutdown)
// ----------------------------------------------------------

void SamplarAudioProcessor::initServices()
{
    // -- VST3/Plugin resource path resolution ----------------------
    // When running as VST3, the exe is the DAW (e.g. FL Studio), so
    // DataPaths::getAppDirectory() would resolve to the DAW's folder.
    // We must override it to point at the SAMPLAR install location.
    if (!isRunningStandalone())
    {
        juce::File resolvedDir;

        // Strategy: check multiple locations for SAMPLAR resources
        // Require data/ directory specifically - it is the install marker
        // (staged with data/samplar_bundle.marker by the CMake POST_BUILD step)
        auto hasResources = [](const juce::File& dir) {
            return dir.isDirectory()
                && dir.getChildFile("data").isDirectory();
        };

        // 0. Inside the VST3 bundle (Inno installer layout, default 2026-05-18+)
        //    The installer drops models/, data/, BACKGROUNDS/, Samples/
        //    INTO .vst3/Contents/x86_64-win/ so the bundle is self-contained.
        //    That dir IS the parent of the .vst3 binary, so just check it.
        //    Checked FIRST: the binary's own resources always match the binary,
        //    while a pointer file can outlive an uninstall or point at a stale
        //    portable/dev folder.
        {
            auto vst3Dll = juce::File::getSpecialLocation(
                               juce::File::currentApplicationFile);
            auto binaryDir = vst3Dll.getParentDirectory();  // .../Contents/x86_64-win/
            if (hasResources(binaryDir))
            {
                resolvedDir = binaryDir;
                juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from bundle Contents/x86_64-win: "
                                         + binaryDir.getFullPathName());
            }
        }

        // 0.5. Portable pointer file (only when the bundle itself has no resources)
        //    Written by "Setup VST3.cmd" inside the portable .zip distribution.
        //    Path lives at %APPDATA%\Samplar\portable_root.txt and contains
        //    the absolute path of the user's extracted portable folder. If the
        //    pointer is stale (folder moved/deleted), hasResources() rejects it
        //    and we fall through to the standard installer strategies below.
        if (!resolvedDir.isDirectory())
        {
            auto pointerFile = juce::File::getSpecialLocation(
                                   juce::File::userApplicationDataDirectory)
                                   .getChildFile("Samplar")
                                   .getChildFile("portable_root.txt");
            if (pointerFile.existsAsFile())
            {
                auto pointed = pointerFile.loadFileAsString().trim();
                if (pointed.isNotEmpty())
                {
                    juce::File portableDir(pointed);
                    if (hasResources(portableDir))
                    {
                        resolvedDir = portableDir;
                        juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from portable pointer: "
                                                 + portableDir.getFullPathName());
                    }
                    else
                    {
                        juce::Logger::writeToLog("[SamplarAudioProcessor] Portable pointer exists but path invalid (stale?): "
                                                 + pointed);
                    }
                }
            }
        }

        // 1. Registry install path (handles custom install directories).
        // Windows only: macOS has no registry; resources live in the bundle.
        juce::String regPath;
#if JUCE_WINDOWS
        if (juce::WindowsRegistry::valueExists(
                "HKEY_LOCAL_MACHINE\\Software\\SAMPLAR\\SAMPLAR\\InstallPath"))
        {
            regPath = juce::WindowsRegistry::getValue(
                "HKEY_LOCAL_MACHINE\\Software\\SAMPLAR\\SAMPLAR\\InstallPath");
        }
        if (regPath.isEmpty()
            && juce::WindowsRegistry::valueExists(
                   "HKEY_CURRENT_USER\\Software\\SAMPLAR\\SAMPLAR\\InstallPath"))
        {
            regPath = juce::WindowsRegistry::getValue(
                "HKEY_CURRENT_USER\\Software\\SAMPLAR\\SAMPLAR\\InstallPath");
        }
#endif

        if (!resolvedDir.isDirectory() && regPath.isNotEmpty())
        {
            juce::File regDir(regPath);
            if (hasResources(regDir))
            {
                resolvedDir = regDir;
                juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from registry: "
                                         + regDir.getFullPathName());
            }
        }

        // 2. Program Files default (most common install)
        if (!resolvedDir.isDirectory())
        {
            auto progFiles = juce::File::getSpecialLocation(
                                 juce::File::globalApplicationsDirectory)
                                 .getChildFile("SAMPLAR");
            if (hasResources(progFiles))
            {
                resolvedDir = progFiles;
                juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from Program Files: "
                                         + progFiles.getFullPathName());
            }
        }

        // 3. Next to the VST3 DLL itself (dev builds / portable)
        if (!resolvedDir.isDirectory())
        {
            auto vst3Dll = juce::File::getSpecialLocation(
                               juce::File::currentApplicationFile);
            // VST3 bundle: .vst3/Contents/x86_64-win/plugin.vst3 ? walk up to .vst3 parent
            auto vst3Parent = vst3Dll.getParentDirectory()   // x86_64-win/
                                     .getParentDirectory()   // Contents/
                                     .getParentDirectory()   // .vst3 bundle/
                                     .getParentDirectory();  // folder containing the bundle
            if (hasResources(vst3Parent))
            {
                resolvedDir = vst3Parent;
                juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory next to VST3 bundle: "
                                         + vst3Parent.getFullPathName());
            }
        }

        // 4. Walk up from VST3 DLL looking for SAMPLAR_Portable or project root (dev builds)
        if (!resolvedDir.isDirectory())
        {
            auto vst3Dll = juce::File::getSpecialLocation(
                               juce::File::currentApplicationFile);
            auto dir = vst3Dll.getParentDirectory();
            for (int i = 0; i < 10 && dir.getParentDirectory() != dir; ++i)
            {
                // Check for SAMPLAR_Portable subfolder (portable/dev builds)
                auto portable = dir.getChildFile("SAMPLAR_Portable");
                if (hasResources(portable))
                {
                    resolvedDir = portable;
                    juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from SAMPLAR_Portable: "
                                             + portable.getFullPathName());
                    break;
                }
                // Also check dir itself (in case resources are at project root)
                if (hasResources(dir))
                {
                    resolvedDir = dir;
                    juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory walking up from VST3: "
                                             + dir.getFullPathName());
                    break;
                }
                dir = dir.getParentDirectory();
            }
        }

        // 5. Breadcrumb file next to VST3 DLL (installer writes SAMPLAR_install_path.txt)
        if (!resolvedDir.isDirectory())
        {
            auto vst3Dll = juce::File::getSpecialLocation(
                               juce::File::currentApplicationFile);
            auto breadcrumb = vst3Dll.getParentDirectory()
                                     .getParentDirectory()
                                     .getParentDirectory()
                                     .getParentDirectory()
                                     .getChildFile("SAMPLAR_install_path.txt");
            if (!breadcrumb.existsAsFile())
                breadcrumb = vst3Dll.getSiblingFile("SAMPLAR_install_path.txt");
            if (breadcrumb.existsAsFile())
            {
                auto path = breadcrumb.loadFileAsString().trim();
                juce::File candidate(path);
                if (hasResources(candidate))
                {
                    resolvedDir = candidate;
                    juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from breadcrumb: "
                                             + candidate.getFullPathName());
                }
            }
        }

        // 6. AppData fallback (installer copies core resources here)
        if (!resolvedDir.isDirectory())
        {
            auto appData = juce::File::getSpecialLocation(
                               juce::File::userApplicationDataDirectory)
                               .getChildFile("Samplar");
            if (hasResources(appData))
            {
                resolvedDir = appData;
                juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from AppData: "
                                         + appData.getFullPathName());
            }
        }

#ifdef SAMPLAR_DEV_RESOURCE_PATH
        // 7. DEV-ONLY fallback: hard-coded path baked in at build time when
        // -DSAMPLAR_ENABLE_DEV_FALLBACK=ON is passed to CMake. Lets developer builds
        // of the installed VST3 find the project's SAMPLAR_Portable resources even
        // when the registry / Program Files / breadcrumb strategies all miss.
        // This code is compiled OUT of release builds.
        if (!resolvedDir.isDirectory())
        {
            juce::File devDir { juce::String(SAMPLAR_DEV_RESOURCE_PATH) };
            if (hasResources(devDir))
            {
                resolvedDir = devDir;
                juce::Logger::writeToLog("[SamplarAudioProcessor] VST3 mode: app directory from DEV fallback: "
                                         + devDir.getFullPathName());
            }
            else
            {
                juce::Logger::writeToLog("[SamplarAudioProcessor] DEV fallback tried but missing resources: "
                                         + devDir.getFullPathName());
            }
        }
#endif

        if (resolvedDir.isDirectory())
        {
            samplar::DataPaths::setAppDirectory(resolvedDir);
        }
        else
        {
            auto vst3Dll = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            juce::Logger::writeToLog("[SamplarAudioProcessor] WARNING: VST3 mode but no SAMPLAR install found!");
            juce::Logger::writeToLog("[SamplarAudioProcessor]   Registry: " + (regPath.isEmpty() ? "(not set)" : regPath));
            juce::Logger::writeToLog("[SamplarAudioProcessor]   VST3 DLL: " + vst3Dll.getFullPathName());
            juce::Logger::writeToLog("[SamplarAudioProcessor]   Program Files: "
                + juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
                    .getChildFile("SAMPLAR").getFullPathName());
            juce::Logger::writeToLog("[SamplarAudioProcessor]   AppData: "
                + juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Samplar").getFullPathName());
        }
    }

    // Early logging
    juce::File logsDir = samplar::getLogsDirectory();
    juce::File logFile = logsDir.getChildFile("SAMPLAR_console.log");
    earlyLogger_ = acquireSharedLogger(logFile);

    // Set our FileLogger as the global JUCE logger in all modes so we can
    // diagnose crashes.  On shutdown we set currentLogger to nullptr BEFORE
    // resetting earlyLogger_, and we do so only after all service threads
    // have been joined � so no thread can hold a dangling logger pointer.
    juce::Logger::writeToLog("[SamplarAudioProcessor] Logging at: " + logFile.getFullPathName()
                             + " (shared by " + juce::String((int) earlyLogger_.use_count()) + " instance(s))");

    // Create registry and register all services
    serviceRegistry_ = std::make_unique<ServiceRegistry>();

    {
        auto licenseSvc = std::make_unique<LicenseSystemService>(eventBus_);
        licenceAudioAllowed_ = &licenseSvc->audioAllowedFlag();   // stable for the processor's lifetime
        serviceRegistry_->registerService("license", std::move(licenseSvc));
    }

    // AudioDeviceService opens the system audio hardware via AudioDeviceManager.
    // In VST3/plugin mode the DAW owns the audio device � opening it here
    // conflicts with the host and causes a crash on plugin toggle (closeAudioDevice
    // fighting FL Studio's audio stack).  Only register it in standalone mode.
    if (isRunningStandalone())
        serviceRegistry_->registerService("audio",
            std::make_unique<AudioDeviceService>());

    serviceRegistry_->registerService("update_checker",
        std::make_unique<UpdateCheckService>(eventBus_));

    {
        auto midiSvc = std::make_unique<MidiMappingService>(eventBus_);
        midiMapping_ = midiSvc.get();
        midiMapping_->setAudioGate(licenceAudioAllowed_);   // ramped live gate + export check (N-18)
        serviceRegistry_->registerService("midi.mapping", std::move(midiSvc));
    }

    // S-Mode service (survives editor close/reopen in VST3)
    serviceRegistry_->registerService("smode",
        std::make_unique<SModeService>(eventBus_));

    serviceRegistry_->initialiseAll();

    // Wire ServiceLocator so all UI code can find services
    claimServiceLocator();

    // UI requests tempo re-publish (when DAW sync checkbox is toggled on)
    eventBus_.subscribe("host.tempo.request", [this](const juce::var&) {
        tempoReRequestPending_.store(true);
    });

    // Auto-wire S-Mode model to MIDI when stems are loaded (persists without editor)
    eventBus_.subscribe("smode.workflow.ready", [this](const juce::var&) {
        auto* smodeSvc = dynamic_cast<SModeService*>(serviceRegistry_->get("smode"));
        if (smodeSvc && midiMapping_)
        {
            midiMapping_->setSModeModel(&smodeSvc->getModel());
            juce::Logger::writeToLog("[SamplarAudioProcessor] Wired SModeModel to MidiMappingService (processor-level)");
        }
    });

    // Standalone MIDI: auto-connect MidiMappingService to AudioDeviceManager
    // and enable all detected MIDI input devices so MIDI works out of the box.
    if (isRunningStandalone() && midiMapping_)
    {
        auto* audioSvc = dynamic_cast<AudioDeviceService*>(serviceRegistry_->get("audio"));
        if (audioSvc)
        {
            auto& devMgr = audioSvc->deviceManager();
            midiMapping_->connectToDeviceManager(&devMgr);
            juce::Logger::writeToLog("[SamplarAudioProcessor] Standalone: MidiMappingService connected to DeviceManager");

            // Auto-enable all available MIDI input devices
            auto midiDevices = juce::MidiInput::getAvailableDevices();
            for (const auto& dev : midiDevices)
            {
                devMgr.setMidiInputDeviceEnabled(dev.identifier, true);
                juce::Logger::writeToLog("[SamplarAudioProcessor] Standalone: auto-enabled MIDI device: "
                    + dev.name + " (" + dev.identifier + ")");
            }
        }
    }

    // Pre-warm singletons (CustomCategoryManager removed)

    servicesInitialised_ = true;
    juce::Logger::writeToLog("[SamplarAudioProcessor] All services initialised");
}

void SamplarAudioProcessor::claimServiceLocator()
{
    if (serviceRegistry_ == nullptr || appState_ == nullptr)
        return;
    ServiceLocator::claim(serviceRegistry_.get(), &eventBus_, &uiState_, appState_.get());
    juce::Logger::writeToLog("[ServiceLocator] claimed by instance 0x"
                             + juce::String::toHexString((juce::pointer_sized_int) this));
}

void SamplarAudioProcessor::shutdownServices()
{
    if (!servicesInitialised_) return;

    juce::Logger::writeToLog("[SamplarAudioProcessor] shutdownServices START");

    // Owner-guarded: never clears another live instance's globals.
    ServiceLocator::shutdown(serviceRegistry_.get());

    if (serviceRegistry_) {
        serviceRegistry_->shutdownAll();
        serviceRegistry_.reset();
    }

    servicesInitialised_ = false;

    // All service threads are joined by shutdownAll() above.
    // It is now safe to null the global logger and destroy the FileLogger
    // because no thread can be mid-call into it.
    juce::Logger::writeToLog("[SamplarAudioProcessor] shutdownServices END");
    releaseSharedLogger(earlyLogger_);
}

// ----------------------------------------------------------
// AudioProcessor � playback
// ----------------------------------------------------------

void SamplarAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] prepareToPlay sr="
        + juce::String(sampleRate) + " block=" + juce::String(samplesPerBlock));

    if (midiMapping_ != nullptr)
    {
        // Pre-allocates every voice, pitch shifter and scratch buffer (A22).
        midiMapping_->prepareToPlay(samplesPerBlock, sampleRate);
        // Pitched voices are pre-rolled so they line up with unpitched ones;
        // the engine adds no latency (A12). Reported explicitly for the host.
        setLatencySamples(midiMapping_->getLatencySamples());
        juce::Logger::writeToLog("[SamplarAudioProcessor] latency reported: "
            + juce::String(midiMapping_->getLatencySamples()) + " samples, tail "
            + juce::String(midiMapping_->getTailLengthSeconds(), 2) + " s");
    }
}

void SamplarAudioProcessor::releaseResources()
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] releaseResources");

    if (midiMapping_ != nullptr)
        midiMapping_->releaseResources();
}

double SamplarAudioProcessor::getTailLengthSeconds() const
{
    return midiMapping_ != nullptr ? midiMapping_->getTailLengthSeconds() : 0.0;
}

// Realtime-safe: no allocation, no locks, no logging, no messages (A1/A9/A10).
void SamplarAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (midiMapping_ == nullptr)
        return;

    // Host transport for sample-accurate sequencing + DAW tempo sync.
    engine::HostInfo host;
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            host.valid   = true;
            host.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())         host.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { host.ppq = *ppq; host.ppqValid = true; }
        }
    }
    host.offline = isNonRealtime();
    // N-4: never pass a non-finite / absurd BPM or PPQ on (BPM -> 0 or clamped
    // to 20..999, PPQ -> "no position"); the timer logs the count.
    if (engine::sanitizeHostInfo(host))
        midiMapping_->noteRejectedHostInfo();

    // UI asked for a host.tempo re-publish (DAW sync toggled on): the engine's
    // message-thread timer publishes it (A9).
    if (tempoReRequestPending_.exchange(false))
        midiMapping_->requestHostTempoRepublish();

    // MIDI (notes, pitch bend, CC, aftertouch) is applied at each event's
    // sample offset inside the engine.
    // Licence gate: the engine keeps running (FIFOs drain, voices advance) but
    // output is silent while unlicensed / expired / locked. renderBlock applies
    // it with a 5 ms ramp (N-18: no hard cut / hard start on a flip).
    midiMapping_->renderBlock(buffer, midiMessages, host);
    if (licenceAudioAllowed_ == nullptr)
        buffer.clear();   // no licence service: never audible
}

void SamplarAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (midiMapping_ != nullptr)
        midiMapping_->renderBlockBypassed(buffer);   // gated (ramped) inside
    else
        buffer.clear();
    if (licenceAudioAllowed_ == nullptr)
        buffer.clear();
}

// ----------------------------------------------------------
// State save / restore
// ----------------------------------------------------------

// -- State save / restore ----------------------------------------------------
// Delegated to AppState (see src/app/AppState.{h,cpp} and
// docs/superpowers/specs/2026-04-21-appstate-full-plan.md).
// Built-in ui/smode/midi bindings are registered by registerDefaultBindings().

void SamplarAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] === getStateInformation START ===");
    appState_->serialize(destData);
    juce::Logger::writeToLog("[SamplarAudioProcessor] === getStateInformation END === ("
        + juce::String(static_cast<int>(destData.getSize())) + " bytes)");
}

void SamplarAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] === setStateInformation START === ("
        + juce::String(sizeInBytes) + " bytes)");

    appState_->deserialize(data, sizeInBytes);

    // Notify editor (if already created) to consume restored state.
    // Uses callAsync because setStateInformation may be called from any thread,
    // and the editor (RootComponent) must process on the message thread.
    juce::MessageManager::callAsync([this, alive = aliveToken_]() {
        if (!alive->load()) return;
        eventBus_.publish("state.restored", juce::var());
    });

    juce::Logger::writeToLog("[SamplarAudioProcessor] === setStateInformation END ===");
}

// -- Register built-in ui / smode / midi bindings -----------------------------
void SamplarAudioProcessor::registerDefaultBindings()
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] registerDefaultBindings START");

    // -- ui binding -------------------------------------------------------
    defaultBindings_.push_back(std::make_unique<ScopedStateBinding>(
        *appState_,
        StateBinding {
            juce::Identifier("ui"), 1,
            // save
            [this](juce::ValueTree& t)
            {
                const auto& ui = uiState_;
                t.setProperty("currentPage",   ui.currentPage,    nullptr);
                t.setProperty("loadedFolder",  ui.loadedFolder,   nullptr);
                t.setProperty("loadedFiles",   ui.loadedFiles,    nullptr);
                t.setProperty("libraryMode",   ui.libraryMode,    nullptr);
                t.setProperty("libraryFolder", ui.libraryFolder,  nullptr);
            },
            // restore (writes to pendingFields so existing UI consumers still work)
            [this](const juce::ValueTree& t)
            {
                auto& ui = uiState_;
                ui.pendingDAWRestore      = true;
                ui.pendingPage            = static_cast<int>(t.getProperty("currentPage"));
                ui.pendingFolder          = t.getProperty("loadedFolder").toString();
                ui.pendingLibraryMode     = static_cast<bool>(t.getProperty("libraryMode"));
                ui.pendingLibraryFolder   = t.getProperty("libraryFolder").toString();
                ui.loadedFiles            = t.getProperty("loadedFiles").toString();

                juce::Logger::writeToLog("[ui-binding] restored page="
                    + juce::String(ui.pendingPage)
                    + " folder=" + ui.pendingFolder
                    + " libraryMode=" + juce::String(ui.pendingLibraryMode ? "YES" : "NO"));
            }
        }));

    // -- smode binding (schema 2, A4 / B8 / A17) ----------------------------
    // The whole S-Mode session lives here: song + stem files, every segment,
    // stem flags, tempo, grid offset, page UI state (lanes) and the engine's
    // play settings. Restore happens in the PROCESSOR (SModeService), with or
    // without an editor (B9). See src/services/smode/SModeSession.h.
    // "smodePage" (the page's old lane binding) is superseded by this node:
    // it stored lanes as DynamicObjects, which ValueTree::writeToStream drops.
    appState_->addSupersededNode("smodePage");
    // The page's old per-stem play bindings (review N-2 / N-18): every save
    // writes <play> (below), so these legacy nodes are never written again
    // (stale values cannot override <play> on reload). Old projects that only
    // have them are migrated by the restore below (read when <play> is absent).
    appState_->addSupersededNode("smodeStemPitch");
    appState_->addSupersededNode("smodeStemMod");
    defaultBindings_.push_back(std::make_unique<ScopedStateBinding>(
        *appState_,
        StateBinding {
            juce::Identifier("smode"), 1,
            // save (message thread: AppState never runs saves elsewhere)
            [this](juce::ValueTree& t)
            {
                t.removeAllChildren(nullptr);
                for (int i = t.getNumProperties() - 1; i >= 0; --i)
                    if (t.getPropertyName(i) != juce::Identifier("version"))
                        t.removeProperty(t.getPropertyName(i), nullptr);
                t.setProperty("schema", session::kSchema, nullptr);

                auto* svc = smodeService();
                if (svc != nullptr)
                {
                    auto sessionTree = svc->createSessionTree();
                    if (sessionTree.isValid())
                        t.appendChild(sessionTree, nullptr);
                }

                if (midiMapping_ != nullptr)
                {
                    session::PlaySettings p;
                    p.mono          = midiMapping_->isMonophonic();
                    p.gate          = midiMapping_->isSampleGateMode();
                    p.loopGate      = midiMapping_->isLoopGateMode();
                    p.transportLoop = midiMapping_->isSModeTransportLooping();
                    p.dawSync       = midiMapping_->isDawTempoSync();
                    for (const auto& b : midiMapping_->getCcBindings())
                        p.ccBindings.push_back({ b.channel, b.cc, b.target });
                    for (int i = 0; i < (int) p.stems.size(); ++i)
                    {
                        auto& s = p.stems[(size_t) i];
                        s.pitch      = midiMapping_->getStemPitchSemitones(i);
                        s.velCurve   = (int) midiMapping_->getStemVelocityCurve(i);
                        s.bendDest   = (int) midiMapping_->getStemPitchBendDest(i);
                        s.wheelDest  = (int) midiMapping_->getStemModWheelDest(i);
                        s.atDest     = (int) midiMapping_->getStemAftertouchDest(i);
                        s.bendRange  = midiMapping_->getStemPitchBendRange(i);
                        s.wheelRange = midiMapping_->getStemModWheelRange(i);
                        s.atRange    = midiMapping_->getStemAftertouchRange(i);
                    }
                    t.appendChild(session::writePlay(p), nullptr);
                }
                juce::Logger::writeToLog("[smode-binding] saved schema " + juce::String(session::kSchema)
                    + (t.getChildWithName("session").isValid()
                           ? " session segments=" + juce::String(session::countSegments(t.getChildWithName("session")))
                           : juce::String(" (no session)")));
            },
            // restore (message thread: AppState defers off-thread restores, A18).
            // `t` is INVALID when the loaded project has no "smode" node
            // (restoreWhenMissing): the previous session and play settings are
            // reset instead of leaking into that project (review N-18).
            [this](const juce::ValueTree& t)
            {
                juce::Logger::writeToLog("[smode-binding] === restore START ("
                    + (t.isValid() ? "schema " + t.getProperty("schema", 0).toString()
                                   : juce::String("project has NO smode node -> reset")) + ") ===");
                const int schema = (int) t.getProperty("schema", 0);
                if (schema > session::kSchema)
                    juce::Logger::writeToLog("[smode-binding] WARNING schema " + juce::String(schema)
                        + " is newer than this build (" + juce::String(session::kSchema) + ") - reading best-effort");

                // 1. Play settings go straight into the engine (atomics): MIDI
                //    plays with the right mode / stem pitch even before the
                //    stems finish loading, and a save in between writes them back.
                if (midiMapping_ != nullptr)
                {
                    // <play> wins; the legacy page nodes (clamped by the tolerant
                    // reader) only when <play> is absent; else defaults.
                    juce::String playSource;
                    const auto p = session::selectPlaySettings(t, appState_->getChildCopy("smodeStemPitch"),
                                                               appState_->getChildCopy("smodeStemMod"), &playSource);
                    juce::Logger::writeToLog("[smode-binding] play settings source: " + playSource);
                    midiMapping_->setMonophonic(p.mono);
                    midiMapping_->setSampleGateMode(p.gate);
                    midiMapping_->setLoopGateMode(p.loopGate);
                    midiMapping_->setSModeTransportLooping(p.transportLoop);
                    midiMapping_->setDawTempoSync(p.dawSync);
                    {
                        std::vector<MidiMappingService::CcBindingInfo> cc;
                        for (const auto& b : p.ccBindings)
                            cc.push_back({ b.channel, b.cc, b.target });
                        midiMapping_->setCcBindings(cc);   // schema 1 projects: none
                    }
                    using MM = MidiMappingService;
                    for (int i = 0; i < (int) p.stems.size(); ++i)
                    {
                        const auto& s = p.stems[(size_t) i];
                        midiMapping_->setStemPitchSemitones(i, s.pitch);
                        midiMapping_->setStemVelocityCurve(i, (MM::VelocityCurve) s.velCurve);
                        midiMapping_->setStemPitchBendDest(i, (MM::ModDestination) s.bendDest);
                        midiMapping_->setStemModWheelDest(i, (MM::ModDestination) s.wheelDest);
                        midiMapping_->setStemAftertouchDest(i, (MM::ModDestination) s.atDest);
                        midiMapping_->setStemPitchBendRange(i, s.bendRange);
                        midiMapping_->setStemModWheelRange(i, s.wheelRange);
                        midiMapping_->setStemAftertouchRange(i, s.atRange);
                    }
                    juce::Logger::writeToLog("[smode-binding] play settings restored: "
                        + juce::String(p.mono ? "MONO" : "POLY") + " " + juce::String(p.gate ? "GATE" : "1SH")
                        + " transportLoop=" + juce::String(p.transportLoop ? 1 : 0)
                        + " dawSync=" + juce::String(p.dawSync ? 1 : 0)
                        + " ccBindings=" + juce::String((int) p.ccBindings.size())
                        + " stem0 pitch=" + juce::String(p.stems[0].pitch, 2));
                }

                // 2. The session: stems are decoded / re-separated in the
                //    service; the saved data stays pending until applied.
                auto sessionTree = t.getChildWithName("session");
                if (! sessionTree.isValid() && t.hasProperty("sourceFile"))
                {
                    // Pre-schema project (path + stems only, no chops).
                    juce::Logger::writeToLog("[smode-binding] pre-schema project: restoring song + stems only");
                    sessionTree = juce::ValueTree("session");
                    sessionTree.setProperty("sourceFile", t.getProperty("sourceFile"), nullptr);
                    const auto stems = t.getChildWithName("stems");
                    for (int i = 0; i < stems.getNumChildren(); ++i)
                    {
                        juce::ValueTree st("stem");
                        st.setProperty("index", i, nullptr);
                        st.setProperty("file", stems.getChild(i).getProperty("file"), nullptr);
                        sessionTree.appendChild(st, nullptr);
                    }
                }
                if (auto* svc = smodeService())
                    svc->restoreSession(sessionTree);

                rebaselinePending_ = true;   // loading a project must not mark it dirty
                juce::Logger::writeToLog("[smode-binding] === restore END ===");
            },
            true   // restoreWhenMissing (N-18)
        }));

    // midi binding removed (B6): the browser-pad MIDI map it saved was dead code
    // in SAMPLAR (always 0 slots). Old projects still load: AppState keeps an
    // unknown "midi" subtree and writes it back unchanged.

    // variation binding removed (VariationService deleted in SAMPLAR fork)

    juce::Logger::writeToLog("[SamplarAudioProcessor] registerDefaultBindings END ("
        + juce::String(static_cast<int>(defaultBindings_.size())) + " bindings)");
}

// ----------------------------------------------------------
// Project persistence helpers (A16 / A18) + debug session hook
// ----------------------------------------------------------

SModeService* SamplarAudioProcessor::smodeService() const
{
    return serviceRegistry_ != nullptr ? dynamic_cast<SModeService*>(serviceRegistry_->get("smode")) : nullptr;
}

uint64_t SamplarAudioProcessor::computeStateFingerprint()
{
    uint64_t h = 0;
    auto mix = [&h](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    if (auto* svc = smodeService())
        mix(svc->computeSessionFingerprint());
    return h;
}

uint64_t SamplarAudioProcessor::computePlayFingerprint()
{
    uint64_t h = 0;
    auto mix = [&h](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    auto mixF = [&mix](float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); mix(u); };
    if (midiMapping_ != nullptr)
    {
        mix((uint64_t) midiMapping_->isMonophonic() | ((uint64_t) midiMapping_->isSampleGateMode() << 1)
            | ((uint64_t) midiMapping_->isLoopGateMode() << 2) | ((uint64_t) midiMapping_->isSModeTransportLooping() << 3)
            | ((uint64_t) midiMapping_->isDawTempoSync() << 4));
        for (const auto& b : midiMapping_->getCcBindings())
            mix(((uint64_t) b.channel << 40) ^ ((uint64_t) b.cc << 32) ^ (uint64_t) (uint32_t) b.target.hashCode());
        for (int i = 0; i < MidiMappingService::MAX_STEMS; ++i)
        {
            mixF(midiMapping_->getStemPitchSemitones(i));
            mix((uint64_t) midiMapping_->getStemVelocityCurve(i) | ((uint64_t) midiMapping_->getStemPitchBendDest(i) << 8)
                | ((uint64_t) midiMapping_->getStemModWheelDest(i) << 16) | ((uint64_t) midiMapping_->getStemAftertouchDest(i) << 24));
            mixF(midiMapping_->getStemPitchBendRange(i));
            mixF(midiMapping_->getStemModWheelRange(i));
            mixF(midiMapping_->getStemAftertouchRange(i));
        }
    }
    return h;
}

void SamplarAudioProcessor::pollStateChanges()
{
    if (! servicesInitialised_ || appState_ == nullptr || appState_->isRestoreDispatchPending())
        return;
    auto* svc = smodeService();
    if (svc == nullptr)
        return;

    const uint64_t fp = computeStateFingerprint();
    const uint64_t playFp = computePlayFingerprint();
    const uint64_t completions = svc->getRestoreCompletionCount();
    const uint64_t relinks = svc->getRelinkCount();
    const bool restoring = svc->getSessionStatus() == SModeService::SessionStatus::Restoring;

    if (rebaselinePending_)
    {
        // The smode binding just applied a project (session + play settings):
        // that is the clean baseline. Refresh the cached tree, do not dirty.
        appState_->snapshotBinding("smode");
        lastStateFingerprint_ = fp;
        lastPlayFingerprint_ = playFp;
        seenRestoreCompletions_ = completions;
        seenRelinks_ = relinks;
        rebaselinePending_ = false;
        juce::Logger::writeToLog("[SamplarAudioProcessor] state watcher re-baselined after a project load"
                                 + juce::String(restoring ? " (session still restoring)" : ""));
        return;
    }

    // Review N-15: while a project restore decodes / re-separates (or just
    // finished), its SESSION changes are not user edits -> re-baseline only the
    // session part. Play settings (and a user-confirmed relink) are user edits
    // even then, so they keep dirtying the project.
    bool sessionDirty = false;
    if (restoring || completions != seenRestoreCompletions_)
    {
        if (fp != lastStateFingerprint_ || completions != seenRestoreCompletions_)
            appState_->snapshotBinding("smode");
        lastStateFingerprint_ = fp;
        seenRestoreCompletions_ = completions;
    }
    else
    {
        sessionDirty = fp != lastStateFingerprint_;
    }
    const bool playDirty = playFp != lastPlayFingerprint_;
    const bool relinked = relinks != seenRelinks_;
    if (! sessionDirty && ! playDirty && ! relinked)
        return;

    // Keep the cached tree current for getStateInformation calls that arrive
    // off the message thread (they stream it without running any binding).
    appState_->snapshotBinding("smode");
    if (! appState_->markDirty())
    {
        // Not notified (suppressed on this thread): keep the old baseline so the
        // next tick tries again instead of losing the edit.
        juce::Logger::writeToLog("[SamplarAudioProcessor] S-Mode change seen but dirty notification suppressed - retrying next tick");
        return;
    }
    // Move the baselines forward only after the host was notified.
    lastStateFingerprint_ = fp;
    lastPlayFingerprint_ = playFp;
    seenRelinks_ = relinks;
    juce::Logger::writeToLog(juce::String("[SamplarAudioProcessor] S-Mode ")
                             + (sessionDirty ? "session " : "") + (playDirty ? "play-settings " : "")
                             + (relinked ? "relink " : "") + "change -> project marked dirty");
}

void SamplarAudioProcessor::writeDebugAck(const juce::String& name, const juce::String& text) const
{
    if (debugAckDir_ == juce::File())
        return;
    debugAckDir_.createDirectory();
    const auto f = debugAckDir_.getChildFile(name);
    f.replaceWithText(text);
    juce::Logger::writeToLog("[SessionDebug] ack written: " + f.getFullPathName());
}

void SamplarAudioProcessor::startDebugSessionHookIfRequested()
{
    const auto ack  = juce::SystemStats::getEnvironmentVariable("SAMPLAR_DEBUG_SESSION_ACK", {});
    const auto seed = juce::SystemStats::getEnvironmentVariable("SAMPLAR_DEBUG_SESSION_SEED", {});
    if (ack.isEmpty())
        return;
    debugAckDir_ = juce::File(ack);
    juce::Logger::writeToLog("[SessionDebug] hook active: ack dir=" + ack + " seed=" + (seed.isEmpty() ? juce::String("(none)") : seed));

    auto describePlay = [this]()
    {
        juce::String d;
        if (midiMapping_ == nullptr) return d;
        d << " mono=" << (int) midiMapping_->isMonophonic() << " gate=" << (int) midiMapping_->isSampleGateMode()
          << " tloop=" << (int) midiMapping_->isSModeTransportLooping();
        for (int i = 0; i < 4; ++i)
            d << " pitch" << i << "=" << juce::String(midiMapping_->getStemPitchSemitones(i), 2);
        d << " pbr1=" << juce::String(midiMapping_->getStemPitchBendRange(1), 2);
        d << " dawSync=" << (int) midiMapping_->isDawTempoSync() << " cc=[";
        for (const auto& b : midiMapping_->getCcBindings())
            d << b.channel << ":" << b.cc << ":" << b.target << " ";
        d << "]";
        return d;
    };

    eventBus_.subscribe("smode.workflow.ready", [this, alive = aliveToken_, describePlay](const juce::var&)
    {
        if (! alive->load()) return;
        auto* svc = smodeService();
        if (svc == nullptr) return;
        if (debugSeedPending_)
        {
            debugSeedPending_ = false;
            seedDebugSession();
            // The smoke host compares rendered audio only when this instance may play it.
            writeDebugAck("licence.txt", juce::String("audioAllowed=") + (licenceAudioAllowed_ != nullptr && licenceAudioAllowed_->load() ? "1" : "0"));
            writeDebugAck("seeded.txt", session::describeModel(svc->getModel()) + describePlay());
            return;
        }
        writeDebugAck("ready_" + juce::String(++debugAckSeq_) + ".txt",
                      "restores=" + juce::String((juce::int64) svc->getRestoreCompletionCount())
                      + " pendingExtra=" + juce::String(svc->getModel().hasPendingExtraState() ? 1 : 0) + "\n"
                      + session::describeModel(svc->getModel()) + describePlay());
    });
    eventBus_.subscribe("smode.session.missing", [this, alive = aliveToken_](const juce::var& msg)
    {
        if (! alive->load()) return;
        writeDebugAck("missing_" + juce::String(++debugAckSeq_) + ".txt", msg.toString());
    });

    if (seed.isNotEmpty())
    {
        debugSeedPending_ = true;
        juce::MessageManager::callAsync([this, alive = aliveToken_, seed]()
        {
            if (! alive->load()) return;
            if (auto* svc = smodeService())
            {
                juce::Logger::writeToLog("[SessionDebug] seeding: loading " + seed);
                svc->startWorkflow(juce::File(seed), SModeSeparationService::StemCount::Four);
            }
        });
    }
}

void SamplarAudioProcessor::seedDebugSession()
{
    auto* svc = smodeService();
    if (svc == nullptr) return;
    auto& m = svc->getModel();
    juce::Logger::writeToLog("[SessionDebug] seeding a known session on " + juce::String(m.getStemCount()) + " stems");
    for (int s = 0; s < m.getStemCount(); ++s)
    {
        const auto len = (int64_t) m.getStem(s)->audioBuffer.getNumSamples();
        for (int k = 0; k < 2; ++k)
        {
            const auto start = (int64_t) (len * (0.10 + 0.45 * k));
            const int idx = m.addSegment(s, start, start + (int64_t) (len * 0.35));
            if (idx >= 0) m.assignMidiNote(s, idx, 60 + 2 * s + k);
        }
    }
    if (auto* seg = m.getSegment(0, 1)) { seg->effects.pitchSemitones = 2; seg->effects.loop = false; seg->effects.pan = -0.25f; seg->effects.attackMs = 5.0f; seg->effects.releaseMs = 40.0f; seg->effects.triggerMode = 1; }
    m.setDetectedTempo(123.5);
    m.setGridOffsetSeconds(0.125);
    m.setLinked(1, true);
    m.setMute(3, true);
    m.setStemVolume(2, 0.8f);
    if (midiMapping_ != nullptr)
    {
        midiMapping_->setSampleGateMode(true);
        midiMapping_->setStemPitchSemitones(0, 3.0f);
        midiMapping_->setStemPitchBendRange(1, 7.0f);
        midiMapping_->setSModeTransportLooping(true);
        // Schema 2 fields: DAW tempo sync + MIDI CC bindings.
        midiMapping_->setDawTempoSync(true);
        midiMapping_->setCcBindings({ { 1, 74, "stem0.pitch" }, { 0, 1, "stem2.volume" } });
    }
    // Two sequencer lanes + grid division 1/8, in the page's own format.
    svc->debugSetUiExtraJson(R"({"v":1,"gridLocked":false,"lockedBpm":0.0,"gridDivision":8,"loopEnabled":false,"linkAll":false,)"
        R"("lanes":[{"patternType":0,"numBars":4,"slotsPerBar":1,"muted":false,"bpm":123.5,"slots":[)"
        R"({"stemIndex":0,"segmentIndex":0,"role":65,"empty":false,"velocity":1.0,"pitch":0.0,"reverse":false,"probability":1.0,"trimStart":0.0,"trimEnd":1.0,"cellSpan":1,"gridCell":0},)"
        R"({"stemIndex":2,"segmentIndex":1,"role":66,"empty":false,"velocity":0.8,"pitch":0.0,"reverse":false,"probability":1.0,"trimStart":0.0,"trimEnd":1.0,"cellSpan":2,"gridCell":4}]},)"
        R"({"patternType":0,"numBars":2,"slotsPerBar":2,"muted":true,"bpm":123.5,"slots":[)"
        R"({"stemIndex":1,"segmentIndex":0,"role":65,"empty":false,"velocity":1.0,"pitch":0.0,"reverse":false,"probability":1.0,"trimStart":0.0,"trimEnd":1.0,"cellSpan":1,"gridCell":2}]}]})");
}

// ----------------------------------------------------------
// Editor
// ----------------------------------------------------------

juce::AudioProcessorEditor* SamplarAudioProcessor::createEditor()
{
    return new SamplarAudioProcessorEditor(*this);
}

// ----------------------------------------------------------
// Bus layout
// ----------------------------------------------------------

bool SamplarAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Accept stereo out (required). Input is optional.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input must be disabled or stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

// ----------------------------------------------------------
// Runtime mode detection
// ----------------------------------------------------------

bool SamplarAudioProcessor::isRunningAsPlugin() const
{
    // Use static PluginHostType � reliable even during constructor
    // (wrapperType member may not be set yet when initServices() runs)
    return juce::PluginHostType::getPluginLoadedAs() != wrapperType_Standalone;
}

bool SamplarAudioProcessor::isRunningStandalone() const
{
    return juce::PluginHostType::getPluginLoadedAs() == wrapperType_Standalone;
}

} // namespace samplar

// ----------------------------------------------------------
// JUCE plugin entry point
// ----------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new samplar::SamplarAudioProcessor();
}
