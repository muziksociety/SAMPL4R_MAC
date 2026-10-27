# SAMPLAR build:
#   Windows release:  cmake -B build -G "Visual Studio 17 2022" -A x64           (VST3 — SamplarPlugin_VST3)
#   Windows dev:      cmake -B build -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_BUILD_STANDALONE=ON
#   macOS release:    cmake -B build -G "Xcode"                                  (VST3 + AU + Standalone, universal2)
#   macOS dev (Ninja):cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

cmake_minimum_required(VERSION 3.20)

# macOS: default to universal2 (arm64 + x86_64) and macOS 11.0 minimum.
# Must be set BEFORE project() for CMake to pick them up cleanly.
# Override on the command line with e.g. -DCMAKE_OSX_ARCHITECTURES=arm64 for a faster dev build.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
        # 11.0 = Big Sur (first Apple Silicon release); matches ORT universal2 requirement.
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

project(samplar VERSION 1.0.0 LANGUAGES C CXX)

option(SAMPLAR_BUILD_TESTS "Build SAMPLAR unit tests" OFF)
option(SAMPLAR_BUILD_STANDALONE "Build the SamplarApp standalone target (dev only)" OFF)

# Emit Release PDB for crash-dump analysis. /Zi keeps line info; /OPT:REF,ICF
# stays on so the linker still produces tight code.
if (MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# Extract version from git tags (e.g., v1.0.2 -> "1.0.2")
execute_process(
  COMMAND git describe --tags --abbrev=0
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE SAMPLAR_GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE GIT_TAG_RESULT
)

if(GIT_TAG_RESULT EQUAL 0 AND SAMPLAR_GIT_TAG)
  string(REGEX REPLACE "^v" "" SAMPLAR_GIT_VERSION "${SAMPLAR_GIT_TAG}")
  message(STATUS "SAMPLAR version from git tag: ${SAMPLAR_GIT_VERSION}")
else()
  set(SAMPLAR_GIT_VERSION "0.0.0-dev")
  message(STATUS "SAMPLAR version: ${SAMPLAR_GIT_VERSION} (no git tag found)")
endif()

configure_file(
  ${CMAKE_SOURCE_DIR}/src/app/SamplarVersion.h.in
  ${CMAKE_BINARY_DIR}/generated/SamplarVersion.h
  @ONLY
)

# Prefer vendored JUCE inside Sample_monsta5, fallback to repo-root JUCE
set(JUCE_LOCAL_DIR    "${CMAKE_CURRENT_LIST_DIR}/JUCE")
set(JUCE_FALLBACK_DIR "${CMAKE_CURRENT_LIST_DIR}/../JUCE")
# Only use vendored JUCE if core GUI modules are present (guard against partial copies)
if(EXISTS "${JUCE_LOCAL_DIR}/CMakeLists.txt" AND EXISTS "${JUCE_LOCAL_DIR}/modules/juce_gui_basics")
  message(STATUS "Using vendored JUCE at: ${JUCE_LOCAL_DIR}")
  # Disable JUCE examples and extras to avoid conflicts
  set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Disable JUCE examples" FORCE)
  set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Disable JUCE extras" FORCE)
  add_subdirectory("${JUCE_LOCAL_DIR}" JUCE_local)
elseif(EXISTS "${JUCE_LOCAL_DIR}/CMakeLists.txt")
  message(WARNING "Vendored JUCE found but missing modules/juce_gui_basics. Falling back to ${JUCE_FALLBACK_DIR}")
  if(EXISTS "${JUCE_FALLBACK_DIR}/CMakeLists.txt")
    # Disable JUCE examples and extras to avoid conflicts
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Disable JUCE examples" FORCE)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Disable JUCE extras" FORCE)
    add_subdirectory("${JUCE_FALLBACK_DIR}" JUCE_fallback)
  else()
    message(WARNING "JUCE not found at fallback either. Configure FetchContent or set JUCE path.")
  endif()
elseif(EXISTS "${JUCE_FALLBACK_DIR}/CMakeLists.txt")
  message(STATUS "Using fallback JUCE at: ${JUCE_FALLBACK_DIR}")
  # Disable JUCE examples and extras to avoid conflicts
  set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Disable JUCE examples" FORCE)
  set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Disable JUCE extras" FORCE)
  add_subdirectory("${JUCE_FALLBACK_DIR}" JUCE_fallback)
