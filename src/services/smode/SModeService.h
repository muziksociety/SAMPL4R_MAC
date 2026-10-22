#pragma once

#include "../../app/Service.h"
#include "../../app/EventBus.h"
#include "SModeSegmentModel.h"
#include "SModeSeparationService.h"
#include "SModeSession.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>
#include <memory>

namespace samplar {

/**
 * SModeService — top-level orchestrator for S-Mode.
 * Coordinates separation, segment model, and MIDI playback.
 * Registered as "smode" in ServiceRegistry.
 *
 * Project persistence (A4/B8/B9): the processor's "smode" AppState binding
 * calls createSessionTree() / restoreSession(). A restore runs entirely in
 * this service (no editor needed): it decodes the saved stems on the worker
 * pool, re-separates the source song if the stems are gone, applies the saved
 * segments and publishes "smode.workflow.ready". Until then the saved session
 * stays pending and every save writes it back unchanged.
 *
 * Events published (message thread):
 *   "smode.workflow.ready"     (stemCount)  stems loaded (new song or restore)
 *   "smode.workflow.error"     (message)
 *   "smode.session.restoring"  (stemCount)  a project restore started
 *   "smode.session.missing"    (message)    restore failed: no stems, no song;
 *                                           the session is kept (never wiped)
 */
class SModeService : public IService
{
public:
    using SessionStatus = session::SessionState::Status;

    explicit SModeService(EventBus& bus);

    void initialise() override;
    void shutdown() override;

    // ── Access ───────────────────────────────────────────────────────
    SModeSegmentModel& getModel() { return model_; }
    const SModeSegmentModel& getModel() const { return model_; }

    SModeSeparationService& getSeparation() { return separation_; }
    const SModeSeparationService& getSeparation() const { return separation_; }

    // ── Workflow ─────────────────────────────────────────────────────
    /** User loads a song: separate stems, then load into model. Discards a
     *  pending restored session, except when the session's audio is missing
     *  and the user picks a file with the saved song's name (relink: the saved
     *  chops are applied to the new stems). */
    void startWorkflow(const juce::File& sourceFile,
                       SModeSeparationService::StemCount stemCount);

    /** Decode stem files on the worker pool and publish them to the model on
     *  the message thread (C15/A19). "smode.workflow.ready" when done.
     *  validateCache: first check (on the worker) that saved stem paths still
     *  form a valid, unchanged stem-cache entry (FullSongService::
     *  validateCachedStemFile); stale stems fail the load -> re-separation. */
    void loadStemAudio(const std::vector<juce::File>& stemFiles, bool validateCache = false);

    /** Is S-Mode active (stems loaded)? */
    bool isActive() const { return active_.load(); }

    /** Stem WAVs are being decoded (after separation or during a restore). */
    bool isLoadingStems() const { return loadingStems_; }

    /** Fully reset S-Mode state (model, active flag, source file, pending session). */
    void reset();

    /** Run automatic downbeat (beat 1) detection on the Drums stem in the
     *  background. On success, updates model.setGridOffsetSeconds() and
     *  publishes "smode.downbeat.detected" on the EventBus with the sample
     *  offset (int64) — UI subscribes to refresh stem-lane grids.
     *  One-shot, non-blocking. BPM must be > 0 (use applyBpm output). */
    void runDownbeatDetectionAsync(double bpm);

    /** Run a heavy job (BPM, transients, spectral colours, WAV export...) on
     *  the service-owned worker pool instead of a detached std::thread.
     *  The pool lives as long as the processor and shutdown() waits for it,
     *  so jobs never outlive the plugin DLL. Rules for jobs:
     *    - capture only data they own (copy audio on the message thread
     *      first; never raw pointers into the model),
     *    - post results back with MessageManager::callAsync guarded by a
     *      SafePointer / alive token + model.getContentGeneration() check,
     *    - long jobs poll getJobCancelFlag() (set first thing in shutdown())
     *      and return early, so unloading the plugin never waits on them.
     *  Returns false (job dropped) after shutdown. */
    bool runBackground(std::function<void()> job);

    /** Set to true when the plugin unloads. Valid for the service lifetime
     *  (the pool is drained before the service dies). Pass it to cancellable
     *  analysis (BeatThis / HybridTempo) inside runBackground jobs. */
    const std::atomic<bool>* getJobCancelFlag() const { return &jobsCancelled_; }

    /** Get/set the source file being processed. */
    const juce::File& getSourceFile() const { return sourceFile_; }
    void setSourceFile(const juce::File& f) { sourceFile_ = f; }

    // ── Project persistence (message thread) ─────────────────────────
    /** The "session" node for the DAW project: the pending restored session
     *  verbatim while a restore is in flight / audio is missing (A4), the
     *  live model when active, an invalid tree when there is no session. */
    juce::ValueTree createSessionTree();

