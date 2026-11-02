# -----------------------------------------------------------------------
# Engine workstream (realtime playback engine). Owned by the engine
# workstream; included OPTIONAL at the end of CMakeLists.txt.
#
#  - Adds the realtime engine sources to the plugin (and SamplarApp if built).
#  - Drops src/audio/SamplePreviewCache.cpp (deleted dead code, B6) from the
#    targets that CMakeLists.txt still lists it in.
#  - EngineTests: EXCLUDE_FROM_ALL console test of the engine WITHOUT the plugin
#      cmake --build build_engine --config Release --target EngineTests --parallel
#      build_engine/EngineTests_artefacts/Release/EngineTests.exe
# -----------------------------------------------------------------------

set(_SAMPLAR_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

set(SAMPLAR_ENGINE_SOURCES
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/EngineTypes.h
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/EngineSnapshot.h
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/RtFifo.h
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/PlaybackEngine.h
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/PlaybackEngine.cpp
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/OfflineRender.h
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/OfflineRender.cpp
)

function(_samplar_engine_drop_dead_sources tgt)
  get_target_property(_srcs ${tgt} SOURCES)
  if(_srcs)
    list(FILTER _srcs EXCLUDE REGEX "SamplePreviewCache\\.(cpp|h)$")
    set_property(TARGET ${tgt} PROPERTY SOURCES ${_srcs})
  endif()
endfunction()

foreach(_tgt SamplarPlugin SamplarApp)
  if(TARGET ${_tgt})
    _samplar_engine_drop_dead_sources(${_tgt})
    target_sources(${_tgt} PRIVATE ${SAMPLAR_ENGINE_SOURCES})
  endif()
endforeach()

# --- EngineTests ---------------------------------------------------------
juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
set_target_properties(EngineTests PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
target_sources(EngineTests PRIVATE
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/tests/EngineTests.cpp
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/PlaybackEngine.cpp
  ${_SAMPLAR_ENGINE_ROOT}/src/audio/engine/OfflineRender.cpp
  ${_SAMPLAR_ENGINE_ROOT}/src/services/smode/SModeSegmentModel.cpp
)
target_include_directories(EngineTests PRIVATE
  ${_SAMPLAR_ENGINE_ROOT}/src
  ${SIGNALSMITH_STRETCH_DIR}
  ${SIGNALSMITH_LINEAR_DIR}/include
)
target_compile_definitions(EngineTests PRIVATE
  JUCE_USE_CURL=0
  JUCE_WEB_BROWSER=0
  WITH_SIGNALSMITH_STRETCH=1
  JUCE_LOG_ASSERTIONS=1)
target_link_libraries(EngineTests PRIVATE
  juce::juce_core
  juce::juce_events
  juce::juce_graphics
  juce::juce_audio_basics
  juce::juce_recommended_config_flags)
if(MSVC)
  target_compile_options(EngineTests PRIVATE /arch:AVX /wd4244 /wd4267)
  target_compile_definitions(EngineTests PRIVATE __AVX__=1)
endif()
