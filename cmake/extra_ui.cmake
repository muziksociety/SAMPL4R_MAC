# -----------------------------------------------------------------------
# UI workstream. Owned by the UI workstream; included OPTIONAL at the end of
# CMakeLists.txt (foreach extra ... ui).
#
#  - Adds new UI sources to the plugin (and SamplarApp if built):
#      ChopInspectorStrip  - per-chop inspector row in SModePage (UX gap 4)
#  - Header-only additions need no entry: SModeUiSelfTest.h,
#    src/audio/engine/SegmentFx.h.
# -----------------------------------------------------------------------

set(SAMPLAR_UI_EXTRA_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/../src/ui/Components/smode/ChopInspectorStrip.h
  ${CMAKE_CURRENT_LIST_DIR}/../src/ui/Components/smode/ChopInspectorStrip.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/audio/engine/SegmentFx.h
)

foreach(_tgt SamplarPlugin SamplarApp)
  if(TARGET ${_tgt})
    target_sources(${_tgt} PRIVATE ${SAMPLAR_UI_EXTRA_SOURCES})
  endif()
endforeach()

# Lane grid rescale (review N-19): header-only, unit tested in SamplarTests.
if(TARGET SamplarTests)
  target_sources(SamplarTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../tests/smode/LaneGridRescaleTests.cpp)
endif()