else()
  message(WARNING "JUCE not found at ${JUCE_LOCAL_DIR} or ${JUCE_FALLBACK_DIR}. Configure FetchContent or set JUCE path.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# nlohmann/json for MIDI pattern database
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Loop Tempo Estimator (STFT + ODF based tempo detection from Audacity)
# Used as secondary tempo detector alongside DWT for consensus voting
set(LTE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/loop-tempo-estimator")
if(EXISTS "${LTE_DIR}/CMakeLists.txt" AND EXISTS "${LTE_DIR}/pffft.cmake")
  message(STATUS "========================================")
  message(STATUS "Loop Tempo Estimator: ENABLED")
  message(STATUS "========================================")
  # Disable tests and vamp plugin - we only need the library
  set(BUILD_TESTS OFF CACHE BOOL "Disable LTE tests" FORCE)
  set(BUILD_VAMP_PLUGIN OFF CACHE BOOL "Disable LTE vamp plugin" FORCE)
  add_subdirectory(${LTE_DIR} loop-tempo-estimator)
  set(LTE_FOUND TRUE)
elseif(EXISTS "${LTE_DIR}/CMakeLists.txt")
  # Partial checkout: LTE source is present but pffft.cmake helper is missing
  # (audacity/loop-tempo-estimator upstream repo is no longer public — 404).
  # Disable LTE rather than fail the build. Tempo detection falls back to the
  # DWT primary detector. Restore pffft.cmake from an archived LTE checkout to
  # re-enable.
  message(STATUS "Loop Tempo Estimator: DISABLED (pffft.cmake missing)")
  set(LTE_FOUND FALSE)
else()
  message(STATUS "Loop Tempo Estimator not found at ${LTE_DIR}")
  set(LTE_FOUND FALSE)
endif()

# Optional: SoundTouch for constant-pitch preview (offline path only — see
# src/audio/Preview/SoundTouchSource.h). The realtime audio-path pitch
# shifter is now signalsmith-stretch (vendored, see below).
option(WITH_SOUNDTOUCH "Enable SoundTouch for constant-pitch time-stretch preview" ON)
option(WITH_ZEROMQ "Enable ZeroMQ IPC clients (Python bridge)" OFF)

# Beat This! neural beat tracker (Foscarin et al., ISMIR 2024) — adds a 4th
# detector to the tempo pipeline that produces explicit beat timestamps via
# ONNX inference. Robust against metric-ratio ambiguity (95 vs 126 BPM on
# 16th-note hat patterns) that confuses autocorrelation-based detectors.
# Pulls in ONNX Runtime as a runtime dependency (DLL copied next to exe).
option(WITH_BEAT_THIS "Enable Beat This! neural beat tracker (ONNX, ~83 MB model)" ON)

# signalsmith-stretch — MIT, header-only realtime pitch shifter used by
# MidiMappingService for stem + voice pitch in the audio path. Vendored
# at third_party/signalsmith-stretch (depends on third_party/signalsmith-linear).
option(WITH_SIGNALSMITH_STRETCH "Enable signalsmith-stretch realtime pitch shifter" ON)
set(SIGNALSMITH_STRETCH_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/signalsmith-stretch")
set(SIGNALSMITH_LINEAR_DIR  "${CMAKE_CURRENT_LIST_DIR}/third_party/signalsmith-linear")
if(WITH_SIGNALSMITH_STRETCH)
  if(NOT EXISTS "${SIGNALSMITH_STRETCH_DIR}/signalsmith-stretch.h")
    message(FATAL_ERROR "WITH_SIGNALSMITH_STRETCH=ON but ${SIGNALSMITH_STRETCH_DIR}/signalsmith-stretch.h not found. Run the vendor step.")
  endif()
  if(NOT EXISTS "${SIGNALSMITH_LINEAR_DIR}/include/signalsmith-linear/stft.h")
    message(FATAL_ERROR "WITH_SIGNALSMITH_STRETCH=ON but ${SIGNALSMITH_LINEAR_DIR}/include/signalsmith-linear/stft.h not found. Run the vendor step.")
  endif()
  message(STATUS "signalsmith-stretch: ENABLED (vendored)")
endif()

# Developer-only fallback: when ON, bakes the project's SAMPLAR_Portable path
# into the built VST3 so the installed plugin can still locate Python /
# audio-separator from a dev machine when no registry/installer is present.
# MUST be OFF for commercial release builds (CMAKE_SOURCE_DIR is dev path).
option(SAMPLAR_ENABLE_DEV_FALLBACK "Bake dev-machine SAMPLAR_Portable path into plugin" OFF)

# B1 tracing: compile-in Chrome-Trace-format event profiler (opens in ui.perfetto.dev).
# Default ON during active development; switch to OFF for commercial release builds.
option(SAMPLAR_ENABLE_TRACING "Enable SAMPLAR Chrome-Trace event profiler" OFF)
if(WITH_SOUNDTOUCH)
  include(FetchContent)
  # Try to fetch SoundTouch; if unavailable, continue without failing the build.
  # SoundTouch moved to codeberg; use official repo
  FetchContent_Declare(
    soundtouch
    GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
    GIT_TAG 2.3.2
    GIT_SHALLOW TRUE
  )
  set(FETCHCONTENT_QUIET OFF)
  # Manually populate and add library to avoid subproject policy issues
  FetchContent_GetProperties(soundtouch)
  if(NOT soundtouch_POPULATED)
    FetchContent_Populate(soundtouch)
  endif()
  set(SOUNDTOUCH_INCLUDE_DIR "${soundtouch_SOURCE_DIR}/include")
  file(GLOB SOUNDTOUCH_SRC
    "${soundtouch_SOURCE_DIR}/source/SoundTouch/*.cpp")
  add_library(soundtouch_local STATIC ${SOUNDTOUCH_SRC})
  target_include_directories(soundtouch_local PUBLIC ${SOUNDTOUCH_INCLUDE_DIR})
  target_compile_definitions(soundtouch_local PUBLIC ST_NO_EXCEPTION_HANDLING)
  set(SOUNDTOUCH_TARGET soundtouch_local)
endif()

# -----------------------------------------------------------------------
# Beat This! neural beat tracker (vendored at third_party/beat_this/)
#   - ONNX Runtime fetched from GitHub releases (pre-built Windows x64)
#   - C++ inference wrapper compiled into a static library
#   - 83 MB ONNX model copied next to the exe at install time
# -----------------------------------------------------------------------
set(BEAT_THIS_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/beat_this")
if(WITH_BEAT_THIS)
  if(NOT EXISTS "${BEAT_THIS_DIR}/models/beat_this.onnx")
    message(WARNING "WITH_BEAT_THIS=ON but ${BEAT_THIS_DIR}/models/beat_this.onnx not found. Disabling.")
    set(WITH_BEAT_THIS OFF)
  endif()
endif()

# ONNX Runtime is required UNCONDITIONALLY by src/services/separation/OnnxSeparationProvider.cpp
# (Demucs stem separation). beat_this is an optional consumer; both link
# against the same vendored ORT below.
include(FetchContent)
set(ORT_VERSION "1.20.1")
if(WIN32)
  set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")
elseif(APPLE)
  set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-universal2-${ORT_VERSION}.tgz")
else()
  set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz")
endif()

FetchContent_Declare(
  onnxruntime_prebuilt
  URL ${ORT_URL}
)
FetchContent_MakeAvailable(onnxruntime_prebuilt)
set(ORT_ROOT "${onnxruntime_prebuilt_SOURCE_DIR}")
message(STATUS "ONNX Runtime ${ORT_VERSION} at: ${ORT_ROOT}")

if(WIN32)
  set(ORT_LIB "${ORT_ROOT}/lib/onnxruntime.lib")
  set(ORT_DLL "${ORT_ROOT}/lib/onnxruntime.dll")
elseif(APPLE)
  set(ORT_LIB "${ORT_ROOT}/lib/libonnxruntime.${ORT_VERSION}.dylib")
  set(ORT_DLL "${ORT_LIB}")
else()
  set(ORT_LIB "${ORT_ROOT}/lib/libonnxruntime.so.${ORT_VERSION}")
  set(ORT_DLL "${ORT_LIB}")
endif()

if(NOT EXISTS "${ORT_LIB}")
  message(FATAL_ERROR "ONNX Runtime archive extracted but lib not at ${ORT_LIB}")
endif()

if(WITH_BEAT_THIS)
  message(STATUS "========================================")
  message(STATUS "Beat This! neural beat tracker: ENABLED")
  message(STATUS "========================================")

  # Static library wrapping the vendored beat_this_cpp inference engine.
  set(BEAT_THIS_LIB_SOURCES
    ${BEAT_THIS_DIR}/src/InferenceProcessor.cpp
    ${BEAT_THIS_DIR}/src/MelSpectrogram.cpp
    ${BEAT_THIS_DIR}/src/Postprocessor.cpp
    ${BEAT_THIS_DIR}/src/beat_this_api.cpp
  )
  add_library(beat_this_engine STATIC ${BEAT_THIS_LIB_SOURCES})
  target_include_directories(beat_this_engine PUBLIC
    ${BEAT_THIS_DIR}/src
    ${BEAT_THIS_DIR}/third_party/pocketfft
    ${ORT_ROOT}/include
  )
  target_link_libraries(beat_this_engine PUBLIC ${ORT_LIB})
  target_compile_features(beat_this_engine PUBLIC cxx_std_17)
  if(MSVC)
    target_compile_options(beat_this_engine PRIVATE /wd4244 /wd4267 /wd4996)
  endif()
endif()

# Sources
set(SAMPLAR_SOURCES
  src/app/App.cpp
  src/app/AudioDeviceService.cpp
  src/audio/Preview/SoundTouchSource.cpp
  src/audio/SamplePreviewCache.cpp
  src/audio/SequencerAudioSource.h
  src/audio/SequencerAudioSource.cpp
  # Spectral separation engine (FFT-based 5-band frequency separation)
  src/audio/SpectralSeparator.h
  src/audio/SpectralSeparator.cpp
  src/audio/BandProcessor.h
  src/audio/BandProcessor.cpp
  src/services/ProcessingCoordinatorService.cpp
  src/services/FullSongService.cpp
  src/services/FullSongSeparationService.h
  src/services/FullSongSeparationService.cpp
  # Native ONNX 4-stem separation (ported from SM5) — replaces Python/UVR path
  src/services/separation/OnnxSeparationProvider.h
  src/services/separation/OnnxSeparationProvider.cpp
  # S-Mode services
  src/services/smode/SModeService.h
  src/services/smode/SModeService.cpp
  src/services/smode/SModeSeparationService.h
  src/services/smode/SModeSeparationService.cpp
  src/services/smode/SModeSegmentModel.h
  src/services/smode/SModeSegmentModel.cpp
  src/services/smode/PatternGenerator.h
  src/services/smode/PatternGenerator.cpp
  src/services/smode/SpectralFluxDetector.h
  src/services/smode/SpectralFluxDetector.cpp
  src/services/DrumLibraryService.h
  src/services/DrumLibraryService.cpp
  # Sample Analysis Pipeline
  src/services/analysis/TempoAnalyzer.h
  src/services/analysis/TempoAnalyzer.cpp
  src/services/analysis/DWTTempoDetector.h
  src/services/analysis/DWTTempoDetector.cpp
  src/services/analysis/HybridTempoDetector.h
  src/services/analysis/HybridTempoDetector.cpp
  src/services/analysis/BeatThisDetector.h
  src/services/analysis/BeatThisDetector.cpp
  src/services/analysis/OctaveCorrection.h
  src/services/analysis/OctaveCorrection.cpp
  src/services/analysis/TempoRefiner.h
  src/services/analysis/TempoRefiner.cpp
  src/services/analysis/TempoCache.h
  src/services/analysis/TempoCache.cpp
  src/services/analysis/LoopDivider.h
  src/services/analysis/LoopDivider.cpp
  src/services/analysis/BeatAlignedSlicer.h
  src/services/analysis/BeatAlignedSlicer.cpp
  src/services/analysis/DownbeatDetector.h
  src/services/analysis/DownbeatDetector.cpp
  src/services/LicenseSystemService.h
  src/services/LicenseSystemService.cpp
  src/services/UpdateCheckService.h
  src/services/UpdateCheckService.cpp
  src/services/MidiMappingService.h
  src/services/MidiMappingService.cpp
  src/security/HardwareFingerprintGenerator.h
  src/security/HardwareFingerprintGenerator.cpp
  src/security/LicenseCache.h
  src/security/LicenseCache.cpp
  src/security/LicenseCleanerUtility.h
  src/security/LicenseCleanerUtility.cpp
  src/security/SecureSessionManager.h
  src/security/SecureSessionManager.cpp
  src/security/SamplarVerificationKey.h
  src/security/SignedTokenVerifier.h
  src/security/SignedTokenVerifier.cpp
  src/networking/LicenseServerClient.h
  src/networking/LicenseServerClient.cpp
  src/networking/OAuthBrowserAuthenticator.h
  src/networking/OAuthBrowserAuthenticator.cpp
  src/licensing/FeatureManager.h
  src/licensing/FeatureManager.cpp
  src/ui/MainWindow.cpp
  src/ui/licensing/LicenseActivationDialog.h
  src/ui/licensing/LicenseActivationDialog.cpp
  src/ui/licensing/LicenseStatusWidget.h
  src/ui/licensing/LicenseStatusWidget.cpp
  tests/licensing/LicenseSystemTests.cpp
  src/ui/Pages/ProcessingPage.cpp
  src/ui/Debug/DebugConsole.cpp
  src/ui/Debug/DebugFlags.cpp
  src/ui/Style/Sm5LookAndFeel.cpp
  src/ui/Style/SamplarLookAndFeel.cpp
  src/ui/Style/DropZoneLookAndFeel.cpp
  src/ui/Style/TransparentHoverButtonLookAndFeel.cpp
  src/app/ServiceRegistry.cpp
  src/app/EventBus.cpp
  src/app/AppState.h
  src/app/AppState.cpp
  src/utils/Tracing.h
  src/utils/Tracing.cpp
  src/ui/Theme/EngineColorManager.h
  src/ui/Components/SamplarAnimation.h
  src/ui/Components/SamplarAnimation.cpp
  src/ui/Components/AudioMeterBars.h
  src/ui/Components/AudioMeterBars.cpp
  src/ui/Components/MidiDragArea.h
  src/ui/Components/MidiDragArea.cpp
  src/ui/Components/WaveDragArea.h
  src/ui/Components/WaveDragArea.cpp
  src/ui/Components/CollapsibleSection.h
  src/ui/Components/CollapsibleSection.cpp
  src/ui/Components/SettingsDialog.h
  src/ui/Components/SettingsDialog.cpp
  src/ui/Components/LoadingAnimation.h
  src/ui/Components/LoadingAnimation.cpp
  src/ui/Components/GifAnimatedBackground.h
  src/ui/Components/GifAnimatedBackground.cpp
  src/ui/Components/UpdateDialog.h
  src/ui/Components/UpdateDialog.cpp
  src/ui/Components/NotificationButton.h
  src/ui/Components/NotificationButton.cpp
  # Utilities
  src/utils/MidiFileGenerator.h
  src/utils/MidiFileGenerator.cpp
  # Onset detection utility (spectral flux - shared by BeatAlignedSlicer)
  src/utils/OnsetDetector.h
  src/utils/OnsetDetector.cpp
  # S-Mode page and components
  src/ui/Pages/SModePage.h
  src/ui/Pages/SModePage.cpp
  src/ui/Components/smode/StemCountDialog.h
  src/ui/Components/smode/StemCountDialog.cpp
  src/ui/Components/smode/StemWaveformComponent.h
  src/ui/Components/smode/StemWaveformComponent.cpp
  src/ui/Components/smode/SequencerSlotComponent.h
  src/ui/Components/smode/SequencerSlotComponent.cpp
  src/ui/Components/smode/SequencerLaneComponent.h
  src/ui/Components/smode/SequencerLaneComponent.cpp
  src/ui/Components/smode/SModeMidiLearnOverlay.h
  src/ui/Components/smode/SModeMidiLearnOverlay.cpp
  # MiniBPM - BPM detection library
  third_party/minibpm/MiniBpm.cpp
  # Loop detection with autocorrelation
  src/services/analysis/LoopDetector.cpp
  # Beat Grid and Loop Boundary UI Feature
  src/services/BeatAnalysisTypes.h
)


# -----------------------------------------------------------------------
# SamplarApp — Standalone GUI app (dev-only). Gated by SAMPLAR_BUILD_STANDALONE.
# Commercial release builds ship VST3 only (SamplarPlugin below). When the
# option is OFF, none of the SamplarApp target / target_sources / POST_BUILD
# commands are generated.
# -----------------------------------------------------------------------
if (SAMPLAR_BUILD_STANDALONE)
message(STATUS "SAMPLAR_BUILD_STANDALONE=ON — SamplarApp standalone target enabled")

juce_add_gui_app(SamplarApp
    PRODUCT_NAME "SAMPLAR"
    COMPANY_NAME "Muziksociety"
    COMPANY_WEBSITE "https://samplemonsta.com/samplar"
    COMPANY_EMAIL "muziksociety@gmail.com"
    BUNDLE_ID "com.muziksociety.samplar"
    VERSION "1.0.0"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_64.png"
)

target_compile_definitions(SamplarApp PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

target_sources(SamplarApp PRIVATE ${SAMPLAR_SOURCES})

target_compile_definitions(SamplarApp
  PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
)

# Performance optimizations: Enable SIMD (always) and fast math (Release only)
if(MSVC)
  # MSVC: Enable AVX2 always, optimization only in Release
  target_compile_options(SamplarApp PRIVATE /arch:AVX2)
  target_compile_options(SamplarApp PRIVATE $<$<CONFIG:Release>:/fp:fast>)
  target_compile_definitions(SamplarApp PRIVATE __AVX__=1 __AVX2__=1)
  message(STATUS "Enabled MSVC AVX2 optimizations")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # GCC/Clang: Enable AVX2 always, fast math only in Release
  target_compile_options(SamplarApp PRIVATE -mavx2 -mfma)
  target_compile_options(SamplarApp PRIVATE $<$<CONFIG:Release>:-ffast-math>)
  target_compile_definitions(SamplarApp PRIVATE __AVX__=1 __AVX2__=1)
  message(STATUS "Enabled GCC/Clang AVX2 optimizations")
endif()

# Check for Intel IPP availability (placeholder for future)
find_path(INTEL_IPP_INCLUDE_DIR NAMES ipp.h PATHS
  "C:/Program Files (x86)/Intel/oneAPI/ipp/latest/include"
  "/opt/intel/ipp/include"
)
if(INTEL_IPP_INCLUDE_DIR)
  target_compile_definitions(SamplarApp PRIVATE INTEL_IPP_AVAILABLE=1)
  target_include_directories(SamplarApp PRIVATE ${INTEL_IPP_INCLUDE_DIR})
  message(STATUS "Intel IPP found at: ${INTEL_IPP_INCLUDE_DIR}")
else()
  message(STATUS "Intel IPP not found - using JUCE FFT")
endif()

target_include_directories(SamplarApp PRIVATE src)
target_include_directories(SamplarApp PRIVATE ${CMAKE_BINARY_DIR}/generated)

if(WITH_SOUNDTOUCH AND SOUNDTOUCH_INCLUDE_DIR)
  target_include_directories(SamplarApp PRIVATE ${SOUNDTOUCH_INCLUDE_DIR})
endif()

if(WITH_SIGNALSMITH_STRETCH)
  target_include_directories(SamplarApp PRIVATE
    ${SIGNALSMITH_STRETCH_DIR}
    ${SIGNALSMITH_LINEAR_DIR}/include
  )
  target_compile_definitions(SamplarApp PRIVATE WITH_SIGNALSMITH_STRETCH=1)
endif()

target_link_libraries(SamplarApp
  PRIVATE
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_cryptography
    juce::juce_events
    juce::juce_core
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_opengl
    nlohmann_json::nlohmann_json
)

# If Loop Tempo Estimator was made available, link it and expose a feature define
if(LTE_FOUND)
  target_link_libraries(SamplarApp PRIVATE loop-tempo-estimator)
  target_include_directories(SamplarApp PRIVATE ${LTE_DIR}/include)
  target_compile_definitions(SamplarApp PRIVATE WITH_LTE=1)
  message(STATUS "Linked loop-tempo-estimator to SamplarApp")
endif()

# If SoundTouch was made available, link it and expose a feature define
if(WITH_SOUNDTOUCH AND SOUNDTOUCH_TARGET)
  target_link_libraries(SamplarApp PRIVATE ${SOUNDTOUCH_TARGET})
  target_compile_definitions(SamplarApp PRIVATE WITH_SOUNDTOUCH=1)
endif()

# Beat This! integration on SamplarApp
if(WITH_BEAT_THIS)
  target_link_libraries(SamplarApp PRIVATE beat_this_engine)
  target_compile_definitions(SamplarApp PRIVATE WITH_BEAT_THIS=1)
  target_include_directories(SamplarApp PRIVATE ${BEAT_THIS_DIR}/src ${ORT_ROOT}/include)

  # Copy onnxruntime.dll next to the exe so ONNX session creation succeeds.
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_DLL}"
      "$<TARGET_FILE_DIR:SamplarApp>/"
    COMMENT "Copying ONNX Runtime DLL next to SamplarApp"
  )
  # Provider extras (CUDA/DirectML stubs) — copy if present, but never fail.
  file(GLOB ORT_EXTRA_DLLS "${ORT_ROOT}/lib/*.dll")
  foreach(_dll IN LISTS ORT_EXTRA_DLLS)
    add_custom_command(TARGET SamplarApp POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:SamplarApp>/"
    )
  endforeach()

  # Copy beat_this.onnx into <exe-dir>/third_party/beat_this/models/ so
  # BeatThisDetector::resolveModelPath() finds it next to the binary.
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:SamplarApp>/third_party/beat_this/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${BEAT_THIS_DIR}/models/beat_this.onnx"
      "$<TARGET_FILE_DIR:SamplarApp>/third_party/beat_this/models/beat_this.onnx"
    COMMENT "Copying beat_this.onnx model next to SamplarApp"
  )
endif()

# Optional ZeroMQ (user-provided system install)
if(WITH_ZEROMQ)
  find_path(CPPZMQ_INCLUDE_DIRS NAMES zmq.hpp)
  find_library(ZMQ_LIB NAMES libzmq zmq)
  if(CPPZMQ_INCLUDE_DIRS AND ZMQ_LIB)
    target_include_directories(SamplarApp PRIVATE ${CPPZMQ_INCLUDE_DIRS})
    target_link_libraries(SamplarApp PRIVATE ${ZMQ_LIB})
    target_compile_definitions(SamplarApp PRIVATE WITH_ZEROMQ=1)
  else()
    message(WARNING "WITH_ZEROMQ=ON but cppzmq/libzmq not found; IPC will be disabled.")
  endif()
endif()

# Copy Python bridge to output directory for runtime access
if(WIN32)
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND powershell -NoLogo -NoProfile -Command "try { Copy-Item -Recurse -Force -LiteralPath \"${CMAKE_CURRENT_SOURCE_DIR}/python_bridge\" -Destination \"$<TARGET_FILE_DIR:SamplarApp>/python_bridge\" } catch { exit 0 }"
    COMMENT "Copying Python bridge to output directory [non-fatal]"
  )
else()
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/python_bridge
      $<TARGET_FILE_DIR:SamplarApp>/python_bridge
    COMMENT "Copying Python bridge to output directory"
  )
