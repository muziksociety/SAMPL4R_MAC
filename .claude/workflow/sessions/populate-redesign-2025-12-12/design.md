# Populate Algorithm Redesign

## Executive Summary

The current DrumAnalyzer classification is inaccurate because it:
1. Uses rigid frequency thresholds that don't match real-world drum samples
2. Calculates attack time but NEVER uses it in classification
3. Finds single FFT peak instead of perceptually-weighted centroid

**Solution**: Improve DrumAnalyzer by using the already-calculated attack time and adding transient analysis.

## Design Approach: Enhance Existing System

This design IMPROVES the existing color/frequency system rather than replacing it.

### Phase 1: Use Attack Time in Classification

**Current State (unused):**
```cpp
result.attackTimeMs = calculateAttackTimeMs(samples, numSamples, sampleRate);
// ... never used in classifySubType()
```

**Proposed Change:**
```cpp
// Add attackTimeMs parameter to classifySubType
result.subType = classifySubType(
    result.band,
    result.spectralFlatness,
    result.dominantFrequency,
    result.attackTimeMs  // NEW PARAMETER
);
```

**Attack Time Classification Rules:**
| Attack Time | Significance |
|-------------|--------------|
| < 5ms | Very percussive (typical of kicks, snares, hi-hats) |
| 5-15ms | Moderately percussive (toms, congas) |
| 15-30ms | Soft attack (might be pad/sustain layer) |
| > 30ms | Likely not a drum hit (ambient/FX) |

### Phase 2: Transient-Focused Spectral Centroid

Replace single-peak dominant frequency with weighted spectral centroid of attack portion.

**Current Problem:**
- A kick's transient "click" might peak at 2000Hz
- The actual body is at 60-100Hz
- Current algorithm picks the wrong frequency

**Proposed Solution:**
```cpp
float calculateTransientSpectralCentroid(
    const float* samples,
    int numSamples,
    double sampleRate,
    float attackMs = 30.0f  // Focus on first 30ms
);
```

Algorithm:
1. Extract first 30ms (or to peak, whichever is shorter)
2. Apply FFT to transient portion only
3. Calculate energy-weighted centroid: Σ(f[i] * mag[i]) / Σ(mag[i])
4. This gives the "perceived pitch" of the attack

### Phase 3: Refined Band Thresholds

**Current (too rigid):**
```cpp
if (frequency < 250.0f) return DrumBand::Kick;
else if (frequency < 800.0f) return DrumBand::Snare;
else if (frequency < 3000.0f) return DrumBand::Tom;
else return DrumBand::HiHat;
```

**Proposed (with attack-aware logic):**
```cpp
DrumBand frequencyToBand(float centroid, float attackMs, float flatness) {
    // Fast attack + low centroid = Kick
    if (centroid < 300.0f && attackMs < 10.0f)
        return DrumBand::Kick;

    // Fast attack + mid-high centroid + noisy = Snare
    if (centroid >= 200.0f && centroid < 1500.0f &&
        attackMs < 10.0f && flatness > 0.35f)
        return DrumBand::Snare;

    // Fast attack + very high centroid OR very noisy = HiHat
    if ((centroid > 4000.0f || flatness > 0.6f) && attackMs < 8.0f)
        return DrumBand::HiHat;

    // Tonal mid-range = Tom
    if (centroid >= 100.0f && centroid < 800.0f && flatness < 0.4f)
        return DrumBand::Tom;

    // Fallback to current logic
    return currentFrequencyToBand(centroid);
}
```

### Phase 4: Add Transient Energy Ratio

New feature to distinguish percussive hits from sustained sounds:

```cpp
float calculateTransientRatio(const float* samples, int numSamples, double sampleRate) {
    // RMS of first 20ms
    int attackSamples = (int)(0.020 * sampleRate);
    float rmsAttack = calculateRms(samples, attackSamples);
    float rmsTotal = calculateRms(samples, numSamples);
    return rmsAttack / (rmsTotal + 0.0001f);
}
```

| Ratio | Meaning |
|-------|---------|
| > 0.7 | Very percussive (typical drum hit) |
| 0.4-0.7 | Moderate (could be drum or other) |
| < 0.4 | Sustained sound (less likely drum hit) |

## Files to Modify

1. **src/utils/DrumAnalyzer.h**
   - Add `calculateTransientSpectralCentroid()` method
   - Add `calculateTransientRatio()` method
   - Modify `classifySubType()` signature to accept attackMs

2. **src/utils/DrumAnalyzer.cpp**
   - Implement new spectral centroid calculation
   - Implement transient ratio calculation
   - Update `frequencyToBand()` with attack-aware logic
   - Update `classifySubType()` to use all features

## User-Requested Features

### A. Visual Category Indicator (k/s/p/h letter)

Display small letter on sample widget top-left corner:
- **K** = Kick (red text)
- **S** = Snare (cyan text)
- **H** = HiHat (green text)
- **P** = Percussion/Tom (yellow text)
- **X** = Other/Unknown (gray text)

**File to modify:** `src/ui/Components/SampleBrowserTile.cpp` (or equivalent tile component)

### B. Right-Click Manual Reclassification

Add context menu option: "Classify as..." with submenu:
- Kick
- Snare
- HiHat
- Percussion

**Storage:** Session-only (per user preference)

**Files to modify:**
1. `SampleBrowserPage.cpp` - Add right-click handler
2. Add `manualOverrides_` map: `std::map<int, DrumCategory>`
3. `onRequestSampleForCategory` - Check manual overrides first

## Implementation Order

1. Improve DrumAnalyzer.cpp classification (core fix)
2. Build and test classification accuracy
3. Add visual indicators to sample widgets
4. Add right-click manual override

## Success Criteria

- Kick samples with body at 60-100Hz are correctly classified even if transient peaks higher
- Snares with prominent snare wires (4000Hz+) are not misclassified as HiHat
- Hi-hats are correctly identified by their high-frequency noisy character
- Attack time < 5ms correlates strongly with correct drum classification
- Users can manually override any misclassification (session-only)
