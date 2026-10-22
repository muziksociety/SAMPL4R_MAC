# SAMPLAR Relaunch - Market + UX Audit (F)

Date: 2026-10-20
Scope: stem-aware samplers, stem separators, and AI chop/sample tools (2025-2026 landscape)
Method: web search plus page fetches. Some vendor pages returned 403 (serato support, appleinsider, djmag); those facts are taken from search snippets and marked "(snippet)". Prices are list prices seen at research time; promos change often.

Product under review: SAMPLAR (Windows VST3 instrument). Local htdemucs ONNX 4-stem split (vocals, bass, drums, other). Each stem gets chopped into segments that act as MIDI pads, with a per-stem sequencer, per-stem pitch, and MIDI keyboard play inside the DAW. Repo check (commit cf8066ba plus a grep of src/) shows SAMPLAR already has Beat This! BPM detection run on the drum stem, real-time per-stem pitch via signalsmith-stretch, per-stem MIDI routing (pitch bend, mod wheel, aftertouch, CC learn), drag-WAV in, and external drag-out from SModePage. I found **no key-detection code** (no chroma, Krumhansl or Camelot symbols). That needs the owner to confirm.

---

## 0. Headline findings

1. **Stem separation is now a commodity.** Logic Pro, Ableton Live Suite 12.3+, FL Studio Producer+, Cubase 15, Studio One Pro 7 / Fender Studio Pro, MPC, Maschine 3, Koala and Serato Sample all ship it. LALAL.AI, StemVst Master and UVR sell or give away local separation as a utility. SAMPLAR cannot sell on "it splits stems". It has to sell on what you can *do* with four stems at once.
2. **Only a handful of plugins combine separation with a pad sampler:**
   - Serato Sample 2.x (4 stems, real time, $129)
   - GODLIKE CHOP (6 stems offline, 32 pads, $40 launch / $100)
   - Mikey Audio FlipCraft (4 stems real time, 4x32 pads, $119)

   All three treat stems as a **global filter on one sampler**: you toggle or solo stems, then chop the result. None that I found presents "four samplers that share one timeline", which is SAMPLAR's concept.
3. **Samplab is gone.** Apple bought its assets and some of its staff (deal closed June 2026, made public through an EU filing on 2026-10-19). The product shut down on 2026-09-17 and samplab.com now redirects to apple.com. That leaves a hole for Windows producers who used a plugin to pull "notes inside a sample" (audio-to-MIDI, chords).
4. **Every intuitive sampler shares the same three moves:**
   - auto-suggest chops in one click
   - detect key and BPM, then key-shift or sync to the project
   - turn slices into something the DAW can use (MIDI or audio drag-out) in one gesture

   SAMPLAR covers part of the third. The first is missing, and so is the key half of the second.

---

## 1. Feature matrix

Legend: L = local/offline, C = cloud, RT = real-time separation. "?" = not documented in the sources I could reach.

### 1a. Stem-aware samplers / chop instruments (direct competitors)

