#include "SModeService.h"
#include "../analysis/DownbeatDetector.h"
#include "../FullSongService.h"
#include "../../utils/DeferredRelease.h"
#include <limits>

namespace samplar {

SModeService::SModeService(EventBus& bus)
    : bus_(bus)
    , separation_(bus)
{
}

void SModeService::initialise()
{
    juce::Logger::writeToLog("[SModeService] === initialise ===");

    jobsCancelled_ = false;
    workers_ = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}
                                                      .withThreadName("SAMPLAR worker")
                                                      .withNumberOfThreads(2));
    juce::Logger::writeToLog("[SModeService] worker pool started (2 threads)");

    separation_.initialise();

    // Subscribe to separation events
    separationCompleteSub_ = bus_.subscribe("smode.separation.complete",
        [this](const juce::var& v) { onSeparationComplete(v); });

    separationErrorSub_ = bus_.subscribe("smode.separation.error",
        [this](const juce::var& v) { onSeparationError(v); });

    juce::Logger::writeToLog("[SModeService] Initialised, subscribed to separation events");
}

void SModeService::shutdown()
{
    juce::Logger::writeToLog("[SModeService] === shutdown ===");
    const double t0 = juce::Time::getMillisecondCounterHiRes();

    alive_->store(false);
    // Cancel in-flight jobs FIRST (stem decode, downbeat, BPM analysis that
    // polls getJobCancelFlag()), so the drain below does not wait on them.
    jobsCancelled_ = true;

    bus_.unsubscribe("smode.separation.complete", separationCompleteSub_);
    bus_.unsubscribe("smode.separation.error", separationErrorSub_);

    separation_.shutdown();

    // Wait for in-flight analysis/export jobs (they only touch their own
    // copied data). Cancellable jobs return within milliseconds; the 20 s cap
    // only matters for a job that ignores the cancel flag.
    if (workers_)
    {
        if (const int jobs = workers_->getNumJobs(); jobs > 0)
            juce::Logger::writeToLog("[SModeService] waiting for worker pool ("
                                     + juce::String(jobs) + " job(s), cancel flag set)");
        const bool drained = workers_->removeAllJobs(true, 20000);
        if (drained)
        {
            // Idle + empty: joining its threads took 100s of ms under CPU load
            // (impl_unload.md), so ~ThreadPool runs on a module-pinned thread.
            DeferredRelease::run([pool = std::shared_ptr<juce::ThreadPool>(std::move(workers_))]() mutable { pool.reset(); },
                                 "SModeService worker pool");
        }
        else
        {
            // ~ThreadPool would TerminateThread a job that ignores the cancel
            // flag (a thread killed inside the heap / loader lock can hang the
            // whole host). Leaking the pool is the lesser evil (review N-9):
            // jobs only touch data they own (runBackground rules) and post
            // through the alive_ token, which is already false.
            juce::Logger::writeToLog("[SModeService] ERROR worker pool drain timed out after 20 s ("
                                     + juce::String(workers_->getNumJobs()) + " job(s)) - pool LEAKED, not destroyed");
            (void) workers_.release();
        }
    }

    // Stem audio + undo history: freed off the message thread (the jobs that
    // could read them are drained or own copies).
    DeferredRelease::run([content = model_.takeContentForRelease()]() mutable { content.reset(); },
                         "SModeService stem buffers");
    active_ = false;
    juce::Logger::writeToLog("[SModeService] shutdown done in "
                             + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + " ms");
}

bool SModeService::runBackground(std::function<void()> job)
{
    if (!alive_->load() || workers_ == nullptr)
    {
        juce::Logger::writeToLog("[SModeService] ERROR runBackground after shutdown - job dropped");
        return false;
    }
    workers_->addJob(std::move(job));
    return true;
}

