#!/usr/bin/env bash
# SAMPLAR macOS bundle staging — bash counterpart to the Windows PowerShell
# staging script embedded in CMakeLists.txt (search for _SAMPLAR_VST3_STAGE_SCRIPT).
#
# Invoked as a POST_BUILD step from CMakeLists.txt (APPLE branch):
#   stage_bundle.sh <bundle-dir> <repo-root> <ort-root>
#
# Where:
#   <bundle-dir> = SAMPLAR.vst3 / SAMPLAR.component / SAMPLAR.app (the .bundle dir)
#   <repo-root>  = CMAKE_CURRENT_SOURCE_DIR
#   <ort-root>   = onnxruntime_prebuilt-src directory from FetchContent
#
# Staging layout produced (same for VST3 / AU / Standalone):
#   <BundleName>/Contents/Resources/
#     libonnxruntime.<ver>.dylib   (rewritten with @loader_path id)
#     libonnxruntime.dylib         (symlink → versioned dylib)
#     models/htdemucs.onnx
#     models/beat_this.onnx
#     data/*.json
#     BACKGROUNDS/                  (UI animations + tilesets)
#     Samples/                      (808 + GM kit, if present)
#     tools/ffmpeg                  (if a universal ffmpeg is staged in tools/mac/)
#
# After staging, the plugin binary inside Contents/MacOS/ is patched with
# install_name_tool so it locates the bundled dylib via @loader_path, and
# the whole bundle is ad-hoc codesigned so it loads on the dev machine.
#
# All file ops are non-fatal: a DAW holding the bundle open during incremental
# builds shouldn't kill the build. The "exit 0" at the end is intentional.

set -u  # NOT set -e — we want best-effort, not fail-fast.

BUNDLE_DIR="${1:-}"
REPO_ROOT="${2:-}"
ORT_ROOT="${3:-}"

if [[ -z "$BUNDLE_DIR" || -z "$REPO_ROOT" ]]; then
    echo "[stage] usage: $0 <bundle-dir> <repo-root> [<ort-root>]" >&2
    exit 0
fi

if [[ ! -d "$BUNDLE_DIR" ]]; then
    echo "[stage] bundle dir not found, skipping: $BUNDLE_DIR"
    exit 0
fi

RESOURCES_DIR="${BUNDLE_DIR}/Contents/Resources"
MACOS_DIR="${BUNDLE_DIR}/Contents/MacOS"
mkdir -p "$RESOURCES_DIR" "$MACOS_DIR"

log() { echo "[stage] $*"; }
safe_cp() {
    # safe_cp <src> <dst>  — copies if src exists; silently skips otherwise.
    local src="$1" dst="$2"
    if [[ -e "$src" ]]; then
        mkdir -p "$(dirname "$dst")"
        cp -RfL "$src" "$dst" 2>/dev/null && log "copied: $(basename "$src") -> $dst" \
            || log "skip (locked?): $src"
    fi
}
safe_cp_tree() {
    # safe_cp_tree <src-dir> <dst-dir>
    local src="$1" dst="$2"
    if [[ -d "$src" ]]; then
        mkdir -p "$dst"
        cp -RfL "$src/." "$dst/" 2>/dev/null && log "copied tree: $src -> $dst" \
            || log "tree skip (locked?): $src"
    fi
}

# ---------------------------------------------------------------------------
# 1) libonnxruntime dylib — find versioned dylib in ORT_ROOT, copy into bundle,
#    rewrite its install_name so the plugin can find it via @loader_path.
# ---------------------------------------------------------------------------
if [[ -n "$ORT_ROOT" && -d "$ORT_ROOT/lib" ]]; then
    # Match libonnxruntime.1.20.1.dylib (the real versioned file, not the symlink)
    ORT_DYLIB="$(/bin/ls -1 "$ORT_ROOT/lib" 2>/dev/null | grep -E '^libonnxruntime\.[0-9].*\.dylib$' | head -n1)"
    if [[ -n "$ORT_DYLIB" ]]; then
        safe_cp "$ORT_ROOT/lib/$ORT_DYLIB" "$RESOURCES_DIR/$ORT_DYLIB"
        # Versionless symlink for runtime lookup convenience.
        ( cd "$RESOURCES_DIR" && ln -sf "$ORT_DYLIB" "libonnxruntime.dylib" ) 2>/dev/null
        # Set the dylib's own install_name so any binary linking to it locates it
        # relative to its loader. The plugin binary install_name_tool -change is
        # handled below.
        install_name_tool -id "@loader_path/../Resources/$ORT_DYLIB" \
                          "$RESOURCES_DIR/$ORT_DYLIB" 2>/dev/null \
            && log "set dylib id: @loader_path/../Resources/$ORT_DYLIB" \
            || log "install_name_tool -id FAILED on $ORT_DYLIB"
    else
        log "no libonnxruntime.*.dylib found under $ORT_ROOT/lib — ORT staging skipped"
    fi
