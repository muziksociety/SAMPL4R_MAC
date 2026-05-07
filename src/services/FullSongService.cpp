#include "FullSongService.h"
#include "../utils/DataPaths.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <thread>
#include <cstdlib>

#if JUCE_WINDOWS
 #include <windows.h>
#endif


namespace samplar {

// Helper to quote a path if it contains spaces (for Windows command line)
static juce::String quotePath(const juce::String& path) {
    if (path.containsChar(' ') && !path.startsWithChar('"')) {
        return "\"" + path + "\"";
    }
    return path;
}

FullSongService::FullSongService(EventBus& bus) : bus_(bus) {
}


void FullSongService::initialise() {
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Initializing (UVR / audio-separator backend)...");
}

void FullSongService::shutdown() {
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Shutting down...");
}

FullSongService::Result FullSongService::process(const juce::File& input, const Config& cfg, const juce::String& tempPrefix) {
    Result res;

    if (!input.existsAsFile()) {
        res.error = "Input file not found";
        juce::Logger::writeToLog("[FAILED] [FullSongService] Input not found: " + input.getFullPathName());
        return res;
    }

    juce::Logger::writeToLog("[AUDIO] [FullSongService] Starting full song processing: " + input.getFileName());

    publishProgress(0, "Queued", cfg);
    publishProgress(5, "Checking UVR availability...", cfg);

    if (!isUvrAvailable()) {
        res.error = "UVR / audio-separator is not available. Ensure the portable Python environment is installed.";
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);
        return res;
    }

    publishProgress(10, "Separating drums (UVR)...", cfg);
    res = separateAudioUVR(input, cfg, tempPrefix);

    // If GPU failed, automatically retry on CPU
    if (res.error.contains("[CUDA_RETRY]"))
    {
        juce::Logger::writeToLog("[UVR] [FullSongService] GPU failed — retrying separation on CPU...");
        publishProgress(5, "GPU unavailable — switching to CPU (slower but reliable)...", cfg);
#ifdef _WIN32
        _putenv_s("CUDA_VISIBLE_DEVICES", "");
#else
        setenv("CUDA_VISIBLE_DEVICES", "", 1);
#endif
        res = separateAudioUVR(input, cfg, tempPrefix);
    }

    if (res.error.isNotEmpty()) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] UVR separation failed: " + res.error);
        return res;
    }

    juce::Logger::writeToLog("[OK] [FullSongService] UVR separation completed successfully");
    publishProgress(80, "Detecting drum samples...", cfg);

    // Step 2: Process the separated drum stem
    res = processDrumStem(res.drumStemFile, cfg, tempPrefix);
    if (res.error.isNotEmpty()) {
        return res;
    }

    publishProgress(100, "Complete", cfg);
    juce::Logger::writeToLog("[OK] [FullSongService] Full song processing complete: " + res.outputDir.getFullPathName());
    return res;
}

FullSongService::Result FullSongService::separateDrums(const juce::File& input, const Config& cfg, const juce::String& tempPrefix) {
    Result res;

    if (!input.existsAsFile()) {
        res.error = "Input file not found";
        juce::Logger::writeToLog("[FAILED] [FullSongService] Input not found: " + input.getFullPathName());
        return res;
    }

    juce::Logger::writeToLog("[AUDIO] [FullSongService] === SEPARATION ONLY (for WavePreviewPage) ===");
    juce::Logger::writeToLog("[AUDIO] [FullSongService] Input: " + input.getFileName());

    publishProgress(0, "Queued", cfg);
    publishProgress(5, "Checking UVR availability...", cfg);

    if (!isUvrAvailable()) {
        res.error = "UVR / audio-separator is not available. Ensure the portable Python environment is installed.";
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);
        return res;
    }

    publishProgress(10, "Separating drums (UVR)...", cfg);
    res = separateAudioUVR(input, cfg, tempPrefix);

    // If GPU failed, automatically retry on CPU
    if (res.error.contains("[CUDA_RETRY]"))
    {
        juce::Logger::writeToLog("[UVR] [FullSongService] GPU failed — retrying separation on CPU...");
        publishProgress(5, "GPU unavailable — switching to CPU (slower but reliable)...", cfg);
#ifdef _WIN32
        _putenv_s("CUDA_VISIBLE_DEVICES", "");
#else
        setenv("CUDA_VISIBLE_DEVICES", "", 1);
#endif
        res = separateAudioUVR(input, cfg, tempPrefix);
    }

    if (res.error.isEmpty()) {
        publishProgress(100, "Separation complete", cfg);
        juce::Logger::writeToLog("[OK] [FullSongService] Separation complete: " + res.drumStemFile.getFullPathName());
    } else {
        juce::Logger::writeToLog("[FAILED] [FullSongService] Separation failed: " + res.error);
    }
    return res;
}

FullSongService::Result FullSongService::processDrumStem(const juce::File& drumStemFile, const Config& cfg, const juce::String& tempPrefix) {
    Result res;

    juce::Logger::writeToLog("[DRUM] [FullSongService] Processing drum stem: " + drumStemFile.getFileName());

    // SAMPLAR fork: DrumSliceService removed. Stems-to-pads workflow returns the
    // drum stem file directly; S-mode places it in a slot rather than slicing.
    // tempPrefix kept in signature for API compatibility but unused here.
    juce::ignoreUnused(tempPrefix);

    res.drumStemFile = drumStemFile;
    res.outputDir = drumStemFile.getParentDirectory();
    res.sampleFiles.add(drumStemFile);

    juce::Logger::writeToLog("[OK] [FullSongService] Drum stem returned for S-mode slot: "
                             + drumStemFile.getFileName());

    return res;
}

void FullSongService::publishProgress(int pct, const juce::String& msg, const Config& cfg) {
    if (cfg.onProgress) {
        cfg.onProgress(pct, msg);
    }
    juce::Logger::writeToLog(juce::String::formatted("[FullSongService] %d%% - %s", pct, msg.toRawUTF8()));
}

void FullSongService::publishError(const juce::String& error, const Config& cfg) {
    juce::Logger::writeToLog("[FAILED] [FullSongService] Error: " + error);
}

bool FullSongService::loadAudioFile(const juce::File& file, juce::AudioBuffer<float>& buffer, double& sampleRate) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] Failed to create reader for: " + file.getFileName());
        return false;
    }

    sampleRate = reader->sampleRate;
    int numChannels = static_cast<int>(reader->numChannels);
    int numSamples = static_cast<int>(reader->lengthInSamples);

    buffer.setSize(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    juce::Logger::writeToLog(juce::String::formatted("[FOLDER] [FullSongService] Loaded audio: %d channels, %d samples, %.1f Hz",
                                                    numChannels, numSamples, sampleRate));
    return true;
}

