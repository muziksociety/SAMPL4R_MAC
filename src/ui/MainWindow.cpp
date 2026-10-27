#include "MainWindow.h"
#include "../app/App.h"
#include "../app/ServiceLocator.h"
#include "../app/ServiceRegistry.h"
#include "../services/ProcessingCoordinatorService.h"
#include "../services/FullSongService.h"
#include "../services/FullSongSeparationService.h"
// SAMPLAR: removed - WaveEditorView deleted
// SAMPLAR: removed - InitialPage deleted (SModePage is now main content)
#include "Pages/ProcessingPage.h"
// SAMPLAR: removed - WavePreviewPage, EngineLabPage, CommunityPage deleted
// SAMPLAR: removed - MentorPage and MentorService deleted
#include "Pages/SModePage.h"
#include "Components/smode/StemCountDialog.h"
#include "../services/smode/SModeService.h"
#include "../services/MidiMappingService.h"
#include "licensing/LicenseStatusWidget.h"
#include "Debug/DebugConsole.h"
#include "../app/MultiLogger.h"
#include "../utils/LogPaths.h"
#include "Style/Sm5LookAndFeel.h"
#include "Style/SamplarLookAndFeel.h"
#include "Debug/DebugFlags.h"
#include "Components/SettingsDialog.h"
#include "Components/NotificationButton.h"
#include "Components/UpdateDialog.h"
#include "../services/UpdateCheckService.h"
#include <thread>


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
        // Find MainWindow and call its closeButtonPressed (which handles batch processing check)
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

// NudgeTimer kept as stub (no longer used for mentor nudge, reserved for future use)
class NudgeTimer : public juce::Timer {
public:
    std::function<void()> onTick;
    void timerCallback() override { if (onTick) onTick(); }
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

            // ProcessingPage: kept as hidden child (shown as overlay during separation)
            addChildComponent(processing_);
            juce::Logger::writeToLog("???? [RootComponent] ProcessingPage added as hidden overlay");

            // SAMPLAR: InitialPage, CommunityPage, MentorPage removed entirely
            juce::Logger::writeToLog("[RootComponent] Nav placeholder only (InitialPage/CommunityPage/MentorPage removed)");

