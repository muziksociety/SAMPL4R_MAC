# SAMPLAR - Third-Party Notices

SAMPLAR (VST3) by Muziksociety includes or links the third-party software and
data listed below. The full licence texts ship inside the plugin bundle at

    SAMPLAR.vst3/Contents/Resources/licenses/

(staged at build time by `CMakeLists.txt` + `cmake/extra_qa.cmake`). Licence texts
that have no file in the source tree (Demucs, Beat This! weights) are reproduced
in full at the end of this document.

VST is a registered trademark of Steinberg Media Technologies GmbH.

| Component | Version | Licence | Shipped as | Licence file in the bundle |
|---|---|---|---|---|
| ONNX Runtime (DirectML build) | 1.19.2 | MIT | `onnxruntime.dll` | `onnxruntime-LICENSE.txt`, `onnxruntime-ThirdPartyNotices.txt` |
| DirectML redistributable | 1.15.1 | Microsoft Software License Terms (DirectML) | `DirectML.dll` | `DirectML-LICENSE.txt`, `DirectML-ThirdPartyNotices.txt` |
| Demucs / HTDemucs (model weights) | v4 (htdemucs) | MIT | `models/htdemucs_body.onnx` (converted) | this file |
| Beat This! (model weights) | 2024 | MIT | `models/beat_this.onnx` (converted) | this file |
| beat_this_cpp (C++ inference port) | vendored | MIT | linked into SAMPLAR | `beat_this_cpp-LICENSE.txt` |
| pocketfft (header-only) | vendored | BSD-3-Clause | linked into SAMPLAR | `pocketfft-LICENSE.md` |
| PFFFT (marton78 fork) | vendored | BSD-style (FFTPACK) | linked into SAMPLAR | `pffft-LICENSE.txt` |
| signalsmith-stretch | vendored | MIT | linked into SAMPLAR | `signalsmith-stretch-LICENSE.txt` |
| signalsmith-linear | vendored | MIT | linked into SAMPLAR | `signalsmith-linear-LICENSE.txt` |
| JUCE | 8.0.10 | JUCE 8 commercial licence (see below) | linked into SAMPLAR | `JUCE-LICENSE.md` |
| JUCE dependencies (zlib, pnglib, jpeglib, FLAC, Ogg Vorbis, HarfBuzz, SheenBidi) | bundled with JUCE | zlib / libpng / IJG / BSD / BSD / Old-MIT / Apache-2.0 | linked into SAMPLAR | `JUCE-*.txt` |
| Steinberg VST3 SDK (via JUCE) | bundled with JUCE | Proprietary Steinberg VST3 License | linked into SAMPLAR | `JUCE-VST3_SDK-LICENSE.txt` |

## ONNX Runtime

- Project: https://github.com/microsoft/onnxruntime
- Package: `Microsoft.ML.OnnxRuntime.DirectML` 1.19.2 (vendored at
  `third_party/onnxruntime-directml/`, fetched by `tools/fetch_onnxruntime_directml.py`).
- Licence: MIT, Copyright (c) Microsoft Corporation. ONNX Runtime's own
  third-party notices ship as `onnxruntime-ThirdPartyNotices.txt`.

## DirectML

- Package: `Microsoft.AI.DirectML` 1.15.1. `DirectML.dll` is a redistributable
  under the Microsoft Software License Terms for DirectML, which must accompany
  it: `DirectML-LICENSE.txt` (+ `DirectML-ThirdPartyNotices.txt`).

## Demucs / HTDemucs (stem separation model)

- Project: https://github.com/facebookresearch/demucs (Defossez, A. *Hybrid
  Transformers for Music Source Separation*, ICASSP 2023).
- `models/htdemucs_body.onnx` is the htdemucs v4 network exported to ONNX and cut
  before its final inverse STFT (`tools/separation/split_htdemucs.py`); the iSTFT
  runs in SAMPLAR (`HtdemucsIstft`).
- Licence: MIT, Copyright (c) Meta Platforms, Inc. and affiliates (full text below).

## Beat This! (beat tracker)

- Paper: Foscarin, F. et al. *Beat This! Accurate beat tracking without DBN
  postprocessing.* ISMIR 2024. https://arxiv.org/abs/2407.21658
- Model weights: https://github.com/CPJKU/beat_this - MIT, Copyright (c) 2024
  Institute of Computational Perception, JKU Linz (full text below).
  Shipped converted to ONNX as `models/beat_this.onnx`.
- C++ inference port: https://github.com/mosynthkey/beat_this_cpp - MIT,
  Copyright (c) 2025 Masaki Ono / Melissa Audio (`third_party/beat_this/LICENSE.txt`).

## pocketfft

- Project: https://github.com/mreineck/pocketfft - BSD-3-Clause, Copyright (C)
  2010-2018 Max-Planck-Society (`third_party/beat_this/third_party/pocketfft/LICENSE.md`).
  Used by the Beat This! mel-spectrogram preprocessor.

## PFFFT

- Project: https://github.com/marton78/pffft - BSD-style (FFTPACK-derived),
  Copyright (c) 2020 Dario Mambro, (c) 2019 Hayati Ayguen, (c) 2013 Julien Pommier,
  (c) 2004 the University Corporation for Atmospheric Research
  (`third_party/pffft/LICENSE.txt`). Used by the host iSTFT (`src/utils/Fft.h`).

## signalsmith-stretch / signalsmith-linear

- https://github.com/Signalsmith-Audio/signalsmith-stretch - MIT, Copyright (c)
  2022 Geraint Luff / Signalsmith Audio Ltd. (`third_party/signalsmith-stretch/LICENSE.txt`)
- https://github.com/Signalsmith-Audio/linear - MIT, Copyright (c) 2025
  Signalsmith Audio (`third_party/signalsmith-linear/LICENSE.txt`)

## JUCE

- https://juce.com - JUCE 8 is dual-licensed (AGPLv3 / commercial JUCE 8 EULA).
  SAMPLAR is closed-source and therefore relies on a **commercial JUCE 8 licence**
  held by Muziksociety (release requirement - verify the licence tier covers the
  product's revenue before shipping).
- JUCE's bundled dependencies that end up in the binary (zlib, pnglib, jpeglib,
  FLAC, Ogg Vorbis, HarfBuzz, SheenBidi) keep their own permissive licences; the
  texts ship as `JUCE-*.txt` in the bundle.
- The Steinberg VST3 SDK (via JUCE) is used under the proprietary Steinberg VST3
  License, which requires a signed Steinberg VST3 licence agreement (release
  requirement).

## Not shipped (for clarity)

The following components appeared in older SAMPLAR / Sample Monsta builds and are
**not** part of SAMPLAR 1.0.1: SoundTouch (LGPL), Loop Tempo Estimator (GPL),
MiniBPM (GPL), nlohmann/json, PyTorch/LibTorch, OpenBLAS, Python, audio-separator,
ffmpeg.

---

## Licence texts not present in the source tree

### Demucs (MIT)

```
MIT License

Copyright (c) Meta Platforms, Inc. and affiliates.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Beat This! model weights (MIT)

```
MIT License

Copyright (c) 2024 Institute of Computational Perception, JKU Linz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
