#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include <vector>

namespace samplar {

class LicenseSystemService;

class UpdateCheckService : public IService, private juce::Timer {
public:
    explicit UpdateCheckService(EventBus& eventBus);
    ~UpdateCheckService() override;

    void initialise() override;
    void shutdown() override;

    struct UpdateInfo {
        bool updateAvailable{false};
        std::string latestVersion;
        std::string currentVersion;
        std::string downloadUrl;
        std::string releaseDate;
        std::vector<std::string> changes;
    };

    struct LicenseReminder {
        std::string status;
        std::string type;
        int daysRemaining{-1};
        std::string message;
    };

    const UpdateInfo& getUpdateInfo() const { return updateInfo_; }
    const LicenseReminder& getLicenseReminder() const { return licenseReminder_; }
    bool wasDismissed(const std::string& version) const;
    void dismissVersion(const std::string& version);

private:
    void timerCallback() override;
    void performCheck();
    void parseResponse(const juce::String& responseBody);
    std::string getHardwareId() const;

    EventBus& eventBus_;
    UpdateInfo updateInfo_;
    LicenseReminder licenseReminder_;

    std::unique_ptr<juce::PropertiesFile> propsFile_;
    static constexpr int startupDelayMs_ = 5000;
};

} // namespace samplar