bool FullSongService::saveAudioFile(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto* wavFormat = formatManager.findFormatForFileExtension("wav");
    if (!wavFormat) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] WAV format not available");
        return false;
    }

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> fileStream(file.createOutputStream());
    if (!fileStream) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] Failed to create output stream for: " + file.getFileName());
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat->createWriterFor(fileStream.get(), sampleRate,
                                                                              buffer.getNumChannels(), 16, {}, 0));
    if (!writer) {
        juce::Logger::writeToLog("[FAILED] [FullSongService] Failed to create WAV writer");
        return false;
    }

    fileStream.release(); // Writer takes ownership

    bool success = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    if (success) {
        juce::Logger::writeToLog("[SAVE] [FullSongService] Saved audio file: " + file.getFileName());
    } else {
        juce::Logger::writeToLog("[FAILED] [FullSongService] Failed to write audio file: " + file.getFileName());
    }

    return success;
}


juce::String FullSongService::getAudioSeparatorPath() {
    // First, check for portable installation relative to app directory
    juce::File appDir = samplar::DataPaths::getAppDirectory();

    // Check for portable audio-separator.exe
    juce::File portablePath = samplar::DataPaths::getPythonDirectory().getChildFile("Scripts").getChildFile("audio-separator.exe");

    if (portablePath.existsAsFile()) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Found portable audio-separator: " + portablePath.getFullPathName());
        return portablePath.getFullPathName();
    }

    // Also check SAMPLAR_portable.json to confirm we're in portable mode
    juce::File portableConfig = appDir.getChildFile("SAMPLAR_portable.json");
    if (portableConfig.existsAsFile()) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Portable config found but audio-separator missing at: " + portablePath.getFullPathName());
    }

    // Fall back to PATH (for development or system-installed version)
    juce::Logger::writeToLog("[UVR] [FullSongService] Using audio-separator from PATH");
    return "audio-separator";
}

// Helper: Set up Python environment for portable mode
void FullSongService::setupPortablePythonEnv() {
    // Guard: only run the filesystem checks and env setup once per process lifetime
    static bool envSetupDone = false;
    if (envSetupDone) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Portable env already configured (skipping)");
        return;
    }

    juce::File exeDir = samplar::DataPaths::getAppDirectory();
    juce::File pythonDir = samplar::DataPaths::getPythonDirectory();
    juce::File portableConfig = exeDir.getChildFile("SAMPLAR_portable.json");

    juce::Logger::writeToLog("[UVR] [FullSongService] === PORTABLE PYTHON ENV SETUP ===");
    juce::Logger::writeToLog("[UVR] [FullSongService] App directory: " + exeDir.getFullPathName());
    juce::Logger::writeToLog("[UVR] [FullSongService] Python directory: " + pythonDir.getFullPathName());
    juce::Logger::writeToLog("[UVR] [FullSongService] Python dir exists: " + juce::String(pythonDir.isDirectory() ? "YES" : "NO"));
    juce::Logger::writeToLog("[UVR] [FullSongService] Config file: " + portableConfig.getFullPathName());
    juce::Logger::writeToLog("[UVR] [FullSongService] Config exists: " + juce::String(portableConfig.existsAsFile() ? "YES" : "NO"));

    // Only set up if we're in portable mode (python folder exists)
    if (pythonDir.isDirectory() && portableConfig.existsAsFile()) {
        juce::String pythonPath = pythonDir.getFullPathName();
        juce::String scriptsPath = pythonDir.getChildFile("Scripts").getFullPathName();

        // Check critical files exist
        juce::File pythonExe = pythonDir.getChildFile("python.exe");
        juce::File audioSepExe = pythonDir.getChildFile("Scripts").getChildFile("audio-separator.exe");
        juce::Logger::writeToLog("[UVR] [FullSongService] python.exe exists: " + juce::String(pythonExe.existsAsFile() ? "YES" : "NO"));
        juce::Logger::writeToLog("[UVR] [FullSongService] audio-separator.exe exists: " + juce::String(audioSepExe.existsAsFile() ? "YES" : "NO"));

        // Set PYTHONHOME to the portable python directory
        juce::String currentPythonHome = juce::SystemStats::getEnvironmentVariable("PYTHONHOME", "");
        juce::Logger::writeToLog("[UVR] [FullSongService] Current PYTHONHOME: " + (currentPythonHome.isEmpty() ? "(empty)" : currentPythonHome));

        if (currentPythonHome.isEmpty() || !currentPythonHome.containsIgnoreCase(pythonPath)) {
#ifdef _WIN32
            _putenv_s("PYTHONHOME", pythonPath.toRawUTF8());
            _putenv_s("PYTHONPATH", "");
            // Prevent Python from writing .pyc files (would fail in Program Files)
            _putenv_s("PYTHONDONTWRITEBYTECODE", "1");

            // CRITICAL: Set Numba cache directory to a writable location
            // Without this, Numba hangs trying to write JIT cache to write-protected Program Files
            juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
            juce::File numbaCacheDir = tempDir.getChildFile("Samplar_numba_cache");
            if (!numbaCacheDir.isDirectory()) {
                numbaCacheDir.createDirectory();
            }
            _putenv_s("NUMBA_CACHE_DIR", numbaCacheDir.getFullPathName().toRawUTF8());
            juce::Logger::writeToLog("[UVR] [FullSongService] Set NUMBA_CACHE_DIR=" + numbaCacheDir.getFullPathName());

            // CRITICAL: Force unbuffered output so pipe captures Python's output in real-time
            // Without this, Python fully buffers stdout/stderr when connected to a pipe
            // and no output reaches JUCE's readProcessOutput() until the buffer fills or process exits
            _putenv_s("PYTHONUNBUFFERED", "1");
#else
            setenv("PYTHONHOME", pythonPath.toRawUTF8(), 1);
            setenv("PYTHONPATH", "", 1);
            setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
            setenv("PYTHONUNBUFFERED", "1", 1);

            // Set Numba cache directory to writable location
            juce::File tempDirUnix = juce::File::getSpecialLocation(juce::File::tempDirectory);
            juce::File numbaCacheDirUnix = tempDirUnix.getChildFile("Samplar_numba_cache");
            if (!numbaCacheDirUnix.isDirectory()) {
                numbaCacheDirUnix.createDirectory();
            }
            setenv("NUMBA_CACHE_DIR", numbaCacheDirUnix.getFullPathName().toRawUTF8(), 1);
#endif
            juce::Logger::writeToLog("[UVR] [FullSongService] Set PYTHONHOME=" + pythonPath);
            juce::Logger::writeToLog("[UVR] [FullSongService] Set PYTHONDONTWRITEBYTECODE=1 (avoid write to Program Files)");
            juce::Logger::writeToLog("[UVR] [FullSongService] Set NUMBA_CACHE_DIR to temp directory");
        } else {
            juce::Logger::writeToLog("[UVR] [FullSongService] PYTHONHOME already set correctly");
        }

        // Always ensure unbuffered output (may not have been set if PYTHONHOME was already correct)
#ifdef _WIN32
        _putenv_s("PYTHONUNBUFFERED", "1");
#else
        setenv("PYTHONUNBUFFERED", "1", 1);
#endif

        // Prepend python and Scripts to PATH
        juce::String currentPath = juce::SystemStats::getEnvironmentVariable("PATH", "");
        if (!currentPath.startsWithIgnoreCase(pythonPath)) {
            juce::String newPath = pythonPath + ";" + scriptsPath + ";" + currentPath;
#ifdef _WIN32
            _putenv_s("PATH", newPath.toRawUTF8());
#else
            setenv("PATH", newPath.toRawUTF8(), 1);
#endif
            juce::Logger::writeToLog("[UVR] [FullSongService] Prepended Python to PATH");
            juce::Logger::writeToLog("[UVR] [FullSongService] New PATH starts with: " + newPath.substring(0, juce::jmin(200, newPath.length())));
        } else {
            juce::Logger::writeToLog("[UVR] [FullSongService] PATH already includes Python directory");
        }

        // Set up model cache directory - use AppData if in Program Files (write-protected)
        juce::String modelsPath;
        bool inProgramFiles = exeDir.getFullPathName().containsIgnoreCase("Program Files");

        if (inProgramFiles) {
            // Use AppData for models when installed in Program Files
            juce::File modelsDir = samplar::DataPaths::getUserDataDirectory().getChildFile("models");

            // Create the directory if it doesn't exist
            if (!modelsDir.isDirectory()) {
                modelsDir.createDirectory();
                juce::Logger::writeToLog("[UVR] [FullSongService] Created AppData models directory: " + modelsDir.getFullPathName());
            }

            modelsPath = modelsDir.getFullPathName();
            juce::Logger::writeToLog("[UVR] [FullSongService] Using AppData for models (Program Files is write-protected)");
        } else {
            // Use local models folder for portable installations
            modelsPath = samplar::DataPaths::getModelsDirectory().getFullPathName();
            juce::Logger::writeToLog("[UVR] [FullSongService] Using local models folder (portable mode)");
        }

        juce::Logger::writeToLog("[UVR] [FullSongService] Models path: " + modelsPath);

        // Set model cache environment variables
#ifdef _WIN32
        _putenv_s("TORCH_HOME", modelsPath.toRawUTF8());
        _putenv_s("XDG_CACHE_HOME", modelsPath.toRawUTF8());
        _putenv_s("HF_HOME", (modelsPath + "\\huggingface").toRawUTF8());
        _putenv_s("HUGGINGFACE_HUB_CACHE", (modelsPath + "\\huggingface").toRawUTF8());

        // (FlashAttention disable moved outside portable-mode check — see end of function)
#else
        setenv("TORCH_HOME", modelsPath.toRawUTF8(), 1);
        setenv("XDG_CACHE_HOME", modelsPath.toRawUTF8(), 1);
        setenv("HF_HOME", (modelsPath + "/huggingface").toRawUTF8(), 1);
        setenv("HUGGINGFACE_HUB_CACHE", (modelsPath + "/huggingface").toRawUTF8(), 1);
#endif
        juce::Logger::writeToLog("[UVR] [FullSongService] Set TORCH_HOME=" + modelsPath);
        juce::Logger::writeToLog("[UVR] [FullSongService] Set HF_HOME=" + modelsPath + "/huggingface");

        juce::Logger::writeToLog("[UVR] [FullSongService] === PORTABLE SETUP COMPLETE ===");
    } else {
        juce::Logger::writeToLog("[UVR] [FullSongService] Not in portable mode - skipping Python env setup");
    }

    // ALWAYS disable FlashAttention regardless of portable mode.
    // FlashAttention CUTLASS kernels only support sm80-sm100 and crash on
    // Blackwell (sm120+) and older GPUs. The math-based SDP backend works
    // on ALL architectures with minimal speed difference.
