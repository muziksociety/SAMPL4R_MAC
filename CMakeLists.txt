# SAMPLAR build:
#   Release / commercial:  cmake -B build -G "Visual Studio 17 2022" -A x64      (VST3 only — SamplarPlugin_VST3)
#   Dev with standalone:   cmake -B build -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_BUILD_STANDALONE=ON
#   macOS (VST3, universal2): scripts/mac/build.sh   (or: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release)

cmake_minimum_required(VERSION 3.20)

# macOS: default to universal2 (arm64 + x86_64) and macOS 11.0 (first Apple Silicon
# release; the ORT osx-universal2 build needs it). Must be set BEFORE project().
# Override with e.g. -DCMAKE_OSX_ARCHITECTURES=arm64 for a faster dev build.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

# C is enabled here for samplar_pffft: MSVC tolerated JUCE enabling it in its subdirectory, Clang/Ninja does not.
project(samplar VERSION 1.0.1 LANGUAGES C CXX)

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

# Single source of truth for the product version: project(... VERSION x.y.z)
# above. It flows into SamplarVersion.h (-> samplar::kAppVersion used by the
# update check + OAuth), the JUCE plugin/app metadata below, and
# release_assets/build_installer.ps1 (parses this file). The git tag above is
# informational only.
set(SAMPLAR_VERSION_STRING "${PROJECT_VERSION}")
message(STATUS "SAMPLAR product version: ${SAMPLAR_VERSION_STRING}")

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

