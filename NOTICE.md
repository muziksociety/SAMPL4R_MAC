# Third-Party Notices

SAMPLAR bundles or links against the following third-party software. Their
respective licenses are included with the source or in the vendored directories.

## Beat This! (Neural Beat Tracker)

- **Paper:** Foscarin, F. et al. *"Beat This! Accurate beat tracking without a
  bag of tricks."* ISMIR 2024. https://arxiv.org/abs/2407.21658
- **C++ port:** https://github.com/mosynthkey/beat_this_cpp
- **License:** MIT — see `third_party/beat_this/LICENSE.txt`
- **Model file:** `third_party/beat_this/models/beat_this.onnx` (~83 MB, ONNX)

## ONNX Runtime

- **Project:** https://github.com/microsoft/onnxruntime
- **License:** MIT — see https://github.com/microsoft/onnxruntime/blob/main/LICENSE
- **Distribution:** pre-built Windows x64 binary fetched by CMake at configure
  time. `onnxruntime.dll` is copied next to the SAMPLAR executable at build time.

## pocketfft (header-only FFT)

- **Project:** https://github.com/mreineck/pocketfft
- **License:** BSD-3-Clause — see `third_party/beat_this/third_party/pocketfft/LICENSE.md`
- Used by the Beat This! mel-spectrogram preprocessor.

## signalsmith-stretch

- **Project:** https://github.com/Signalsmith-Audio/signalsmith-stretch
- **License:** MIT — vendored at `third_party/signalsmith-stretch/`

## SoundTouch

- **Project:** https://codeberg.org/soundtouch/soundtouch
- **License:** LGPL-2.1 — fetched by CMake at configure time
- Used only by the offline preview path (`SoundTouchSource`).

## Loop Tempo Estimator (Audacity)

- **Project:** https://github.com/audacity/audacity (extracted as
  `third_party/loop-tempo-estimator/`)
- **License:** GPL-2.0-or-later — see vendored LICENSE

## nlohmann/json

- **Project:** https://github.com/nlohmann/json
- **License:** MIT
- Fetched by CMake at configure time.

## JUCE

- **Project:** https://juce.com/
- See JUCE's own licensing terms in the vendored JUCE directory.
