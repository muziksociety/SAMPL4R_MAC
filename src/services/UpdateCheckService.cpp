#include "UpdateCheckService.h"
#include "../Version.h"
#include "../app/App.h"
#include "../app/ServiceRegistry.h"
#include "../app/ServiceLocator.h"
#include "LicenseSystemService.h"

#include <thread>

namespace samplar {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

UpdateCheckService::UpdateCheckService(EventBus& eventBus)
    : eventBus_(eventBus)
{
    juce::Logger::writeToLog("[UpdateCheckService] === Constructor START ===");

    // Set up a PropertiesFile for persisting dismissed-version info
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Samplar";
    opts.folderName          = "Samplar";
    opts.filenameSuffix       = ".update_prefs";
    opts.osxLibrarySubFolder = "Application Support";

    propsFile_ = std::make_unique<juce::PropertiesFile>(opts);
    juce::Logger::writeToLog("[UpdateCheckService]   PropertiesFile created at: "
                             + propsFile_->getFile().getFullPathName());

    juce::Logger::writeToLog("[UpdateCheckService] === Constructor END ===");
}

UpdateCheckService::~UpdateCheckService()
{
    juce::Logger::writeToLog("[UpdateCheckService] === Destructor ===");
    stopTimer();
}

// ---------------------------------------------------------------------------
// IService lifecycle
// ---------------------------------------------------------------------------

void UpdateCheckService::initialise()
{
    juce::Logger::writeToLog("[UpdateCheckService] === initialise START ===");
    juce::Logger::writeToLog("[UpdateCheckService]   Scheduling update check in "
                             + juce::String(startupDelayMs_) + " ms");

    startTimer(startupDelayMs_);

    juce::Logger::writeToLog("[UpdateCheckService] === initialise END ===");
}

void UpdateCheckService::shutdown()
{
    juce::Logger::writeToLog("[UpdateCheckService] === shutdown START ===");
    stopTimer();
    juce::Logger::writeToLog("[UpdateCheckService] === shutdown END ===");
}

// ---------------------------------------------------------------------------
// Timer callback  (fires once after startup delay)
// ---------------------------------------------------------------------------

void UpdateCheckService::timerCallback()
{
    juce::Logger::writeToLog("[UpdateCheckService] === timerCallback START ===");
    stopTimer();  // one-shot

    // Launch the network check on a detached background thread so we never
    // block the message thread.
    std::thread([this]() {
        juce::Logger::writeToLog("[UpdateCheckService]   Background thread started for performCheck()");
        performCheck();
    }).detach();

    juce::Logger::writeToLog("[UpdateCheckService] === timerCallback END ===");
}

// ---------------------------------------------------------------------------
// Network check  (runs on background thread)
// ---------------------------------------------------------------------------

void UpdateCheckService::performCheck()
{
    juce::Logger::writeToLog("[UpdateCheckService] === performCheck START ===");

    // 1. Current version
    juce::String currentVersion = samplar::kAppVersion;
    updateInfo_.currentVersion = currentVersion.toStdString();
    juce::Logger::writeToLog("[UpdateCheckService]   currentVersion: " + currentVersion);

    // 2. Hardware ID (may be empty if license service unavailable)
    std::string hwId = getHardwareId();
    juce::Logger::writeToLog("[UpdateCheckService]   hardwareId: "
                             + juce::String(hwId.empty() ? "(empty)" : hwId));

    // 3. Build the URL
    juce::String baseUrl = "https://samplemonsta.com/api/update/check";
    juce::URL url(baseUrl);
    url = url.withParameter("current_version", currentVersion)
             .withParameter("product", "samplar")
             .withParameter("hardware_id", juce::String(hwId));

    juce::Logger::writeToLog("[UpdateCheckService]   Request URL: " + url.toString(true));

    // 4. Perform the HTTP GET
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(8000));

    if (stream == nullptr)
    {
        juce::Logger::writeToLog("[UpdateCheckService] ERROR: Failed to create input stream (network error or timeout)");
        juce::Logger::writeToLog("[UpdateCheckService] === performCheck END (network failure) ===");
        return;
    }

    juce::String responseBody = stream->readEntireStreamAsString();
    juce::Logger::writeToLog("[UpdateCheckService]   Response length: " + juce::String(responseBody.length()));
    juce::Logger::writeToLog("[UpdateCheckService]   Response body (first 500 chars): "
                             + responseBody.substring(0, 500));

    // 5. Parse
    parseResponse(responseBody);

