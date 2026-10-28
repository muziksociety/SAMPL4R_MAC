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

for arg in "$@"; do
    case "$arg" in
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