void SModeService::reset()
{
    juce::Logger::writeToLog("[SModeService] === reset ===");
    if (sessionSeparationRunning_ && separation_.isSeparating())
    {
        juce::Logger::writeToLog("[SModeService] reset: cancelling the session's separation");
        separation_.cancelSeparation();
    }
    ++loadGeneration_;              // drop any stem decode in flight
    loadingStems_ = false;
    session_.clear();
    stemFiles_.clear();
    uiExtraJson_.clear();
    restoreFromProject_ = false;
    restoreTriedSeparation_ = false;
    sessionSeparationRunning_ = false;
    sessionSeparationQueued_ = false;
    relinkConfirmFile_ = juce::File();
    model_.clear();
    sourceFile_ = juce::File();  // `= {}` is ambiguous on Clang (File / String assignment)
    active_ = false;
}

void SModeService::beginPendingSession(const juce::ValueTree& session, bool fromProject)
{
    ++loadGeneration_;
    loadingStems_ = false;
    session_.beginRestore(session);
    restoreFromProject_ = fromProject;
    restoreTriedSeparation_ = false;
    sessionSeparationQueued_ = false;
    relinkConfirmFile_ = juce::File();
}

void SModeService::startWorkflow(const juce::File& sourceFile,
                                  SModeSeparationService::StemCount stemCount)
{
    juce::Logger::writeToLog("[SModeService] === startWorkflow ===");
    juce::Logger::writeToLog("[SModeService] Source: " + sourceFile.getFullPathName());
    juce::Logger::writeToLog("[SModeService] Stems: " + juce::String(static_cast<int>(stemCount)));

    // Relink: the restored project's audio is missing and the user picked a
    // file with the saved song's name -> keep the saved chops and apply them
    // to the new stems instead of discarding the session. The name alone is
    // not trusted (review N-10): size / content fingerprint and duration must
    // match, otherwise the user is warned and must pick the file again.
    const bool relink = session_.status() == SessionStatus::MissingAudio
                     && session_.hasPending()
                     && session::readSessionSource(session_.pending()).sourceFile.getFileName()
                            .equalsIgnoreCase(sourceFile.getFileName());

    if (relink)
    {
        if (! acceptRelinkCandidate(sourceFile))
        {
            juce::Logger::writeToLog("[SModeService] startWorkflow: same name but different audio - NOT relinked, "
                                     "saved session kept (MissingAudio) until the user confirms");
            return;
        }
        juce::Logger::writeToLog("[SModeService] startWorkflow: RELINK - saved session kept for " + sourceFile.getFileName());
        beginPendingSession(relinkedPendingCopy(sourceFile), true);
        ++relinkCount_;
    }
    else
    {
        if (session_.hasPending())
            juce::Logger::writeToLog("[SModeService] startWorkflow: user loaded a new song - pending project session discarded");
        // A new song is a "pending session" holding just the source: a save
        // during the separation writes the song path, so a reload re-separates.
        juce::ValueTree minimal("session");
        minimal.setProperty("schema", session::kSchema, nullptr);
        minimal.setProperty("sourceFile", sourceFile.getFullPathName(), nullptr);
        beginPendingSession(minimal, false);
        uiExtraJson_.clear();
    }

    sourceFile_ = sourceFile;
    stemFiles_.clear();
    model_.clear();
    active_ = false;

    separation_.setStemCount(stemCount);
    startSessionSeparation();
}

void SModeService::onSeparationComplete(const juce::var& stemPaths)
{
    juce::Logger::writeToLog("[SModeService] === onSeparationComplete ===");

    if (sessionSeparationQueued_)
    {
        juce::Logger::writeToLog("[SModeService] separation result of a replaced run ignored - starting the queued one");
        sessionSeparationQueued_ = false;
        startSessionSeparation();
        return;
    }
    if (!sessionSeparationRunning_)
    {
        juce::Logger::writeToLog("[SModeService] separation result ignored (not for the current session, status="
                                 + juce::String(session::SessionState::toString(session_.status())) + ")");
        return;
    }
    sessionSeparationRunning_ = false;

    std::vector<juce::File> files;
    if (stemPaths.isArray())
    {
        for (int i = 0; i < stemPaths.size(); ++i)
            files.emplace_back(stemPaths[i].toString());
    }

    if (files.empty())
    {
        juce::Logger::writeToLog("[SModeService] ERROR No stem files in completion event");
        if (restoreFromProject_ && session_.hasPending())
        {
            continueRestoreWithoutStems("separation produced no stems");
            return;
        }
        session_.clear();
        bus_.publish("smode.workflow.error", juce::var("No stems produced"));
        return;
    }

    loadStemAudio(files);
}

