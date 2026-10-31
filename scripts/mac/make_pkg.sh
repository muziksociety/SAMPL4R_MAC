#!/usr/bin/env bash
# SAMPLAR macOS installer package - wraps a built SAMPLAR.vst3 into a .pkg that
# installs to /Library/Audio/Plug-Ins/VST3/SAMPLAR.vst3.
#
# The models, ORT dylib, data marker and licenses are already inside the bundle
# (Contents/Resources/, staged by stage_bundle.sh); the plugin resolves them from
# there (SamplarAudioProcessor, JUCE_MAC branch), so installing the bundle
# installs the models.
#
# Usage:
#   scripts/mac/make_pkg.sh [--build-dir DIR] [--out DIR]
#                           [--identity "Developer ID Application: ..."]
#                           [--installer-identity "Developer ID Installer: ..."]
#                           [--notary-profile PROFILE]
#
#   --build-dir DIR          build tree holding SamplarPlugin_artefacts (default: build)
#   --out DIR                output folder for the .pkg (default: dist/mac)
#   --identity ID            codesign identity for the bundle (default: ad-hoc "-")
#   --installer-identity ID  productsign identity for the .pkg (optional)
#   --notary-profile NAME    notarytool keychain profile; needs --identity and
#                            --installer-identity; notarises and staples the .pkg
#
# The version comes from project(... VERSION x.y.z) in CMakeLists.txt.
# Nothing is installed, uploaded or released by this script.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build"
OUT_DIR="dist/mac"
IDENTITY="-"
INSTALLER_IDENTITY=""
NOTARY_PROFILE=""
PKG_IDENTIFIER="com.muziksociety.samplar.vst3"
INSTALL_LOCATION="/Library/Audio/Plug-Ins/VST3"
BUNDLE_NAME="SAMPLAR.vst3"

log()  { echo "[pkg] $*"; }
fail() { echo "[pkg] ERROR: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)          BUILD_DIR="${2:?--build-dir needs a value}"; shift 2 ;;
        --out)                OUT_DIR="${2:?--out needs a value}"; shift 2 ;;
        --identity)           IDENTITY="${2:?--identity needs a value}"; shift 2 ;;
        --installer-identity) INSTALLER_IDENTITY="${2:?--installer-identity needs a value}"; shift 2 ;;
        --notary-profile)     NOTARY_PROFILE="${2:?--notary-profile needs a value}"; shift 2 ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) fail "unknown arg: $1" ;;
    esac
done

if [[ -n "$NOTARY_PROFILE" && ( "$IDENTITY" == "-" || -z "$INSTALLER_IDENTITY" ) ]]; then
    fail "--notary-profile needs --identity and --installer-identity (ad-hoc builds cannot be notarised)"
fi

# 1) Version from CMakeLists.txt project().
VERSION="$(sed -nE 's/^project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -1)"
[[ -n "$VERSION" ]] || fail "could not read project() VERSION from CMakeLists.txt"
log "version:   $VERSION"

SRC_BUNDLE="$BUILD_DIR/SamplarPlugin_artefacts/Release/VST3/$BUNDLE_NAME"
[[ -d "$SRC_BUNDLE" ]] || fail "bundle not found: $SRC_BUNDLE (build Release first: scripts/mac/build.sh)"
[[ -d "$SRC_BUNDLE/Contents/Resources/models" ]] || fail "no models/ in bundle: $SRC_BUNDLE"
[[ -f "$SRC_BUNDLE/Contents/Resources/data/samplar_bundle.marker" ]] || fail "no data marker in bundle: $SRC_BUNDLE"
log "bundle:    $SRC_BUNDLE"
log "identity:  $([[ "$IDENTITY" == "-" ]] && echo "ad-hoc" || echo "$IDENTITY")"

# 2) Stage a copy so the build tree is never re-signed in place.
STAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/samplar_pkg.XXXXXX")"
trap 'rm -rf "$STAGE_DIR"' EXIT
PAYLOAD_ROOT="$STAGE_DIR/root"
mkdir -p "$PAYLOAD_ROOT"
ditto "$SRC_BUNDLE" "$PAYLOAD_ROOT/$BUNDLE_NAME"
STAGED="$PAYLOAD_ROOT/$BUNDLE_NAME"
log "staged:    $STAGED"

# 3) Sign nested dylibs first, then the bundle.
SIGN_ARGS=(--force --sign "$IDENTITY")
if [[ "$IDENTITY" == "-" ]]; then
    SIGN_ARGS+=(--timestamp=none)
else
    SIGN_ARGS+=(--timestamp --options runtime)
fi
while IFS= read -r -d '' dylib; do
    codesign "${SIGN_ARGS[@]}" "$dylib"
    log "signed:    ${dylib#"$PAYLOAD_ROOT/"}"
done < <(find "$STAGED/Contents" -name "*.dylib" -type f -print0)
codesign "${SIGN_ARGS[@]}" --deep "$STAGED"
log "signed:    $BUNDLE_NAME"

# 4) Every bundle must verify before pkgbuild.
codesign --verify --deep --strict --verbose=2 "$STAGED" || fail "codesign --verify failed: $STAGED"
log "verified:  codesign --verify --deep --strict"

# 5) Build the component package.
mkdir -p "$OUT_DIR"
PKG_NAME="SAMPLAR-${VERSION}-mac"
UNSIGNED_PKG="$STAGE_DIR/${PKG_NAME}-unsigned.pkg"
FINAL_PKG="$OUT_DIR/${PKG_NAME}.pkg"

COMPONENT_PLIST="$STAGE_DIR/component.plist"
pkgbuild --analyze --root "$PAYLOAD_ROOT" "$COMPONENT_PLIST" >/dev/null
# Always install to the fixed location; never relocate onto a copy found elsewhere.
if /usr/libexec/PlistBuddy -c "Print :0:BundleIsRelocatable" "$COMPONENT_PLIST" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$COMPONENT_PLIST"
    log "component: relocation off"
else
    log "component: no relocatable bundle entry (nothing to turn off)"
fi

pkgbuild --root "$PAYLOAD_ROOT" \
    --component-plist "$COMPONENT_PLIST" \
    --identifier "$PKG_IDENTIFIER" \
    --version "$VERSION" \
    --install-location "$INSTALL_LOCATION" \
    "$UNSIGNED_PKG"

# 6) Optional installer signing + notarisation.
if [[ -n "$INSTALLER_IDENTITY" ]]; then
    productsign --sign "$INSTALLER_IDENTITY" "$UNSIGNED_PKG" "$FINAL_PKG"
    log "pkg signed: $INSTALLER_IDENTITY"
else
    cp -f "$UNSIGNED_PKG" "$FINAL_PKG"
    log "pkg unsigned (no --installer-identity)"
fi

if [[ -n "$NOTARY_PROFILE" ]]; then
    log "notarising with profile: $NOTARY_PROFILE"
    xcrun notarytool submit "$FINAL_PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$FINAL_PKG"
    log "notarised and stapled"
fi

log "done:      $FINAL_PKG"
log "size:      $(stat -f %z "$FINAL_PKG") bytes"
log "sha256:    $(shasum -a 256 "$FINAL_PKG" | awk '{print $1}')"
log "binary:    $(shasum -a 256 "$STAGED/Contents/MacOS/SAMPLAR" | awk '{print $1}')"