| Product | Type | Separation | Stems | Speed claim | Chop modes | Key / BPM | Pitch / stretch per chop | Pads / keyboard | Drag-out / export | Price |
|---|---|---|---|---|---|---|---|---|---|---|
| **Serato Sample 2.2** | Plugin (VST/VST3/AU/AAX) | L, RT | 4 (vocals, melody, bass, drums). Per-stem levels and automation since 2.2 | Real time ("analysis is quick... almost eerie" - CDM) | Find Samples (auto), Set Random, Set Slicer (even grid, 1/16 to 16 beats), manual cues. Favorited cues are kept on re-roll | Key + BPM auto | Per pad: pitch, speed, level, filter, attack, release, reverse. Key shift by semitone + detune. Pitch 'n Time stretch | 32 pads, Keyboard Mode (chromatic), velocity, pad quantize, hold/trigger, mono/poly, per-pad outputs, MIDI learn, SLAB controller | Not documented on product page. Stem file is persisted next to the source audio | $129 perpetual or $9.99/mo Producer Suite. 30-day trial |
| **GODLIKE CHOP 1.6.1** | Plugin VST3 (Win), VST3/AU (Mac) | L (offline engine). 16 GB RAM recommended | 6 (vocals, drums, bass, guitar, piano, other) | Not stated | Instant 8 / 16 / 32 slices + custom Cut | Key + tempo detect | Pitch, level, time-stretch 1/2x / 1x / 2x (pitch-preserved), attack/release/fade, reverse | 32 pads (2 banks). HOLD / ONE SHOT / KEYS modes. First-MIDI-note setting | Audio patterns, individual WAVs, MIDI export | $40 launch (reg. $100), one-time. Bundles 10 sample packs |
| **Mikey Audio FlipCraft** | Plugin VST3/AU/CLAP | L, RT toggles (GPU on Apple Silicon) | 4 | "Toggle each stem in real time" | Lazy-chop during playback, auto-chop by beat step, random chop, manual | Tempo + beat markers; key incl. Camelot notation | Pitch +-24 st / +-50 c, stretch 0-400%, pan, gain | 4 layers x 32 pads, MIDI-assignable | Export all slices + stems as WAV, token folder naming | $119 |
| **Akai MPC 3 / MPC Stems** (desktop + standalone HW) | Hardware + desktop app | L (zPlane). Runs standalone on hardware since 2.15 | 4 (vocals, bass, drums, other) | ? | Chop: Threshold, Regions, BPM, Manual. Convert to new program | BPM; Super Timestretch | Full per-pad program editing | 16 pads (MPCe 3D pads on Live III). Pad Perform | Export / resample | Stems launched as a $10 add-on (2.14). Bundled as "Pro Stems" on MPC3 / Live III |
| **Maschine 3** (NI) | Software + controllers | L (iZotope RX tech) | 4 | Users report it is slow, one song at a time, large files | Slice: Detect (transient), Split (N equal), Grid; apply to new group / pads / single pad (keyboard) | BPM | Full sampler | 16 pads + keyboard mode | Drag audio/MIDI | $109 incl. Maschine Central ($39 update) |
| **Koala Sampler** (iOS/Android) | Mobile app | L | 4. You pick which stems go to a new pad | ? | Auto / manual chop | ? | Pitch, FX | 16 pads/bank | Export | Low-cost mobile |
| **Output Arcade** (Kit Generator) | Plugin (subscription) | No separation | - | - | Kit Generator auto-chops any audio. Stretched / Shifted / Chopped modes | Host sync | Per-voice controls | White keys = samples, black keys = modifiers (Resequence, Playhead, Repeater) | Resample | Output One subscription |
| **Antares Auto-Tune Slice** | Plugin | No separation | - | - | Vocal-tuned transient slicing that auto-maps to MIDI | - | Pitch / Auto-Tune, 14 FX | Keys | - | Auto-Tune Unlimited $24.99/mo (~$179 standalone at retailers) |
| **XLN Life** | Plugin + phone recorder | No separation | - | - | Auto-slice recordings, then ML beat / sound / pattern variations; Density / Syncopation / Symmetry | Host sync | - | Sequencers | Drag out | $149 |
| **SAMPLAR (current)** | VST3 (Win) | L (htdemucs ONNX, CPU) | 4 | Not published | Segments per stem (confirm which modes are exposed) | BPM (Beat This! on the drum stem). Key: not found in code | Real-time per-stem pitch (signalsmith-stretch) | Per-stem pads + per-stem sequencer, MIDI keyboard, per-stem PB/MW/AT/CC | Drag-out exists (SModePage) | TBD |

### 1b. DAW-native stem separation (indirect: "good enough and free with my DAW")

| Product | Separation | Stems | Speed / quality modes | Chop path after split | Price |
|---|---|---|---|---|---|
| **Logic Pro 11.2+** (Mac) | L (Apple Silicon only) | 6 (vocals, drums, bass, guitar, piano, other). Presets (acapella, instrumental...) + submix export | Rated "excellent" in a June 2026 DAW comparison | Quick Sampler Slice: Transient / Beat Divisions / Equal Divisions. Start Key mapping: Chromatic / White / Black | $199.99 one-time |
| **Ableton Live 12.3 / 12.4 Suite** | L (Music.AI / Moises algorithms) | 4 | High Speed vs High Quality. Older machines can take "several minutes". 12.4 processes only the audible or selected region | Simpler Slice: Transients / Beats / Regions / Manual. "Slice to New MIDI Track" builds a Drum Rack. Splice "Search with Sound" in the browser | Suite only |
| **FL Studio 21.2+ / 2025** | C by default ("1,000x faster" on FL Cloud), local option | 4 | Cloud fast, local slower | Fruity Slicer 2 (Dull/Medium/Sharp auto-slice, "Dump score to piano roll", auto-dump), Slicex | Producer Edition and above |
| **Cubase 15** | L | 4 | "Moderate". Reviewers say the stems sound heavily processed | Manual | Cubase license |
| **Studio One Pro 7 / Fender Studio Pro** | L | 4 | "Not bad / pretty good" (SOS) | Manual. Splice Search with Sound integrated | License |