include(FetchContent)

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
# -----------------------------------------------------------------------
# Beat This! neural beat tracker (vendored at third_party/beat_this/)
#   - ONNX Runtime fetched from GitHub releases (pre-built Windows x64)
#   - C++ inference wrapper compiled into a static library
#   - 83 MB ONNX model copied next to the exe at install time
# -----------------------------------------------------------------------
set(BEAT_THIS_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/beat_this")
# A Release build must never ship without the tracker, so a missing model is
# fatal there. Debug builds keep the old behaviour (warn and disable).
# Multi-config generators (Xcode, Visual Studio) pick the config at build time,
# so they get a guard target that fails any non-Debug build instead.
set(BEAT_THIS_MODEL_MISSING OFF)
if(WITH_BEAT_THIS)
  if(NOT EXISTS "${BEAT_THIS_DIR}/models/beat_this.onnx")
    get_property(_samplar_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT _samplar_multi_config AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
      message(FATAL_ERROR "WITH_BEAT_THIS=ON but ${BEAT_THIS_DIR}/models/beat_this.onnx not found. "
                          "A ${CMAKE_BUILD_TYPE} build must ship the beat tracker: restore the model, "
                          "or configure with -DWITH_BEAT_THIS=OFF on purpose.")
    endif()
    message(WARNING "WITH_BEAT_THIS=ON but ${BEAT_THIS_DIR}/models/beat_this.onnx not found. Disabling.")
    set(WITH_BEAT_THIS OFF)
    set(BEAT_THIS_MODEL_MISSING ON)
  endif()
endif()
if(BEAT_THIS_MODEL_MISSING)
  add_custom_target(beat_this_model_guard
    COMMAND ${CMAKE_COMMAND} -E echo "beat_this_model_guard: $<CONFIG> build requested WITH_BEAT_THIS but ${BEAT_THIS_DIR}/models/beat_this.onnx is missing"
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,true,false>
    VERBATIM)
endif()

# -----------------------------------------------------------------------
# ONNX Runtime - ONE build for the whole module (stem separation + Beat This!).
# Preferred: the DirectML build SM5 ships (Microsoft.ML.OnnxRuntime.DirectML 1.19.2 +
# Microsoft.AI.DirectML 1.15.1), vendored at third_party/onnxruntime-directml (gitignored
# binaries; fetch with `python tools/fetch_onnxruntime_directml.py` or copy from SM5).
# Fallback: the CPU 1.19.2 zip (same API level - never mix ORT versions in one module).
# -----------------------------------------------------------------------
option(SAMPLAR_ORT_DIRECTML "Use the DirectML build of ONNX Runtime (GPU stem separation)" ON)
set(SAMPLAR_ORT_DML_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/onnxruntime-directml")
set(SAMPLAR_HAS_DIRECTML FALSE)
set(ORT_VERSION "1.19.2")
if(WIN32 AND SAMPLAR_ORT_DIRECTML
   AND EXISTS "${SAMPLAR_ORT_DML_DIR}/include/dml_provider_factory.h"
   AND EXISTS "${SAMPLAR_ORT_DML_DIR}/lib/DirectML.dll"
   AND EXISTS "${SAMPLAR_ORT_DML_DIR}/lib/onnxruntime.dll")
  set(ORT_ROOT "${SAMPLAR_ORT_DML_DIR}")
  set(SAMPLAR_HAS_DIRECTML TRUE)
else()
  if(WIN32 AND SAMPLAR_ORT_DIRECTML)
    message(WARNING "DirectML ONNX Runtime not found at ${SAMPLAR_ORT_DML_DIR} - stem separation will be CPU-only. Run: python tools/fetch_onnxruntime_directml.py")
  endif()
  if(WIN32)
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")
  elseif(APPLE)
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-universal2-${ORT_VERSION}.tgz")
  else()
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz")
  endif()
  FetchContent_Declare(onnxruntime_cpu_prebuilt URL ${ORT_URL})
  FetchContent_MakeAvailable(onnxruntime_cpu_prebuilt)
  set(ORT_ROOT "${onnxruntime_cpu_prebuilt_SOURCE_DIR}")
endif()

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
  message(FATAL_ERROR "ONNX Runtime lib not found at ${ORT_LIB}")
endif()

add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
  IMPORTED_LOCATION "${ORT_DLL}"
  IMPORTED_IMPLIB "${ORT_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include")
if(SAMPLAR_HAS_DIRECTML)
  set_property(TARGET onnxruntime APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS SAMPLAR_HAS_DIRECTML=1)
  # Same bits SM5 ships (guards against a stale/wrong copy).
  file(SHA256 "${ORT_DLL}" _samplar_ort_sha)
  if(NOT _samplar_ort_sha STREQUAL "410de3464172dae2945a4663f274e34c59965bc8233b0411de16baab56dda1a7")
    message(WARNING "onnxruntime.dll is not SM5's ORT-DirectML 1.19.2 build (sha256 ${_samplar_ort_sha})")
  endif()
endif()
message(STATUS "ONNX Runtime ${ORT_VERSION}: ${ORT_ROOT} (DirectML: ${SAMPLAR_HAS_DIRECTML})")

# Separation model shipped in the bundle: exactly ONE (body model when present).
set(SAMPLAR_SEP_MODEL_NAME htdemucs.onnx)
set(SAMPLAR_SEP_STALE_MODEL htdemucs_body.onnx)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/models/htdemucs_body.onnx")
  set(SAMPLAR_SEP_MODEL_NAME htdemucs_body.onnx)
  set(SAMPLAR_SEP_STALE_MODEL htdemucs.onnx)
else()
  message(WARNING "models/htdemucs_body.onnx missing - the bundle ships the full model (CPU only). Copy it from SM5 resources/models or run tools/separation/split_htdemucs.py")
endif()
message(STATUS "Separation model staged into the VST3 bundle: ${SAMPLAR_SEP_MODEL_NAME}")

# --- PFFFT (vendored marton78 fork, BSD-style; LICENSE ships in the bundle) - SIMD FFT
# behind samplar::Fft (src/utils/Fft.h), used by the host iSTFT of the body model. Every
# exported symbol is renamed sm5_pffft_* via a force-included header (relative /FI: the
# repo path contains a space). ---
set(SAMPLAR_PFFFT_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/pffft")
add_library(samplar_pffft STATIC "${SAMPLAR_PFFFT_DIR}/src/pffft.c" "${SAMPLAR_PFFFT_DIR}/src/pffft_common.c")
target_include_directories(samplar_pffft PUBLIC "${SAMPLAR_PFFFT_DIR}/include" "${SAMPLAR_PFFFT_DIR}"
                                         PRIVATE "${SAMPLAR_PFFFT_DIR}/include/pffft")
set_target_properties(samplar_pffft PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(samplar_pffft PUBLIC PFFFT_STATIC_DEFINE PRIVATE PFFFT_ENABLE_NEON)
if(MSVC)
  target_compile_definitions(samplar_pffft PRIVATE _USE_MATH_DEFINES)
  # Same CRT as the plugin (/MD). C is enabled late (inside JUCE) and this target came out
  # with an empty RuntimeLibrary (= /MT) -> LNK4098 LIBCMT conflict in the plugin link.
  target_compile_options(samplar_pffft PRIVATE $<IF:$<CONFIG:Debug>,/MDd,/MD>)
  target_compile_options(samplar_pffft PRIVATE /FIsm5_pffft_prefix.h)
else()
  target_compile_options(samplar_pffft PRIVATE -include sm5_pffft_prefix.h)
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
  )
  target_link_libraries(beat_this_engine PUBLIC onnxruntime)
  target_compile_features(beat_this_engine PUBLIC cxx_std_17)
  if(MSVC)
    target_compile_options(beat_this_engine PRIVATE /wd4244 /wd4267 /wd4996)
  endif()
endif()

# Sources
set(SAMPLAR_SOURCES
  src/app/AudioDeviceService.cpp
  src/services/FullSongService.cpp
  # Native ONNX 4-stem separation (ported from SM5) — replaces Python/UVR path
  src/services/separation/OnnxSeparationProvider.h
  src/services/separation/OnnxSeparationProvider.cpp
  # GPU (DirectML) separation: adapter probe + host iSTFT for htdemucs_body.onnx (SM5 453e7733)
  src/services/separation/GpuAdapterProbe.h
  src/services/separation/GpuAdapterProbe.cpp
  src/services/separation/HtdemucsIstft.h
  src/services/separation/HtdemucsIstft.cpp
  src/utils/Fft.h
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
  src/security/LicenseSignature.h
  src/security/LicenseSignature.cpp
  src/security/LicenseSigningKey.h
  src/security/ClockGuard.h
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
  src/ui/licensing/LicenseGateOverlay.h
  src/ui/licensing/LicenseGateOverlay.cpp
  src/ui/Debug/DebugConsole.cpp
  src/ui/Style/SamplarLookAndFeel.cpp
  src/app/ServiceRegistry.cpp
  src/app/EventBus.cpp
  src/app/AppState.h
  src/app/AppState.cpp
  src/utils/Tracing.h
  src/utils/Tracing.cpp
  src/utils/DeferredRelease.h
  src/utils/DeferredRelease.cpp
  src/ui/Components/WaveDragArea.h
  src/ui/Components/WaveDragArea.cpp
  src/ui/Components/SettingsDialog.h
  src/ui/Components/SettingsDialog.cpp
  src/ui/Components/UpdateDialog.h
  src/ui/Components/UpdateDialog.cpp
  src/ui/Components/NotificationButton.h
  src/ui/Components/NotificationButton.cpp
  # S-Mode page and components
  src/ui/Pages/SModePage.h
  src/ui/Pages/SModePage.cpp
  src/ui/Components/smode/StemWaveformComponent.h
  src/ui/Components/smode/StemWaveformComponent.cpp
  src/ui/Components/smode/SequencerSlotComponent.h
  src/ui/Components/smode/SequencerSlotComponent.cpp
  src/ui/Components/smode/SequencerLaneComponent.h
  src/ui/Components/smode/SequencerLaneComponent.cpp
  src/ui/Components/smode/SModeMidiLearnOverlay.h
  src/ui/Components/smode/SModeMidiLearnOverlay.cpp
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
    VERSION "${SAMPLAR_VERSION_STRING}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_64.png"
)

target_compile_definitions(SamplarApp PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

target_sources(SamplarApp PRIVATE ${SAMPLAR_SOURCES} src/app/App.cpp)

target_compile_definitions(SamplarApp
  PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
)

# Performance optimizations: Enable SIMD (always) and fast math (Release only)
if(MSVC)
  # MSVC: AVX baseline (NOT AVX2) - AVX2 emits instructions that crash with
  # STATUS_ILLEGAL_INSTRUCTION on pre-Haswell CPUs (SM5 fix 5de0dee8).
  target_compile_options(SamplarApp PRIVATE /arch:AVX)
  target_compile_options(SamplarApp PRIVATE $<$<CONFIG:Release>:/fp:fast>)
  target_compile_definitions(SamplarApp PRIVATE __AVX__=1)
  message(STATUS "Enabled MSVC AVX optimizations")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # GCC/Clang: Enable AVX2 always, fast math only in Release
  target_compile_options(SamplarApp PRIVATE -mavx)
  target_compile_options(SamplarApp PRIVATE $<$<CONFIG:Release>:-ffast-math>)
  target_compile_definitions(SamplarApp PRIVATE __AVX__=1)
  message(STATUS "Enabled GCC/Clang AVX optimizations")
endif()

target_include_directories(SamplarApp PRIVATE src)
target_include_directories(SamplarApp PRIVATE ${CMAKE_BINARY_DIR}/generated)

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
    onnxruntime
    samplar_pffft
)

# Beat This! integration on SamplarApp
if(BEAT_THIS_MODEL_MISSING)
  add_dependencies(SamplarApp beat_this_model_guard)
endif()
if(WITH_BEAT_THIS)
  target_link_libraries(SamplarApp PRIVATE beat_this_engine)
  target_compile_definitions(SamplarApp PRIVATE WITH_BEAT_THIS=1)
  target_include_directories(SamplarApp PRIVATE ${BEAT_THIS_DIR}/src)
endif()
if(ORT_DLL)  # ONNX Runtime (+ DirectML) runtime next to the exe - separation needs it too

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
endif()

if(WITH_BEAT_THIS)
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

# Copy data/ (install-location marker for DataPaths) to output directory
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

endif() # SAMPLAR_BUILD_STANDALONE — end of SamplarApp standalone target block

# -----------------------------------------------------------------------
# SamplarResources — shared juce_add_binary_data target (used by both
# SamplarApp [when enabled] and SamplarPlugin VST3). Always built.
# -----------------------------------------------------------------------
# Embedded static resources
juce_add_binary_data(SamplarResources SOURCES
  # In-app SAMPLAR logo (top bar). 800x525 PNG (~200 KB), generated from
  # logo.png at the repo root via assets/generate_icon.py.
  ${CMAKE_CURRENT_LIST_DIR}/assets/samplar_logo.png

)

if (SAMPLAR_BUILD_STANDALONE)
    target_link_libraries(SamplarApp PRIVATE SamplarResources)
endif()


# -----------------------------------------------------------------------
# SamplarPlugin — VST3 + Standalone plugin target (Phase 1 VST3 conversion)
# -----------------------------------------------------------------------
# Apple bundle settings. No AU: there is no Apple developer account (MM-024).
# Ad-hoc signed, so the hardened runtime stays off until a Developer ID is wired.
set(SAMPLAR_PLUGIN_BUNDLE_ID "com.muziksociety.samplar.vst3")
set(SAMPLAR_PLUGIN_APPLE_ARGS)
if(APPLE)
    set(SAMPLAR_PLUGIN_BUNDLE_ID "com.muziksociety.samplar")
    set(SAMPLAR_PLUGIN_APPLE_ARGS
        HARDENED_RUNTIME_ENABLED FALSE
        MICROPHONE_PERMISSION_ENABLED FALSE)
endif()

juce_add_plugin(SamplarPlugin
    PRODUCT_NAME "SAMPLAR"
    COMPANY_NAME "Muziksociety"
    COMPANY_WEBSITE "https://samplemonsta.com/samplar"
    COMPANY_EMAIL "muziksociety@gmail.com"
    BUNDLE_ID "${SAMPLAR_PLUGIN_BUNDLE_ID}"
    VERSION "${SAMPLAR_VERSION_STRING}"
    PLUGIN_MANUFACTURER_CODE Mzks
    PLUGIN_CODE Smpl
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    FORMATS VST3                       # VST3 only for now; standalone stays as SamplarApp
    VST3_CATEGORIES "Instrument" "Sampler" "Drum"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/samplar_icon_64.png"
    ${SAMPLAR_PLUGIN_APPLE_ARGS}
)

# Plugin sources: shared SAMPLAR_SOURCES plus plugin entry points (App.cpp is SamplarApp-only)
target_sources(SamplarPlugin PRIVATE
    ${SAMPLAR_SOURCES}
    src/plugin/SamplarAudioProcessor.cpp
    src/plugin/SamplarAudioProcessorEditor.cpp
    src/plugin/GpuTeardownOffload.cpp
    src/plugin/AsyncFileLogger.cpp
)

# No modal loops in the plugin: a modal loop inside a DAW can freeze the host
# (the licence sign-in is an in-window overlay now, SM5 b0526074).
target_compile_definitions(SamplarPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(SamplarPlugin PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
)

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
        onnxruntime
        samplar_pffft
)

if(BEAT_THIS_MODEL_MISSING)
    add_dependencies(SamplarPlugin beat_this_model_guard)
endif()
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
    # AVX baseline, not AVX2: the plugin runs inside the host process, so an
    # AVX2 instruction on an older CPU takes the whole DAW down.
    target_compile_options(SamplarPlugin PRIVATE /arch:AVX)
    target_compile_options(SamplarPlugin PRIVATE $<$<CONFIG:Release>:/fp:fast>)
    target_compile_definitions(SamplarPlugin PRIVATE __AVX__=1)
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
    if(TARGET SamplarApp)
        target_sources(SamplarApp PRIVATE src/utils/OnnxDelayLoad.cpp)
    endif()
    if(TARGET SamplarPlugin_VST3)
        target_link_options(SamplarPlugin_VST3 PRIVATE
            /DELAYLOAD:onnxruntime.dll
            # d3d12.dll is only needed by the GPU adapter probe (GpuAdapterProbe.cpp):
            # keep it off the DAW plugin-scan path.
            /DELAYLOAD:d3d12.dll
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
            /DELAYLOAD:d3d12.dll
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
#     SAMPLAR.map         (debug map - dev only, excluded by the installer)
#     onnxruntime.dll     (ORT 1.19.2 DirectML build; delay-loaded, TLS preload)
#     DirectML.dll        (1.15.1; loaded by full path before the first GPU session)
#     models/             (htdemucs_body.onnx + beat_this.onnx)
#   Samplar.vst3/Contents/Resources/licenses/  (ORT, DirectML, PFFFT notices)
#     data/               (samplar_bundle.marker - install-location marker)
#
# Failures are non-fatal so locked files (DAW holding the VST3) don't kill
# the build. The Inno installer is the canonical distribution path.
set(_SAMPLAR_VST3_STAGE_SCRIPT "${CMAKE_BINARY_DIR}/samplar_vst3_stage.ps1")
file(WRITE  "${_SAMPLAR_VST3_STAGE_SCRIPT}" "param([string]$VstDir, [string]$RepoRoot)\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$ErrorActionPreference = 'SilentlyContinue'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "function MakeDir([string]$d) { if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null } }\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "function CopyFile([string]$src, [string]$dst) { if (Test-Path -LiteralPath $src) { try { MakeDir (Split-Path -Parent $dst); Copy-Item -Force -LiteralPath $src -Destination $dst } catch { Write-Host \"[stage] file skip: $src\" } } }\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "function CopyTree([string]$src, [string]$dst) { if (Test-Path -LiteralPath $src) { try { MakeDir $dst; Get-ChildItem -LiteralPath $src | ForEach-Object { Copy-Item -Force -Recurse -LiteralPath $_.FullName -Destination $dst } } catch { Write-Host \"[stage] tree skip: $src\" } } }\n")
# 1) ONNX Runtime alongside the .vst3 binary (required for TLS preload). Paths are baked
#    at configure time from the imported target - never globbed from build/_deps (a stale
#    CPU-only DLL left there would silently disable the GPU).
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${ORT_DLL}' (Join-Path $VstDir 'onnxruntime.dll')\n")
if(SAMPLAR_HAS_DIRECTML)
  file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${ORT_ROOT}/lib/DirectML.dll' (Join-Path $VstDir 'DirectML.dll')\n")
else()
  file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$p = Join-Path $VstDir 'DirectML.dll'; if (Test-Path -LiteralPath $p) { Remove-Item -Force -LiteralPath $p }\n")
endif()
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$p = Join-Path $VstDir 'onnxruntime_providers_shared.dll'; if (Test-Path -LiteralPath $p) { Remove-Item -Force -LiteralPath $p }\n")
# 2) models/ - exactly ONE separation model + beat_this.onnx (the other model is removed so
#    a stale 290 MB htdemucs.onnx never ships next to htdemucs_body.onnx)
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$modelsDst = Join-Path $VstDir 'models'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "MakeDir $modelsDst\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile (Join-Path $RepoRoot 'models/${SAMPLAR_SEP_MODEL_NAME}') (Join-Path $modelsDst '${SAMPLAR_SEP_MODEL_NAME}')\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$p = Join-Path $modelsDst '${SAMPLAR_SEP_STALE_MODEL}'; if (Test-Path -LiteralPath $p) { Remove-Item -Force -LiteralPath $p }\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile (Join-Path $RepoRoot 'third_party/beat_this/models/beat_this.onnx') (Join-Path $modelsDst 'beat_this.onnx')\n")
# 3) data/ - install-location marker only. SamplarAudioProcessor / DataPaths
#    detect the resource root by the presence of data/; the old pattern JSONs
#    (56 MB), tools/ffmpeg.exe (148 MB), BACKGROUNDS/ (148 MB) and Samples/
#    (6 MB) had zero runtime references and are no longer staged. Remove any
#    copies left in the bundle by older builds.
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$dataDst = Join-Path $VstDir 'data'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "MakeDir $dataDst\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "Set-Content -LiteralPath (Join-Path $dataDst 'samplar_bundle.marker') -Value 'SAMPLAR resource root marker' -Encoding ascii\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "foreach ($stale in @('data/patterns_with_rolls.json','data/latin_patterns.json','tools','BACKGROUNDS','Samples','logs')) { $p = Join-Path $VstDir $stale; if (Test-Path -LiteralPath $p) { Remove-Item -Recurse -Force -LiteralPath $p } }\n")
# 4) third-party licenses (the DirectML redistributable license must ship with DirectML.dll)
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "$licDst = Join-Path (Split-Path -Parent $VstDir) 'Resources/licenses'\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "MakeDir $licDst\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${ORT_ROOT}/LICENSE' (Join-Path $licDst 'onnxruntime-LICENSE.txt')\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${ORT_ROOT}/ThirdPartyNotices.txt' (Join-Path $licDst 'onnxruntime-ThirdPartyNotices.txt')\n")
if(SAMPLAR_HAS_DIRECTML)
  file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${ORT_ROOT}/DirectML-LICENSE.txt' (Join-Path $licDst 'DirectML-LICENSE.txt')\n")
  file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${ORT_ROOT}/DirectML-ThirdPartyNotices.txt' (Join-Path $licDst 'DirectML-ThirdPartyNotices.txt')\n")
endif()
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "CopyFile '${SAMPLAR_PFFFT_DIR}/LICENSE.txt' (Join-Path $licDst 'pffft-LICENSE.txt')\n")
file(APPEND "${_SAMPLAR_VST3_STAGE_SCRIPT}" "exit 0\n")

if(WIN32)  # PowerShell staging is Windows-only; macOS uses scripts/mac/stage_bundle.sh below
  add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
    COMMAND powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "${_SAMPLAR_VST3_STAGE_SCRIPT}" -VstDir "$<TARGET_FILE_DIR:SamplarPlugin_VST3>" -RepoRoot "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Staging resources INTO Samplar.vst3 bundle (self-contained for Inno installer)"
  )
endif()

# macOS bundle staging: same payload as the Windows script above, placed in
# SAMPLAR.vst3/Contents/Resources/ (libonnxruntime dylib, models/, data/ marker,
# licenses/). The script also rewrites the plugin's ORT reference to
# @loader_path and ad-hoc signs the bundle. Failures are non-fatal.
if(APPLE)
  add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mac/stage_bundle.sh"
            "$<TARGET_BUNDLE_DIR:SamplarPlugin_VST3>"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${ORT_DLL}"
            "${SAMPLAR_SEP_MODEL_NAME}"
            "${SAMPLAR_SEP_STALE_MODEL}"
            "${SAMPLAR_PFFFT_DIR}/LICENSE.txt"
    COMMENT "Staging resources INTO SAMPLAR.vst3 bundle (macOS)"
    VERBATIM)
endif()
# -----------------------------------------------------------------------

# -----------------------------------------------------------------------
# VST3SmokeHost - loads the built SAMPLAR VST3 like a DAW (no DAW needed):
# instantiate, process audio + MIDI, open/close/reopen the editor, save/restore
# state, unload; every step timed, exit 0 = all pass. --two-instances checks
# that one instance can be unloaded while another keeps running.
# NOT part of ALL: build on demand with --target VST3SmokeHost.
# (Ported from Sample Monsta 5.)
# -----------------------------------------------------------------------
juce_add_console_app(VST3SmokeHost PRODUCT_NAME "VST3SmokeHost")
set_target_properties(VST3SmokeHost PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
target_sources(VST3SmokeHost PRIVATE tools/vst3_smoke_host/VST3SmokeHost.cpp)
target_compile_definitions(VST3SmokeHost PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_USE_CURL=0
  JUCE_WEB_BROWSER=0
  VST3_SMOKE_DEFAULT_PLUGIN="${CMAKE_BINARY_DIR}/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3")
target_link_libraries(VST3SmokeHost PRIVATE
  juce::juce_audio_processors
  juce::juce_audio_utils
  juce::juce_gui_extra
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------
# SeparationProbe - headless check of FullSongService / OnnxSeparationProvider:
# CPU vs GPU (DirectML) timing, per-stem RMS, CPU/GPU/legacy SNR, mid-chunk cancel
# latency, peak working set, stem cache miss/hit. NOT part of ALL:
#   cmake --build build_dml --config Release --target SeparationProbe
#   build_dml/SeparationProbe_artefacts/Release/SeparationProbe.exe [song.wav] [--legacy]
# -----------------------------------------------------------------------
juce_add_console_app(SeparationProbe PRODUCT_NAME "SeparationProbe")
set_target_properties(SeparationProbe PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
target_sources(SeparationProbe PRIVATE
  tools/separation_probe/SeparationProbe.cpp
  src/services/FullSongService.cpp
  src/services/separation/OnnxSeparationProvider.cpp
  src/services/separation/HtdemucsIstft.cpp
  src/services/separation/GpuAdapterProbe.cpp
  src/app/EventBus.cpp)
target_include_directories(SeparationProbe PRIVATE src)
target_compile_definitions(SeparationProbe PRIVATE
  JUCE_USE_CURL=0
  JUCE_WEB_BROWSER=0
  SAMPLAR_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SeparationProbe PRIVATE
  juce::juce_core
  juce::juce_events
  juce::juce_data_structures
  juce::juce_audio_basics
  juce::juce_audio_formats
  juce::juce_dsp
  onnxruntime
  samplar_pffft
  juce::juce_recommended_config_flags)
add_custom_command(TARGET SeparationProbe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "$<TARGET_FILE_DIR:SeparationProbe>/"
  COMMENT "Copying ONNX Runtime next to SeparationProbe")
if(SAMPLAR_HAS_DIRECTML)
  add_custom_command(TARGET SeparationProbe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_ROOT}/lib/DirectML.dll" "$<TARGET_FILE_DIR:SeparationProbe>/"
    COMMENT "Copying DirectML next to SeparationProbe")
endif()

juce_add_console_app(SamplarLicenseCleaner PRODUCT_NAME "SAMPLAR License Cleaner")
target_sources(SamplarLicenseCleaner PRIVATE
  tools/license_cleaner/LicenseCleaner.cpp
  src/security/LicenseCleanerUtility.cpp
)
target_link_libraries(SamplarLicenseCleaner PRIVATE juce::juce_core)
target_include_directories(SamplarLicenseCleaner PRIVATE src)

# -----------------------------------------------------------------------
# SamplarTests - unit test runner (-DSAMPLAR_BUILD_TESTS=ON). Defined in
# cmake/extra_qa.cmake (included at the end of this file); the tests are
# NOT compiled into the shipping plugin.
# -----------------------------------------------------------------------




# -----------------------------------------------------------------------
# Per-workstream extra targets (tests / probes / tools). Each file is owned by
# one workstream so parallel work never edits this file at the same time.
# -----------------------------------------------------------------------
foreach(_samplar_extra engine analysis qa persistence ui licensing)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/extra_${_samplar_extra}.cmake" OPTIONAL)
endforeach()
