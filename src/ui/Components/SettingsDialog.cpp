#include "SettingsDialog.h"
#include "../../app/AudioDeviceService.h"
#include "../../app/ServiceRegistry.h"
#include "../../app/App.h"
#include "../../app/ServiceLocator.h"
#include "../../services/LicenseSystemService.h"
#include "../../services/MidiMappingService.h"
#include "../licensing/LicenseActivationDialog.h"

namespace samplar {

//==============================================================================
// SettingsDialog Implementation
//==============================================================================

SettingsDialog::SettingsDialog()
{
    juce::Logger::writeToLog("[SettingsDialog] === CONSTRUCTOR START ===");

    // Set size first
    setSize(700, 550);

    try {
        // Title
        titleLabel_.setFont(juce::Font(18.0f, juce::Font::bold));
        titleLabel_.setColour(juce::Label::textColourId, accentColor_);
        addAndMakeVisible(titleLabel_);

        // Close button
        closeBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff5555));
        closeBtn_.onClick = [this] { onClose(); };
        addAndMakeVisible(closeBtn_);

        // Detect standalone mode: AudioDeviceService is only registered in standalone
        {
            auto* reg = samplar::ServiceLocator::getRegistry();
            standaloneMode_ = (reg != nullptr &&
                dynamic_cast<AudioDeviceService*>(reg->get("audio")) != nullptr);
        }
        juce::Logger::writeToLog("[SettingsDialog] Standalone mode: " +
                                 juce::String(standaloneMode_ ? "YES" : "NO"));

        // Tab buttons (3 tabs — MIDI only in standalone)
        auto setupTab = [this](juce::TextButton& btn, int index) {
            btn.setColour(juce::TextButton::buttonColourId, headerColor_);
            btn.setColour(juce::TextButton::textColourOffId, textColor_);
            btn.onClick = [this, index] { switchTab(index); };
            addAndMakeVisible(btn);
        };

        setupTab(audioTab_, 0);
        setupTab(midiTab_, 1);
        setupTab(licenseTab_, 2);

        // Hide MIDI tab in VST3 mode (DAW manages MIDI)
        if (!standaloneMode_)
            midiTab_.setVisible(false);

        // Create panels
        juce::Logger::writeToLog("[SettingsDialog] Creating AudioSettingsPanel");
        audioPanel_ = std::make_unique<AudioSettingsPanel>();
        juce::Logger::writeToLog("[SettingsDialog] Creating MidiSettingsPanel");
        midiPanel_ = std::make_unique<MidiSettingsPanel>();
        juce::Logger::writeToLog("[SettingsDialog] Creating LicenseInfoPanel");
        licensePanel_ = std::make_unique<LicenseInfoPanel>();

        addAndMakeVisible(audioPanel_.get());
        addAndMakeVisible(midiPanel_.get());
        addAndMakeVisible(licensePanel_.get());

        // Footer button
        closeFooterBtn_.setColour(juce::TextButton::buttonColourId, accentColor_.withAlpha(0.3f));
        closeFooterBtn_.onClick = [this] { onClose(); };
        addAndMakeVisible(closeFooterBtn_);

        // Force layout update
        resized();

        // Start on Audio tab
        switchTab(0);

        juce::Logger::writeToLog("[SettingsDialog] === CONSTRUCTOR END ===");
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog("[SettingsDialog] EXCEPTION in constructor: " + juce::String(e.what()));
    }
    catch (...) {
        juce::Logger::writeToLog("[SettingsDialog] UNKNOWN EXCEPTION in constructor");
    }
}

SettingsDialog::~SettingsDialog()
{
    juce::Logger::writeToLog("[SettingsDialog] === DESTRUCTOR ===");
}

void SettingsDialog::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(bgColor_);

    // Header bar
    auto headerArea = getLocalBounds().removeFromTop(50);
    g.setColour(headerColor_);
    g.fillRect(headerArea);

    // Border
    g.setColour(accentColor_.withAlpha(0.5f));
    g.drawRect(getLocalBounds(), 2);
}

void SettingsDialog::resized()
{
    auto bounds = getLocalBounds();

    // Header (50px)
    auto header = bounds.removeFromTop(50);
    titleLabel_.setBounds(header.removeFromLeft(150).reduced(15, 10));
    closeBtn_.setBounds(header.removeFromRight(40).reduced(8));

    // Tab bar (40px) — 3 tabs in standalone, 2 in VST3
    auto tabBar = bounds.removeFromTop(40);
    int numTabs = standaloneMode_ ? 3 : 2;
    int tabWidth = tabBar.getWidth() / numTabs;
    audioTab_.setBounds(tabBar.removeFromLeft(tabWidth).reduced(2));
    if (standaloneMode_)
        midiTab_.setBounds(tabBar.removeFromLeft(tabWidth).reduced(2));
    licenseTab_.setBounds(tabBar.reduced(2));

    // Footer (50px)
    auto footer = bounds.removeFromBottom(50);
    footer = footer.reduced(10, 8);
    closeFooterBtn_.setBounds(footer.removeFromRight(100));

    // Content area
    auto content = bounds.reduced(10);
    if (audioPanel_)
        audioPanel_->setBounds(content);
    if (midiPanel_)
        midiPanel_->setBounds(content);
    if (licensePanel_)
        licensePanel_->setBounds(content);
}

