# OAuth Browser Authentication Implementation Guide

This document describes the complete OAuth browser-based authentication flow used in SAMPLAR. Use this as a reference for implementing the same system in other plugins.

## Overview

The OAuth flow allows users to authenticate via their web browser (Google OAuth) without entering credentials in the app. The flow is:

```
App → Opens Browser → Server Login Page → Google OAuth → Server validates →
Callback to localhost → App receives license data → Cache for 7 days
```

## Architecture Components

### 1. Server-Side (License Server)
- Handles Google OAuth flow
- Validates user credentials
- Issues signed license data
- Sends callback to localhost with license info

### 2. Client-Side (JUCE App)
- `OAuthBrowserAuthenticator` - Manages OAuth flow and localhost callback server
- `LicenseSystemService` - Coordinates authentication and caching
- `LicenseCache` - Encrypted local storage with 7-day revalidation
- `LicenseActivationDialog` - UI for login button

---

## Files Involved

### Core OAuth Files
```
src/networking/OAuthBrowserAuthenticator.h    - OAuth authenticator class definition
src/networking/OAuthBrowserAuthenticator.cpp  - Implementation (callback server, browser launch)
```

### License System Files
```
src/services/LicenseSystemService.h           - Service interface
src/services/LicenseSystemService.cpp         - OAuth integration, caching logic
src/security/LicenseCache.h                   - Cache structure and encryption
src/security/LicenseCache.cpp                 - BlowFish encryption, 7-day validation
```

### UI Files
```
src/ui/licensing/LicenseActivationDialog.h    - Login dialog UI
src/ui/licensing/LicenseActivationDialog.cpp  - Browser login button handler
src/ui/Components/SettingsDialog.cpp          - LicenseInfoPanel with deactivate button
```

---

## Step-by-Step OAuth Flow

### Step 1: User Clicks "Login with Browser"
**File:** `LicenseActivationDialog.cpp`

```cpp
void LicenseActivationDialog::performBrowserLogin()
{
    juce::Logger::writeToLog("[LicenseDialog] performBrowserLogin() - starting browser auth flow");
    isOAuthInProgress_ = true;  // Block dialog close during OAuth

    licenseService_.startBrowserAuthentication(
        [this](bool success, const juce::String& message) {
            // Callback when OAuth completes
            handleOAuthCallback(success, message);
        });
}
```

### Step 2: Start OAuth Authentication
**File:** `LicenseSystemService.cpp`

```cpp
void LicenseSystemService::startBrowserAuthentication(BrowserAuthCallback callback)
{
    // Create OAuth authenticator
    if (!oauthAuthenticator_) {
        oauthAuthenticator_ = std::make_unique<OAuthBrowserAuthenticator>();
    }

    // Start authentication with server URL and hardware ID
    oauthAuthenticator_->startAuthentication(serverUrl, hardwareId,
        [this, callback, hardwareId](const OAuthBrowserAuthenticator::AuthResult& result) {
            // Process result when OAuth completes
            handleOAuthResult(result, callback, hardwareId);
        });
}
```

### Step 3: Launch Localhost Callback Server
**File:** `OAuthBrowserAuthenticator.cpp`

```cpp
void OAuthBrowserAuthenticator::startAuthentication(
    const std::string& serverUrl,
    const std::string& hardwareId,
    CompletionCallback callback)
{
    // Generate random state token for CSRF protection
    stateToken_ = generateStateToken();

    // Start callback server on localhost:9876
    authThread_ = std::thread([this]() {
        runAuthenticationFlow();
    });
}

void OAuthBrowserAuthenticator::runAuthenticationFlow()
{
    // 1. Create TCP server listening on port 9876
    juce::StreamingSocket serverSocket;
    serverSocket.createListener(9876, "0.0.0.0");

    // 2. Build OAuth URL with callback info
    juce::String authUrl = juce::String(serverUrl_) + "/auth/app-login"
        + "?callback_port=9876"
        + "&state=" + juce::String(stateToken_)
        + "&hardware_id=" + juce::String(hardwareId_);

    // 3. Open URL in default browser
    juce::URL(authUrl).launchInDefaultBrowser();

    // 4. Wait for callback (60 second timeout)
    auto client = serverSocket.waitForNextConnection();

    // 5. Parse HTTP request and extract license data
    // ...
}
```

### Step 4: Browser Shows Server Login Page
The server at `https://license-server.example.com/auth/app-login`:
1. Shows Google OAuth login button
2. User authenticates with Google
3. Server validates license in database
4. Server redirects browser to `http://127.0.0.1:9876/callback?...`