            // Subscribe to ui.navigate so MentorHelperButton (and others) can route to mentor page
            if (auto* bus = samplar::ServiceLocator::getBus())
            {
                // SAMPLAR: ui.navigate mentor/back routing removed (no page stack)
                juce::Logger::writeToLog("[RootComponent] EventBus: ui.navigate mentor routing removed");

                // -- FullSong separation: live progress / completion / error --
                // The service lives on the AudioProcessor's ServiceRegistry, so
                // closing the VST3 wrapper window mid-separation does NOT lose
                // the work. Reopening reconstructs MainWindow, re-subscribes,
                // and (further down) resumes the UI by querying service state.
                fullSongProgSub_ = bus->subscribe("fullSong.separation.progress",
                    [this, alive = alive_](const juce::var& v) {
                        if (!*alive) return;
                        const int pct = (int) v;
                        juce::MessageManager::callAsync([this, alive, pct]() {
                            if (!*alive) return;
                            processing_.setProgress(static_cast<double>(pct));
                        });
                    });
                fullSongMsgSub_ = bus->subscribe("fullSong.separation.message",
                    [this, alive = alive_](const juce::var& v) {
                        if (!*alive) return;
                        const juce::String msg = v.toString();
                        juce::MessageManager::callAsync([this, alive, msg]() {
                            if (!*alive) return;
                            processing_.setStatusMessage(msg);
                            // Refresh batch indicator while running
                            if (fullSongSepService_ && fullSongSepService_->isBatch()) {
                                const int idx   = fullSongSepService_->getBatchFileIndex();
                                const int count = fullSongSepService_->getBatchFileCount();
                                processing_.setBatchInfo(idx + 1, count);
                            }
                        });
                    });
                fullSongCompleteSub_ = bus->subscribe("fullSong.separation.complete",
                    [this, alive = alive_](const juce::var& v) {
                        if (!*alive) return;
                        const juce::String stemPath = v.toString();
                        juce::Logger::writeToLog("[MainWindow] FullSong separation COMPLETE: " + stemPath);
                        juce::MessageManager::callAsync([this, alive, stemPath]() {
                            if (!*alive) return;

                            // Drain the result from the service
                            samplar::FullSongSeparationService* svc = nullptr;
                            if (auto* reg = samplar::ServiceLocator::getRegistry())
                                svc = dynamic_cast<samplar::FullSongSeparationService*>(reg->get("fullSong.separation"));
                            if (svc) (void) svc->consumeResult();

                            separationInProgress_ = false;
                            separationAlreadyDone_ = true;
                            // Hide ProcessingPage overlay - SModePage is always the background
                            processing_.setVisible(false);
                        });
                    });
                fullSongBatchCompleteSub_ = bus->subscribe("fullSong.separation.batch.complete",
                    [this, alive = alive_](const juce::var& v) {
                        if (!*alive) return;
                        std::vector<juce::File> stems;
                        if (v.isArray())
                            for (int i = 0; i < v.size(); ++i)
                                stems.emplace_back(v[i].toString());
                        juce::Logger::writeToLog("[MainWindow] FullSong BATCH COMPLETE: " + juce::String(static_cast<int>(stems.size())) + " stems");
                        juce::MessageManager::callAsync([this, alive, stems]() {
                            if (!*alive) return;
                            samplar::FullSongSeparationService* svc = nullptr;
                            if (auto* reg = samplar::ServiceLocator::getRegistry())
                                svc = dynamic_cast<samplar::FullSongSeparationService*>(reg->get("fullSong.separation"));
                            if (svc) (void) svc->consumeBatchResult();

                            separationInProgress_     = false;
                            batchSeparationActive_    = false;
                            separationAlreadyDone_    = true;
                            batchDrumStemFiles_       = stems;
                            if (stems.empty())
                            {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::AlertWindow::WarningIcon,
                                    "Batch Separation Failed",
                                    "Could not extract drums from any of the songs.",
                                    "OK");
                                processing_.setVisible(false);
                                return;
                            }
                            // SAMPLAR: preview_ removed - WavePreviewPage deleted
                            processing_.setVisible(false); // hide overlay, SModePage is always visible
                        });
                    });
                fullSongErrorSub_ = bus->subscribe("fullSong.separation.error",
                    [this, alive = alive_](const juce::var& v) {
                        if (!*alive) return;
                        const juce::String err = v.toString();
                        juce::Logger::writeToLog("[MainWindow] FullSong separation FAILED: " + err);
                        juce::MessageManager::callAsync([this, alive, err]() {
                            if (!*alive) return;
                            separationInProgress_ = false;
                            processing_.setVisible(false);
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Separation Failed",
                                "Could not extract drums from the song:\n" + err,
                                "OK");
                        });
                    });
            }

            // -- FullSong separation: RESUME on editor reopen --
            // If a separation is in flight (or finished while editor was closed),
            // wire the UI to it directly so the producer sees live progress
            // (or lands on WavePreviewPage with the drum stem already loaded).
            if (auto* reg = samplar::ServiceLocator::getRegistry())
            {
                auto* svc = dynamic_cast<samplar::FullSongSeparationService*>(reg->get("fullSong.separation"));
                if (svc)
                {
                    fullSongSepService_ = svc;
                    if (svc->isSeparating())
                    {
                        const bool batch = svc->isBatch();
                        juce::Logger::writeToLog(juce::String("[MainWindow] RESUME: FullSong separation in progress")
                            + (batch ? " (batch)" : " (single)"));
                        processing_.resetForNewJob();
                        processing_.setInputFile(svc->getCurrentSourceFile());
                        processing_.setProcessMode(2); // FullSong
                        if (batch)
                        {
                            const int idx   = svc->getBatchFileIndex();
                            const int count = svc->getBatchFileCount();
                            processing_.setBatchInfo(idx + 1, count);
                            batchSeparationActive_ = true;
                            batchFilesToSeparate_   = svc->getCurrentBatchSources();
                        }
                        processing_.setProgress(static_cast<double>(svc->getCurrentProgress()));
                        processing_.setStatusMessage(svc->getCurrentMessage());
                        separationInProgress_ = true;
                        juce::MessageManager::callAsync([this, alive = alive_]() {
                            if (!*alive) return;
                            // Show ProcessingPage as overlay during resumed separation
                            processing_.setVisible(true);
                            processing_.toFront(false);
                        });
                    }
                    else if (svc->hasUnconsumedBatchResult())
                    {
                        juce::Logger::writeToLog("[MainWindow] RESUME: FullSong BATCH completed while editor closed - loading stems");
                        auto stems = svc->consumeBatchResult();
                        if (! stems.empty())
                        {
                            separationAlreadyDone_  = true;
                            batchDrumStemFiles_     = stems;
                            // Nothing to navigate - SModePage is always visible
                        }
                    }
                    else if (svc->hasUnconsumedResult())
                    {
                        juce::Logger::writeToLog("[MainWindow] RESUME: FullSong separation completed while editor closed - loading drum stem");
                        auto res = svc->consumeResult();
                        if (res.isValid())
                        {
                            separationAlreadyDone_ = true;
                            // Nothing to navigate - SModePage is always visible
                        }
                    }
                }
            }

            // SAMPLAR: SModePage is now the permanent main content (added in constructor above).
            // onBack callback kept for MIDI cleanup but no longer switches pages.
            smodePage_.onBack = [this]{
                // Fully reset S-Mode: disconnect MIDI, clear model, reset service
                if (auto* reg = samplar::ServiceLocator::getRegistry()) {
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
            if (auto* reg = samplar::ServiceLocator::getRegistry())
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
                        juce::Logger::writeToLog("[RootComponent] S-Mode already active with audio - restoring UI");
                        smodeService_ = smodeSvc;
                        smodePage_.onStemsReady(*smodeService_);
                        smodeRestored = true;
                    }
                }
            }

            // SModePage is always visible — no page switch needed regardless of smodeRestored
            juce::Logger::writeToLog("[RootComponent] SModePage is permanent main content (smodeRestored=" + juce::String(smodeRestored ? "true" : "false") + ")");

            if (auto* registry = samplar::ServiceLocator::getRegistry())
            {
                auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"));
                licenseStatusWidget_ = std::make_unique<LicenseStatusWidget>(licenseSvc, samplar::ServiceLocator::getBus());
                if (licenseStatusWidget_ != nullptr)
                {
                    addAndMakeVisible(licenseStatusWidget_.get());
                    licenseStatusWidget_->setVisible(nav_.isVisible());
                }
            }
            juce::Logger::writeToLog("[RootComponent] Constructor: SModePage visible, ready");

            juce::Logger::writeToLog("???? [RootComponent] Adding window control buttons");
            bool isPlugin = (samplar::ServiceLocator::getUIState() != nullptr);
            if (!isPlugin)
            {
                addAndMakeVisible(exitBtn_);
                addAndMakeVisible(pinBtn_);
                addAndMakeVisible(dragBtn_);
            }
            addAndMakeVisible(settingsBtn_);

            addAndMakeVisible(notifBtn_); // always visible for version info

            // Handle notification button click - show update dialog
            notifBtn_.onClick = [this]() {
                juce::Logger::writeToLog("[RootComponent] NotificationButton clicked");
                showUpdateDialog();
            };

            // Subscribe to update events from UpdateCheckService
            if (auto* bus = samplar::ServiceLocator::getBus()) {
                bus->subscribe("update.available", [this](const juce::var& payload) {
                    juce::Logger::writeToLog("[RootComponent] Received update.available event");
                    handleUpdateAvailable(payload);
                });
                bus->subscribe("license.status.reminder", [this](const juce::var& payload) {
                    juce::Logger::writeToLog("[RootComponent] Received license.status.reminder event");
                    handleLicenseReminder(payload);
                });
            }

            juce::Logger::writeToLog("???? [RootComponent] Setting up debug console and MultiLogger");
            addChildComponent(debugConsole_);
            debugConsole_.setVisible(debugVisible_);
            {
                juce::File logsDir = samplar::getLogsDirectory();
                juce::File logFile = logsDir.getChildFile("SAMPLAR_console.log");
                // Use a mirrored logger that writes to file and UI console (MultiLogger ensures directory exists)
                // Log dir is %APPDATA%\Samplar\logs for Program Files, or <exe>/logs for portable
                multiLogger_ = std::make_unique<samplar::MultiLogger>(debugConsole_, logFile);
                juce::Logger::setCurrentLogger(multiLogger_.get());
                juce::Logger::writeToLog("[MainWindow] MultiLogger set. Log file: " + logFile.getFullPathName());
                // SAMPLAR: browser_ removed - SampleBrowserPage deleted
                // browser_.logMLStatus();
            }

            juce::Logger::writeToLog("[RootComponent] Setting up SModePage separation callbacks");

            // === SMODE SEPARATION FLOW (triggered from SModePage via drag-drop or menu) ===
            // SModePage calls onRequestSeparation when the user drops a song file.
            // Shows StemCountDialog, then runs SModeService workflow with ProcessingPage
            // shown as an overlay while separation runs.
            smodePage_.onRequestSeparation = [this, alive = alive_](const juce::File& f) {
                if (!*alive) return;
                juce::Logger::writeToLog("[MainWindow] SModePage requested separation for: " + f.getFullPathName());

                // Guard: if separation is already running, show a toast and bail out.
                if (smodeService_ && smodeService_->getSeparation().isSeparating())
                {
                    juce::Logger::writeToLog("[MainWindow] Ignoring Load Audio — separation already in progress");
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                            .withTitle("Separation in progress")
                            .withMessage("A stem separation is already running.\nPlease wait for it to finish before loading a new file.")
                            .withButton("OK"),
                        nullptr);
                    return;
                }
                // Also check via ServiceLocator in case smodeService_ hasn't been cached yet
                if (!smodeService_) {
                    auto* reg = samplar::ServiceLocator::getRegistry();
                    if (reg) smodeService_ = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
                }
                if (smodeService_ && smodeService_->getSeparation().isSeparating())
                {
                    juce::Logger::writeToLog("[MainWindow] Ignoring Load Audio — separation already in progress (late check)");
                    juce::AlertWindow::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                            .withTitle("Separation in progress")
                            .withMessage("A stem separation is already running.\nPlease wait for it to finish before loading a new file.")
                            .withButton("OK"),
                        nullptr);
                    return;
                }

                samplar::StemCountDialog::show(this, [this, alive, f](int stemCount) {
                    if (!*alive) return;
                    juce::Logger::writeToLog("[MainWindow] S-Mode stem count selected: " + juce::String(stemCount));

                    if (!smodeService_) {
                        auto* reg = samplar::ServiceLocator::getRegistry();
                        if (reg)
                            smodeService_ = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
                    }
                    if (!smodeService_) {
                        juce::Logger::writeToLog("[MainWindow] ERROR: SModeService not registered");
                        return;
                    }

                    if (auto* bus = samplar::ServiceLocator::getBus()) {
                        smodeWorkflowReadySub_ = bus->subscribe("smode.workflow.ready", [this, alive](const juce::var&) {
                            if (!*alive) return;
                            juce::Logger::writeToLog("[MainWindow] S-Mode workflow ready - loading bars off, stems loading");
                            smodePage_.setStemLoadingState(false);
                            smodePage_.onStemsReady(*smodeService_);
                            processing_.setVisible(false);
                            smodePage_.resized();
                            smodePage_.repaint();
                            juce::MessageManager::callAsync([this]() {
                                smodePage_.resized();
                                smodePage_.repaint();
                            });
                            if (auto* b = samplar::ServiceLocator::getBus())
                                b->unsubscribe("smode.workflow.ready", smodeWorkflowReadySub_);
                        });

                        smodeWorkflowErrorSub_ = bus->subscribe("smode.workflow.error", [this, alive](const juce::var& err) {
                            if (!*alive) return;
                            juce::Logger::writeToLog("[MainWindow] S-Mode error: " + err.toString());
                            smodePage_.setStemLoadingState(false);
                            processing_.setVisible(false);
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "S-Mode Failed",
                                "Could not separate stems:\n" + err.toString(), "OK");
                            if (auto* b = samplar::ServiceLocator::getBus())
                                b->unsubscribe("smode.workflow.error", smodeWorkflowErrorSub_);
                        });

                        bus->subscribe("smode.separation.progress", [this, alive](const juce::var& pct) {
                            if (!*alive) return;
                            const int pctInt = static_cast<int>(pct);
                            processing_.setProgress(static_cast<double>(pctInt));
                            // Forward real progress to per-lane loading bars (0..100 → 0.0..1.0)
                            const float p01 = juce::jlimit(0.0f, 1.0f, static_cast<float>(pctInt) / 100.0f);
                            juce::Logger::writeToLog("[MainWindow] smode.separation.progress → " + juce::String(pctInt) + "% → setStemLoadingProgress");
                            smodePage_.setStemLoadingProgress(p01);
                        });
                    }

                    // FIX3: Show per-lane loading bars instead of the full-screen ProcessingPage overlay.
                    // ProcessingPage is suppressed (not shown) so SModePage stays fully visible.
                    processing_.resetForNewJob();
                    processing_.setInputFile(f);
                    processing_.setProcessMode(2);
                    // processing_.setVisible(true);  // suppressed — per-lane loading bars handle UX
                    smodePage_.setStemLoadingProgress(0.0f);  // reset bars to 0% before starting
                    smodePage_.beginSeparationVisuals(stemCount);  // pre-create N placeholder lanes
                    smodePage_.setStemLoadingState(true);

                    auto sc = (stemCount == 6)
                        ? samplar::SModeSeparationService::StemCount::Six
                        : samplar::SModeSeparationService::StemCount::Four;
                    smodeService_->startWorkflow(f, sc);
                });
            };

            // ProcessingPage back button: just hide overlay (SModePage always in background)
            processing_.onBack = [this]{ processing_.setVisible(false); };

                // Defer GIF preload so the editor window appears instantly.
        // Decoding 6-12 MB of GIF on the message thread blocks the open animation.
        // callAfterDelay runs on the message thread 300ms later � editor is visible by then.
        juce::Timer::callAfterDelay(300, [this, alive = alive_]() {
            if (!*alive) return;
            juce::Logger::writeToLog("[RootComponent] Deferred: preloading ProcessingPage GIF...");
            processing_.preloadResources();
            juce::Logger::writeToLog("[RootComponent] Deferred: ProcessingPage GIF preloaded");
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
            auto* reg = samplar::ServiceLocator::getRegistry();
            if (!reg) return;
            auto* smodeSvc = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
            if (smodeSvc && smodeSvc->isActive() && smodeSvc->getModel().getStemCount() > 0)
            {
                juce::Logger::writeToLog("[RootComponent] Reconnecting S-Mode UI to existing session ("
                                         + juce::String(smodeSvc->getModel().getStemCount()) + " stems)");
                smodeService_ = smodeSvc;
                smodePage_.onStemsReady(*smodeSvc);
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
        if (auto* uiState = samplar::ServiceLocator::getUIState())
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
        if (auto* bus = samplar::ServiceLocator::getBus())
        {
            if (mentorNavSub_ != 0) bus->unsubscribe("ui.navigate",         mentorNavSub_);
            if (waveEditorId_ != 0) bus->unsubscribe("wave_editor.open",   waveEditorId_);
            if (vizId_        != 0) bus->unsubscribe("processing.viz",      vizId_);
            if (progId_       != 0) bus->unsubscribe("processing.progress", progId_);
            if (resId_        != 0) bus->unsubscribe("processing.result",   resId_);
            // FullSong separation subs (do NOT cancel the in-flight separation �
            // it lives on the processor's service and continues across editor close).
            if (fullSongProgSub_     != 0) bus->unsubscribe("fullSong.separation.progress", fullSongProgSub_);
            if (fullSongMsgSub_      != 0) bus->unsubscribe("fullSong.separation.message",  fullSongMsgSub_);
            if (fullSongCompleteSub_ != 0) bus->unsubscribe("fullSong.separation.complete", fullSongCompleteSub_);
            if (fullSongErrorSub_    != 0) bus->unsubscribe("fullSong.separation.error",    fullSongErrorSub_);
            if (fullSongBatchCompleteSub_ != 0) bus->unsubscribe("fullSong.separation.batch.complete", fullSongBatchCompleteSub_);
        }
        juce::Logger::writeToLog("[RootComponent] Destructor complete");
        // CRITICAL: Must clear before multiLogger_ member is auto-destroyed.
        // multiLogger_ unique_ptr destructs after this body (reverse member order),
        // but juce::Logger still holds the raw pointer. Any log call during member
        // destruction (component children, timers, etc.) hits freed memory ? crash.
        juce::Logger::setCurrentLogger(nullptr);
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

        // SModePage fills the main area directly (no pages_ container needed, kept for debug nav)
        smodePage_.setBounds(main.reduced(4));
        // ProcessingPage overlay: same bounds as smodePage_ (shown on top during separation)
        processing_.setBounds(smodePage_.getBounds());
        // Window control buttons centered at top
        // Order (centered): Pin - Settings - Drag - Exit | (right corner): [Notification]
        auto top = smodePage_.getBounds().removeFromTop(28);
        const int btnSize = 20;
        const int btnGap = 6;
        const int centerBtns = 4; // pin, settings, drag, exit
        const int totalWidth = (btnSize * centerBtns) + (btnGap * (centerBtns - 1));
        int startX = top.getCentreX() - totalWidth / 2;
        int btnY = top.getCentreY() - btnSize / 2;
        int idx = 0;
        pinBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize); idx++;
        settingsBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize); idx++;
        dragBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize); idx++;
        exitBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize);
        // Notification button pinned to top-right corner
        notifBtn_.setBounds(top.getRight() - btnSize - 8, btnY, btnSize, btnSize);
        // SAMPLAR: initial_, community_, mentor_ removed - SModePage is the permanent content
        // processing_ bounds already set above as overlay
        juce::Logger::writeToLog("[RootComponent] resized: SModePage=" + smodePage_.getBounds().toString());
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
                samplar::DebugFlags::enabled().store(debugVisible_);
                nav_.setVisible(debugVisible_);
                debugConsole_.setVisible(debugVisible_);
                resized();
                return true;
            }
        }
        return Component::keyPressed(key);
    }