void SettingsDialog::switchTab(int tabIndex)
{
    currentTab_ = tabIndex;

    // Update tab button colors
    auto setTabActive = [this](juce::TextButton& btn, bool active) {
        btn.setColour(juce::TextButton::buttonColourId,
                      active ? accentColor_.withAlpha(0.3f) : headerColor_);
    };

    setTabActive(audioTab_, tabIndex == 0);
    setTabActive(midiTab_, tabIndex == 1);
    setTabActive(licenseTab_, tabIndex == 2);

    // Show/hide panels
    audioPanel_->setVisible(tabIndex == 0);
    midiPanel_->setVisible(tabIndex == 1);
    licensePanel_->setVisible(tabIndex == 2);

    // Refresh data when switching to relevant tabs
    if (tabIndex == 1) {
        midiPanel_->refreshDeviceList();
    }
    if (tabIndex == 2) {
        licensePanel_->refreshLicenseInfo();
    }

    repaint();
}

void SettingsDialog::onClose()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
        dw->exitModalState(0);
    }
}

void SettingsDialog::showDialog(juce::Component* parent)
{
    juce::Logger::writeToLog("[SettingsDialog] showDialog() called");

    try {
        auto* dialog = new SettingsDialog();

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "";
        options.dialogBackgroundColour = juce::Colour(0xff1a1a2e);
        options.content.setOwned(dialog);
        options.componentToCentreAround = parent;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;

        options.launchAsync();
        juce::Logger::writeToLog("[SettingsDialog] Dialog launched");
    }
    catch (const std::exception& e) {
        juce::Logger::writeToLog("[SettingsDialog] EXCEPTION in showDialog: " + juce::String(e.what()));
    }
}

// LearningModelPanel removed (ClassificationService/DrumMLClassifier deleted)

//==============================================================================
// AudioSettingsPanel Implementation (functional settings only)
//==============================================================================

// Helper to get AudioDeviceService via ServiceLocator
static AudioDeviceService* getAudioDeviceService()
{
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return nullptr;

    return dynamic_cast<AudioDeviceService*>(reg->get("audio"));
}

AudioSettingsPanel::AudioSettingsPanel()
{
    juce::Logger::writeToLog("[AudioSettingsPanel] === CONSTRUCTOR START ===");

    // Device label and combo
    deviceLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    deviceLabel_.setColour(juce::Label::textColourId, textColor_);
    addAndMakeVisible(deviceLabel_);

    deviceCombo_.setColour(juce::ComboBox::backgroundColourId, sectionBgColor_);
    deviceCombo_.setColour(juce::ComboBox::textColourId, textColor_);
    deviceCombo_.setColour(juce::ComboBox::outlineColourId, borderColor_.withAlpha(0.5f));
    deviceCombo_.onChange = [this] { onDeviceChanged(); };
    addAndMakeVisible(deviceCombo_);

    deviceHelpLabel_.setFont(juce::Font(11.0f));
    deviceHelpLabel_.setColour(juce::Label::textColourId, dimTextColor_);
    addAndMakeVisible(deviceHelpLabel_);

    // Buffer size label and combo
    bufferLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    bufferLabel_.setColour(juce::Label::textColourId, textColor_);
    addAndMakeVisible(bufferLabel_);

    bufferCombo_.setColour(juce::ComboBox::backgroundColourId, sectionBgColor_);
    bufferCombo_.setColour(juce::ComboBox::textColourId, textColor_);
    bufferCombo_.setColour(juce::ComboBox::outlineColourId, borderColor_.withAlpha(0.5f));
    bufferCombo_.onChange = [this] { onBufferChanged(); };
    addAndMakeVisible(bufferCombo_);

    bufferHelpLabel_.setFont(juce::Font(11.0f));
    bufferHelpLabel_.setColour(juce::Label::textColourId, dimTextColor_);
    addAndMakeVisible(bufferHelpLabel_);

    // Sample rate display
    sampleRateLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    sampleRateLabel_.setColour(juce::Label::textColourId, textColor_);
    addAndMakeVisible(sampleRateLabel_);

    sampleRateValue_.setFont(juce::Font(14.0f));
    sampleRateValue_.setColour(juce::Label::textColourId, borderColor_);
    addAndMakeVisible(sampleRateValue_);

    // Populate device list
    refreshDeviceList();

    juce::Logger::writeToLog("[AudioSettingsPanel] === CONSTRUCTOR END ===");
}

void AudioSettingsPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Title
    auto titleArea = bounds.removeFromTop(40);
    g.setColour(textColor_);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("AUDIO SETTINGS", titleArea.reduced(5, 0), juce::Justification::centredLeft);

    bounds.removeFromTop(10);

    // Device section background
    auto deviceSection = bounds.removeFromTop(100);
    g.setColour(sectionBgColor_.withAlpha(0.3f));
    g.fillRoundedRectangle(deviceSection.toFloat(), 4.0f);
    g.setColour(borderColor_.withAlpha(0.2f));
    g.drawRoundedRectangle(deviceSection.toFloat(), 4.0f, 1.0f);

    bounds.removeFromTop(15);

    // Buffer section background
    auto bufferSection = bounds.removeFromTop(100);
    g.setColour(sectionBgColor_.withAlpha(0.3f));
    g.fillRoundedRectangle(bufferSection.toFloat(), 4.0f);
    g.setColour(borderColor_.withAlpha(0.2f));
    g.drawRoundedRectangle(bufferSection.toFloat(), 4.0f, 1.0f);
}

