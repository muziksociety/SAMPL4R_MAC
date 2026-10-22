#!/usr/bin/env python3
"""
SAMPLAR release linter - shared helpers (paths, allowlists, CMake + Inno parsing).

SAMPLAR ships ONE artefact: the self-contained VST3 bundle
    build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3
which release_assets/installer/samplar.iss packages as-is. Every check that looks
at build output takes --bundle (default: that path, or env SAMPLAR_LINT_BUNDLE).
"""
from __future__ import annotations

import fnmatch
import hashlib
import os
import re
from pathlib import Path

LINTER_DIR = Path(__file__).resolve().parent
REPO_ROOT = LINTER_DIR.parents[1]
CMAKE_MAIN = REPO_ROOT / "CMakeLists.txt"
CMAKE_EXTRA_DIR = REPO_ROOT / "cmake"
INSTALLER_ISS = REPO_ROOT / "release_assets" / "installer" / "samplar.iss"
DEFAULT_BUNDLE = REPO_ROOT / "build" / "SamplarPlugin_artefacts" / "Release" / "VST3" / "SAMPLAR.vst3"

# Pinned runtime bits (same values as release_assets/build_installer.ps1 + CMakeLists.txt).
ORT_DML_SHA256 = "410de3464172dae2945a4663f274e34c59965bc8233b0411de16baab56dda1a7"      # ORT 1.19.2 DirectML
DIRECTML_SHA256 = "f3b672b9fe6d8228ae978e74217a9bb1d6605643550be166eab15f1c09eddb86"     # DirectML 1.15.1
HTDEMUCS_BODY_SHA256 = "236be9938a4ef30dd388817b87d6144a5fd7f197b3f6961a9092f9b6d0c6f2c5"


def default_bundle() -> Path:
    env = os.environ.get("SAMPLAR_LINT_BUNDLE", "").strip()
    return Path(env) if env else DEFAULT_BUNDLE


def bundle_bin_dir(bundle: Path) -> Path:
    return bundle / "Contents" / "x86_64-win"


def bundle_binary(bundle: Path) -> Path:
    return bundle_bin_dir(bundle) / "SAMPLAR.vst3"


def rel(p: Path) -> str:
    try:
        return p.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return p.as_posix()


def load_allowlist(name: str, lower: bool = False) -> set[str]:
    """One entry per line, '#' comments. Missing file = empty."""
    path = LINTER_DIR / name
    out: set[str] = set()
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line.lower() if lower else line.replace("\\", "/"))
    return out


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------------
# CMake: every source path referenced by CMakeLists.txt + cmake/*.cmake, with the
# simple set(VAR "...") variables those files use resolved. Not a CMake
# interpreter - good enough for path lists (target_sources / set(... SOURCES)).
# ---------------------------------------------------------------------------------
_SET_RE = re.compile(r'^\s*set\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+"?([^")\s]+)"?\s*\)', re.MULTILINE)
_VAR_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")
_SRC_RE = re.compile(r'[\w${}./\-]+\.(?:cpp|c|cc|mm)\b')


def cmake_files() -> list[Path]:
    # Only files the build actually includes: CMakeLists.txt + the per-workstream
    # cmake/extra_*.cmake it include()s (cmake/DetectCpuAndBlas.cmake is unused).
    files = [CMAKE_MAIN]
    if CMAKE_EXTRA_DIR.is_dir():
        files += sorted(CMAKE_EXTRA_DIR.glob("extra_*.cmake"))
    return [f for f in files if f.exists()]


def _strip_comments(text: str) -> str:
    return re.sub(r"#[^\n]*", "", text)


def _resolve(token: str, variables: dict[str, str], list_dir: Path) -> str | None:
    local = dict(variables)
    local["CMAKE_CURRENT_LIST_DIR"] = list_dir.as_posix()
    local["CMAKE_CURRENT_SOURCE_DIR"] = REPO_ROOT.as_posix()
    local["CMAKE_SOURCE_DIR"] = REPO_ROOT.as_posix()
    local["PROJECT_SOURCE_DIR"] = REPO_ROOT.as_posix()
    for _ in range(6):
        if "${" not in token:
            break
        token = _VAR_RE.sub(lambda m: local.get(m.group(1), "\0"), token)
    if "\0" in token or "${" in token or "$<" in token:
        return None
    return token