    /** Restore a saved "session" node (invalid tree = no session: resets). */
    void restoreSession(const juce::ValueTree& session);

    /** MissingAudio only: the user located the song (any name). Re-runs the
     *  restore with that file as the source - the saved chops are kept and
     *  applied to the new stems. False (nothing changes) in any other state,
     *  and when the file does not match the saved song (size / fingerprint /
     *  shorter than the chops): then a "smode.session.missing" warning is
     *  published and picking the SAME file again confirms the relink (N-10). */
    bool relinkMissingSource(const juce::File& song);

    SessionStatus getSessionStatus() const { return session_.status(); }
    /** Why the audio is missing (MissingAudio), else empty. */
    juce::String getSessionMessage() const { return session_.message(); }
    /** Exact stem files of the current session (separation result or restore). */
    const std::vector<juce::File>& getStemFiles() const { return stemFiles_; }
    /** Cheap fingerprint of everything the project stores (dirty detection). */
    uint64_t computeSessionFingerprint();
    /** Incremented every time a PROJECT restore finished (the processor
     *  re-baselines its dirty detection instead of marking the project dirty). */
    uint64_t getRestoreCompletionCount() const { return restoreCompletions_; }
    /** Incremented on every accepted relink (the project's song changed: a
     *  user edit, the processor marks the project dirty). */
    uint64_t getRelinkCount() const { return relinkCount_; }

    /** Debug/test hook (smoke host): replaces the opaque page UI state that
     *  the next save writes (lanes, grid division). Message thread. */
    void debugSetUiExtraJson(const juce::String& json) { uiExtraJson_ = json; }

private:
    EventBus& bus_;
    SModeSegmentModel model_;
    SModeSeparationService separation_;
    juce::File sourceFile_;
    std::atomic<bool> active_ { false };

    // Processor-lifetime worker pool for analysis/export jobs (see runBackground).
    std::unique_ptr<juce::ThreadPool> workers_;
    // Liveness token for callAsync lambdas posted by this service.
    std::shared_ptr<std::atomic<bool>> alive_ { std::make_shared<std::atomic<bool>>(true) };
    std::atomic<bool> jobsCancelled_ { false };

    // Event subscriptions
    EventBus::HandlerId separationCompleteSub_ { 0 };
    EventBus::HandlerId separationErrorSub_ { 0 };

    // Session persistence (message thread only)
    session::SessionState session_;
    std::vector<juce::File> stemFiles_;
    juce::String uiExtraJson_;               // page UI state of the session (opaque JSON)
    uint64_t loadGeneration_ { 0 };          // bumps drop in-flight stem decodes
    bool loadingStems_ { false };
    bool restoreFromProject_ { false };      // pending session came from setStateInformation
    bool restoreTriedSeparation_ { false };
    bool sessionSeparationRunning_ { false };   // the running separation belongs to the session
    bool sessionSeparationQueued_ { false };    // waiting for a foreign separation to cancel
    uint64_t restoreCompletions_ { 0 };
    uint64_t relinkCount_ { 0 };
    // Relink validation (review N-10): a mismatching file is accepted only when
    // the user picks the SAME file a second time (the first pick only warns).
    juce::File relinkConfirmFile_;
    // Cached content fingerprint of the session's song (see sourceFingerprintFor).
    juce::File fpFile_;
    juce::int64 fpSize_ { -1 };
    juce::int64 fpMtime_ { 0 };
    juce::String fpValue_;

    /** MissingAudio relink: true when `candidate` matches the saved song (size /
     *  fingerprint / duration) or the user confirmed it by picking it twice;
     *  otherwise publishes a "smode.session.missing" warning and keeps the
     *  pending session untouched. Message thread. */
    bool acceptRelinkCandidate(const juce::File& candidate);
    /** Copy of the pending session pointing at `song` (path, size, mtime, fingerprint). */
    juce::ValueTree relinkedPendingCopy(const juce::File& song);
    /** session::computeSourceFingerprint, cached per (path, size, mtime). */
    juce::String sourceFingerprintFor(const juce::File& song);
    /** Header-only read: audio length in seconds (0 = unknown / unreadable). */
    static double readAudioDurationSeconds(const juce::File& file);

    /** Called when separation completes — loads stems into model. */
    void onSeparationComplete(const juce::var& stemPaths);

    /** Called on separation error. */
    void onSeparationError(const juce::var& errorMsg);

    struct DecodedStems;
    void onStemsDecoded(uint64_t generation, DecodedStems& decoded);
    void startSessionSeparation();
    void continueRestoreWithoutStems(const juce::String& why);
    void beginPendingSession(const juce::ValueTree& session, bool fromProject);

    /** Decode one audio file (worker thread). Stops early when `abort` is set. */
    static juce::AudioBuffer<float> loadAudioFile(const juce::File& file, double& outSampleRate,
                                                  const std::atomic<bool>& abort);
};

} // namespace samplar