void AudioSettingsPanel::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50);  // Title space

    // Device section
    auto deviceSection = bounds.removeFromTop(100).reduced(10);
    auto deviceRow = deviceSection.removeFromTop(30);
    deviceLabel_.setBounds(deviceRow.removeFromLeft(120));
    deviceCombo_.setBounds(deviceRow.removeFromLeft(300));

    deviceSection.removeFromTop(5);
    deviceHelpLabel_.setBounds(deviceSection.removeFromTop(20));

    // Sample rate in same section
    deviceSection.removeFromTop(10);
    auto srRow = deviceSection.removeFromTop(25);
    sampleRateLabel_.setBounds(srRow.removeFromLeft(120));
    sampleRateValue_.setBounds(srRow.removeFromLeft(150));

    bounds.removeFromTop(15);

    // Buffer section
    auto bufferSection = bounds.removeFromTop(100).reduced(10);
    auto bufferRow = bufferSection.removeFromTop(30);
    bufferLabel_.setBounds(bufferRow.removeFromLeft(120));
    bufferCombo_.setBounds(bufferRow.removeFromLeft(200));

    bufferSection.removeFromTop(5);
    bufferHelpLabel_.setBounds(bufferSection.removeFromTop(20));
}

void AudioSettingsPanel::refreshDeviceList()
{
    juce::Logger::writeToLog("[AudioSettingsPanel] Refreshing device list");

    auto* audioService = getAudioDeviceService();
    if (!audioService) {
        juce::Logger::writeToLog("[AudioSettingsPanel] No AudioDeviceService available");
        return;
    }

    // Populate device dropdown
    deviceCombo_.clear();
    auto devices = audioService->getAvailableOutputDevices();
    juce::String currentDevice = audioService->getCurrentDeviceName();

    int selectedId = 1;
    for (int i = 0; i < devices.size(); ++i) {
        deviceCombo_.addItem(devices[i], i + 1);
        if (devices[i] == currentDevice) {
            selectedId = i + 1;
        }
    }
    deviceCombo_.setSelectedId(selectedId, juce::dontSendNotification);

    // Populate buffer size dropdown
    bufferCombo_.clear();
    auto bufferSizes = audioService->getAvailableBufferSizes();
    int currentBuffer = audioService->getCurrentBufferSize();

    int bufferSelectedId = 1;
    for (int i = 0; i < bufferSizes.size(); ++i) {
        juce::String label = juce::String(bufferSizes[i]) + " samples";
        bufferCombo_.addItem(label, bufferSizes[i]);
        if (bufferSizes[i] == currentBuffer) {
            bufferSelectedId = bufferSizes[i];
        }
    }
    bufferCombo_.setSelectedId(bufferSelectedId, juce::dontSendNotification);

    // Update sample rate display
    updateSampleRateDisplay();

    juce::Logger::writeToLog("[AudioSettingsPanel] Device list refreshed: " +
                             juce::String(devices.size()) + " devices");
}

void AudioSettingsPanel::onDeviceChanged()
{
    juce::String selectedDevice = deviceCombo_.getText();
    juce::Logger::writeToLog("[AudioSettingsPanel] Device changed to: " + selectedDevice);

    auto* audioService = getAudioDeviceService();
    if (!audioService) return;

    if (audioService->trySetPreferredDevice(selectedDevice)) {
        juce::Logger::writeToLog("[AudioSettingsPanel] Device set successfully");
        // Refresh buffer sizes for new device
        bufferCombo_.clear();
        auto bufferSizes = audioService->getAvailableBufferSizes();
        int currentBuffer = audioService->getCurrentBufferSize();

        for (int i = 0; i < bufferSizes.size(); ++i) {
            juce::String label = juce::String(bufferSizes[i]) + " samples";
            bufferCombo_.addItem(label, bufferSizes[i]);
        }
        bufferCombo_.setSelectedId(currentBuffer, juce::dontSendNotification);

        updateSampleRateDisplay();
    }
}

void AudioSettingsPanel::onBufferChanged()
{
    int selectedBuffer = bufferCombo_.getSelectedId();
    juce::Logger::writeToLog("[AudioSettingsPanel] Buffer changed to: " + juce::String(selectedBuffer));

    auto* audioService = getAudioDeviceService();
    if (!audioService) return;

    if (audioService->setBufferSize(selectedBuffer)) {
        juce::Logger::writeToLog("[AudioSettingsPanel] Buffer size set successfully");
    }
}

void AudioSettingsPanel::updateSampleRateDisplay()
{
    auto* audioService = getAudioDeviceService();
    if (!audioService) return;

    auto* device = audioService->deviceManager().getCurrentAudioDevice();
    if (device) {
        double sampleRate = device->getCurrentSampleRate();
        sampleRateValue_.setText(juce::String(static_cast<int>(sampleRate)) + " Hz", juce::dontSendNotification);
    } else {
        sampleRateValue_.setText("N/A", juce::dontSendNotification);
    }
}

//==============================================================================
// MidiSettingsPanel Implementation
//==============================================================================