### 1c. Separation utilities and stem-adjacent tools (no pad sampler)

| Product | Separation | Stems | Speed claim | Notes | Price |
|---|---|---|---|---|---|
| **LALAL.AI VST** | L after activation, with periodic subscription checks | Up to 7 (vocals, instrumental, drums, bass, piano, acoustic gtr, electric gtr) | ~2-3 s per track claimed (GPU/NPU "Lyra" model) | No sampler. VST3 Win/Mac/Linux, AU beta. No trial | $19.99/mo or $180/yr |
| **Fadr Stems Plugin** | C | 4 (16 stem types in upload mode) | "Quickly" | Record into the plugin, mute/solo stems, drag stems out. Stems stay local if you cancel | $10/mo or $100/yr |
| **StemVst Master** | L, NVIDIA CUDA / TensorRT | 4 or 6 | "In seconds", plus a low-latency preview mode | Drag stems into the session. No sampler | One-time. 5-day / 10-split trial |
| **Hit'n'Mix RipX DAW 8** | L | 6 (voice, bass, drums, guitar, piano, other) | ? | Standalone DAW with note-level editing inside the mix, Track Starters, 11 FX | $99 / Pro $198 |
| **UVR 5** | L | 2-4+ (HTDemucs, MDX-Net, BS-RoFormer, ensembles) | Slow without a GPU. Choosing models is "a hobby in itself" | Free, open source, standalone | Free |
| **Samplab 2** (defunct) | C (uploads) | 4 + audio-to-MIDI + chord editing | - | Shut down 2026-09-17. Assets acquired by Apple | Was free / $9.99/mo |
| **Algonaut Atlas 2** | None | - | - | AI map of one-shot drums, sequencer, Variation/humanize, "New Kit" one-click generation, drag MIDI/audio | $99 |
| **XLN XO** | None | - | - | XO Space similarity map (drag to audition), Playground randomize/swap | ~$149-180 |
| **Sononym** | None | - | - | Local ML similarity browser, search by recording a sound | $99 |
| **Splice (Sounds plugin, Create)** | None (catalog) | - | - | Search with Sound (Listener captures 8 bars), audition in key/tempo, Variations of any sample, Create "Stacks" up to 8 layers | Subscription |
| **Loopcloud 7** | None (catalog) | - | - | Key Lock, auto-stretch to DAW, slice editor, "Flip Sample" one-click (1,000+ patterns, Aug 2025) | Subscription |
| **Roland Melody Flip** (Mar 2026, with Sony CSL) | None | - | - | Analyzes an imported track (tempo, beats, key, chords, genre, mood) and recommends MIDI "Palettes" | ? |
| **Kontakt 8** | None | - | - | Leap loop tool, Chords/Phrases tools. No stem separation found | Komplete |
| **iMPC Pro 2 v3.0** (iOS / AUv3, 2026-09-22) | Paid subscription | 4 | ? | Stems behind a subscription that reviewers call pricey | Sub |

---

## 2. Ten UX patterns that make the best ones feel intuitive and creative

1. **One-click "suggested chops" with re-roll, keeping favorites locked.** Serato's Find Samples algorithm finds regions "suitable for sampling" and places cues on-beat inside them. Set Random and Set Slicer are the alternates. Pressing Autoset again re-rolls, but favorited cues survive. FlipCraft copies this with lazy-chop, auto-chop by beat step and random chop. *The user never faces a blank waveform.*
   - Serato Autoset modes (snippet): https://support.serato.com/hc/en-us/articles/360001461476-Sample-Deck-Autoset-Autoset-Modes
   - Serato product page: https://serato.com/sample
   - FlipCraft (KVR): https://www.kvraudio.com/product/flipcraft-by-mikey-audio

