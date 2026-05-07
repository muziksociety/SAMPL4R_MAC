# FL Studio crash analysis — SAMPLAR batch-switch (2026-04-29)

## Crash metadata

- **Dump file:** `C:\Users\Muzik\AppData\Local\CrashDumps\FL64.exe.9312.dmp`
- **Dump time:** 2026-04-29 11:13 (74 MB)
- **Host process:** `FL64.exe` (FL Studio 64-bit)
- **Plugin module:** `SAMPLAR.vst3` (deployed build 2026-04-28 16:48)
- **Debugger used:** `cdbX64.exe` v10.0.29547.1002 from
  `C:\Users\Muzik\AppData\Local\Microsoft\WindowsApps\cdbX64.exe`
  (shipped by the `Microsoft.WinDbg` UWP package, already installed)
- **Symbols:** Microsoft public symbol server resolved system DLLs and the
  deployed `SAMPLAR.pdb` was already side-by-side with the VST3, so all
  `Sample_Monsta!samplar::...` frames came back fully symbolicated on the first
  pass. No PDB rebuild was actually required to identify the crash, but the
  CMake change (`/Zi` + `/DEBUG` for Release) is still committed so future
  Release artefacts have guaranteed PDB coverage.

## Exception

```
ExceptionAddress: 00007ffb019d286e (ucrtbase!abort+0x4e)
ExceptionCode:    c0000409   Security check failure / stack buffer overrun
                              (== fail-fast / __fastfail / CRT abort path)
PROCESS_NAME:     FL64.exe
FAILURE_BUCKET:   FAIL_FAST_FATAL_APP_EXIT_c0000409_ucrtbase.dll!abort
```

`c0000409` from `ucrtbase!abort` is the CRT fail-fast path. In our context the
likely trigger is heap corruption / vector iterator invalidation detected by
`std::vector` debug invariants when `interleavedOut_` / `interleavedIn_` /
`tmpBlock_` are mutated from one thread while another thread is mid-read.

## Faulting module / thread

Faulting frame is **inside SAMPLAR**, audio thread:

```
00 ucrtbase!abort+0x4e
01 ucrtbase!terminate+0x1a (inferred)
02 Sample_Monsta!samplar::SoundTouchSource::getNextAudioBlock+0x7b
03 Sample_Monsta!samplar::ProcessingAudioSource::getNextAudioBlock+0x34
04 Sample_Monsta!juce::MixerAudioSource::getNextAudioBlock+0x173
05 (inline) Sample_Monsta!samplar::PluginPreviewService::getNextAudioBlock+0xe
06 Sample_Monsta!samplar::SamplarAudioProcessor::processBlock+0x479
07 Sample_Monsta!juce::JuceVST3Component::processAudio<float>+0x165
08 Sample_Monsta!juce::JuceVST3Component::process+0x1ac
09 FL64.exe (VST3 host process callback)
```

Top-of-stack offset `+0x7b` inside `SoundTouchSource::getNextAudioBlock`
(implementation lives in
`src/audio/Preview/SoundTouchSource.h:54-96`) lands in the inner `while`
loop — most plausibly the
`st_.receiveSamples(interleavedOut_.data(), ...)` / `ensureInterleavedOutSize`
sequence (lines 64-72) or the immediately following `feedToSoundTouch` ?
`interleavedIn_.data()` path (lines 78-83). Both touch the
`std::vector<float>` members that are NOT protected against concurrent
resize from any other thread.

## Smoking gun on the message thread

The other interesting thread in the dump is the JUCE message thread, which
was actively rebuilding the preview's audio chain at the moment of crash:

```
01 ntdll!ZwWaitForAlertByThreadId        (just woke)
02 Sample_Monsta!samplar::WavePreviewPage::loadFileForDisplay+0xe4
03 Sample_Monsta!samplar::WavePreviewPage::restoreFileState+0xdd
04 Sample_Monsta!samplar::WavePreviewPage::switchToFile+0x1e4
05 Sample_Monsta!std::_Func_class<void>::operator()+0x6
06 Sample_Monsta!juce::NullCheckedInvocation::invoke+0xf
07 Sample_Monsta!juce::ComboBox::handleAsyncUpdate+0x64
08 Sample_Monsta!juce::InternalMessageQueue::dispatchMessage+0x9
09 Sample_Monsta!juce::InternalMessageQueue::dispatchMessages+0x97
0a Sample_Monsta!juce::InternalMessageQueue::messageWndProc+0x46
```