// Helper to get the AudioDeviceManager for MIDI device management.
// In standalone mode, AudioDeviceService owns the device manager.
static juce::AudioDeviceManager* getStandaloneDeviceManager()
{
    auto* audioService = getAudioDeviceService();
    if (audioService)
        return &audioService->deviceManager();

    return nullptr;
}

MidiSettingsPanel::MidiSettingsPanel()
{
    juce::Logger::writeToLog("[MidiSettingsPanel] === CONSTRUCTOR START ===");

    // Header
    headerLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    headerLabel_.setColour(juce::Label::textColourId, textColor_);
    addAndMakeVisible(headerLabel_);

    // Help text
    helpLabel_.setFont(juce::Font(11.0f));
    helpLabel_.setColour(juce::Label::textColourId, dimTextColor_);
    addAndMakeVisible(helpLabel_);

    // Status label
    statusLabel_.setFont(juce::Font(13.0f));
    statusLabel_.setColour(juce::Label::textColourId, dimTextColor_);
    statusLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel_);

    // Computer Keyboard MIDI checkbox
    computerKeyboardBtn_.setColour(juce::ToggleButton::textColourId, textColor_);
    computerKeyboardBtn_.setColour(juce::ToggleButton::tickColourId, enabledColor_);
    computerKeyboardBtn_.setTooltip("Use your computer keyboard as a MIDI controller (QWERTY layout: Z-M = C3-B3, Q-U = C4-B4)");
    computerKeyboardBtn_.setToggleState(true, juce::dontSendNotification);  // Default ON
    computerKeyboardBtn_.onClick = [this] {
        bool enabled = computerKeyboardBtn_.getToggleState();
        if (auto* reg = samplar::ServiceLocator::getRegistry())
        {
            if (auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping")))
                midiSvc->setComputerKeyboardEnabled(enabled);
        }
        juce::Logger::writeToLog("[MidiSettings] Computer keyboard MIDI: " + juce::String(enabled ? "ON" : "OFF"));
    };
    addAndMakeVisible(computerKeyboardBtn_);

    // Refresh button
    refreshBtn_.setColour(juce::TextButton::buttonColourId, borderColor_.withAlpha(0.3f));
    refreshBtn_.setColour(juce::TextButton::textColourOffId, textColor_);
    refreshBtn_.onClick = [this] { refreshDeviceList(); };
    addAndMakeVisible(refreshBtn_);

    // Viewport for scrollable device list
    deviceListComponent_ = std::make_unique<juce::Component>();
    viewport_.setViewedComponent(deviceListComponent_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    // Initial device scan
    refreshDeviceList();

    // Poll for device changes every 3 seconds
    startTimer(3000);

    juce::Logger::writeToLog("[MidiSettingsPanel] === CONSTRUCTOR END ===");
}

MidiSettingsPanel::~MidiSettingsPanel()
{
    stopTimer();
    juce::Logger::writeToLog("[MidiSettingsPanel] === DESTRUCTOR ===");
}

void MidiSettingsPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Title area
    bounds.removeFromTop(40);
    bounds.removeFromTop(25); // help text
    bounds.removeFromTop(10); // spacing

    // Device list section background
    auto listArea = bounds.reduced(0, 5);
    listArea.removeFromBottom(50); // reserve space for refresh button
    g.setColour(sectionBgColor_.withAlpha(0.3f));
    g.fillRoundedRectangle(listArea.toFloat(), 4.0f);
    g.setColour(borderColor_.withAlpha(0.2f));
    g.drawRoundedRectangle(listArea.toFloat(), 4.0f, 1.0f);
}

void MidiSettingsPanel::resized()
{
    auto bounds = getLocalBounds();

    // Title
    headerLabel_.setBounds(bounds.removeFromTop(40).reduced(5, 5));

    // Help text
    helpLabel_.setBounds(bounds.removeFromTop(25).reduced(5, 0));

    bounds.removeFromTop(10);

    // Bottom bar: Computer Keyboard checkbox + Refresh button
    auto bottomBar = bounds.removeFromBottom(50);
    auto bottomReduced = bottomBar.reduced(5, 10);
    computerKeyboardBtn_.setBounds(bottomReduced.removeFromLeft(200));
    bottomReduced.removeFromLeft(10);
    refreshBtn_.setBounds(bottomReduced.removeFromLeft(160));

    // Device list area (viewport)
    auto listArea = bounds.reduced(5, 5);
    viewport_.setBounds(listArea);

    if (deviceRows_.isEmpty()) {
        statusLabel_.setBounds(listArea);
        statusLabel_.setVisible(true);
    } else {
        statusLabel_.setVisible(false);
    }

    // Layout device rows inside the viewport component
    rebuildDeviceList();
}

void MidiSettingsPanel::timerCallback()
{
    // Check if device count changed (hot-plug detection)
    auto devices = juce::MidiInput::getAvailableDevices();
    if (devices.size() != lastMidiDeviceCount_)
    {
        juce::Logger::writeToLog("[MidiSettingsPanel] MIDI device count changed, refreshing");
        refreshDeviceList();
    }
}

