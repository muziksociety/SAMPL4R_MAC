# Update Notification & License Status — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add version update notifications with soft license checks to SAMPLAR, backed by a new license server endpoint and git-tag-based versioning.

**Architecture:** License server gets a `data/releases.json` file and `/api/update/check` endpoint. SAMPLAR gets git-tag versioning via CMake, an `UpdateCheckService` that hits the endpoint on startup, and UI components (dialog + blinking title bar button) to notify the user.

**Tech Stack:** Python/FastAPI (server), C++17/JUCE (app), CMake (build)

---

## Task 1: Create `data/releases.json` on License Server

**Files:**
- Create: `B:\SANTI\license_server\data\releases.json`

**Step 1: Create the releases JSON file**

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

**Step 2: Verify the file is valid JSON**

Run: `python -c "import json; json.load(open('B:/SANTI/license_server/data/releases.json')); print('OK')"`
Expected: `OK`

**Step 3: Commit**

```bash
cd B:/SANTI/license_server
git add data/releases.json
git commit -m "feat: add releases.json as version/changelog source of truth"
```

---

## Task 2: Create `/api/update/check` Endpoint Plugin (License Server)

**Files:**
- Create: `B:\SANTI\license_server\plugins\update_checker\__init__.py`

**Step 1: Create the update_checker plugin**

```python
"""
Update Checker Plugin - provides /api/update/check endpoint.
Returns latest version info + soft license status for SAMPLAR app.
"""

import json
import logging
from datetime import datetime, timezone
from pathlib import Path

from fastapi import APIRouter, Query
from core.app import LicenseServerApp

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/update", tags=["update"])

RELEASES_FILE = Path(__file__).parent.parent.parent / "data" / "releases.json"


def load_releases() -> dict:
    """Load releases.json from disk."""
    try:
        with open(RELEASES_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        logger.error(f"[UpdateChecker] Failed to load releases.json: {e}")
        return {}


def compare_versions(current: str, latest: str) -> bool:
    """Return True if latest > current using semantic versioning."""
    try:
        def parse(v: str):
            return tuple(int(x) for x in v.strip().lstrip("v").split("."))
        return parse(latest) > parse(current)
    except (ValueError, AttributeError):
        return False


@router.get("/check")
async def check_for_update(
    current_version: str = Query(..., description="Client's current app version"),
    product: str = Query("sample_monsta", description="Product identifier"),
    hardware_id: str = Query("", description="Hardware ID for license lookup"),
):
    """Check for updates and return license status."""
    logger.info(f"[UpdateChecker] Check request: product={product}, "
                f"version={current_version}, hw={hardware_id[:16]}...")

    releases_data = load_releases()
    product_data = releases_data.get("products", {}).get(product)

    if not product_data:
        return {
            "update_available": False,
            "current_version": current_version,
            "error": f"Unknown product: {product}",
        }

    latest_version = product_data.get("latest_version", "0.0.0")
    update_available = compare_versions(current_version, latest_version)

    # Get changelog for the latest release only
    releases = product_data.get("releases", [])
    latest_release = releases[0] if releases else {}

    result = {
        "update_available": update_available,
        "latest_version": latest_version,
        "current_version": current_version,
        "download_url": product_data.get("download_url", ""),
        "release_date": latest_release.get("date", ""),
        "changes": latest_release.get("changes", []),
    }

    # Soft license check if hardware_id provided
    if hardware_id:
        result["license"] = await _get_license_status(hardware_id)

    logger.info(f"[UpdateChecker] Response: update={update_available}, "
                f"latest={latest_version}")
    return result


async def _get_license_status(hardware_id: str) -> dict:
    """Look up license status for a hardware ID."""
    default = {"status": "unknown", "type": "", "days_remaining": -1, "message": ""}

    try:
        app = LicenseServerApp.get_instance()
        db_service = app.services.get("database_manager")
        if not db_service:
            return default

        # Find license by hardware_id through machine activations
        license_data = await db_service.get_license_by_hardware_id(hardware_id)
        if not license_data:
            return default

        license_type = license_data.get("license_type", "")
        expires_at = license_data.get("expires_at")
        is_active = license_data.get("is_active", False)

        if not is_active:
            return {
                "status": "expired",
                "type": license_type,
                "days_remaining": 0,
                "message": "Your license has expired",
            }

        # Calculate days remaining
        days_remaining = -1
        message = ""
        if expires_at:
            try:
                if isinstance(expires_at, str):
                    exp = datetime.fromisoformat(expires_at.replace("Z", "+00:00"))
                else:
                    exp = expires_at
                now = datetime.now(timezone.utc)
                delta = (exp - now).days
                days_remaining = max(0, delta)

                if license_type in ("TRL", "TRIAL"):
                    if days_remaining <= 0:
                        return {
                            "status": "expired",
                            "type": license_type,
                            "days_remaining": 0,
                            "message": "Your free trial has expired",
                        }
                    elif days_remaining <= 7:
                        message = f"You have {days_remaining} day{'s' if days_remaining != 1 else ''} left on your free trial"
                    else:
                        message = ""
                elif days_remaining <= 7 and days_remaining >= 0:
                    message = f"Your license expires in {days_remaining} day{'s' if days_remaining != 1 else ''}"
            except Exception as e:
                logger.warning(f"[UpdateChecker] Date parse error: {e}")

        status = "trial" if license_type in ("TRL", "TRIAL") else "active"

        return {
            "status": status,
            "type": license_type,
            "days_remaining": days_remaining,
            "message": message,
        }
    except Exception as e:
        logger.error(f"[UpdateChecker] License lookup error: {e}")
        return default


def register(app):
    """Plugin register hook."""
    app.fastapi_app.include_router(router)
    logger.info("[PLUGIN] update_checker registered")
```