void SModeService::onSeparationError(const juce::var& errorMsg)
{
    juce::Logger::writeToLog("[SModeService] Separation error: " + errorMsg.toString());

    if (sessionSeparationQueued_)
    {
        juce::Logger::writeToLog("[SModeService] previous separation ended - starting the queued session separation");
        sessionSeparationQueued_ = false;
        startSessionSeparation();
        return;
    }

    const bool ours = sessionSeparationRunning_;
    if (! ours)
    {
        // A run that was cancelled / replaced (restore, reset, eject) ended
        // after its owner moved on (review N-8). It must not deactivate a
        // session restored meanwhile, nor fire the UI's "Cancelled" path.
        juce::Logger::writeToLog("[SModeService] separation error ignored (stale run, not the current session's; status="
                                 + juce::String(session::SessionState::toString(session_.status()))
                                 + ", active=" + juce::String(active_.load() ? 1 : 0) + ")");
        return;
    }
    sessionSeparationRunning_ = false;
    active_ = false;

    if (restoreFromProject_ && session_.hasPending())
    {
        // A project restore that had to re-separate: keep the session.
        loadingStems_ = false;
        session_.markMissing("Could not re-create the stems from " + sourceFile_.getFullPathName()
                             + " (" + errorMsg.toString() + ")");
        bus_.publish("smode.session.missing", juce::var(session_.message()));
        return;
    }
    if (session_.hasPending())
    {
        // A user load failed / was cancelled: nothing to restore.
        session_.clear();
    }
    bus_.publish("smode.workflow.error", errorMsg);
}

void SModeService::startSessionSeparation()
{
    if (separation_.isSeparating())
    {
        juce::Logger::writeToLog("[SModeService] startSessionSeparation: another separation is running - cancelling it first (queued)");
        sessionSeparationQueued_ = true;
        sessionSeparationRunning_ = false;
        separation_.cancelSeparation();
        return;
    }
    juce::Logger::writeToLog("[SModeService] startSessionSeparation: " + sourceFile_.getFullPathName());
    sessionSeparationRunning_ = true;
    restoreTriedSeparation_ = true;
    separation_.startSeparation(sourceFile_);
}

void SModeService::continueRestoreWithoutStems(const juce::String& why)
{
    juce::Logger::writeToLog("[SModeService] restore: stems unusable (" + why + ")");
    if (!restoreTriedSeparation_ && sourceFile_.existsAsFile())
    {
        juce::Logger::writeToLog("[SModeService] restore: re-separating the source song (stem cache is used if present)");
        separation_.setStemCount(SModeSeparationService::StemCount::Four);
        startSessionSeparation();
        return;
    }
    loadingStems_ = false;
    const juce::String msg = sourceFile_.existsAsFile()
        ? "The stems of this project could not be loaded or re-created (" + why + ")."
        : "Audio missing: the song '" + sourceFile_.getFullPathName()
              + "' and its saved stems were not found. Your chops are kept; load the song again to relink them.";
    session_.markMissing(msg);
    bus_.publish("smode.session.missing", juce::var(msg));
}

// -- Project persistence -------------------------------------------------------