endif()

# Copy data folder (pattern databases) to output directory
# Contains: patterns_with_rolls.json (main DB), latin_patterns.json (Latin genres), etc.
if(WIN32)
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND powershell -NoLogo -NoProfile -Command "try { Copy-Item -Recurse -Force -LiteralPath \"${CMAKE_CURRENT_SOURCE_DIR}/data\" -Destination \"$<TARGET_FILE_DIR:SamplarApp>/data\" } catch { exit 0 }"
    COMMENT "Copying pattern data files to output directory [non-fatal]"
  )
else()
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_FILE_DIR:SamplarApp>/data
    COMMENT "Copying pattern data files to output directory"
  )
endif()

# Copy BACKGROUNDS folder (GIF animations + TILESET.png for ProcessingPage)
# These are loaded from the filesystem at runtime � too large to embed in BinaryData.
if(WIN32)
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND powershell -NoLogo -NoProfile -Command "try { Copy-Item -Recurse -Force -LiteralPath \"${CMAKE_CURRENT_SOURCE_DIR}/BACKGROUNDS\" -Destination \"$<TARGET_FILE_DIR:SamplarApp>/BACKGROUNDS\" } catch { exit 0 }"
    COMMENT "Copying BACKGROUNDS to output directory [non-fatal]"
  )
