#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace samplar {

// Forward declarations
class AudioSettingsPanel;
class MidiSettingsPanel;
class LicenseInfoPanel;

/**
 * SettingsDialog - Main settings dialog with tabbed interface
 *
 * Tabs (functional only):
 * - Audio: Audio device settings
 * - MIDI: MIDI device selection (standalone only)
 * - License: License information and activation
 */
class SettingsDialog : public juce::Component {
public:
    SettingsDialog();
    ~SettingsDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Static method to show dialog
    static void showDialog(juce::Component* parent);

private:
    // Title bar
    juce::Label titleLabel_{ {}, "SETTINGS" };
    juce::TextButton closeBtn_{ "X" };

    // Tab buttons (3 tabs — MIDI only visible in standalone)
    juce::TextButton audioTab_{ "Audio" };
    juce::TextButton midiTab_{ "MIDI" };
    juce::TextButton licenseTab_{ "License" };

    // Tab content panels
    std::unique_ptr<AudioSettingsPanel> audioPanel_;
    std::unique_ptr<MidiSettingsPanel> midiPanel_;
    std::unique_ptr<LicenseInfoPanel> licensePanel_;

    bool standaloneMode_ = false;  // true when running as standalone app

    // Footer buttons
    juce::TextButton closeFooterBtn_{ "Close" };

    // State
    int currentTab_ = 0;  // Default to Audio tab

    // Methods
    void switchTab(int tabIndex);
    void onClose();

    // Colors
    juce::Colour bgColor_{ 0xff1a1a2e };
    juce::Colour headerColor_{ 0xff252545 };
    juce::Colour accentColor_{ 0xff00ffff };
    juce::Colour textColor_{ 0xffe0e0e0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};

/**
 * AudioSettingsPanel - Audio device settings (functional only)
 *
 * Settings:
 * - Output device selection
 * - Buffer size configuration
 * - Sample rate display (read-only)
 */
class AudioSettingsPanel : public juce::Component {
public:
    AudioSettingsPanel();
    ~AudioSettingsPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshDeviceList();

private:
    // Device settings
    juce::Label deviceLabel_{ {}, "Output Device" };
    juce::ComboBox deviceCombo_;

    juce::Label bufferLabel_{ {}, "Buffer Size" };
    juce::ComboBox bufferCombo_;

    juce::Label sampleRateLabel_{ {}, "Sample Rate" };
    juce::Label sampleRateValue_{ {}, "---" };

    // Help text
    juce::Label bufferHelpLabel_{ {}, "Lower = less latency, higher CPU. Higher = more stable." };
    juce::Label deviceHelpLabel_{ {}, "Select your audio output device for sample preview." };

    // Colors
    juce::Colour bgColor_{ 0xff1a1a2e };
    juce::Colour sectionBgColor_{ 0xff252540 };
    juce::Colour borderColor_{ 0xff00ffff };
    juce::Colour textColor_{ 0xffe0e0e0 };
    juce::Colour dimTextColor_{ 0xff808090 };

    void onDeviceChanged();
    void onBufferChanged();
    void updateSampleRateDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsPanel)
};

/**
 * MidiSettingsPanel - MIDI device selection for standalone mode
 *
 * Features:
 * - Lists available MIDI input devices
 * - Enable/disable individual devices
 * - Refresh button to re-scan for devices
 * - Shows "managed by DAW" message in VST3 mode
 */
class MidiSettingsPanel : public juce::Component,
                          public juce::Timer {
public:
    MidiSettingsPanel();
    ~MidiSettingsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void refreshDeviceList();

private:
    // Header
    juce::Label headerLabel_{ {}, "MIDI INPUT DEVICES" };
    juce::Label helpLabel_{ {}, "Select your MIDI controller. Enable a device to receive MIDI input in the Sample Browser." };

    // Device list area
    struct MidiDeviceRow {
        juce::String name;
        juce::String identifier;
        std::unique_ptr<juce::ToggleButton> enableBtn;
        std::unique_ptr<juce::ComboBox> channelCombo;  // MIDI channel: All, 1-16
        bool enabled = false;
        int channel = 0;  // 0 = all, 1-16 = specific
    };
    juce::OwnedArray<MidiDeviceRow> deviceRows_;
    int lastMidiDeviceCount_ = -1;  // for hot-plug detection

    // Controls
    juce::TextButton refreshBtn_{ "Refresh Devices" };
    juce::ToggleButton computerKeyboardBtn_{ "Computer Keyboard MIDI" };
    juce::Label statusLabel_{ {}, "No MIDI devices detected" };

    // Viewport for scrollable device list
    juce::Viewport viewport_;
    std::unique_ptr<juce::Component> deviceListComponent_;

    // Colors
    juce::Colour bgColor_{ 0xff1a1a2e };
    juce::Colour sectionBgColor_{ 0xff252540 };
    juce::Colour borderColor_{ 0xff00ffff };
    juce::Colour textColor_{ 0xffe0e0e0 };
    juce::Colour dimTextColor_{ 0xff808090 };
    juce::Colour enabledColor_{ 0xff44ff44 };

    void onDeviceToggled(int index);
    void onChannelChanged(int index);
    void rebuildDeviceList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSettingsPanel)
};