**Step 2: Verify the database service has `get_license_by_hardware_id`**

Check `B:\SANTI\license_server\plugins\database_manager\database_service.py` for existing method. If it doesn't exist, add a method that queries machines table joined with licenses to find the license for a given hardware_id. The pattern to follow:

```python
async def get_license_by_hardware_id(self, hardware_id: str) -> dict | None:
    """Find the active license associated with a hardware ID."""
    # Query license_machines join licenses where hardware_id matches
    ...
```

**Step 3: Test the endpoint locally**

Run: `curl "http://localhost:8000/api/update/check?current_version=1.0.1&product=sample_monsta&hardware_id=test123"`

Expected: JSON response with `"update_available": true` and `"latest_version": "1.0.2"`

**Step 4: Commit**

```bash
cd B:/SANTI/license_server
git add plugins/update_checker/__init__.py
git commit -m "feat: add /api/update/check endpoint with soft license validation"
```

---

## Task 3: Update Dashboard Template to Read from releases.json (License Server)

**Files:**
- Modify: `B:\SANTI\license_server\plugins\user_portal\__init__.py` (pass releases data to template)
- Modify: `B:\SANTI\license_server\plugins\user_portal\templates\dashboard.html` (lines ~956-978, replace hardcoded changelog)

**Step 1: Load releases in the portal route**

In `plugins/user_portal/__init__.py`, in the `portal_root` function, add before `template.render()`:

```python
# Load releases for changelog
import json
releases_file = Path(__file__).parent.parent.parent / "data" / "releases.json"
releases = []
latest_version = ""
try:
    with open(releases_file, "r", encoding="utf-8") as f:
        releases_data = json.load(f)
    product = releases_data.get("products", {}).get("sample_monsta", {})
    releases = product.get("releases", [])
    latest_version = product.get("latest_version", "")
except Exception as e:
    logger.error(f"Failed to load releases.json: {e}")
```

Add `releases=releases, latest_version=latest_version` to the `template.render()` call.

**Step 2: Replace hardcoded changelog HTML**

In `dashboard.html`, replace lines ~956-978 (the hardcoded changelog section) with Jinja2 template:

```html
<span class="changelog-version-badge">v{{ latest_version }}</span>
</div>
<div class="changelog-content">
    {% for release in releases %}
    <div class="changelog-release">
        <div class="changelog-release-header">
            <span class="changelog-release-version">Version {{ release.version }}</span>
            <span class="changelog-release-date">{{ release.date }}</span>
        </div>
        <ul class="changelog-list">
            {% for change in release.changes %}
            <li>{{ change }}</li>
            {% endfor %}
        </ul>
    </div>
    {% endfor %}
</div>
```

**Step 3: Verify dashboard renders correctly**

Open `http://localhost:8000/portal` and verify the changelog shows the same content as before but now from JSON.

**Step 4: Commit**

```bash
cd B:/SANTI/license_server
git add plugins/user_portal/__init__.py plugins/user_portal/templates/dashboard.html
git commit -m "refactor: dashboard changelog reads from releases.json"
```

---

## Task 4: Git-Tag Versioning in CMake (SAMPLAR App)

**Files:**
- Create: `Sample_monsta5/src/app/SamplarVersion.h.in`
- Modify: `Sample_monsta5/CMakeLists.txt` (after line 3, before `set(CMAKE_CXX_STANDARD 17)`)
- Modify: `Sample_monsta5/src/app/App.h` (line 15)

**Step 1: Create the CMake version template**

Create `src/app/SamplarVersion.h.in`:

```cpp
#pragma once
#define SAMPLAR_VERSION "@SAMPLAR_GIT_VERSION@"
```