### Step 5: Receive Callback with License Data
**File:** `OAuthBrowserAuthenticator.cpp`

```cpp
void OAuthBrowserAuthenticator::parseCallbackAndComplete(const juce::String& queryString)
{
    // Parse URL parameters
    auto params = parseQueryString(queryString);

    // Validate state token (CSRF protection)
    if (params["state"] != juce::String(stateToken_)) {
        result.success = false;
        result.errorMessage = "Invalid state token";
        return;
    }

    // Extract license data
    result.success = (params["success"] == "true");
    result.accessToken = params["token"].toStdString();
    result.licenseKey = params["license_key"].toStdString();
    result.email = params["email"].toStdString();
    result.licenseType = params["license_type"].toStdString();
    result.expiresAt = params["expires_at"].toStdString();
    result.hardwareId = params["hardware_id"].toStdString();
    result.signature = params["signature"].toStdString();

    // Build licenseInfo for caching
    auto* licenseObj = new juce::DynamicObject();
    licenseObj->setProperty("email", juce::String(result.email));
    licenseObj->setProperty("license_key", juce::String(result.licenseKey));
    licenseObj->setProperty("license_type", juce::String(result.licenseType));
    // ... more properties
    result.licenseInfo = juce::var(licenseObj);

    // Call completion callback
    completionCallback_(result);
}
```

### Step 6: Cache License Data
**File:** `LicenseSystemService.cpp`

```cpp
// In OAuth callback handler
if (result.success) {
    currentStatus_ = LicenseStatus::Valid;

    // Cache the license with encrypted data
    if (cache_ && !result.licenseKey.empty()) {
        cache_->storeLicense(result.licenseKey, result.licenseInfo, hardwareId);
        cache_->updateLastValidation(hardwareId);
    }
}
```

### Step 7: Encrypted Storage
**File:** `LicenseCache.cpp`

```cpp
bool LicenseCache::storeLicense(const std::string& licenseKey,
                                const juce::var& licenseData,
                                const std::string& hardwareId)
{
    CacheEntry entry;
    entry.licenseKey = licenseKey;
    entry.licenseData = encryptData(licenseData);  // BlowFish encryption
    entry.hardwareId = hardwareId;
    entry.cachedAt = std::chrono::system_clock::now();
    entry.lastValidated = entry.cachedAt;
    entry.integrityHash = computeIntegrityHash(entry);

    persistCache(entry);
    return true;
}

juce::var LicenseCache::encryptData(const juce::var& data) const
{
    auto key = deriveKeyMaterial();  // Derived from hardware ID
    juce::BlowFish cipher(key.getData(), static_cast<int>(key.getSize()));

    juce::String json = juce::JSON::toString(data, false);
    juce::MemoryBlock plain(json.toRawUTF8(), json.getNumBytesAsUTF8());

    // Pad to 8-byte block size
    while (plain.getSize() % 8 != 0)
        plain.append("\0", 1);

    cipher.encrypt(plain);

    auto* obj = new juce::DynamicObject();
    obj->setProperty("enc", true);
    obj->setProperty("data", toBase64(plain));
    return juce::var(obj);
}
```

---

## 7-Day Cache System

### Startup Validation Flow
**File:** `LicenseSystemService.cpp`

```cpp
void LicenseSystemService::performStartupValidation()
{
    auto cachedEntry = cache_->retrieveLicense(currentHardwareId_);

    if (cachedEntry.has_value()) {
        // Check if within 7-day revalidation window
        constexpr int REVALIDATION_INTERVAL_HOURS = 168;  // 7 days
        bool needsRevalidation = cache_->needsPeriodicValidation(
            currentHardwareId_, REVALIDATION_INTERVAL_HOURS);

        if (!needsRevalidation) {
            // Within 7 days - use cached license, no server check
            juce::Logger::writeToLog("[LicenseSystem] Within 7-day window - using cached license");
            currentStatus_ = LicenseStatus::Valid;
            loadCachedLicense();
            return;
        }

        // Beyond 7 days - require re-authentication
        // ...
    }

    // No cache - require login
    currentStatus_ = LicenseStatus::InvalidKey;
}
```

### Cache Validation Check
**File:** `LicenseCache.cpp`

```cpp
bool LicenseCache::needsPeriodicValidation(const std::string& hardwareId, int intervalHours) const
{
    const auto entry = retrieveLicense(hardwareId);
    if (!entry.has_value())
        return true;  // No cache = needs validation

    const auto now = std::chrono::system_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
        now - entry->lastValidated);

    return elapsed.count() >= intervalHours;
}
```