/**
 * LicenseInfoPanel - License information display (functional only)
 *
 * Displays:
 * - License status (Active/Not Licensed)
 * - Hardware ID (for support)
 * - License key (masked)
 * - App version
 * - Support contact
 * - Activate/Deactivate buttons
 */
class LicenseInfoPanel : public juce::Component {
public:
    LicenseInfoPanel();
    ~LicenseInfoPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshLicenseInfo();

private:
    // License status section
    juce::Label statusSectionLabel_{ {}, "LICENSE STATUS" };
    juce::Label statusLabel_{ {}, "Status" };
    juce::Label statusValue_{ {}, "Checking..." };

    // License key display (masked)
    juce::Label keyLabel_{ {}, "License Key" };
    juce::Label keyValue_{ {}, "****-****-****-****" };

    // Hardware ID section
    juce::Label hardwareLabel_{ {}, "Hardware ID" };
    juce::Label hardwareValue_{ {}, "Loading..." };
    juce::TextButton copyHardwareBtn_{ "Copy" };

    // App info section
    juce::Label appSectionLabel_{ {}, "APPLICATION" };
    juce::Label versionLabel_{ {}, "Version" };
    juce::Label versionValue_{ {}, "SAMPLAR v1.0" };

    juce::Label supportLabel_{ {}, "Support" };
    juce::Label supportValue_{ {}, "support@samplemonsta.com" };

    // User info section (functional - retrieved from license service)
    juce::Label userSectionLabel_{ {}, "USER INFORMATION" };
    juce::Label emailLabel_{ {}, "Email" };
    juce::Label emailValue_{ {}, "Not registered" };
    juce::Label expiryLabel_{ {}, "Expires" };
    juce::Label expiryValue_{ {}, "Never (Perpetual)" };
    juce::Label typeLabel_{ {}, "License Type" };
    juce::Label typeValue_{ {}, "Standard" };

    // Actions
    juce::TextButton activateBtn_{ "Activate License" };
    juce::TextButton deactivateBtn_{ "Deactivate" };

    // Colors
    juce::Colour bgColor_{ 0xff1a1a2e };
    juce::Colour sectionBgColor_{ 0xff252540 };
    juce::Colour borderColor_{ 0xff00ffff };
    juce::Colour textColor_{ 0xffe0e0e0 };
    juce::Colour dimTextColor_{ 0xff808090 };
    juce::Colour activeColor_{ 0xff44ff44 };
    juce::Colour inactiveColor_{ 0xffff4444 };

    void onCopyHardwareId();
    void onActivate();
    void onDeactivate();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseInfoPanel)
};

/**
 * SettingsButton - Circular button with gear icon
 */
class SettingsButton : public juce::Button {
public:
    SettingsButton() : Button("Settings") {
        setTooltip("Settings");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void clicked() override {
        if (auto* parent = getTopLevelComponent()) {
            SettingsDialog::showDialog(parent);
        }
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        auto baseColor = juce::Colour::fromString("ff9966FF");  // Purple
        auto color = down ? baseColor.brighter(0.2f) : (over ? baseColor.brighter(0.15f) : baseColor);

        // Circle outline only (no fill)
        g.setColour(color);
        g.drawEllipse(r, 1.5f);

        // Gear icon (simplified - S letter)
        g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));
        g.setFont(juce::Font(r.getWidth() * 0.45f, juce::Font::bold));
        g.drawText("S", r.toNearestInt(), juce::Justification::centred);
    }
};

} // namespace samplar