#ifdef _WIN32
    _putenv_s("TORCH_CUDNN_V8_API_DISABLED", "1");
#else
    setenv("TORCH_CUDNN_V8_API_DISABLED", "1", 1);
#endif
    juce::Logger::writeToLog("[UVR] [FullSongService] Disabled FlashAttention for GPU compatibility (always)");

    // CPU inference performance: set thread counts to physical core count
    // Without these, OpenMP/MKL may over-subscribe or under-utilize cores
    auto physicalCores = juce::String(juce::SystemStats::getNumCpus());
#ifdef _WIN32
    _putenv_s("OMP_NUM_THREADS", physicalCores.toRawUTF8());
    _putenv_s("MKL_NUM_THREADS", physicalCores.toRawUTF8());
#else
    setenv("OMP_NUM_THREADS", physicalCores.toRawUTF8(), 1);
    setenv("MKL_NUM_THREADS", physicalCores.toRawUTF8(), 1);
#endif
    juce::Logger::writeToLog("[UVR] [FullSongService] Set OMP/MKL_NUM_THREADS=" + physicalCores);

    envSetupDone = true;
}

bool FullSongService::isUvrAvailable() {
    // Cache the result — spawning a Python process just for --version costs 5-15 seconds
    static bool checkedOnce = false;
    static bool cachedResult = false;
    if (checkedOnce) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Using cached availability: " + juce::String(cachedResult ? "YES" : "NO"));
        return cachedResult;
    }

    // Check if 'audio-separator' command exists (portable or PATH)
    juce::Logger::writeToLog("[UVR] [FullSongService] Checking if audio-separator is available...");

    // Set up portable Python environment if needed
    setupPortablePythonEnv();

    juce::File exeDir = samplar::DataPaths::getAppDirectory();
    juce::File pythonDir = samplar::DataPaths::getPythonDirectory();
    juce::File pythonExe = pythonDir.getChildFile("python.exe");
    juce::File portableConfig = exeDir.getChildFile("SAMPLAR_portable.json");
    bool inProgramFiles = exeDir.getFullPathName().containsIgnoreCase("Program Files");

    juce::ChildProcess test;
    juce::StringArray args;

    // For portable mode: use "python.exe -m audio_separator --version"
    // This is more reliable than audio-separator.exe because python.exe can find its DLLs
    if (pythonExe.existsAsFile() && portableConfig.existsAsFile()) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Using portable Python: " + pythonExe.getFullPathName());
        // NOTE: Do NOT quote paths when using juce::StringArray - JUCE handles escaping internally
        // Adding quotes causes double-quoting which breaks the command
        args.add(pythonExe.getFullPathName());
        args.add("-m");
        args.add("audio_separator");
        args.add("--version");
    } else {
        // Fall back to audio-separator command (system-installed)
        juce::String audioSepPath = getAudioSeparatorPath();
        juce::Logger::writeToLog("[UVR] [FullSongService] Using system audio-separator: " + audioSepPath);
        args.add(audioSepPath);
        args.add("--version");
    }

    juce::Logger::writeToLog("[UVR] [FullSongService] Running: " + args.joinIntoString(" "));

    // Start the process with proper working directory
    // Change CWD temporarily so the child process inherits a writable directory
    bool started = false;

