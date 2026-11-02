# -----------------------------------------------------------------------------
# QA workstream (included at the end of CMakeLists.txt).
#
#   1. SamplarTests  - juce::UnitTest runner (-DSAMPLAR_BUILD_TESTS=ON).
#      Compiles ONLY the sources the tests exercise (never the whole plugin), with
#      SAMPLAR_SELFTEST=1 (test-only hooks: LicenseSignature::testKeyOverride,
#      ClockGuard stays off the real registry). Exit code != 0 on any failure.
#        cmake -B build_qa -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_BUILD_TESTS=ON
#        cmake --build build_qa --config Release --target SamplarTests --parallel
#        build_qa/SamplarTests_artefacts/Release/SamplarTests.exe [--list] [name-filter ...]
#      Also registered with CTest (ctest -C Release).
#
#   2. Release link hygiene: /PDBALTPATH (no build-machine path in the binary).
#   3. Third-party licence texts staged into the VST3 bundle
#      (SAMPLAR.vst3/Contents/Resources/licenses/) next to the ORT / DirectML /
#      PFFFT texts the main staging script already copies (audit D19).
# -----------------------------------------------------------------------------

set(_SAMPLAR_QA_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(_SAMPLAR_QA_ROOT "${_SAMPLAR_QA_ROOT}" ABSOLUTE)

# ---- 1. SamplarTests ----------------------------------------------------------------
if(SAMPLAR_BUILD_TESTS)
  enable_testing()

  juce_add_console_app(SamplarTests PRODUCT_NAME "SamplarTests")

  target_sources(SamplarTests PRIVATE
    tests/main.cpp
    tests/TestHelpers.h
    # framework
    tests/app/EventBusTests.cpp
    tests/app/ServiceRegistryTests.cpp
    tests/app/AppStateTests.cpp
    tests/utils/DataPathsTests.cpp
    # licensing / security
    tests/licensing/LicenseSystemTests.cpp
    tests/licensing/LicenseSignatureTests.cpp
    tests/licensing/ClockGuardTests.cpp
    # S-Mode model
    tests/smode/SModeSegmentModelTests.cpp
    # separation (host iSTFT of htdemucs_body.onnx; ported from SM5 tests/unit)
    tests/separation/HtdemucsIstftTests.cpp
    # sources under test
    src/app/EventBus.cpp
    src/app/ServiceRegistry.cpp
    src/app/AppState.cpp
    src/security/LicenseSignature.cpp
    src/security/LicenseCache.cpp
    src/security/HardwareFingerprintGenerator.cpp
    src/licensing/FeatureManager.cpp
    src/services/smode/SModeSegmentModel.cpp
    src/services/separation/HtdemucsIstft.cpp
  )

  target_include_directories(SamplarTests PRIVATE
    src
    tests
    ${CMAKE_BINARY_DIR}/generated)

  target_compile_definitions(SamplarTests PRIVATE
    SAMPLAR_SELFTEST=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1      # MessageManager::runDispatchLoopUntil (AppState late-drain test)
    SAMPLAR_TESTS_SOURCE_DIR="${_SAMPLAR_QA_ROOT}")

  target_link_libraries(SamplarTests PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_cryptography
    juce::juce_graphics
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
    samplar_pffft
    juce::juce_recommended_config_flags)

  target_compile_features(SamplarTests PRIVATE cxx_std_17)

  add_test(NAME SamplarTests COMMAND SamplarTests)
endif()

# ---- 2. Release hygiene of the shipped binary ---------------------------------------
# /DEBUG (Release PDBs for crash dumps) embeds the ABSOLUTE PDB path of the build machine
# (B:\SANTI\VST PROJECT\...\SAMPLAR.pdb) in SAMPLAR.vst3 - found by the release linter's
# hardcoded-path scan. /PDBALTPATH:%_PDB% stores only the file name; symbol servers and
# WinDbg match PDBs by GUID/age, so crash-dump symbolication is unchanged.
if(MSVC AND TARGET SamplarPlugin_VST3)
  target_link_options(SamplarPlugin_VST3 PRIVATE $<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>)
endif()

# ---- 3. Licence texts into the VST3 bundle (D19) -----------------------------------
if(TARGET SamplarPlugin_VST3)
  # JUCE in use: same choice as CMakeLists.txt (vendored ./JUCE if complete, else ../JUCE).
  if(EXISTS "${_SAMPLAR_QA_ROOT}/JUCE/CMakeLists.txt" AND EXISTS "${_SAMPLAR_QA_ROOT}/JUCE/modules/juce_gui_basics")
    set(_SAMPLAR_QA_JUCE "${_SAMPLAR_QA_ROOT}/JUCE")
  else()
    get_filename_component(_SAMPLAR_QA_JUCE "${_SAMPLAR_QA_ROOT}/../JUCE" ABSOLUTE)
  endif()

  set(_SAMPLAR_LIC_DIR "$<TARGET_FILE_DIR:SamplarPlugin_VST3>/../Resources/licenses")

  # destination name -> source file (all must exist; a missing one fails the build so a
  # release can never ship without its notices)
  set(_SAMPLAR_LIC_FILES
    "NOTICE.md|${_SAMPLAR_QA_ROOT}/NOTICE.md"
    "signalsmith-stretch-LICENSE.txt|${_SAMPLAR_QA_ROOT}/third_party/signalsmith-stretch/LICENSE.txt"
    "signalsmith-linear-LICENSE.txt|${_SAMPLAR_QA_ROOT}/third_party/signalsmith-linear/LICENSE.txt"
    "JUCE-LICENSE.md|${_SAMPLAR_QA_JUCE}/LICENSE.md"
    "JUCE-VST3_SDK-LICENSE.txt|${_SAMPLAR_QA_JUCE}/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt"
    "JUCE-zlib-README.txt|${_SAMPLAR_QA_JUCE}/modules/juce_core/zip/zlib/README"
    "JUCE-pnglib-LICENSE.txt|${_SAMPLAR_QA_JUCE}/modules/juce_graphics/image_formats/pnglib/LICENSE"
    "JUCE-jpeglib-README.txt|${_SAMPLAR_QA_JUCE}/modules/juce_graphics/image_formats/jpglib/README"
    "JUCE-harfbuzz-COPYING.txt|${_SAMPLAR_QA_JUCE}/modules/juce_graphics/fonts/harfbuzz/COPYING"
    "JUCE-sheenbidi-LICENSE.txt|${_SAMPLAR_QA_JUCE}/modules/juce_graphics/unicode/sheenbidi/LICENSE"
    "JUCE-FLAC-LICENSE.txt|${_SAMPLAR_QA_JUCE}/modules/juce_audio_formats/codecs/flac/Flac Licence.txt"
    "JUCE-OggVorbis-LICENSE.txt|${_SAMPLAR_QA_JUCE}/modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt"
  )
  if(WITH_BEAT_THIS)
    list(APPEND _SAMPLAR_LIC_FILES
      "beat_this_cpp-LICENSE.txt|${_SAMPLAR_QA_ROOT}/third_party/beat_this/LICENSE.txt"
      "pocketfft-LICENSE.md|${_SAMPLAR_QA_ROOT}/third_party/beat_this/third_party/pocketfft/LICENSE.md")
  endif()

  # Stale SM5-era pattern JSONs left in Contents/data/ by old builds: the main staging
  # script only cleans Contents/x86_64-win/data, and samplar.iss copies the whole bundle,
  # so they would ship (56 MB, zero runtime readers). Nothing reads Contents/data.
  set(_SAMPLAR_BUNDLE_CONTENTS "$<TARGET_FILE_DIR:SamplarPlugin_VST3>/..")
  set(_samplar_lic_cmds
    COMMAND ${CMAKE_COMMAND} -E rm -f "${_SAMPLAR_BUNDLE_CONTENTS}/data/patterns_with_rolls.json"
                                      "${_SAMPLAR_BUNDLE_CONTENTS}/data/latin_patterns.json"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_SAMPLAR_LIC_DIR}")
  foreach(_entry IN LISTS _SAMPLAR_LIC_FILES)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _dst)
    list(GET _pair 1 _src)
    if(NOT EXISTS "${_src}")
      message(FATAL_ERROR "[extra_qa] licence text missing (D19): ${_src}")
    endif()
    list(APPEND _samplar_lic_cmds COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_SAMPLAR_LIC_DIR}/${_dst}")
  endforeach()

  # macOS: this step runs after scripts/mac/stage_bundle.sh has codesigned the bundle, so a
  # newly copied licence text breaks the seal. Re-sign ad hoc after the copies.
  if(APPLE)
    list(APPEND _samplar_lic_cmds
      COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:SamplarPlugin_VST3>")
  endif()

  add_custom_command(TARGET SamplarPlugin_VST3 POST_BUILD
    ${_samplar_lic_cmds}
    COMMENT "Staging third-party licence texts into SAMPLAR.vst3/Contents/Resources/licenses (+ stale data cleanup)"
    VERBATIM)
endif()
