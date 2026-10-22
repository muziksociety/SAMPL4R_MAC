# -----------------------------------------------------------------------------
# Licensing workstream (included at the end of CMakeLists.txt, after extra_qa).
#
# Adds the licensing state-machine tests to SamplarTests (defined in
# extra_qa.cmake, -DSAMPLAR_BUILD_TESTS=ON) together with the licensing sources
# they exercise. SAMPLAR_SELFTEST=1 there: no network, no browser, no timer,
# LicenseSignature::testKeyOverride() available.
#   SamplarTests.exe "Licensing hardening"
# -----------------------------------------------------------------------------

if(SAMPLAR_BUILD_TESTS AND TARGET SamplarTests)
  set(_SAMPLAR_LIC_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
  get_filename_component(_SAMPLAR_LIC_ROOT "${_SAMPLAR_LIC_ROOT}" ABSOLUTE)

  target_sources(SamplarTests PRIVATE
    "${_SAMPLAR_LIC_ROOT}/tests/licensing/LicensingHardeningTests.cpp"
    "${_SAMPLAR_LIC_ROOT}/tests/licensing/ContractV2Tests.cpp"
    "${_SAMPLAR_LIC_ROOT}/src/services/LicenseSystemService.cpp"
    "${_SAMPLAR_LIC_ROOT}/src/security/SecureSessionManager.cpp"
    "${_SAMPLAR_LIC_ROOT}/src/networking/LicenseServerClient.cpp"
    "${_SAMPLAR_LIC_ROOT}/src/networking/OAuthBrowserAuthenticator.cpp")
endif()