juce::ValueTree SModeService::createSessionTree()
{
    // Refresh the cached page UI state while a page is attached (and has
    // consumed any restored state), so it survives editor close.
    if (model_.hasExtraStateHooks() && !model_.hasPendingExtraState() && session_.status() == SessionStatus::Active)
        uiExtraJson_ = juce::JSON::toString(model_.captureExtraState(), true);

    auto t = session_.treeForSave([this]()
    {
        session::SessionSource src;
        src.sourceFile  = sourceFile_;
        src.stemFiles   = stemFiles_;
        src.uiExtraJson = uiExtraJson_;
        src.sourceFingerprint = sourceFingerprintFor(sourceFile_);
        return session::writeSession(model_, src);
    });
    juce::Logger::writeToLog("[SModeService] createSessionTree: status="
        + juce::String(session::SessionState::toString(session_.status()))
        + (t.isValid() ? " segments=" + juce::String(session::countSegments(t)) : juce::String(" (no session)")));
    return t;
}

void SModeService::restoreSession(const juce::ValueTree& tree)
{
    juce::Logger::writeToLog("[SModeService] === restoreSession START ===");
    const auto src = session::readSessionSource(tree);
    bool anyStem = false;
    for (const auto& f : src.stemFiles) anyStem = anyStem || f != juce::File();

    if (!tree.isValid() || (src.sourceFile == juce::File() && !anyStem))
    {
        const bool hadSession = session_.status() != SessionStatus::Empty || active_ || model_.getStemCount() > 0;
        juce::Logger::writeToLog("[SModeService] restoreSession: project has no S-Mode session"
                                 + juce::String(hadSession ? " -> resetting the current one" : ""));
        if (hadSession)
            reset();
        return;
    }

    if (sessionSeparationRunning_ && separation_.isSeparating())
    {
        juce::Logger::writeToLog("[SModeService] restoreSession: cancelling the running separation (replaced by the project)");
        separation_.cancelSeparation();
    }
    sessionSeparationRunning_ = false;

    beginPendingSession(tree, true);
    sourceFile_  = src.sourceFile;
    stemFiles_   = src.stemFiles;
    uiExtraJson_ = src.uiExtraJson;
    model_.clear();
    active_ = false;

    juce::Logger::writeToLog("[SModeService] restoreSession: source=" + sourceFile_.getFullPathName()
        + " (" + juce::String(sourceFile_.existsAsFile() ? "exists" : "MISSING") + "), stems="
        + juce::String((int) stemFiles_.size()) + ", segments=" + juce::String(session::countSegments(tree))
        + ", uiExtra=" + juce::String(uiExtraJson_.length()) + " chars");

    bus_.publish("smode.session.restoring", juce::var(juce::jmax(src.stemCount, (int) stemFiles_.size())));

    bool stemsOk = !stemFiles_.empty();
    for (const auto& f : stemFiles_)
    {
        if (!f.existsAsFile())
        {
            juce::Logger::writeToLog("[SModeService] restoreSession: stem missing: " + f.getFullPathName());
            stemsOk = false;
        }
    }

    if (stemsOk)
    {
        // Validated on the worker (cross-process cache lock, file sizes vs
        // entry.json): stale / truncated / foreign stems re-separate (S-M5).
        juce::Logger::writeToLog("[SModeService] restoreSession: decoding the saved stems (fast path, cache-validated)");
        loadStemAudio(stemFiles_, true);
    }
    else
    {
        continueRestoreWithoutStems("saved stem files missing");
    }
    juce::Logger::writeToLog("[SModeService] === restoreSession END (status="
        + juce::String(session::SessionState::toString(session_.status())) + ") ===");
}