**Step 2: Add git version extraction to CMakeLists.txt**

After line 3 (`project(samplar VERSION 2.0.0 LANGUAGES CXX)`), add:

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
  # Strip leading 'v' if present (v1.0.2 -> 1.0.2)
  string(REGEX REPLACE "^v" "" SAMPLAR_GIT_VERSION "${SAMPLAR_GIT_TAG}")
  message(STATUS "SAMPLAR version from git tag: ${SAMPLAR_GIT_VERSION}")
else()
  set(SAMPLAR_GIT_VERSION "0.0.0-dev")
  message(STATUS "SAMPLAR version: ${SAMPLAR_GIT_VERSION} (no git tag found)")
endif()

configure_file(
  ${CMAKE_SOURCE_DIR}/src/app/SamplarVersion.h.in
  ${CMAKE_BINARY_DIR}/generated/SamplarVersion.h
  @ONLY
)
```

Also add the generated header to include path. Find where `target_include_directories` is set for SM5App (or after `target_sources(SM5App ...)`), and add:

```cmake
target_include_directories(SM5App PRIVATE ${CMAKE_BINARY_DIR}/generated)
```

**Step 3: Update App.h to use generated version**

Change line 15 of `src/app/App.h` from:

```cpp
const juce::String getApplicationVersion() override { return "2.0.0"; }
```

To:

```cpp
const juce::String getApplicationVersion() override;
```

And in `src/app/App.cpp`, add near the top:

```cpp
#include "SamplarVersion.h"
```

And add the implementation (e.g., after the existing includes):

```cpp
const juce::String App::getApplicationVersion() { return SAMPLAR_VERSION; }
```

**Step 4: Create initial git tag and test build**

```bash
cd b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5
git tag v1.0.2
cmake -B build -G "Visual Studio 17 2022" -A x64
```

Expected: Console shows `SAMPLAR version from git tag: 1.0.2`

**Step 5: Commit**

```bash
git add src/app/SamplarVersion.h.in CMakeLists.txt src/app/App.h src/app/App.cpp
git commit -m "feat: git-tag based versioning replaces hardcoded version"
```

---

## Task 5: Create UpdateCheckService (SAMPLAR App)

**Files:**
- Create: `Sample_monsta5/src/services/UpdateCheckService.h`
- Create: `Sample_monsta5/src/services/UpdateCheckService.cpp`
- Modify: `Sample_monsta5/CMakeLists.txt` (add to SAMPLAR_SOURCES, around line 258 near other services)
- Modify: `Sample_monsta5/src/app/App.cpp` (register the service, around line 127)

**Step 1: Create UpdateCheckService.h**

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
        std::string status;     // "active", "trial", "expired", "unknown"
        std::string type;       // "TRL", "PRO", etc.
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

**Step 2: Create UpdateCheckService.cpp**

```cpp
#include "UpdateCheckService.h"
#include "LicenseSystemService.h"
#include "../app/App.h"
#include "../app/ServiceRegistry.h"
#include "SamplarVersion.h"