---

## Deactivation

### Local Deactivation (Clears Cache)
**File:** `LicenseSystemService.cpp`

```cpp
bool LicenseSystemService::deactivateLicense()
{
    // Try server deactivation (may fail for OAuth licenses)
    const auto response = serverClient_->deactivateLicense(licenseKey, hardwareId);

    // Always clear local cache regardless of server response
    if (response.statusCode == 404) {
        // OAuth licenses don't have server deactivation endpoint
        juce::Logger::writeToLog("[LicenseSystem] Clearing local cache");
        cache_->clearCache();
        sessionManager_->clearSession(hardwareId);
        resetCurrentLicense();
        currentStatus_ = LicenseStatus::InvalidKey;
        return true;
    }

    // Handle other cases...
}
```

---

## Security Considerations

### Strengths

1. **Credentials Never Touch App**
   - User authenticates via Google OAuth on server
   - App never sees username/password

2. **State Token (CSRF Protection)**
   - Random token generated per auth attempt
   - Callback must include matching token
   - Prevents malicious redirects

3. **Hardware-Bound Licenses**
   - License tied to hardware ID
   - Can't copy license to another machine

4. **Encrypted Local Cache**
   - BlowFish encryption with hardware-derived key
   - Integrity hash prevents tampering

5. **Signed License Data**
   - Server signs license data
   - Signature can be verified client-side

### Trade-offs

1. **Localhost Callback Not HTTPS**
   - Callback URL visible in browser history
   - Mitigated by: one-time use, state token validation
   - Consider: Clearing browser history after callback

2. **Token in URL Parameters**
   - License data visible in callback URL
   - Mitigated by: Short-lived tokens, signed data

3. **Local Cache Can Be Deleted**
   - User can delete cache to force re-login
   - Not a security issue, just UX consideration

---

## Implementation Checklist for Other Plugins

### 1. Add OAuth Authenticator Class
- [ ] Copy `OAuthBrowserAuthenticator.h/.cpp`
- [ ] Update namespace/class names
- [ ] Configure callback port (avoid conflicts)

### 2. Add License Cache
- [ ] Copy `LicenseCache.h/.cpp`
- [ ] Update encryption key derivation
- [ ] Configure cache file path

### 3. Integrate with License Service
- [ ] Add `startBrowserAuthentication()` method
- [ ] Handle OAuth callback results
- [ ] Implement 7-day cache logic

### 4. Add UI
- [ ] Add "Login with Browser" button
- [ ] Show login status
- [ ] Add deactivate button in settings

### 5. Server-Side
- [ ] Add OAuth endpoint `/auth/app-login`
- [ ] Handle Google OAuth callback
- [ ] Generate signed license data
- [ ] Redirect to `localhost:PORT/callback?...`

---

## Callback URL Format

```
http://127.0.0.1:9876/callback?
  success=true
  &token=<JWT_TOKEN>
  &license_key=<LICENSE_KEY>
  &email=<USER_EMAIL>
  &license_type=<PRO|BASIC|etc>
  &expires_at=<ISO8601_DATE|perpetual>
  &hardware_id=<HARDWARE_ID>
  &state=<STATE_TOKEN>
  &issued_at=<ISO8601_DATE>
  &signature=<SERVER_SIGNATURE>
```

---

## Troubleshooting

### Common Issues

1. **"Can't reach this page" in browser after login**
   - This is normal - callback server closes after receiving data
   - The app should still receive the license data

2. **Cache decryption fails**
   - Check if `licenseInfo` is being populated in OAuth callback
   - Verify BlowFish padding is trimmed during decryption

3. **State token mismatch**
   - Ensure state token is URL-encoded properly
   - Check for race conditions in token generation

4. **Port 9876 in use**
   - Kill previous app instances
   - Or configure different port

---

## Testing

### Manual Test Flow
1. Delete `license_cache.json` from AppData
2. Launch app → should show login dialog
3. Click "Login with Browser"
4. Complete Google OAuth in browser
5. App should show "License validated successfully"
6. Close and restart app → should skip login (7-day cache)
7. Go to Settings → Deactivate → should clear cache
8. Restart app → should show login dialog again

### Log Messages to Watch
```
[OAuth] Callback server started on port 9876
[OAuth] Browser launched successfully
[OAuth] Received callback query: success=true...
[OAuth] License data received - email: ..., type: ...
[LicenseSystem] Caching OAuth license...
[LicenseCache] Cache retrieved successfully
[LicenseSystem] Within 7-day window - using cached license
```