#ifdef _WIN32
    juce::String savedCwd;
    if (inProgramFiles) {
        juce::File modelsDir = samplar::DataPaths::getUserDataDirectory().getChildFile("models");
        if (!modelsDir.isDirectory()) {
            modelsDir.createDirectory();
        }

        wchar_t cwdBuf[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwdBuf);
        savedCwd = juce::String(cwdBuf);
        SetCurrentDirectoryW(modelsDir.getFullPathName().toWideCharPointer());
        juce::Logger::writeToLog("[UVR] [FullSongService] Changed CWD to: " + modelsDir.getFullPathName());
    }

    juce::Logger::writeToLog("[UVR] [FullSongService] Launching directly: " + args.joinIntoString(" "));
    started = test.start(args);

    // Capture Windows error code IMMEDIATELY after start() fails (before any other API calls)
    DWORD lastError = 0;
    if (!started) {
        lastError = ::GetLastError();
    }

    if (savedCwd.isNotEmpty()) {
        SetCurrentDirectoryW(savedCwd.toWideCharPointer());
        juce::Logger::writeToLog("[UVR] [FullSongService] Restored CWD to: " + savedCwd);
    }
#else
    started = test.start(args);
#endif

    if (!started) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Failed to start audio-separator process");
#ifdef _WIN32
        // Log detailed Windows error information for debugging
        juce::Logger::writeToLog("[UVR] [FullSongService] Windows LastError code: " + juce::String((int)lastError));

        // Translate error code to message
        LPWSTR messageBuffer = nullptr;
        size_t size = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&messageBuffer, 0, NULL);
        if (size > 0 && messageBuffer != nullptr) {
            juce::String errorMsg(messageBuffer);
            juce::Logger::writeToLog("[UVR] [FullSongService] Windows error message: " + errorMsg.trim());
            LocalFree(messageBuffer);
        }

        // Log additional diagnostic info
        juce::Logger::writeToLog("[UVR] [FullSongService] Executable path: " + args[0]);
        juce::File exeFile(args[0]);
        juce::Logger::writeToLog("[UVR] [FullSongService] Executable exists: " + juce::String(exeFile.existsAsFile() ? "YES" : "NO"));
        if (exeFile.existsAsFile()) {
            juce::Logger::writeToLog("[UVR] [FullSongService] Executable size: " + juce::String(exeFile.getSize()) + " bytes");
        }

        // Check for critical Python DLLs
        juce::File pythonDir = exeFile.getParentDirectory();
        juce::File python311Dll = pythonDir.getChildFile("python311.dll");
        juce::File vcruntime140Dll = pythonDir.getChildFile("vcruntime140.dll");
        juce::Logger::writeToLog("[UVR] [FullSongService] python311.dll exists: " + juce::String(python311Dll.existsAsFile() ? "YES" : "NO"));
        juce::Logger::writeToLog("[UVR] [FullSongService] vcruntime140.dll exists: " + juce::String(vcruntime140Dll.existsAsFile() ? "YES" : "NO"));
#endif
        checkedOnce = true;
        cachedResult = false;
        return false;
    }

    // Wait for process to complete (up to 10 seconds - first run may download models)
    bool finished = test.waitForProcessToFinish(10000);
    if (!finished) {
        juce::Logger::writeToLog("[UVR] [FullSongService] audio-separator timed out during version check");
        test.kill();
        checkedOnce = true;
        cachedResult = false;
        return false;
    }

    // Read output to verify
    char buffer[1024];
    juce::String output;
    while (int bytesRead = test.readProcessOutput(buffer, sizeof(buffer) - 1)) {
        buffer[bytesRead] = '\0';
        output += buffer;
        if (bytesRead < (int)sizeof(buffer) - 1) break;
    }

    // Get actual exit code (IMPORTANT: waitForProcessToFinish returns bool, not exit code!)
    juce::uint32 exitCode = test.getExitCode();

    bool available = (exitCode == 0);
    if (available) {
        juce::Logger::writeToLog("[UVR] [FullSongService] audio-separator is available!");
        if (output.isNotEmpty()) {
            juce::Logger::writeToLog("[UVR] [FullSongService] Version: " + output.trim());
        }
    } else {
        juce::Logger::writeToLog("[UVR] [FullSongService] audio-separator check failed with exit code: " + juce::String(exitCode));
        juce::Logger::writeToLog("[UVR] [FullSongService] Output: " + output.trim());
    }

    checkedOnce = true;
    cachedResult = available;
    return available;
}