    // 6. Publish events on the message thread
    juce::MessageManager::callAsync([this]() {
        juce::Logger::writeToLog("[UpdateCheckService] === Publishing events on message thread ===");

        if (updateInfo_.updateAvailable)
        {
            juce::Logger::writeToLog("[UpdateCheckService]   Publishing 'update.available'");
            juce::Logger::writeToLog("[UpdateCheckService]     latestVersion: "
                                     + juce::String(updateInfo_.latestVersion));
            juce::Logger::writeToLog("[UpdateCheckService]     downloadUrl: "
                                     + juce::String(updateInfo_.downloadUrl));
            juce::Logger::writeToLog("[UpdateCheckService]     releaseDate: "
                                     + juce::String(updateInfo_.releaseDate));

            auto* obj = new juce::DynamicObject();
            obj->setProperty("version",     juce::String(updateInfo_.latestVersion));
            obj->setProperty("downloadUrl", juce::String(updateInfo_.downloadUrl));
            obj->setProperty("releaseDate", juce::String(updateInfo_.releaseDate));

            juce::String changesList;
            for (size_t i = 0; i < updateInfo_.changes.size(); ++i)
            {
                if (i > 0) changesList += "\n";
                changesList += juce::String(updateInfo_.changes[i]);
            }
            obj->setProperty("changes", changesList);

            eventBus_.publish("update.available", juce::var(obj));
            juce::Logger::writeToLog("[UpdateCheckService]   'update.available' published");
        }
        else
        {
            juce::Logger::writeToLog("[UpdateCheckService]   No update available; skipping 'update.available' event");
        }

        if (!licenseReminder_.message.empty())
        {
            juce::Logger::writeToLog("[UpdateCheckService]   Publishing 'license.status.reminder'");
            juce::Logger::writeToLog("[UpdateCheckService]     status: "
                                     + juce::String(licenseReminder_.status));
            juce::Logger::writeToLog("[UpdateCheckService]     type: "
                                     + juce::String(licenseReminder_.type));
            juce::Logger::writeToLog("[UpdateCheckService]     daysRemaining: "
                                     + juce::String(licenseReminder_.daysRemaining));
            juce::Logger::writeToLog("[UpdateCheckService]     message: "
                                     + juce::String(licenseReminder_.message));

            auto* obj = new juce::DynamicObject();
            obj->setProperty("status",        juce::String(licenseReminder_.status));
            obj->setProperty("type",          juce::String(licenseReminder_.type));
            obj->setProperty("daysRemaining", licenseReminder_.daysRemaining);
            obj->setProperty("message",       juce::String(licenseReminder_.message));

            eventBus_.publish("license.status.reminder", juce::var(obj));
            juce::Logger::writeToLog("[UpdateCheckService]   'license.status.reminder' published");
        }
        else
        {
            juce::Logger::writeToLog("[UpdateCheckService]   No license reminder; skipping event");
        }

        juce::Logger::writeToLog("[UpdateCheckService] === Event publishing complete ===");
    });

    juce::Logger::writeToLog("[UpdateCheckService] === performCheck END ===");
}

// ---------------------------------------------------------------------------
// JSON response parsing
// ---------------------------------------------------------------------------

void UpdateCheckService::parseResponse(const juce::String& responseBody)
{
    juce::Logger::writeToLog("[UpdateCheckService] === parseResponse START ===");

    auto parsed = juce::JSON::parse(responseBody);

    if (parsed.isVoid())
    {
        juce::Logger::writeToLog("[UpdateCheckService] ERROR: JSON parse returned void (invalid JSON)");
        juce::Logger::writeToLog("[UpdateCheckService] === parseResponse END (parse error) ===");
        return;
    }

    // --- Update info ---
    if (auto* updateObj = parsed.getDynamicObject())
    {
        if (updateObj->hasProperty("update_available"))
        {
            updateInfo_.updateAvailable = (bool)parsed["update_available"];
            juce::Logger::writeToLog("[UpdateCheckService]   update_available: "
                                     + juce::String(updateInfo_.updateAvailable ? "true" : "false"));
        }

        if (updateObj->hasProperty("latest_version"))
        {
            updateInfo_.latestVersion = parsed["latest_version"].toString().toStdString();
            juce::Logger::writeToLog("[UpdateCheckService]   latest_version: "
                                     + juce::String(updateInfo_.latestVersion));
        }

        if (updateObj->hasProperty("download_url"))
        {
            updateInfo_.downloadUrl = parsed["download_url"].toString().toStdString();
            juce::Logger::writeToLog("[UpdateCheckService]   download_url: "
                                     + juce::String(updateInfo_.downloadUrl));
        }

        if (updateObj->hasProperty("release_date"))
        {
            updateInfo_.releaseDate = parsed["release_date"].toString().toStdString();
            juce::Logger::writeToLog("[UpdateCheckService]   release_date: "
                                     + juce::String(updateInfo_.releaseDate));
        }

        if (updateObj->hasProperty("changes"))
        {
            updateInfo_.changes.clear();
            auto changesVar = parsed["changes"];
            if (auto* changesArray = changesVar.getArray())
            {
                for (const auto& item : *changesArray)
                {
                    updateInfo_.changes.push_back(item.toString().toStdString());
                }
                juce::Logger::writeToLog("[UpdateCheckService]   changes count: "
                                         + juce::String((int)updateInfo_.changes.size()));
            }
        }

        // --- License reminder ---
        if (updateObj->hasProperty("license_reminder"))
        {
            auto reminderVar = parsed["license_reminder"];
            if (auto* reminderObj = reminderVar.getDynamicObject())
            {
                if (reminderObj->hasProperty("status"))
                    licenseReminder_.status = reminderVar["status"].toString().toStdString();
                if (reminderObj->hasProperty("type"))
                    licenseReminder_.type = reminderVar["type"].toString().toStdString();
                if (reminderObj->hasProperty("days_remaining"))
                    licenseReminder_.daysRemaining = (int)reminderVar["days_remaining"];
                if (reminderObj->hasProperty("message"))
                    licenseReminder_.message = reminderVar["message"].toString().toStdString();

                juce::Logger::writeToLog("[UpdateCheckService]   license_reminder.status: "
                                         + juce::String(licenseReminder_.status));
                juce::Logger::writeToLog("[UpdateCheckService]   license_reminder.daysRemaining: "
                                         + juce::String(licenseReminder_.daysRemaining));
                juce::Logger::writeToLog("[UpdateCheckService]   license_reminder.message: "
                                         + juce::String(licenseReminder_.message));
            }
        }
    }
    else
    {
        juce::Logger::writeToLog("[UpdateCheckService] WARNING: Response is not a JSON object");
    }

    juce::Logger::writeToLog("[UpdateCheckService] === parseResponse END ===");
}

