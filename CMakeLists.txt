cmake_minimum_required(VERSION 3.20)

project(samplar VERSION 0.1.0 LANGUAGES CXX)

option(SAMPLAR_BUILD_TESTS "Build SAMPLAR unit tests" OFF)

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
if(EXISTS "${LTE_DIR}/CMakeLists.txt")
  message(STATUS "========================================")
  message(STATUS "Loop Tempo Estimator: ENABLED")
  message(STATUS "========================================")
  # Disable tests and vamp plugin - we only need the library
  set(BUILD_TESTS OFF CACHE BOOL "Disable LTE tests" FORCE)
  set(BUILD_VAMP_PLUGIN OFF CACHE BOOL "Disable LTE vamp plugin" FORCE)
  add_subdirectory(${LTE_DIR} loop-tempo-estimator)
  set(LTE_FOUND TRUE)
else()
  message(STATUS "Loop Tempo Estimator not found at ${LTE_DIR}")
  set(LTE_FOUND FALSE)
endif()

# Optional: SoundTouch for constant-pitch preview
option(WITH_SOUNDTOUCH "Enable SoundTouch for constant-pitch time-stretch preview" ON)
option(WITH_ZEROMQ "Enable ZeroMQ IPC clients (Python bridge)" OFF)

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
  src/services/analysis/TempoCache.h
  src/services/analysis/TempoCache.cpp
  src/services/analysis/LoopDivider.h
  src/services/analysis/LoopDivider.cpp
  src/services/analysis/BeatAlignedSlicer.h
  src/services/analysis/BeatAlignedSlicer.cpp
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


juce_add_gui_app(SamplarApp
    PRODUCT_NAME "SAMPLAR"
    COMPANY_NAME "Muziksociety"
    COMPANY_WEBSITE "https://samplemonsta.com/samplar"
    COMPANY_EMAIL "muziksociety@gmail.com"
    BUNDLE_ID "com.muziksociety.samplar"
    VERSION "0.1.0"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/plugins/REF.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/plugins/REF.png"
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

# Embed static resources (e.g., backdrop.png) so images are always available
juce_add_binary_data(SamplarResources SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/backdrop.png
  # REF.png removed from BinaryData � 2.4 MB pushes MSVC over heap limit.
  # InitialPage loads it from AppData (DataPaths::getAppDataDirectory) at runtime.

  # Loading animation sprite sheet (11x11 grid of 400x172 frames, 121 frames total)
  ${CMAKE_CURRENT_LIST_DIR}/loading_spritesheet.png

  # NOTE: Snake/Flappy/Shooter game assets removed (Games feature stripped from SAMPLAR)
  # NOTE: All BACKGROUNDS/ files are NOT embedded here:
  #   TILESET.png        22 MB  ? ~132 MB C++ source
  #   LAVA_optimized.gif 12 MB  ? ~72 MB C++ source
  #   CAMPO_optimized.gif 6 MB  ? ~36 MB C++ source
  # Embedding them overflows MSVC's compiler heap (error C1060).
  # ProcessingPage.cpp and GifAnimatedBackground.cpp load them from the
  # BACKGROUNDS/ folder on the filesystem at runtime.
)

target_link_libraries(SamplarApp PRIVATE SamplarResources)


# -----------------------------------------------------------------------
# SamplarPlugin � VST3 + Standalone plugin target (Phase 1 VST3 conversion)
# -----------------------------------------------------------------------
juce_add_plugin(SamplarPlugin
    PRODUCT_NAME "SAMPLAR"
    COMPANY_NAME "Muziksociety"
    COMPANY_WEBSITE "https://samplemonsta.com/samplar"
    COMPANY_EMAIL "muziksociety@gmail.com"
    BUNDLE_ID "com.muziksociety.samplar.vst3"
    VERSION "0.1.0"
    PLUGIN_MANUFACTURER_CODE Mzks
    PLUGIN_CODE Smpl
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    FORMATS VST3                       # VST3 only for now; standalone stays as SamplarApp
    VST3_CATEGORIES "Instrument" "Sampler" "Drum"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/plugins/REF.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/plugins/REF.png"
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
)

if(WITH_SOUNDTOUCH AND SOUNDTOUCH_INCLUDE_DIR)
    target_include_directories(SamplarPlugin PRIVATE ${SOUNDTOUCH_INCLUDE_DIR})
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

if (SAMPLAR_ENABLE_DEV_FALLBACK)
    set(_SAMPLAR_DEV_PATH "${CMAKE_SOURCE_DIR}/SAMPLAR_Portable")
    message(STATUS "SAMPLAR DEV fallback enabled � baking resource path: ${_SAMPLAR_DEV_PATH}")
    target_compile_definitions(SamplarPlugin PRIVATE
        "SAMPLAR_DEV_RESOURCE_PATH=\"${_SAMPLAR_DEV_PATH}\"")
endif()

if (SAMPLAR_ENABLE_TRACING)
    message(STATUS "SAMPLAR tracing enabled � Chrome Trace JSON profiler active")
    target_compile_definitions(SamplarPlugin PRIVATE SAMPLAR_ENABLE_TRACING=1)
    target_compile_definitions(SamplarApp    PRIVATE SAMPLAR_ENABLE_TRACING=1)
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

# Copy data folder (pattern databases) next to VST3 bundle for plugin mode
# Also copy to %APPDATA%/Samplar/data for reliable VST3 access
add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SamplarPlugin_VST3>/../../../data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/data/patterns_with_rolls.json"
    "$<TARGET_FILE_DIR:SamplarPlugin_VST3>/../../../data/patterns_with_rolls.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/data/latin_patterns.json"
    "$<TARGET_FILE_DIR:SamplarPlugin_VST3>/../../../data/latin_patterns.json"
  COMMENT "Copying pattern data files next to VST3 bundle"
)

# Copy BACKGROUNDS folder to AppData for VST3 mode
# DataPaths::getBackgroundsDirectory() falls back to %APPDATA%/Samplar/BACKGROUNDS
add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{APPDATA}/Samplar/BACKGROUNDS"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/BACKGROUNDS"
    "$ENV{APPDATA}/Samplar/BACKGROUNDS"
  COMMENT "Copying BACKGROUNDS to AppData for VST3 mode"
)

# Copy Samples/ folder to AppData for VST3 mode (808 preview kit + GM samples)
# GMSampleGenerator::loadWavFile() checks %APPDATA%/Samplar/Samples/ as VST3 fallback.
add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{APPDATA}/Samplar/Samples"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Samples"
    "$ENV{APPDATA}/Samplar/Samples"
  COMMENT "Copying Samples (808 kit + GM) to AppData for VST3 mode"
)
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