2. **Several slice algorithms one control away: transient, grid/beat, N-equal, manual.** The same four-way choice shows up everywhere: Ableton Simpler (Transients / Beats / Regions / Manual), Logic Quick Sampler (Transient / Beat Divisions / Equal Divisions), MPC (Threshold / Regions / BPM / Manual), Maschine (Detect / Split / Grid), and FL Slicer 2 (Dull / Medium / Sharp sensitivity). Users expect the choice to be undoable and non-destructive; MPC moved chop-type selection into a cancellable popup so slice edits are not lost.
   - Ableton slicing (SOS): https://www.soundonsound.com/techniques/making-sampled-instruments-ableton-live
   - Logic Slice mode: https://support.apple.com/guide/logicpro-ipad/slice-mode-lpipda25caca/3.0/ipados/26
   - MPC chop: https://www.audeobox.com/learn/mpc-software/chop-shop-sampling/
   - Maschine: https://www.native-instruments.com/ni-tech-manuals/maschine-software-manual/en/sampling-and-sample-mapping
   - Fruity Slicer 2: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Slicer%202.htm

3. **Detect key + BPM and show both, then key-shift or sync to the project.** Serato does automatic key/BPM analysis, semitone + detune key shift, and BPM sync. GODLIKE CHOP and FlipCraft detect key (FlipCraft adds Camelot notation for DJs). Loopcloud's Key Lock auto-transposes to a chosen key. Splice auditions everything in project key and tempo. *A sample that is in the wrong key is the number-one reason a flip dies.*
   - Serato: https://serato.com/sample
   - GODLIKE CHOP: https://godlikeloops.com/product/godlike-chop/
   - Loopcloud: https://www.musicradar.com/news/slice-edit-and-remix-millions-of-samples-with-loopcloud-40
   - Splice Search with Sound: https://support.splice.com/en/articles/13860589-how-to-use-search-with-sound-splice-sounds-plugin

4. **A compact per-pad parameter strip.** The common set is pitch, speed/stretch, level, filter, attack/release, reverse, plus playback mode (hold/trigger, one-shot, mono/poly), velocity and per-pad output routing. This is what Serato and GODLIKE CHOP expose; FlipCraft gives +-24 st pitch and 0-400% stretch per pad. The strip appears when the pad is selected, and there are no nested menus.
   - https://serato.com/sample
   - https://godlikeloops.com/product/godlike-chop/
   - https://www.kvraudio.com/product/flipcraft-by-mikey-audio

5. **Keyboard mode: play one chop chromatically, or lay all chops across the keys from a chosen start note.** Serato has Keyboard Mode. Logic's Start Key can map Chromatic, White-only or Black-only. GODLIKE CHOP has a KEYS mode and a "first MIDI note" setting. Output Arcade splits the keyboard so white keys play samples and black keys apply performance modifiers.
   - https://serato.com/sample
   - Logic: https://support.apple.com/guide/logicpro-ipad/lpipda25caca/ipados
   - Arcade keyboard: https://support.output.com/en/articles/10297585-the-keyboard-samplers

6. **Slices become DAW material in one gesture (slice-to-MIDI, drag-out).**
   - FL Slicer 2 "Dump score to piano roll" maps slices low to high, with an Auto-dump option on load.
   - Ableton "Slice to New MIDI Track" builds a Drum Rack with one Simpler per pad.
   - MPC "Convert" turns chops into a new program.
   - Atlas drags patterns out as MIDI, audio or stems.
   - GODLIKE CHOP exports audio patterns, WAVs and MIDI.
   - FlipCraft exports all slices and stems.

   Sources:
   - https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Slicer%202.htm
   - https://modeaudio.com/magazine/turn-drum-loops-into-drum-kits-in-ableton
   - https://support.akaipro.com/en/support/solutions/articles/69000859300-mpc-series-convert-your-chopped-samples-into-a-drum-program
   - https://musictech.com/reviews/software-instruments/algonaut-atlas-2-review/