namespace samplar {

UpdateCheckService::UpdateCheckService(EventBus& eventBus)
    : eventBus_(eventBus)
{
    juce::Logger::writeToLog("[UpdateCheckService] === CONSTRUCTOR ===");

    // Setup properties file for storing dismissed versions
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Samplar";
    opts.folderName = "Samplar";
    opts.filenameSuffix = ".update_prefs";
    opts.osxLibrarySubFolder = "Application Support";
    propsFile_ = std::make_unique<juce::PropertiesFile>(opts);
}

UpdateCheckService::~UpdateCheckService()
{
    stopTimer();
}

void UpdateCheckService::initialise()
{
    juce::Logger::writeToLog("[UpdateCheckService] === INITIALISE ===");
    juce::Logger::writeToLog("[UpdateCheckService] Will check for updates in "
                             + juce::String(startupDelayMs_ / 1000) + " seconds");
    startTimer(startupDelayMs_);
}

void UpdateCheckService::shutdown()
{
    juce::Logger::writeToLog("[UpdateCheckService] === SHUTDOWN ===");
    stopTimer();
}

void UpdateCheckService::timerCallback()
{
    stopTimer(); // One-shot
    juce::Logger::writeToLog("[UpdateCheckService] Timer fired, starting update check");

    // Run network request on background thread
    auto* self = this;
    std::thread([self]() {
        self->performCheck();
    }).detach();
}

void UpdateCheckService::performCheck()
{
    juce::Logger::writeToLog("[UpdateCheckService] === performCheck START ===");

    std::string currentVersion = SAMPLAR_VERSION;
    std::string hardwareId = getHardwareId();

    juce::Logger::writeToLog("[UpdateCheckService] Current version: "
                             + juce::String(currentVersion));
    juce::Logger::writeToLog("[UpdateCheckService] Hardware ID: "
                             + juce::String(hardwareId.substr(0, 16)) + "...");

    // Build URL
    juce::String baseUrl = "https://samplemonsta.com/api/update/check";
    juce::URL url(baseUrl);
    url = url.withParameter("current_version", juce::String(currentVersion));
    url = url.withParameter("product", "sample_monsta");
    if (!hardwareId.empty())
        url = url.withParameter("hardware_id", juce::String(hardwareId));

    juce::Logger::writeToLog("[UpdateCheckService] Requesting: " + url.toString(true));

    // Perform GET request
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(8000)
                       .withResponseHeaders(nullptr)
                       .withNumRedirectsToFollow(3);

    auto stream = url.createInputStream(options);

    if (!stream) {
        juce::Logger::writeToLog("[UpdateCheckService] Failed to connect to update server");
        return;
    }

    auto responseBody = stream->readEntireStreamAsString();
    juce::Logger::writeToLog("[UpdateCheckService] Response received ("
                             + juce::String(responseBody.length()) + " bytes)");

    parseResponse(responseBody);

    // Publish events on message thread
    juce::MessageManager::callAsync([this]() {
        if (updateInfo_.updateAvailable) {
            juce::Logger::writeToLog("[UpdateCheckService] Update available: v"
                                     + juce::String(updateInfo_.latestVersion));

            auto payload = new juce::DynamicObject();
            payload->setProperty("version", juce::String(updateInfo_.latestVersion));
            payload->setProperty("downloadUrl", juce::String(updateInfo_.downloadUrl));
            payload->setProperty("releaseDate", juce::String(updateInfo_.releaseDate));

            juce::StringArray changesArr;
            for (auto& c : updateInfo_.changes)
                changesArr.add(juce::String(c));
            payload->setProperty("changes", changesArr.joinIntoString("\n"));

            eventBus_.publish("update.available", juce::var(payload));
        }

        if (!licenseReminder_.message.empty()) {
            juce::Logger::writeToLog("[UpdateCheckService] License reminder: "
                                     + juce::String(licenseReminder_.message));

            auto payload = new juce::DynamicObject();
            payload->setProperty("status", juce::String(licenseReminder_.status));
            payload->setProperty("type", juce::String(licenseReminder_.type));
            payload->setProperty("daysRemaining", licenseReminder_.daysRemaining);
            payload->setProperty("message", juce::String(licenseReminder_.message));

            eventBus_.publish("license.status.reminder", juce::var(payload));
        }
    });

    juce::Logger::writeToLog("[UpdateCheckService] === performCheck END ===");
}

void UpdateCheckService::parseResponse(const juce::String& responseBody)
{
    juce::Logger::writeToLog("[UpdateCheckService] === parseResponse START ===");

    auto json = juce::JSON::parse(responseBody);
    if (!json.isObject()) {
        juce::Logger::writeToLog("[UpdateCheckService] Invalid JSON response");
        return;
    }

    // Parse update info
    updateInfo_.updateAvailable = json.getProperty("update_available", false);
    updateInfo_.latestVersion = json.getProperty("latest_version", "").toString().toStdString();
    updateInfo_.currentVersion = json.getProperty("current_version", "").toString().toStdString();
    updateInfo_.downloadUrl = json.getProperty("download_url", "").toString().toStdString();
    updateInfo_.releaseDate = json.getProperty("release_date", "").toString().toStdString();

    // Parse changes array
    updateInfo_.changes.clear();
    if (auto* changesArray = json.getProperty("changes", juce::var()).getArray()) {
        for (auto& item : *changesArray)
            updateInfo_.changes.push_back(item.toString().toStdString());
    }

    // Parse license info
    auto licenseVar = json.getProperty("license", juce::var());
    if (licenseVar.isObject()) {
        licenseReminder_.status = licenseVar.getProperty("status", "").toString().toStdString();
        licenseReminder_.type = licenseVar.getProperty("type", "").toString().toStdString();
        licenseReminder_.daysRemaining = licenseVar.getProperty("days_remaining", -1);
        licenseReminder_.message = licenseVar.getProperty("message", "").toString().toStdString();
    }

    juce::Logger::writeToLog("[UpdateCheckService] Parsed: update="
                             + juce::String(updateInfo_.updateAvailable ? "yes" : "no")
                             + " latest=" + juce::String(updateInfo_.latestVersion)
                             + " license=" + juce::String(licenseReminder_.status));
    juce::Logger::writeToLog("[UpdateCheckService] === parseResponse END ===");
}

std::string UpdateCheckService::getHardwareId() const
{
    if (auto* appInstance = dynamic_cast<App*>(juce::JUCEApplication::getInstance())) {
        if (auto* licenseSvc = dynamic_cast<LicenseSystemService*>(
                appInstance->services().get("license"))) {
            return licenseSvc->generateHardwareId();
        }
    }
    return "";
}

bool UpdateCheckService::wasDismissed(const std::string& version) const
{
    if (!propsFile_) return false;
    return propsFile_->getValue("dismissed_version", "") == juce::String(version);
}

void UpdateCheckService::dismissVersion(const std::string& version)
{
    juce::Logger::writeToLog("[UpdateCheckService] Dismissing version: "
                             + juce::String(version));
    if (propsFile_) {
        propsFile_->setValue("dismissed_version", juce::String(version));
        propsFile_->saveIfNeeded();
    }
}

} // namespace samplar
```

**Step 3: Add to CMakeLists.txt SAMPLAR_SOURCES**

Add after line 258 (after `src/services/ChatService.cpp`):

```cmake
  src/services/UpdateCheckService.h
  src/services/UpdateCheckService.cpp
