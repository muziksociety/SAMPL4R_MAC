# Build Notes (SAMPLAR)

- JUCE vendoring: CMake prefers `Sample_monsta5/JUCE` and falls back to `../JUCE`.
- Constant‑pitch preview: enable `WITH_SOUNDTOUCH=ON` to fetch and link SoundTouch via CMake FetchContent.

## Quick Start

- Configure: `cmake -S Sample_monsta5 -B build/sm5 -DWITH_SOUNDTOUCH=ON`
- Build: `cmake --build build/sm5 --config Release`

## JUCE Path Resolution

Order of preference:
1. `Sample_monsta5/JUCE` (vendored)
2. `../JUCE` (repo root)

If neither exists, CMake issues a warning. You can add JUCE in either location.

## SoundTouch (Optional)

- Toggle with `-DWITH_SOUNDTOUCH=ON|OFF` (defaults ON in this repo).
- When ON, CMake attempts to fetch SoundTouch (`v2.3.2`) using `FetchContent`.
- If the target (`SoundTouch` or `soundtouch`) is available, it links and defines `WITH_SOUNDTOUCH=1` for the app.
- If fetch fails or no target is exported, the build continues without SoundTouch (no define set).

## Offline/No‑Network Builds

- Disable fetches: `-DWITH_SOUNDTOUCH=OFF`.
- Ensure JUCE is present at one of the preferred paths above.