bool SModeService::relinkMissingSource(const juce::File& song)
{
    juce::Logger::writeToLog("[SModeService] === relinkMissingSource === " + song.getFullPathName());
    if (session_.status() != SessionStatus::MissingAudio || ! session_.hasPending())
    {
        juce::Logger::writeToLog("[SModeService] relink refused: no missing-audio session (status="
                                 + juce::String(session::SessionState::toString(session_.status())) + ")");
        return false;
    }
    if (! song.existsAsFile())
    {
        juce::Logger::writeToLog("[SModeService] relink refused: file does not exist");
        return false;
    }
    if (! acceptRelinkCandidate(song))
    {
        juce::Logger::writeToLog("[SModeService] relink refused for now: different audio - waiting for the user to confirm "
                                 "(pick the same file again); the saved session is kept");
        return false;
    }
    auto relinked = relinkedPendingCopy(song);
    juce::Logger::writeToLog("[SModeService] relink: " + juce::String(session::countSegments(relinked))
                             + " saved segments kept, re-running the restore with the new source");
    restoreSession(relinked);   // saved stems are gone -> re-separates the new source (cache if present)
    ++relinkCount_;
    return true;
}

double SModeService::readAudioDurationSeconds(const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return 0.0;
    return (double) reader->lengthInSamples / reader->sampleRate;
}

bool SModeService::acceptRelinkCandidate(const juce::File& candidate)
{
    // Message thread. Reads the audio header + 128 KB (fingerprint) only.
    const double seconds = readAudioDurationSeconds(candidate);
    const auto chk = session::checkRelinkCandidate(session_.pending(), candidate, seconds);
    juce::Logger::writeToLog("[SModeService] relink check " + candidate.getFullPathName() + ": "
                             + (chk.accept ? "OK" : "MISMATCH") + " (" + chk.reason + ", duration "
                             + juce::String(seconds, 2) + " s)");
    if (chk.accept)
    {
        relinkConfirmFile_ = juce::File();
        return true;
    }
    if (relinkConfirmFile_ != juce::File() && relinkConfirmFile_ == candidate)
    {
        juce::Logger::writeToLog("[SModeService] relink: the user picked the same file again -> CONFIRMED despite the mismatch");
        relinkConfirmFile_ = juce::File();
        return true;
    }

    // First pick of a mismatching file: keep the saved session untouched,
    // tell the user, and accept the same file only on a second pick.
    relinkConfirmFile_ = candidate;
    juce::String msg = "Different audio - chops may not line up: '" + candidate.getFileName() + "'";
    if (chk.contentMismatch)
        msg << " is not the same file as this project's song";
    if (chk.tooShort)
        msg << (chk.contentMismatch ? " and" : "") << " is shorter than your chops ("
            << juce::String(chk.segsClamped) << " would be shortened, " << juce::String(chk.segsDropped) << " removed)";
    msg << ". Your chops are kept. Choose the same file again to use it anyway.";
    session_.markMissing(msg);   // still MissingAudio, pending tree kept verbatim
    bus_.publish("smode.session.missing", juce::var(msg));
    return false;
}

juce::ValueTree SModeService::relinkedPendingCopy(const juce::File& song)
{
    // The new song's identity is written with the pending tree, so a save
    // before the restore finished describes the file that is actually used.
    auto relinked = session_.pending().createCopy();
    relinked.setProperty("sourceFile", song.getFullPathName(), nullptr);
    relinked.setProperty("sourceSize", (juce::int64) song.getSize(), nullptr);
    relinked.setProperty("sourceMtime", song.getLastModificationTime().toMilliseconds(), nullptr);
    const auto fp = sourceFingerprintFor(song);
    if (fp.isNotEmpty()) relinked.setProperty("sourceFp", fp, nullptr);
    else                 relinked.removeProperty("sourceFp", nullptr);
    return relinked;
}

juce::String SModeService::sourceFingerprintFor(const juce::File& song)
{
    // Cached per (path, size, mtime): saves run often, the song rarely changes.
    if (! song.existsAsFile())
        return {};
    const auto size = song.getSize();
    const auto mtime = song.getLastModificationTime().toMilliseconds();
    if (song != fpFile_ || size != fpSize_ || mtime != fpMtime_)
    {
        fpFile_ = song;
        fpSize_ = size;
        fpMtime_ = mtime;
        fpValue_ = session::computeSourceFingerprint(song);
        juce::Logger::writeToLog("[SModeService] source fingerprint " + song.getFileName() + " = "
                                 + (fpValue_.isNotEmpty() ? fpValue_ : juce::String("<unreadable>")));
    }
    return fpValue_;
}