def cmake_source_refs() -> dict[str, list[str]]:
    """repo-relative source path -> [cmake files referencing it]. Unresolvable
    variables are skipped. Paths outside the repo are kept absolute."""
    variables: dict[str, str] = {}
    refs: dict[str, list[str]] = {}
    for cm in cmake_files():
        text = _strip_comments(cm.read_text(encoding="utf-8", errors="replace"))
        list_dir = cm.parent
        for m in _SET_RE.finditer(text):
            val = _resolve(m.group(2), variables, list_dir)
            if val is not None:
                variables[m.group(1)] = val
        for m in _SRC_RE.finditer(text):
            tok = m.group(0)
            resolved = _resolve(tok, variables, list_dir)
            if resolved is None:
                continue
            p = Path(resolved)
            if not p.is_absolute():
                p = REPO_ROOT / p
            try:
                key = Path(os.path.normpath(p)).relative_to(Path(os.path.normpath(REPO_ROOT))).as_posix()
            except ValueError:
                key = Path(os.path.normpath(p)).as_posix()
            refs.setdefault(key, [])
            if cm.name not in refs[key]:
                refs[key].append(cm.name)
    return refs


# ---------------------------------------------------------------------------------
# Inno Setup script
# ---------------------------------------------------------------------------------
def iss_text() -> str:
    return INSTALLER_ISS.read_text(encoding="utf-8-sig", errors="replace") if INSTALLER_ISS.exists() else ""


def iss_files_entries(text: str) -> list[dict]:
    """[Files] entries (line continuations joined): {source, dest, excludes, flags}."""
    joined = re.sub(r"\\\s*\n\s*", " ", text)   # Inno '\' line continuation
    out = []
    section = ""
    for line in joined.splitlines():
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            section = s.lower()
            continue
        if section != "[files]" or not s.lower().startswith("source:"):
            continue
        def field(name: str) -> str:
            m = re.search(rf'{name}:\s*"([^"]*)"', s, re.IGNORECASE)
            return m.group(1) if m else ""
        flags = re.search(r"Flags:\s*([^;]+)", s, re.IGNORECASE)
        out.append({
            "source": field("Source"),
            "dest": field("DestDir"),
            "excludes": [e.strip() for e in field("Excludes").split(",") if e.strip()],
            "flags": flags.group(1).split() if flags else [],
            "line": s,
        })
    return out


def iss_source_path(source: str) -> Path:
    """Resolve an Inno Source: path (relative to the .iss folder)."""
    src = source.replace("\\", "/")
    if src.endswith("/*"):
        src = src[:-2]
    return Path(os.path.normpath(INSTALLER_ISS.parent / src))


def excluded(relpath: str, excludes: list[str]) -> bool:
    """Inno Excludes semantics (simplified): a pattern without a path separator
    matches any file or directory NAME at any depth."""
    parts = relpath.replace("\\", "/").split("/")
    for pat in excludes:
        pat = pat.replace("\\", "/").strip("/")
        if "/" in pat:
            if fnmatch.fnmatch(relpath.lower(), pat.lower()):
                return True
        elif any(fnmatch.fnmatch(part.lower(), pat.lower()) for part in parts):
            return True
    return False


def shipped_payload(bundle: Path) -> tuple[list[str], list[str]]:
    """Files the installer would copy out of the bundle (bundle-relative), and the
    files present in the bundle but dropped by [Files] Excludes."""
    entry = next((e for e in iss_files_entries(iss_text()) if "samplarplugin_artefacts" in e["source"].lower()), None)
    excludes = entry["excludes"] if entry else []
    shipped, dropped = [], []
    if not bundle.is_dir():
        return shipped, dropped
    for f in sorted(bundle.rglob("*")):
        if not f.is_file():
            continue
        r = f.relative_to(bundle).as_posix()
        (dropped if excluded(r, excludes) else shipped).append(r)
    return shipped, dropped


def cmake_project_version() -> str:
    m = re.search(r"project\s*\(\s*samplar\s+VERSION\s+([0-9.]+)", CMAKE_MAIN.read_text(encoding="utf-8", errors="replace"), re.IGNORECASE)
    return m.group(1) if m else ""


def common_args(ap, bundle: bool = False):
    ap.add_argument("--json", type=Path, help="Write JSON report to this path")
    ap.add_argument("--quiet", action="store_true", help="Suppress stdout report")
    if bundle:
        ap.add_argument("--bundle", type=Path, default=None,
                        help="SAMPLAR.vst3 bundle dir (default: build/SamplarPlugin_artefacts/Release/VST3/SAMPLAR.vst3 or $SAMPLAR_LINT_BUNDLE)")
    return ap
