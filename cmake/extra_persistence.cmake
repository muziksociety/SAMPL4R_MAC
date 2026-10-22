# -----------------------------------------------------------------------
# Persistence workstream (DAW project round trip: A4 / B8 / B9 / A16-A19).
# Owned by the persistence workstream; included OPTIONAL at the end of
# CMakeLists.txt (after extra_qa.cmake, so SamplarTests already exists).
#
#  - Adds the S-Mode session codec (src/services/smode/SModeSession.*) to the
#    plugin (and SamplarApp if built).
#  - SamplarTests (-DSAMPLAR_BUILD_TESTS=ON): session serialize -> deserialize
#    equality, tolerant reader, the "save before restore keeps the pending
#    session" rule, AppState off-thread deferral / superseded nodes.
#      cmake -B build_persist -G "Visual Studio 17 2022" -A x64 -DSAMPLAR_BUILD_TESTS=ON
#      cmake --build build_persist --config Release --target SamplarTests --parallel
#      build_persist/SamplarTests_artefacts/Release/SamplarTests.exe Session AppState
# -----------------------------------------------------------------------

set(_SAMPLAR_PERSIST_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(_SAMPLAR_PERSIST_ROOT "${_SAMPLAR_PERSIST_ROOT}" ABSOLUTE)

set(SAMPLAR_PERSIST_SOURCES
  ${_SAMPLAR_PERSIST_ROOT}/src/services/smode/SModeSession.h
  ${_SAMPLAR_PERSIST_ROOT}/src/services/smode/SModeSession.cpp
)

foreach(_tgt SamplarPlugin SamplarApp)
  if(TARGET ${_tgt})
    target_sources(${_tgt} PRIVATE ${SAMPLAR_PERSIST_SOURCES})
  endif()
endforeach()

if(TARGET SamplarTests)
  target_sources(SamplarTests PRIVATE
    ${SAMPLAR_PERSIST_SOURCES}
    ${_SAMPLAR_PERSIST_ROOT}/tests/smode/SModeSessionTests.cpp
    ${_SAMPLAR_PERSIST_ROOT}/tests/app/AppStatePersistenceTests.cpp
  )
endif()