7. **Stem buttons are one-click mute, shift-click solo, the waveform redraws to match, and levels can be automated.** Serato 2.0 shows stem state in the waveform and uses Shift+Click to solo. Serato 2.2 made stem on/off and levels DAW-automatable. Koala asks which stems you want and drops them on a new pad. Serato DJ adds stem-specific FX (Vocal Echo, Instrumental Echo, Instrumental Braker, Drums Echo), which is a model for stem-aware performance FX.
   - Serato 2.0 release notes: https://serato.com/sample/downloads/2.0.0/releasenotes
   - Serato 2.2: https://mixdownmag.com.au/news/serato-sample-2-2-0-brings-stem-automation-and-native-slab-controller-integration
   - Koala: https://www.musicradar.com/news/koala-sampler-split-stems-ios-android
   - Serato DJ stem FX: https://musictech.com/news/gear/serato-dj-pro-stem-separation/

8. **Audition by gesture: scrub or drag to hear, with no click-load-play loop.** In the XO Space, dragging the pointer triggers every sample it passes over. Serato on SLAB offers waveform scrubbing. Serato colors its waveform by frequency (red = lows, blue = highs), so kicks, bass and hats can be spotted by eye before listening.
   - XO: https://www.kvraudio.com/news/xln-audio-releases-xo---beat-making-tool-44873
   - XO guide: https://assets.xlnaudio.com/documents/xo-getting-started.pdf
   - https://serato.com/sample

9. **A "safe" randomize / variation sandbox with locks.** The products that do this:
   - XO Playground: Sample Combiner / Beat Combiner, randomize one sound or all, in a "safe space" you can back out of.
   - Atlas: "New Kit" one-click, hot-swap, and a Variation section for note-to-note humanizing.
   - XLN Life: ML Beat / Sound / Pattern variations with Density / Syncopation / Symmetry macros.
   - Loopcloud Flip Sample: one-click flips from 1,000+ patterns.
   - Splice: Variations of any sample in any key or tempo.

   *Creativity comes from cheap re-rolls you can always undo.*

   Sources:
   - XO: https://www.kvraudio.com/news/xln-audio-releases-xo---beat-making-tool-44873
   - Atlas: https://musictech.com/reviews/software-instruments/algonaut-atlas-2-review/
   - Life: https://musictech.com/news/gear/xlm-audio-life-plugin-everyday-sounds/
   - Flip Sample: https://www.loopcloud.com/cloud/blog/5309--One-Click-to-Flip-Introducing-Flip-Sample
   - Splice Variations: https://splice.com/blog/splice-sounds-plugin

10. **Instant results, with heavy analysis hidden behind caching or progressive processing.** Approaches by vendor:
    - Serato separates in real time and persists a stem file next to the source, so reloads are instant.
    - Ableton 12.4 separates only the audible or selected region.
    - LALAL claims ~2-3 s per track on GPU/NPU.
    - StemVst offers a low-latency preview mode.
    - Maschine 3 is criticized for slow, one-song-at-a-time separation.

    *Time-to-first-chop is a UX metric that users feel.*

    Sources:
    - https://serato.com/sample/downloads/2.0.0/releasenotes
    - https://help.ableton.com/hc/en-us/articles/26963870210076-What-s-new-in-Live-12-4
    - https://gearspace.com/threads/lalal-ai-vst-plugin-fast-offline-ai-stem-separation-up-to-6-stems.1463545/
    - https://community.native-instruments.com/discussion/38525/maschine-3-stem-separation

Honorable mentions:
- **Search by sound / compatibility suggestions.** Splice Search with Sound recommends samples that match your track's key and tempo. Roland Melody Flip analyzes a song and recommends palettes. https://www.roland.com/us/products/rc_melody_flip/
- **Big, playable performance modifiers on keys.** Arcade's Resequence, Playhead and Repeater. https://www.soundonsound.com/reviews/output-arcade

---

## 3. Gaps no competitor fills well, which "every stem is its own sampler" can own

### Gap A - Cross-stem chops at the same timestamp (the "Stem Grid")
- **Current state.** Serato, FlipCraft and GODLIKE CHOP apply stem toggles to the loaded sample. Serato's stem buttons and the redrawn waveform reflect one stem state per instance, and levels are instance parameters (medium confidence; the Serato support page was 403). GODLIKE CHOP lets you "solo a layer or combine stems" but does not document a stem mix per pad. DAWs dump four stems onto four tracks, and you then slice each one by hand.
- **Opportunity.** Show one shared timeline with four lanes. A single chop marker cuts all four stems at once. Each pad stores its own time range **and** its own stem mask or levels. Examples: pad 1 = vocals only at 1:12, pad 2 = drums + bass at the same 1:12, pad 3 = full mix minus vocals. "Chop once, play four ways." This is the most literal expression of the owner's vision, and I found no one doing it.
- **Build cost.** Moderate. It is mostly data model and UI on top of the existing per-stem buffers.

