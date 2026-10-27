#include "SamplarAudioProcessor.h"
#include "../app/ServiceLocator.h"
#include "../app/AudioDeviceService.h"
#include "../utils/Tracing.h"
#include "../utils/FontWarmer.h"
#include "../services/LicenseSystemService.h"
#include "../services/ProcessingCoordinatorService.h"
#include "../services/UpdateCheckService.h"
#include "../services/MidiMappingService.h"
#include "../services/smode/SModeService.h"
#include "../services/FullSongSeparationService.h"
#include "../audio/PluginPreviewService.h"
#include "../audio/AudioMeterTap.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "../utils/LogPaths.h"
#include "../utils/DataPaths.h"
#include "SamplarAudioProcessorEditor.h"

namespace samplar {

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
    samplar::FontWarmer::warmInBackground();
    juce::Logger::writeToLog("[SamplarAudioProcessor] === CONSTRUCTOR START ===");
    appState_ = std::make_unique<AppState>(*this);
    initServices();
    registerDefaultBindings();
    juce::Logger::writeToLog("[SamplarAudioProcessor] === CONSTRUCTOR END ===");
}

SamplarAudioProcessor::~SamplarAudioProcessor()
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] === DESTRUCTOR START ===");
    {
        SAMPLAR_TRACE_SCOPE("lifecycle", "SamplarAudioProcessor::dtor");
        {
            SAMPLAR_TRACE_SCOPE("lifecycle", "shutdownServices");
            shutdownServices();
        }
    }
    SAMPLAR_TRACE_SESSION_STOP();
    juce::Logger::writeToLog("[SamplarAudioProcessor] === DESTRUCTOR END ===");
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
        // Require data/ directory specifically � pattern databases live there
        auto hasResources = [](const juce::File& dir) {
            return dir.isDirectory()
                && dir.getChildFile("data").isDirectory();
        };

        // 0. Portable pointer file (highest priority � explicit user action)
        //    Written by "Setup VST3.cmd" inside the portable .zip distribution.
        //    Path lives at %APPDATA%\Samplar\portable_root.txt and contains
        //    the absolute path of the user's extracted portable folder. If the
        //    pointer is stale (folder moved/deleted), hasResources() rejects it
        //    and we fall through to the standard installer strategies below.
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

        // 0.5. Inside the VST3 bundle (Inno installer layout, default 2026-05-18+)
        //      The installer drops models/, data/, BACKGROUNDS/, Samples/, tools/
        //      INTO .vst3/Contents/x86_64-win/ so the bundle is self-contained.
        //      That dir IS the parent of the .vst3 binary, so just check it.
        if (!resolvedDir.isDirectory())
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

        // 1. Registry install path (Windows only — Inno installer writes this).
        // macOS has no equivalent system registry; the .pkg installer places
        // resources inside the .vst3/.component bundle's Contents/Resources/
        // and the bundle-relative lookup above already covers that case.
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
    if (logFile.existsAsFile()) (void) logFile.deleteFile();
    earlyLogger_.reset(new juce::FileLogger(logFile, "[SAMPLAR] Startup", 0));

    // Set our FileLogger as the global JUCE logger in all modes so we can
    // diagnose crashes.  On shutdown we set currentLogger to nullptr BEFORE
    // resetting earlyLogger_, and we do so only after all service threads
    // have been joined � so no thread can hold a dangling logger pointer.
    juce::Logger::setCurrentLogger(earlyLogger_.get());
    juce::Logger::writeToLog("[SamplarAudioProcessor] Logging at: " + logFile.getFullPathName());

    // Create registry and register all services
    serviceRegistry_ = std::make_unique<ServiceRegistry>();

    serviceRegistry_->registerService("license",
        std::make_unique<LicenseSystemService>(eventBus_));

    // AudioDeviceService opens the system audio hardware via AudioDeviceManager.
    // In VST3/plugin mode the DAW owns the audio device � opening it here
    // conflicts with the host and causes a crash on plugin toggle (closeAudioDevice
    // fighting FL Studio's audio stack).  Only register it in standalone mode.
    if (isRunningStandalone())
        serviceRegistry_->registerService("audio",
            std::make_unique<AudioDeviceService>());

    serviceRegistry_->registerService("processing.coordinator",
        std::make_unique<ProcessingCoordinatorService>(eventBus_));

    serviceRegistry_->registerService("update_checker",
        std::make_unique<UpdateCheckService>(eventBus_));

    {
        auto midiSvc = std::make_unique<MidiMappingService>(eventBus_);
        midiMapping_ = midiSvc.get();
        serviceRegistry_->registerService("midi.mapping", std::move(midiSvc));
    }

    // S-Mode service (survives editor close/reopen in VST3)
    serviceRegistry_->registerService("smode",
        std::make_unique<SModeService>(eventBus_));

    // FullSong separation service (survives editor close/reopen in VST3 so
    // closing the wrapper window mid-separation does NOT lose the work).
    serviceRegistry_->registerService("fullSong.separation",
        std::make_unique<FullSongSeparationService>(eventBus_));

    // Plugin mode: register preview audio service so SampleBrowserPage can
    // route preview playback through processBlock instead of AudioDeviceManager.
    if (isRunningAsPlugin())
    {
        auto previewSvc = std::make_unique<PluginPreviewService>();
        pluginPreview_ = previewSvc.get();
        serviceRegistry_->registerService("preview_audio", std::move(previewSvc));
    }

    serviceRegistry_->initialiseAll();

    // Wire ServiceLocator so all UI code can find services
    ServiceLocator::initialise(serviceRegistry_.get(), &eventBus_);
    ServiceLocator::setAppState(appState_.get());

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

