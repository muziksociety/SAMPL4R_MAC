#include "UpdateDialog.h"

namespace samplar {

//==============================================================================
// UpdateDialog Implementation
//==============================================================================

UpdateDialog::UpdateDialog(const UpdateCheckService::UpdateInfo& updateInfo,
                           const UpdateCheckService::LicenseReminder& licenseReminder)
    : updateInfo_(updateInfo),
      licenseReminder_(licenseReminder)
{
    juce::Logger::writeToLog("[UpdateDialog] === CONSTRUCTOR START ===");
    juce::Logger::writeToLog("[UpdateDialog]   updateAvailable: " + juce::String(updateInfo_.updateAvailable ? "true" : "false"));
    juce::Logger::writeToLog("[UpdateDialog]   latestVersion: " + juce::String(updateInfo_.latestVersion));
    juce::Logger::writeToLog("[UpdateDialog]   changes count: " + juce::String((int)updateInfo_.changes.size()));
    juce::Logger::writeToLog("[UpdateDialog]   licenseReminder status: " + juce::String(licenseReminder_.status));
    juce::Logger::writeToLog("[UpdateDialog]   licenseReminder message: " + juce::String(licenseReminder_.message));

    // Style updateBtn_ - green accent
    updateBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff00CF8A));
    updateBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    updateBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    updateBtn_.onClick = [this] {
        juce::Logger::writeToLog("[UpdateDialog] Update Now clicked");
        if (onUpdateNow)
            onUpdateNow();
    };
    addAndMakeVisible(updateBtn_);

    // Style laterBtn_ - dark with grey text
    laterBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    laterBtn_.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffaaaaaa));
    laterBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
    laterBtn_.onClick = [this] {
        juce::Logger::writeToLog("[UpdateDialog] Later clicked");
        if (onDismiss)
            onDismiss();
    };
    addAndMakeVisible(laterBtn_);

    // Style upgradeBtn_ - orange accent
    upgradeBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffFF8800));
    upgradeBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    upgradeBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    upgradeBtn_.onClick = [this] {
        juce::Logger::writeToLog("[UpdateDialog] Upgrade to PRO clicked - opening browser");
        juce::URL("https://samplemonsta.com/portal").launchInDefaultBrowser();
    };

    // Only show upgrade button for trial or expired
    bool showUpgrade = (licenseReminder_.status == "trial" || licenseReminder_.status == "expired");
    juce::Logger::writeToLog("[UpdateDialog]   showUpgrade: " + juce::String(showUpgrade ? "true" : "false"));
    if (showUpgrade)
        addAndMakeVisible(upgradeBtn_);

    setSize(380, 320);

    juce::Logger::writeToLog("[UpdateDialog] === CONSTRUCTOR END ===");
}

void UpdateDialog::setLicenseOnlyMode(bool licenseOnly)
{
    juce::Logger::writeToLog("[UpdateDialog] === setLicenseOnlyMode START ===");
    juce::Logger::writeToLog("[UpdateDialog]   licenseOnly: " + juce::String(licenseOnly ? "true" : "false"));
    juce::Logger::writeToLog("[UpdateDialog]   previous licenseOnlyMode_: " + juce::String(licenseOnlyMode_ ? "true" : "false"));

    licenseOnlyMode_ = licenseOnly;

    if (licenseOnly)
    {
        juce::Logger::writeToLog("[UpdateDialog]   Decision: license-only mode - hiding updateBtn_, resizing to 380x180");
        updateBtn_.setVisible(false);
        setSize(380, 180);
    }
    else
    {
        juce::Logger::writeToLog("[UpdateDialog]   Decision: normal mode - showing updateBtn_, resizing to 380x320");
        updateBtn_.setVisible(true);
        setSize(380, 320);
    }

    resized();
    repaint();

    juce::Logger::writeToLog("[UpdateDialog] === setLicenseOnlyMode END ===");
}

void UpdateDialog::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background rounded rectangle
    g.setColour(juce::Colour(0xff1a1a2e));
    g.fillRoundedRectangle(bounds, 12.0f);

    // Border
    g.setColour(juce::Colour(0xff333355));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

    auto contentArea = getLocalBounds().reduced(24);
    int yPos = contentArea.getY();

    // --- Update section (when not license-only and update available) ---
    if (!licenseOnlyMode_ && updateInfo_.updateAvailable)
    {
        // Title: "What's New in v{version}"
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        juce::String title = "What's New in v" + juce::String(updateInfo_.latestVersion);
        g.drawText(title, contentArea.getX(), yPos, contentArea.getWidth(), 28,
                   juce::Justification::centredLeft);
        yPos += 36;

        // Change items as bullet points
        g.setFont(juce::Font(13.0f));
        g.setColour(juce::Colour(0xffcccccc));

        for (const auto& change : updateInfo_.changes)
        {
            // UTF-8 bullet character
            juce::String bulletLine = juce::String::fromUTF8("\xe2\x80\xa2 ") + juce::String(change);
            g.drawText(bulletLine, contentArea.getX() + 8, yPos, contentArea.getWidth() - 8, 22,
                       juce::Justification::centredLeft);
            yPos += 22;
        }

        yPos += 8;
    }

    // --- License reminder section ---
    if (!licenseReminder_.message.empty())
    {
        // Draw separator line (only if update section was shown above)
        if (!licenseOnlyMode_ && updateInfo_.updateAvailable)
        {
            g.setColour(juce::Colour(0xff333355));
            int separatorY = getHeight() - 80;
            g.drawHorizontalLine(separatorY, (float)contentArea.getX(), (float)contentArea.getRight());
            yPos = separatorY + 8;
        }
        else if (licenseOnlyMode_)
        {
            yPos = contentArea.getY() + 8;
        }

        // License message color based on status
        if (licenseReminder_.status == "expired")
        {
            g.setColour(juce::Colour(0xffFF4444));
        }
        else
        {
            // trial or other
            g.setColour(juce::Colour(0xffFFAA00));
        }

        g.setFont(juce::Font(12.0f));
        g.drawFittedText(juce::String(licenseReminder_.message),
                         contentArea.getX(), yPos, contentArea.getWidth(), 40,
                         juce::Justification::centredLeft, 2);
    }
}

void UpdateDialog::resized()
{
    auto bounds = getLocalBounds().reduced(24);
    int btnHeight = 30;
    int btnY = getHeight() - 24 - btnHeight;

    // laterBtn_ - right aligned, 100px wide
    laterBtn_.setBounds(bounds.getRight() - 100, btnY, 100, btnHeight);

    if (licenseOnlyMode_)
    {
        // In license-only mode: upgradeBtn_ replaces updateBtn_
        upgradeBtn_.setBounds(bounds.getRight() - 100 - 10 - 140, btnY, 140, btnHeight);
    }
    else
    {
        // updateBtn_ - to the left of Later, 120px wide, 10px gap
        updateBtn_.setBounds(bounds.getRight() - 100 - 10 - 120, btnY, 120, btnHeight);
        // upgradeBtn_ positioned if visible (below or beside)
        if (upgradeBtn_.isVisible())
        {
            upgradeBtn_.setBounds(bounds.getX(), btnY, 140, btnHeight);
        }
    }
}

} // namespace samplar
