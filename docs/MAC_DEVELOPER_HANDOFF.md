# SAMPLAR - Update Notification System
## Mac Developer Handoff Document

**Date:** 2026-03-08
**Platform:** Windows (reference implementation)
**Target:** macOS port

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Architecture Flow](#2-architecture-flow)
3. [Server-Side API](#3-server-side-api)
4. [Version Management (CMake + Git Tags)](#4-version-management)
5. [UpdateCheckService](#5-updatecheckservice)
6. [NotificationButton Component](#6-notificationbutton-component)
7. [UpdateDialog Component](#7-updatedialog-component)
8. [MainWindow Integration](#8-mainwindow-integration)
9. [Service Registration](#9-service-registration)
10. [CMakeLists.txt Entries](#10-cmakeliststxt-entries)
11. [File Manifest](#11-file-manifest)
12. [macOS-Specific Notes](#12-macos-specific-notes)

---

## 1. System Overview

The update notification system checks `samplemonsta.com/api/update/check` on startup (after a 5-second delay) to determine if a newer version is available and whether the user's license needs attention. Results are displayed via a title-bar notification button and an optional dialog.

**Key behaviors:**
- Notification button is **always visible** in the top-right corner of the title bar
- In **UpToDate** mode: subtle gray "i" icon, clicking shows a simple "You're up to date" alert
- In **UpdateAvailable** mode: green pulsing circle with up-arrow icon
- In **TrialExpiring** mode: amber pulsing circle with exclamation icon
- Users can dismiss updates (persisted via `juce::PropertiesFile`)
- Dismissed updates still show the green button but don't auto-open the dialog

---

## 2. Architecture Flow

```
App Startup
    |
    v  (5 second delay)
UpdateCheckService.timerCallback()
    |
    v  (background thread)
GET https://samplemonsta.com/api/update/check
    ?current_version=1.0.2
    &product=sample_monsta
    &hardware_id=<from LicenseSystemService>
    |
    v
Server responds with JSON:
  - update_available, latest_version, download_url, changes[]
  - license_reminder: { status, type, days_remaining, message }
    |
    v  (message thread via MessageManager::callAsync)
EventBus publishes:
  - "update.available"         -> handleUpdateAvailable()
  - "license.status.reminder"  -> handleLicenseReminder()
    |
    v
NotificationButton mode set:
  - UpdateAvailable (green pulse)
  - TrialExpiring (amber pulse)
  - UpToDate (gray steady - default)
    |
    v
If not previously dismissed -> auto-show UpdateDialog
```

---

## 3. Server-Side API

**Endpoint:** `GET /api/update/check`
**Server:** `https://samplemonsta.com` (Render-hosted)
**Source:** `b:/SANTI/license_server/plugins/update_checker/__init__.py`

### Request Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `current_version` | string | Yes | e.g. "1.0.2" or "v1.0.2" |
| `product` | string | Yes | Always "sample_monsta" |
| `hardware_id` | string | No | For license status lookup |

### Response JSON

```json
{
  "update_available": true,
  "latest_version": "1.0.3",
  "current_version": "1.0.2",
  "download_url": "https://samplemonsta.com/portal",
  "release_date": "2026-03-08",
  "changes": [
    "Multiple Song Processing",
    "Fixes to stability & crashing",
    "macOS: Drag and drop deprecated, switched to Browser",
    "Fixes to safe audio threading"
  ],
  "license_status": {
    "status": "active",
    "type": "PRO",
    "days_remaining": 365,
    "message": ""
  }
}
```

### Releases Data (Single Source of Truth)

**File:** `license_server/data/releases.json`

```json
{
  "products": {
    "sample_monsta": {
      "latest_version": "1.0.2",
      "download_url": "https://samplemonsta.com/portal",
      "releases": [
        {
          "version": "1.0.2",
          "date": "2026-02-19",
          "changes": [
            "Multiple Song Processing",
            "Fixes to stability & crashing",
            "macOS: Drag and drop deprecated, switched to Browser",
            "Fixes to safe audio threading"
          ]
        }
      ]
    }
  }
}
```

**To release a new version:** Update `releases.json` with the new version entry and deploy the license server.

### License Status Values

| Status | Meaning |
|--------|---------|
| `"active"` | License valid, no action needed |
| `"expired"` | License/trial has ended |
| `"unknown"` | No hardware_id provided or no license found |

### Trial Detection

License types `"TRL"` or `"TRIAL"` trigger special messaging:
- 1-7 days remaining: "You have X days left on your free trial"
- 0 days: "Your free trial has expired"

---

## 4. Version Management

Version is derived from **git tags** at build time via CMake.

### CMakeLists.txt (version extraction)

```cmake
# Extract version from git tags (e.g., v1.0.2 -> "1.0.2")
execute_process(
  COMMAND git describe --tags --abbrev=0
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE SAMPLAR_GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE GIT_TAG_RESULT
)

if(GIT_TAG_RESULT EQUAL 0 AND SAMPLAR_GIT_TAG)
  string(REGEX REPLACE "^v" "" SAMPLAR_GIT_VERSION "${SAMPLAR_GIT_TAG}")
  message(STATUS "SAMPLAR version from git tag: ${SAMPLAR_GIT_VERSION}")
else()
  set(SAMPLAR_GIT_VERSION "0.0.0-dev")
  message(STATUS "SAMPLAR version: ${SAMPLAR_GIT_VERSION} (no git tag found)")
endif()
```

### SamplarVersion.h.in (template)

**File:** `src/app/SamplarVersion.h.in`

```cpp
#pragma once
#define SAMPLAR_VERSION "@SAMPLAR_GIT_VERSION@"
```

CMake's `configure_file()` generates `SamplarVersion.h` from this template, substituting `@SAMPLAR_GIT_VERSION@` with the git tag value (e.g., `"1.0.2"`).

**To set the version:** Create a git tag: `git tag v1.0.3` then rebuild.

---

## 5. UpdateCheckService

### Header: `src/services/UpdateCheckService.h`

```cpp
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
```

### Implementation: `src/services/UpdateCheckService.cpp`

```cpp
#include "UpdateCheckService.h"
#include "SamplarVersion.h"
#include "../app/App.h"
#include "../app/ServiceRegistry.h"
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
    juce::String currentVersion = SAMPLAR_VERSION;
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
             .withParameter("product", "sample_monsta")
             .withParameter("hardware_id", juce::String(hwId));

    juce::Logger::writeToLog("[UpdateCheckService]   Request URL: " + url.toString(true));

    // 4. Perform the HTTP GET
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(8000));

    if (stream == nullptr)
    {
        juce::Logger::writeToLog("[UpdateCheckService] ERROR: Failed to create input stream");
        juce::Logger::writeToLog("[UpdateCheckService] === performCheck END (network failure) ===");
        return;
    }

    juce::String responseBody = stream->readEntireStreamAsString();
    juce::Logger::writeToLog("[UpdateCheckService]   Response length: "
                             + juce::String(responseBody.length()));
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
        }

        if (!licenseReminder_.message.empty())
        {
            juce::Logger::writeToLog("[UpdateCheckService]   Publishing 'license.status.reminder'");

            auto* obj = new juce::DynamicObject();
            obj->setProperty("status",        juce::String(licenseReminder_.status));
            obj->setProperty("type",          juce::String(licenseReminder_.type));
            obj->setProperty("daysRemaining", licenseReminder_.daysRemaining);
            obj->setProperty("message",       juce::String(licenseReminder_.message));

            eventBus_.publish("license.status.reminder", juce::var(obj));
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
        juce::Logger::writeToLog("[UpdateCheckService] ERROR: JSON parse returned void");
        return;
    }

    if (auto* updateObj = parsed.getDynamicObject())
    {
        if (updateObj->hasProperty("update_available"))
            updateInfo_.updateAvailable = (bool)parsed["update_available"];

        if (updateObj->hasProperty("latest_version"))
            updateInfo_.latestVersion = parsed["latest_version"].toString().toStdString();

        if (updateObj->hasProperty("download_url"))
            updateInfo_.downloadUrl = parsed["download_url"].toString().toStdString();

        if (updateObj->hasProperty("release_date"))
            updateInfo_.releaseDate = parsed["release_date"].toString().toStdString();

        if (updateObj->hasProperty("changes"))
        {
            updateInfo_.changes.clear();
            auto changesVar = parsed["changes"];
            if (auto* changesArray = changesVar.getArray())
            {
                for (const auto& item : *changesArray)
                    updateInfo_.changes.push_back(item.toString().toStdString());
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
            }
        }
    }

    juce::Logger::writeToLog("[UpdateCheckService] === parseResponse END ===");
}

// ---------------------------------------------------------------------------
// Hardware ID helper
// ---------------------------------------------------------------------------

std::string UpdateCheckService::getHardwareId() const
{
    auto* app = dynamic_cast<samplar::App*>(juce::JUCEApplication::getInstance());
    if (app == nullptr) return {};

    auto* licenseSvc = dynamic_cast<samplar::LicenseSystemService*>(
        app->services().get("license"));

    if (licenseSvc == nullptr) return {};

    return licenseSvc->generateHardwareId();
}

// ---------------------------------------------------------------------------
// Dismiss / wasDismissed  (persisted via PropertiesFile)
// ---------------------------------------------------------------------------

bool UpdateCheckService::wasDismissed(const std::string& version) const
{
    if (propsFile_ == nullptr) return false;
    juce::String dismissed = propsFile_->getValue("dismissed_version", "");
    return (dismissed.toStdString() == version);
}

void UpdateCheckService::dismissVersion(const std::string& version)
{
    if (propsFile_ == nullptr) return;
    propsFile_->setValue("dismissed_version", juce::String(version));
    propsFile_->saveIfNeeded();
}

} // namespace samplar
```

---

## 6. NotificationButton Component

A custom `juce::Button` with three visual modes and a 30Hz pulse animation timer.

### Header: `src/ui/Components/NotificationButton.h`

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

class NotificationButton : public juce::Button, private juce::Timer {
public:
    enum class Mode {
        UpToDate,        // Gray "i" info icon, no pulse, always visible
        UpdateAvailable, // Green pulsing circle with up-arrow
        TrialExpiring    // Amber pulsing circle with exclamation mark
    };

    NotificationButton();

    void setMode(Mode mode);
    Mode getMode() const { return mode_; }

    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    void timerCallback() override;

    Mode mode_{Mode::UpToDate};
    float pulsePhase_{0.0f};
    static constexpr float pulseSpeed_ = 0.05f;
};

} // namespace samplar
```

### Implementation: `src/ui/Components/NotificationButton.cpp`

```cpp
#include "NotificationButton.h"
#include <cmath>

namespace samplar {

NotificationButton::NotificationButton()
    : Button("Notification")
{
    juce::Logger::writeToLog("[NotificationButton] === constructor START ===");
    setTooltip("Version Info");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setVisible(true);
    juce::Logger::writeToLog("[NotificationButton] === constructor END === visible: true, mode: UpToDate");
}

void NotificationButton::setMode(Mode mode)
{
    juce::Logger::writeToLog("[NotificationButton] === setMode START ===");
    juce::Logger::writeToLog("[NotificationButton]   mode: "
        + juce::String(mode == Mode::UpdateAvailable ? "UpdateAvailable"
                       : mode == Mode::TrialExpiring ? "TrialExpiring" : "UpToDate"));

    mode_ = mode;
    if (mode == Mode::UpToDate) {
        juce::Logger::writeToLog("[NotificationButton]   Decision: UpToDate mode, stopping timer, staying visible");
        stopTimer();
        setVisible(true);
    } else {
        juce::Logger::writeToLog("[NotificationButton]   Decision: Active mode, starting 30Hz timer and showing");
        setVisible(true);
        startTimerHz(30);
    }
    repaint();
    juce::Logger::writeToLog("[NotificationButton] === setMode END ===");
}

void NotificationButton::timerCallback()
{
    pulsePhase_ += pulseSpeed_;
    if (pulsePhase_ > juce::MathConstants<float>::twoPi)
        pulsePhase_ -= juce::MathConstants<float>::twoPi;
    repaint();
}

void NotificationButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    auto cx = r.getCentreX();
    auto cy = r.getCentreY();

    if (mode_ == Mode::UpToDate) {
        // Subtle steady circle - no pulse, dim until hovered
        auto baseColor = juce::Colour(0xff888899);
        auto color = down ? baseColor.brighter(0.3f)
                          : (over ? baseColor.brighter(0.2f) : baseColor);

        g.setColour(color.withAlpha(over ? 0.25f : 0.1f));
        g.fillEllipse(r);

        g.setColour(color.withAlpha(over ? 0.7f : 0.4f));
        g.drawEllipse(r, 1.5f);

        // "i" info icon
        g.setColour(color.withAlpha(over ? 0.9f : 0.5f));
        g.fillEllipse(cx - 1.5f, cy - r.getHeight() * 0.22f, 3.0f, 3.0f);     // dot
        g.fillRoundedRectangle(cx - 1.5f, cy - r.getHeight() * 0.05f,
                               3.0f, r.getHeight() * 0.28f, 1.0f);              // stem
        return;
    }

    // --- Pulsing modes (UpdateAvailable / TrialExpiring) ---
    float pulse = 0.75f + 0.25f * std::sin(pulsePhase_);

    juce::Colour baseColor;
    if (mode_ == Mode::UpdateAvailable) {
        baseColor = juce::Colour::fromString("ff00FF88"); // Green
    } else {
        baseColor = juce::Colour::fromString("ffFFAA00"); // Amber
    }

    auto color = down ? baseColor.brighter(0.2f)
                      : (over ? baseColor.brighter(0.15f) : baseColor);

    // Outer glow with pulse
    g.setColour(color.withAlpha(pulse * 0.3f));
    g.fillEllipse(r);

    // Circle outline
    g.setColour(color.withAlpha(pulse));
    g.drawEllipse(r, 1.5f);

    // Center icon
    g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));

    if (mode_ == Mode::UpdateAvailable) {
        // Up arrow icon
        auto arrowSize = r.getWidth() * 0.25f;
        juce::Path arrow;
        arrow.addTriangle(cx, cy - arrowSize,
                          cx - arrowSize, cy + arrowSize * 0.3f,
                          cx + arrowSize, cy + arrowSize * 0.3f);
        g.fillPath(arrow);
        g.fillRoundedRectangle(cx - 1.5f, cy, 3.0f, arrowSize, 1.0f);
    } else {
        // Exclamation mark for trial warning
        g.fillRoundedRectangle(cx - 1.5f, cy - r.getHeight() * 0.2f,
                               3.0f, r.getHeight() * 0.22f, 1.0f);
        g.fillEllipse(cx - 2.0f, cy + r.getHeight() * 0.1f, 4.0f, 4.0f);
    }
}

} // namespace samplar
```

---

## 7. UpdateDialog Component

A modal-style overlay showing changelog and license status.

### Header: `src/ui/Components/UpdateDialog.h`

```cpp
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
```

### Implementation: `src/ui/Components/UpdateDialog.cpp`

```cpp
#include "UpdateDialog.h"

namespace samplar {

UpdateDialog::UpdateDialog(const UpdateCheckService::UpdateInfo& updateInfo,
                           const UpdateCheckService::LicenseReminder& licenseReminder)
    : updateInfo_(updateInfo),
      licenseReminder_(licenseReminder)
{
    // Style updateBtn_ - green accent
    updateBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff00CF8A));
    updateBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    updateBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    updateBtn_.onClick = [this] {
        if (onUpdateNow)
            onUpdateNow();
    };
    addAndMakeVisible(updateBtn_);

    // Style laterBtn_ - dark with grey text
    laterBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    laterBtn_.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffaaaaaa));
    laterBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaaaaa));
    laterBtn_.onClick = [this] {
        if (onDismiss)
            onDismiss();
    };
    addAndMakeVisible(laterBtn_);

    // Style upgradeBtn_ - orange accent
    upgradeBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffFF8800));
    upgradeBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    upgradeBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    upgradeBtn_.onClick = [this] {
        juce::URL("https://samplemonsta.com/portal").launchInDefaultBrowser();
    };

    // Only show upgrade button for trial or expired
    bool showUpgrade = (licenseReminder_.status == "trial"
                     || licenseReminder_.status == "expired");
    if (showUpgrade)
        addAndMakeVisible(upgradeBtn_);

    setSize(380, 320);
}

