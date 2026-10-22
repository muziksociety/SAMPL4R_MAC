#include "MainWindow.h"
#include "../app/App.h"
#include "../app/ServiceLocator.h"
#include "../app/ServiceRegistry.h"
#include "Pages/SModePage.h"
#include "../services/smode/SModeService.h"
#include "../services/smode/SModeSession.h"
#include <type_traits>
#include "../services/MidiMappingService.h"
#include "../services/FullSongService.h"
#include "licensing/LicenseStatusWidget.h"
#include "Debug/DebugConsole.h"
#include "../app/MultiLogger.h"
#include "../utils/LogPaths.h"
#include "Style/SamplarLookAndFeel.h"
#include "Components/SettingsDialog.h"
#include "Components/NotificationButton.h"
#include "Components/UpdateDialog.h"
#include "../services/UpdateCheckService.h"


using namespace juce;

namespace samplar {

// ============ WINDOW CONTROL BUTTONS (macOS-style traffic lights) ============

// Exit Button - Red circular button with X icon
class ExitButton : public juce::Button {
public:
    ExitButton() : Button("Exit") {
        setTooltip("Exit");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void clicked() override {
        // Find MainWindow and call its closeButtonPressed
        if (auto* mainWindow = findParentComponentOfClass<juce::DocumentWindow>())
        {
            mainWindow->closeButtonPressed();
        }
        else
        {
            samplar::ServiceLocator::requestQuit();
        }
    }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        auto baseColor = juce::Colour::fromString("ffFF5F56");  // Red
        auto color = down ? baseColor.brighter(0.2f) : (over ? baseColor.brighter(0.15f) : baseColor);

        // Circle outline only (no fill)
        g.setColour(color);
        g.drawEllipse(r, 1.5f);

        // X icon
        g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));
        auto cx = r.getCentreX();
        auto cy = r.getCentreY();
        auto size = r.getWidth() * 0.25f;
        g.drawLine(cx - size, cy - size, cx + size, cy + size, 1.5f);
        g.drawLine(cx + size, cy - size, cx - size, cy + size, 1.5f);
    }
};

// Pin Button - Yellow/Green circular button for Always On Top toggle
class PinButton : public juce::Button {
public:
    PinButton() : Button("Pin") {
        setTooltip("Toggle Always On Top");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setClickingTogglesState(true);
    }
    void clicked() override {
        if (auto* tl = getTopLevelComponent()) {
            bool newState = !tl->isAlwaysOnTop();
            tl->setAlwaysOnTop(newState);
            setToggleState(newState, juce::dontSendNotification);
            repaint();
        }
    }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        bool pinned = getToggleState();

        // Yellow when off, Green when pinned
        auto offColor = juce::Colour::fromString("ffFFBD2E");   // Yellow
        auto onColor = juce::Colour::fromString("ff00FF88");    // Green
        auto baseColor = pinned ? onColor : offColor;
        auto color = down ? baseColor.brighter(0.2f) : (over ? baseColor.brighter(0.15f) : baseColor);

        // Circle outline only (no fill)
        g.setColour(color);
        g.drawEllipse(r, 1.5f);

        // Pin icon (circle or filled dot when pinned)
        g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));
        auto cx = r.getCentreX();
        auto cy = r.getCentreY();
        if (pinned) {
            // Filled dot when pinned
            g.fillEllipse(cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
        } else {
            // Empty circle when unpinned
            g.drawEllipse(cx - 3.0f, cy - 3.0f, 6.0f, 6.0f, 1.5f);
        }
    }
};

// Drag Button - Cyan circular button with grip lines for window dragging
class DragButton : public juce::Button {
public:
    DragButton() : Button("Drag") {
        setTooltip("Drag to move window");
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (auto* tl = getTopLevelComponent()) dragger_.startDraggingComponent(tl, e);
        Button::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (auto* tl = getTopLevelComponent()) dragger_.dragComponent(tl, e, nullptr);
        Button::mouseDrag(e);
    }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        auto baseColor = juce::Colour::fromString("ff00CFCF");  // Cyan
        auto color = down ? baseColor.brighter(0.2f) : (over ? baseColor.brighter(0.15f) : baseColor);

        // Circle outline only (no fill)
        g.setColour(color);
        g.drawEllipse(r, 1.5f);

        // Grip lines (3 horizontal lines)
        g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));
        auto cx = r.getCentreX();
        auto cy = r.getCentreY();
        auto w = r.getWidth() * 0.4f;
        auto h = 1.5f;
        auto gap = 3.0f;
        g.fillRoundedRectangle(cx - w * 0.5f, cy - h - gap, w, h, 0.75f);
        g.fillRoundedRectangle(cx - w * 0.5f, cy - h * 0.5f, w, h, 0.75f);
        g.fillRoundedRectangle(cx - w * 0.5f, cy + gap, w, h, 0.75f);
    }
private:
    juce::ComponentDragger dragger_;
};