// ---------------------------------------------------------------------------
// Hardware ID helper
// ---------------------------------------------------------------------------

std::string UpdateCheckService::getHardwareId() const
{
    juce::Logger::writeToLog("[UpdateCheckService] === getHardwareId START ===");

    auto* registry = samplar::ServiceLocator::getRegistry();
    if (registry == nullptr)
    {
        juce::Logger::writeToLog("[UpdateCheckService] WARNING: ServiceLocator registry is null");
        juce::Logger::writeToLog("[UpdateCheckService] === getHardwareId END (no registry) ===");
        return {};
    }

    auto* licenseSvc = dynamic_cast<samplar::LicenseSystemService*>(
        registry->get("license"));

    if (licenseSvc == nullptr)
    {
        juce::Logger::writeToLog("[UpdateCheckService] WARNING: LicenseSystemService not found");
        juce::Logger::writeToLog("[UpdateCheckService] === getHardwareId END (no license svc) ===");
        return {};
    }

    std::string hwId = licenseSvc->generateHardwareId();
    juce::Logger::writeToLog("[UpdateCheckService]   Hardware ID obtained (length "
                             + juce::String((int)hwId.size()) + ")");
    juce::Logger::writeToLog("[UpdateCheckService] === getHardwareId END ===");
    return hwId;
}

// ---------------------------------------------------------------------------
// Dismiss / wasDismissed  (persisted via PropertiesFile)
// ---------------------------------------------------------------------------

bool UpdateCheckService::wasDismissed(const std::string& version) const
{
    juce::Logger::writeToLog("[UpdateCheckService] === wasDismissed START ===");
    juce::Logger::writeToLog("[UpdateCheckService]   version: " + juce::String(version));

    if (propsFile_ == nullptr)
    {
        juce::Logger::writeToLog("[UpdateCheckService] WARNING: propsFile_ is null");
        juce::Logger::writeToLog("[UpdateCheckService] === wasDismissed END (false) ===");
        return false;
    }

    juce::String dismissed = propsFile_->getValue("dismissed_version", "");
    bool result = (dismissed.toStdString() == version);

    juce::Logger::writeToLog("[UpdateCheckService]   dismissed_version in prefs: " + dismissed);
    juce::Logger::writeToLog("[UpdateCheckService]   result: " + juce::String(result ? "true" : "false"));
    juce::Logger::writeToLog("[UpdateCheckService] === wasDismissed END ===");
    return result;
}

void UpdateCheckService::dismissVersion(const std::string& version)
{
    juce::Logger::writeToLog("[UpdateCheckService] === dismissVersion START ===");
    juce::Logger::writeToLog("[UpdateCheckService]   version: " + juce::String(version));

    if (propsFile_ == nullptr)
    {
        juce::Logger::writeToLog("[UpdateCheckService] ERROR: propsFile_ is null, cannot dismiss");
        juce::Logger::writeToLog("[UpdateCheckService] === dismissVersion END (error) ===");
        return;
    }

    propsFile_->setValue("dismissed_version", juce::String(version));
    propsFile_->saveIfNeeded();

    juce::Logger::writeToLog("[UpdateCheckService]   Version dismissed and saved");
    juce::Logger::writeToLog("[UpdateCheckService] === dismissVersion END ===");
}

} // namespace samplar