void UpdateDialog::setLicenseOnlyMode(bool licenseOnly)
{
    licenseOnlyMode_ = licenseOnly;

    if (licenseOnly)
    {
        updateBtn_.setVisible(false);
        setSize(380, 180);
    }
    else
    {
        updateBtn_.setVisible(true);
        setSize(380, 320);
    }

    resized();
    repaint();
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
            juce::String bulletLine = juce::String::fromUTF8("\xe2\x80\xa2 ")
                                    + juce::String(change);
            g.drawText(bulletLine, contentArea.getX() + 8, yPos,
                       contentArea.getWidth() - 8, 22,
                       juce::Justification::centredLeft);
            yPos += 22;
        }

        yPos += 8;
    }

    // --- License reminder section ---
    if (!licenseReminder_.message.empty())
    {
        // Separator line (only if update section shown above)
        if (!licenseOnlyMode_ && updateInfo_.updateAvailable)
        {
            g.setColour(juce::Colour(0xff333355));
            int separatorY = getHeight() - 80;
            g.drawHorizontalLine(separatorY,
                                 (float)contentArea.getX(),
                                 (float)contentArea.getRight());
            yPos = separatorY + 8;
        }
        else if (licenseOnlyMode_)
        {
            yPos = contentArea.getY() + 8;
        }

        // License message color based on status
        if (licenseReminder_.status == "expired")
            g.setColour(juce::Colour(0xffFF4444));  // Red
        else
            g.setColour(juce::Colour(0xffFFAA00));  // Amber

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
        if (upgradeBtn_.isVisible())
        {
            upgradeBtn_.setBounds(bounds.getX(), btnY, 140, btnHeight);
        }
    }
}

} // namespace samplar
```

---

## 8. MainWindow Integration

These code snippets show how the notification system is wired into `MainWindow.cpp`'s inner `RootComponent` class.

### Includes

```cpp
#include "Components/NotificationButton.h"
#include "Components/UpdateDialog.h"
#include "../services/UpdateCheckService.h"
```

### Member Variables

```cpp
samplar::NotificationButton notifBtn_;
std::unique_ptr<samplar::UpdateDialog> updateDialog_;
```

### Constructor Setup (in RootComponent)

```cpp
addAndMakeVisible(notifBtn_); // always visible for version info

