// src/Version.h
// SAMPLAR version string. The ONLY place to bump the version is
// CMakeLists.txt `project(samplar VERSION x.y.z)`; it is generated into
// SamplarVersion.h (build/generated) by configure_file.
//
// Consumed by: OAuthBrowserAuthenticator (sent on every OAuth request to the
// license server for version-gating), UpdateCheckService, LicenseSystemService.
#pragma once

#include "SamplarVersion.h"

namespace samplar {
    inline constexpr const char* kAppVersion = SAMPLAR_VERSION;
}
