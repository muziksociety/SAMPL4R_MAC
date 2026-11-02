# PFFFT (vendored)

Source: https://github.com/marton78/pffft  commit aa16fd3db58de4ba5dae8b0438440bb9da46b6fa
Only the single-precision real/complex FFT is vendored: src/pffft.c, src/pffft_common.c,
src/pffft_priv_impl.h, src/simd/pf_*float*.h + pf_fma_x86.h, include/pffft/pffft.h.

License: BSD-style (see LICENSE.txt). Redistribution in BINARY form must reproduce the
copyright notices, conditions and disclaimer from LICENSE.txt in the product documentation
(e.g. the installer's third-party notices / manual).

## SM5 changes
- `sm5_pffft_prefix.h` (generated): renames every external PFFFT symbol to `sm5_pffft_*`,
  because `third_party/loop-tempo-estimator` links the original Pommier pffft under the same
  C names. CMake force-includes it into the PFFFT sources; `src/utils/Fft.h` includes it
  before `pffft.h`. Regenerate it when updating PFFFT:
  `grep -ohE "\b(pffftd?_[a-z0-9_]+|validate_pffftd?_simd(_ex)?)\b" src/*.c src/*.h src/simd/*.h include/pffft/pffft.h | grep -vE "_t$" | sort -u`
- NEON is enabled with `PFFFT_ENABLE_NEON` (Apple Silicon / ARM builds).
- License text ships in every build via `THIRD_PARTY_NOTICES.txt` (cmake/ThirdPartyNotices.cmake).