else()
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/BACKGROUNDS
      $<TARGET_FILE_DIR:SamplarApp>/BACKGROUNDS
    COMMENT "Copying BACKGROUNDS to output directory"
  )
endif()

# Copy REF.png to output directory (app icon / branding, loaded from filesystem)
if(WIN32)
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND powershell -NoLogo -NoProfile -Command "try { Copy-Item -Force -LiteralPath \"${CMAKE_CURRENT_SOURCE_DIR}/plugins/REF.png\" -Destination \"$<TARGET_FILE_DIR:SamplarApp>/REF.png\" } catch { exit 0 }"
    COMMENT "Copying REF.png to output directory [non-fatal]"
  )
endif()

# Copy Samples/ folder (includes 808 preview kit + GM drum samples)
if(WIN32)
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND powershell -NoLogo -NoProfile -Command "try { Copy-Item -Recurse -Force -LiteralPath \"${CMAKE_CURRENT_SOURCE_DIR}/Samples\" -Destination \"$<TARGET_FILE_DIR:SamplarApp>/Samples\" } catch { exit 0 }"
    COMMENT "Copying Samples (808 kit + GM) to output directory [non-fatal]"
  )
else()
  add_custom_command(TARGET SamplarApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/Samples
      $<TARGET_FILE_DIR:SamplarApp>/Samples
    COMMENT "Copying Samples to output directory"
  )