class RootComponent : public Component, public DragAndDropContainer, public FileDragAndDropTarget, private Timer {
public:
    RootComponent()
    {
        juce::Logger::writeToLog("???? [RootComponent] Constructor ENTER");

        try {
            juce::Logger::writeToLog("???? [RootComponent] Setting up focus (skipping size - parent handles it)");

            setWantsKeyboardFocus(true);
            juce::Logger::writeToLog("???? [RootComponent] Keyboard focus requested");

            grabKeyboardFocus();
            juce::Logger::writeToLog("???? [RootComponent] Keyboard focus grabbed");

            juce::Logger::writeToLog("???? [RootComponent] Setting up LookAndFeel");
            LookAndFeel::setDefaultLookAndFeel(&lnf_);
            juce::Logger::writeToLog("???? [RootComponent] LookAndFeel set successfully");

            juce::Logger::writeToLog("???? [RootComponent] Setting up navigation (debug-only sidebar)");
            addAndMakeVisible(nav_);
            nav_.setModel(&navModel_);
            nav_.setRowHeight(26);
            nav_.setVisible(debugVisible_);

            juce::Logger::writeToLog("???? [RootComponent] Setting up SModePage as main content");
            addAndMakeVisible(smodePage_);


            // SAMPLAR: SModePage is now the permanent main content (added in constructor above).
            // onBack callback kept for MIDI cleanup but no longer switches pages.
            smodePage_.onBack = [this]{
                // Fully reset S-Mode: disconnect MIDI, clear model, reset service
                if (auto* reg = ctx_.getRegistry()) {
                    auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
                    if (midiSvc) {
                        midiSvc->stopAllVoices();
                        midiSvc->setSModeModel(nullptr);
                    }
                    auto* smodeSvc = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
                    if (smodeSvc) {
                        smodeSvc->reset();
                        juce::Logger::writeToLog("[MainWindow] S-Mode fully reset on Back");
                    }
                }
                smodeService_ = nullptr;
                // SModePage stays visible — just cleared
            };
            juce::Logger::writeToLog("[RootComponent] SModePage is permanent main content");

            // Restore S-Mode if SModeService is already active AND has loaded audio (editor close/reopen)
            bool smodeRestored = false;
            if (auto* reg = ctx_.getRegistry())
            {
                auto* smodeSvc = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
                if (smodeSvc && smodeSvc->isActive() && smodeSvc->getModel().getStemCount() > 0)
                {
                    // Verify stems actually have audio data (not just empty model)
                    bool hasAudio = false;
                    for (int i = 0; i < smodeSvc->getModel().getStemCount(); ++i)
                    {
                        auto* stem = smodeSvc->getModel().getStem(i);
                        if (stem && stem->audioBuffer.getNumSamples() > 0)
                        { hasAudio = true; break; }
                    }

                    if (hasAudio)
                    {
                        // B11: shown ONCE, by the deferred reconnect below (a second
                        // onStemsReady rebuilt every lane and wiped the undo history).
                        juce::Logger::writeToLog("[RootComponent] S-Mode already active with audio - UI reconnect deferred");
                        smodeService_ = smodeSvc;
                        smodeRestored = true;
                    }
                }
            }

            // SModePage is always visible — no page switch needed regardless of smodeRestored
            juce::Logger::writeToLog("[RootComponent] SModePage is permanent main content (smodeRestored=" + juce::String(smodeRestored ? "true" : "false") + ")");

            if (auto* registry = ctx_.getRegistry())
            {
                auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"));
                licenseStatusWidget_ = std::make_unique<LicenseStatusWidget>(licenseSvc, ctx_.getBus());
                if (licenseStatusWidget_ != nullptr)
                {
                    addAndMakeVisible(licenseStatusWidget_.get());
                    licenseStatusWidget_->setVisible(nav_.isVisible());
                }
            }
            juce::Logger::writeToLog("[RootComponent] Constructor: SModePage visible, ready");

            juce::Logger::writeToLog("???? [RootComponent] Adding window control buttons");
            bool isPlugin = (ctx_.getUIState() != nullptr);
            isPlugin_ = isPlugin;
            if (!isPlugin)
            {
                addAndMakeVisible(exitBtn_);
                addAndMakeVisible(pinBtn_);
                addAndMakeVisible(dragBtn_);
            }
            settingsBtn_.setContext(ctx_);   // the dialog uses THIS instance's services (A2)
            addAndMakeVisible(settingsBtn_);

            addAndMakeVisible(notifBtn_); // always visible for version info

            // Handle notification button click - show update dialog
            notifBtn_.onClick = [this]() {
                juce::Logger::writeToLog("[RootComponent] NotificationButton clicked");
                showUpdateDialog();
            };

            // Subscribe to update events from UpdateCheckService
            // B5: keep the handler ids and unsubscribe in the destructor - the
            // bus is processor-lifetime and these events can fire (update check
            // runs ~5 s after load) after the editor has been closed.
            if (auto* bus = ctx_.getBus()) {
                updateAvailableSub_ = bus->subscribe("update.available", [this, alive = alive_](const juce::var& payload) {
                    if (!*alive) return;
                    juce::Logger::writeToLog("[RootComponent] Received update.available event");
                    handleUpdateAvailable(payload);
                });
                licenseReminderSub_ = bus->subscribe("license.status.reminder", [this, alive = alive_](const juce::var& payload) {
                    if (!*alive) return;
                    juce::Logger::writeToLog("[RootComponent] Received license.status.reminder event");
                    handleLicenseReminder(payload);
                });
            }

            juce::Logger::writeToLog("???? [RootComponent] Setting up debug console");
            addChildComponent(debugConsole_);
            debugConsole_.setVisible(debugVisible_);
            // A21: the editor no longer replaces the process-global logger. The
            // processor (plugin) / App (standalone) FileLogger owns logging for
            // the whole lifetime, so teardown after the first editor close is
            // logged and two instances can't swap loggers under each other.

            juce::Logger::writeToLog("[RootComponent] Setting up SModePage separation callbacks");

            // === SMODE SEPARATION FLOW (triggered from SModePage via drag-drop or menu) ===
            // SModePage calls onRequestSeparation when the user drops a song file.
            // Runs the SModeService workflow (htdemucs: always 4 stems); progress is
            // shown as per-lane loading bars on SModePage.
            smodePage_.onRequestSeparation = [this, alive = alive_](const juce::File& f) {
                if (!*alive) return;
                juce::Logger::writeToLog("[MainWindow] SModePage requested separation for: " + f.getFullPathName());

                if (!smodeService_)
                    smodeService_ = smodeSvc();
                if (!smodeService_) {
                    juce::Logger::writeToLog("[MainWindow] ERROR: SModeService not registered");
                    return;
                }

                // A song is still separating: cancel it and load the new one as
                // soon as the "Cancelled" error arrives (C2 - the ONNX run now
                // aborts mid-chunk, so this takes well under a second).
                if (smodeService_->getSeparation().isSeparating())
                {
                    juce::Logger::writeToLog("[MainWindow] New song while separating - cancelling current, queued: "
                                             + f.getFileName());
                    pendingSeparationFile_ = f;
                    smodeService_->getSeparation().cancelSeparation();
                    return;
                }

                const int stemCount = 4;   // htdemucs.onnx is 4-stem only
                juce::Logger::writeToLog("[MainWindow] S-Mode separation starting, stems: " + juce::String(stemCount));

                // Progress / ready / error arrive through the permanent
                // subscriptions made in the constructor (subscribeSModeEvents).
                smodePage_.setStemLoadingProgress(0.0f);       // reset bars to 0% before starting
                smodePage_.beginSeparationVisuals(stemCount);  // pre-create N placeholder lanes
                smodePage_.setStemLoadingState(true);

                smodeService_->startWorkflow(f, samplar::SModeSeparationService::StemCount::Four);
            };

            // Permanent S-Mode subscriptions + replay of an in-flight
            // separation / restore when the editor is (re)opened (B10).
            subscribeSModeEvents();
            juce::MessageManager::callAsync([this, alive = alive_]()
            {
                if (!*alive) return;
                replaySModeProgress();
            });

        // -- Reconnect S-Mode UI if processor already has an active session --
        // This handles editor close/reopen (minimize, DAW hide/show) where the
        // processor-lifetime SModeService still has loaded stems + segments but
        // the UI was destroyed and recreated from scratch.
        // DEFERRED: must wait until the component tree is fully constructed and
        // laid out � calling onStemsReady() synchronously in the constructor
        // crashes because SModePage layout isn't ready yet.
        juce::Timer::callAfterDelay(100, [this, alive = alive_]()
        {
            if (!*alive) return;
            auto* reg = ctx_.getRegistry();
            if (!reg) return;
            auto* smodeSvc = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
            if (smodeSvc && smodeSvc->isActive() && smodeSvc->getModel().getStemCount() > 0)
            {
                if (shownGeneration_ == smodeSvc->getModel().getContentGeneration())
                {
                    juce::Logger::writeToLog("[RootComponent] Reconnect skipped: the page already shows this session");
                    return;
                }
                juce::Logger::writeToLog("[RootComponent] Reconnecting S-Mode UI to existing session ("
                                         + juce::String(smodeSvc->getModel().getStemCount()) + " stems)");
                smodeService_ = smodeSvc;
                smodePage_.onStemsReady(*smodeSvc);
                afterStemsReady();
                switchTo(9);  // Navigate to S-Mode page so user sees their session
            }
        });

        juce::Logger::writeToLog("???? [RootComponent] Constructor SUCCESS");
        }
        catch (const std::exception& e) {
            juce::Logger::writeToLog("???? [RootComponent] EXCEPTION in constructor: " + juce::String(e.what()));
            throw;
        }
        catch (...) {
            juce::Logger::writeToLog("???? [RootComponent] UNKNOWN EXCEPTION in constructor");
            throw;
        }
    }