public:
    // Check if batch processing is active (prevents premature app closure)
    bool isBatchProcessingActive() const
    {
        return batchSeparationActive_.load() || batchFinalProcessingActive_.load();
    }

    // Hook EventBus progress/result to UI
    void attachBus()
    {
        if (auto* busPtr = samplar::ServiceLocator::getBus())
        {
            auto& bus = *busPtr;
            // SAMPLAR: wave_editor.open handler removed - juceEditor_ (WaveEditorView) deleted
            // waveEditorId_ stays 0 (unsubscribed)
            vizId_ = bus.subscribe("processing.viz", [this, alive = alive_](const juce::var& v){
                if (!*alive) return;
                juce::MessageManager::callAsync([this, alive, v]{
                    if (!*alive) return;
                    if (! v.isObject()) return;
                    auto* o = v.getDynamicObject();
                    if (! o->hasProperty("type")) return;
                    juce::String type = o->getProperty("type").toString();
                    if (type == "fft_info") {
                        juce::Logger::writeToLog("[UI] Viz FFT: win=" + juce::String((int) o->getProperty("win")) +
                                                   " hop=" + juce::String((int) o->getProperty("hop")) +
                                                   " frames=" + juce::String((int) o->getProperty("frames")));
                    } else if (type == "phase_start") {
                        juce::String phase = o->getProperty("phase").toString();
                        juce::Logger::writeToLog("[UI] ???? Starting phase: " + phase);

                        // Transition from monster animation to waveform when drum stem processing begins
                        if (phase == "mel_spectrogram" && false /* SAMPLAR: FullSong mode removed with InitialPage */) {
                            juce::Logger::writeToLog("???? [MainWindow] Drum stem processing starting (mel_spectrogram phase) - transitioning to waveform visualization");
                            processing_.startDrumStemVisualization();
                        }

                        // Enable zoom window for all processing phases
                        if (phase == "mel_spectrogram" || phase == "spectral_flux" || phase == "peak_detection" || phase == "supervision") {
                            processing_.enableZoomWindow(true);
                        }

                        // Start pre-loading Sample Browser during supervision phase
                        if (phase == "supervision") {
                            juce::Logger::writeToLog("[UI] ???? Pre-loading Sample Browser during supervision...");
                            // Trigger sample browser preparation in background
                            juce::Timer::callAfterDelay(100, [this]() {
                                // Pre-load sample browser - for now just trigger refresh
                                juce::Logger::writeToLog("[UI] ???? Pre-loading Sample Browser components");
                                // TODO: Add proper output directory access when available
                            });
                        }
                    } else if (type == "phase_complete") {
                        juce::String phase = o->getProperty("phase").toString();
                        juce::Logger::writeToLog("[UI] ??? Completed phase: " + phase);
                        // Clear temporary visualizations for next phase
                        processing_.clearVisualizationForNewPhase();
                        // Clear phase progress indicator
                        processing_.setPhaseProgress("", 0.0f);

                        // If all processing is complete, switch to Sample Browser
                        if (phase == "supervision") {
                            juce::Logger::writeToLog("[UI] ???? All processing complete - switching to Sample Browser");
                            juce::Timer::callAfterDelay(1000, [this]() {
                                processing_.enableZoomWindow(false); // Disable zoom window
                                switchTo(3); // Switch to Sample Browser (index 3)
                            });
                        }
                    } else if (type == "mel_progress") {
                        int frame = (int) o->getProperty("frame");
                        int total = (int) o->getProperty("total_frames");
                        float progress = (float) o->getProperty("progress");
                        juce::Logger::writeToLog("[UI] ???? Mel spectrogram: " + juce::String(frame) + "/" + juce::String(total) + " (" + juce::String(progress * 100, 1) + "%)");
                        processing_.setPhaseProgress("Mel Spectrogram", progress);

                        // Update zoom window position based on analysis progress
                        if (o->hasProperty("current_time_seconds")) {
                            double timeSeconds = (double) o->getProperty("current_time_seconds");
                            processing_.setAnalysisPosition(timeSeconds);
                        }
                    } else if (type == "flux_progress") {
                        int frame = (int) o->getProperty("frame");
                        int total = (int) o->getProperty("total_frames");
                        float progress = (float) o->getProperty("progress");
                        juce::Logger::writeToLog("[UI] ???? Spectral flux: " + juce::String(frame) + "/" + juce::String(total) + " (" + juce::String(progress * 100, 1) + "%)");
                        processing_.setPhaseProgress("Spectral Flux", progress);

                        // Update zoom window position based on analysis progress
                        if (o->hasProperty("current_time_seconds")) {
                            double timeSeconds = (double) o->getProperty("current_time_seconds");
                            processing_.setAnalysisPosition(timeSeconds);
                        }
                    } else if (type == "supervision_progress") {
                        int onsetIndex = (int) o->getProperty("onset_index");
                        int totalOnsets = (int) o->getProperty("total_onsets");
                        float progress = (float) o->getProperty("progress");
                        juce::Logger::writeToLog("[UI] ???? Supervision: " + juce::String(onsetIndex) + "/" + juce::String(totalOnsets) + " (" + juce::String(progress * 100, 1) + "%)");
                        processing_.setPhaseProgress("Supervision", progress);

                        // Update zoom window position based on supervision progress
                        if (o->hasProperty("current_time_seconds")) {
                            double timeSeconds = (double) o->getProperty("current_time_seconds");
                            processing_.setAnalysisPosition(timeSeconds);
                        }
                    } else if (type == "peak_detected") {
                        int samplePos = (int) o->getProperty("sample_position");
                        int peakCount = (int) o->getProperty("peak_count");
                        juce::Logger::writeToLog("[UI] ???? Peak #" + juce::String(peakCount) + " detected at sample " + juce::String(samplePos));
                        processing_.addSlicePosition(samplePos);

                        // Update zoom window to follow peak detection
                        if (o->hasProperty("onset_time")) {
                            double timeSeconds = (double) o->getProperty("onset_time");
                            processing_.setAnalysisPosition(timeSeconds);
                        }
                        // Force immediate UI update for this peak
                        repaint();
                    } else if (type == "onset_supervised") {
                        int samplePos = (int) o->getProperty("sample_position");
                        int processed = (int) o->getProperty("processed_count");
                        int total = (int) o->getProperty("total_input");
                        juce::Logger::writeToLog("[UI] ???? Supervised onset " + juce::String(processed) + "/" + juce::String(total) + " at sample " + juce::String(samplePos));

                        // Mark this slice as supervised (turn it green)
                        processing_.markSliceSupervised(samplePos);

                        // Update zoom window to follow supervision process
                        if (o->hasProperty("onset_time")) {
                            double timeSeconds = (double) o->getProperty("onset_time");
                            processing_.setAnalysisPosition(timeSeconds);
                        }
                    } else if (type == "flux_chunk") {
                        // Forward chunk to Processing page overlay
                        juce::Array<juce::var> vals = *o->getProperty("values").getArray();
                        std::vector<float> vvec(vals.size());
                        for (int i = 0; i < vals.size(); ++i) vvec[i] = (float) vals[i];
                        int offset = (int) o->getProperty("offset");
                        processing_.appendFluxChunk(offset, vvec);
                    } else if (type == "flux_stats") {
                        float mn = (float) o->getProperty("min");
                        float mx = (float) o->getProperty("max");
                        float me = (float) o->getProperty("mean");
                        processing_.setFluxStats(mn, mx, me);
                    } else if (type == "slices") {
                        juce::Logger::writeToLog("[UI] Received final slices event!");
                        if (o->hasProperty("positions")) {
                            juce::Array<juce::var> vals = *o->getProperty("positions").getArray();
                            juce::Logger::writeToLog("[UI] Final slice positions count: " + juce::String(vals.size()));
                        }
                    }
                });
            });

            progId_ = bus.subscribe("processing.progress", [this, alive = alive_](const juce::var& v){
                if (!*alive) return;
                juce::MessageManager::callAsync([this, alive, v]{
                    if (!*alive) return;
                    if (v.isObject())
                    {
                        auto* o = v.getDynamicObject();
                        if (o->hasProperty("pct"))
                        {
                            double pct = (double) o->getProperty("pct");
                            processing_.setProgress(pct);

                            // Update status message for ALL modes
                            if (o->hasProperty("msg"))
                            {
                                juce::String msg = o->getProperty("msg").toString();
                                // Send status message to ProcessingPage for display
                                processing_.setStatusMessage(msg);
                            }

                            // Update BPM display when detected (tempo-first pipeline)
                            if (o->hasProperty("bpm"))
                            {
                                float bpm = static_cast<float>((double)o->getProperty("bpm"));
                                processing_.setBPM(bpm);
                            }

                            // EPIC SAMPLAR Animation Control (Full Song mode specific)
                            if (o->hasProperty("msg"))
                            {
                                juce::String msg = o->getProperty("msg").toString();

                                // Start animation when Full Song processing begins (backup trigger)
                                if (msg == "Queued" && false /* SAMPLAR: FullSong mode removed with InitialPage */)
                                {
                                    juce::Logger::writeToLog("???? [MainWindow] Backup trigger - ensuring SAMPLAR animation is running for Full Song processing!");
                                    processing_.startFullSongAnimation();
                                }
                                // Transition to drum stem visualization ONLY when drum-stem phase truly begins
                                else if ((msg.containsIgnoreCase("Detecting drum samples") || pct >= 80) && false /* SAMPLAR: FullSong mode removed with InitialPage */)
                                {
                                    juce::Logger::writeToLog("???? [MainWindow] Transitioning from monster animation to drum stem waveform visualization (confirmed)");
                                    processing_.startDrumStemVisualization();
                                }
                            }
                        }
                    }
                });
            });
            resId_ = bus.subscribe("processing.result", [this, alive = alive_](const juce::var& v){
                if (!*alive) return;
                juce::Logger::writeToLog("???? [TRACE] Processing result received - scheduling UI update");
                // already subscribed to processing.viz above
                juce::MessageManager::callAsync([this, alive, v]{
                    if (!*alive) return;
                    juce::Logger::writeToLog("???? [TRACE] Processing result UI update executing");

                    // ???? Ensure SAMPLAR animation is stopped when processing completes
                    if (false /* SAMPLAR: FullSong mode removed with InitialPage */)
                    {
                        juce::Logger::writeToLog("???? [MainWindow] Processing complete - ensuring SAMPLAR animation is stopped");
                        processing_.stopFullSongAnimation();
                    }

                    juce::String outDir;
                    double detectedBPM = 0.0;
                    if (v.isObject())
                    {
                        auto* o = v.getDynamicObject();
                        outDir = o->getProperty("outputDir").toString();
                        juce::Logger::writeToLog("???? [TRACE] Extracted output directory: " + outDir);

                        // Extract BPM from result if available
                        if (o->hasProperty("bpm"))
                        {
                            detectedBPM = (double)o->getProperty("bpm");
                        }
                    } else {
                        juce::Logger::writeToLog("???? [TRACE] ERROR: Processing result is not an object!");
                    }
                    if (outDir.isNotEmpty())
                    {
                        juce::Logger::writeToLog("[Processing] Job complete; outputs ready at: " + outDir);

                        // Check if we're in batch final processing mode
                        if (batchFinalProcessingActive_)
                        {
                            juce::Logger::writeToLog("[Processing] Batch mode - calling onBatchFileProcessed");
                            onBatchFileProcessed(outDir);
                            return;  // Don't load browser yet - wait for all files
                        }

                        // SAMPLAR: browser_ removed - SampleBrowserPage deleted
                        // Single file processing complete; output at: outDir (no browser to load)
                        juce::Logger::writeToLog("[MainWindow] Processing complete, output: " + outDir + " (browser_ removed)");
                        processing_.enableZoomWindow(false);
                        switchTo(0); // SAMPLAR: return to initial (no browser page)
                    } else {
                        juce::Logger::writeToLog("???? [TRACE] ERROR: Output directory is empty!");
                    }
                });
            });
        }

        // Restore UI state from previous editor session (VST3 close/reopen)
        if (auto* uiState = samplar::ServiceLocator::getUIState())
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
        if (auto* bus = samplar::ServiceLocator::getBus())
        {
            stateRestoredSub_ = bus->subscribe("state.restored", [this, alive = alive_](const juce::var&) {
                if (!*alive) return;

                auto* uiState = samplar::ServiceLocator::getUIState();
                if (!uiState || !uiState->pendingDAWRestore) return;

                juce::Logger::writeToLog("[RootComponent] state.restored: restoring page="
                    + juce::String(uiState->pendingPage) + " folder=" + uiState->pendingFolder);

                // SAMPLAR: browser_ removed - pendingLibraryMode/pendingFolder restore skipped

                // -- Restore S-Mode session (fast path: reload stems from disk) --
                if (uiState->pendingSModeRestore)
                {
                    uiState->pendingSModeRestore = false;
                    juce::Logger::writeToLog("[RootComponent] Restoring S-Mode session...");

                    auto* reg = samplar::ServiceLocator::getRegistry();
                    if (!reg) return;

                    auto* smodeSvc = dynamic_cast<samplar::SModeService*>(reg->get("smode"));
                    if (!smodeSvc) return;

                    smodeService_ = smodeSvc;

                    // Check if cached stem files still exist on disk
                    bool stemsValid = !uiState->smodeStemFiles.empty();
                    for (const auto& f : uiState->smodeStemFiles)
                    {
                        if (!f.existsAsFile()) { stemsValid = false; break; }
                    }

                    if (stemsValid)
                    {
                        juce::Logger::writeToLog("[RootComponent] S-Mode fast restore: "
                            + juce::String(uiState->smodeStemFiles.size()) + " stems on disk");

                        if (auto* b = samplar::ServiceLocator::getBus())
                        {
                            smodeWorkflowReadySub_ = b->subscribe("smode.workflow.ready", [this, alive](const juce::var&) {
                                if (!*alive) return;
                                juce::Logger::writeToLog("[RootComponent] S-Mode restore ready � switching to page 9");
                                smodePage_.onStemsReady(*smodeService_);
                                switchTo(9);
                                smodePage_.resized();
                                smodePage_.repaint();
                                if (auto* b2 = samplar::ServiceLocator::getBus())
                                    b2->unsubscribe("smode.workflow.ready", smodeWorkflowReadySub_);
                            });
                        }

                        smodeSvc->setSourceFile(uiState->smodeSourceFile);
                        smodeSvc->loadStemAudio(uiState->smodeStemFiles);
                    }
                    else
                    {
                        juce::Logger::writeToLog("[RootComponent] S-Mode stems missing � re-separating");
                        if (uiState->smodeSourceFile.existsAsFile())
                        {
                            auto sc = (uiState->smodeStemCount == 6)
                                ? samplar::SModeSeparationService::StemCount::Six
                                : samplar::SModeSeparationService::StemCount::Four;

                            if (auto* b = samplar::ServiceLocator::getBus())
                            {
                                smodeWorkflowReadySub_ = b->subscribe("smode.workflow.ready", [this, alive](const juce::var&) {
                                    if (!*alive) return;
                                    smodePage_.onStemsReady(*smodeService_);
                                    switchTo(9);
                                    smodePage_.resized();
                                    smodePage_.repaint();
                                    if (auto* b2 = samplar::ServiceLocator::getBus())
                                        b2->unsubscribe("smode.workflow.ready", smodeWorkflowReadySub_);
                                });
                            }
                            smodeSvc->startWorkflow(uiState->smodeSourceFile, sc);
                        }
                    }
                }
                else
                {
                    // Non-S-Mode: switch to the saved page
                    switchTo(uiState->pendingPage);
                }

                uiState->pendingDAWRestore = false;  // consumed
            });
        }
    }

    // SAMPLAR: switchTo() is a no-op stub — SModePage is always the permanent main content.
    // ProcessingPage visibility is controlled directly (setVisible true/false as overlay).
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
            if (auto* uiState = samplar::ServiceLocator::getUIState())
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
        if (auto* registry = samplar::ServiceLocator::getRegistry()) {
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
        if (auto* registry = samplar::ServiceLocator::getRegistry()) {
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
    // SAMPLAR: InitialPage initial_, CommunityPage community_, mentor_ all removed.
    // ProcessingPage shown as overlay during separation (setVisible true/false).
    ProcessingPage processing_;
    samplar::SModePage smodePage_ { *samplar::ServiceLocator::getBus() };
    samplar::EventBus::HandlerId mentorNavSub_ { 0 }; // kept for bus cleanup in destructor
    // Debug console
    DebugConsole debugConsole_;
    std::unique_ptr<samplar::MultiLogger> multiLogger_;
    bool debugVisible_ { false };
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
    NudgeTimer nudgeTimer_; // reserved, not actively used
    samplar::NotificationButton notifBtn_;
    std::unique_ptr<samplar::UpdateDialog> updateDialog_;
    samplar::EventBus::HandlerId progId_ { 0 }, resId_ { 0 };
    samplar::EventBus::HandlerId vizId_ { 0 };
    samplar::EventBus::HandlerId waveEditorId_ { 0 };
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    // Gates whether switchTo() writes the current page/folder back to uiState.
    // Kept false during construction so the initial switchTo(0) doesn't clobber
    // the saved state before the restore block reads it. Flipped to true by the
    // restore-async lambda AFTER the saved page has been applied.
    bool uiStateWriteEnabled_ { false };
    // SAMPLAR: processMode_ removed with InitialPage; SModePage always runs S-Mode separation
    int previousPage_ { 0 };  // Track previous page for back navigation
    int currentPage_ { 0 };   // Track current page

    // Full Song separation service (for extracting drums before WavePreviewPage)
    std::unique_ptr<samplar::FullSongService> fullSongService_;

    // S-Mode service (owned by SamplarAudioProcessor � raw pointer)
    samplar::SModeService* smodeService_ { nullptr };
    samplar::EventBus::HandlerId smodeWorkflowReadySub_ { 0 };

    // FullSong drums-only separation (long-lived, lives on processor's ServiceRegistry)
    samplar::FullSongSeparationService* fullSongSepService_ { nullptr };  // not owned
    samplar::EventBus::HandlerId fullSongProgSub_     { 0 };
    samplar::EventBus::HandlerId fullSongMsgSub_      { 0 };
    samplar::EventBus::HandlerId fullSongCompleteSub_ { 0 };
    samplar::EventBus::HandlerId fullSongErrorSub_    { 0 };
    samplar::EventBus::HandlerId fullSongBatchCompleteSub_ { 0 };
    samplar::EventBus::HandlerId smodeWorkflowErrorSub_ { 0 };
    samplar::EventBus::HandlerId stateRestoredSub_ { 0 };
    std::unique_ptr<std::thread> separationThread_;
    std::atomic<bool> separationInProgress_ { false };
    // Flag to track if demucs separation was already done (for Full Song workflow)
    // After separation, subsequent processing should use mode 3/4, NOT mode 2
    bool separationAlreadyDone_ { false };

    // ========== BATCH SEPARATION STATE ==========
    std::vector<juce::File> batchFilesToSeparate_;       // Original full song files
    std::vector<juce::File> batchDrumStemFiles_;         // Extracted drum stem files
    int batchSeparationIndex_ { 0 };                     // Current file being separated
    std::atomic<bool> batchSeparationActive_ { false };  // True during batch separation

    // ========== BATCH FINAL PROCESSING STATE ==========
    // SAMPLAR: WavePreviewPage::BatchExportItem removed with WavePreviewPage
    // batchExportItems_ replaced with empty stub vector (never populated since preview_.onBatchExport removed)
    struct BatchExportItemStub { juce::File exportedFile; juce::String sourceSongName;
        int processingMode{3}; std::vector<double> slicePoints; BeatGridInfo beatGrid; };
    std::vector<BatchExportItemStub> batchExportItems_;  // SAMPLAR: always empty (preview_ removed)
    int batchProcessingIndex_ { 0 };                     // Current file being processed
    std::atomic<bool> batchFinalProcessingActive_ { false };  // True during batch final processing
    std::vector<juce::String> batchOutputDirectories_;    // Output directories from each file
    juce::File batchCombinedOutputDir_;                   // Combined directory for all samples

    void startBatchSeparation(const std::vector<juce::File>& files)
    {
        if (files.empty())
        {
            juce::Logger::writeToLog("[BatchSeparation] Empty file list - ignoring");
            return;
        }

        // Lazy-fetch the long-lived separation service
        if (!fullSongSepService_)
        {
            if (auto* reg = samplar::ServiceLocator::getRegistry())
                fullSongSepService_ = dynamic_cast<samplar::FullSongSeparationService*>(reg->get("fullSong.separation"));
        }
        if (!fullSongSepService_)
        {
            juce::Logger::writeToLog("[BatchSeparation] ERROR: FullSongSeparationService not registered");
            return;
        }
        if (fullSongSepService_->isSeparating())
        {
            juce::Logger::writeToLog("[BatchSeparation] Service already running - ignored");
            return;
        }

        juce::Logger::writeToLog("[BatchSeparation] === DISPATCHING BATCH OF " +
                                 juce::String(files.size()) + " FILES TO FullSongSeparationService ===");

        batchFilesToSeparate_ = files;
        batchDrumStemFiles_.clear();
        batchDrumStemFiles_.reserve(files.size());
        batchSeparationIndex_ = 0;
        batchSeparationActive_ = true;
        separationInProgress_  = true;

        processing_.resetForNewJob();
        processing_.setInputFile(files[0]);
        processing_.setProcessMode(2);  // FullSong mode
        processing_.setBatchInfo(1, static_cast<int>(files.size()));
        processing_.setStatusMessage("Starting drum separation...");
        switchTo(2);

        if (! fullSongSepService_->startBatchSeparation(files))
            juce::Logger::writeToLog("[BatchSeparation] startBatchSeparation returned false (race)");
    }

    void separateNextBatchFile()
    {
        if (batchSeparationIndex_ >= static_cast<int>(batchFilesToSeparate_.size()))
        {
            // All files separated - proceed to WavePreviewPage
            onBatchSeparationComplete();
            return;
        }

        const auto& currentFile = batchFilesToSeparate_[batchSeparationIndex_];
        juce::Logger::writeToLog("[BatchSeparation] Separating file " +
                                 juce::String(batchSeparationIndex_ + 1) + "/" +
                                 juce::String(batchFilesToSeparate_.size()) + ": " +
                                 currentFile.getFileName());

        // Update processing page with current file and batch info
        processing_.setInputFile(currentFile);
        processing_.setBatchInfo(batchSeparationIndex_ + 1, static_cast<int>(batchFilesToSeparate_.size()));
        processing_.setStatusMessage("Preparing audio for separation...");

        // Run separation in background thread
        separationThread_ = std::make_unique<std::thread>([this, alive = alive_, currentFile]() {
            samplar::FullSongService::Config cfg;
            cfg.onProgress = [this, alive](int pct, const juce::String& msg) {
                juce::MessageManager::callAsync([this, alive, pct, msg]() {
                    if (!*alive) return;
                    // Scale progress: each file contributes (100 / numFiles) percent
                    int totalFiles = static_cast<int>(batchFilesToSeparate_.size());
                    int baseProgress = (batchSeparationIndex_ * 100) / totalFiles;
                    int fileProgress = (pct * 100 / totalFiles) / 100;
                    int totalProgress = baseProgress + fileProgress;

                    processing_.setProgress(static_cast<double>(totalProgress));

                    // Create user-friendly status message based on progress
                    juce::String statusMsg;
                    if (pct < 10)
                        statusMsg = "Loading and preparing audio...";
                    else if (pct < 30)
                        statusMsg = "Encoding audio features...";
                    else if (pct < 70)
                        statusMsg = "Separating drums with AI model...";
                    else if (pct < 90)
                        statusMsg = "Decoding separated sources...";
                    else
                        statusMsg = "Finalizing drum stem...";

                    processing_.setStatusMessage(statusMsg + " (" + juce::String(pct) + "%)");

                    juce::Logger::writeToLog("[BatchSeparation] File " +
                                             juce::String(batchSeparationIndex_ + 1) + ": " +
                                             juce::String(pct) + "% - " + msg +
                                             " (Total: " + juce::String(totalProgress) + "%)");
                });
            };

            auto result = fullSongService_->separateDrums(currentFile, cfg,
                "batch_" + juce::String(batchSeparationIndex_) + "_");

            juce::MessageManager::callAsync([this, alive, result, currentFile]() {
                if (!*alive) return;
                if (result.error.isNotEmpty())
                {
                    juce::Logger::writeToLog("[BatchSeparation] FAILED for " + currentFile.getFileName() +
                                             ": " + result.error);
                    processing_.setStatusMessage("Error: " + result.error);
                    // Continue with next file even if one fails
                }
                else
                {
                    juce::Logger::writeToLog("[BatchSeparation] SUCCESS: " +
                                             result.drumStemFile.getFileName());
                    batchDrumStemFiles_.push_back(result.drumStemFile);
                    processing_.setStatusMessage("Drum stem extracted successfully!");
                }

                // Move to next file
                batchSeparationIndex_++;

                // Update status for next file if there are more
                if (batchSeparationIndex_ < static_cast<int>(batchFilesToSeparate_.size()))
                {
                    processing_.setStatusMessage("Moving to next file...");
                }

                separateNextBatchFile();
            });
        });
        separationThread_->detach();
    }

    void onBatchSeparationComplete()
    {
        batchSeparationActive_ = false;
        separationAlreadyDone_ = true;

        juce::Logger::writeToLog("[BatchSeparation] === ALL SEPARATIONS COMPLETE ===");
        juce::Logger::writeToLog("[BatchSeparation] " + juce::String(batchDrumStemFiles_.size()) +
                                 " drum stems extracted from " +
                                 juce::String(batchFilesToSeparate_.size()) + " files");

        if (batchDrumStemFiles_.empty())
        {
            juce::Logger::writeToLog("[BatchSeparation] ERROR: No drum stems extracted!");
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Batch Separation Failed",
                "Could not extract drums from any of the songs.",
                "OK");
            switchTo(0);  // Go back to initial page
            return;
        }

        // SAMPLAR: preview_ removed - WavePreviewPage deleted
        // Was: preview_.loadMultipleFiles(batchDrumStemFiles_); switchTo(1);
        juce::Logger::writeToLog("[BatchSeparation] " + juce::String(batchDrumStemFiles_.size())
            + " drum stems ready (preview_ removed)");
        switchTo(0); // Return to initial page

        // Clear batch state
        batchFilesToSeparate_.clear();
    }

    // ========== BATCH FINAL PROCESSING FUNCTIONS ==========
    void processNextBatchFile()
    {
        if (batchProcessingIndex_ >= static_cast<int>(batchExportItems_.size()))
        {
            // All files processed
            onBatchFinalProcessingComplete();
            return;
        }

        const auto& item = batchExportItems_[batchProcessingIndex_];

        juce::Logger::writeToLog("[BatchFinalProcessing] Processing file " +
                                 juce::String(batchProcessingIndex_ + 1) + "/" +
                                 juce::String(batchExportItems_.size()) + ": " +
                                 item.sourceSongName);

        // Update processing page
        processing_.setInputFile(item.exportedFile);
        processing_.setBatchInfo(batchProcessingIndex_ + 1, static_cast<int>(batchExportItems_.size()));
        processing_.setStatusMessage("Slicing " + item.sourceSongName + "...");
        processing_.setProgress(0.0);

        // Set beat grid info
        processing_.setBeatGridInfo(item.beatGrid);

        // Enqueue processing job
        if (auto* registry = samplar::ServiceLocator::getRegistry())
        {
            if (auto* coordSvc = dynamic_cast<samplar::ProcessingCoordinatorService*>(
                    registry->get("processing.coordinator")))
            {
                // Store beat grid selection offset for coordinate conversion
                BeatGridInfo beatGrid = item.beatGrid;
                beatGrid.selectionOffset = item.beatGrid.selectionOffset;

                if (item.processingMode == 4 && !item.slicePoints.empty())
                {
                    // GridSlice mode with user-defined slice points
                    juce::Logger::writeToLog("[BatchFinalProcessing] Using GridSlice mode with " +
                                             juce::String(item.slicePoints.size()) + " slice points");
                    coordSvc->enqueue(item.exportedFile, 4, beatGrid, item.slicePoints);
                }
                else
                {
                    // TempoFirst mode (auto slicing)
                    juce::Logger::writeToLog("[BatchFinalProcessing] Using TempoFirst mode");
                    coordSvc->enqueue(item.exportedFile, 3, beatGrid);
                }
            }
        }
    }

    void onBatchFileProcessed(const juce::String& outputDir)
    {
        juce::Logger::writeToLog("[BatchFinalProcessing] File " +
                                 juce::String(batchProcessingIndex_ + 1) + " complete, output: " + outputDir);

        // Collect ALL output directories
        if (outputDir.isNotEmpty())
        {
            batchOutputDirectories_.push_back(outputDir);
        }

        // Move to next file
        batchProcessingIndex_++;

        // Check if more files to process
        if (batchProcessingIndex_ < static_cast<int>(batchExportItems_.size()))
        {
            // CRITICAL: Add delay before queueing next file to allow processing thread
            // to fully exit and reset isProcessing_ flag. Without this delay, the next
            // job gets rejected with "already processing a job".
            juce::Timer::callAfterDelay(200, [this]() {
                if (batchFinalProcessingActive_)  // Guard against shutdown
                {
                    processNextBatchFile();
                }
            });
        }
        else
        {
            onBatchFinalProcessingComplete();
        }
    }

    void onBatchFinalProcessingComplete()
    {
        batchFinalProcessingActive_ = false;

        juce::Logger::writeToLog("[BatchFinalProcessing] === ALL FILES PROCESSED ===");
        juce::Logger::writeToLog("[BatchFinalProcessing] " + juce::String(batchExportItems_.size()) +
                                 " files processed across " + juce::String(batchOutputDirectories_.size()) + " directories");

        if (batchOutputDirectories_.empty())
        {
            juce::Logger::writeToLog("[BatchFinalProcessing] ERROR: No output directories!");
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Batch Processing Failed",
                "Could not process any of the files.",
                "OK");
            switchTo(0);  // SAMPLAR: preview page removed, return to initial
            return;
        }

        // Create a combined output directory for all samples
        // Use temp directory with timestamp for uniqueness
        auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        batchCombinedOutputDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("SAMPLAR_batch_" + timestamp);
        batchCombinedOutputDir_.createDirectory();

        juce::Logger::writeToLog("[BatchFinalProcessing] Combining samples into: " +
                                 batchCombinedOutputDir_.getFullPathName());

        // Build song->files mapping for direct metadata passing to browser
        // This eliminates fragile filename parsing - we KNOW which files came from which song
        std::map<juce::String, std::vector<juce::File>> songFiles;
        int totalSamples = 0;

        for (size_t i = 0; i < batchOutputDirectories_.size(); ++i)
        {
            juce::File srcDir(batchOutputDirectories_[i]);
            if (!srcDir.isDirectory())
            {
                juce::Logger::writeToLog("[BatchFinalProcessing] WARNING: Source dir not found: " + srcDir.getFullPathName());
                continue;
            }

            // Get the KNOWN song name for this batch item
            juce::String songName;
            if (i < batchExportItems_.size())
            {
                songName = batchExportItems_[i].sourceSongName;
            }
            else
            {
                songName = "Song " + juce::String(i + 1);
            }

            juce::Logger::writeToLog("[BatchFinalProcessing] Processing song: " + songName);

            // Copy all WAV files and track which song they came from
            auto wavFiles = srcDir.findChildFiles(juce::File::findFiles, false, "*.wav");
            std::vector<juce::File> songFileList;

            for (const auto& srcFile : wavFiles)
            {
                // Still prefix filenames to avoid collisions in combined folder
                juce::String newName = songName + "_" + srcFile.getFileName();
                juce::File destFile = batchCombinedOutputDir_.getChildFile(newName);

                if (srcFile.copyFileTo(destFile))
                {
                    songFileList.push_back(destFile);  // Track the DESTINATION file
                    totalSamples++;
                }
                else
                {
                    juce::Logger::writeToLog("[BatchFinalProcessing] WARNING: Failed to copy: " + srcFile.getFileName());
                }
            }

            // Store the song->files mapping
            songFiles[songName] = std::move(songFileList);

            juce::Logger::writeToLog("[BatchFinalProcessing] Copied " + juce::String(wavFiles.size()) +
                                     " samples from: " + songName);
        }

        juce::Logger::writeToLog("[BatchFinalProcessing] Total samples combined: " + juce::String(totalSamples));
        juce::Logger::writeToLog("[BatchFinalProcessing] Songs tracked: " + juce::String(static_cast<int>(songFiles.size())));

        if (totalSamples == 0)
        {
            juce::Logger::writeToLog("[BatchFinalProcessing] ERROR: No samples were copied!");
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Batch Processing Failed",
                "No samples were generated from the files.",
                "OK");
            switchTo(1);
            return;
        }

        // SAMPLAR: browser_ removed - batch results cannot be loaded into SampleBrowserPage
        juce::Logger::writeToLog("[BatchFinalProcessing] Complete; combined output: "
            + batchCombinedOutputDir_.getFullPathName() + " (browser_ removed)");
        switchTo(0); // SAMPLAR: return to initial (no browser page)

        // Clear batch state
        batchExportItems_.clear();
        batchOutputDirectories_.clear();
    }
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
        juce::Logger::writeToLog("???? [MainWindow] Window set to resizable");

        juce::Logger::writeToLog("???? [MainWindow] Creating RootComponent");
        auto* rc = new RootComponent();
        juce::Logger::writeToLog("???? [MainWindow] RootComponent created");

        juce::Logger::writeToLog("???? [MainWindow] About to set content owned WITHOUT sizing first");
        setContentOwned(rc, true);
        juce::Logger::writeToLog("???? [MainWindow] RootComponent set as content");

        juce::Logger::writeToLog("???? [MainWindow] About to set window size");
        setSize(1061, 724);
        juce::Logger::writeToLog("???? [MainWindow] Window size set to 1061x724");

        juce::Logger::writeToLog("???? [MainWindow] About to attach EventBus");
        if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
            root->attachBus();
        juce::Logger::writeToLog("???? [MainWindow] EventBus attached");

        juce::Logger::writeToLog("???? [MainWindow] About to center window");
        centreWithSize(1061, 724);
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
    // Check if batch processing is active before allowing close
    if (auto* root = dynamic_cast<RootComponent*>(getContentComponent()))
    {
        if (root->isBatchProcessingActive())
        {
            juce::Logger::writeToLog("[MainWindow] Close requested during batch processing - showing confirmation");

            // Show async confirmation dialog
            auto options = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Batch Processing In Progress")
                .withMessage("Batch processing is still running.\n\nClosing now will cancel processing and samples may be incomplete.\n\nAre you sure you want to exit?")
                .withButton("Exit Anyway")
                .withButton("Continue Processing");

            juce::AlertWindow::showAsync(options, [](int result) {
                if (result == 1)  // User clicked "Exit Anyway"
                {
                    juce::Logger::writeToLog("[MainWindow] User confirmed exit during batch processing");
                    samplar::ServiceLocator::requestQuit();
                }
                else
                {
                    juce::Logger::writeToLog("[MainWindow] User chose to continue batch processing");
                }
            });
            return;  // Don't quit yet - wait for user decision
        }
    }

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



