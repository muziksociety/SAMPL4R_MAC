# Audio Research Report: Demucs Model Comparison for Drum Separation

**Date**: 2026-03-12
**Context**: SAMPLAR currently uses `htdemucs_6s` via `audio-separator` v0.41.1

---

## Research Question

Which Demucs (or compatible) model produces the best drum isolation quality for SAMPLAR's drum separation pipeline?

---

## Key SDR Benchmark Data (Drums Stem Only)

Source: MVSep benchmark dataset (multisong evaluation), confirmed via arxiv:2305.07489v2

| Model | Drums SDR | Bass SDR | Vocals SDR | Notes |
|-------|-----------|----------|------------|-------|
| **htdemucs_ft** | **11.24** | 12.05 | 8.33 | Best quality, 4x slower |
| htdemucs | 10.90 | 11.74 | 8.18 | Default, fastest |
| hdemucs_mmi | ~9.7 | 12.00 | 10.30 | Older MMI training |
| htdemucs_6s | 10.59 | 11.42 | 8.17 | **CURRENTLY USED** |

**Critical finding**: htdemucs_6s produces drums SDR of 10.59. htdemucs_ft produces 11.24 — that is a **+0.65 dB** improvement in drum isolation, which is meaningful at this scale.

---

## Why htdemucs_6s Underperforms for Drums

The 6-stem model separates 6 sources: bass, guitar, piano, drums, vocals, other. Adding guitar and piano creates two problems:

1. **Model capacity is split further** — the same network architecture must now distinguish 6 classes instead of 4. Each additional stem degrades performance on existing stems.
2. **The "other" stem collapses** — with guitar and piano carved out, the residual "other" stem loses coherence. Its SDR drops to just 2.63, compared to 5.57 for the 4-stem models. Instruments that bleed into guitar/piano training territory can contaminate the drum stem.
3. **Piano source is explicitly documented as broken** — Facebook Research README states "the piano source is not working great at the moment." An undertrained source class can introduce artifacts into adjacent frequency bands that affect drums.

**Conclusion**: Using htdemucs_6s for drum isolation specifically is the wrong model choice. The extra stems add complexity that hurts drum quality relative to the 4-stem models.

---

## Model Comparison Summary

### htdemucs (4-stem, default)
- Drums SDR: 10.90
- Speed: Fastest reference point
- Quality vs 6s: +0.31 dB drums improvement over htdemucs_6s
- Available in audio-separator as: `htdemucs.yaml`

### htdemucs_ft (4-stem, fine-tuned)
- Drums SDR: 11.24
- Speed: **4x slower** than htdemucs (confirmed by Facebook Research)
- Quality improvement over htdemucs: approximately 1-3% per community reports from mvsep.com
- Quality improvement over htdemucs_6s: +0.65 dB drums SDR
- Available in audio-separator as: `htdemucs_ft.yaml`
- Tradeoff: Requires 8GB+ RAM; can crash on lower-memory systems
- Verdict: **Best drum SDR among all Demucs models**

### htdemucs_6s (6-stem, currently used)
- Drums SDR: 10.59
- Speed: Similar to htdemucs baseline
- Problem: Weakest drum SDR of the v4 group despite being the current choice
- The 6-stem design actively hurts drum isolation quality
- Available in audio-separator as: `htdemucs_6s.yaml`
- Verdict: **Wrong model for drum-focused isolation**

### hdemucs_mmi (hybrid demucs MMI)
- Drums SDR: ~9.7 (MVSep benchmark)
- This is a v3-era model with MMI (mutual information) training
- Clearly outperformed by v4 htdemucs variants
- Available in audio-separator as: `hdemucs_mmi.yaml`
- Verdict: Obsolete for this use case

---

## Newer/Alternative Architectures (2024-2025)