    ~RootComponent() override
    {
        juce::Logger::writeToLog("[RootComponent] Destructor - saving UI state and unsubscribing bus handlers");

        // Save UI state so the next editor can restore it (VST3: editor is destroyed on close)
        if (auto* uiState = ctx_.getUIState())
        {
            uiState->currentPage = currentPage_;
            uiState->previousPage = previousPage_;
            // SAMPLAR: browser_ removed - folder state not saved
            uiState->loadedFolder = juce::String();
            uiState->libraryMode = false;
            uiState->libraryFolder = juce::String();
            uiState->hasSavedState = true;
            juce::Logger::writeToLog("[RootComponent] UI state saved: page=" + juce::String(currentPage_));
        }

        // CRITICAL: Must be FIRST. setDefaultLookAndFeel(&lnf_) leaves a pointer in JUCE's
        // Desktop singleton. If not cleared before lnf_ is freed, the next editor open
        // calls lookAndFeelChanged() on dead memory and crashes the host.
        LookAndFeel::setDefaultLookAndFeel(nullptr);
        *alive_ = false;
        stopTimer();
        unsubscribeSModeEvents();
        if (auto* bus = ctx_.getBus())
        {
            // Update / licence / S-Mode workflow / state-restore handlers (B5)
            if (updateAvailableSub_    != 0) bus->unsubscribe("update.available",        updateAvailableSub_);
            if (licenseReminderSub_    != 0) bus->unsubscribe("license.status.reminder", licenseReminderSub_);
            if (stateRestoredSub_      != 0) bus->unsubscribe("state.restored",          stateRestoredSub_);
            juce::Logger::writeToLog("[RootComponent] bus handlers unsubscribed");
        }
        juce::Logger::writeToLog("[RootComponent] Destructor complete");
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto consoleH = debugVisible_ ? 180 : 0;
        auto main = r.removeFromTop(r.getHeight() - consoleH);

        if (nav_.isVisible())
        {
            auto navColumn = main.removeFromLeft(180);
            const int navMinimumHeight = 120;
            auto statusHeight = juce::jmax(0, navColumn.getHeight() - navMinimumHeight);
            statusHeight = juce::jmin(statusHeight, 160);
            juce::Rectangle<int> statusArea;
            if (statusHeight > 0)
                statusArea = navColumn.removeFromBottom(statusHeight);
            nav_.setBounds(navColumn.reduced(4));

            if (licenseStatusWidget_ != nullptr)
            {
                if (statusHeight > 0)
                {
                    licenseStatusWidget_->setVisible(true);
                    licenseStatusWidget_->setBounds(statusArea.reduced(6));
                }
                else
                {
                    licenseStatusWidget_->setVisible(false);
                    licenseStatusWidget_->setBounds(0, 0, 0, 0);
                }
            }
        }
        else
        {
            nav_.setBounds(0, 0, 0, 0);
            if (licenseStatusWidget_ != nullptr)
            {
                licenseStatusWidget_->setVisible(false);
                licenseStatusWidget_->setBounds(0, 0, 0, 0);
            }
        }

        // SModePage fills the main area directly (no pages_ container needed, kept for debug nav).
        // Window buttons sit in a "system zone" the page reserves at the right end of
        // its top bar (B15: they used to float over the DAW toggle).
        static constexpr int kWinBtn = 20, kWinGap = 4;
        const int nBtns = isPlugin_ ? 2 : 5;   // settings + notification (+ pin, drag, exit)
        smodePage_.setSystemZoneWidth(nBtns * kWinBtn + (nBtns - 1) * kWinGap);   // BEFORE setBounds
        smodePage_.setBounds(main.reduced(4));
        auto zone = smodePage_.getSystemZoneBounds().translated(smodePage_.getX(), smodePage_.getY());
        auto placeRight = [&zone](juce::Component& c)
        {
            c.setBounds(zone.removeFromRight(kWinBtn).withSizeKeepingCentre(kWinBtn, kWinBtn));
            zone.removeFromRight(kWinGap);
        };
        // Left-to-right result: pin, settings, drag, exit, notification
        placeRight(notifBtn_);
        if (! isPlugin_) { placeRight(exitBtn_); placeRight(dragBtn_); }
        placeRight(settingsBtn_);
        if (! isPlugin_) placeRight(pinBtn_);
        if (updateDialog_ != nullptr)
            updateDialog_->setCentrePosition(getWidth() / 2, getHeight() / 2);
        const auto log = "[RootComponent] resized: SModePage=" + smodePage_.getBounds().toString()
                       + " sysZone=" + smodePage_.getSystemZoneBounds().toString();
        if (log != lastResizeLog_)
        {
            lastResizeLog_ = log;
            juce::Logger::writeToLog(log);
        }
        debugConsole_.setBounds(r);
    }