void MidiSettingsPanel::refreshDeviceList()
{
    juce::Logger::writeToLog("[MidiSettingsPanel] === refreshDeviceList START ===");

    auto* devMgr = getStandaloneDeviceManager();
    auto devices = juce::MidiInput::getAvailableDevices();

    juce::Logger::writeToLog("[MidiSettingsPanel] Found " +
                             juce::String(devices.size()) + " MIDI input devices");

    // Remember which devices were enabled AND which channel each was on
    std::map<juce::String, bool> previousState;
    std::map<juce::String, int>  previousChannel;   // Bug 7 fix: capture channel BEFORE clear
    for (auto* row : deviceRows_) {
        previousState[row->identifier]   = row->enabled;
        previousChannel[row->identifier] = row->channel;
    }

    // Clear and rebuild
    deviceRows_.clear();
    lastMidiDeviceCount_ = devices.size();  // keep in sync

    for (const auto& device : devices) {
        juce::Logger::writeToLog("[MidiSettingsPanel]   Device: " + device.name +
                                 " (id: " + device.identifier + ")");

        auto* row = new MidiDeviceRow();
        row->name = device.name;
        row->identifier = device.identifier;

        // Check if currently enabled in the standalone's AudioDeviceManager
        bool isEnabled = false;
        if (devMgr) {
            isEnabled = devMgr->isMidiInputDeviceEnabled(device.identifier);
        }

        row->enabled = isEnabled;

        // Create toggle button
        row->enableBtn = std::make_unique<juce::ToggleButton>(device.name);
        row->enableBtn->setToggleState(isEnabled, juce::dontSendNotification);
        row->enableBtn->setColour(juce::ToggleButton::textColourId, textColor_);
        row->enableBtn->setColour(juce::ToggleButton::tickColourId, enabledColor_);

        int index = deviceRows_.size();
        row->enableBtn->onClick = [this, index] { onDeviceToggled(index); };

        // Create MIDI channel selector (All, 1-16)
        row->channelCombo = std::make_unique<juce::ComboBox>();
        row->channelCombo->addItem("All Ch", 1);  // itemId 1 = channel 0 (all)
        for (int ch = 1; ch <= 16; ++ch)
            row->channelCombo->addItem("Ch " + juce::String(ch), ch + 1);  // itemId 2-17

        // Restore previous channel selection from pre-captured map (Bug 7 fix)
        auto prevChIt = previousChannel.find(device.identifier);
        int prevChannel = (prevChIt != previousChannel.end()) ? prevChIt->second : 0;
        row->channel = prevChannel;
        row->channelCombo->setSelectedId(prevChannel + 1, juce::dontSendNotification);

        row->channelCombo->setColour(juce::ComboBox::backgroundColourId, sectionBgColor_);
        row->channelCombo->setColour(juce::ComboBox::textColourId, textColor_);
        row->channelCombo->setColour(juce::ComboBox::outlineColourId, borderColor_.withAlpha(0.3f));
        row->channelCombo->onChange = [this, index] { onChannelChanged(index); };

        deviceListComponent_->addAndMakeVisible(row->enableBtn.get());
        deviceListComponent_->addAndMakeVisible(row->channelCombo.get());
        deviceRows_.add(row);
    }

    // Update status
    if (devices.isEmpty()) {
        statusLabel_.setText("No MIDI devices detected.\nConnect a MIDI controller and click Refresh.",
                             juce::dontSendNotification);
        statusLabel_.setVisible(true);
    } else {
        statusLabel_.setVisible(false);
    }

    rebuildDeviceList();
    repaint();

    juce::Logger::writeToLog("[MidiSettingsPanel] === refreshDeviceList END ===");
}

void MidiSettingsPanel::rebuildDeviceList()
{
    if (deviceRows_.isEmpty()) return;

    int rowHeight = 40;
    int totalHeight = deviceRows_.size() * rowHeight;
    int viewWidth = viewport_.getWidth() - 10;

    deviceListComponent_->setSize(viewWidth > 0 ? viewWidth : 400, totalHeight);

    const int comboWidth = 90;
    const int comboPad = 10;

    for (int i = 0; i < deviceRows_.size(); ++i) {
        auto* row = deviceRows_[i];
        int y = i * rowHeight + 5;
        int h = rowHeight - 10;
        int toggleWidth = viewWidth - 20 - comboWidth - comboPad;

        if (row->enableBtn)
            row->enableBtn->setBounds(10, y, toggleWidth, h);

        if (row->channelCombo)
            row->channelCombo->setBounds(10 + toggleWidth + comboPad, y, comboWidth, h);
    }
}

void MidiSettingsPanel::onDeviceToggled(int index)
{
    if (index < 0 || index >= deviceRows_.size()) return;

    auto* row = deviceRows_[index];
    bool nowEnabled = row->enableBtn->getToggleState();
    row->enabled = nowEnabled;

    juce::Logger::writeToLog("[MidiSettingsPanel] Device toggled: " + row->name +
                             " -> " + juce::String(nowEnabled ? "ENABLED" : "DISABLED"));

    // Apply to the AudioDeviceService's manager
    auto* devMgr = getStandaloneDeviceManager();
    if (devMgr) {
        devMgr->setMidiInputDeviceEnabled(row->identifier, nowEnabled);
        juce::Logger::writeToLog("[MidiSettingsPanel] DeviceManager updated for: " + row->name);

        // Verify it took effect
        bool verified = devMgr->isMidiInputDeviceEnabled(row->identifier);
        juce::Logger::writeToLog("[MidiSettingsPanel] Verification: " +
                                 juce::String(verified ? "OK" : "FAILED"));

        // MidiMappingService was already connected to this DeviceManager at app
        // startup (App::initialise). No need to re-register on every toggle.
    } else {
        juce::Logger::writeToLog("[MidiSettingsPanel] WARNING: No DeviceManager available");
    }
}