```

**Step 4: Register in App.cpp**

Add include at top of `src/app/App.cpp`:

```cpp
#include "../services/UpdateCheckService.h"
```

Add registration after line 127 (after ChatService registration):

```cpp
        juce::Logger::writeToLog("[App] Registering UpdateCheckService");
        serviceRegistry->registerService("update_checker",
            std::make_unique<samplar::UpdateCheckService>(eventBus_));
```

**Step 5: Build and verify**

```bash
cmake --build build --config Release
```

Expected: Compiles without errors. On launch, console log shows update check after 5 seconds.

**Step 6: Commit**

```bash
git add src/services/UpdateCheckService.h src/services/UpdateCheckService.cpp CMakeLists.txt src/app/App.cpp
git commit -m "feat: add UpdateCheckService with server check and EventBus events"
```

---

## Task 6: Create UpdateDialog UI Component (SAMPLAR App)

**Files:**
- Create: `Sample_monsta5/src/ui/Components/UpdateDialog.h`
- Create: `Sample_monsta5/src/ui/Components/UpdateDialog.cpp`
- Modify: `Sample_monsta5/CMakeLists.txt` (add to SAMPLAR_SOURCES)

**Step 1: Create UpdateDialog.h**

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

    std::function<void()> onDismiss;     // Called when "Later" clicked
    std::function<void()> onUpdateNow;   // Called when "Update Now" clicked

    // Show as license-only reminder (no update section)
    void setLicenseOnlyMode(bool licenseOnly);

private:
    UpdateCheckService::UpdateInfo updateInfo_;
    UpdateCheckService::LicenseReminder licenseReminder_;
    bool licenseOnlyMode_{false};

    juce::TextButton updateBtn_{"Update Now"};
    juce::TextButton laterBtn_{"Later"};
    juce::TextButton upgradeBtn_{"Upgrade to PRO"};
};

} // namespace samplar
```

**Step 2: Create UpdateDialog.cpp**