endif()

endif() # SAMPLAR_BUILD_STANDALONE — end of SamplarApp standalone target block

# -----------------------------------------------------------------------
# SamplarResources — shared juce_add_binary_data target (used by both
# SamplarApp [when enabled] and SamplarPlugin VST3). Always built.
# -----------------------------------------------------------------------
# Embed static resources (e.g., backdrop.png) so images are always available
juce_add_binary_data(SamplarResources SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/backdrop.png
  # REF.png removed from BinaryData � 2.4 MB pushes MSVC over heap limit.
  # InitialPage loads it from AppData (DataPaths::getAppDataDirectory) at runtime.

  # Loading animation sprite sheet (11x11 grid of 400x172 frames, 121 frames total)
  ${CMAKE_CURRENT_LIST_DIR}/loading_spritesheet.png

  # In-app SAMPLAR logo (top bar). 800x525 PNG (~200 KB), generated from
  # logo.png at the repo root via assets/generate_icon.py.
  ${CMAKE_CURRENT_LIST_DIR}/assets/samplar_logo.png

  # NOTE: Snake/Flappy/Shooter game assets removed (Games feature stripped from SAMPLAR)
  # NOTE: All BACKGROUNDS/ files are NOT embedded here:
  #   TILESET.png        22 MB  ? ~132 MB C++ source
  #   LAVA_optimized.gif 12 MB  ? ~72 MB C++ source
  #   CAMPO_optimized.gif 6 MB  ? ~36 MB C++ source
  # Embedding them overflows MSVC's compiler heap (error C1060).
  # ProcessingPage.cpp and GifAnimatedBackground.cpp load them from the
  # BACKGROUNDS/ folder on the filesystem at runtime.
)

