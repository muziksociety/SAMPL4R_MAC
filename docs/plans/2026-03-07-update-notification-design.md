# Update Notification & License Status System

**Date:** 2026-03-07
**Status:** Approved design, pending implementation

## Summary

Add a system that notifies SAMPLAR users when a new version is available and shows license/trial status reminders. The version check piggybacks a soft license validation for security.

## Three Components

### 1. Source of Truth: `data/releases.json` (License Server)

A single JSON file on the license server, edited manually per release. Both the website dashboard and the SAMPLAR app API read from it.

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

**Why this approach:**
- One file to edit per release
- Website dashboard template reads from it (replaces hardcoded changelog HTML)
- App update-check API reads from it
- Version-controlled in the license server repo
- No database changes needed

### 2. Server Side: `/api/update/check` Endpoint (License Server)

New plugin: `plugins/update_checker/`

**Request:**
```
GET /api/update/check?current_version=1.0.2&product=sample_monsta&hardware_id=ABC123
```

**Response:**
```json
{
  "update_available": true,
  "latest_version": "1.0.3",
  "current_version": "1.0.2",
  "download_url": "https://samplemonsta.com/portal",
  "release_date": "2026-03-01",
  "changes": [
    "New Spectral Clone engine",
    "Performance improvements"
  ],
  "license": {
    "status": "trial",
    "type": "TRL",
    "days_remaining": 3,
    "message": "You have 3 days left on your free trial"
  }
}
```

**Endpoint behavior:**
- Reads `data/releases.json` for version/changelog info
- Compares `current_version` param against `latest_version`
- If `hardware_id` provided: looks up license in database, returns status/days remaining
- No authentication required (lightweight check)
- License status values: `"active"`, `"trial"`, `"expired"`, `"unknown"`

**Dashboard template update:**
- `dashboard.html` changelog section reads from `data/releases.json` instead of hardcoded HTML
- Keeps website and app changelog in sync automatically

### 3. App Side: SAMPLAR Changes (C++/JUCE)

#### A. Build-Time Version from Git Tags

**CMake changes:**
- At configure time, run `git describe --tags --always` to extract version
- Generate `SamplarVersion.h` with `SAMPLAR_VERSION` define
- Fallback to `"0.0.0-dev"` if no tags exist

**Workflow:**
```
git tag v1.0.3
cmake -B build ...       # CMake extracts "1.0.3" from tag
cmake --build build      # Binary has version "1.0.3" compiled in
```

**App.h change:**
```cpp
#include "SamplarVersion.h"
const juce::String getApplicationVersion() override { return SAMPLAR_VERSION; }
```

#### B. New `UpdateCheckService` (implements `IService`)

**Location:** `src/services/UpdateCheckService.h/.cpp`

**Behavior:**
- `initialise()`: starts a background thread after a short delay (~5 seconds after startup)
- Sends GET to `/api/update/check` with current version + hardware ID
- Parses response
- Publishes EventBus events:
  - `"update.available"` — when newer version exists (payload: version, changes, download URL)
  - `"license.status.reminder"` — when trial is expiring or license needs attention (payload: status, days remaining, message)
- Stores "last dismissed version" in JUCE PropertiesFile to avoid re-showing the same update
- Check runs once per app launch (not periodic)

#### C. UI Components

**UpdateDialog** (`src/ui/Components/UpdateDialog.h/.cpp`):

```
+---------------------------------------+
|  What's New in v1.0.3                 |
|                                       |
|  * New Spectral Clone engine          |
|  * Performance improvements           |
|                                       |
|  [Update Now]            [Later]      |
|                                       |
|  --- trial/license info (if any) ---  |
|  You have 3 days left on your free    |
|  trial. Upgrade to PRO ->            |
+---------------------------------------+
```

- "Update Now" ? opens system browser to `download_url`
- "Later" ? dismisses, saves dismissed version to settings
- Trial/license section only appears when relevant
- Shown on startup when update is available AND not previously dismissed for this version

**NotificationButton** (`src/ui/Components/NotificationButton.h/.cpp`):

A small circle button in the title bar area (near drag/min/max/close buttons):

- **Green + blinking** ? update available (click reopens UpdateDialog)
- **Amber/orange + blinking** ? no update, but trial expiring (click opens dialog with license info only)
- **Hidden** ? no update + license is fine

Blinking: gentle pulse animation using JUCE Timer + repaint.

## Flow

```
App starts
    |
    v
UpdateCheckService.initialise()
    | (background thread, ~5s delay)
    v
GET samplemonsta.com/api/update/check
    ?current_version=1.0.2
    &product=sample_monsta
    &hardware_id=ABC123
    |
    v
Parse response
    |
    +---> update_available == true?
    |       |
    |       +---> Was this version dismissed?
    |               |           |
    |              NO          YES
    |               |           |
    |               v           v
    |         Show Dialog    Show green
    |                       NotificationButton
    |
    +---> license.days_remaining <= 7?
            |
            +---> Show amber NotificationButton
                  (or include in dialog if also showing update)
```

## What This Does NOT Include

- No auto-download or auto-install (user goes to dashboard)
- No GitHub API integration (private repo, not needed)
- No database schema changes on server
- No periodic background checking (once per launch only)
- No forced update blocking

## Files to Create/Modify

### License Server
| Action | File | Description |
|--------|------|-------------|
| CREATE | `data/releases.json` | Source of truth for versions/changelog |
| CREATE | `plugins/update_checker/__init__.py` | API endpoint plugin |
| MODIFY | `plugins/user_portal/templates/dashboard.html` | Read changelog from JSON instead of hardcoded |

### SAMPLAR App
| Action | File | Description |
|--------|------|-------------|
| MODIFY | `CMakeLists.txt` | Git tag version extraction + generated header |
| CREATE | `src/app/SamplarVersion.h.in` | CMake template for version header |
| MODIFY | `src/app/App.h` | Use generated version instead of hardcoded |
| CREATE | `src/services/UpdateCheckService.h` | Service header |
| CREATE | `src/services/UpdateCheckService.cpp` | Service implementation |
| CREATE | `src/ui/Components/UpdateDialog.h` | Update dialog header |
| CREATE | `src/ui/Components/UpdateDialog.cpp` | Update dialog implementation |
| CREATE | `src/ui/Components/NotificationButton.h` | Title bar button header |
| CREATE | `src/ui/Components/NotificationButton.cpp` | Title bar button implementation |
| MODIFY | `src/ui/MainWindow.h/.cpp` | Add NotificationButton to title bar |
| MODIFY | Service registration | Register UpdateCheckService |