    bool keyPressed(const KeyPress& key) override
    {
        if (key.getKeyCode() == KeyPress::F1Key) {
            if (f1Count_ == 0) startTimer(resetMs_);
            if (++f1Count_ >= 10) {
                f1Count_ = 0;
                stopTimer();
                debugVisible_ = !debugVisible_;
                nav_.setVisible(debugVisible_);
                debugConsole_.setVisible(debugVisible_);
                resized();
                return true;
            }
        }
        return Component::keyPressed(key);
    }

    // -- S-Mode wiring (persistence workstream) --------------------------------
    // Everything resolves through ctx_ (this editor's own processor), never the
    // process-global claim, so two instances never cross-wire (review A2).
    samplar::SModeService* smodeSvc() const
    {
        auto* reg = ctx_.getRegistry();
        return reg != nullptr ? dynamic_cast<samplar::SModeService*>(reg->get("smode")) : nullptr;
    }

    void subscribeSModeEvents()
    {
        auto* bus = ctx_.getBus();
        if (bus == nullptr)
        {
            juce::Logger::writeToLog("[RootComponent] subscribeSModeEvents: no bus");
            return;
        }
        smodeSubsBus_ = bus;

        smodeWorkflowReadySub_ = bus->subscribe("smode.workflow.ready", [this, alive = alive_](const juce::var& n) {
            if (!*alive) return;
            auto* svc = smodeSvc();
            if (svc == nullptr) return;
            juce::Logger::writeToLog("[RootComponent] smode.workflow.ready (" + n.toString() + " stems) -> page onStemsReady");
            smodeService_ = svc;
            smodePage_.setStemLoadingState(false);
            smodePage_.onStemsReady(*svc);
            afterStemsReady();
            smodePage_.resized();
            smodePage_.repaint();
            juce::MessageManager::callAsync([this, alive]() {
                if (!*alive) return;
                smodePage_.resized();
                smodePage_.repaint();
            });
        });

        smodeWorkflowErrorSub_ = bus->subscribe("smode.workflow.error", [this, alive = alive_](const juce::var& err) {
            if (!*alive) return;
            juce::Logger::writeToLog("[MainWindow] S-Mode error: " + err.toString());
            smodePage_.setStemLoadingState(false);
            if (err.toString() == "Cancelled")
            {
                // User replaced the song mid-separation: start the queued one.
                const auto next = pendingSeparationFile_;
                pendingSeparationFile_ = juce::File();
                if (next.existsAsFile() && smodePage_.onRequestSeparation)
                {
                    juce::Logger::writeToLog("[MainWindow] starting queued song: " + next.getFileName());
                    smodePage_.onRequestSeparation(next);
                }
                return;
            }
            // Only an error that REPORTS a missing runtime / model (N-21): a cached
            // song still loads without the runtime, and other errors while it is
            // missing keep their own message.
            if (samplar::FullSongService::isReinstallError(err.toString()))
            {
                // ONNX Runtime / model missing: in-page notice, not a modal alert.
                smodePage_.showSeparationUnavailableNotice();
                return;
            }
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "S-Mode Failed",
                "Could not separate stems:\n" + err.toString(), "OK");
        });

        smodeProgressSub_ = bus->subscribe("smode.separation.progress", [this, alive = alive_](const juce::var& pct) {
            if (!*alive) return;
            const int pctInt = static_cast<int>(pct);
            const float p01 = juce::jlimit(0.0f, 1.0f, static_cast<float>(pctInt) / 100.0f);
            juce::Logger::writeToLog("[MainWindow] smode.separation.progress -> " + juce::String(pctInt) + "% -> setStemLoadingProgress");
            smodePage_.setStemLoadingProgress(p01);
        });

        smodeRestoringSub_ = bus->subscribe("smode.session.restoring", [this, alive = alive_](const juce::var& n) {
            if (!*alive) return;
            juce::Logger::writeToLog("[RootComponent] smode.session.restoring (" + n.toString() + " stems) -> loading lanes");
            showSModeLoadingVisuals(juce::jlimit(1, 8, static_cast<int>(n) > 0 ? static_cast<int>(n) : 4), 0.0f);
        });