void SamplarAudioProcessor::shutdownServices()
{
    if (!servicesInitialised_) return;

    juce::Logger::writeToLog("[SamplarAudioProcessor] shutdownServices START");

    ServiceLocator::shutdown();

    if (serviceRegistry_) {
        serviceRegistry_->shutdownAll();
        serviceRegistry_.reset();
    }

    servicesInitialised_ = false;

    // All service threads are joined by shutdownAll() above.
    // It is now safe to null the global logger and destroy the FileLogger
    // because no thread can be mid-call into it.
    juce::Logger::writeToLog("[SamplarAudioProcessor] shutdownServices END");
    juce::Logger::setCurrentLogger(nullptr);
    earlyLogger_.reset();
}

// ----------------------------------------------------------
// AudioProcessor � playback
// ----------------------------------------------------------

void SamplarAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] prepareToPlay sr="
        + juce::String(sampleRate) + " block=" + juce::String(samplesPerBlock));

    if (midiMapping_ != nullptr)
        midiMapping_->prepareToPlay(samplesPerBlock, sampleRate);

    if (pluginPreview_ != nullptr)
    {
        pluginPreview_->prepareToPlay(samplesPerBlock, sampleRate);
        previewBuffer_.setSize(2, samplesPerBlock);  // Pre-allocate for processBlock
    }
}

void SamplarAudioProcessor::releaseResources()
{
    juce::Logger::writeToLog("[SamplarAudioProcessor] releaseResources");

    if (midiMapping_ != nullptr)
        midiMapping_->releaseResources();

    if (pluginPreview_ != nullptr)
        pluginPreview_->releaseResources();
}

void SamplarAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (midiMapping_ == nullptr)
        return;

    // -- 1. Handle incoming MIDI ? trigger mapped samples --------------------
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            midiMapping_->handleNoteOn(msg.getChannel(),
                                       msg.getNoteNumber(),
                                       msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            midiMapping_->handleNoteOff(msg.getChannel(),
                                        msg.getNoteNumber());
        }
    }

    // -- 2. Pull audio from MIDI-triggered voices into the output buffer ------
    juce::AudioSourceChannelInfo info(&buffer, 0, buffer.getNumSamples());
    midiMapping_->getNextAudioBlock(info);

    // -- 2a. DEBUG audio peak meter — logs ~1x/sec so you can verify signal is
    // flowing without local speakers (useful for RDP/cloud sessions). Lock-free
    // hot path; the once-per-second juce::Logger call holds a mutex briefly but
    // not often enough to cause audio dropouts. Remove this block if it ever does.
    {
        static std::atomic<float> peakAccum{0.0f};
        static std::atomic<int>   samplesAccum{0};
        float blockPeak = 0.0f;
        float peakL = 0.0f, peakR = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            const float chPeak = buffer.getMagnitude(ch, 0, buffer.getNumSamples());
            blockPeak = juce::jmax(blockPeak, chPeak);
            if (ch == 0) peakL = chPeak;
            else if (ch == 1) peakR = chPeak;
        }
        // Publish per-channel peak to the UI meter strip (AudioMeterBars).
        AudioMeterTap::instance().publish(peakL, peakR > 0.0f ? peakR : peakL);
        float prev = peakAccum.load(std::memory_order_relaxed);
        while (blockPeak > prev
               && !peakAccum.compare_exchange_weak(prev, blockPeak,
                                                    std::memory_order_relaxed)) {}
        const int added = samplesAccum.fetch_add(buffer.getNumSamples(),
                                                  std::memory_order_relaxed)
                          + buffer.getNumSamples();
        const int logEveryNSamples = juce::jmax(1, (int) getSampleRate()); // ~1s
        if (added >= logEveryNSamples) {
            const float peak = peakAccum.exchange(0.0f, std::memory_order_relaxed);
            samplesAccum.store(0, std::memory_order_relaxed);
            juce::Logger::writeToLog(
                "[AUDIO-METER] peak=" + juce::String(peak, 4)
                + " (" + juce::String(juce::Decibels::gainToDecibels(peak, -120.0f), 1) + " dBFS) "
                + (peak > 0.0001f ? "SIGNAL" : "silent"));
        }
    }

    // -- 2b. Handle pending tempo re-request ------------------------------------
    const bool wasReRequested = tempoReRequestPending_.exchange(false);
    if (wasReRequested)
    {
        lastHostBpm_ = 0.0;  // Force re-publish below even if BPM hasn't changed
        juce::Logger::writeToLog("[SamplarAudioProcessor] Tempo re-request received, forcing re-publish");
    }

    // -- 2c. Read host tempo and publish for DAW sync --------------------------
    double hostBpm = 0.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;
    }

    // Standalone or hosts without a playhead: synthesise a sensible default so
    // the "DAW" sync toggle still produces something usable instead of silently
    // doing nothing.
    if (hostBpm <= 0.0 && wasReRequested)
    {
        hostBpm = 120.0;
        juce::Logger::writeToLog("[SamplarAudioProcessor] No host BPM available � falling back to 120 BPM");
    }

    if (hostBpm > 0.0 && (wasReRequested || std::abs(hostBpm - lastHostBpm_) > 0.01))
    {
        lastHostBpm_ = hostBpm;
        juce::Logger::writeToLog("[SamplarAudioProcessor] Publishing host.tempo: " + juce::String(hostBpm, 3));
        juce::MessageManager::callAsync([this, hostBpm]() {
            eventBus_.publish("host.tempo", juce::var(hostBpm));
        });
    }

    // -- 3. Mix in sample browser preview audio (plugin mode only) ------------
    if (pluginPreview_ != nullptr)
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();

        // Use pre-allocated buffer (sized in prepareToPlay) � no heap alloc on audio thread
        if (previewBuffer_.getNumSamples() < numSamples || previewBuffer_.getNumChannels() < numCh)
            previewBuffer_.setSize(numCh, numSamples);  // Fallback resize (rare, only if host changes block size)

        previewBuffer_.clear(0, numSamples);
        juce::AudioSourceChannelInfo previewInfo(&previewBuffer_, 0, numSamples);
        pluginPreview_->getNextAudioBlock(previewInfo);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.addFrom(ch, 0, previewBuffer_, ch, 0, numSamples);
    }
}

// ----------------------------------------------------------
// State save / restore
// ----------------------------------------------------------

// -- FLAC encode/decode helpers -----------------------------------------------

