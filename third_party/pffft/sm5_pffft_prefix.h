/* Generated for Sample Monsta 5: prefixes every external PFFFT symbol with sm5_ so this
   vendored copy (marton78 fork) can be linked next to another pffft (loop-tempo-estimator
   builds the original Pommier pffft under the same names). Force-included into the PFFFT
   C sources by CMake and included by src/utils/Fft.h before pffft.h. Regenerate when
   updating PFFFT (see README_SM5.md). The *_nosimd names are plain macros in scalar-only
   builds (harmless redefinition there); with SSE/NEON they are real functions. */
#pragma once

#define pffft_aligned_free sm5_pffft_aligned_free
#define pffft_aligned_malloc sm5_pffft_aligned_malloc
#define pffft_assert1 sm5_pffft_assert1
#define pffft_assert4 sm5_pffft_assert4
#define pffft_cplx_finalize sm5_pffft_cplx_finalize
#define pffft_cplx_preprocess sm5_pffft_cplx_preprocess
#define pffft_destroy_setup sm5_pffft_destroy_setup
#define pffft_is_power_of_two sm5_pffft_is_power_of_two
#define pffft_is_valid_size sm5_pffft_is_valid_size
#define pffft_min_fft_size sm5_pffft_min_fft_size
#define pffft_nearest_transform_size sm5_pffft_nearest_transform_size
#define pffft_new_setup sm5_pffft_new_setup
#define pffft_next_power_of_two sm5_pffft_next_power_of_two
#define pffft_priv_impl sm5_pffft_priv_impl
#define pffft_real_finalize sm5_pffft_real_finalize
#define pffft_real_finalize_4x4 sm5_pffft_real_finalize_4x4
#define pffft_real_preprocess sm5_pffft_real_preprocess
#define pffft_real_preprocess_4x4 sm5_pffft_real_preprocess_4x4
#define pffft_simd_arch sm5_pffft_simd_arch
#define pffft_simd_size sm5_pffft_simd_size
#define pffft_transform sm5_pffft_transform
#define pffft_transform_internal sm5_pffft_transform_internal
#define pffft_transform_internal_nosimd sm5_pffft_transform_internal_nosimd
#define pffft_transform_ordered sm5_pffft_transform_ordered
#define pffft_zconvert_zp sm5_pffft_zconvert_zp
#define pffft_zconvert_zp_nosimd sm5_pffft_zconvert_zp_nosimd
#define pffft_zconvolve sm5_pffft_zconvolve
#define pffft_zconvolve_accumulate sm5_pffft_zconvolve_accumulate
#define pffft_zconvolve_accumulate_nosimd sm5_pffft_zconvolve_accumulate_nosimd
#define pffft_zconvolve_no_accu sm5_pffft_zconvolve_no_accu
#define pffft_zconvolve_nosimd sm5_pffft_zconvolve_nosimd
#define pffft_zconvolve_scale sm5_pffft_zconvolve_scale
#define pffft_zconvolve_scale_nosimd sm5_pffft_zconvolve_scale_nosimd
#define pffft_zconvolve_zp sm5_pffft_zconvolve_zp
#define pffft_zconvolve_zp_nosimd sm5_pffft_zconvolve_zp_nosimd
#define pffft_zreorder sm5_pffft_zreorder
#define pffft_zreorder_nosimd sm5_pffft_zreorder_nosimd
#define pffftd_aligned_free sm5_pffftd_aligned_free
#define pffftd_aligned_malloc sm5_pffftd_aligned_malloc
#define pffftd_is_power_of_two sm5_pffftd_is_power_of_two
#define pffftd_next_power_of_two sm5_pffftd_next_power_of_two
#define validate_pffft_simd sm5_validate_pffft_simd
#define validate_pffft_simd_ex sm5_validate_pffft_simd_ex