```cpp
#include "UpdateDialog.h"

namespace samplar {

UpdateDialog::UpdateDialog(const UpdateCheckService::UpdateInfo& updateInfo,
                           const UpdateCheckService::LicenseReminder& licenseReminder)
    : updateInfo_(updateInfo), licenseReminder_(licenseReminder)
{
    juce::Logger::writeToLog("[UpdateDialog] === CONSTRUCTOR ===");

    updateBtn_.setColour(juce::TextButton::buttonColourId,
                         juce::Colour::fromString("ff00CF8A"));
    updateBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    updateBtn_.onClick = [this]() {
        juce::Logger::writeToLog("[UpdateDialog] Update Now clicked");
        if (onUpdateNow) onUpdateNow();
    };

    laterBtn_.setColour(juce::TextButton::buttonColourId,
                        juce::Colour(0xff333333));
    laterBtn_.setColour(juce::TextButton::textColourOnId,
                        juce::Colour(0xffaaaaaa));
    laterBtn_.onClick = [this]() {
        juce::Logger::writeToLog("[UpdateDialog] Later clicked");
        if (onDismiss) onDismiss();
    };

    upgradeBtn_.setColour(juce::TextButton::buttonColourId,
                          juce::Colour::fromString("ffFF8800"));
    upgradeBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    upgradeBtn_.onClick = [this]() {
        juce::Logger::writeToLog("[UpdateDialog] Upgrade to PRO clicked");
        juce::URL("https://samplemonsta.com/portal").launchInDefaultBrowser();
    };

    addAndMakeVisible(updateBtn_);
    addAndMakeVisible(laterBtn_);

    // Only show upgrade button for trial users
    if (licenseReminder_.status == "trial" || licenseReminder_.status == "expired") {
        addAndMakeVisible(upgradeBtn_);
    }

    setSize(380, 320);
}

void UpdateDialog::setLicenseOnlyMode(bool licenseOnly)
{
    licenseOnlyMode_ = licenseOnly;
    updateBtn_.setVisible(!licenseOnly);
    setSize(380, licenseOnly ? 180 : 320);
    resized();
    repaint();
}

void UpdateDialog::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xff1a1a2e));
    g.fillRoundedRectangle(bounds, 12.0f);

    // Border
    g.setColour(juce::Colour(0xff333355));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

    float y = 20.0f;
    float margin = 24.0f;

    if (!licenseOnlyMode_ && updateInfo_.updateAvailable) {
        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f).boldened());
        g.drawText("What's New in v" + juce::String(updateInfo_.latestVersion),
                   margin, y, getWidth() - margin * 2, 24.0f,
                   juce::Justification::centredLeft);
        y += 34.0f;

        // Changes list
        g.setFont(juce::Font(13.0f));
        g.setColour(juce::Colour(0xffcccccc));
        for (auto& change : updateInfo_.changes) {
            juce::String line = juce::CharPointer_UTF8("\xe2\x80\xa2 ") + juce::String(change);
            g.drawText(line, margin + 4, y, getWidth() - margin * 2 - 8, 20.0f,
                       juce::Justification::centredLeft);
            y += 20.0f;
        }
    }

    // License reminder section
    if (!licenseReminder_.message.empty()) {
        float separatorY = licenseOnlyMode_ ? 16.0f : y + 50.0f;

        if (!licenseOnlyMode_) {
            // Separator line
            g.setColour(juce::Colour(0xff333355));
            g.drawLine(margin, separatorY, getWidth() - margin, separatorY, 0.5f);
            separatorY += 12.0f;
        }

        // License icon and message
        g.setColour(licenseReminder_.status == "trial"
                        ? juce::Colour::fromString("ffFFAA00")
                        : juce::Colour::fromString("ffFF4444"));
        g.setFont(juce::Font(12.0f));
        g.drawText(juce::String(licenseReminder_.message),
                   margin, separatorY, getWidth() - margin * 2, 20.0f,
                   juce::Justification::centredLeft);
    }
}

void UpdateDialog::resized()
{
    auto bounds = getLocalBounds().reduced(24);

    // Buttons at bottom
    auto btnArea = bounds.removeFromBottom(36);

    if (licenseOnlyMode_) {
        laterBtn_.setBounds(btnArea.removeFromRight(100));
        if (upgradeBtn_.isVisible()) {
            btnArea.removeFromRight(10);
            upgradeBtn_.setBounds(btnArea.removeFromRight(140));
        }
    } else {
        laterBtn_.setBounds(btnArea.removeFromRight(100));
        btnArea.removeFromRight(10);
        updateBtn_.setBounds(btnArea.removeFromRight(120));
    }
}

} // namespace samplar
```

**Step 3: Add to CMakeLists.txt SAMPLAR_SOURCES**

Add after the UpdateCheckService entries:

```cmake
  src/ui/Components/UpdateDialog.h
  src/ui/Components/UpdateDialog.cpp
```

**Step 4: Commit**

```bash
git add src/ui/Components/UpdateDialog.h src/ui/Components/UpdateDialog.cpp CMakeLists.txt
git commit -m "feat: add UpdateDialog UI component for version notifications"
```

---

## Task 7: Create NotificationButton UI Component (SAMPLAR App)

**Files:**
- Create: `Sample_monsta5/src/ui/Components/NotificationButton.h`
- Create: `Sample_monsta5/src/ui/Components/NotificationButton.cpp`
- Modify: `Sample_monsta5/CMakeLists.txt` (add to SAMPLAR_SOURCES)

**Step 1: Create NotificationButton.h**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplar {

class NotificationButton : public juce::Button, private juce::Timer {
public:
    enum class Mode {
        Hidden,        // Nothing to show
        UpdateAvailable, // Green blinking - update available
        TrialExpiring    // Amber blinking - trial/license expiring
    };

    NotificationButton();

    void setMode(Mode mode);
    Mode getMode() const { return mode_; }

    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    void timerCallback() override;

    Mode mode_{Mode::Hidden};
    float pulsePhase_{0.0f};
    static constexpr float pulseSpeed_ = 0.05f; // radians per timer tick
};

} // namespace samplar
```

**Step 2: Create NotificationButton.cpp**

```cpp
#include "NotificationButton.h"
#include <cmath>