### Gap B - A "FLIP" generator that recombines chops across stems in key and tempo
- **Current state.** Loopcloud Flip Sample and Splice Create/Stacks recombine *catalog* content, not your own song. XLN Life makes beats from *your recordings*, but one source at a time and with no stems. Melody Flip suggests MIDI, not audio. Serato Find Samples suggests chops but does not arrange them.
- **Opportunity.** One button builds a 4-lane pattern from the song's own stems:
  - a drum chop from bar 12
  - a bass phrase from the bridge, pitch-conformed to the target key
  - a vocal hook from the chorus
  - a melodic loop from the "other" stem

  Each lane is tempo-synced. Locks keep lanes you like, and Density / Variation macros re-roll the rest. Stem-aware ranking (for example, "drum break where the other stems are quiet" or "vocal phrase with low bleed") is something full-mix tools cannot do.
- **Build cost.** Moderate to high. It needs key detection, which SAMPLAR appears to lack. SAMPLAR already has the per-stem sequencer and the drum-stem BPM.

### Gap C - Vocal-chop instrument from the vocal stem (syllable-aware slicing, formant-preserving pitch)
- **Current state.**
  - Antares Auto-Tune Slice has vocal-tuned slicing, but no stem extraction and it needs a $24.99/mo subscription.
  - Little AlterBoy and Pitchmunk are pitch/formant effects, not samplers.
  - Serato / GODLIKE / FlipCraft pitch-shift chops, but none advertise formant preservation or syllable-aware cutting.
  - Vocal-chop articles in 2026 still describe effect chains, not an instrument.
- **Opportunity.** "Vocal Chop" mode on the vocal stem:
  1. Auto-slice on syllable/phrase onsets so cuts never land mid-word.
  2. Map slices across the keyboard (or one slice chromatic).
  3. Use formant-preserving pitch so chops played an octave up don't chipmunk.
  4. Add a formant knob for the character effect on purpose.
  5. Optionally snap each chop to a scale.

  This is the most "creative" and demo-able feature, and the one most likely to go viral in short-form video.
- **Build cost.** Moderate. Check whether signalsmith-stretch's formant options or a dedicated formant shifter fit the CPU budget.

### Secondary opportunities
- **D. Drum stem to kit + groove.** Atlas and XO organize one-shots but cannot pull them *out of a song*. Ableton's Slice-to-Drum-Rack is manual and works on the full mix. Auto-extract kick/snare/hat hits from the drum stem into pads, and export the original groove as MIDI.
- **E. Bass/melody stem to MIDI (the Samplab vacuum).** With Samplab gone (Apple acquisition) and RipX being a standalone DAW, Windows plugin users have no "notes from the sample" tool. Even monophonic bass-to-MIDI would be distinctive. Sources: https://mubert.com/blog/apple-samplab-acquisition-logic-pro and https://www.ithinkdiff.com/apple-acquires-samplab-ai-audio-editing/
- **F. Per-chop bleed / cleanliness indicator.** Users' main complaint about stems is artifacts and bleed. Showing a confidence badge on each suggested chop turns that weakness into a selection aid. Sources: https://www.kits.ai/blog/best-ai-stem-splitters-for-clean-music-stems and https://www.kvraudio.com/forum/viewtopic.php?p=9273345
- **G. Stem-specific performance FX on keys** (Arcade-style black-key modifiers per stem, Serato-DJ-style vocal echo / drum braker).

### SAMPLAR gaps to close before relaunch (table stakes vs Serato / GODLIKE / FlipCraft)
1. **Key detection and key-shift-to-target.** I found no key-detection code. Every direct competitor has it.
2. **One-click suggested chops with re-roll and favorites lock.** Every leader has this.
3. **A published time-to-first-chop.** Benchmark htdemucs ONNX on CPU. If it is longer than ~20-30 s per song, let users start chopping before separation finishes:
   - progressively separate the visible region first, or
   - give an instant full-mix preview while stems render.

   Also cache stems next to the source the way Serato does.