        smodeMissingSub_ = bus->subscribe("smode.session.missing", [this, alive = alive_](const juce::var& msg) {
            if (!*alive) return;
            juce::Logger::writeToLog("[RootComponent] smode.session.missing: " + msg.toString());
            smodePage_.setStemLoadingState(false);
            showMissingAudio(msg.toString());
        });
        juce::Logger::writeToLog("[RootComponent] S-Mode events subscribed on this instance's bus");
    }

    void unsubscribeSModeEvents()
    {
        if (smodeSubsBus_ == nullptr) return;
        auto* bus = smodeSubsBus_;
        if (smodeWorkflowReadySub_ != 0) bus->unsubscribe("smode.workflow.ready",      smodeWorkflowReadySub_);
        if (smodeWorkflowErrorSub_ != 0) bus->unsubscribe("smode.workflow.error",      smodeWorkflowErrorSub_);
        if (smodeProgressSub_      != 0) bus->unsubscribe("smode.separation.progress", smodeProgressSub_);
        if (smodeRestoringSub_     != 0) bus->unsubscribe("smode.session.restoring",   smodeRestoringSub_);
        if (smodeMissingSub_       != 0) bus->unsubscribe("smode.session.missing",     smodeMissingSub_);
        smodeWorkflowReadySub_ = smodeWorkflowErrorSub_ = smodeProgressSub_ = smodeRestoringSub_ = smodeMissingSub_ = 0;
        smodeSubsBus_ = nullptr;
    }

    void showSModeLoadingVisuals(int stemCount, float progress01)
    {
        smodePage_.setStemLoadingProgress(progress01);
        smodePage_.beginSeparationVisuals(stemCount);
        smodePage_.setStemLoadingState(true);
        smodePage_.setStemLoadingProgress(progress01);
    }

    // B10: the editor was (re)opened while a separation / project restore runs
    // in the processor: show the loading lanes with the current progress. The
    // permanent subscriptions deliver the rest (progress, ready, error).
    void replaySModeProgress()
    {
        auto* svc = smodeSvc();
        if (svc == nullptr) return;
        const auto status = svc->getSessionStatus();
        const bool separating = svc->getSeparation().isSeparating();
        const bool restoring  = status == samplar::SModeService::SessionStatus::Restoring;
        juce::Logger::writeToLog("[RootComponent] replaySModeProgress: separating=" + juce::String(separating ? 1 : 0)
            + " loadingStems=" + juce::String(svc->isLoadingStems() ? 1 : 0)
            + " status=" + juce::String(samplar::session::SessionState::toString(status))
            + " progress=" + juce::String(svc->getSeparation().getProgressPercent()) + "%");
        if (svc->isActive())
            return;   // stems are loaded: the reconnect path shows them
        if (separating || svc->isLoadingStems() || restoring)
        {
            smodeService_ = svc;
            const float p01 = separating ? juce::jlimit(0.0f, 1.0f, svc->getSeparation().getProgressPercent() / 100.0f) : 0.0f;
            showSModeLoadingVisuals(4, p01);
        }
        else if (status == samplar::SModeService::SessionStatus::MissingAudio)
        {
            showMissingAudio(svc->getSessionMessage());
        }
    }

    // In-page notice with a "Locate song..." relink button (no modal alert).
    void showMissingAudio(const juce::String& message)
    {
        smodePage_.showMissingAudioNotice(message);
    }

    // After every onStemsReady(): the page builds fresh stem lanes that do not
    // read the model's grid offset, so replay it through the page's existing
    // "smode.downbeat.detected" handler (restored projects / editor reopen).
    void afterStemsReady()
    {
        auto* svc = smodeSvc();
        if (svc == nullptr) return;
        shownGeneration_ = svc->getModel().getContentGeneration();
        const double offset = svc->getModel().getGridOffsetSeconds();
        if (offset > 0.0)
        {
            if (auto* bus = ctx_.getBus())
            {
                auto* obj = new juce::DynamicObject();
                obj->setProperty("beat1Sample", static_cast<juce::int64>(std::llround(offset * svc->getModel().getSampleRate())));
                obj->setProperty("offsetSeconds", offset);
                obj->setProperty("confidence", 1.0);
                juce::Logger::writeToLog("[RootComponent] afterStemsReady: grid offset " + juce::String(offset, 4) + " s -> stem lanes");
                bus->publish("smode.downbeat.detected", juce::var(obj));
            }
        }
        writeEditorDebugAck(*svc);
    }

    // Smoke-host round-trip check (SAMPLAR_DEBUG_SESSION_ACK): what the editor sees.
    void writeEditorDebugAck(samplar::SModeService& svc)
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable("SAMPLAR_DEBUG_SESSION_ACK", {});
        if (dir.isEmpty()) return;
        const auto extra = svc.getModel().captureExtraState();
        int lanes = -1, slots = 0, division = 0;
        if (auto* o = extra.getDynamicObject())
        {
            division = (int) o->getProperty("gridDivision");
            if (auto* arr = o->getProperty("lanes").getArray())
            {
                lanes = arr->size();
                for (auto& l : *arr)
                    if (auto* lo = l.getDynamicObject())
                        if (auto* sl = lo->getProperty("slots").getArray())
                            for (auto& sv : *sl)
                                if (auto* so = sv.getDynamicObject())
                                    if (! (bool) so->getProperty("empty")) ++slots;
            }
        }
        juce::File(dir).createDirectory();
        const auto f = juce::File(dir).getChildFile("editor_" + juce::String(++editorAckSeq_) + ".txt");
        f.replaceWithText("pendingExtra=" + juce::String(svc.getModel().hasPendingExtraState() ? 1 : 0)
                          + " lanes=" + juce::String(lanes) + " slots=" + juce::String(slots)
                          + " division=" + juce::String(division) + "\n"
                          + samplar::session::describeModel(svc.getModel()));
        juce::Logger::writeToLog("[RootComponent] editor debug ack: " + f.getFullPathName());
    }

