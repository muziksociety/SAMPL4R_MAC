#include "AppState.h"
#include "../utils/Tracing.h"
#include <algorithm>

namespace samplar {

const juce::Identifier AppState::kRootId { "SM5AppState" };

namespace {
// AppStates currently rewriting their tree ON THIS THREAD (review N-15). A
// shared counter let an off-thread serialize swallow the message thread's
// markDirty(); per-thread suppression only silences the rewriting thread.
thread_local std::vector<const AppState*> tlsDirtySuppress;
}

void AppState::pushDirtySuppress(const AppState* s) { tlsDirtySuppress.push_back(s); }

void AppState::popDirtySuppress(const AppState* s)
{
    for (auto it = tlsDirtySuppress.rbegin(); it != tlsDirtySuppress.rend(); ++it)
    {
        if (*it == s)
        {
            tlsDirtySuppress.erase(std::next(it).base());
            return;
        }
    }
}

bool AppState::isDirtySuppressedOnThisThread() const
{
    return std::find(tlsDirtySuppress.begin(), tlsDirtySuppress.end(), this) != tlsDirtySuppress.end();
}

AppState::AppState(juce::AudioProcessor& processor)
    : processor_(processor), root_(kRootId)
{
    stampRootVersion();
    root_.addListener(&listener_);
    juce::Logger::writeToLog("[AppState] Constructed");
}

AppState::~AppState()
{
    aliveToken_->store(false);
    root_.removeListener(&listener_);
    juce::Logger::writeToLog("[AppState] Destroyed (remaining bindings: "
                             + juce::String(static_cast<int>(bindings_.size())) + ")");
}

void AppState::stampRootVersion()
{
    if (! root_.hasProperty("version"))
        root_.setProperty("version", static_cast<int>(kFormatVersion), nullptr);
}

int AppState::registerBinding(StateBinding binding)
{
    const int token = nextToken_++;
    const auto id = binding.nodeId;
    const bool hasRestore = static_cast<bool>(binding.restore);

    bindings_.push_back({ token, std::move(binding) });

    juce::Logger::writeToLog("[AppState] registerBinding '" + id.toString()
                             + "' token=" + juce::String(token)
                             + (restoreReady_ ? " (draining now)" : " (buffered)")
                             + (isSuperseded(id) ? " [superseded: never restored]" : ""));

    // Late-drain: if state has already been deserialized and a child tree exists,
    // fire restore immediately so the feature doesn't need a separate "pending" path.
    // NOTE: we defer the actual restore call to the message queue so the caller's
    // constructor can finish (wiring callbacks, adding child components) before any
    // restore logic fires. This fixes the bug where readFromTree triggers onFileDropped
    // but the parent hadn't yet assigned that callback.
    if (restoreReady_ && hasRestore && ! isSuperseded(id))
    {
        juce::ValueTree child;
        {
            const juce::ScopedLock sl(treeLock_);
            child = root_.getChildWithName(id).createCopy();
        }
        if (child.isValid())
        {
            const int capturedToken = token;
            juce::MessageManager::callAsync([this, alive = aliveToken_, capturedToken, id, child]() {
                if (!alive->load()) return;   // AppState destroyed before the post ran
                // Re-find the binding by token — it may have been unregistered between
                // the async post and now.
                for (auto& e : bindings_)
                {
                    if (e.token != capturedToken) continue;
                    if (! e.binding.restore) return;
                    try {
                        e.binding.restore(child);
                        juce::Logger::writeToLog("[AppState]   binding '" + id.toString() + "' restored (late drain async)");
                    }
                    catch (const std::exception& ex) {
                        juce::Logger::writeToLog("[AppState]   binding '" + id.toString()
                                                 + "' late-drain restore FAILED: " + juce::String(ex.what()));
                    }
                    return;
                }
                juce::Logger::writeToLog("[AppState]   late-drain skipped: binding token "
                                         + juce::String(capturedToken) + " unregistered before async fire");
            });
        }
    }

    return token;
}

void AppState::unregisterBinding(int token)
{
    for (auto it = bindings_.begin(); it != bindings_.end(); ++it)
    {
        if (it->token == token)
        {
            juce::Logger::writeToLog("[AppState] unregisterBinding '"
                                     + it->binding.nodeId.toString()
                                     + "' token=" + juce::String(token));
            bindings_.erase(it);
            return;
        }
    }
    juce::Logger::writeToLog("[AppState] unregisterBinding token=" + juce::String(token) + " NOT FOUND");
}

juce::ValueTree AppState::getOrCreateChild(const juce::Identifier& nodeId)
{
    const juce::ScopedLock sl(treeLock_);
    auto child = root_.getChildWithName(nodeId);
    if (! child.isValid())
    {
        child = juce::ValueTree(nodeId);
        root_.appendChild(child, nullptr);
    }
    return child;
}

void AppState::addSupersededNode(const juce::Identifier& nodeId)
{
    supersededNodes_.addIfNotAlreadyThere(nodeId);
    juce::Logger::writeToLog("[AppState] node '" + nodeId.toString() + "' marked superseded (never restored / saved)");
}

juce::ValueTree AppState::getChildCopy(const juce::Identifier& nodeId)
{
    const juce::ScopedLock sl(treeLock_);
    return root_.getChildWithName(nodeId).createCopy();
}

void AppState::stripSuperseded(juce::ValueTree& tree) const
{
    for (const auto& id : supersededNodes_)
    {
        auto c = tree.getChildWithName(id);
        if (c.isValid())
            tree.removeChild(c, nullptr);
    }
}

bool AppState::markDirty()
{
    if (isDirtySuppressedOnThisThread())
        return false;   // this thread is rewriting the tree (save / load / snapshot)

    auto fire = [this]()
    {
        // Only nonParameterStateChanged sets VST3's pluginShouldBeMarkedDirtyFlag (A16).
        processor_.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged(true));
        juce::Logger::writeToLog("[AppState] markDirty -> host notified (nonParameterStateChanged)");
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        fire();
    else
        juce::MessageManager::callAsync([alive = aliveToken_, fire]() { if (alive->load()) fire(); });
    return true;
}

bool AppState::snapshotBinding(const juce::Identifier& nodeId)
{
    jassert(juce::MessageManager::existsAndIsCurrentThread());
    ScopedDirtySuppress quiet(*this);
    // Lock FIRST, then test dispatchPending_ (review N-14): deserialize sets it
    // inside the same lock scope that swaps root_, so a project loaded off the
    // message thread can never be overwritten by this snapshot of the old state.
    const juce::ScopedLock sl(treeLock_);
    if (dispatchPending_.load())
    {
        juce::Logger::writeToLog("[AppState] snapshotBinding '" + nodeId.toString() + "' skipped (restore dispatch pending)");
        return false;
    }
    for (auto& e : bindings_)
    {
        if (e.binding.nodeId != nodeId || ! e.binding.save) continue;
        auto child = getOrCreateChild(nodeId);
        child.setProperty("version", e.binding.version, nullptr);
        try {
            e.binding.save(child);
            return true;
        } catch (const std::exception& ex) {
            juce::Logger::writeToLog("[AppState]   snapshotBinding '" + nodeId.toString() + "' FAILED: " + juce::String(ex.what()));
            return false;
        }
    }
    return false;
}

void AppState::snapshotAllBindings()
{
    SAMPLAR_TRACE_SCOPE("state", "AppState::snapshotAllBindings");
    jassert(juce::MessageManager::existsAndIsCurrentThread());
    ScopedDirtySuppress quiet(*this);
    const juce::ScopedLock sl(treeLock_);   // lock, THEN test (review N-14)
    if (dispatchPending_.load())
    {
        // The loaded project has not been handed to the bindings yet: the
        // live state is stale, the tree is the truth (A4 at AppState level).
        juce::Logger::writeToLog("[AppState] snapshotAllBindings skipped (restore dispatch pending - keeping the loaded tree)");
        return;
    }
    juce::Logger::writeToLog("[AppState] snapshotAllBindings (" + juce::String(static_cast<int>(bindings_.size())) + " bindings)");
    for (auto& e : bindings_)
    {
        if (! e.binding.save) continue;
        auto child = getOrCreateChild(e.binding.nodeId);
        child.setProperty("version", e.binding.version, nullptr);
        try {
            e.binding.save(child);
            juce::Logger::writeToLog("[AppState]   snapshot '" + e.binding.nodeId.toString() + "' ok");
        } catch (const std::exception& ex) {
            juce::Logger::writeToLog("[AppState]   snapshot '" + e.binding.nodeId.toString() + "' FAILED: " + juce::String(ex.what()));
        }
    }
    // restoreReady_ stays true so new registrations late-drain from this snapshot.
    restoreReady_ = true;
}

void AppState::restoreAllBindings()
{
    juce::Logger::writeToLog("[AppState] restoreAllBindings (" + juce::String(static_cast<int>(bindings_.size())) + " bindings)");
    ScopedDirtySuppress quiet(*this);
    for (auto& e : bindings_)
    {
        if (! e.binding.restore) continue;
        if (isSuperseded(e.binding.nodeId)) continue;
        juce::ValueTree child;
        {
            const juce::ScopedLock sl(treeLock_);
            child = root_.getChildWithName(e.binding.nodeId).createCopy();
        }
        if (! child.isValid())
        {
            juce::Logger::writeToLog("[AppState]   restore '" + e.binding.nodeId.toString() + "' skipped (no child in tree)");
            continue;
        }
        try {
            e.binding.restore(child);
            juce::Logger::writeToLog("[AppState]   restore '" + e.binding.nodeId.toString() + "' ok");
        } catch (const std::exception& ex) {
            juce::Logger::writeToLog("[AppState]   restore '" + e.binding.nodeId.toString() + "' FAILED: " + juce::String(ex.what()));
        }
    }
}

void AppState::serialize(juce::MemoryBlock& out)
{
    SAMPLAR_TRACE_SCOPE("state", "AppState::serialize");
    const bool onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
    juce::Logger::writeToLog("[AppState] === serialize START (bindings=" + juce::String(static_cast<int>(bindings_.size()))
                             + ", thread=" + juce::String(onMessageThread ? "message" : "OTHER") + ") ===");
    ScopedDirtySuppress quiet(*this);

    // Refresh every child tree from live state before streaming out - only on
    // the message thread and only once a loaded project reached the bindings.
    if (onMessageThread && ! dispatchPending_.load())
        snapshotAllBindings();
    else
        juce::Logger::writeToLog(juce::String("[AppState]   streaming the cached tree (")
                                 + (onMessageThread ? "restore dispatch pending" : "not the message thread") + ")");

    juce::ValueTree copy;
    {
        const juce::ScopedLock sl(treeLock_);
        copy = root_.createCopy();
    }
    stripSuperseded(copy);

    out.reset();
    out.append(kMagic, kMagicLen);
    const juce::uint16 ver = kFormatVersion;
    out.append(&ver, sizeof(ver));

    juce::MemoryOutputStream mos;
    copy.writeToStream(mos);
    out.append(mos.getData(), mos.getDataSize());

    juce::Logger::writeToLog("[AppState] === serialize END (" + juce::String(static_cast<int>(out.getSize())) + " bytes) ===");
}

void AppState::dispatchRestores()
{
    jassert(juce::MessageManager::existsAndIsCurrentThread());
    ScopedDirtySuppress quiet(*this);

    // Dispatch restore to every already-registered binding (copies of the
    // children, so a binding never mutates the loaded tree).
    int restored = 0, missed = 0;
    for (auto& e : bindings_)
    {
        if (! e.binding.restore)
            continue;
        if (isSuperseded(e.binding.nodeId))
        {
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString() + "' superseded - not restored");
            continue;
        }

        juce::ValueTree child;
        {
            const juce::ScopedLock sl(treeLock_);
            child = root_.getChildWithName(e.binding.nodeId).createCopy();
        }
        if (! child.isValid() && ! e.binding.restoreWhenMissing)
        {
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString() + "' - no child in tree, skipping");
            ++missed;
            continue;
        }
        try {
            e.binding.restore(child);   // invalid tree = "project has no such node" (restoreWhenMissing)
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString() + "' restore ok"
                                     + juce::String(child.isValid() ? "" : " (node absent -> reset)"));
            ++restored;
        }
        catch (const std::exception& ex) {
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString()
                                     + "' restore FAILED: " + juce::String(ex.what()));
        }
    }

    juce::Logger::writeToLog("[AppState] restore dispatch done (restored=" + juce::String(restored)
                             + " missed=" + juce::String(missed) + ")");
}