4. **Slice-to-MIDI drag-out** of a pattern or sequence, not only WAV.
5. **6 stems (guitar/piano)** is a spec-sheet checkbox: GODLIKE, Logic, LALAL and RipX have it. It matters less than items 1-4. Don't chase it before the creative workflow is done.

---

## 4. Pricing and positioning

### Price anchors (one-time unless noted)
| Product | Price |
|---|---|
| GODLIKE CHOP | $40 launch / $100 regular |
| Atlas 2, RipX DAW, Sononym | $99 |
| FlipCraft | $119 |
| Serato Sample | $129 (or $9.99/mo suite) |
| XLN Life, XO | ~$149-180 |
| RipX Pro | $198 |
| LALAL.AI plugin | $15-20/mo |
| Fadr Plus | $10/mo |
| Auto-Tune Unlimited (Slice) | $25/mo |
| Splice, Loopcloud, Output | Subscription |

Users push back on subscriptions for utilities (the iMPC stems subscription was called "pricey").

### Recommendation
- **$99 one-time**, with a **$59-69 launch price** for the first 2-4 weeks. This undercuts Serato and FlipCraft, sits above GODLIKE CHOP's "bundle" positioning, and matches Atlas / RipX / Sononym.
- **30-day full trial**, matching Serato and Ableton. Fall back to 14 days (Atlas) if the license server needs a shorter window.
- Optional later: a low-priced rent-to-own (~$9.99/mo for 10 months), not a pure subscription.
- **Positioning line:** "Sample the song, not the mix. Four samplers in one - vocals, drums, bass, melody - 100% local, one price."
- **Proof points to lead with:**
  - the Stem Grid (Gap A)
  - the FLIP button (Gap B)
  - the Vocal Chop instrument (Gap C)
  - local / offline / no uploads / no subscription
  - Windows-first VST3 (Logic's best-in-class splitter is Mac-only; Samplab is gone)
- **Don't lead with separation quality.** It is free in every DAW now, and reviewers rate even Serato's stems as "not always studio quality". Lead with time-to-flip and creative outcomes.
- **Messaging hygiene.** Add a short "clear your samples" note in onboarding and docs. Reviewers already flag copyright risk when stems are used for flips. (https://www.sneakzandbeatz.com/the-lane/akai-impc-pro-2-update-3-0-auv3-plugin-stem-separation-ipad-mac-2026)

### Metrics to instrument for the relaunch (per the product playbook)
- **North star:** % of trial users who drag a chop or pattern into their DAW within the first session.
- **Guardrails:**
  - separation time p50/p90 by CPU class
  - crash rate in host
  - trial-to-paid conversion

---

## 5. Sources (primary list)

Serato Sample
- https://serato.com/sample
- https://www.serato.com/sample/pricing
- https://serato.com/sample/downloads/2.0.0/releasenotes
- https://mixdownmag.com.au/news/serato-sample-2-2-0-brings-stem-automation-and-native-slab-controller-integration
- https://cdm.link/serato-sample-2-0-stem-separation/
- https://musicradar.com/reviews/serato-sample-2-review (snippet)
- https://support.serato.com/hc/en-us/articles/360001461476-Sample-Deck-Autoset-Autoset-Modes (snippet)
- https://musictech.com/news/gear/serato-dj-pro-stem-separation/

GODLIKE CHOP
- https://godlikeloops.com/product/godlike-chop/
- https://godlikeloops.com/blog/best-sample-chopping-plugins-hip-hop-trap/

FlipCraft
- https://www.kvraudio.com/product/flipcraft-by-mikey-audio

Ableton
- https://www.ableton.com/en/stem-separation-in-ableton-live/
- https://help.ableton.com/hc/en-us/articles/26963870210076-What-s-new-in-Live-12-4
- https://mixdownmag.com.au/news/ableton-live-12-3-drops-overnight-with-stem-separation-and-splice-integration/

Logic Pro
- https://www.apple.com/newsroom/2025/05/logic-pro-amplifies-beat-making-on-mac-and-ipad-with-advanced-new-capabilities/
- https://support.apple.com/guide/logicpro-ipad/slice-mode-lpipda25caca/3.0/ipados/26

FL Studio
- https://musictech.com/news/gear/fl-studio-21-2-stem-separation-daw/
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Slicer%202.htm
- https://www.image-line.com/fl-studio-news/fl-studio-2025-whats-new-2

DAW comparison
- https://www.editorskeys.com/blogs/news/stem-separation-in-2026-which-daw-does-it-best

Akai
- https://www.musicradar.com/news/akai-pro-mpc-stems-released
- https://www.soundonsound.com/news/akai-mpcs-gain-stem-separation
- https://www.audiotechnology.com/gear/interfaces/standalone-recorders/mpc-stems-stand-alone
- https://www.bonedo.de/artikel/akai-mpc-live-3-test/
- https://www.audeobox.com/learn/mpc-software/chop-shop-sampling/
- https://www.sneakzandbeatz.com/the-lane/akai-impc-pro-2-update-3-0-auv3-plugin-stem-separation-ipad-mac-2026

Native Instruments
- https://musictech.com/news/gear/native-instruments-maschine-3-update/
- https://www.native-instruments.com/en/pricing/maschine-3/
- https://community.native-instruments.com/discussion/38525/maschine-3-stem-separation
- https://djmag.com/tech/native-instruments-announces-kontakt-8

Koala
- https://www.musicradar.com/news/koala-sampler-split-stems-ios-android

LALAL.AI
- https://lalal.ai/plugin/
- https://bedroomproducersblog.com/2026/05/22/lalal-ai-plugin/
- https://mixdownmag.com.au/news/lalal-ai-brings-six-stem-separation-offline-and-inside-your-daw-with-major-vst-plugin-update/

Fadr
- https://fadr.com/stems-plugin

StemVst Master
- https://stemvst-master.pages.dev/

RipX
- https://musictech.com/news/gear/hitnmix-ripx-daw-8/
- https://www.soundonsound.com/news/hitnmix-ripx-daw-8-now-available

UVR
- https://create.routenote.com/blog/the-best-vocal-stem-separator/
- https://www.kits.ai/blog/best-ai-stem-splitters-for-clean-music-stems

Samplab
- https://mubert.com/blog/apple-samplab-acquisition-logic-pro
- https://appleinsider.com/articles/26/10/19/ai-audio-firm-samplab-acquired-by-apple-to-boost-logic-pro (snippet)
- https://rekkerd.org/?p=236924

Algonaut Atlas
- https://musictech.com/reviews/software-instruments/algonaut-atlas-2-review/

XLN
- https://www.kvraudio.com/news/xln-audio-releases-xo---beat-making-tool-44873
- https://musictech.com/news/gear/xlm-audio-life-plugin-everyday-sounds/

Output Arcade
- https://support.output.com/en/articles/10297585-the-keyboard-samplers
- https://output.com/blog/best-sampler-plugins

Splice
- https://support.splice.com/en/articles/13860589-how-to-use-search-with-sound-splice-sounds-plugin
- https://splice.com/blog/splice-sounds-plugin

Loopcloud
- https://www.loopcloud.com/cloud/blog/5309--One-Click-to-Flip-Introducing-Flip-Sample

Roland Melody Flip
- https://www.roland.com/us/products/rc_melody_flip/

Antares Slice
- https://www.kvraudio.com/forum/viewtopic.php?p=8226691

Sononym
- https://rekkerd.org/sononym-sample-brower-updated-to-v1-3-0-on-sale-at-50-off/

Studio One / Cubase
- https://www.soundonsound.com/reviews/presonus-studio-one-pro-7
- https://www.musicradar.com/music-tech/i-tested-11-of-the-best-stem-separation-tools-and-you-might-already-have-the-winner-in-your-daw (snippet)

Vocal chops
- https://magneticmag.com/2026/07/plugins-for-vocal-chops/

### Confidence notes
- **Serato stems per-instance (not per-pad):** medium. It is inferred from the release notes and support snippets; the support page itself was blocked.
- **"No competitor has a per-pad stem mask":** medium-high for the products surveyed. Smaller or new tools may exist.
- **SAMPLAR lacks key detection:** medium. This comes from a symbol grep only, so the owner should confirm.
- **Prices:** Serato showed $129 on its pricing page, but one snippet said $159. GODLIKE's $40 is a promo price.