FullSongService::Result FullSongService::separateAudioUVR(const juce::File& input, const Config& cfg, const juce::String& tempPrefix) {
    Result res;

    // Tier 2: Stem cache — skip separation if we already have stems for this exact input + model
    // Cache key: filename + file size + last modified time + model name + drums-only flag
    {
        auto cacheDir = samplar::DataPaths::getUserDataDirectory().getChildFile("stem_cache");
        auto cacheKey = input.getFileName()
            + "_" + juce::String(input.getSize())
            + "_" + juce::String(input.getLastModificationTime().toMilliseconds())
            + "_" + cfg.uvrModelName
            + "_" + (cfg.uvrDrumsOnly ? "drums" : "all");
        auto cacheHash = juce::String::toHexString(cacheKey.hashCode64());
        auto cachedDir = cacheDir.getChildFile(cacheHash);

        if (cachedDir.isDirectory())
        {
            // Check if cached stems still exist
            juce::Array<juce::File> cachedFiles;
            cachedDir.findChildFiles(cachedFiles, juce::File::findFiles, false, "*.wav");
            cachedDir.findChildFiles(cachedFiles, juce::File::findFiles, false, "*.flac");

            if (cachedFiles.size() > 0)
            {
                juce::Logger::writeToLog("[UVR] [FullSongService] CACHE HIT: " + cacheHash.substring(0, 12)
                    + " (" + juce::String(cachedFiles.size()) + " stems)");
                res.outputDir = cachedDir;
                // Find drums stem if present
                for (const auto& f : cachedFiles)
                {
                    if (f.getFileNameWithoutExtension().containsIgnoreCase("drum"))
                        res.drumStemFile = f;
                }
                if (res.drumStemFile == juce::File() && cachedFiles.size() > 0)
                    res.drumStemFile = cachedFiles[0];
                return res;
            }
        }
    }

    // Ensure Python environment is set up for portable mode
    setupPortablePythonEnv();

    juce::Logger::writeToLog("========================================");
    juce::Logger::writeToLog("[UVR] UVR/AUDIO-SEPARATOR BACKEND SELECTED");
    juce::Logger::writeToLog("[UVR] Model: " + cfg.uvrModelName);
    juce::Logger::writeToLog("[UVR] Output format: " + cfg.uvrOutputFormat);
    juce::Logger::writeToLog("[UVR] Drums only: " + juce::String(cfg.uvrDrumsOnly ? "YES" : "NO"));
    juce::Logger::writeToLog("[UVR] Expected speed: ~2-3 min for 5-min song (FASTEST)");
    juce::Logger::writeToLog("========================================");

    // Create temporary directory for output
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile(tempPrefix + juce::String(juce::Random::getSystemRandom().nextInt64()));
    tempDir.createDirectory();
    res.outputDir = tempDir;

    // PROGRESS FILE INTEGRATION: Create progress file for real-time UI updates
    juce::File progressFile = tempDir.getChildFile("progress.json");
    juce::Logger::writeToLog("[UVR] [FullSongService] Progress file: " + progressFile.getFullPathName());

    juce::Logger::writeToLog("[UVR] [FullSongService] Output directory: " + tempDir.getFullPathName());

    // Check for portable mode - use python.exe -m audio_separator instead of audio-separator.exe
    juce::File exeDir = samplar::DataPaths::getAppDirectory();
    juce::File pythonDir = samplar::DataPaths::getPythonDirectory();
    juce::File pythonExe = pythonDir.getChildFile("python.exe");
    juce::File portableConfig = exeDir.getChildFile("SAMPLAR_portable.json");
    bool isPortableMode = pythonExe.existsAsFile() && portableConfig.existsAsFile();

    // Build CLI arguments
    juce::StringArray args;

    if (isPortableMode) {
        // Portable mode: use python.exe -m audio_separator
        // This is more reliable because python.exe can find its DLLs
        juce::Logger::writeToLog("[UVR] [FullSongService] Portable mode: using python.exe -m audio_separator");
        // NOTE: Do NOT quote paths when using juce::StringArray - JUCE handles escaping internally
        // Adding quotes causes double-quoting which breaks the command on Windows
        args.add(pythonExe.getFullPathName());
        args.add("-m");
        args.add("audio_separator");
    } else {
        // System mode: use audio-separator command
        juce::String audioSepPath = getAudioSeparatorPath();
        juce::Logger::writeToLog("[UVR] [FullSongService] System mode: using " + audioSepPath);
        args.add(audioSepPath);
    }

    // Add common arguments - JUCE StringArray handles path escaping automatically
    args.add(input.getFullPathName());
    args.add("--model_filename");
    args.add(cfg.uvrModelName);
    args.add("--output_dir");
    args.add(tempDir.getFullPathName());
    args.add("--output_format");
    args.add(cfg.uvrOutputFormat);

    if (cfg.uvrDrumsOnly) {
        args.add("--single_stem");
        args.add("Drums");
    }

    // Performance optimizations (Tier 1+2 — matches UVR desktop defaults)
    // NOTE: audio-separator CLI uses model-prefixed flags for demucs (--demucs_*)
    args.add("--use_autocast");  // FP16 mixed precision: ~30% faster GPU, ~50% less VRAM

    args.add("--demucs_shifts");
    args.add("0");  // Skip random shift averaging: ~50% faster at minimal quality cost

    // NOTE: --demucs_segment_size left at default (auto). Larger values (60+) cause
    // CUDA TDR timeouts on GPUs with <8GB VRAM. Let audio-separator pick the safe default.

    // NOTE: --progress-file is NOT supported by audio-separator CLI
    // Progress will be estimated based on process runtime instead

    // Determine models directory.
    //
    // PERMANENT FIX (users reported "htdemucs_*.yaml not found" on installed
    // builds): always PREFER the bundled {app}\models directory when it
    // already contains the requested model yaml. The previous logic blindly
    // forced Program Files installs to use an %APPDATA%\...\models dir that
    // the installer only partially seeded (htdemucs_ft.yaml was copied, but
    // htdemucs_6s.yaml / htdemucs.yaml were NOT), so the subprocess would be
    // pointed at a directory that did not contain the requested model and
    // fail with "model not found".
    //
    // New resolution order:
    //   1. Bundled  {app}\models     — if it contains the requested yaml
    //   2. AppData  %APPDATA%\...    — fallback / writable download cache
    //   3. Bundled  {app}\models     — last-resort default (createDirectory)
    //
    // audio-separator only needs write access to the model dir when it has
    // to download a new model. Bundled installs already ship every model we
    // use, so a read-only Program Files path is completely fine.
    juce::String modelsPath;
    bool inProgramFiles = exeDir.getFullPathName().containsIgnoreCase("Program Files");

    juce::File bundledModelsDir = samplar::DataPaths::getModelsDirectory();         // {app}\models
    juce::File appDataModelsDir = samplar::DataPaths::getUserDataDirectory()
                                      .getChildFile("models");                  // %APPDATA%\...\models

    const juce::File bundledYaml  = bundledModelsDir.getChildFile(cfg.uvrModelName);
    const juce::File appDataYaml  = appDataModelsDir.getChildFile(cfg.uvrModelName);

    juce::Logger::writeToLog("[UVR] [FullSongService] Looking for model: " + cfg.uvrModelName);
    juce::Logger::writeToLog("[UVR] [FullSongService]   bundled candidate: " + bundledYaml.getFullPathName()
                             + " exists=" + juce::String(bundledYaml.existsAsFile() ? "YES" : "NO"));
    juce::Logger::writeToLog("[UVR] [FullSongService]   appdata candidate: " + appDataYaml.getFullPathName()
                             + " exists=" + juce::String(appDataYaml.existsAsFile() ? "YES" : "NO"));

    if (bundledYaml.existsAsFile()) {
        modelsPath = bundledModelsDir.getFullPathName();
        juce::Logger::writeToLog("[UVR] [FullSongService] Using BUNDLED models directory (model found there): " + modelsPath);
    } else if (appDataYaml.existsAsFile()) {
        modelsPath = appDataModelsDir.getFullPathName();
        juce::Logger::writeToLog("[UVR] [FullSongService] Using APPDATA models directory (bundled missing the yaml): " + modelsPath);
    } else {
        // Model isn't present in either location — pick a writable dir so the
        // subprocess can at least try to download it. Prefer AppData in
        // Program Files installs (write-protected), otherwise bundled.
        juce::File fallback = inProgramFiles ? appDataModelsDir : bundledModelsDir;
        if (!fallback.isDirectory())
            fallback.createDirectory();
        modelsPath = fallback.getFullPathName();
        juce::Logger::writeToLog("[UVR] [FullSongService] ⚠️  Model not present in either location — using writable fallback: " + modelsPath);
    }

    // Add model directory argument - JUCE handles path escaping automatically
    if (modelsPath.isNotEmpty()) {
        args.add("--model_file_dir");
        args.add(modelsPath);
    }

    juce::Logger::writeToLog("[UVR] [FullSongService] Command: " + args.joinIntoString(" "));

    // === Determine log file for subprocess output ===
    // Write all subprocess stdout+stderr to a file so we can diagnose crashes
    // even when the pipe fails to capture output (common with cmd.exe wrappers)
    juce::File uvrLogFile = samplar::DataPaths::getUserDataDirectory().getChildFile("logs")
                                       .getChildFile("uvr_subprocess.log");
    juce::Logger::writeToLog("[UVR] [FullSongService] Subprocess log: " + uvrLogFile.getFullPathName());

    // Start the process with proper working directory
    // CRITICAL: We use CreateProcess directly instead of JUCE ChildProcess when in
    // Program Files, because JUCE's ChildProcess doesn't support setting lpCurrentDirectory.
    // The cmd.exe /c workaround kills stdout/stderr piping (no output reaches the reader).
    juce::ChildProcess p;
    bool started = false;

#ifdef _WIN32
    // Change the process-level CWD before launching, then restore it.
    // This is simpler and more reliable than cmd.exe /c or CreateProcess hacks
    // because JUCE ChildProcess inherits the parent's CWD.
    juce::String savedCwd;
    if (modelsPath.isNotEmpty()) {
        // Save current CWD
        wchar_t cwdBuf[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwdBuf);
        savedCwd = juce::String(cwdBuf);

        // Change to models directory before launching so that any relative
        // path lookups inside audio-separator resolve against the dir that
        // actually contains the model files. Works for both bundled
        // (Program Files) and portable installs.
        SetCurrentDirectoryW(modelsPath.toWideCharPointer());
        juce::Logger::writeToLog("[UVR] [FullSongService] Changed CWD to: " + modelsPath);
    }

    // Launch directly with StringArray — no cmd.exe wrapper needed!
    // JUCE ChildProcess captures stdout+stderr through pipes correctly this way
    juce::Logger::writeToLog("[UVR] [FullSongService] Launching directly: " + args.joinIntoString(" "));

    // Log critical environment state for debugging install vs portable differences
    juce::Logger::writeToLog("[UVR] [FullSongService] === ENVIRONMENT DIAGNOSTICS ===");
    juce::Logger::writeToLog("[UVR] [FullSongService] inProgramFiles: " + juce::String(inProgramFiles ? "YES" : "NO"));
    juce::Logger::writeToLog("[UVR] [FullSongService] isPortableMode: " + juce::String(isPortableMode ? "YES" : "NO"));
    juce::Logger::writeToLog("[UVR] [FullSongService] modelsPath: " + modelsPath);

    // Check model files in expected location
    juce::File modelsDir(modelsPath);
    juce::Logger::writeToLog("[UVR] [FullSongService] modelsDir exists: " + juce::String(modelsDir.isDirectory() ? "YES" : "NO"));
    if (modelsDir.isDirectory()) {
        juce::Array<juce::File> modelFiles;
        modelsDir.findChildFiles(modelFiles, juce::File::findFiles, false);
        juce::Logger::writeToLog("[UVR] [FullSongService] Model files found: " + juce::String(modelFiles.size()));
        for (int i = 0; i < juce::jmin(5, modelFiles.size()); i++) {
            juce::Logger::writeToLog("[UVR] [FullSongService]   - " + modelFiles[i].getFileName() + " (" + juce::String(modelFiles[i].getSize()) + " bytes)");
        }
    }

    // Log current environment variables
    char* pythonHome = getenv("PYTHONHOME");
    char* torchHome = getenv("TORCH_HOME");
    char* hfHome = getenv("HF_HOME");
    juce::Logger::writeToLog("[UVR] [FullSongService] PYTHONHOME: " + juce::String(pythonHome ? pythonHome : "(not set)"));
    juce::Logger::writeToLog("[UVR] [FullSongService] TORCH_HOME: " + juce::String(torchHome ? torchHome : "(not set)"));
    juce::Logger::writeToLog("[UVR] [FullSongService] HF_HOME: " + juce::String(hfHome ? hfHome : "(not set)"));

    // Check Python DLLs exist
    juce::File python311Dll = pythonDir.getChildFile("python311.dll");
    juce::File python3Dll = pythonDir.getChildFile("python3.dll");
    juce::Logger::writeToLog("[UVR] [FullSongService] python311.dll: " + juce::String(python311Dll.existsAsFile() ? "EXISTS" : "MISSING"));
    juce::Logger::writeToLog("[UVR] [FullSongService] python3.dll: " + juce::String(python3Dll.existsAsFile() ? "EXISTS" : "MISSING"));
    juce::Logger::writeToLog("[UVR] [FullSongService] === END ENVIRONMENT DIAGNOSTICS ===");

    started = p.start(args);

    // Capture Windows error code IMMEDIATELY after start() fails
    DWORD lastErrorSep = 0;
    if (!started) {
        lastErrorSep = ::GetLastError();
    }

    // Restore original CWD
    if (savedCwd.isNotEmpty()) {
        SetCurrentDirectoryW(savedCwd.toWideCharPointer());
        juce::Logger::writeToLog("[UVR] [FullSongService] Restored CWD to: " + savedCwd);
    }
#else
    started = p.start(args);
#endif

    if (!started) {
        res.error = "Failed to start audio-separator CLI";
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);
#ifdef _WIN32
        // Log detailed Windows error information for debugging
        juce::Logger::writeToLog("[FAILED] [FullSongService] Windows LastError code: " + juce::String((int)lastErrorSep));

        // Translate error code to message
        LPWSTR messageBuffer = nullptr;
        size_t size = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, lastErrorSep, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&messageBuffer, 0, NULL);
        if (size > 0 && messageBuffer != nullptr) {
            juce::String errorMsg(messageBuffer);
            juce::Logger::writeToLog("[FAILED] [FullSongService] Windows error message: " + errorMsg.trim());
            LocalFree(messageBuffer);
        }

        // Log additional diagnostic info
        juce::Logger::writeToLog("[FAILED] [FullSongService] Executable path: " + args[0]);
        juce::File exeFileSep(args[0]);
        juce::Logger::writeToLog("[FAILED] [FullSongService] Executable exists: " + juce::String(exeFileSep.existsAsFile() ? "YES" : "NO"));
        if (exeFileSep.existsAsFile()) {
            juce::Logger::writeToLog("[FAILED] [FullSongService] Executable size: " + juce::String(exeFileSep.getSize()) + " bytes");
        }

        // Check for critical Python DLLs
        juce::File pythonDirSep = exeFileSep.getParentDirectory();
        juce::File python311DllSep = pythonDirSep.getChildFile("python311.dll");
        juce::File vcruntime140DllSep = pythonDirSep.getChildFile("vcruntime140.dll");
        juce::Logger::writeToLog("[FAILED] [FullSongService] python311.dll exists: " + juce::String(python311DllSep.existsAsFile() ? "YES" : "NO"));
        juce::Logger::writeToLog("[FAILED] [FullSongService] vcruntime140.dll exists: " + juce::String(vcruntime140DllSep.existsAsFile() ? "YES" : "NO"));