public:
    // Hook EventBus progress/result to UI
    void attachBus()
    {
        // Restore UI state from previous editor session (VST3 close/reopen)
        if (auto* uiState = ctx_.getUIState())
        {
            if (uiState->hasSavedState)
            {
                juce::Logger::writeToLog("[RootComponent] Restoring UI state: page=" + juce::String(uiState->currentPage)
                    + " folder=" + uiState->loadedFolder);
                previousPage_ = uiState->previousPage;

                // SAMPLAR: browser_ removed - folder/library restore skipped

                // Switch to the page that was active (defer to avoid reentrancy during construction).
                // After the async switchTo runs, flip uiStateWriteEnabled_ ? true so
                // subsequent user-driven page switches save back to uiState normally.
                int targetPage = uiState->currentPage;
                juce::MessageManager::callAsync([this, alive = alive_, targetPage]() {
                    if (!*alive) return;
                    switchTo(targetPage);
                    uiStateWriteEnabled_ = true;
                });
            }
            else
            {
                // No saved state � enable uiState writes immediately so the
                // first user navigation gets persisted.
                juce::MessageManager::callAsync([this, alive = alive_]() {
                    if (!*alive) return;
                    uiStateWriteEnabled_ = true;
                });
            }
        }

        // Subscribe to "state.restored" � fired by setStateInformation AFTER editor is built.
        // This handles the timing gap: DAW calls setStateInformation after createEditor.
        if (auto* bus = ctx_.getBus())
        {
            stateRestoredSub_ = bus->subscribe("state.restored", [this, alive = alive_](const juce::var&) {
                if (!*alive) return;

                auto* uiState = ctx_.getUIState();
                if (!uiState || !uiState->pendingDAWRestore) return;

                juce::Logger::writeToLog("[RootComponent] state.restored: restoring page="
                    + juce::String(uiState->pendingPage) + " folder=" + uiState->pendingFolder);

                // SAMPLAR: browser_ removed - pendingLibraryMode/pendingFolder restore skipped

                // S-Mode sessions are restored by the processor (SModeService,
                // B9); this editor reacts to "smode.session.restoring" /
                // "smode.workflow.ready" like any other load.
                switchTo(uiState->pendingPage);

                uiState->pendingDAWRestore = false;  // consumed
            });
        }
    }

    // SAMPLAR: switchTo() is a no-op stub — SModePage is always the permanent main content.
    // Kept as a function to avoid touching all the callsites (state-restore blocks, etc.)
    void switchTo(int index)
    {
        juce::Logger::writeToLog("[Navigation] switchTo(" + juce::String(index) + ") called - no-op in SAMPLAR (SModePage is permanent)");
        // Update tracking state so DAW state-save still works
        if (index != currentPage_)
        {
            previousPage_ = currentPage_;
            currentPage_ = index;
        }
        if (uiStateWriteEnabled_)
        {
            if (auto* uiState = ctx_.getUIState())
            {
                uiState->currentPage = currentPage_;
                uiState->previousPage = previousPage_;
                uiState->loadedFolder = juce::String();
                uiState->libraryMode = false;
                uiState->libraryFolder = juce::String();
                uiState->hasSavedState = true;
            }
        }
    }

    // FileDragAndDropTarget methods - forward to InitialPage when active
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        // SAMPLAR: SModePage is always the main content - forward drag-drop to it
        juce::Logger::writeToLog("[RootComponent] isInterestedInFileDrag - checking SModePage");
        // Accept audio files for separation; SModePage will call onRequestSeparation
        for (const auto& f : files)
        {
            auto ext = juce::File(f).getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".aif" || ext == ".ogg")
                return true;
        }
        return false;
    }

    void fileDragEnter(const juce::StringArray& /*files*/, int /*x*/, int /*y*/) override
    {
        juce::Logger::writeToLog("[RootComponent] fileDragEnter - SModePage is target");
    }

    void fileDragExit(const juce::StringArray& /*files*/) override
    {
        juce::Logger::writeToLog("[RootComponent] fileDragExit");
    }

    void filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) override
    {
        juce::Logger::writeToLog("[RootComponent] filesDropped on SModePage: " + juce::String(files.size()) + " file(s)");
        if (files.isEmpty()) return;
        juce::File f(files[0]);
        if (f.existsAsFile() && smodePage_.onRequestSeparation)
            smodePage_.onRequestSeparation(f);
    }

    // When an internal JUCE drag exits the plugin window, convert to OS-level file drag.
    // This is non-blocking (JUCE handles the OLE loop safely) unlike performExternalDragDropOfFiles.
    bool shouldDropFilesWhenDraggedExternally(const DragAndDropTarget::SourceDetails& details,
                                               juce::StringArray& files, bool& canMoveFiles) override
    {
        if (auto* obj = details.description.getDynamicObject())
        {
            auto filePath = obj->getProperty("file").toString();
            if (filePath.isNotEmpty())
            {
                files.add(filePath);
                canMoveFiles = false;
                juce::Logger::writeToLog("[RootComponent] External drag: " + filePath);
                return true;
            }
        }
        return false;
    }

    void timerCallback() override
    {
        f1Count_ = 0;
        stopTimer();
    }

    void showUpdateDialog()
    {
        juce::Logger::writeToLog("[RootComponent] === showUpdateDialog START ===");
        if (auto* registry = ctx_.getRegistry()) {
            auto* updateSvc = dynamic_cast<samplar::UpdateCheckService*>(
                registry->get("update_checker"));

            auto currentVersion = samplar::ServiceLocator::getAppVersion();

            if (!updateSvc || !updateSvc->getUpdateInfo().updateAvailable) {
                // No update available - show simple version info dialog
                juce::Logger::writeToLog("[RootComponent] No update - showing version info");
                auto options = juce::MessageBoxOptions()
                    .withTitle("SAMPLAR")
                    .withMessage("You are running v" + currentVersion + "\n\nYou're up to date!")
                    .withButton("OK")
                    .withIconType(juce::MessageBoxIconType::InfoIcon);
                juce::AlertWindow::showAsync(options, nullptr);
                return;
            }

            updateDialog_ = std::make_unique<samplar::UpdateDialog>(
                updateSvc->getUpdateInfo(), updateSvc->getLicenseReminder());

            if (!updateSvc->getUpdateInfo().updateAvailable
                && !updateSvc->getLicenseReminder().message.empty()) {
                updateDialog_->setLicenseOnlyMode(true);
            }

            updateDialog_->onUpdateNow = [this, updateSvc]() {
                juce::Logger::writeToLog("[RootComponent] Update Now action");
                auto url = updateSvc->getUpdateInfo().downloadUrl;
                if (!url.empty())
                    juce::URL(juce::String(url)).launchInDefaultBrowser();
                updateSvc->dismissVersion(updateSvc->getUpdateInfo().latestVersion);
                updateDialog_.reset();
                notifBtn_.setMode(samplar::NotificationButton::Mode::UpToDate);
            };

            updateDialog_->onDismiss = [this, updateSvc]() {
                juce::Logger::writeToLog("[RootComponent] Update dismissed");
                updateSvc->dismissVersion(updateSvc->getUpdateInfo().latestVersion);
                updateDialog_.reset();
            };

            addAndMakeVisible(updateDialog_.get());
            updateDialog_->setCentrePosition(getWidth() / 2, getHeight() / 2);
            juce::Logger::writeToLog("[RootComponent] UpdateDialog shown");
        }
    }

    void handleUpdateAvailable(const juce::var& /*payload*/)
    {
        juce::Logger::writeToLog("[RootComponent] === handleUpdateAvailable ===");
        if (auto* registry = ctx_.getRegistry()) {
            auto* updateSvc = dynamic_cast<samplar::UpdateCheckService*>(
                registry->get("update_checker"));
            if (!updateSvc) return;

            auto latest = updateSvc->getUpdateInfo().latestVersion;
            notifBtn_.setMode(samplar::NotificationButton::Mode::UpdateAvailable);
            resized();

            if (!updateSvc->wasDismissed(latest)) {
                juce::Logger::writeToLog("[RootComponent] First time seeing v" + juce::String(latest) + " - showing dialog");
                showUpdateDialog();
            } else {
                juce::Logger::writeToLog("[RootComponent] v" + juce::String(latest) + " was dismissed - button only");
            }
        }
    }

    void handleLicenseReminder(const juce::var& payload)
    {
        juce::Logger::writeToLog("[RootComponent] === handleLicenseReminder ===");
        auto message = payload.getProperty("message", "").toString();
        if (message.isEmpty()) return;

        auto status = payload.getProperty("status", "").toString();
        auto daysRemaining = (int)payload.getProperty("daysRemaining", -1);

        juce::Logger::writeToLog("[RootComponent] License: status=" + status
            + " days=" + juce::String(daysRemaining) + " msg=" + message);

        if ((status == "trial" || status == "expired") && daysRemaining <= 7) {
            if (notifBtn_.getMode() != samplar::NotificationButton::Mode::UpdateAvailable) {
                notifBtn_.setMode(samplar::NotificationButton::Mode::TrialExpiring);
                resized();
            }
        }
    }

    // Nav model that calls back on selection
    struct NavModel : public ListBoxModel {
        RootComponent& owner;
        // SAMPLAR: Preview(1), Browser(3), Editor(4), Engine Lab(5), JUCE Editor(6) removed
        StringArray items { "Initial", "[removed]", "Processing", "[removed]", "[removed]", "[removed]", "[removed]", "Community", "Mentor", "S-Mode" };
        explicit NavModel(RootComponent& o) : owner(o) {}
        int getNumRows() override { return items.size(); }
        void paintListBoxItem(int row, Graphics& g, int w, int h, bool sel) override {
            g.fillAll(sel ? Colours::darkgrey : Colours::transparentBlack);
            g.setColour(Colours::white);
            if (isPositiveAndBelow(row, items.size()))
                g.drawText(items[row], 6, 0, w-12, h, Justification::centredLeft);
        }
        void selectedRowsChanged(int lastRowSelected) override { owner.switchTo(lastRowSelected); }
    } navModel_ { *this };