// Handle notification button click - show update dialog
notifBtn_.onClick = [this]() {
    juce::Logger::writeToLog("[RootComponent] NotificationButton clicked");
    showUpdateDialog();
};

// Subscribe to update events from UpdateCheckService
if (auto* appInstance = dynamic_cast<App*>(juce::JUCEApplication::getInstance())) {
    appInstance->bus().subscribe("update.available", [this](const juce::var& payload) {
        juce::Logger::writeToLog("[RootComponent] Received update.available event");
        handleUpdateAvailable(payload);
    });
    appInstance->bus().subscribe("license.status.reminder", [this](const juce::var& payload) {
        juce::Logger::writeToLog("[RootComponent] Received license.status.reminder event");
        handleLicenseReminder(payload);
    });
}
```

### Title Bar Layout (in resized())

```cpp
// Title bar layout:
//   Centered group: [Pin] [Settings] [Drag] [Exit]
//   Right corner:   [Notification]

const int centerBtns = 4;
const int totalWidth = (btnSize * centerBtns) + (btnGap * (centerBtns - 1));
int startX = top.getCentreX() - totalWidth / 2;

// ... center buttons positioned here ...

// Notification button pinned to top-right corner
notifBtn_.setBounds(top.getRight() - btnSize - 8, btnY, btnSize, btnSize);
```

### showUpdateDialog()

```cpp
void showUpdateDialog()
{
    if (auto* appInstance = dynamic_cast<App*>(juce::JUCEApplication::getInstance())) {
        auto* updateSvc = dynamic_cast<samplar::UpdateCheckService*>(
            appInstance->services().get("update_checker"));

        auto currentVersion = appInstance->getApplicationVersion();

        // No update available - show simple version info dialog
        if (!updateSvc || !updateSvc->getUpdateInfo().updateAvailable) {
            auto options = juce::MessageBoxOptions()
                .withTitle("SAMPLAR")
                .withMessage("You are running v" + currentVersion + "\n\nYou're up to date!")
                .withButton("OK")
                .withIconType(juce::MessageBoxIconType::InfoIcon);
            juce::AlertWindow::showAsync(options, nullptr);
            return;
        }

        // Create UpdateDialog with server data
        updateDialog_ = std::make_unique<samplar::UpdateDialog>(
            updateSvc->getUpdateInfo(), updateSvc->getLicenseReminder());

        // License-only mode if no update but has license message
        if (!updateSvc->getUpdateInfo().updateAvailable
            && !updateSvc->getLicenseReminder().message.empty()) {
            updateDialog_->setLicenseOnlyMode(true);
        }

        // "Update Now" -> open browser, dismiss version, reset button
        updateDialog_->onUpdateNow = [this, updateSvc]() {
            auto url = updateSvc->getUpdateInfo().downloadUrl;
            if (!url.empty())
                juce::URL(juce::String(url)).launchInDefaultBrowser();
            updateSvc->dismissVersion(updateSvc->getUpdateInfo().latestVersion);
            updateDialog_.reset();
            notifBtn_.setMode(samplar::NotificationButton::Mode::UpToDate);
        };

        // "Later" -> dismiss version, close dialog
        updateDialog_->onDismiss = [this, updateSvc]() {
            updateSvc->dismissVersion(updateSvc->getUpdateInfo().latestVersion);
            updateDialog_.reset();
        };

        addAndMakeVisible(updateDialog_.get());
        updateDialog_->setCentrePosition(getWidth() / 2, getHeight() / 2);
    }
}
```

### Event Handlers

```cpp
void handleUpdateAvailable(const juce::var& /*payload*/)
{
    if (auto* appInstance = dynamic_cast<App*>(juce::JUCEApplication::getInstance())) {
        auto* updateSvc = dynamic_cast<samplar::UpdateCheckService*>(
            appInstance->services().get("update_checker"));
        if (!updateSvc) return;

        auto latest = updateSvc->getUpdateInfo().latestVersion;
        notifBtn_.setMode(samplar::NotificationButton::Mode::UpdateAvailable);
        resized();

        // Auto-show dialog only if this version wasn't previously dismissed
        if (!updateSvc->wasDismissed(latest)) {
            showUpdateDialog();
        }
    }
}