// Raw float PCM encoding � simple, no codec crashes, ~4 bytes/sample
// Format: [int32 numChannels][int32 numSamples][float[] interleaved data]
static juce::MemoryBlock encodeBufferRaw(const juce::AudioBuffer<float>& buffer, double /*sampleRate*/)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return {};

    int numCh = buffer.getNumChannels();
    int numSmp = buffer.getNumSamples();
    size_t dataSize = 8 + static_cast<size_t>(numCh) * static_cast<size_t>(numSmp) * sizeof(float);

    juce::MemoryBlock block(dataSize);
    auto* dst = static_cast<char*>(block.getData());

    // Header
    std::memcpy(dst, &numCh, 4);
    std::memcpy(dst + 4, &numSmp, 4);
    dst += 8;

    // Interleaved float samples
    for (int s = 0; s < numSmp; ++s)
        for (int c = 0; c < numCh; ++c)
        {
            float val = buffer.getSample(c, s);
            std::memcpy(dst, &val, sizeof(float));
            dst += sizeof(float);
        }

    return block;
}

static juce::AudioBuffer<float> decodeBufferRaw(const void* data, size_t size, double& outSampleRate)
{
    outSampleRate = 44100.0;  // stored separately in JSON
    if (size < 8) return {};

    auto* src = static_cast<const char*>(data);
    int numCh, numSmp;
    std::memcpy(&numCh, src, 4);
    std::memcpy(&numSmp, src + 4, 4);
    src += 8;

    if (numCh <= 0 || numCh > 8 || numSmp <= 0) return {};
    size_t expectedSize = 8 + static_cast<size_t>(numCh) * static_cast<size_t>(numSmp) * sizeof(float);
    if (size < expectedSize) return {};

    juce::AudioBuffer<float> buf(numCh, numSmp);
    buf.clear();

    for (int s = 0; s < numSmp; ++s)
        for (int c = 0; c < numCh; ++c)
        {
            float val;
            std::memcpy(&val, src, sizeof(float));
            buf.setSample(c, s, val);
            src += sizeof(float);
        }

    return buf;
}

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
    juce::MessageManager::callAsync([this]() {
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

    // -- smode binding ----------------------------------------------------
    defaultBindings_.push_back(std::make_unique<ScopedStateBinding>(
        *appState_,
        StateBinding {
            juce::Identifier("smode"), 1,
            // save
            [this](juce::ValueTree& t)
            {
                auto* svc = dynamic_cast<SModeService*>(serviceRegistry_->get("smode"));
                if (svc == nullptr || ! svc->isActive())
                {
                    // Clear any previous children so old state doesn't linger
                    t.removeAllChildren(nullptr);
                    for (int i = t.getNumProperties() - 1; i >= 0; --i)
                        t.removeProperty(t.getPropertyName(i), nullptr);
                    return;
                }

                const auto& model = svc->getModel();
                t.setProperty("sourceFile", svc->getSourceFile().getFullPathName(), nullptr);
                t.setProperty("stemCount",  model.getStemCount(),  nullptr);
                t.setProperty("bpm",        model.getDetectedTempo(), nullptr);
                t.setProperty("sampleRate", model.getSampleRate(),  nullptr);

                // Stem files as child nodes
                t.removeAllChildren(nullptr);
                const auto& stemFiles = svc->getSeparation().getStemFiles();
                if (! stemFiles.empty())
                {
                    juce::ValueTree stems("stems");
                    for (const auto& f : stemFiles)
                    {
                        juce::ValueTree s("stem");
                        s.setProperty("file", f.getFullPathName(), nullptr);
                        stems.appendChild(s, nullptr);
                    }
                    t.appendChild(stems, nullptr);
                }
                juce::Logger::writeToLog("[smode-binding] saved, stems="
                    + juce::String(static_cast<int>(stemFiles.size())));
            },
            // restore
            [this](const juce::ValueTree& t)
            {
                if (! t.hasProperty("sourceFile"))
                    return;

                auto& ui = uiState_;
                ui.pendingSModeRestore = true;
                ui.smodeSourceFile     = juce::File(t.getProperty("sourceFile").toString());
                ui.smodeStemCount      = static_cast<int>(t.getProperty("stemCount"));
                ui.smodeBpm            = static_cast<double>(t.getProperty("bpm"));
                ui.smodeSampleRate     = static_cast<double>(t.getProperty("sampleRate"));

                // Rebuild a DynamicObject mirror in smodeSegmentData for any legacy
                // UI consumer that still reads it as juce::var.
                auto* legacy = new juce::DynamicObject();
                for (int i = 0; i < t.getNumProperties(); ++i)
                {
                    auto name = t.getPropertyName(i);
                    legacy->setProperty(name, t.getProperty(name));
                }
                ui.smodeSegmentData = juce::var(legacy);

                // Restore stem files: accept either flat "stemFiles" array (legacy JSON import
                // left a property) or "<stems><stem file=.../></stems>" child tree.
                ui.smodeStemFiles.clear();

                auto stemsNode = t.getChildWithName("stems");
                if (stemsNode.isValid())
                {
                    for (int i = 0; i < stemsNode.getNumChildren(); ++i)
                    {
                        auto s = stemsNode.getChild(i);
                        auto path = s.getProperty("file").toString();
                        if (path.isNotEmpty())
                            ui.smodeStemFiles.emplace_back(path);
                    }
                }

                juce::Logger::writeToLog("[smode-binding] restored source="
                    + ui.smodeSourceFile.getFullPathName()
                    + " stems=" + juce::String(static_cast<int>(ui.smodeStemFiles.size())));
            }
        }));

    // -- midi binding -----------------------------------------------------
    defaultBindings_.push_back(std::make_unique<ScopedStateBinding>(
        *appState_,
        StateBinding {
            juce::Identifier("midi"), 1,
            // save
            [this](juce::ValueTree& t)
            {
                t.removeAllChildren(nullptr);
                if (midiMapping_ == nullptr)
                    return;

                auto slots = midiMapping_->getOccupiedSlotsMeta();
                for (const auto& slot : slots)
                {
                    juce::ValueTree s("slot");
                    s.setProperty("note",        slot.noteNumber,  nullptr);
                    s.setProperty("file",        slot.filePath,    nullptr);
                    s.setProperty("sampleRate",  slot.sampleRate,  nullptr);
                    s.setProperty("locked",      slot.locked,      nullptr);
                    t.appendChild(s, nullptr);
                }
                juce::Logger::writeToLog("[midi-binding] saved " + juce::String(static_cast<int>(slots.size())) + " slots");
            },
            // restore
            [this](const juce::ValueTree& t)
            {
                if (midiMapping_ == nullptr)
                    return;

                juce::AudioFormatManager fmtMgr;
                fmtMgr.registerBasicFormats();

                int restored = 0, failed = 0;
                for (int i = 0; i < t.getNumChildren(); ++i)
                {
                    auto s = t.getChild(i);

                    int note       = static_cast<int>(s.getProperty("note"));
                    juce::String p = s.getProperty("file").toString();
                    double sr      = static_cast<double>(s.getProperty("sampleRate"));
                    bool locked    = static_cast<bool>(s.getProperty("locked"));

                    if (note < 0 || note >= 128 || p.isEmpty())
                        continue;

                    juce::File f(p);
                    if (! f.existsAsFile())
                    {
                        juce::Logger::writeToLog("[midi-binding] file missing note="
                            + juce::String(note) + " path=" + p);
                        ++failed;
                        continue;
                    }

                    std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(f));
                    if (! reader)
                    {
                        juce::Logger::writeToLog("[midi-binding] unreadable note="
                            + juce::String(note) + " " + f.getFileName());
                        ++failed;
                        continue;
                    }

                    auto buffer = std::make_shared<juce::AudioBuffer<float>>(
                        static_cast<int>(reader->numChannels),
                        static_cast<int>(reader->lengthInSamples));
                    buffer->clear();
                    reader->read(buffer.get(), 0,
                                 static_cast<int>(reader->lengthInSamples),
                                 0, true, reader->numChannels > 1);

                    if (sr <= 0.0)
                        sr = reader->sampleRate;

                    midiMapping_->restoreSlot(note, f, std::move(buffer), sr, locked);
                    ++restored;
                }
                juce::Logger::writeToLog("[midi-binding] restored=" + juce::String(restored)
                    + " failed=" + juce::String(failed));
            }
        }));

    // variation binding removed (VariationService deleted in SAMPLAR fork)

    juce::Logger::writeToLog("[SamplarAudioProcessor] registerDefaultBindings END ("
        + juce::String(static_cast<int>(defaultBindings_.size())) + " bindings)");
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
