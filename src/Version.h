// src/Version.h
// Single source of truth for the SAMPLAR version string.
// Keep in sync with installer_sm5.iss MyAppVersion and the release git tag.
//
// Consumed by: OAuthBrowserAuthenticator (sent on every OAuth request to the
// license server for version-gating).
//
// TODO: unify with CMake project(VERSION ...) via configure_file so there is
// truly only ONE place to bump the version. Currently CMakeLists.txt says
// VERSION 2.0.0 which is stale. Fix that separately.
#pragma once

namespace samplar {
    inline constexpr const char* kAppVersion = "1.2.2";
}
