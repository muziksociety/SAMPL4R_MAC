#include "AppState.h"
#include "../utils/Tracing.h"

namespace samplar {

const juce::Identifier AppState::kRootId { "SM5AppState" };

AppState::AppState(juce::AudioProcessor& processor)
    : processor_(processor), root_(kRootId)
{
    stampRootVersion();
    root_.addListener(&listener_);
    juce::Logger::writeToLog("[AppState] Constructed");
}

AppState::~AppState()
{
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
                             + (restoreReady_ ? " (draining now)" : " (buffered)"));

    // Late-drain: if state has already been deserialized and a child tree exists,
    // fire restore immediately so the feature doesn't need a separate "pending" path.
    // NOTE: we defer the actual restore call to the message queue so the caller's
    // constructor can finish (wiring callbacks, adding child components) before any
    // restore logic fires. This fixes the bug where readFromTree triggers onFileDropped
    // but the parent hadn't yet assigned that callback.
    if (restoreReady_ && hasRestore)
    {
        auto child = root_.getChildWithName(id);
        if (child.isValid())
        {
            const int capturedToken = token;
            juce::MessageManager::callAsync([this, capturedToken, id, child]() {
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
    auto child = root_.getChildWithName(nodeId);
    if (! child.isValid())
    {
        child = juce::ValueTree(nodeId);
        root_.appendChild(child, nullptr);
    }
    return child;
}

void AppState::markDirty()
{
    processor_.updateHostDisplay();
}

void AppState::snapshotAllBindings()
{
    SAMPLAR_TRACE_SCOPE("state", "AppState::snapshotAllBindings");
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
    for (auto& e : bindings_)
    {
        if (! e.binding.restore) continue;
        auto child = root_.getChildWithName(e.binding.nodeId);
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
    juce::Logger::writeToLog("[AppState] === serialize START (bindings=" + juce::String(static_cast<int>(bindings_.size())) + ") ===");

    // Refresh every child tree from live state before streaming out.
    snapshotAllBindings();

    out.reset();
    out.append(kMagic, kMagicLen);
    const juce::uint16 ver = kFormatVersion;
    out.append(&ver, sizeof(ver));

    juce::MemoryOutputStream mos;
    root_.writeToStream(mos);
    out.append(mos.getData(), mos.getDataSize());

    juce::Logger::writeToLog("[AppState] === serialize END (" + juce::String(static_cast<int>(out.getSize())) + " bytes) ===");
}

void AppState::deserialize(const void* data, int size)
{
    SAMPLAR_TRACE_SCOPE("state", "AppState::deserialize");
    juce::Logger::writeToLog("[AppState] === deserialize START (" + juce::String(size) + " bytes) ===");

    if (data == nullptr || size <= 0)
    {
        restoreReady_ = true;  // empty state is still a valid "restored" state
        juce::Logger::writeToLog("[AppState]   empty payload — marking restore ready");
        return;
    }

    auto* bytes = static_cast<const juce::uint8*>(data);

    // ── New binary format ─────────────────────────────────────────────
    if (size >= kMagicLen && std::memcmp(bytes, kMagic, kMagicLen) == 0)
    {
        if (size < kMagicLen + 2)
        {
            juce::Logger::writeToLog("[AppState]   binary header truncated — aborting");
            restoreReady_ = true;
            return;
        }
        const juce::uint16 ver = static_cast<juce::uint16>(bytes[kMagicLen]) |
                                  (static_cast<juce::uint16>(bytes[kMagicLen + 1]) << 8);
        juce::Logger::writeToLog("[AppState]   format=SMAS version=" + juce::String(ver));

        juce::MemoryInputStream mis(bytes + kMagicLen + 2, size - kMagicLen - 2, false);
        auto parsed = juce::ValueTree::readFromStream(mis);
        if (parsed.isValid() && parsed.hasType(kRootId))
        {
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
            root_.addListener(&listener_);
            juce::Logger::writeToLog("[AppState]   tree loaded: " + juce::String(root_.getNumChildren()) + " children");
        }
        else
        {
            juce::Logger::writeToLog("[AppState]   binary parse FAILED or wrong root type");
        }
    }
    // ── Legacy JSON format ────────────────────────────────────────────
    else if (bytes[0] == '{' || bytes[0] == ' ' || bytes[0] == '\n' || bytes[0] == '\r' || bytes[0] == '\t')
    {
        juce::Logger::writeToLog("[AppState]   legacy JSON detected — importing");
        auto jsonText = juce::String::fromUTF8(static_cast<const char*>(data), size);
        importLegacyJson(jsonText);
    }
    else
    {
        juce::Logger::writeToLog("[AppState]   unknown state format — ignoring");
    }

    stampRootVersion();
    restoreReady_ = true;

    // Dispatch restore to every already-registered binding.
    int restored = 0, missed = 0;
    for (auto& e : bindings_)
    {
        if (! e.binding.restore)
            continue;

        auto child = root_.getChildWithName(e.binding.nodeId);
        if (! child.isValid())
        {
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString() + "' — no child in tree, skipping");
            ++missed;
            continue;
        }
        try {
            e.binding.restore(child);
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString() + "' restore ok");
            ++restored;
        }
        catch (const std::exception& ex) {
            juce::Logger::writeToLog("[AppState]   binding '" + e.binding.nodeId.toString()
                                     + "' restore FAILED: " + juce::String(ex.what()));
        }
    }

    juce::Logger::writeToLog("[AppState] === deserialize END (restored=" + juce::String(restored)
                             + " missed=" + juce::String(missed) + ") ===");
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
