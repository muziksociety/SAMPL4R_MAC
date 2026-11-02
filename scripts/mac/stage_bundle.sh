#!/usr/bin/env bash
# SAMPLAR macOS bundle staging - bash counterpart of the Windows PowerShell
# staging script generated in CMakeLists.txt (_SAMPLAR_VST3_STAGE_SCRIPT).
#
# Invoked as a POST_BUILD step from CMakeLists.txt (APPLE branch):
#   stage_bundle.sh <bundle-dir> <repo-root> <ort-dylib> <sep-model> <stale-model> <pffft-license>
#
# Layout produced (same payload as Windows, under Contents/Resources/):
#   SAMPLAR.vst3/Contents/Resources/
#     libonnxruntime.<ver>.dylib   (install id rewritten to @loader_path)
#     models/<sep-model>           (exactly ONE separation model) + beat_this.onnx
#     data/samplar_bundle.marker   (install-location marker; DataPaths looks for data/)
#     licenses/                    (ORT + PFFFT notices)
#
# The plugin binary in Contents/MacOS/ is patched with install_name_tool so it
# loads the bundled dylib, then the bundle is ad-hoc codesigned.
#
# All steps are best-effort: a DAW holding the bundle open during an
# incremental build must not fail the build. The final "exit 0" is intentional.

set -u  # NOT set -e - best-effort, not fail-fast.

BUNDLE_DIR="${1:-}"
REPO_ROOT="${2:-}"
ORT_DYLIB_SRC="${3:-}"
SEP_MODEL="${4:-htdemucs.onnx}"
STALE_MODEL="${5:-}"
PFFFT_LICENSE="${6:-}"

log() { echo "[stage] $*"; }

if [[ -z "$BUNDLE_DIR" || -z "$REPO_ROOT" ]]; then
    log "usage: $0 <bundle-dir> <repo-root> <ort-dylib> <sep-model> <stale-model> <pffft-license>" >&2
    exit 0
fi
if [[ ! -d "$BUNDLE_DIR" ]]; then
    log "bundle dir not found, skipping: $BUNDLE_DIR"
    exit 0
fi

RESOURCES_DIR="${BUNDLE_DIR}/Contents/Resources"
MACOS_DIR="${BUNDLE_DIR}/Contents/MacOS"
mkdir -p "$RESOURCES_DIR"

safe_cp() {
    # safe_cp <src> <dst> - copies if src exists, logs a skip otherwise.
    local src="$1" dst="$2"
    if [[ -e "$src" ]]; then
        mkdir -p "$(dirname "$dst")"
        cp -fL "$src" "$dst" 2>/dev/null && log "copied: $(basename "$src") -> $dst" \
            || log "skip (locked?): $src"
    else
        log "missing, not staged: $src"
    fi
}

# 1) ONNX Runtime dylib, with an @loader_path install id.
ORT_DYLIB=""
if [[ -n "$ORT_DYLIB_SRC" && -f "$ORT_DYLIB_SRC" ]]; then
    ORT_DYLIB="$(basename "$ORT_DYLIB_SRC")"
    safe_cp "$ORT_DYLIB_SRC" "$RESOURCES_DIR/$ORT_DYLIB"
    install_name_tool -id "@loader_path/../Resources/$ORT_DYLIB" "$RESOURCES_DIR/$ORT_DYLIB" 2>/dev/null \
        && log "set dylib id: @loader_path/../Resources/$ORT_DYLIB" \
        || log "install_name_tool -id FAILED on $ORT_DYLIB"
else
    log "ORT dylib not provided or missing ($ORT_DYLIB_SRC) - ORT staging skipped"
fi

# 2) models/ - exactly one separation model + beat_this.onnx.
MODELS_DIR="$RESOURCES_DIR/models"
mkdir -p "$MODELS_DIR"
safe_cp "$REPO_ROOT/models/$SEP_MODEL" "$MODELS_DIR/$SEP_MODEL"
[[ -n "$STALE_MODEL" && -e "$MODELS_DIR/$STALE_MODEL" ]] && rm -f "$MODELS_DIR/$STALE_MODEL" && log "removed stale model: $STALE_MODEL"
safe_cp "$REPO_ROOT/third_party/beat_this/models/beat_this.onnx" "$MODELS_DIR/beat_this.onnx"

# 3) data/ - install-location marker only.
mkdir -p "$RESOURCES_DIR/data"
printf 'SAMPLAR resource root marker\n' > "$RESOURCES_DIR/data/samplar_bundle.marker" \
    && log "wrote data/samplar_bundle.marker"

# 4) Third-party licenses.
LIC_DIR="$RESOURCES_DIR/licenses"
mkdir -p "$LIC_DIR"
if [[ -n "$ORT_DYLIB_SRC" ]]; then
    ORT_ROOT="$(cd "$(dirname "$ORT_DYLIB_SRC")/.." 2>/dev/null && pwd)"
    safe_cp "$ORT_ROOT/LICENSE"               "$LIC_DIR/onnxruntime-LICENSE.txt"
    safe_cp "$ORT_ROOT/ThirdPartyNotices.txt" "$LIC_DIR/onnxruntime-ThirdPartyNotices.txt"
fi
[[ -n "$PFFFT_LICENSE" ]] && safe_cp "$PFFFT_LICENSE" "$LIC_DIR/pffft-LICENSE.txt"

# 5) Point the plugin binary at the bundled dylib instead of the build tree.
BUNDLE_BASENAME="$(basename "$BUNDLE_DIR")"
PLUGIN_BIN="$MACOS_DIR/${BUNDLE_BASENAME%.*}"
if [[ -f "$PLUGIN_BIN" && -n "$ORT_DYLIB" ]]; then
    OLD_REF="$(otool -L "$PLUGIN_BIN" 2>/dev/null | awk '/libonnxruntime/ {print $1; exit}')"
    NEW_REF="@loader_path/../Resources/$ORT_DYLIB"
    if [[ -n "$OLD_REF" && "$OLD_REF" != "$NEW_REF" ]]; then
        install_name_tool -change "$OLD_REF" "$NEW_REF" "$PLUGIN_BIN" 2>/dev/null \
            && log "patched plugin binary: $OLD_REF -> $NEW_REF" \
            || log "install_name_tool -change FAILED on $PLUGIN_BIN"
    else
        log "plugin binary ORT reference already bundled or absent: ${OLD_REF:-none}"
    fi
    # Drop build-tree rpaths so nothing resolves outside the bundle.
    otool -l "$PLUGIN_BIN" 2>/dev/null | awk '/LC_RPATH/ {getline; getline; print $2}' | sort -u | while read -r rp; do
        if [[ "$rp" == *"/_deps/"* ]]; then
            install_name_tool -delete_rpath "$rp" "$PLUGIN_BIN" 2>/dev/null \
                && log "removed build-tree rpath: $rp" \
                || log "delete_rpath FAILED: $rp"
        fi
    done
fi

# 6) Ad-hoc codesign (no Developer ID). Apple Silicon refuses modified, unsigned code.
#    Sign the nested dylib first, then the bundle.
[[ -n "$ORT_DYLIB" && -f "$RESOURCES_DIR/$ORT_DYLIB" ]] && \
    codesign --force --sign - --timestamp=none "$RESOURCES_DIR/$ORT_DYLIB" 2>/dev/null
codesign --force --sign - --timestamp=none "$BUNDLE_DIR" 2>/dev/null \
    && log "ad-hoc codesigned: $BUNDLE_DIR" \
    || log "codesign FAILED (non-fatal): $BUNDLE_DIR"

log "staging complete: $BUNDLE_DIR"
exit 0
