#include "App.h"
#include "SamplarVersion.h"
#include "ServiceRegistry.h"
#include "ServiceLocator.h"
#include "Service.h"
#include "AudioDeviceService.h"
#include "../utils/Tracing.h"
#include "../utils/FontWarmer.h"
// SAMPLAR fork: GeneratorService, VariationService, MentorRegistration, CustomCategoryManager removed
#include "../services/LicenseSystemService.h"
#include "../services/UpdateCheckService.h"
#include "../services/MidiMappingService.h"
#include "../services/smode/SModeService.h"
#include "../ui/MainWindow.h"
#include "../ui/licensing/LicenseActivationDialog.h"
#include "../utils/LogPaths.h"               // Writable log directory for Program Files installs

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#include <eh.h>
#endif

using namespace juce;

namespace samplar {

const juce::String App::getApplicationVersion() { return SAMPLAR_VERSION; }

#ifdef _WIN32
// Windows crash handler
LONG WINAPI crashHandler(EXCEPTION_POINTERS* exceptionInfo) {
    juce::Logger::writeToLog("[CRASH][CRASH][CRASH][CRASH] [CRASH] Windows exception caught!");
    juce::Logger::writeToLog("[ERROR] Exception code: 0x" + juce::String::toHexString((int)exceptionInfo->ExceptionRecord->ExceptionCode));
    juce::Logger::writeToLog("[ADDR] Exception address: 0x" + juce::String::toHexString((juce::pointer_sized_int)exceptionInfo->ExceptionRecord->ExceptionAddress));

    // Try to get a stack backtrace
    try {
        juce::String backtrace = juce::SystemStats::getStackBacktrace();
        if (backtrace.isNotEmpty()) {
            juce::Logger::writeToLog("[CRASH][CRASH][CRASH][CRASH] [CRASH] Stack backtrace:");
            juce::Logger::writeToLog(backtrace);
        } else {
            juce::Logger::writeToLog("[CRASH][CRASH][CRASH][CRASH] [CRASH] No stack backtrace available");
        }
    } catch (...) {
        juce::Logger::writeToLog("[CRASH][CRASH][CRASH][CRASH] [CRASH] Exception while getting backtrace");
    }

    juce::Logger::writeToLog("[CRASH][CRASH][CRASH][CRASH] [CRASH] Application will terminate");

    // Force flush the log
    if (auto* logger = juce::Logger::getCurrentLogger()) {
        delete logger;
        juce::Logger::setCurrentLogger(nullptr);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

void signalHandler(int signal) {
    juce::Logger::writeToLog("???????????? [CRASH] Signal caught: " + juce::String(signal));
    juce::Logger::writeToLog("???????????? [CRASH] Application will terminate");

    // Force flush the log
    if (auto* logger = juce::Logger::getCurrentLogger()) {
        delete logger;
        juce::Logger::setCurrentLogger(nullptr);
    }

    exit(signal);
}
#endif

class App::MainWindow : public samplar::MainWindow {
public:
    MainWindow() : samplar::MainWindow("SAMPLAR") {}
};

void App::initialise (const String& /*commandLine*/)
{
    // Early logging setup to catch crashes before UI
    // Use writable directory (%APPDATA% for Program Files installs, exe-dir for portable)
    juce::File logsDir = samplar::getLogsDirectory();
    juce::File logFile = logsDir.getChildFile("SAMPLAR_console.log");
    if (logFile.existsAsFile()) (void) logFile.deleteFile(); // fresh file each run
    earlyLogger_.reset(new juce::FileLogger(logFile, "[SAMPLAR] Early startup", 0));
    juce::Logger::setCurrentLogger(earlyLogger_.get());
    juce::Logger::writeToLog("[App] Logging initialized at: " + logFile.getFullPathName());

    // -- VST3 portable-pointer write -----------------------------
    // When the standalone runs from a folder that has python/, models/, or data/
    // siblings (i.e. an extracted portable bundle OR a Program Files install with
    // resources next to the exe), write the absolute folder path to
    // %APPDATA%\Samplar\portable_root.txt so the VST3 plugin can locate
    // these same resources after being copied to the system VST3 folder.
    //
    // No script involved � just a normal app writing a config file at startup.
    // This sidesteps the AV heuristics that flag installer .cmd files.
    {
        auto exeDir = juce::File::getSpecialLocation(
                          juce::File::currentExecutableFile)
                          .getParentDirectory();
        const bool hasResources = exeDir.getChildFile("python").isDirectory()
                               || exeDir.getChildFile("models").isDirectory()
                               || exeDir.getChildFile("data").isDirectory();
        if (hasResources)
        {
            auto pointerDir = juce::File::getSpecialLocation(
                                  juce::File::userApplicationDataDirectory)
                                  .getChildFile("Samplar");
            if (! pointerDir.exists())
                pointerDir.createDirectory();
            auto pointerFile = pointerDir.getChildFile("portable_root.txt");
            const auto absPath = exeDir.getFullPathName();
            const bool ok = pointerFile.replaceWithText(absPath);
            if (ok)
                juce::Logger::writeToLog("[App] Wrote VST3 portable pointer: "
                                         + pointerFile.getFullPathName()
                                         + " -> " + absPath);
            else
                juce::Logger::writeToLog("[App] WARNING: failed to write VST3 portable pointer at "
                                         + pointerFile.getFullPathName());
        }
        else
        {
            juce::Logger::writeToLog("[App] Exe has no portable siblings (python/models/data) "
                                     "- skipping VST3 pointer write");
        }
    }

    // B1 tracing � start the Chrome Trace JSON session (no-op in release builds
    // unless SAMPLAR_ENABLE_TRACING=ON was passed to CMake).
    SAMPLAR_TRACE_SESSION_START();

    // Kick off font warming immediately � before any editor is built � so the
    // DirectWrite cache is populated by the time the first Label::paint runs.
    samplar::FontWarmer::warmInBackground();

    juce::Logger::writeToLog("???? [App] initialise() ENTER");
    juce::Logger::writeToLog("???? [App] Working directory: " + juce::File::getCurrentWorkingDirectory().getFullPathName());

#ifdef _WIN32
    // Install crash handlers
    juce::Logger::writeToLog("???? [App] Installing Windows crash handlers");
    SetUnhandledExceptionFilter(crashHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGILL, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGTERM, signalHandler);
    juce::Logger::writeToLog("???? [App] Crash handlers installed");
#endif

    try {
        juce::Logger::writeToLog("???? [App] Creating ServiceRegistry");
        serviceRegistry = std::make_unique<samplar::ServiceRegistry>();
        juce::Logger::writeToLog("???? [App] ServiceRegistry created successfully");

        juce::Logger::writeToLog("???? [App] Registering LicenseSystemService");
        serviceRegistry->registerService("license", std::make_unique<samplar::LicenseSystemService>(eventBus_));
        juce::Logger::writeToLog("???? [App] LicenseSystemService registered successfully");
        juce::Logger::writeToLog("???? [App] Registering AudioDeviceService");
        serviceRegistry->registerService("audio", std::make_unique<samplar::AudioDeviceService>());
        juce::Logger::writeToLog("???? [App] AudioDeviceService registered successfully");


        // SAMPLAR fork: variation services and MentorService removed (stems-to-pads only)

        juce::Logger::writeToLog("[App] Registering UpdateCheckService");
        serviceRegistry->registerService("update_checker",
            std::make_unique<samplar::UpdateCheckService>(eventBus_));

        juce::Logger::writeToLog("[App] Registering MidiMappingService");
        serviceRegistry->registerService("midi.mapping",
            std::make_unique<samplar::MidiMappingService>(eventBus_));

        juce::Logger::writeToLog("[App] Registering SModeService");
        serviceRegistry->registerService("smode",
            std::make_unique<samplar::SModeService>(eventBus_));

        juce::Logger::writeToLog("?? [App] Initialising all services");

        serviceRegistry->initialiseAll();

        // Phase 0: Wire up ServiceLocator for VST3 compatibility
        juce::Logger::writeToLog("[App] Initialising ServiceLocator");
        ServiceLocator::initialise(serviceRegistry.get(), &eventBus_);
        juce::Logger::writeToLog("[App] ServiceLocator ready");

        // Standalone MIDI: connect MidiMappingService to AudioDeviceManager
        // and enable all detected MIDI input devices immediately so MIDI
        // works out of the box without the user opening Settings.
        {
            auto* audioSvc = dynamic_cast<samplar::AudioDeviceService*>(serviceRegistry->get("audio"));
            auto* midiSvc  = dynamic_cast<samplar::MidiMappingService*>(serviceRegistry->get("midi.mapping"));
            if (audioSvc && midiSvc)
            {
                auto& devMgr = audioSvc->deviceManager();
                // Licence gate on the standalone audio path + exports (N-18).
                if (auto* lic = dynamic_cast<samplar::LicenseSystemService*>(serviceRegistry->get("license")))
                    midiSvc->setAudioGate(&lic->audioAllowedFlag());
                midiSvc->connectToDeviceManager(&devMgr);
                juce::Logger::writeToLog("[App] MidiMappingService connected to AudioDeviceManager");

                auto midiDevices = juce::MidiInput::getAvailableDevices();
                for (const auto& dev : midiDevices)
                {
                    devMgr.setMidiInputDeviceEnabled(dev.identifier, true);
                    juce::Logger::writeToLog("[App] MIDI device enabled: " + dev.name);
                }

                if (midiDevices.isEmpty())
                    juce::Logger::writeToLog("[App] No MIDI input devices found at startup.");
            }
            else
            {
                juce::Logger::writeToLog("[App] WARNING: Could not wire MIDI � audioSvc="
                    + juce::String(audioSvc ? "OK" : "null")
                    + " midiSvc=" + juce::String(midiSvc ? "OK" : "null"));
            }
        }

        const auto runTestsFlag = juce::SystemStats::getEnvironmentVariable("SAMPLAR_RUN_TESTS", {});
        if (runTestsFlag.equalsIgnoreCase("1") || runTestsFlag.equalsIgnoreCase("true"))
        {
            juce::Logger::writeToLog("?? [App] SAMPLAR_RUN_TESTS detected; executing unit tests");
            juce::UnitTestRunner runner;
            runner.setAssertOnFailure(true);
            runner.runAllTests();
            ServiceLocator::requestQuit();
            return;
        }

        juce::Logger::writeToLog("?? [App] All services initialized successfully");

        // SAMPLAR fork: CustomCategoryManager pre-warm removed (no sample browser, no categories)

        if (auto* licenseService = dynamic_cast<samplar::LicenseSystemService*>(serviceRegistry->get("license"))) {

            if (licenseService->getCurrentStatus() != samplar::LicenseSystemService::LicenseStatus::Valid) {

                juce::Logger::writeToLog("?? [App] Launching license activation dialog");

                const int modalResult = samplar::LicenseActivationDialog::showModal(*licenseService, eventBus_);

                if (modalResult != 1 || licenseService->getCurrentStatus() != samplar::LicenseSystemService::LicenseStatus::Valid) {

                    juce::Logger::writeToLog("?? [App] License validation failed or cancelled; quitting");

                    ServiceLocator::requestQuit();

                    return;

                }

                juce::Logger::writeToLog("?? [App] License validated successfully");

            }

        } else {

            juce::Logger::writeToLog("?? [App] LicenseSystemService not found; quitting");

            ServiceLocator::requestQuit();

            return;

        }



        juce::Logger::writeToLog("?? [App] Creating MainWindow");

        mainWindow = std::make_unique<App::MainWindow>();

        juce::Logger::writeToLog("???? [App] initialise() SUCCESS");
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog("???? [App] EXCEPTION in initialise(): " + juce::String(e.what()));
        throw;
    }
    catch (...) {
        juce::Logger::writeToLog("???? [App] UNKNOWN EXCEPTION in initialise()");
        throw;
    }
}

void App::shutdown()
{
    juce::Logger::writeToLog("???? [App] shutdown() ENTER");

    try {
        juce::Logger::writeToLog("???? [App] Destroying MainWindow");
        mainWindow.reset();

        // Phase 0: Tear down ServiceLocator before services
        ServiceLocator::shutdown();

        if (serviceRegistry) {
            juce::Logger::writeToLog("???? [App] Shutting down all services");
            serviceRegistry->shutdownAll();
            serviceRegistry.reset();
        }

        juce::Logger::writeToLog("???? [App] shutdown() SUCCESS");

        // Clean up early logger
        // B1 tracing � flush + close the Chrome Trace JSON file before logger teardown.
        SAMPLAR_TRACE_SESSION_STOP();

        juce::Logger::setCurrentLogger(nullptr);
        earlyLogger_.reset();
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog("???? [App] EXCEPTION in shutdown(): " + juce::String(e.what()));
    }
    catch (...) {
        juce::Logger::writeToLog("???? [App] UNKNOWN EXCEPTION in shutdown()");
    }
}

} // namespace samplar

// This macro generates the main() routine that launches the app.
// Excluded from plugin builds (SAMPLARPlugin uses createPluginFilter() instead).
#if ! defined(JucePlugin_Build_VST3) && ! defined(JucePlugin_Build_Standalone)
START_JUCE_APPLICATION (samplar::App)
#endif