void MidiSettingsPanel::onChannelChanged(int index)
{
    if (index < 0 || index >= deviceRows_.size()) return;

    auto* row = deviceRows_[index];
    int selectedId = row->channelCombo->getSelectedId();
    int channel = selectedId - 1;  // itemId 1 = channel 0 (all), itemId 2 = channel 1, etc.
    row->channel = channel;

    juce::Logger::writeToLog("[MidiSettingsPanel] Channel changed for " + row->name +
                             " -> " + (channel == 0 ? juce::String("ALL") : juce::String(channel)));

    // Apply channel filter to MidiMappingService
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (reg) {
        auto* midiSvc = dynamic_cast<samplar::MidiMappingService*>(reg->get("midi.mapping"));
        if (midiSvc) {
            midiSvc->setMidiChannelFilter(channel);
            juce::Logger::writeToLog("[MidiSettingsPanel] MidiMappingService channel filter set to: " +
                                     (channel == 0 ? juce::String("ALL") : juce::String(channel)));
        }
    }
}

//==============================================================================
// LicenseInfoPanel Implementation
//==============================================================================

// Helper to get LicenseSystemService via ServiceLocator
static LicenseSystemService* getLicenseSystemService()
{
    auto* reg = samplar::ServiceLocator::getRegistry();
    if (!reg) return nullptr;

    return dynamic_cast<LicenseSystemService*>(reg->get("license"));
}

LicenseInfoPanel::LicenseInfoPanel()
{
    juce::Logger::writeToLog("[LicenseInfoPanel] Constructor");

    // Setup section labels
    auto setupSectionLabel = [this](juce::Label& label) {
        label.setFont(juce::Font(14.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, borderColor_);
        addAndMakeVisible(label);
    };

    setupSectionLabel(statusSectionLabel_);
    setupSectionLabel(appSectionLabel_);
    setupSectionLabel(userSectionLabel_);

    // Setup regular labels
    auto setupLabel = [this](juce::Label& label, bool isValue = false) {
        label.setFont(juce::Font(13.0f));
        label.setColour(juce::Label::textColourId, isValue ? textColor_ : dimTextColor_);
        addAndMakeVisible(label);
    };

    // License status
    setupLabel(statusLabel_);
    setupLabel(statusValue_, true);
    statusValue_.setFont(juce::Font(14.0f, juce::Font::bold));

    setupLabel(keyLabel_);
    setupLabel(keyValue_, true);

    setupLabel(hardwareLabel_);
    setupLabel(hardwareValue_, true);
    hardwareValue_.setFont(juce::Font(11.0f));

    copyHardwareBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a5a));
    copyHardwareBtn_.onClick = [this] { onCopyHardwareId(); };
    addAndMakeVisible(copyHardwareBtn_);

    // App info
    setupLabel(versionLabel_);
    setupLabel(versionValue_, true);

    setupLabel(supportLabel_);
    setupLabel(supportValue_, true);
    supportValue_.setColour(juce::Label::textColourId, borderColor_);

    // User info (functional - retrieved from license service)
    setupLabel(emailLabel_);
    setupLabel(emailValue_, true);
    emailValue_.setColour(juce::Label::textColourId, dimTextColor_);

    setupLabel(expiryLabel_);
    setupLabel(expiryValue_, true);

    setupLabel(typeLabel_);
    setupLabel(typeValue_, true);

    // Action buttons
    activateBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a5a3a));
    activateBtn_.onClick = [this] { onActivate(); };
    addAndMakeVisible(activateBtn_);

    deactivateBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a3a3a));
    deactivateBtn_.onClick = [this] { onDeactivate(); };
    addAndMakeVisible(deactivateBtn_);

    // Initial refresh
    refreshLicenseInfo();
}

void LicenseInfoPanel::paint(juce::Graphics& g)
{
    g.fillAll(bgColor_);

    auto bounds = getLocalBounds().reduced(5);

    // License Status section background
    auto statusSection = bounds.removeFromTop(130);
    g.setColour(sectionBgColor_);
    g.fillRoundedRectangle(statusSection.toFloat(), 5.0f);
    g.setColour(borderColor_.withAlpha(0.3f));
    g.drawRoundedRectangle(statusSection.toFloat(), 5.0f, 1.0f);

    bounds.removeFromTop(10);

    // App Info section background
    auto appSection = bounds.removeFromTop(80);
    g.setColour(sectionBgColor_);
    g.fillRoundedRectangle(appSection.toFloat(), 5.0f);
    g.setColour(borderColor_.withAlpha(0.3f));
    g.drawRoundedRectangle(appSection.toFloat(), 5.0f, 1.0f);

    bounds.removeFromTop(10);

    // User Info section background
    auto userSection = bounds.removeFromTop(100);
    g.setColour(sectionBgColor_);
    g.fillRoundedRectangle(userSection.toFloat(), 5.0f);
    g.setColour(borderColor_.withAlpha(0.3f));
    g.drawRoundedRectangle(userSection.toFloat(), 5.0f, 1.0f);
}

