#!/usr/bin/env bash
# SAMPLAR macOS build driver — wraps `cmake -B build && cmake --build build`
# with sane defaults (universal2 binary, Release, Ninja if available).
#
# Usage:
#   scripts/mac/build.sh                  # universal2 Release with Ninja or Xcode
#   scripts/mac/build.sh --arm64          # arm64 only (faster dev iteration)
#   scripts/mac/build.sh --debug          # Debug config
#   scripts/mac/build.sh --xcode          # force Xcode generator (for debugging in IDE)
#   scripts/mac/build.sh --clean          # wipe build/ first

set -euo pipefail

# Add ~/.local/bin to PATH so a user-local cmake/ninja install (e.g. when
# Homebrew is unavailable) is picked up automatically.
if [[ -d "$HOME/.local/bin" ]]; then
    export PATH="$HOME/.local/bin:$PATH"
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build"
CONFIG="Release"
ARCHS="arm64;x86_64"
GENERATOR=""
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --arm64)   ARCHS="arm64" ;;
        --x86_64)  ARCHS="x86_64" ;;
        --debug)   CONFIG="Debug" ;;
        --xcode)   GENERATOR="-G Xcode" ;;
        --ninja)   GENERATOR="-G Ninja" ;;
        --clean)   CLEAN=1 ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# //'
            exit 0
            ;;
        *)
            echo "unknown arg: $arg" >&2
            exit 2
            ;;
    esac
done

if [[ -z "$GENERATOR" ]]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="-G Ninja"
    else
        GENERATOR="-G Xcode"
    fi
fi

if [[ $CLEAN -eq 1 ]]; then
    echo "[build] cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

echo "[build] generator: $GENERATOR"
echo "[build] config:    $CONFIG"
echo "[build] archs:     $ARCHS"
echo "[build] repo:      $REPO_ROOT"

# shellcheck disable=SC2086
cmake -B "$BUILD_DIR" $GENERATOR \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

# `-k 0` tells ninja to keep building after errors (instead of stopping on the
# first FAILED), so each attempt surfaces as many compile issues as possible.
# Trade-off: noisier output, but avoids the per-error rebuild ping-pong.
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel -- -k 0

echo "[build] done."
echo "[build] artifacts under: $BUILD_DIR/SamplarPlugin_artefacts/$CONFIG/"
find "$BUILD_DIR/SamplarPlugin_artefacts/$CONFIG" -maxdepth 2 \
    \( -name "*.vst3" -o -name "*.component" -o -name "*.app" \) 2>/dev/null \
    | sed 's/^/  /'