private:
    // SAMPLAR: pages_ container removed — SModePage is the permanent main content.
    // nav_ kept as debug-only sidebar (hidden unless F1×10 pressed).
    ListBox nav_ { "Pages" };
    std::unique_ptr<LicenseStatusWidget> licenseStatusWidget_;
    // This editor's own services, captured once: the editor claims the locator
    // for its processor right before building the UI (review A2).
    samplar::ServiceLocator::Context ctx_ { samplar::ServiceLocator::capture() };
    template <typename Page>
    static std::unique_ptr<Page> makeSModePage(const samplar::ServiceLocator::Context& ctx)
    {
        if constexpr (std::is_constructible_v<Page, samplar::EventBus&, const samplar::ServiceLocator::Context&>)
            return std::make_unique<Page>(*ctx.getBus(), ctx);
        else
            return std::make_unique<Page>(*ctx.getBus());
    }
    std::unique_ptr<samplar::SModePage> smodePagePtr_ { makeSModePage<samplar::SModePage>(ctx_) };
    samplar::SModePage& smodePage_ { *smodePagePtr_ };
    // Debug console
    DebugConsole debugConsole_;
    bool debugVisible_ { false };
    bool isPlugin_ { false };          // plugin: settings + notification only
    juce::String lastResizeLog_;
    int f1Count_ { 0 };
    const int resetMs_ { 4000 };
    samplar::SamplarLookAndFeel lnf_;
    // Tooltip window — provides the surface JUCE renders tooltip text on.
    // Without this, every setTooltip(...) call in the tree is dormant.
    // 600 ms hover delay before showing.
    juce::TooltipWindow tooltipWindow_ { this, 600 };
    // Window control buttons (macOS-style traffic lights + settings)
    ExitButton exitBtn_;
    PinButton pinBtn_;
    SettingsButton settingsBtn_;
    DragButton dragBtn_;
    samplar::NotificationButton notifBtn_;
    std::unique_ptr<samplar::UpdateDialog> updateDialog_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    // Gates whether switchTo() writes the current page/folder back to uiState.
    // Kept false during construction so the initial switchTo(0) doesn't clobber
    // the saved state before the restore block reads it. Flipped to true by the
    // restore-async lambda AFTER the saved page has been applied.
    bool uiStateWriteEnabled_ { false };
    // SAMPLAR: processMode_ removed with InitialPage; SModePage always runs S-Mode separation
    int previousPage_ { 0 };  // Track previous page for back navigation
    int currentPage_ { 0 };   // Track current page

    // S-Mode service (owned by SamplarAudioProcessor - raw pointer)
    samplar::SModeService* smodeService_ { nullptr };
    samplar::EventBus::HandlerId smodeWorkflowReadySub_ { 0 };
    samplar::EventBus::HandlerId smodeWorkflowErrorSub_ { 0 };
    samplar::EventBus::HandlerId smodeProgressSub_ { 0 };
    samplar::EventBus::HandlerId smodeRestoringSub_ { 0 };
    samplar::EventBus::HandlerId smodeMissingSub_ { 0 };
    samplar::EventBus* smodeSubsBus_ { nullptr };
    int editorAckSeq_ { 0 };
    uint64_t shownGeneration_ { ~(uint64_t) 0 };   // model generation the page last showed (B11)
    juce::File pendingSeparationFile_;   // song dropped while another was separating
    samplar::EventBus::HandlerId stateRestoredSub_ { 0 };
    samplar::EventBus::HandlerId updateAvailableSub_ { 0 };
    samplar::EventBus::HandlerId licenseReminderSub_ { 0 };
};