void LicenseInfoPanel::resized()
{
    auto bounds = getLocalBounds().reduced(5);

    // License Status section
    auto statusSection = bounds.removeFromTop(130).reduced(10);
    statusSectionLabel_.setBounds(statusSection.removeFromTop(22));

    auto statusRow = statusSection.removeFromTop(25);
    statusLabel_.setBounds(statusRow.removeFromLeft(100));
    statusValue_.setBounds(statusRow);

    auto keyRow = statusSection.removeFromTop(25);
    keyLabel_.setBounds(keyRow.removeFromLeft(100));
    keyValue_.setBounds(keyRow);

    auto hwRow = statusSection.removeFromTop(25);
    hardwareLabel_.setBounds(hwRow.removeFromLeft(100));
    copyHardwareBtn_.setBounds(hwRow.removeFromRight(50));
    hwRow.removeFromRight(5);
    hardwareValue_.setBounds(hwRow);

    bounds.removeFromTop(10);

    // App Info section
    auto appSection = bounds.removeFromTop(80).reduced(10);
    appSectionLabel_.setBounds(appSection.removeFromTop(22));

    auto versionRow = appSection.removeFromTop(25);
    versionLabel_.setBounds(versionRow.removeFromLeft(100));
    versionValue_.setBounds(versionRow);

    auto supportRow = appSection.removeFromTop(25);
    supportLabel_.setBounds(supportRow.removeFromLeft(100));
    supportValue_.setBounds(supportRow);

    bounds.removeFromTop(10);

    // User Info section
    auto userSection = bounds.removeFromTop(100).reduced(10);
    userSectionLabel_.setBounds(userSection.removeFromTop(22));

    auto emailRow = userSection.removeFromTop(25);
    emailLabel_.setBounds(emailRow.removeFromLeft(100));
    emailValue_.setBounds(emailRow);

    auto expiryRow = userSection.removeFromTop(25);
    expiryLabel_.setBounds(expiryRow.removeFromLeft(100));
    expiryValue_.setBounds(expiryRow);

    auto typeRow = userSection.removeFromTop(25);
    typeLabel_.setBounds(typeRow.removeFromLeft(100));
    typeValue_.setBounds(typeRow);

    bounds.removeFromTop(10);

    // Action buttons at bottom
    auto buttonRow = bounds.removeFromTop(35);
    activateBtn_.setBounds(buttonRow.removeFromLeft(120));
    buttonRow.removeFromLeft(10);
    deactivateBtn_.setBounds(buttonRow.removeFromLeft(100));
}

void LicenseInfoPanel::refreshLicenseInfo()
{
    juce::Logger::writeToLog("[LicenseInfoPanel] Refreshing license info");

    auto* licenseService = getLicenseSystemService();

    if (licenseService)
    {
        auto status = licenseService->getCurrentStatus();
        bool isLicensed = (status == LicenseSystemService::LicenseStatus::Valid);

        // Update status
        if (isLicensed)
        {
            statusValue_.setText("Active", juce::dontSendNotification);
            statusValue_.setColour(juce::Label::textColourId, activeColor_);
            activateBtn_.setVisible(false);
            deactivateBtn_.setVisible(true);
        }
        else if (status == LicenseSystemService::LicenseStatus::GracePeriodActive)
        {
            auto remainingMins = licenseService->getRemainingGraceTime().count();
            statusValue_.setText("Grace Period (" + juce::String(remainingMins / 60) + "h remaining)", juce::dontSendNotification);
            statusValue_.setColour(juce::Label::textColourId, juce::Colour(0xffffaa44));
            activateBtn_.setVisible(true);
            deactivateBtn_.setVisible(false);
        }
        else
        {
            statusValue_.setText("Not Licensed", juce::dontSendNotification);
            statusValue_.setColour(juce::Label::textColourId, inactiveColor_);
            activateBtn_.setVisible(true);
            deactivateBtn_.setVisible(false);
        }

        // Get license info for email, type, expiry
        auto licenseInfo = licenseService->getCurrentLicenseInfo();

        // Update email
        if (!licenseInfo.customerEmail.empty())
        {
            emailValue_.setText(juce::String(licenseInfo.customerEmail), juce::dontSendNotification);
            emailValue_.setColour(juce::Label::textColourId, textColor_);
        }
        else
        {
            emailValue_.setText("Not registered", juce::dontSendNotification);
            emailValue_.setColour(juce::Label::textColourId, dimTextColor_);
        }

        // Update license type
        if (!licenseInfo.licenseType.empty())
        {
            juce::String typeStr = juce::String(licenseInfo.licenseType);
            if (typeStr == "TRL") typeStr = "Trial";
            else if (typeStr == "STD") typeStr = "Standard";
            else if (typeStr == "PRO") typeStr = "Professional";
            else if (typeStr == "EDU") typeStr = "Education";
            else if (typeStr == "PROMO") typeStr = "Promotional";
            typeValue_.setText(typeStr, juce::dontSendNotification);
        }
        else
        {
            typeValue_.setText("Unknown", juce::dontSendNotification);
        }

        // Update expiry date
        auto expiryTime = licenseInfo.expiresAt;
        if (expiryTime != std::chrono::system_clock::time_point{})
        {
            auto time_t_val = std::chrono::system_clock::to_time_t(expiryTime);
            auto* tm = std::localtime(&time_t_val);
            char buffer[64];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm);
            expiryValue_.setText(juce::String(buffer), juce::dontSendNotification);
        }
        else
        {
            expiryValue_.setText("Never (Perpetual)", juce::dontSendNotification);
        }

        // License key
        if (isLicensed)
        {
            keyValue_.setText("****-****-****-****", juce::dontSendNotification);
        }
        else
        {
            keyValue_.setText("No active key", juce::dontSendNotification);
        }

        // Update hardware ID
        juce::String hwId = juce::String(licenseService->generateHardwareId());
        if (hwId.isNotEmpty())
        {
            hardwareValue_.setText(hwId, juce::dontSendNotification);
        }
        else
        {
            hardwareValue_.setText("Unknown", juce::dontSendNotification);
        }

        juce::Logger::writeToLog("[LicenseInfoPanel] Licensed: " + juce::String(isLicensed ? "Yes" : "No"));
    }
    else
    {
        statusValue_.setText("Service Unavailable", juce::dontSendNotification);
        statusValue_.setColour(juce::Label::textColourId, dimTextColor_);
        keyValue_.setText("N/A", juce::dontSendNotification);
        hardwareValue_.setText("N/A", juce::dontSendNotification);
        emailValue_.setText("N/A", juce::dontSendNotification);
        expiryValue_.setText("N/A", juce::dontSendNotification);
        typeValue_.setText("N/A", juce::dontSendNotification);
        activateBtn_.setVisible(true);
        deactivateBtn_.setVisible(false);

        juce::Logger::writeToLog("[LicenseInfoPanel] LicenseSystemService not available");
    }

    // Update version dynamically
    juce::String appVersion = "SAMPLAR";
    {
        juce::String ver = samplar::ServiceLocator::getAppVersion();
        if (ver.isNotEmpty() && ver != "unknown")
            appVersion += " v" + ver;
    }
    versionValue_.setText(appVersion, juce::dontSendNotification);
}