if (SAMPLAR_BUILD_STANDALONE)
    target_link_libraries(SamplarApp PRIVATE SamplarResources)
endif()


# -----------------------------------------------------------------------
# SamplarPlugin — VST3 (all platforms) + AU + Standalone (macOS adds these)
# -----------------------------------------------------------------------
# Plugin format list: VST3 everywhere; AU + Standalone on macOS.
# Windows keeps only VST3 to match the existing Inno installer pipeline.
set(SAMPLAR_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND SAMPLAR_PLUGIN_FORMATS AU Standalone)
endif()

juce_add_plugin(SamplarPlugin
    PRODUCT_NAME "SAMPLAR"
    COMPANY_NAME "Muziksociety"
    COMPANY_WEBSITE "https://samplemonsta.com/samplar"
    COMPANY_EMAIL "muziksociety@gmail.com"
    BUNDLE_ID "com.muziksociety.samplar"
    VERSION "1.0.0"
    PLUGIN_MANUFACTURER_CODE Mzks
    PLUGIN_CODE Smpl
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    FORMATS ${SAMPLAR_PLUGIN_FORMATS}
    VST3_CATEGORIES "Instrument" "Sampler" "Drum"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice"
    HARDENED_RUNTIME_ENABLED FALSE     # ad-hoc signed; flip ON once a Developer ID cert is wired
    MICROPHONE_PERMISSION_ENABLED FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_64.png"
)

# Plugin sources: shared SAMPLAR_SOURCES minus App.cpp, plus plugin entry points
target_sources(SamplarPlugin PRIVATE
    ${SAMPLAR_SOURCES}
    src/plugin/SamplarAudioProcessor.cpp
    src/plugin/SamplarAudioProcessorEditor.cpp
)
# Remove App.cpp from the plugin build (it has START_JUCE_APPLICATION)
set_property(
    SOURCE src/app/App.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS "SAMPLAR_SKIP_APP_MAIN=1"
)