namespace samplar {

NotificationButton::NotificationButton()
    : Button("Notification")
{
    setTooltip("Notifications");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setVisible(false);
}

void NotificationButton::setMode(Mode mode)
{
    juce::Logger::writeToLog("[NotificationButton] Mode set to: "
        + juce::String(mode == Mode::UpdateAvailable ? "UpdateAvailable"
                       : mode == Mode::TrialExpiring ? "TrialExpiring" : "Hidden"));

    mode_ = mode;
    if (mode == Mode::Hidden) {
        stopTimer();
        setVisible(false);
    } else {
        setVisible(true);
        startTimerHz(30); // 30fps for smooth pulse
    }
    repaint();
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
    if (mode_ == Mode::Hidden) return;

    auto r = getLocalBounds().toFloat().reduced(2.0f);

    // Pulse alpha between 0.5 and 1.0
    float pulse = 0.75f + 0.25f * std::sin(pulsePhase_);

    juce::Colour baseColor;
    if (mode_ == Mode::UpdateAvailable) {
        baseColor = juce::Colour::fromString("ff00FF88"); // Green
    } else {
        baseColor = juce::Colour::fromString("ffFFAA00"); // Amber
    }

    auto color = down ? baseColor.brighter(0.2f)
                      : (over ? baseColor.brighter(0.15f) : baseColor);

    // Outer circle with pulse
    g.setColour(color.withAlpha(pulse * 0.3f));
    g.fillEllipse(r);

    // Inner circle outline
    g.setColour(color.withAlpha(pulse));
    g.drawEllipse(r, 1.5f);

    // Center icon: arrow-up for update, clock for trial
    g.setColour(color.withAlpha(down ? 1.0f : (over ? 0.95f : 0.85f)));
    auto cx = r.getCentreX();
    auto cy = r.getCentreY();

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
        // Exclamation mark for trial
        g.fillRoundedRectangle(cx - 1.5f, cy - r.getHeight() * 0.2f,
                               3.0f, r.getHeight() * 0.22f, 1.0f);
        g.fillEllipse(cx - 2.0f, cy + r.getHeight() * 0.1f, 4.0f, 4.0f);
    }
}

} // namespace samplar
```

**Step 3: Add to CMakeLists.txt SAMPLAR_SOURCES**

```cmake
  src/ui/Components/NotificationButton.h
  src/ui/Components/NotificationButton.cpp
```

**Step 4: Commit**

```bash
git add src/ui/Components/NotificationButton.h src/ui/Components/NotificationButton.cpp CMakeLists.txt
git commit -m "feat: add NotificationButton with green/amber pulse animation"
```

---

## Task 8: Integrate into MainWindow (SAMPLAR App)

**Files:**
- Modify: `Sample_monsta5/src/ui/MainWindow.cpp`
  - Add includes (~line 20)
  - Add NotificationButton member (~line 1213)
  - Add button to layout in `resized()` (~line 740)
  - Subscribe to EventBus events in RootComponent constructor (~line 239)
  - Handle showing dialog and managing button state

**Step 1: Add includes**

After line 20 (`#include "Components/SettingsDialog.h"`), add:

```cpp
#include "Components/NotificationButton.h"
#include "Components/UpdateDialog.h"
#include "../services/UpdateCheckService.h"
```

**Step 2: Add member variables**

After line 1213 (`DragButton dragBtn_;`), add:

```cpp
    samplar::NotificationButton notifBtn_;
    std::unique_ptr<samplar::UpdateDialog> updateDialog_;
```

**Step 3: Add NotificationButton to the layout**

In `resized()`, expand the title bar button area (around line 740-751). Change from 4 buttons to 5:

Replace lines 740-751 with:

```cpp
        // Window control buttons (5 circular buttons centered at top)
        // Order: Notification (!) - Pin (O) - Settings (S) - Drag (=) - Exit (X)
        auto top = pages_.getBounds().removeFromTop(28);
        const int btnSize = 20;
        const int btnGap = 6;
        int visibleBtns = notifBtn_.isVisible() ? 5 : 4;
        const int totalWidth = (btnSize * visibleBtns) + (btnGap * (visibleBtns - 1));
        int startX = top.getCentreX() - totalWidth / 2;
        int btnY = top.getCentreY() - btnSize / 2;
        int idx = 0;
        if (notifBtn_.isVisible()) {
            notifBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize);
            idx++;
        }
        pinBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize); idx++;
        settingsBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize); idx++;
        dragBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize); idx++;
        exitBtn_.setBounds(startX + (btnSize + btnGap) * idx, btnY, btnSize, btnSize);
```

**Step 4: Wire up EventBus subscriptions and button click**

In the RootComponent constructor, after line 243 (`addAndMakeVisible(dragBtn_);`), add:

```cpp
            addChildComponent(notifBtn_); // starts hidden

            // Handle notification button click
            notifBtn_.onClick = [this]() {
                juce::Logger::writeToLog("[RootComponent] NotificationButton clicked");
                showUpdateDialog();
            };

            // Subscribe to update events
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

**Step 5: Add handler methods**

Add these private methods to the RootComponent class (before the member variable declarations around line 1200):

```cpp
    void showUpdateDialog()
    {
        if (auto* appInstance = dynamic_cast<App*>(juce::JUCEApplication::getInstance())) {
            auto* updateSvc = dynamic_cast<UpdateCheckService*>(
                appInstance->services().get("update_checker"));
            if (!updateSvc) return;

            updateDialog_ = std::make_unique<UpdateDialog>(
                updateSvc->getUpdateInfo(), updateSvc->getLicenseReminder());

            // If no update but trial expiring, show license-only mode
            if (!updateSvc->getUpdateInfo().updateAvailable
                && !updateSvc->getLicenseReminder().message.empty()) {
                updateDialog_->setLicenseOnlyMode(true);
            }

            updateDialog_->onUpdateNow = [this, updateSvc]() {
                auto url = updateSvc->getUpdateInfo().downloadUrl;
                if (!url.empty())
                    juce::URL(juce::String(url)).launchInDefaultBrowser();
                updateSvc->dismissVersion(updateSvc->getUpdateInfo().latestVersion);
                updateDialog_.reset();
                notifBtn_.setMode(NotificationButton::Mode::Hidden);
                resized();
            };

            updateDialog_->onDismiss = [this, updateSvc]() {
                updateSvc->dismissVersion(updateSvc->getUpdateInfo().latestVersion);
                updateDialog_.reset();
                // Keep button visible but stop blinking? No - keep it blinking.
            };

            // Show centered as overlay
            addAndMakeVisible(updateDialog_.get());
            auto dialogBounds = updateDialog_->getLocalBounds();
            updateDialog_->setCentrePosition(getWidth() / 2, getHeight() / 2);
        }
    }

    void handleUpdateAvailable(const juce::var& /*payload*/)
    {
        if (auto* appInstance = dynamic_cast<App*>(juce::JUCEApplication::getInstance())) {
            auto* updateSvc = dynamic_cast<UpdateCheckService*>(
                appInstance->services().get("update_checker"));
            if (!updateSvc) return;

            auto latest = updateSvc->getUpdateInfo().latestVersion;
            if (updateSvc->wasDismissed(latest)) {
                // Previously dismissed - show button only
                notifBtn_.setMode(NotificationButton::Mode::UpdateAvailable);
                resized();
            } else {
                // First time seeing this version - show dialog
                notifBtn_.setMode(NotificationButton::Mode::UpdateAvailable);
                resized();
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

        // Show amber button for trial expiring (7 days or fewer)
        if ((status == "trial" || status == "expired") && daysRemaining <= 7) {
            if (notifBtn_.getMode() != NotificationButton::Mode::UpdateAvailable) {
                notifBtn_.setMode(NotificationButton::Mode::TrialExpiring);
                resized();
            }
        }
    }
```

**Step 6: Build and test**

```bash
cmake --build build --config Release
```

Expected: Compiles. On launch, after 5 seconds the update check runs. If server responds with update/trial info, dialog or button appears.

**Step 7: Commit**

```bash
git add src/ui/MainWindow.cpp
git commit -m "feat: integrate NotificationButton and UpdateDialog into MainWindow"
```

---

## Task 9: Final Integration Test and Push

**Step 1: Start license server locally**

```bash
cd B:/SANTI/license_server
python -m uvicorn app:app --host 0.0.0.0 --port 8000
```

**Step 2: Test the endpoint**

```bash
curl "http://localhost:8000/api/update/check?current_version=1.0.0&product=sample_monsta"
```

Expected: `{"update_available": true, "latest_version": "1.0.2", ...}`

**Step 3: Temporarily point SAMPLAR to localhost for testing**

In `UpdateCheckService.cpp`, temporarily change:
```cpp
juce::String baseUrl = "http://localhost:8000/api/update/check";
```

Build, run, verify:
- Console log shows update check succeeding
- Dialog appears with "What's New in v1.0.2"
- "Update Now" opens browser
- "Later" dismisses, green button appears in title bar
- Clicking green button reopens dialog

**Step 4: Revert to production URL**

Change back to `https://samplemonsta.com/api/update/check`

**Step 5: Push both repos**

```bash
cd B:/SANTI/license_server
git push

cd b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5
git push
```

**Step 6: Create initial version tag**

```bash
cd b:/SANTI/SAMPLE_MONSTA/sample_monsta4/Sample_monsta5
git tag v1.0.2
git push origin v1.0.2
```
