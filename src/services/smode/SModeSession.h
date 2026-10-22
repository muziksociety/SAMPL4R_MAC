#pragma once

// S-Mode project persistence (A4 / B8 / A17): the versioned "session" node that
// a DAW project stores for one SAMPLAR instance, and the pending-restore rule.
//
// Layout (schema 2), inside the AppState "smode" node:
//
//   <smode schema="2" version="1">
//     <session sourceFile sourceSize sourceMtime [sourceFp] stemCacheKey stemCount
//              sampleRate bpm gridOffset uiExtra(JSON text)>
//       (sourceFp: optional content fingerprint used to validate a relink)
//       <stem index name file linked solo mute volume>
//         <seg start end note pitch vol pan cutoff hp rev loop atk rel trig/>
//       </stem>
//     </session>
//     <play mono gate loopGate transportLoop dawSync>
//       <stemParam index pitch vc pbd mwd atd pbr mwr atr/>
//       <cc channel cc target/>            (schema 2: MIDI CC bindings)
//     </play>
//   </smode>
//
// Readers are tolerant: unknown nodes / properties are ignored, missing ones
// take defaults, out-of-range values are clamped, a newer schema is read
// best-effort; schema 1 (no dawSync / cc) reads with defaults. Positions are stored in samples at `sampleRate` and rescaled
// if the stems load at a different rate.
//
// Pure data code: no services, no UI, no threads. All functions run on the
// message thread (they read / write SModeSegmentModel).

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "SModeSegmentModel.h"
#include <array>
#include <functional>
#include <vector>

namespace samplar {
namespace session {

constexpr int kSchema = 2;   // 2: <play dawSync> + <cc> bindings (schema 1 reads with defaults)

/** Realtime play settings that live in the playback engine (MidiMappingService). */
struct StemPlayParams
{
    float pitch      { 0.0f };   // stem pitch knob, semitones
    int   velCurve   { 0 };      // MidiMappingService::VelocityCurve
    int   bendDest   { 1 };      // ModDestination::Pitch
    int   wheelDest  { 0 };
    int   atDest     { 0 };
    float bendRange  { 2.0f };
    float wheelRange { 12.0f };
    float atRange    { 12.0f };

    bool operator== (const StemPlayParams& o) const;
};

/** One MIDI CC -> named control binding (MidiMappingService CC learn). */
struct CcBinding
{
    int channel { 0 };      // 0 = omni, 1..16
    int cc { 0 };           // 0..127
    juce::String target;    // stable control id, e.g. "stem0.pitch"

    bool operator== (const CcBinding& o) const { return channel == o.channel && cc == o.cc && target == o.target; }
};

struct PlaySettings
{
    bool mono          { false };   // POLY / MONO
    bool gate          { false };   // ONE-SHOT / GATE
    bool loopGate      { false };
    bool transportLoop { false };   // LOOP button (transport wrap)
    bool dawSync       { false };   // DAW tempo sync toggle (schema 2)
    std::vector<CcBinding> ccBindings;   // schema 2
    std::array<StemPlayParams, engine::kMaxStems> stems {};