This is exactly the user-reported scenario: switching between samples in the
batch via the file ComboBox triggers `WavePreviewPage::switchToFile` ?
`restoreFileState` ? `loadFileForDisplay`.

Inspecting `loadFileForDisplay` (src/ui/Pages/WavePreviewPage.cpp:418-465)
shows the lifecycle:

```
432  audioEngine_.setSource(nullptr);
437  transport_.setSource(nullptr);
444  thumbnail_.setSource(src.release());
451  transport_.setSource(readerSource_.get(), 0, nullptr, reader->sampleRate);
454  stSource_ = std::make_unique<SoundTouchSource>(&transport_, false);
459  audioEngine_.setSource(processingSource_.get());
```

Line 454 destroys the previous `SoundTouchSource` (and its
`interleavedIn_/Out_/tmpBlock_/inputFifo_`) and constructs a fresh one,
without any synchronization barrier against the host's audio thread, which —
per the crashing stack — is at that very instant inside
`SoundTouchSource::getNextAudioBlock` reading those vectors via the chain
`SamplarAudioProcessor::processBlock ? PluginPreviewService ? MixerAudioSource ?
ProcessingAudioSource ? SoundTouchSource`.

`audioEngine_.setSource(nullptr)` on line 432 is meant to detach, but it
detaches from `audioEngine_`, not from the VST3 audio callback's
`MixerAudioSource` reached via `PluginPreviewService::getNextAudioBlock`. The
old `stSource_` remains reachable from `processingSource_` and from whatever
the mixer was holding when the audio thread last sampled it.

## Diagnosis (2 sentences)

While the user was rapidly switching between samples in the batch, the
message thread inside `WavePreviewPage::loadFileForDisplay` destroyed and
recreated `stSource_` (the `SoundTouchSource`) without synchronizing with
the VST3 audio callback, which was concurrently running through that exact
same `SoundTouchSource::getNextAudioBlock`. The race corrupts the
`interleavedIn_/Out_` `std::vector` members (or leaves `input_` dangling
to the freed `transport_` reader), tripping a CRT fail-fast (`abort`,
`c0000409`) on the audio thread inside SoundTouch's vector access.

## Code location to harden

- **Primary:** `src/audio/Preview/SoundTouchSource.h:54-96`
  (`SoundTouchSource::getNextAudioBlock`) — needs to be fed only by an
  audio-thread-safe owning structure, or guarded by an audio-thread-safe
  pointer swap.
- **Root cause site:** `src/ui/Pages/WavePreviewPage.cpp:418-465`
  (`WavePreviewPage::loadFileForDisplay`) — must not destroy `stSource_`
  while the audio chain still references it. Options:
  1. Suspend the VST `processBlock` for the duration of the swap (e.g. via
     `juce::AudioProcessor::suspendProcessing(true)` in
     `SamplarAudioProcessor`, then resume).
  2. Hot-swap with an `AbstractFifo` / `juce::SpinLock::ScopedTryLockType`
     pattern around the source pointers held by `PluginPreviewService`.
  3. Move the destruction of the old `stSource_` onto a deferred/GC list
     released only after the next audio callback observes the new source.
- **Secondary:** `src/audio/PluginPreviewService.h` — confirm it holds its
  source pointer atomically (raw pointer reads/writes from two threads are
  the smoking gun here).

## Action items

1. Land the CMake `/Zi`+`/DEBUG` Release-PDB change (this commit) so future
   crashes get full symbols even on stripped installer builds.
2. Reproduce locally by spamming combo-box switches under a debug build with
   ASan/MSVC iterator-debug enabled.
3. Implement option (1) above as the smallest safe fix:
   `audioProcessor.suspendProcessing(true)` at the top of
   `loadFileForDisplay` and `false` at the end. Verify under FL Studio with
   the same batch-switch scenario.
4. Add `juce::Logger::writeToLog("[Preview] loadFileForDisplay swap "
   "begin/end ...")` lines around the source rebuild so future repros
   produce a clear timeline in the log.