void AppState::deserialize(const void* data, int size)
{
    SAMPLAR_TRACE_SCOPE("state", "AppState::deserialize");
    const bool onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
    juce::Logger::writeToLog("[AppState] === deserialize START (" + juce::String(size) + " bytes, thread="
                             + juce::String(onMessageThread ? "message" : "OTHER") + ") ===");
    ScopedDirtySuppress quiet(*this);

    if (data == nullptr || size <= 0)
    {
        restoreReady_ = true;  // empty state is still a valid "restored" state
        juce::Logger::writeToLog("[AppState]   empty payload - marking restore ready");
        return;
    }

    auto* bytes = static_cast<const juce::uint8*>(data);
    bool loaded = false;
    int seq = 0;

    // Called with treeLock_ held, in the SAME scope that replaced root_ (review
    // N-14): a snapshot that takes the lock afterwards always sees the flag.
    auto markLoadedLocked = [this, onMessageThread, &seq]()
    {
        dispatchPending_ = ! onMessageThread;   // off-thread: restores are posted
        seq = ++dispatchSeq_;                   // a newer load supersedes older posts
    };

    // -- New binary format ---------------------------------------------
    if (size >= kMagicLen && std::memcmp(bytes, kMagic, kMagicLen) == 0)
    {
        if (size < kMagicLen + 2)
        {
            juce::Logger::writeToLog("[AppState]   binary header truncated - aborting");
            restoreReady_ = true;
            return;
        }
        const juce::uint16 ver = static_cast<juce::uint16>(bytes[kMagicLen]) |
                                  (static_cast<juce::uint16>(bytes[kMagicLen + 1]) << 8);
        juce::Logger::writeToLog("[AppState]   format=SMAS version=" + juce::String(ver));

        juce::MemoryInputStream mis(bytes + kMagicLen + 2, (size_t) (size - kMagicLen - 2), false);
        auto parsed = juce::ValueTree::readFromStream(mis);
        if (parsed.isValid() && parsed.hasType(kRootId))
        {
            const juce::ScopedLock sl(treeLock_);
            // Replace root_ contents while keeping the same ValueTree handle + listener.
            root_.removeListener(&listener_);
            root_.removeAllProperties(nullptr);
            root_.removeAllChildren(nullptr);
            for (int i = 0; i < parsed.getNumProperties(); ++i)
            {
                auto name = parsed.getPropertyName(i);
                root_.setProperty(name, parsed.getProperty(name), nullptr);
            }
            for (int i = 0; i < parsed.getNumChildren(); ++i)
                root_.appendChild(parsed.getChild(i).createCopy(), nullptr);
            // Superseded nodes stay in memory (never restored, stripped on
            // save) so a newer binding can migrate them (smodeStemPitch/Mod).
            stampRootVersion();
            root_.addListener(&listener_);
            markLoadedLocked();
            loaded = true;
            juce::Logger::writeToLog("[AppState]   tree loaded: " + juce::String(root_.getNumChildren()) + " children");
        }
        else
        {
            juce::Logger::writeToLog("[AppState]   binary parse FAILED or wrong root type");
        }
    }
    // -- Legacy JSON format --------------------------------------------
    else if (bytes[0] == '{' || bytes[0] == ' ' || bytes[0] == '\n' || bytes[0] == '\r' || bytes[0] == '\t')
    {
        juce::Logger::writeToLog("[AppState]   legacy JSON detected - importing");
        auto jsonText = juce::String::fromUTF8(static_cast<const char*>(data), size);
        const juce::ScopedLock sl(treeLock_);
        importLegacyJson(jsonText);
        stampRootVersion();
        markLoadedLocked();
        loaded = true;
    }
    else
    {
        juce::Logger::writeToLog("[AppState]   unknown state format - ignoring");
    }

    restoreReady_ = true;

    if (! loaded)
    {
        const juce::ScopedLock sl(treeLock_);
        stampRootVersion();
        juce::Logger::writeToLog("[AppState] === deserialize END (nothing loaded) ===");
        return;
    }

    if (onMessageThread)
    {
        dispatchRestores();
        juce::Logger::writeToLog("[AppState] === deserialize END (restored on the message thread, seq "
                                 + juce::String(seq) + ") ===");
        return;
    }

    // A18: never run binding restores (UI, services, model) off the message
    // thread. Until the post runs, serialize() streams the loaded tree.
    juce::MessageManager::callAsync([this, alive = aliveToken_, seq]()
    {
        if (! alive->load()) return;
        {
            const juce::ScopedLock sl(treeLock_);
            if (seq != dispatchSeq_.load())
                return;   // a newer setState superseded this one; its own post restores
            dispatchPending_ = false;
        }
        juce::Logger::writeToLog("[AppState] deferred restore dispatch (seq " + juce::String(seq) + ")");
        dispatchRestores();
    });
    juce::Logger::writeToLog("[AppState] === deserialize END (restores posted to the message thread) ===");
}