else
    log "ORT_ROOT not provided or missing — ORT staging skipped"
fi

# ---------------------------------------------------------------------------
# 2) Models — htdemucs.onnx + beat_this.onnx (skip cleanly if missing in clone)
# ---------------------------------------------------------------------------
mkdir -p "$RESOURCES_DIR/models"
safe_cp "$REPO_ROOT/models/htdemucs.onnx"                          "$RESOURCES_DIR/models/htdemucs.onnx"
safe_cp "$REPO_ROOT/third_party/beat_this/models/beat_this.onnx"   "$RESOURCES_DIR/models/beat_this.onnx"

# ---------------------------------------------------------------------------
# 3) Data JSONs — drum pattern banks
# ---------------------------------------------------------------------------
mkdir -p "$RESOURCES_DIR/data"
for p in \
    "data/patterns_with_rolls.json" \
    "SM5_Portable_2026-02-18/data/patterns_with_rolls.json" \
    "data/latin_patterns.json" \
    "SM5_Portable_2026-02-18/data/latin_patterns.json"
do
    [[ -e "$REPO_ROOT/$p" ]] && cp -f "$REPO_ROOT/$p" "$RESOURCES_DIR/data/" 2>/dev/null
done

# ---------------------------------------------------------------------------
# 4) BACKGROUNDS/ — UI animations + tilesets
# ---------------------------------------------------------------------------
safe_cp_tree "$REPO_ROOT/BACKGROUNDS" "$RESOURCES_DIR/BACKGROUNDS"

# ---------------------------------------------------------------------------
# 5) Samples/ — 808 + GM kit (optional, only present in distribution builds)
# ---------------------------------------------------------------------------
safe_cp_tree "$REPO_ROOT/Samples" "$RESOURCES_DIR/Samples"

# ---------------------------------------------------------------------------
# 6) ffmpeg — strategy TBD. If a universal ffmpeg binary is dropped at
#    tools/mac/ffmpeg, stage it. Otherwise the plugin will fall back to a
#    system ffmpeg on PATH (or Homebrew's /opt/homebrew/bin/ffmpeg).
# ---------------------------------------------------------------------------
mkdir -p "$RESOURCES_DIR/tools"
safe_cp "$REPO_ROOT/tools/mac/ffmpeg" "$RESOURCES_DIR/tools/ffmpeg"
[[ -f "$RESOURCES_DIR/tools/ffmpeg" ]] && chmod +x "$RESOURCES_DIR/tools/ffmpeg"

# ---------------------------------------------------------------------------
# 7) Patch the plugin binary's reference to libonnxruntime so it loads our
#    bundled copy via @loader_path/../Resources/ instead of an absolute
#    DYLD path from the build tree.
# ---------------------------------------------------------------------------
# Plugin binary is named after the bundle (sans .vst3/.component/.app).
BUNDLE_BASENAME="$(basename "$BUNDLE_DIR")"
PLUGIN_BIN_NAME="${BUNDLE_BASENAME%.*}"
PLUGIN_BIN="$MACOS_DIR/$PLUGIN_BIN_NAME"

if [[ -f "$PLUGIN_BIN" && -n "${ORT_DYLIB:-}" ]]; then
    # Find existing path that contains libonnxruntime so we can rewrite it.
    OLD_REF="$(otool -L "$PLUGIN_BIN" 2>/dev/null | awk '/libonnxruntime/ {print $1; exit}')"
    if [[ -n "$OLD_REF" ]]; then
        install_name_tool -change "$OLD_REF" \
                          "@loader_path/../Resources/$ORT_DYLIB" \
                          "$PLUGIN_BIN" 2>/dev/null \
            && log "patched plugin binary: $OLD_REF -> @loader_path/../Resources/$ORT_DYLIB" \
            || log "install_name_tool -change FAILED on $PLUGIN_BIN"
    else
        log "no libonnxruntime reference found in $PLUGIN_BIN — nothing to patch"
    fi
fi

# ---------------------------------------------------------------------------
# 8) Ad-hoc codesign the bundle (no Developer ID). Required on Apple Silicon
#    for any dylib reference rewrites to be honored at load time.
#    --deep handles the embedded dylib in Resources/.
# ---------------------------------------------------------------------------
if [[ -d "$BUNDLE_DIR" ]]; then
    codesign --force --sign - --deep --timestamp=none "$BUNDLE_DIR" 2>/dev/null \
        && log "ad-hoc codesigned: $BUNDLE_DIR" \
        || log "codesign FAILED (non-fatal): $BUNDLE_DIR"
fi

log "staging complete: $BUNDLE_DIR"
exit 0