void LicenseInfoPanel::onCopyHardwareId()
{
    auto* licenseService = getLicenseSystemService();
    if (licenseService)
    {
        juce::String hwId = juce::String(licenseService->generateHardwareId());
        juce::SystemClipboard::copyTextToClipboard(hwId);
        juce::Logger::writeToLog("[LicenseInfoPanel] Hardware ID copied to clipboard");

        copyHardwareBtn_.setButtonText("Copied!");
        juce::Component::SafePointer<LicenseInfoPanel> safeThis(this);
        juce::Timer::callAfterDelay(1500, [safeThis] {
            if (safeThis != nullptr)
                safeThis->copyHardwareBtn_.setButtonText("Copy");
        });
    }
}

void LicenseInfoPanel::onActivate()
{
    juce::Logger::writeToLog("[LicenseInfoPanel] Opening activation dialog");

    auto* licenseService = getLicenseSystemService();
    if (licenseService)
    {
        auto* bus = samplar::ServiceLocator::getBus();
        if (bus)
        {
            // Launch activation dialog asynchronously instead of blocking with showModal()
            auto* dialog = new LicenseActivationDialog(*licenseService, *bus);
            juce::Component::SafePointer<LicenseInfoPanel> safeThis(this);

            dialog->onValidated = [safeThis]() {
                if (safeThis != nullptr)
                    safeThis->refreshLicenseInfo();
            };

            juce::DialogWindow::LaunchOptions launchOpts;
            launchOpts.dialogTitle = "License Activation";
            launchOpts.content.setOwned(dialog);
            launchOpts.dialogBackgroundColour = juce::Colour(0xff1a1a2e);
            launchOpts.escapeKeyTriggersCloseButton = true;
            launchOpts.useNativeTitleBar = false;
            launchOpts.resizable = false;
            launchOpts.componentToCentreAround = this;
            launchOpts.launchAsync();
        }
    }
}

void LicenseInfoPanel::onDeactivate()
{
    juce::Logger::writeToLog("[LicenseInfoPanel] Deactivation requested");

    auto* licenseService = getLicenseSystemService();
    if (!licenseService) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Error",
            "License service not available",
            "OK"
        );
        return;
    }

    juce::Component::SafePointer<LicenseInfoPanel> safeThis(this);

    auto options = juce::MessageBoxOptions()
        .withIconType(juce::AlertWindow::QuestionIcon)
        .withTitle("Deactivate License")
        .withMessage("Are you sure you want to deactivate your license on this machine?\n\n"
                     "This will free up an activation slot that can be used on another device.\n"
                     "You will need to reactivate to use the full features again.")
        .withButton("Deactivate")
        .withButton("Cancel");

    juce::AlertWindow::showAsync(options, [safeThis](int result) {
        if (result != 1 || safeThis == nullptr) return;

        juce::Logger::writeToLog("[LicenseInfoPanel] User confirmed deactivation");

        auto* svc = getLicenseSystemService();
        if (!svc) return;

        bool success = svc->deactivateLicense();

        if (success) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Deactivated",
                "License has been deactivated on this machine.\n\n"
                "You can reactivate at any time using your account credentials.",
                "OK"
            );
            safeThis->refreshLicenseInfo();
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Deactivation Failed",
                "Failed to deactivate: " + juce::String(svc->getLastResultMessage()),
                "OK"
            );
        }
    });
}

} // namespace samplar
