# -----------------------------------------------------------------------
# Analysis workstream (tempo / downbeat / onset / key). Owned by the
# analysis workstream; included OPTIONAL at the end of CMakeLists.txt.
#
#  - Adds the new analysis sources to the plugin (and SamplarApp if built).
#  - AnalysisTests: EXCLUDE_FROM_ALL console target with synthetic
#    regression tests + an optional real-stem benchmark:
#      cmake --build build_analysis --config Release --target AnalysisTests --parallel
#      build_analysis/AnalysisTests_artefacts/Release/AnalysisTests.exe [--stem <file>] [--legacy]
# -----------------------------------------------------------------------

set(SAMPLAR_ANALYSIS_EXTRA_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/../src/services/analysis/AnalysisDsp.h
  ${CMAKE_CURRENT_LIST_DIR}/../src/services/analysis/AnalysisDsp.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/services/analysis/KeyDetector.h
  ${CMAKE_CURRENT_LIST_DIR}/../src/services/analysis/KeyDetector.cpp
)
foreach(_f IN LISTS SAMPLAR_ANALYSIS_EXTRA_SOURCES)
  if(NOT EXISTS "${_f}")
    list(REMOVE_ITEM SAMPLAR_ANALYSIS_EXTRA_SOURCES "${_f}")
  endif()
endforeach()

if(TARGET SamplarPlugin AND SAMPLAR_ANALYSIS_EXTRA_SOURCES)
  target_sources(SamplarPlugin PRIVATE ${SAMPLAR_ANALYSIS_EXTRA_SOURCES})
endif()
if(TARGET SamplarApp AND SAMPLAR_ANALYSIS_EXTRA_SOURCES)
  target_sources(SamplarApp PRIVATE ${SAMPLAR_ANALYSIS_EXTRA_SOURCES})
endif()

set(_SAMPLAR_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

juce_add_console_app(AnalysisTests PRODUCT_NAME "AnalysisTests")
set_target_properties(AnalysisTests PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
target_sources(AnalysisTests PRIVATE
  ${_SAMPLAR_ROOT}/src/services/analysis/tests/AnalysisTests.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/TempoAnalyzer.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/DWTTempoDetector.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/HybridTempoDetector.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/BeatThisDetector.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/OctaveCorrection.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/TempoRefiner.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/DownbeatDetector.cpp
  ${_SAMPLAR_ROOT}/src/services/smode/SpectralFluxDetector.cpp
  ${SAMPLAR_ANALYSIS_EXTRA_SOURCES}
)
target_include_directories(AnalysisTests PRIVATE ${_SAMPLAR_ROOT}/src)
target_compile_definitions(AnalysisTests PRIVATE
  JUCE_USE_CURL=0
  JUCE_WEB_BROWSER=0
  SAMPLAR_REPO_ROOT="${_SAMPLAR_ROOT}")
target_link_libraries(AnalysisTests PRIVATE
  juce::juce_core
  juce::juce_events
  juce::juce_data_structures
  juce::juce_audio_basics
  juce::juce_audio_formats
  juce::juce_dsp
  juce::juce_recommended_config_flags)
if(WITH_BEAT_THIS)
  target_link_libraries(AnalysisTests PRIVATE beat_this_engine)
  target_compile_definitions(AnalysisTests PRIVATE WITH_BEAT_THIS=1)
  add_custom_command(TARGET AnalysisTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "$<TARGET_FILE_DIR:AnalysisTests>/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnalysisTests>/third_party/beat_this/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEAT_THIS_DIR}/models/beat_this.onnx"
            "$<TARGET_FILE_DIR:AnalysisTests>/third_party/beat_this/models/beat_this.onnx"
    COMMENT "Copying ONNX Runtime + beat_this.onnx next to AnalysisTests")
  if(SAMPLAR_HAS_DIRECTML)
    add_custom_command(TARGET AnalysisTests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_ROOT}/lib/DirectML.dll" "$<TARGET_FILE_DIR:AnalysisTests>/")
  endif()
endif()

# -----------------------------------------------------------------------
# Separation / analysis hardening (impl_sep2):
#  - ORT_API_MANUAL_INIT on EVERY consumer of the ORT C++ API (audit S-H2). Without it the
#    header's static initialiser calls OrtGetApiBase() during DLL init, so a missing
#    onnxruntime.dll raises the delay-load exception inside the host's LoadLibrary and kills
#    the DAW. With it, src/services/separation/OrtRuntime.cpp binds the API lazily inside a
#    SEH guard. The definition must be identical in all TUs of a module (ODR on
#    Ort::Global<void>::api_), hence the INTERFACE property on the imported target that every
#    ORT user (plugin, app, beat_this_engine, probes, tests) links.
#  - OrtRuntime.cpp added to every target that compiles ORT-using sources.
# -----------------------------------------------------------------------
if(TARGET onnxruntime)
  set_property(TARGET onnxruntime APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS ORT_API_MANUAL_INIT)
endif()

set(SAMPLAR_ORT_RUNTIME_SOURCES
  ${_SAMPLAR_ROOT}/src/services/separation/OrtRuntime.h
  ${_SAMPLAR_ROOT}/src/services/separation/OrtRuntime.cpp
  ${_SAMPLAR_ROOT}/src/services/separation/StemCacheLock.h
  ${_SAMPLAR_ROOT}/src/services/separation/StemCacheLock.cpp
  ${_SAMPLAR_ROOT}/src/services/analysis/AnalysisCancel.h)
foreach(_t SamplarPlugin SamplarApp SeparationProbe AnalysisTests)
  if(TARGET ${_t})
    target_sources(${_t} PRIVATE ${SAMPLAR_ORT_RUNTIME_SOURCES})
  endif()
endforeach()
# AnalysisTests uses GpuAdapterProbe (OrtRuntime logs the loaded DLL path) and the
# separation code for the NaN / cache / missing-runtime checks.
target_sources(AnalysisTests PRIVATE
  ${_SAMPLAR_ROOT}/src/services/analysis/tests/SepHardeningTests.cpp
  ${_SAMPLAR_ROOT}/src/services/separation/GpuAdapterProbe.cpp
  ${_SAMPLAR_ROOT}/src/services/separation/OnnxSeparationProvider.cpp
  ${_SAMPLAR_ROOT}/src/services/separation/HtdemucsIstft.cpp
  ${_SAMPLAR_ROOT}/src/services/FullSongService.cpp
  ${_SAMPLAR_ROOT}/src/app/EventBus.cpp)
if(TARGET samplar_pffft)
  target_link_libraries(AnalysisTests PRIVATE samplar_pffft)
endif()
target_link_libraries(AnalysisTests PRIVATE onnxruntime)
if(WIN32 AND MSVC)
  # Delay-load like the plugin, so a copy of AnalysisTests.exe without onnxruntime.dll starts
  # and exercises the "runtime unavailable" paths (--ort-missing-check).
  target_sources(AnalysisTests PRIVATE ${_SAMPLAR_ROOT}/src/utils/OnnxDelayLoad.cpp)
  target_link_options(AnalysisTests PRIVATE /DELAYLOAD:onnxruntime.dll /DELAYLOAD:d3d12.dll)
  target_link_libraries(AnalysisTests PRIVATE delayimp)
endif()