void handleLicenseReminder(const juce::var& payload)
{
    auto message = payload.getProperty("message", "").toString();
    if (message.isEmpty()) return;

    auto status = payload.getProperty("status", "").toString();
    auto daysRemaining = (int)payload.getProperty("daysRemaining", -1);

    // Show amber trial warning if not already showing update
    if ((status == "trial" || status == "expired") && daysRemaining <= 7) {
        if (notifBtn_.getMode() != samplar::NotificationButton::Mode::UpdateAvailable) {
            notifBtn_.setMode(samplar::NotificationButton::Mode::TrialExpiring);
            resized();
        }
    }
}
```

---

## 9. Service Registration

In `src/app/App.cpp`, the `UpdateCheckService` is registered during app initialization:

```cpp
#include "../services/UpdateCheckService.h"

// ... in initialisation code:
juce::Logger::writeToLog("[App] Registering UpdateCheckService");
serviceRegistry->registerService("update_checker",
    std::make_unique<samplar::UpdateCheckService>(eventBus_));
```

The service name `"update_checker"` is used to retrieve it later via:
```cpp
appInstance->services().get("update_checker")
```

---

## 10. CMakeLists.txt Entries

Ensure these source files are listed in `target_sources`:

```cmake
target_sources(SM5App PRIVATE
    # ... existing sources ...
    src/services/UpdateCheckService.h
    src/services/UpdateCheckService.cpp
    src/ui/Components/NotificationButton.h
    src/ui/Components/NotificationButton.cpp
    src/ui/Components/UpdateDialog.h
    src/ui/Components/UpdateDialog.cpp
)
```

---

## 11. File Manifest

| File | Type | Description |
|------|------|-------------|
| `src/app/SamplarVersion.h.in` | Template | CMake generates `SamplarVersion.h` with `SAMPLAR_VERSION` define |
| `src/services/UpdateCheckService.h` | Header | Service interface, UpdateInfo/LicenseReminder structs |
| `src/services/UpdateCheckService.cpp` | Impl | Network check, JSON parsing, EventBus publishing |
| `src/ui/Components/NotificationButton.h` | Header | Button with Mode enum (UpToDate/UpdateAvailable/TrialExpiring) |
| `src/ui/Components/NotificationButton.cpp` | Impl | Custom paintButton with icons and pulse animation |
| `src/ui/Components/UpdateDialog.h` | Header | Dialog component with callbacks |
| `src/ui/Components/UpdateDialog.cpp` | Impl | Changelog display, buttons, license reminder section |
| `src/ui/MainWindow.cpp` | Modified | Integration: button setup, event handlers, showUpdateDialog() |
| `src/app/App.cpp` | Modified | Service registration as "update_checker" |
| `CMakeLists.txt` | Modified | Source file entries + git tag version extraction |

---

## 12. macOS-Specific Notes

### PropertiesFile Location
The `UpdateCheckService` already configures the macOS path:
```cpp
opts.osxLibrarySubFolder = "Application Support";
```
This means dismissed-version preferences will be stored at:
`~/Library/Application Support/Samplar/.update_prefs`

### Network Permissions
On macOS, ensure the app has network access for the HTTPS call to `samplemonsta.com`.
If sandboxed, the `com.apple.security.network.client` entitlement is required.

### URL Launch
`juce::URL::launchInDefaultBrowser()` works cross-platform. No macOS changes needed.

### Title Bar Layout
The notification button is positioned at `top.getRight() - btnSize - 8`. On macOS, the
traffic light buttons (close/minimize/zoom) are on the **left** side, so the right-corner
placement should work without conflicts. However, verify:
- If using a custom title bar: ensure the button doesn't overlap the macOS toolbar
- If using native title bar: the notification button may need to be placed differently

### Font Rendering
The `paintButton` and `UpdateDialog::paint` methods use explicit font sizes (12-18pt).
These should render fine on macOS but may need DPI scaling adjustments for Retina displays.
JUCE handles this automatically in most cases.

### Build System
The git tag version extraction in CMakeLists.txt is platform-agnostic. Ensure git is
available in the macOS build environment.

---

## Quick Start for Mac Developers

1. **Add all files** from the [File Manifest](#11-file-manifest) to your project
2. **Update CMakeLists.txt** with source file entries and version extraction
3. **Register the service** in `App.cpp` as `"update_checker"`
4. **Wire up MainWindow** with the notification button, event subscriptions, and dialog
5. **Tag your release** with `git tag v1.0.2` (or appropriate version)
6. **Update `releases.json`** on the license server when releasing new versions
7. **Build and test** - the button should appear in the top-right corner
8. **Click the button** with no server connection to verify "You're up to date" dialog appears

---

*Document generated 2026-03-08 for macOS porting team.*
