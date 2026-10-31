# MM-025: merge and hand-off plan (sync/win-895af949 → Mac main)

Card t_2d26b26d. Written 2026-10-27 by samplar-mac. This is a plan only. Nothing is merged, pushed or released.

## Where things stand

- Branch `sync/win-895af949` exists only in the worktree `~/Desktop/SAMPL4R_sync`. Its base is `win-main` @ 895af949 (Windows main, from MASTERMIND's git bundle).
- Mac commits on top of win-main, one item each:

| Commit | Content |
|--------|---------|
| 4283b70f | Configure and build the VST3 on macOS from win-main |
| c6026fdd | Clang portability fixes (analysis headers, HtdemucsIstft, SModeService) |
| 731885a8 | EngineTests: valid `std::aligned_alloc` size on non-MSVC |
| 257789c4 | .gitignore: Claude runtime state, signing material |
| 136517e5 | CMake: macOS universal2 defaults, Apple bundle settings, `scripts/mac/build.sh` + `stage_bundle.sh` |
| 750a025a | POSIX crash handler for the standalone app |
| 892d213f | Resource root inside the macOS VST3 bundle (`Contents/Resources`) |
| 8f175f34 | Tests: POSIX bounded-command branch, DataPaths resolved paths |
| c179b04a | Re-sign the macOS VST3 after the licence texts are staged |

- Item 8 (audio meter) was dropped by MASTERMIND. It is a Windows UI feature (TopBarSpec slot) and gets its own card.
- Local build: universal2 (x86_64 + arm64), VST3 1.0.1, full payload staged (htdemucs_body.onnx, beat_this.onnx, ORT 1.19.2 dylib, data marker, licences). EngineTests and SamplarTests pass. AU stays off.

## What samplar-win must review

Every change to a shared file is behind `APPLE` / `JUCE_MAC` / non-MSVC guards. Even so, samplar-win should check these files against Windows behaviour:

1. `CMakeLists.txt`: macOS defaults and the APPLE POST_BUILD staging branch. The Windows PowerShell staging is now under `if(WIN32)`.
2. `cmake/extra_qa.cmake`: an APPLE-only ad-hoc re-sign at the end of the licence step.
3. `src/plugin/SamplarAudioProcessor.cpp`: the `JUCE_MAC` resource-root check in `initServices`. The Windows order of checks is unchanged.
4. `src/app/App.cpp`: the POSIX crash handler (`#if ! JUCE_WINDOWS`).
5. Header-only fixes that the Mac compilers needed: `DWTTempoDetector.h`, `HybridTempoDetector.h`, `TempoAnalyzer.h`, `HtdemucsIstft.h`, `SModeService.cpp`.
6. Tests: `EngineTests.cpp`, `LicensingHardeningTests.cpp`, `DataPathsTests.cpp`.
7. Open on the Windows side (Note A, samplar-win's decision): win-main tracks `.claude/` agent files and `graphify-out/`.

## What the merge into Mac main replaces

Mac `main` (0c6e18c9) and win-main share cf8066ba. Mac-only commits: 6cc2e9f7, db43d885 (relay notes) and 0c6e18c9 (Mac port + audio meter + vendored JUCE). After the merge, Mac main's tree **equals the sync branch tree**:

- The whole Windows 2026-10 revival comes in: realtime engine, session persistence, licensing contract v2, layout redesign, and the dead-code purge (about 30k deleted files, libtorch included).
- 0c6e18c9's port fixes are replaced by the per-item commits above. The audio meter goes.
- **Vendored JUCE goes.** Main tracks 3436 files under `JUCE/` and the sync branch tracks 0. A plain merge silently keeps them, so run `git rm -r --cached JUCE`. Builds use an untracked JUCE 8.0.10 at `./JUCE`, the same version Windows uses.
- **CLAUDE.md conflict:** keep `MASTERMIND relay address: samplar-mac` on the Mac. Do not take win-main's `samplar-win`.

Mechanics, once approved. Do this in the worktree, never in the main checkout: switching the main checkout to a JUCE-less tree deletes JUCE from disk.

```
git checkout main            # in a fresh worktree
git merge --no-ff --no-commit sync/<tip>
git checkout --theirs -- .   # sync side wins every conflict
git rm -r --cached JUCE      # drop vendored JUCE from the index
# restore the samplar-mac relay address in CLAUDE.md
git diff --stat sync/<tip>   # must show only CLAUDE.md
scripts/mac/build.sh --tests --build-dir build_1031 # plus the bundle checks below; `build/` is pinned to the removed SDK 26.5
git commit
```

## Gates before any Mac release

1. ~~**MM-005**~~ cleared 2026-10-28: the SAMPL4R licensing v2 key is live on the license server, fingerprint 46cfe00569377025 = `kFingerprint` in this build. The Mac still does not ship on licensing v1.
2. **MM-034**: decide how JUCE / `JUCE/extras/Build` is tracked or fetched (git-tracked JUCE lacks `extras/Build/` because of case-insensitive disks and the `build/` ignore), so a clean clone builds.
3. samplar-win review (above) and the merge into Mac main, approved by MASTERMIND.
4. Bundle checks on the release build: `lipo -archs` gives x86_64 arm64; `otool -L` shows no `_deps`; `codesign -v --strict --deep` passes; dlopen works on both archs; both models load with the bundled ORT.
5. Packaging: `scripts/mac/make_pkg.sh --build-dir <dir>` builds an unsigned .pkg with ad-hoc signed bundles (the release plan for now: no Apple developer account, same as Sample Monsta for Mac). `--identity`, `--installer-identity` and `--notary-profile` add Developer ID signing and notarisation once an account exists.
6. Santi tests the build in a DAW. The release then goes through MASTERMIND (`release-request`). No release from this repo.
