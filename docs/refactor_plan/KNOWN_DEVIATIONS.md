# Known Deviations and Risks (to track during SAMPLAR port)

Status: empty until implementation begins. Pre-populated with potential divergences to watch for.

Potential Deviations
- Frequency Shift: exact SSB/QIFFT parity may differ; start with ring-mod approximation and document differences.
- Pitch/Time: library choice (Rubber Band/SoundTouch) will not be bit-identical to Python; validate tolerances.
- Pitch Bend: realtime long/steep bends may require offline rendering; document UX.
- Visuals: minor UI spacing/anti-aliasing differences due to JUCE vs Qt drawing; keep metrics close.
- Keyboard focus: JUCE focus traversal differs; ensure shortcuts match SM4 behavior where possible.
- Separation (IPC): GPU availability and model versions may change outputs; pin versions and hash assets.

Process
- When a deviation is discovered, record:
  - Module/path affected
  - Expected vs actual behavior
  - Impact (audio/UX/perf)
  - Mitigation plan and target milestone

Links
- See `13_Audit_and_Gap_Analysis.md` for checklist and coverage status.