### Mel-Band RoFormer
- Architecture: Mel-frequency band-split transformer (ByteDance AI Labs)
- Drums SDR: 9.91 dB on MUSDB18HQ (vs htdemucs baseline)
- **Note**: This is on MUSDB18HQ, while Demucs numbers above are from MVSep (different dataset; MVSep tends to score higher). On equivalent MUSDB18HQ comparison, BS-RoFormer outperforms hDemucs by +2.08 dB average, and Mel-RoFormer adds another +0.30 dB over BS-RoFormer on drums.
- Models in audio-separator: Primarily vocal-focused variants are in production. Drum-specific Mel-RoFormer models exist in the ZFTurbo training repo but are not yet mainstream in audio-separator's bundled model list.
- **Practical status**: Not currently available as a ready-to-use drum model in audio-separator v0.41.1 for simple `--single_stem Drums` workflow.

### MDX-Net (kuielab)
- `kuielab_a_drums.onnx` — Drum-specific MDX model
- Drums SDR: ~7.7 per audio-separator repo owner
- Clearly inferior to htdemucs_ft for drums
- Verdict: Not recommended for drum isolation

---

## Speed vs Quality Tradeoff

For SAMPLAR (processes user audio files, not real-time):

| Model | Relative Speed | Drums SDR | Recommended? |
|-------|---------------|-----------|--------------|
| htdemucs_ft | 0.25x (4x slower) | 11.24 | YES — for quality priority |
| htdemucs | 1x (baseline) | 10.90 | YES — for speed priority |
| htdemucs_6s | ~1x | 10.59 | NO — current choice is suboptimal |

The 4x slower processing of htdemucs_ft is the main tradeoff. For a plugin that processes drum samples (which tend to be short, typically 1-8 bars), 4x slower on a short clip is still fast in absolute terms. A 4-bar drum loop at 44.1kHz is roughly 3-10 seconds of audio. 4x slower on 3-10 seconds is negligible.

---

## Recommended Action

**Switch from `htdemucs_6s` to `htdemucs_ft`**.

Rationale:
- htdemucs_ft achieves the highest drum SDR (11.24) of all available Demucs models
- The 4x speed penalty is inconsequential for short drum samples
- htdemucs_6s was designed for multi-instrument extraction, not drum isolation; using it for `--single_stem Drums` discards its only advantage (guitar/piano stems) while accepting its quality penalty
- The change is a one-line config update in the Python bridge or wherever the model name is specified

**Fallback**: Use `htdemucs` if memory constraints make htdemucs_ft unstable (requires 8GB+ RAM).

**Do not use** htdemucs_6s for drum-only extraction. There is no advantage and measurable quality loss.

---

## Audio Separator Usage

Both models are available in audio-separator v0.41.1:

```python
# Current (suboptimal for drums)
separator = Separator(model_file_path="htdemucs_6s.yaml")

# Recommended (best drum quality)
separator = Separator(model_file_path="htdemucs_ft.yaml")

# Fast alternative (still better than htdemucs_6s for drums)
separator = Separator(model_file_path="htdemucs.yaml")
```

The `--single_stem Drums` flag works identically with all three models.

---

## References

- Facebook Research Demucs README: https://github.com/facebookresearch/demucs
- "Benchmarks and leaderboards for sound demixing tasks", arXiv:2305.07489v2: https://arxiv.org/html/2305.07489v2
- MVSep Demucs4 HT algorithm page with per-stem SDR: https://mvsep.com/algorithms/3
- audio-separator PyPI (nomadkaraoke): https://pypi.org/project/audio-separator/
- audio-separator GitHub discussions "which model" #133: https://github.com/nomadkaraoke/python-audio-separator/discussions/133
- Mel-Band RoFormer paper, arXiv:2310.01809: https://arxiv.org/html/2310.01809v1
- Sound Demixing Challenge 2023, arXiv:2308.06979v2: https://arxiv.org/html/2308.06979v2
- drumsep (inagoy) Hybrid Demucs drum separation: https://github.com/inagoy/drumsep
- IEEE: Benchmarking Models for Deep Drum Source Separation: https://ieeexplore.ieee.org/document/10704147/
