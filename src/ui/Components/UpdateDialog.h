#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../services/UpdateCheckService.h"
#include <functional>

namespace samplar {

class UpdateDialog : public juce::Component {
public:
    UpdateDialog(const UpdateCheckService::UpdateInfo& updateInfo,
                 const UpdateCheckService::LicenseReminder& licenseReminder);

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onDismiss;
    std::function<void()> onUpdateNow;

    void setLicenseOnlyMode(bool licenseOnly);

private:
    UpdateCheckService::UpdateInfo updateInfo_;
    UpdateCheckService::LicenseReminder licenseReminder_;
    bool licenseOnlyMode_{false};

    juce::TextButton updateBtn_{"Update Now"};
    juce::TextButton laterBtn_{"Later"};
    juce::TextButton upgradeBtn_{"Upgrade to PRO"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateDialog)
};

} // namespace samplar
