# SAMPLAR (Mac) run log

## 2026-11-12 - Plugin-ID guard (card t_d0d4cc69)

- Branch `chore/plugin-id-guard` from `main` (eb3a20ff), worktree `../SAMPL4R_idguard`.
- `cmake/identity.cmake` is now the only place the identity is written: company
  `Muziksociety`, manufacturer `Mzks`, plugin `Smpl`, VST3 class ID
  `ABCDEF019182FAEB4D7A6B73536D706C`, bundle id `com.muziksociety.samplar`
  (Windows VST3 keeps its existing `com.muziksociety.samplar.vst3`). Values are
  unchanged from `main`; `CMakeLists.txt` reads them from the include. No AU on Mac
  (MM-024), so no AU type is set.
- `.gitignore`: `!cmake/*.cmake` so `identity.cmake` is tracked (the `*.cmake` rule hid it).
- `tools/release_linter/check_plugin_ids.py` reads the BUILT bundle
  (`moduleinfo.json` class ID + vendor, macOS `Info.plist` bundle id, AU
  `AudioComponents` when a `.component` is built) and also checks that the class
  ID in identity.cmake is the one JUCE derives from the codes. Exit 0 clean, 1 drift, 2 error.
- Wired in: ctest `PluginIdCheck` (`-DSAMPLAR_BUILD_TESTS=ON`), release linter
  `run_all.py` (check #9, so `tools/run_all_checks.ps1` runs it on Windows), and
  `scripts/mac/build.sh` runs it after every build.
- Proof: a scratch identity with `Smpl` -> `Smpx` or a changed class ID fails (exit 1);
  a fake AU with manufacturer `Mzkz` fails; the real identity passes against a fresh
  arm64 build of this branch (`build_idguard/`).
