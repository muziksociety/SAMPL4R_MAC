#!/usr/bin/env bash
# SAMPLAR macOS build driver - wraps `cmake -B build && cmake --build build`
# with the macOS defaults (universal2, Release, macOS 11.0, Ninja if available).
#
# Usage:
#   scripts/mac/build.sh                  # universal2 Release VST3
#   scripts/mac/build.sh --arm64          # arm64 only (faster dev iteration)
#   scripts/mac/build.sh --debug          # Debug config
#   scripts/mac/build.sh --xcode          # force the Xcode generator
#   scripts/mac/build.sh --tests          # also build EngineTests + SamplarTests
#   scripts/mac/build.sh --clean          # wipe the build dir first
#   scripts/mac/build.sh --build-dir DIR  # use DIR instead of build/
#
# A build dir configured against an SDK that is no longer installed (Xcode
# updated, e.g. a cache pinned to MacOSX26.5.sdk) is refused: rerun with --clean
# or pick a fresh --build-dir.

set -euo pipefail

# Pick up a user-local cmake/ninja (when Homebrew is unavailable).
if [[ -d "$HOME/.local/bin" ]]; then
    export PATH="$HOME/.local/bin:$PATH"
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build"
CONFIG="Release"
ARCHS="arm64;x86_64"
GENERATOR=""
TESTS="OFF"
CLEAN=0

while [[ $# -gt 0 ]]; do
    arg="$1"; shift
    case "$arg" in
        --build-dir)
            BUILD_DIR="${1:?--build-dir needs a value}"; shift ;;
        --arm64)   ARCHS="arm64" ;;
        --x86_64)  ARCHS="x86_64" ;;
        --debug)   CONFIG="Debug" ;;
        --xcode)   GENERATOR="Xcode" ;;
        --ninja)   GENERATOR="Ninja" ;;
        --tests)   TESTS="ON" ;;
        --clean)   CLEAN=1 ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "unknown arg: $arg" >&2
            exit 2
            ;;
    esac
done

if [[ -z "$GENERATOR" ]]; then
    if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; else GENERATOR="Xcode"; fi
fi

if [[ $CLEAN -eq 1 ]]; then
    echo "[build] cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# Refuse a cache whose framework paths point at an SDK that is gone.
CACHE="$BUILD_DIR/CMakeCache.txt"
if [[ -f "$CACHE" ]]; then
    STALE_SDK="$(sed -nE 's/^juce_found_[A-Za-z]+:FILEPATH=(.*\.sdk)\/System\/.*/\1/p' "$CACHE" | sort -u | while read -r sdk; do
        [[ -d "$sdk" ]] || echo "$sdk"
    done | head -1)"
    if [[ -n "$STALE_SDK" ]]; then
        echo "[build] ERROR: $BUILD_DIR was configured against a missing SDK: $STALE_SDK" >&2
        echo "[build]        rerun with --clean, or use a fresh --build-dir" >&2
        exit 2
    fi
fi

echo "[build] generator: $GENERATOR"
echo "[build] config:    $CONFIG"
echo "[build] archs:     $ARCHS"
echo "[build] tests:     $TESTS"
echo "[build] repo:      $REPO_ROOT"

cmake -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DSAMPLAR_BUILD_TESTS="$TESTS"

# Ninja: keep going after errors so one attempt surfaces every compile issue.
NATIVE_ARGS=()
[[ "$GENERATOR" == "Ninja" ]] && NATIVE_ARGS=(-- -k 0)
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel --target SamplarPlugin_VST3 ${NATIVE_ARGS[@]+"${NATIVE_ARGS[@]}"}
if [[ "$TESTS" == "ON" ]]; then
    cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel --target EngineTests SamplarTests ${NATIVE_ARGS[@]+"${NATIVE_ARGS[@]}"}
fi

echo "[build] done."
echo "[build] artifacts under: $BUILD_DIR/SamplarPlugin_artefacts/$CONFIG/"
find "$BUILD_DIR/SamplarPlugin_artefacts/$CONFIG" -maxdepth 2 -name "*.vst3" 2>/dev/null | sed 's/^/  /'