#endif
        return res;
    }

    juce::Logger::writeToLog("[OK] [FullSongService] audio-separator process started (direct, no cmd.exe wrapper)");

    // NOTE: audio-separator CLI does NOT support --progress-file.
    // Progress is estimated from stdout parsing below.
    publishProgress(10, "Starting audio separation (UVR)...", cfg);

    // Real-time progress parsing from audio-separator stdout.
    // Maps audio-separator phases to UI progress ranges:
    //   0-5%   Queued / checking availability  (published by caller before separateAudioUVR)
    //   10-12% Starting / loading model
    //   12-15% Model loaded, beginning inference
    //   15-85% Separation (parsed from tqdm N/M segment progress)
    //   85-95% Writing output stems
    //   95-100% Complete
    // IMPORTANT: progress must be strictly increasing — never publish a value lower than the last
    int totalSegments = 0;  // Detected from tqdm "N/M" output
    int lastPublishedPct = 10;  // We start at 10% (set by caller)
    auto parseUvrProgress = [this, &cfg, &totalSegments, &lastPublishedPct](const juce::String& line) {
        // Helper: only publish if progress moved forward
        auto safePublish = [&](int pct, const juce::String& msg) {
            if (pct > lastPublishedPct) {
                lastPublishedPct = pct;
                publishProgress(pct, msg, cfg);
            }
        };

        // Phase detection: model loading
        if (line.containsIgnoreCase("Loading model")) {
            safePublish(11, "Loading AI model...");
            return;
        }
        if (line.containsIgnoreCase("Load model duration")) {
            safePublish(13, "Model loaded, starting separation...");
            return;
        }
        if (line.containsIgnoreCase("Downloading")) {
            safePublish(11, "Downloading model (first run)...");
            return;
        }
        if (line.containsIgnoreCase("Processing file") || line.containsIgnoreCase("Starting separation")) {
            safePublish(14, "Analyzing audio...");
            return;
        }
        if (line.containsIgnoreCase("Separation complete")) {
            safePublish(90, "Writing stems...");
            return;
        }

        // tqdm segment progress: parse "N/M" pattern from lines like
        //   "  20%|██        | 1/5 [00:20<01:20, 20.0s/it]"
        //   "  0%|          | 0/5 [00:00<?, ?it/s]"
        int slashPos = line.indexOf("/");
        if (slashPos > 0) {
            // Walk backwards from '/' to find the segment number
            int numStart = slashPos - 1;
            while (numStart >= 0 && juce::CharacterFunctions::isDigit(line[numStart]))
                --numStart;
            ++numStart;

            // Walk forwards from '/' to find the total
            int denomEnd = slashPos + 1;
            while (denomEnd < line.length() && juce::CharacterFunctions::isDigit(line[denomEnd]))
                ++denomEnd;

            if (numStart < slashPos && (slashPos + 1) < denomEnd) {
                int current = line.substring(numStart, slashPos).getIntValue();
                int total = line.substring(slashPos + 1, denomEnd).getIntValue();

                if (total > 0 && total < 100 && current >= 0 && current <= total) {
                    totalSegments = total;
                    // Map segment progress to 15-85% range
                    int pct = 15 + (current * 70 / total);
                    juce::String msg = "Separating: segment " + juce::String(current)
                        + "/" + juce::String(total);
                    safePublish(pct, msg);
                }
            }
        }
    };

    // Monitor process with real-time output parsing
    juce::String outputBuffer;
    juce::String detectedFileError;   // Captures file-format errors from audio-separator
    bool detectedCudaError = false;   // CUDA kernel crash / GPU incompatibility
    const juce::int64 hardMaxMs = 3600000;  // 1 hour timeout for UVR
    juce::int64 startMs = juce::Time::getMillisecondCounter();

    // CRITICAL: Check if process is still running BEFORE entering loop
    bool initiallyRunning = p.isRunning();
    juce::Logger::writeToLog("[UVR] [FullSongService] Process initially running: " + juce::String(initiallyRunning ? "YES" : "NO"));

    if (!initiallyRunning) {
        // Process crashed immediately after starting!
        juce::Logger::writeToLog("[FAILED] [FullSongService] ⚠️ Process CRASHED immediately after start!");
        juce::uint32 crashExitCode = p.getExitCode();
        juce::Logger::writeToLog("[FAILED] [FullSongService] Immediate crash exit code: " + juce::String(crashExitCode));

        // Try to read any error output before it's lost
        char crashBuffer[8192];
        int crashBytesRead = p.readProcessOutput(crashBuffer, sizeof(crashBuffer) - 1);
        if (crashBytesRead > 0) {
            crashBuffer[crashBytesRead] = '\0';
            juce::String crashOutput = juce::String::createStringFromData(crashBuffer, crashBytesRead);
            juce::Logger::writeToLog("[FAILED] [FullSongService] Crash output: " + crashOutput);
        } else {
            juce::Logger::writeToLog("[FAILED] [FullSongService] No crash output available");
        }

        res.error = "audio-separator crashed immediately (exit code " + juce::String(crashExitCode) + ")";
        publishProgress(0, "UVR failed - process crashed", cfg);
        return res;
    }

    int loopIterations = 0;
    while (p.isRunning()) {
        loopIterations++;
        char buffer[4096];
        int bytesRead = p.readProcessOutput(buffer, sizeof(buffer) - 1);

        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            outputBuffer += juce::String::createStringFromData(buffer, bytesRead);

            // Process complete lines
            int newlinePos;
            while ((newlinePos = outputBuffer.indexOfChar('\n')) >= 0) {
                juce::String line = outputBuffer.substring(0, newlinePos).trimEnd();
                outputBuffer = outputBuffer.substring(newlinePos + 1);

                // Handle carriage return lines (progress bars)
                if (line.contains("\r")) {
                    // Take last segment after \r for progress bar updates
                    int lastCR = line.lastIndexOf("\r");
                    if (lastCR >= 0) {
                        line = line.substring(lastCR + 1);
                    }
                }

                if (line.isNotEmpty()) {
                    juce::Logger::writeToLog("[UVR_CLI] " + line);
                    parseUvrProgress(line);

                    // Detect file-format errors from audio-separator so we can
                    // show a user-friendly message instead of the generic
                    // "did not produce drums stem" error.
                    if (line.containsIgnoreCase("Format not recognised") ||
                        line.containsIgnoreCase("format not recognized") ||
                        line.containsIgnoreCase("Could not read audio file") ||
                        line.containsIgnoreCase("Error opening"))
                    {
                        detectedFileError = line;
                        juce::Logger::writeToLog("[UVR] [FullSongService] Detected file-format error: " + line);
                    }
                    else if (line.containsIgnoreCase("Failed to process file") ||
                             line.containsIgnoreCase("No valid audio"))
                    {
                        if (detectedFileError.isEmpty())
                            detectedFileError = line;
                    }

                    // Detect CUDA / GPU errors — triggers CPU fallback retry.
                    // NOTE: audio-separator may return exit code 0 even when CUDA fails
                    // (it catches the exception internally). We detect errors from stdout
                    // and trigger [CUDA_RETRY] in the "no output" path as well.
                    if (line.containsIgnoreCase("FATAL: kernel") ||
                        line.containsIgnoreCase("CUDA error") ||
                        line.containsIgnoreCase("CUDA out of memory") ||
                        line.containsIgnoreCase("RuntimeError: CUDA") ||
                        line.containsIgnoreCase("launch timed out") ||
                        line.containsIgnoreCase("device-side assert") ||
                        line.containsIgnoreCase("cusolver") ||
                        line.containsIgnoreCase("cublas") ||
                        line.containsIgnoreCase("No GPU") ||
                        (line.containsIgnoreCase("was built for sm") && line.containsIgnoreCase("FATAL")))
                    {
                        detectedCudaError = true;
                        juce::Logger::writeToLog("[UVR] [FullSongService] Detected CUDA/GPU error: " + line);
                    }
                }
            }

            // Also check for \r-delimited progress (tqdm often uses \r instead of \n)
            int crPos;
            while ((crPos = outputBuffer.indexOf("\r")) >= 0) {
                juce::String line = outputBuffer.substring(0, crPos).trim();
                outputBuffer = outputBuffer.substring(crPos + 1);

                if (line.isNotEmpty() && line.contains("%|")) {
                    parseUvrProgress(line);
                }
            }
        }

        // Safety timeout
        auto now = juce::Time::getMillisecondCounter();
        if ((now - startMs) > hardMaxMs) {
            juce::Logger::writeToLog("[FAILED] [FullSongService] audio-separator exceeded timeout; killing...");
            p.kill();
            res.error = "audio-separator timed out";
            return res;
        }

        juce::Thread::sleep(100);

        // Periodic status log (every 50 iterations = ~5 seconds)
        if (loopIterations % 50 == 0) {
            juce::Logger::writeToLog("[UVR] [FullSongService] Still running... iterations=" + juce::String(loopIterations) +
                " elapsed=" + juce::String((juce::Time::getMillisecondCounter() - startMs) / 1000) + "s");
        }
    }

    // Loop exited - process is no longer running
    juce::Logger::writeToLog("[UVR] [FullSongService] Process loop exited after " + juce::String(loopIterations) + " iterations");
    juce::Logger::writeToLog("[UVR] [FullSongService] Total runtime: " + juce::String((juce::Time::getMillisecondCounter() - startMs) / 1000) + " seconds");

    // Read any remaining output after process exits
    {
        char buffer[4096];
        int bytesRead;
        while ((bytesRead = p.readProcessOutput(buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            juce::String remaining = juce::String::createStringFromData(buffer, bytesRead);
            juce::Logger::writeToLog("[UVR_CLI_FINAL] " + remaining.trim());
        }
    }

    // Get exit code (process already finished since isRunning() returned false)
    juce::uint32 exitCode = p.getExitCode();
    juce::Logger::writeToLog("[UVR] [FullSongService] audio-separator exited with code: " + juce::String(exitCode));

    // Also dump remaining output buffer
    if (outputBuffer.isNotEmpty()) {
        juce::Logger::writeToLog("[UVR_CLI_REMAINING] " + outputBuffer.trim());
    }

    if (exitCode != 0) {
        if (detectedCudaError)
        {
            res.error = "[CUDA_RETRY] GPU error during separation — will retry on CPU";
            juce::Logger::writeToLog("[UVR] [FullSongService] CUDA/GPU error detected (exit code "
                                     + juce::String(exitCode) + ") — caller should retry with CPU");
        }
        else
        {
            res.error = "audio-separator failed with exit code " + juce::String(exitCode);
        }
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);

        // PROGRESS FILE CLEANUP: Remove progress file on error
        if (progressFile.existsAsFile()) {
            progressFile.deleteFile();
        }

        return res;
    }

    // Find the drums output file
    // audio-separator naming: {input_name}_(Drums)_{model}.wav or .flac
    juce::Logger::writeToLog("[UVR] [FullSongService] Searching for drums output in: " + tempDir.getFullPathName());

    juce::Array<juce::File> outputFiles;
    tempDir.findChildFiles(outputFiles, juce::File::findFiles, false, "*.wav");
    tempDir.findChildFiles(outputFiles, juce::File::findFiles, false, "*.flac");

    juce::File drumsFile;
    for (auto& f : outputFiles) {
        juce::Logger::writeToLog("[UVR] [FullSongService] Found output: " + f.getFileName());
        if (f.getFileName().containsIgnoreCase("Drums") || f.getFileName().containsIgnoreCase("drum")) {
            drumsFile = f;
            break;
        }
    }

    // If no explicit drums file, take the first .wav (for drums-only mode)
    if (!drumsFile.existsAsFile() && outputFiles.size() > 0) {
        drumsFile = outputFiles[0];
        juce::Logger::writeToLog("[UVR] [FullSongService] Using first output as drums: " + drumsFile.getFileName());
    }

    if (!drumsFile.existsAsFile()) {
        // CUDA errors: audio-separator may return exit code 0 but produce no output.
        // Detect this and trigger CPU fallback retry instead of showing a confusing error.
        if (detectedCudaError)
        {
            res.error = "[CUDA_RETRY] GPU error during separation — will retry on CPU";
            juce::Logger::writeToLog("[UVR] [FullSongService] CUDA error detected with exit code 0 (no output produced) — triggering CPU fallback");
            return res;
        }

        // Provide a user-friendly error based on what audio-separator reported.
        if (detectedFileError.isNotEmpty())
        {
            res.error = "Could not extract drums from the song: the audio file could not be read. "
                        "The file may be corrupted, empty, or in an unsupported format. "
                        "Please try a standard WAV or MP3 file.";
        }
        else
        {
            res.error = "audio-separator did not produce drums stem";
        }
        juce::Logger::writeToLog("[FAILED] [FullSongService] " + res.error);

        // List all files for debugging
        juce::Array<juce::File> allFiles;
        tempDir.findChildFiles(allFiles, juce::File::findFilesAndDirectories, true);
        for (auto& f : allFiles) {
            juce::Logger::writeToLog("[UVR] [DEBUG] " + f.getRelativePathFrom(tempDir));
        }
        
        // PROGRESS FILE CLEANUP: Remove progress file on error
        if (progressFile.existsAsFile()) {
            progressFile.deleteFile();
        }
        
        return res;
    }

    res.drumStemFile = drumsFile;
    
    // PROGRESS FILE CLEANUP: Remove progress file after completion
    if (progressFile.existsAsFile()) {
        progressFile.deleteFile();
        juce::Logger::writeToLog("[UVR] [FullSongService] Progress file cleaned up");
    }
    
    juce::Logger::writeToLog("[OK] [FullSongService] UVR separation complete: " + drumsFile.getFileName());
    juce::Logger::writeToLog("[UVR] [FullSongService] Drums stem: " + drumsFile.getFullPathName());

    // Tier 2: Save stems to cache for future re-use
    {
        auto cacheDir = samplar::DataPaths::getUserDataDirectory().getChildFile("stem_cache");
        auto cacheKey = input.getFileName()
            + "_" + juce::String(input.getSize())
            + "_" + juce::String(input.getLastModificationTime().toMilliseconds())
            + "_" + cfg.uvrModelName
            + "_" + (cfg.uvrDrumsOnly ? "drums" : "all");
        auto cacheHash = juce::String::toHexString(cacheKey.hashCode64());
        auto cachedDir = cacheDir.getChildFile(cacheHash);

        if (cachedDir.createDirectory())
        {
            juce::Array<juce::File> outputFiles;
            tempDir.findChildFiles(outputFiles, juce::File::findFiles, true, "*.wav");
            tempDir.findChildFiles(outputFiles, juce::File::findFiles, true, "*.flac");

            int copied = 0;
            for (const auto& f : outputFiles)
            {
                if (f.copyFileTo(cachedDir.getChildFile(f.getFileName())))
                    ++copied;
            }
            juce::Logger::writeToLog("[UVR] [FullSongService] CACHE SAVE: " + cacheHash.substring(0, 12)
                + " (" + juce::String(copied) + " stems cached)");
        }
    }

    return res;
}

} // namespace samplar