target_compile_definitions(SamplarPlugin PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_include_directories(SamplarPlugin PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
    ${ORT_ROOT}/include    # OnnxSeparationProvider.cpp needs onnxruntime_cxx_api.h
                           # unconditionally — was previously only added via the
                           # WITH_BEAT_THIS branch which is disabled on this clone.
)

if(WITH_SOUNDTOUCH AND SOUNDTOUCH_INCLUDE_DIR)
    target_include_directories(SamplarPlugin PRIVATE ${SOUNDTOUCH_INCLUDE_DIR})
endif()

if(WITH_SIGNALSMITH_STRETCH)
    target_include_directories(SamplarPlugin PRIVATE
        ${SIGNALSMITH_STRETCH_DIR}
        ${SIGNALSMITH_LINEAR_DIR}/include
    )
    target_compile_definitions(SamplarPlugin PRIVATE WITH_SIGNALSMITH_STRETCH=1)
endif()

target_link_libraries(SamplarPlugin
    PRIVATE
        juce::juce_gui_extra
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_events
        juce::juce_core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_opengl
        nlohmann_json::nlohmann_json
)

if(LTE_FOUND)
    target_link_libraries(SamplarPlugin PRIVATE loop-tempo-estimator)
    target_include_directories(SamplarPlugin PRIVATE ${LTE_DIR}/include)
    target_compile_definitions(SamplarPlugin PRIVATE WITH_LTE=1)
endif()

if(WITH_SOUNDTOUCH AND SOUNDTOUCH_TARGET)
    target_link_libraries(SamplarPlugin PRIVATE ${SOUNDTOUCH_TARGET})
    target_compile_definitions(SamplarPlugin PRIVATE WITH_SOUNDTOUCH=1)
endif()

# Link ORT into the plugin unconditionally — OnnxSeparationProvider.cpp uses
# the C++ API directly (not just transitively through beat_this_engine).
target_link_libraries(SamplarPlugin PRIVATE ${ORT_LIB})

if(WITH_BEAT_THIS)
    target_link_libraries(SamplarPlugin PRIVATE beat_this_engine)
    target_compile_definitions(SamplarPlugin PRIVATE WITH_BEAT_THIS=1)
    target_include_directories(SamplarPlugin PRIVATE ${BEAT_THIS_DIR}/src)
endif()

if (SAMPLAR_ENABLE_DEV_FALLBACK)
    set(_SAMPLAR_DEV_PATH "${CMAKE_SOURCE_DIR}/SAMPLAR_Portable")
    message(STATUS "SAMPLAR DEV fallback enabled � baking resource path: ${_SAMPLAR_DEV_PATH}")
    target_compile_definitions(SamplarPlugin PRIVATE
        "SAMPLAR_DEV_RESOURCE_PATH=\"${_SAMPLAR_DEV_PATH}\"")
endif()

if (SAMPLAR_ENABLE_TRACING)
    message(STATUS "SAMPLAR tracing enabled — Chrome Trace JSON profiler active")
    target_compile_definitions(SamplarPlugin PRIVATE SAMPLAR_ENABLE_TRACING=1)
    if (SAMPLAR_BUILD_STANDALONE)
        target_compile_definitions(SamplarApp PRIVATE SAMPLAR_ENABLE_TRACING=1)
    endif()
endif()

if(MSVC)
    target_compile_options(SamplarPlugin PRIVATE /arch:AVX2)
    target_compile_options(SamplarPlugin PRIVATE $<$<CONFIG:Release>:/fp:fast>)
    target_compile_definitions(SamplarPlugin PRIVATE __AVX__=1 __AVX2__=1)
    # Emit PDB symbols for Release builds so DAW crash dumps resolve to
    # function names. /Z7 embeds debug info inside each .obj (avoids the PDB
    # write-race that /Zi triggers with parallel compilation in JUCE plugins).
    # /DEBUG:FULL then produces a single .pdb at link-time.
    # /OPT:REF + /OPT:ICF keep the Release-mode dead-code stripping.
    target_compile_options(SamplarPlugin PRIVATE $<$<CONFIG:Release>:/Z7>)
    target_link_options(SamplarPlugin PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /MAP /MAPINFO:EXPORTS>)
    # Apply the same to the VST3 wrapper target. Without this, the final .vst3
    # DLL has no embedded CodeView record ? crash-log offsets can't be
    # symbolicated against the SharedCode PDB (different address space).
    if(TARGET SamplarPlugin_VST3)
      target_link_options(SamplarPlugin_VST3 PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /MAP /MAPINFO:EXPORTS>)
    endif()
endif()

target_link_libraries(SamplarPlugin PRIVATE SamplarResources)

# ----------------------------------------------------------------------
# Delay-load onnxruntime.dll on Windows
# Windows ships a 2,832-byte WinML forwarder stub at System32\onnxruntime.dll
# (owned by TrustedInstaller). When a DAW LoadLibrary()'s our .vst3, the
# default DLL search hits that stub first and fails with ERROR_BAD_EXE_FORMAT.
# Solution: delay-load onnxruntime.dll, and our custom failure hook in
# src/utils/OnnxDelayLoad.cpp manually LoadLibraryEx's the real DLL from the
# .vst3's own directory. See that file for details.
# ----------------------------------------------------------------------
if(WIN32 AND MSVC)
    target_sources(SamplarPlugin PRIVATE src/utils/OnnxDelayLoad.cpp)
    if(TARGET SamplarPlugin_VST3)
        target_link_options(SamplarPlugin_VST3 PRIVATE
            /DELAYLOAD:onnxruntime.dll
            # Force-include the TLS callback, delay-load hooks, and beacon
            # at the FINAL link step. Per-TU pragmas don't propagate from
            # the static lib to the final binary, so we have to repeat them
            # here on the executable target.
            /INCLUDE:_tls_used
            /INCLUDE:samplar_tls_callback_ptr
            /INCLUDE:__pfnDliNotifyHook2
            /INCLUDE:__pfnDliFailureHook2)
        target_link_libraries(SamplarPlugin_VST3 PRIVATE delayimp)
    endif()
    if(TARGET SamplarApp)
        target_link_options(SamplarApp PRIVATE
            /DELAYLOAD:onnxruntime.dll
            /INCLUDE:_tls_used
            /INCLUDE:samplar_tls_callback_ptr
            /INCLUDE:__pfnDliNotifyHook2
            /INCLUDE:__pfnDliFailureHook2)
        target_link_libraries(SamplarApp PRIVATE delayimp)
    endif()
endif()

# VST3 bundle staging: copies ALL runtime resources INTO the .vst3 bundle's
# Contents/x86_64-win/ directory ($VstDir). Result is a fully self-contained
# bundle that the Inno installer just drops into Program Files\Common\VST3.
#
# Bundle layout produced:
#   Samplar.vst3/Contents/x86_64-win/
#     SAMPLAR.vst3        (binary - JUCE places this)
#     SAMPLAR.map         (debug map)
#     onnxruntime.dll     (delay-loaded by TLS callback)
#     models/             (htdemucs.onnx + beat_this.onnx)
#     data/               (pattern JSONs)
#     BACKGROUNDS/        (UI animations + tilesets)
#     Samples/            (808 + GM kit)
#     tools/ffmpeg.exe    (audio decode for non-WAV input)
#
# Failures are non-fatal so locked files (DAW holding the VST3) don't kill
# the build. The Inno installer is the canonical distribution path.
set(_SAMPLAR_VST3_STAGE_SCRIPT "${CMAKE_BINARY_DIR}/samplar_vst3_stage.ps1")
file(WRITE  "${_SAMPLAR_VST3_STAGE_SCRIPT}" "param([string]$VstDir, [string]$RepoRoot)\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$ErrorActionPreference = 'SilentlyContinue'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "function MakeDir([string]$d) { if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null } }\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "function CopyFile([string]$src, [string]$dst) { if (Test-Path -LiteralPath $src) { try { MakeDir (Split-Path -Parent $dst); Copy-Item -Force -LiteralPath $src -Destination $dst } catch { Write-Host \"[stage] file skip: $src\" } } }\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "function CopyTree([string]$src, [string]$dst) { if (Test-Path -LiteralPath $src) { try { MakeDir $dst; Get-ChildItem -LiteralPath $src | ForEach-Object { Copy-Item -Force -Recurse -LiteralPath $_.FullName -Destination $dst } } catch { Write-Host \"[stage] tree skip: $src\" } } }\n")
# 1) onnxruntime.dll alongside the .vst3 binary (required for TLS preload)
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$ortHit = Get-ChildItem -Path (Join-Path $RepoRoot 'build/_deps') -Recurse -Filter 'onnxruntime.dll' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'onnxruntime_prebuilt-src' } | Select-Object -First 1\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "if ($ortHit) { CopyFile $ortHit.FullName (Join-Path $VstDir 'onnxruntime.dll') }\n")
# 2) models/ - htdemucs.onnx + beat_this.onnx
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$modelsDst = Join-Path $VstDir 'models'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "MakeDir $modelsDst\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile (Join-Path $RepoRoot 'models/htdemucs.onnx')          (Join-Path $modelsDst 'htdemucs.onnx')\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile (Join-Path $RepoRoot 'third_party/beat_this/models/beat_this.onnx') (Join-Path $modelsDst 'beat_this.onnx')\n")
# 3) data/ - pattern JSONs (multiple candidate source locations)
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$dataDst = Join-Path $VstDir 'data'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "MakeDir $dataDst\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "foreach ($p in @('data/patterns_with_rolls.json','SM5_Portable_2026-02-18/data/patterns_with_rolls.json','data/latin_patterns.json','SM5_Portable_2026-02-18/data/latin_patterns.json')) { $src = Join-Path $RepoRoot $p; if (Test-Path -LiteralPath $src) { try { Copy-Item -Force -LiteralPath $src -Destination $dataDst } catch { } } }\n")
# 4) BACKGROUNDS/
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyTree (Join-Path $RepoRoot 'BACKGROUNDS') (Join-Path $VstDir 'BACKGROUNDS')\n")
# 5) Samples/
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyTree (Join-Path $RepoRoot 'Samples') (Join-Path $VstDir 'Samples')\n")
# 6) tools/ffmpeg.exe (try multiple candidate locations)
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$toolsDst = Join-Path $VstDir 'tools'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "MakeDir $toolsDst\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "foreach ($p in @('portable_cache/ffmpeg/ffmpeg.exe','SAMPLAR_Portable/python/Scripts/ffmpeg.exe')) { $src = Join-Path $RepoRoot $p; if (Test-Path -LiteralPath $src) { CopyFile $src (Join-Path $toolsDst 'ffmpeg.exe'); break } }\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "exit 0\n")