MainWindow::MainWindow(const String& name)
    : DocumentWindow(name,
                     Desktop::getInstance().getDefaultLookAndFeel()
                         .findColour(ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    juce::Logger::writeToLog("???? [MainWindow] Constructor ENTER");

    try {
        juce::Logger::writeToLog("???? [MainWindow] Setting window properties");
        setUsingNativeTitleBar(false); // borderless
        juce::Logger::writeToLog("???? [MainWindow] Native title bar disabled");

        setTitleBarHeight(0);
        juce::Logger::writeToLog("???? [MainWindow] Title bar height set to 0");

        setResizable(true, true);
        setResizeLimits(850, 500, 1810, 1130);   // same limits as the plugin editor
        juce::Logger::writeToLog("???? [MainWindow] Window set to resizable (850x500 .. 1810x1130)");

        juce::Logger::writeToLog("???? [MainWindow] Creating RootComponent");
        auto* rc = new RootComponent();
        juce::Logger::writeToLog("???? [MainWindow] RootComponent created");

        juce::Logger::writeToLog("???? [MainWindow] About to set content owned WITHOUT sizing first");
        setContentOwned(rc, true);
        juce::Logger::writeToLog("???? [MainWindow] RootComponent set as content");

        juce::Logger::writeToLog("???? [MainWindow] About to set window size");
        setSize(1060, 724);
        juce::Logger::writeToLog("???? [MainWindow] Window size set to 1060x724");

        juce::Logger::writeToLog("???? [MainWindow] About to attach EventBus");
        if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
            root->attachBus();
        juce::Logger::writeToLog("???? [MainWindow] EventBus attached");

        juce::Logger::writeToLog("???? [MainWindow] About to center window");
        centreWithSize(1060, 724);
        juce::Logger::writeToLog("???? [MainWindow] Window centered");

        juce::Logger::writeToLog("???? [MainWindow] About to make window visible");
        setVisible(true);
        juce::Logger::writeToLog("???? [MainWindow] Window made visible");

        juce::Logger::writeToLog("???? [MainWindow] Constructor SUCCESS");
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog("???? [MainWindow] EXCEPTION in constructor: " + juce::String(e.what()));
        throw;
    }
    catch (...) {
        juce::Logger::writeToLog("???? [MainWindow] UNKNOWN EXCEPTION in constructor");
        throw;
    }
}

void MainWindow::closeButtonPressed()
{
    samplar::ServiceLocator::requestQuit();
}

// FileDragAndDropTarget methods - forward to RootComponent content
bool MainWindow::isInterestedInFileDrag(const juce::StringArray& files)
{
    juce::Logger::writeToLog("[MainWindow] isInterestedInFileDrag called - forwarding to RootComponent");
    if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
    {
        return root->isInterestedInFileDrag(files);
    }
    return false;
}

void MainWindow::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::Logger::writeToLog("[MainWindow] fileDragEnter called - forwarding to RootComponent");
    if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
    {
        root->fileDragEnter(files, x, y);
    }
}

void MainWindow::fileDragExit(const juce::StringArray& files)
{
    juce::Logger::writeToLog("[MainWindow] fileDragExit called - forwarding to RootComponent");
    if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
    {
        root->fileDragExit(files);
    }
}

void MainWindow::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::Logger::writeToLog("[MainWindow] filesDropped called - forwarding to RootComponent");
    if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
    {
        root->filesDropped(files, x, y);
    }
}

// Factory: used by SamplarAudioProcessorEditor to create the same UI
std::unique_ptr<juce::Component> createRootComponent()
{
    auto root = std::make_unique<RootComponent>();
    root->attachBus();
    return root;
}

} // namespace samplar