uint64_t SModeService::computeSessionFingerprint()
{
    uint64_t h = model_.computeSessionHash();
    auto mix = [&h](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    mix((uint64_t) session_.status());
    mix((uint64_t) sourceFile_.getFullPathName().hashCode64());
    for (const auto& f : stemFiles_)
        mix((uint64_t) f.getFullPathName().hashCode64());
    if (session_.hasPending())
        mix((uint64_t) session::countSegments(session_.pending()));

    juce::String extra = uiExtraJson_;
    if (model_.hasExtraStateHooks() && !model_.hasPendingExtraState() && session_.status() == SessionStatus::Active)
        extra = juce::JSON::toString(model_.captureExtraState(), true);
    mix((uint64_t) extra.hashCode64());
    return h;
}

// -- Stem loading (C15 / A19) ----------------------------------------------------

struct SModeService::DecodedStems
{
    std::vector<juce::File> files;
    std::vector<juce::AudioBuffer<float>> buffers;
    std::vector<double> rates;
    juce::String error;      // first failure, empty = all stems decoded
    double ms { 0.0 };
};

void SModeService::loadStemAudio(const std::vector<juce::File>& stemFiles, bool validateCache)
{
    juce::Logger::writeToLog("[SModeService] === loadStemAudio === " + juce::String(static_cast<int>(stemFiles.size()))
                             + " stems (decode on the worker pool" + juce::String(validateCache ? ", cache validation first)" : ")"));

    const uint64_t generation = ++loadGeneration_;
    loadingStems_ = true;
    active_ = false;

    auto decoded = std::make_shared<DecodedStems>();
    decoded->files = stemFiles;

    const bool queued = runBackground([this, alive = alive_, generation, decoded, validateCache]()
    {
        // Worker thread: decode only, never touch the model. `this` stays
        // valid while the job runs (shutdown drains the pool); jobsCancelled_
        // makes the decode stop early on unload. No logging here (worker).
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        // The validation may wait for the cross-process cache lock: an unload
        // (jobsCancelled_) stops that wait at once (review N-9).
        if (validateCache && ! decoded->files.empty() && ! jobsCancelled_.load()
            && ! FullSongService::validateCachedStemFile(decoded->files.front(),
                                                         [this] { return jobsCancelled_.load(); }))
            decoded->error = jobsCancelled_.load() ? juce::String("cancelled")
                                                   : juce::String("saved stems are stale or incomplete (stem cache entry invalid)");
        for (const auto& f : decoded->files)
        {
            if (decoded->error.isNotEmpty())
                break;
            double sr = 0.0;
            auto buf = loadAudioFile(f, sr, jobsCancelled_);
            if (jobsCancelled_.load())
            {
                decoded->error = "cancelled";
                break;
            }
            if (buf.getNumSamples() == 0 && decoded->error.isEmpty())
                decoded->error = "could not decode " + f.getFullPathName();
            decoded->buffers.push_back(std::move(buf));
            decoded->rates.push_back(sr);
        }
        decoded->ms = juce::Time::getMillisecondCounterHiRes() - t0;

        juce::MessageManager::callAsync([this, alive, generation, decoded]()
        {
            if (!alive->load()) return;
            onStemsDecoded(generation, *decoded);
        });
    });

    if (!queued)
    {
        loadingStems_ = false;
        juce::Logger::writeToLog("[SModeService] ERROR loadStemAudio: worker pool unavailable");
    }
}

void SModeService::onStemsDecoded(uint64_t generation, DecodedStems& decoded)
{
    if (generation != loadGeneration_)
    {
        juce::Logger::writeToLog("[SModeService] decoded stems dropped (a newer load / reset happened)");
        return;
    }
    loadingStems_ = false;
    juce::Logger::writeToLog("[SModeService] stems decoded off the message thread in " + juce::String(decoded.ms, 1) + " ms"
                             + (decoded.error.isEmpty() ? juce::String() : " - ERROR: " + decoded.error));

    if (decoded.error.isNotEmpty() || decoded.buffers.empty())
    {
        if (restoreFromProject_ && session_.hasPending())
        {
            continueRestoreWithoutStems(decoded.error.isNotEmpty() ? decoded.error : juce::String("no stems"));
            return;
        }
        if (session_.hasPending())
            session_.clear();
        bus_.publish("smode.workflow.error", juce::var("Could not load the stems: " + decoded.error));
        return;
    }

    auto stemNames = separation_.getStemNames();
    const int stemCount = static_cast<int>(decoded.buffers.size());
    model_.setStemCount(stemCount);

    double sampleRate = 44100.0;
    for (int i = 0; i < stemCount; ++i)
    {
        auto* stem = model_.getStem(i);
        if (!stem) continue;

        stem->name = i < stemNames.size() ? stemNames[i] : "Stem " + juce::String(i + 1);

        // Shared storage: the realtime engine's snapshots keep their own
        // reference, so a later clear()/reload never frees audio that the
        // audio thread is still playing (A1/A3).
        model_.setStemAudio(i, std::move(decoded.buffers[(size_t) i]));
        if (decoded.rates[(size_t) i] > 0.0) sampleRate = decoded.rates[(size_t) i];

        juce::Logger::writeToLog("  [SModeService] " + stem->name + ": "
            + juce::String(stem->audioBuffer.getNumSamples()) + " samples, "
            + juce::String(stem->audioBuffer.getNumChannels()) + " ch");
    }
    model_.setSampleRate(sampleRate);
    stemFiles_ = decoded.files;

    if (session_.hasPending())
    {
        session::ApplyReport report;
        const int applied = session::applySessionToModel(session_.pending(), model_, &report);
        if (report.clamped + report.dropped + report.skipped > 0)
            juce::Logger::writeToLog("[SModeService] WARNING restore: " + juce::String(report.clamped)
                                     + " chop(s) shortened, " + juce::String(report.dropped) + " removed, "
                                     + juce::String(report.skipped) + " on stems that did not load (audio shorter / different)");
        const auto extra = juce::JSON::parse(uiExtraJson_);
        model_.setPendingExtraState(extra);
        const bool fromProject = restoreFromProject_;
        session_.completeRestore();
        restoreFromProject_ = false;
        if (fromProject)
            ++restoreCompletions_;
        juce::Logger::writeToLog("[SModeService] session restored: " + juce::String(applied) + " segments, uiExtra "
                                 + juce::String(extra.isVoid() ? "none" : "pending for the page")
                                 + (fromProject ? " (project)" : " (new song)"));
    }
    else
    {
        session_.setActive();
    }

    active_ = true;
    juce::Logger::writeToLog("[SModeService] All stems loaded, sampleRate=" + juce::String(sampleRate));
    bus_.publish("smode.workflow.ready", juce::var(stemCount));
}

void SModeService::runDownbeatDetectionAsync(double bpm)
{
    juce::Logger::writeToLog("🎵 [SModeService] === runDownbeatDetectionAsync === bpm=" + juce::String(bpm, 2));

    if (!active_.load())
    {
        juce::Logger::writeToLog("❌ [SModeService] downbeat: service not active");
        return;
    }
    if (bpm < 60.0 || bpm > 220.0)
    {
        juce::Logger::writeToLog("❌ [SModeService] downbeat: bpm out of range");
        return;
    }

    // Find the drums stem (name match, then fallback to index 2 — see
    // SModeSeparationService 4-stem order: Vocals/Bass/Drums/Melody).
    int drumIdx = -1;
    int stemCount = model_.getStemCount();
    for (int i = 0; i < stemCount; ++i)
    {
        auto* s = model_.getStem(i);
        if (s && s->name.containsIgnoreCase("drum")) { drumIdx = i; break; }
    }
    if (drumIdx < 0 && stemCount > 2) drumIdx = 2;
    if (drumIdx < 0)
    {
        juce::Logger::writeToLog("❌ [SModeService] downbeat: no drums stem available");
        return;
    }

    auto* drumStem = model_.getStem(drumIdx);
    if (!drumStem || drumStem->audioBuffer.getNumSamples() == 0)
    {
        juce::Logger::writeToLog("❌ [SModeService] downbeat: drum stem empty");
        return;
    }

    // Copy the drum stem HERE (message thread, a memcpy). The model can be
    // cleared by a new song load while the job runs, so the job must never
    // hold raw pointers into it.
    auto buf = std::make_shared<juce::AudioBuffer<float>>(drumStem->audioBuffer);
    const double sampleRate = model_.getSampleRate();
    const uint64_t generation = model_.getContentGeneration();

    const bool queued = runBackground([this, alive = alive_, buf, sampleRate, bpm, generation]()
    {
        if (jobsCancelled_.load()) return;   // unloading: skip
        // Cancellable (BeatThis inference aborts within ~200 ms on unload).
        auto result = DownbeatDetector::detect(*buf, sampleRate, bpm, AnalysisCancel(&jobsCancelled_));
        if (result.cancelled) return;

        juce::MessageManager::callAsync([this, alive, result, sampleRate, generation]()
        {
            if (!alive->load()) return;
            if (generation != model_.getContentGeneration())
            {
                juce::Logger::writeToLog("[SModeService] downbeat result dropped (song changed)");
                return;
            }
            if (!result.valid)
            {
                juce::Logger::writeToLog("❌ [SModeService] downbeat invalid — leaving grid offset untouched");
                return;
            }
            const double offsetSec = static_cast<double>(result.beat1Sample) / sampleRate;
            model_.setGridOffsetSeconds(offsetSec);
            juce::Logger::writeToLog("✅ [SModeService] gridOffset set: " + juce::String(offsetSec, 4)
                                     + "s (conf=" + juce::String(result.confidence, 3) + ")");

            // Notify UI to apply to all stem lanes.
            auto* obj = new juce::DynamicObject();
            obj->setProperty("beat1Sample", static_cast<juce::int64>(result.beat1Sample));
            obj->setProperty("offsetSeconds", offsetSec);
            obj->setProperty("confidence", result.confidence);
            bus_.publish("smode.downbeat.detected", juce::var(obj));
        });
    });
    juce::Logger::writeToLog(queued ? "[SModeService] downbeat job queued"
                                    : "❌ [SModeService] downbeat job NOT queued");
}

juce::AudioBuffer<float> SModeService::loadAudioFile(const juce::File& file, double& outSampleRate,
                                                    const std::atomic<bool>& abort)
{
    // Worker thread. Reads in blocks so an unload (abort) stops it quickly.
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader)
    {
        outSampleRate = 0.0;
        return {};
    }

    outSampleRate = reader->sampleRate;
    const int numChannels = static_cast<int>(reader->numChannels);
    const auto total = reader->lengthInSamples;
    if (numChannels <= 0 || total <= 0 || total > (juce::int64) std::numeric_limits<int>::max())
        return {};
    const int numSamples = static_cast<int>(total);

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    buffer.clear();
    constexpr int kBlock = 1 << 18;
    for (int pos = 0; pos < numSamples; pos += kBlock)
    {
        if (abort.load())
            return {};
        const int n = juce::jmin(kBlock, numSamples - pos);
        if (!reader->read(&buffer, pos, n, pos, true, numChannels > 1))
            return {};
    }
    return buffer;
}

} // namespace samplar