if(WIN32)
    add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
      COMMAND powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "${_SAMPLAR_VST3_STAGE_SCRIPT}" -VstDir "$<TARGET_FILE_DIR:SamplarPlugin_VST3>" -RepoRoot "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMENT "Staging resources INTO Samplar.vst3 bundle (self-contained for Inno installer)"
    )
endif()

# -----------------------------------------------------------------------
# macOS bundle staging: copies libonnxruntime.dylib, models, BACKGROUNDS,
# Samples, and ffmpeg INTO each plugin bundle's Contents/Resources/.
# Layout (per format):
#   SAMPLAR.vst3/Contents/Resources/
#   SAMPLAR.component/Contents/Resources/
#   SAMPLAR.app/Contents/Resources/
# Driven by scripts/mac/stage_bundle.sh — failures are non-fatal so a DAW
# holding the bundle open during incremental rebuilds doesn't kill the build.
# -----------------------------------------------------------------------
if(APPLE)
    set(_SAMPLAR_MAC_STAGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mac/stage_bundle.sh")
    foreach(_fmt VST3 AU Standalone)
        set(_tgt "SamplarPlugin_${_fmt}")
        if(TARGET ${_tgt})
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND /bin/bash "${_SAMPLAR_MAC_STAGE_SCRIPT}"
                        "$<TARGET_BUNDLE_DIR:${_tgt}>"
                        "${CMAKE_CURRENT_SOURCE_DIR}"
                        "${ORT_ROOT}"
                COMMENT "Staging resources INTO ${_tgt} bundle"
                VERBATIM)
        endif()
    endforeach()
endif()
# -----------------------------------------------------------------------

juce_add_console_app(SamplarLicenseCleaner PRODUCT_NAME "SAMPLAR License Cleaner")
target_sources(SamplarLicenseCleaner PRIVATE
  tools/license_cleaner/LicenseCleaner.cpp
  src/security/LicenseCleanerUtility.cpp
)
target_link_libraries(SamplarLicenseCleaner PRIVATE juce::juce_core)
target_include_directories(SamplarLicenseCleaner PRIVATE src)

# -----------------------------------------------------------------------
# SamplarTests � Optional unit test runner (enable with -DSAMPLAR_BUILD_TESTS=ON)
# -----------------------------------------------------------------------
if (SAMPLAR_BUILD_TESTS)
    juce_add_console_app(SamplarTests PRODUCT_NAME "SAMPLAR Tests")
    target_sources(SamplarTests PRIVATE
        tests/main.cpp
        src/app/EventBus.cpp
    )
    target_link_libraries(SamplarTests PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
    )
    target_include_directories(SamplarTests PRIVATE src)
    target_compile_features(SamplarTests PRIVATE cxx_std_17)
endif()