    bool operator== (const PlaySettings& o) const;
};

juce::ValueTree writePlay (const PlaySettings& p);
PlaySettings    readPlay  (const juce::ValueTree& play);   // invalid / partial -> defaults

/** Play settings of the pre-schema-2 page bindings <smodeStemPitch p0..p7/> and
 *  <smodeStemMod vc0 pbd0 mwd0 atd0 pbr0 mwr0 atr0 .../> (either may be invalid).
 *  Same clamps as readPlay: NaN / inf -> default, out-of-range enums -> default. */
PlaySettings    readLegacyPlay (const juce::ValueTree& stemPitch, const juce::ValueTree& stemMod);

/** The processor's restore rule (review N-2 / N-18): the <play> child of the
 *  "smode" node wins; the legacy nodes are read only when <play> is absent;
 *  nothing saved -> defaults. sourceOut (optional) describes the choice. */
PlaySettings    selectPlaySettings (const juce::ValueTree& smodeNode,
                                    const juce::ValueTree& legacyStemPitch,
                                    const juce::ValueTree& legacyStemMod,
                                    juce::String* sourceOut = nullptr);

/** Where the session's audio comes from. */
struct SessionSource
{
    juce::File sourceFile;
    std::vector<juce::File> stemFiles;   // in stem order
    juce::String uiExtraJson;            // page UI state (lanes, grid division, ...), opaque
    int stemCount { 0 };
    juce::String sourceFingerprint;      // computeSourceFingerprint (optional, "sourceFp")
};

/** Builds the "session" node from the model + its audio files. */
juce::ValueTree writeSession (const SModeSegmentModel& model, const SessionSource& src);

/** File info of a saved session (used to schedule the restore). */
SessionSource readSessionSource (const juce::ValueTree& session);

/** What applySessionToModel did with the saved segments (never silent: logged). */
struct ApplyReport
{
    int applied { 0 };
    int clamped { 0 };   // kept but shortened to the stem length
    int dropped { 0 };   // empty after clamping / invalid
    int skipped { 0 };   // on a stem index that is not loaded
};

/** Applies segments, per-segment fields, stem flags, tempo and grid offset of a
 *  saved session to a model whose stem audio is loaded. Segment positions are
 *  rescaled from the saved rate to the model rate and clamped to the stem
 *  length; empty / invalid segments are dropped. Returns segments applied. */
int applySessionToModel (const juce::ValueTree& session, SModeSegmentModel& model,
                         ApplyReport* report = nullptr);

/** Cheap content fingerprint of a song file ("v1:<hex>": hash of the length and
 *  the first / last 64 KB). Empty when unreadable. */
juce::String computeSourceFingerprint (const juce::File& file);

/** End of the latest saved chop in seconds (0 = no chops / no sample rate). */
double lastChopEndSeconds (const juce::ValueTree& session);

/** Relink validation (review N-10): is `candidate` the session's song?
 *  Identity: saved "sourceFp" (content fingerprint) if present, else the saved
 *  "sourceSize"; neither -> identityUnknown (accepted on duration alone).
 *  Duration: candidateSeconds (<= 0 = unknown, not checked) must cover every
 *  saved chop. accept = same content AND long enough. */
struct RelinkCheck
{
    bool accept { false };
    bool contentMismatch { false };
    bool tooShort { false };
    bool identityUnknown { false };
    int segsClamped { 0 };       // chops that would be shortened by the new audio
    int segsDropped { 0 };       // chops that would start after its end (removed)
    double lastChopEndSeconds { 0.0 };
    juce::String reason;
};
RelinkCheck checkRelinkCandidate (const juce::ValueTree& session, const juce::File& candidate,
                                  double candidateSeconds);

/** Number of <seg> nodes in a session (diagnostics / tests). */
int countSegments (const juce::ValueTree& session);

/** One-line ASCII description of the persisted model content (stems, flags,
 *  every segment start/end/note, tempo, grid). Diagnostics and the smoke
 *  host's round-trip comparison. */
juce::String describeModel (const SModeSegmentModel& model);

/** Name of the persistent stem-cache entry the stems live in
 *  (<userData>/stem_cache_onnx/<key>/...), or empty. */
juce::String stemCacheKeyFor (const std::vector<juce::File>& stemFiles);

/**
 * The A4 rule. A restored project session stays "pending" until its stems are
 * decoded and its segments applied; while pending, every save writes the
 * pending tree back VERBATIM (never the half-built live model), so saving
 * before the restore finished - or while the audio is missing - never loses
 * the project.
 */
class SessionState
{
public:
    enum class Status { Empty, Restoring, Active, MissingAudio };

    void beginRestore (const juce::ValueTree& session);   // stores a deep copy
    void markMissing  (const juce::String& reason);        // keeps the pending tree
    void completeRestore();                                // pending applied -> Active
    void setActive();                                      // fresh (non-restored) session
    void clear();                                          // -> Empty

    Status status() const               { return status_; }
    bool hasPending() const             { return pending_.isValid(); }
    const juce::ValueTree& pending() const { return pending_; }
    const juce::String& message() const { return message_; }

    /** Pending -> copy of it. Active -> buildLive(). Empty -> invalid tree. */
    juce::ValueTree treeForSave (const std::function<juce::ValueTree()>& buildLive) const;

    static const char* toString (Status s);

private:
    Status status_ { Status::Empty };
    juce::ValueTree pending_;
    juce::String message_;
};

} // namespace session
} // namespace samplar