void AppState::importLegacyJson(const juce::String& jsonText)
{
    auto parsed = juce::JSON::parse(jsonText);
    if (! parsed.isObject())
    {
        juce::Logger::writeToLog("[AppState]   legacy JSON parse failed");
        return;
    }

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    // Helper: copy all properties of a juce::var object into a ValueTree child.
    auto copyObjProps = [](const juce::var& v, juce::ValueTree& dst)
    {
        auto* d = v.getDynamicObject();
        if (d == nullptr) return;
        const auto& props = d->getProperties();
        for (int i = 0; i < props.size(); ++i)
            dst.setProperty(props.getName(i), props.getValueAt(i), nullptr);
    };

    // --- ui section ---
    if (auto uiVar = obj->getProperty("ui"); uiVar.isObject())
    {
        auto ui = getOrCreateChild("ui");
        copyObjProps(uiVar, ui);
        juce::Logger::writeToLog("[AppState]   legacy import: ui");
    }

    // --- smode section ---
    if (auto smodeVar = obj->getProperty("smode"); smodeVar.isObject())
    {
        auto sm = getOrCreateChild("smode");
        copyObjProps(smodeVar, sm);

        // stemFiles array → <stems><stem file="..."/>...</stems>
        if (auto sd = smodeVar.getDynamicObject())
        {
            auto stemsVar = sd->getProperty("stemFiles");
            if (stemsVar.isArray())
            {
                // Remove the flat property copy (if any) and store as children instead.
                sm.removeProperty("stemFiles", nullptr);
                auto stems = juce::ValueTree("stems");
                for (int i = 0; i < stemsVar.size(); ++i)
                {
                    juce::ValueTree s("stem");
                    s.setProperty("file", stemsVar[i].toString(), nullptr);
                    stems.appendChild(s, nullptr);
                }
                sm.appendChild(stems, nullptr);
            }
        }
        juce::Logger::writeToLog("[AppState]   legacy import: smode");
    }

    // --- midi section (array of slot objects) ---
    if (auto midiVar = obj->getProperty("midi"); midiVar.isArray())
    {
        auto midi = getOrCreateChild("midi");
        // Clear any existing children so import is idempotent.
        midi.removeAllChildren(nullptr);

        for (int i = 0; i < midiVar.size(); ++i)
        {
            auto entry = midiVar[i];
            if (! entry.isObject()) continue;
            juce::ValueTree slot("slot");
            copyObjProps(entry, slot);
            midi.appendChild(slot, nullptr);
        }
        juce::Logger::writeToLog("[AppState]   legacy import: midi (" + juce::String(midi.getNumChildren()) + " slots)");
    }
}

} // namespace samplar
