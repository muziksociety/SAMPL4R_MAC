# SM4 License System - C++ Porting Plan for SAMPLAR

## Executive Summary

This document provides a comprehensive plan for porting the SM4 Python-based license system to C++ for SAMPLAR. The plan maintains all security features, hardware binding, and user experience while adapting to the C++/JUCE architecture.

## Status Update (2025-09-18)
- Hardware fingerprinting now includes platform-specific identifiers and deterministic validation (see `src/security/HardwareFingerprintGenerator.cpp`).
- License cache and session storage use AES-encrypted payloads with per-machine keying.
- A reusable `LicenseStatusWidget` surfaces status changes inside the main UI and hooks the EventBus.
- JUCE unit tests for licensing components can be executed with `SAMPLAR_RUN_TESTS=1` (sources under `tests/licensing`).
- Migration guidance is documented in `docs/SM4_LICENSE_MIGRATION_NOTES.md`, including cache override utilities.
- A dedicated cleanup utility (`SM5LicenseCleaner`) removes local license caches and sessions for support resets.

## 1. Architecture Overview

### Current SM4 System Analysis
- **Python-based licensing** with PyQt5 UI components
- **Hardware fingerprinting** using system commands and platform APIs
- **RESTful API communication** with secure license server
- **Local cache encryption** with tamper protection
- **Multi-tier license types** (Trial, Basic, Standard, Professional, Education, Enterprise)
- **48-hour offline grace period** with automatic re-validation

### Target SAMPLAR Architecture
- **C++ implementation** using JUCE framework
- **Plugin-based architecture** following BIBLE.MD compliance
- **Cross-platform compatibility** (Windows, macOS, Linux)
- **Modern C++ features** (C++17 minimum)
- **JUCE networking** for HTTP communication
- **OS-specific APIs** for hardware fingerprinting

## 2. Core Components Mapping

### 2.1 Main Service (`LicenseSystemService`)
**Source**: `plugins/license_system/service.py`
**Target**: `src/services/LicenseSystemService.h/.cpp`

```cpp
class LicenseSystemService : public ServiceInterface {
public:
    enum class LicenseStatus {
        VALID,
        EXPIRED,
        HARDWARE_MISMATCH,
        ACTIVATION_LIMIT_EXCEEDED,
        INVALID_KEY,
        NETWORK_ERROR,
        GRACE_PERIOD_ACTIVE,
        GRACE_PERIOD_EXPIRED,
        USER_LOCKED_OUT
    };

    struct LicenseInfo {
        std::string customerEmail;
        std::string licenseType;  // TRL, BAS, STD, PRO, EDU, ENT
        std::chrono::system_clock::time_point expiresAt;
        std::vector<std::string> features;
        int maxActivations;
        int currentActivations;
    };

    void initialise() override;
    void shutdown() override;

    // Core license operations
    LicenseStatus validateLicense(const std::string& licenseKey);
    bool activateLicense(const std::string& licenseKey, const std::string& email, const std::string& password);
    bool isFeatureEnabled(const std::string& featureName) const;
    LicenseInfo getCurrentLicenseInfo() const;

    // Hardware binding
    std::string generateHardwareId() const;
    bool isHardwareValid() const;

    // Offline/grace period management
    bool hasOfflineGrace() const;
    std::chrono::minutes getRemainingGraceTime() const;

private:
    std::unique_ptr<HardwareFingerprintGenerator> hardwareGenerator_;
    std::unique_ptr<LicenseCache> cache_;
    std::unique_ptr<LicenseServerClient> serverClient_;
    std::unique_ptr<SecureSessionManager> sessionManager_;

    LicenseStatus currentStatus_;
    LicenseInfo currentLicense_;
    std::string currentHardwareId_;
};
```

### 2.2 Hardware Fingerprinting (`HardwareFingerprintGenerator`)
**Source**: `plugins/license_system/hardware.py`
**Target**: `src/security/HardwareFingerprintGenerator.h/.cpp`

```cpp
class HardwareFingerprintGenerator {
public:
    struct HardwareComponents {
        std::string cpuInfo;
        std::string macAddress;
        std::string motherboardSerial;
        std::string systemUuid;
        std::string diskSerial;
    };

    // Primary hardware ID generation
    std::string generateHardwareId();

    // Consistency verification (10 iterations)
    bool verifyHardwareIdConsistency();

    // Component extraction methods
    HardwareComponents gatherHardwareComponents() const;

private:
    // Platform-specific implementations
    std::string getCpuInfo() const;
    std::string getMacAddress() const;
    std::string getMotherboardSerial() const;
    std::string getSystemUuid() const;
    std::string getDiskSerial() const;

    // Cross-platform system command execution
    std::string executeSystemCommand(const std::string& command) const;

    // Security: SHA256 hashing
    std::string computeSha256Hash(const std::string& input) const;

    mutable std::mutex hardwareIdMutex_;
    mutable std::optional<std::string> cachedHardwareId_;
};
```

**Platform-Specific Implementation Details**:
```cpp
// Windows implementation using WinAPI and WMI
#ifdef _WIN32
std::string HardwareFingerprintGenerator::getMotherboardSerial() const {
    // Use CoInitialize, CoCreateInstance for WMI queries
    // Alternative: Execute "wmic baseboard get serialnumber"
}

std::string HardwareFingerprintGenerator::getSystemUuid() const {
    // Registry: HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\MachineGuid
    // Alternative: "wmic csproduct get uuid"
}
#endif

// macOS implementation using IOKit/SystemConfiguration
#ifdef __APPLE__
std::string HardwareFingerprintGenerator::getMotherboardSerial() const {
    // IOServiceMatching, IOServiceGetMatchingService
    // system_profiler SPHardwareDataType parsing
}
#endif

// Linux implementation using /proc and /sys
#ifdef __linux__
std::string HardwareFingerprintGenerator::getCpuInfo() const {
    // Parse /proc/cpuinfo
    // Read /sys/class/dmi/id/board_serial
}
#endif
```

### 2.3 License Cache with Encryption (`LicenseCache`)
**Source**: `plugins/license_system/cache.py`
**Target**: `src/security/LicenseCache.h/.cpp`

```cpp
class LicenseCache {
public:
    struct CacheEntry {
        std::string licenseKey;
        juce::var licenseData;
        std::string hardwareId;
        std::chrono::system_clock::time_point cachedAt;
        std::chrono::system_clock::time_point lastValidated;
        std::string integrityHash;
    };

    // Cache management
    bool storeLicense(const std::string& licenseKey, const juce::var& licenseData, const std::string& hardwareId);
    std::optional<CacheEntry> retrieveLicense(const std::string& hardwareId);
    bool isCacheValid(const std::string& hardwareId);
    void clearCache();

    // Periodic validation tracking
    bool needsPeriodicValidation(const std::string& hardwareId, int intervalHours = 24);
    void updateLastValidation(const std::string& hardwareId);

    // Offline grace period
    struct GraceStatus {
        bool hasGrace;
        std::chrono::minutes remainingTime;
        std::chrono::system_clock::time_point graceExpiresAt;
    };
    GraceStatus getOfflineGraceStatus(const std::string& hardwareId, int gracePeriodHours = 48);

    // Security measures
    bool verifyIntegrity(const CacheEntry& entry);
    void recordFailedAttempt(const std::string& username, const std::string& hardwareId);
    bool isUserLockedOut(const std::string& username, const std::string& hardwareId);

private:
    juce::File getCacheDirectory();
    juce::File getLicenseCacheFile();
    juce::File getFailedAttemptsFile();

    std::string computeIntegrityHash(const CacheEntry& entry);
    juce::var encryptData(const juce::var& data);
    juce::var decryptData(const juce::var& encryptedData);

    std::mutex cacheMutex_;

    // Security configuration
    static constexpr int MAX_FAILED_ATTEMPTS = 5;
    static constexpr int LOCKOUT_DURATION_MINUTES = 30;
};
```

### 2.4 License Server Communication (`LicenseServerClient`)
**Source**: `plugins/license_system/client.py`
**Target**: `src/networking/LicenseServerClient.h/.cpp`

```cpp
class LicenseServerClient {
public:
    struct ValidationRequest {
        std::string licenseKey;
        std::string hardwareId;
        std::string appVersion;
        std::string userAgent;
        std::string ipAddress;
    };

    struct ValidationResponse {
        bool valid;
        juce::var licenseInfo;
        juce::var validationInfo;
        std::string message;
        int statusCode;
    };

    struct ActivationRequest {
        std::string licenseKey;
        std::string hardwareId;
        std::string email;
        std::string password;
    };

    // Primary API methods
    ValidationResponse validateLicense(const ValidationRequest& request);
    ValidationResponse activateLicense(const ActivationRequest& request);
    ValidationResponse authenticateWithHardware(const std::string& email, const std::string& password, const std::string& hardwareId);

    // Network configuration
    void setServerUrl(const std::string& url);
    void setTimeout(int timeoutSeconds);
    void setRetryAttempts(int attempts);

private:
    // JUCE HTTP implementation
    std::unique_ptr<juce::URL::DownloadTask> createHttpRequest(const juce::URL& url, const juce::String& postData);
    ValidationResponse parseValidationResponse(const juce::String& responseBody, int statusCode);
    juce::String createJsonPayload(const juce::var& data);

    std::string serverUrl_ = "https://license-server-n5it.onrender.com";
    int timeoutSeconds_ = 8;
    int retryAttempts_ = 2;
    int retryDelayMs_ = 1000;

    std::mutex networkMutex_;
};
```

### 2.5 Secure Session Management (`SecureSessionManager`)
**Source**: `plugins/license_system/secure_session_manager.py`
**Target**: `src/security/SecureSessionManager.h/.cpp`

```cpp
class SecureSessionManager {
public:
    struct SessionData {
        std::string accessToken;
        std::string userEmail;
        int userId;
        std::chrono::system_clock::time_point expiresAt;
        std::string hardwareId;
    };

    // Session management
    bool storeSession(const SessionData& session);
    std::optional<SessionData> retrieveSession(const std::string& hardwareId);
    bool isSessionValid(const std::string& hardwareId);
    void clearSession(const std::string& hardwareId);
    void clearExpiredSessions();

    // Encryption methods
    bool isEncryptionAvailable() const;

private:
    // Encryption using platform-specific methods
    juce::String encryptData(const juce::String& data);
    juce::String decryptData(const juce::String& encryptedData);
    juce::String generateEncryptionKey();

    juce::File getSessionFile(const std::string& hardwareId);
    juce::File getEncryptionKeyFile();

    std::mutex sessionMutex_;
    static constexpr int SESSION_DURATION_DAYS = 7;
};
```

### 2.6 Feature Access Control (`FeatureManager`)
**Source**: `plugins/license_system/features.py`
**Target**: `src/licensing/FeatureManager.h/.cpp`

```cpp
class FeatureManager {
public:
    enum class FeatureType {
        PREMIUM_SAMPLES,
        UNLIMITED_EXPORTS,
        CLOUD_SYNC,
        ADVANCED_EFFECTS,
        BATCH_PROCESSING,
        CUSTOM_PRESETS,
        PRIORITY_SUPPORT,
        COMMERCIAL_LICENSE
    };

    // Feature configuration by license type
    struct LicenseFeatures {
        std::set<FeatureType> enabledFeatures;
        int maxExportsPerSession;
        int maxSampleLibrarySize;
        int maxProjectSize;
        bool hasWatermark;
    };

    // License type definitions (matching SM4)
    static const std::map<std::string, LicenseFeatures> LICENSE_TYPE_FEATURES;

    // Feature checking
    bool isFeatureEnabled(FeatureType feature, const std::string& licenseType) const;
    bool canPerformAction(const std::string& action, const std::string& licenseType) const;
    LicenseFeatures getFeaturesForLicense(const std::string& licenseType) const;

    // Usage tracking
    void recordFeatureUsage(FeatureType feature);
    bool hasReachedUsageLimit(const std::string& action, const std::string& licenseType) const;

private:
    std::map<FeatureType, int> usageCounters_;
    std::mutex featureMutex_;
};
```

## 3. UI Components (JUCE Implementation)

### 3.1 License Activation Dialog
**Source**: `plugins/license_system/ui/activation_dialog.py`
**Target**: `src/ui/licensing/LicenseActivationDialog.h/.cpp`

```cpp
class LicenseActivationDialog : public juce::DialogWindow {
public:
    LicenseActivationDialog(LicenseSystemService* licenseService);

    // Dialog management
    void showActivationDialog();
    void closeButtonPressed() override;

    // Callbacks
    std::function<void(bool success, const std::string& message)> onActivationComplete;

private:
    class ActivationComponent : public juce::Component {
    public:
        ActivationComponent(LicenseSystemService* service);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        // UI Components
        juce::Label titleLabel_ { {}, "License Activation" };
        juce::Label instructionsLabel_;

        juce::Label licenseKeyLabel_ { {}, "License Key:" };
        juce::TextEditor licenseKeyEditor_;

        juce::Label emailLabel_ { {}, "Email:" };
        juce::TextEditor emailEditor_;

        juce::Label passwordLabel_ { {}, "Password:" };
        juce::TextEditor passwordEditor_;

        juce::Label hardwareIdLabel_ { {}, "Hardware ID:" };
        juce::Label hardwareIdDisplay_;

        juce::TextButton activateButton_ { "Activate License" };
        juce::TextButton cancelButton_ { "Cancel" };

        juce::ProgressBar progressBar_;
        juce::Label statusLabel_;

        // License validation
        void validateLicenseKey();
        void performActivation();
        void updateProgress(float progress, const juce::String& message);

        LicenseSystemService* licenseService_;
        std::unique_ptr<juce::ThreadWithProgressWindow> activationThread_;
    };

    std::unique_ptr<ActivationComponent> activationComponent_;
    LicenseSystemService* licenseService_;
};
```

### 3.2 License Status Widget
**Source**: `plugins/license_system/ui/status_widget.py`
**Target**: `src/ui/licensing/LicenseStatusWidget.h/.cpp`

```cpp
class LicenseStatusWidget : public juce::Component {
public:
    LicenseStatusWidget(LicenseSystemService* licenseService);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Status updates
    void updateLicenseStatus();
    void setVisible(bool shouldBeVisible) override;

private:
    // UI Components
    juce::Label statusLabel_;
    juce::Label licenseTypeLabel_;
    juce::Label expirationLabel_;
    juce::Label hardwareIdLabel_;
    juce::ProgressBar graceProgressBar_;

    // Action buttons
    juce::TextButton activateButton_ { "Activate License" };
    juce::TextButton renewButton_ { "Renew License" };
    juce::TextButton supportButton_ { "Contact Support" };

    // Status visualization
    juce::Colour getStatusColour(LicenseSystemService::LicenseStatus status) const;
    juce::String getStatusText(LicenseSystemService::LicenseStatus status) const;
    void showLicenseExpiredDialog();
    void showHardwareMismatchDialog();
    void showGracePeriodWarning();

    LicenseSystemService* licenseService_;
    LicenseSystemService::LicenseStatus lastStatus_;

    static constexpr int UPDATE_INTERVAL_MS = 30000; // 30 seconds
};
```

## 4. Security Implementation Details

### 4.1 Cryptographic Security
```cpp
// Using JUCE's built-in cryptography + platform APIs
class CryptographicSecurity {
public:
    // SHA-256 hashing for integrity
    static juce::String computeSha256(const juce::String& input) {
        return juce::SHA256(input.toUTF8()).toHexString();
    }

    // AES encryption for cache data
    static juce::MemoryBlock encryptAES(const juce::MemoryBlock& data, const juce::String& key) {
        // Use platform-specific crypto APIs
#ifdef _WIN32
        // Windows CryptoAPI
#elif __APPLE__
        // CommonCrypto
#elif __linux__
        // OpenSSL
#endif
    }

    // Random key generation
    static juce::String generateSecureKey(int length = 32) {
        juce::Random random(juce::Time::currentTimeMillis());
        juce::String key;
        const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < length; ++i) {
            key += charset[random.nextInt(sizeof(charset) - 1)];
        }
        return key;
    }
};
```

### 4.2 Anti-Tampering Measures
```cpp
class AntiTamperingProtection {
public:
    // File integrity verification
    static bool verifyFileIntegrity(const juce::File& file, const juce::String& expectedHash) {
        if (!file.exists()) return false;

        juce::FileInputStream stream(file);
        if (!stream.openedOk()) return false;

        juce::SHA256 hasher;
        const int bufferSize = 8192;
        juce::HeapBlock<char> buffer(bufferSize);

        while (!stream.isExhausted()) {
            auto bytesRead = stream.read(buffer, bufferSize);
            hasher.process(buffer, bytesRead);
        }

        return hasher.toHexString() == expectedHash;
    }

    // Process integrity checking
    static bool isDebuggerPresent() {
#ifdef _WIN32
        return IsDebuggerPresent() != 0;
#else
        // Platform-specific debugger detection
        return false;
#endif
    }

    // Timing-based tamper detection
    static bool performTimingCheck() {
        auto start = juce::Time::getHighResolutionTicks();
        // Perform known operation
        juce::Thread::sleep(10);
        auto end = juce::Time::getHighResolutionTicks();

        auto elapsed = juce::Time::highResolutionTicksToSeconds(end - start);
        return elapsed >= 0.008 && elapsed <= 0.015; // Expected range
    }
};
```

### 4.3 Code Obfuscation Strategy
```cpp
// Compile-time string encryption
template<int N>
class ObfuscatedString {
private:
    char data_[N];

public:
    constexpr ObfuscatedString(const char(&str)[N]) {
        for (int i = 0; i < N; ++i) {
            data_[i] = str[i] ^ (i + 0x55); // Simple XOR obfuscation
        }
    }

    std::string decrypt() const {
        std::string result;
        for (int i = 0; i < N - 1; ++i) {
            result += static_cast<char>(data_[i] ^ (i + 0x55));
        }
        return result;
    }
};

#define OBFUSCATED_STRING(str) ObfuscatedString(str).decrypt()

// Usage:
const auto SERVER_URL = OBFUSCATED_STRING("https://license-server-n5it.onrender.com");
```

## 5. Integration with SAMPLAR Architecture

### 5.1 Service Registration
```cpp
// In main.cpp or plugin registration
void registerLicenseSystem(CoreApp& app) {
    auto licenseService = std::make_unique<LicenseSystemService>(app.eventBus());
    app.registerService("license_system", std::move(licenseService));

    // Register as critical service (priority 1)
    app.eventBus().publish("service.register_critical", {
        {"name", "license_system"},
        {"priority", 1},
        {"blocking", true}  // Must initialize before other services
    });
}
```

### 5.2 Event Bus Integration
```cpp
class LicenseSystemService : public ServiceInterface {
private:
    void setupEventSubscriptions() {
        // License validation requests
        eventBus_.subscribe("license.validate", [this](const juce::var& data) {
            auto licenseKey = data["license_key"].toString().toStdString();
            auto status = validateLicense(licenseKey);

            eventBus_.publish("license.validation_result", {
                {"status", static_cast<int>(status)},
                {"valid", status == LicenseStatus::VALID}
            });
        });

        // Feature access checks
        eventBus_.subscribe("license.check_feature", [this](const juce::var& data) {
            auto feature = data["feature"].toString().toStdString();
            auto enabled = isFeatureEnabled(feature);

            eventBus_.publish("license.feature_result", {
                {"feature", feature},
                {"enabled", enabled}
            });
        });

        // Application startup hook
        eventBus_.subscribe("app.startup", [this](const juce::var&) {
            performStartupValidation();
        });
    }

    void performStartupValidation() {
        auto hardwareId = generateHardwareId();

        // Check cached license first
        if (auto cachedLicense = cache_->retrieveLicense(hardwareId)) {
            if (cache_->isCacheValid(hardwareId)) {
                currentStatus_ = LicenseStatus::VALID;
                return;
            }
        }

        // No valid cache - require license activation
        currentStatus_ = LicenseStatus::INVALID_KEY;
        eventBus_.publish("license.activation_required", {});
    }
};
```

### 5.3 Feature Gating Implementation
```cpp
// Feature gating throughout the application
class FeatureGate {
public:
    static bool checkFeature(const std::string& featureName, EventBus& eventBus) {
        // Synchronous feature check
        auto future = std::make_shared<std::promise<bool>>();
        auto result = future->get_future();

        auto handlerId = eventBus.subscribe("license.feature_result",
            [future, featureName](const juce::var& data) {
                if (data["feature"].toString().toStdString() == featureName) {
                    future->set_value(data["enabled"].operator bool());
                }
            });

        eventBus.publish("license.check_feature", {{"feature", featureName}});

        // Wait for result with timeout
        auto status = result.wait_for(std::chrono::seconds(1));
        eventBus.unsubscribe("license.feature_result", handlerId);

        return status == std::future_status::ready ? result.get() : false;
    }
};

// Usage in application code:
void SampleBrowserPage::showExportDialog() {
    if (!FeatureGate::checkFeature("unlimited_exports", eventBus_)) {
        showFeatureLockedDialog("Export functionality requires a Professional license.");
        return;
    }

    // Proceed with export...
}
```

## 6. Build System Integration

### 6.1 CMake Configuration
```cmake
# License system dependencies
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Platform-specific libraries
if(WIN32)
    set(PLATFORM_LIBS wmi ole32 oleaut32 uuid)
elseif(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    set(PLATFORM_LIBS ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
elseif(UNIX)
    set(PLATFORM_LIBS uuid)
endif()

# License system target
add_library(LicenseSystem STATIC
    src/services/LicenseSystemService.cpp
    src/security/HardwareFingerprintGenerator.cpp
    src/security/LicenseCache.cpp
    src/networking/LicenseServerClient.cpp
    src/security/SecureSessionManager.cpp
    src/licensing/FeatureManager.cpp
    src/ui/licensing/LicenseActivationDialog.cpp
    src/ui/licensing/LicenseStatusWidget.cpp
)

target_link_libraries(LicenseSystem
    PRIVATE
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ${PLATFORM_LIBS}
    juce::juce_core
    juce::juce_gui_basics
    juce::juce_cryptography
)

# Compile-time obfuscation flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(LicenseSystem PRIVATE ENABLE_OBFUSCATION=1)
endif()
```

### 6.2 Deployment Considerations
```cpp
// Deployment security measures
namespace DeploymentSecurity {
    // Binary packing/protection
    constexpr bool ENABLE_BINARY_PROTECTION = true;

    // Code signing requirements
    constexpr bool REQUIRE_CODE_SIGNING = true;

    // License server configuration
    constexpr const char* PRODUCTION_SERVER = "https://license-server-n5it.onrender.com";
    constexpr const char* STAGING_SERVER = "https://staging-license-server.onrender.com";

    // Feature flags
    constexpr bool ENABLE_TRIAL_MODE = true;
    constexpr int TRIAL_DURATION_DAYS = 14;
    constexpr bool ENABLE_OFFLINE_GRACE = true;
    constexpr int OFFLINE_GRACE_HOURS = 48;
}
```

## 7. Testing Strategy

### 7.1 Unit Tests
```cpp
class LicenseSystemTests : public juce::UnitTest {
public:
    LicenseSystemTests() : juce::UnitTest("License System Tests") {}

    void runTest() override {
        beginTest("Hardware ID Generation");
        testHardwareIdGeneration();

        beginTest("License Cache Integrity");
        testLicenseCacheIntegrity();

        beginTest("Offline Grace Period");
        testOfflineGracePeriod();

        beginTest("Feature Access Control");
        testFeatureAccessControl();

        beginTest("Hardware Binding");
        testHardwareBinding();
    }

private:
    void testHardwareIdGeneration() {
        HardwareFingerprintGenerator generator;

        // Test consistency
        auto id1 = generator.generateHardwareId();
        auto id2 = generator.generateHardwareId();
        expect(id1 == id2, "Hardware ID should be consistent");

        // Test format
        expect(id1.length() == 32, "Hardware ID should be 32 characters");
        expect(std::all_of(id1.begin(), id1.end(), ::isxdigit), "Hardware ID should be hexadecimal");
    }

    void testLicenseCacheIntegrity() {
        LicenseCache cache;

        // Store license
        juce::var licenseData;
        licenseData["type"] = "PRO";
        licenseData["expires"] = "2025-12-31T23:59:59Z";

        expect(cache.storeLicense("TEST-KEY", licenseData, "TEST-HARDWARE"),
               "License storage should succeed");

        // Retrieve and verify
        auto retrieved = cache.retrieveLicense("TEST-HARDWARE");
        expect(retrieved.has_value(), "License should be retrievable");
        expect(cache.verifyIntegrity(*retrieved), "License integrity should be valid");
    }

    // Additional test methods...
};
```

### 7.2 Integration Tests
```cpp
class LicenseIntegrationTests : public juce::UnitTest {
public:
    LicenseIntegrationTests() : juce::UnitTest("License Integration Tests") {}

    void runTest() override {
        beginTest("Full Activation Workflow");
        testFullActivationWorkflow();

        beginTest("Network Failure Handling");
        testNetworkFailureHandling();

        beginTest("Grace Period Behavior");
        testGracePeriodBehavior();
    }

private:
    void testFullActivationWorkflow() {
        // Mock license server
        MockLicenseServer mockServer;
        mockServer.start();

        LicenseSystemService service(mockEventBus_);
        service.initialise();

        // Test activation
        bool activationResult = service.activateLicense("TEST-LICENSE-KEY", "test@example.com", "password");
        expect(activationResult, "License activation should succeed");

        // Test validation
        auto status = service.validateLicense("TEST-LICENSE-KEY");
        expect(status == LicenseSystemService::LicenseStatus::VALID, "License should be valid after activation");

        mockServer.stop();
    }
};
```

## 8. Migration Strategy

### 8.1 Data Migration from SM4
```cpp
class SM4DataMigration {
public:
    struct MigrationResult {
        bool success;
        std::string message;
        int migratedLicenses;
        int migratedSessions;
    };

    static MigrationResult migrateLicenseData() {
        MigrationResult result{false, "", 0, 0};

        try {
            // Find SM4 cache directory
            auto sm4CacheDir = findSM4CacheDirectory();
            if (!sm4CacheDir.exists()) {
                result.message = "No SM4 license data found";
                return result;
            }

            // Migrate license cache
            auto licenseCache = sm4CacheDir.getChildFile("license_cache.json");
            if (licenseCache.exists()) {
                migrateLicenseCache(licenseCache);
                result.migratedLicenses++;
            }

            // Migrate user sessions
            auto userCredentials = sm4CacheDir.getChildFile("user_credentials.json");
            if (userCredentials.exists()) {
                migrateUserSessions(userCredentials);
                result.migratedSessions++;
            }

            result.success = true;
            result.message = "Migration completed successfully";

        } catch (const std::exception& e) {
            result.message = "Migration failed: " + std::string(e.what());
        }

        return result;
    }

private:
    static juce::File findSM4CacheDirectory() {
        // Look for SM4 cache in standard locations
        auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

        std::vector<juce::String> possiblePaths = {
            ".sample_monsta4/cache",
            ".cache/sample_monsta4",
            "AppData/Local/samplar4/cache"  // Windows
        };

        for (const auto& path : possiblePaths) {
            auto cacheDir = homeDir.getChildFile(path);
            if (cacheDir.exists()) {
                return cacheDir;
            }
        }

        return {};
    }

    static void migrateLicenseCache(const juce::File& sm4CacheFile) {
        // Read SM4 cache format and convert to SAMPLAR format
        // Handle potential decryption if needed
    }

    static void migrateUserSessions(const juce::File& sm4CredentialsFile) {
        // Migrate user authentication sessions
        // Re-encrypt using SAMPLAR security methods
    }
};
```

### 8.2 Backward Compatibility
```cpp
class BackwardCompatibility {
public:
    // Support SM4 license key format during transition period
    static bool isLegacyLicenseKey(const std::string& licenseKey) {
        return licenseKey.substr(0, 4) == "SM4-";
    }

    static std::string convertLegacyLicenseKey(const std::string& sm4Key) {
        // Convert SM4-XXX-XXXXXXXX to SAMPLAR-XXX-XXXXXXXX format
        if (isLegacyLicenseKey(sm4Key)) {
            return "SAMPLAR" + sm4Key.substr(3);
        }
        return sm4Key;
    }

    // Handle SM4 feature mappings
    static std::vector<std::string> mapLegacyFeatures(const std::vector<std::string>& sm4Features) {
        std::vector<std::string> sm5Features;

        std::map<std::string, std::string> featureMapping = {
            {"premium_samples", "premium_samples"},
            {"unlimited_exports", "unlimited_exports"},
            {"cloud_sync", "cloud_sync"},
            // Add more mappings as needed
        };

        for (const auto& feature : sm4Features) {
            auto it = featureMapping.find(feature);
            if (it != featureMapping.end()) {
                sm5Features.push_back(it->second);
            }
        }

        return sm5Features;
    }
};
```

## 9. Performance Optimization

### 9.1 Caching Strategy
```cpp
class PerformanceOptimizedLicenseService {
private:
    // Memory caching for frequent license checks
    mutable std::map<std::string, std::pair<bool, std::chrono::steady_clock::time_point>> featureCache_;
    mutable std::mutex featureCacheMutex_;
    static constexpr auto FEATURE_CACHE_DURATION = std::chrono::minutes(5);

    // Hardware ID caching (expensive to generate)
    mutable std::optional<std::string> cachedHardwareId_;
    mutable std::chrono::steady_clock::time_point hardwareIdCacheTime_;
    static constexpr auto HARDWARE_ID_CACHE_DURATION = std::chrono::hours(1);

public:
    bool isFeatureEnabled(const std::string& featureName) const override {
        std::lock_guard<std::mutex> lock(featureCacheMutex_);

        auto now = std::chrono::steady_clock::now();
        auto it = featureCache_.find(featureName);

        if (it != featureCache_.end()) {
            auto [cached_result, cache_time] = it->second;
            if (now - cache_time < FEATURE_CACHE_DURATION) {
                return cached_result;
            }
        }

        // Cache miss or expired - perform full check
        bool result = performFullFeatureCheck(featureName);
        featureCache_[featureName] = {result, now};

        return result;
    }

    std::string generateHardwareId() const override {
        auto now = std::chrono::steady_clock::now();

        if (cachedHardwareId_.has_value() &&
            now - hardwareIdCacheTime_ < HARDWARE_ID_CACHE_DURATION) {
            return *cachedHardwareId_;
        }

        // Generate new hardware ID
        auto hardwareId = hardwareGenerator_->generateHardwareId();
        cachedHardwareId_ = hardwareId;
        hardwareIdCacheTime_ = now;

        return hardwareId;
    }
};
```

### 9.2 Asynchronous Operations
```cpp
class AsyncLicenseOperations {
public:
    // Non-blocking license validation
    std::future<LicenseSystemService::LicenseStatus> validateLicenseAsync(const std::string& licenseKey) {
        return std::async(std::launch::async, [this, licenseKey]() {
            return validateLicenseSync(licenseKey);
        });
    }

    // Background periodic validation
    void startPeriodicValidation() {
        periodicValidationThread_ = std::thread([this]() {
            while (isRunning_) {
                std::this_thread::sleep_for(std::chrono::hours(1));

                if (needsValidation()) {
                    auto status = validateLicenseSync(getCurrentLicenseKey());

                    // Notify on status change
                    if (status != lastKnownStatus_) {
                        eventBus_.publish("license.status_changed", {
                            {"old_status", static_cast<int>(lastKnownStatus_)},
                            {"new_status", static_cast<int>(status)}
                        });
                        lastKnownStatus_ = status;
                    }
                }
            }
        });
    }

private:
    std::thread periodicValidationThread_;
    std::atomic<bool> isRunning_{true};
    LicenseSystemService::LicenseStatus lastKnownStatus_{LicenseSystemService::LicenseStatus::INVALID_KEY};
};
```

## 10. Deployment and Distribution

### 10.1 License Server Deployment
```yaml
# Docker configuration for license server
version: '3.8'
services:
  license-server:
    image: license-server:latest
    ports:
      - "443:443"
    environment:
      - SSL_CERT_PATH=/etc/ssl/cert.pem
      - SSL_KEY_PATH=/etc/ssl/key.pem
      - DATABASE_URL=postgresql://user:pass@db:5432/licenses
    volumes:
      - ./ssl:/etc/ssl
    restart: unless-stopped

  database:
    image: postgres:13
    environment:
      - POSTGRES_DB=licenses
      - POSTGRES_USER=license_user
      - POSTGRES_PASSWORD=secure_password
    volumes:
      - license_data:/var/lib/postgresql/data
    restart: unless-stopped

volumes:
  license_data:
```

### 10.2 Client Distribution
```cpp
// Deployment configuration
namespace DeploymentConfig {
    constexpr bool ENABLE_LOGGING = false;  // Disable in release
    constexpr bool ENABLE_DEBUG_UI = false;
    constexpr bool REQUIRE_INTERNET = true;

    // Obfuscated server configuration
    const std::string getProductionServerUrl() {
        // Compile-time obfuscated URL
        return decryptUrl(OBFUSCATED_SERVER_DATA);
    }

    // Certificate pinning for security
    const std::string getServerCertificateHash() {
        return OBFUSCATED_STRING("sha256:ABCD1234...");
    }
}
```

## 11. Implementation Timeline

### Phase 1: Core Infrastructure (4-6 weeks)
1. **Hardware Fingerprinting** (1 week)
   - Implement `HardwareFingerprintGenerator` with platform-specific code
   - Cross-platform testing and validation

2. **License Cache System** (1 week)
   - Implement `LicenseCache` with encryption
   - Integrity protection and tamper detection

3. **Network Communication** (1 week)
   - Implement `LicenseServerClient` using JUCE networking
   - HTTP request/response handling and error management

4. **Core Service Implementation** (1-2 weeks)
   - Implement `LicenseSystemService` main orchestration
   - Event bus integration and service lifecycle

5. **Security Features** (1 week)
   - Implement `SecureSessionManager` and cryptographic functions
   - Anti-tampering and code obfuscation

### Phase 2: User Interface (2-3 weeks)
1. **License Activation Dialog** (1 week)
   - JUCE-based activation interface
   - Real-time validation and progress indication

2. **Status Widget** (1 week)
   - License status display and monitoring
   - Grace period warnings and renewal prompts

3. **Feature Integration** (1 week)
   - Feature gating throughout application
   - UI updates for license-restricted features

### Phase 3: Testing and Integration (3-4 weeks)
1. **Unit Testing** (1 week)
   - Comprehensive test suite for all components
   - Mock services for isolated testing

2. **Integration Testing** (1 week)
   - End-to-end license workflow testing
   - Network failure and edge case handling

3. **Security Testing** (1 week)
   - Penetration testing and vulnerability assessment
   - Code obfuscation and anti-tampering validation

4. **Performance Testing** (1 week)
   - Load testing and optimization
   - Memory usage and startup time analysis

### Phase 4: Deployment and Migration (2-3 weeks)
1. **SM4 Data Migration** (1 week)
   - Migration tools and backward compatibility
   - User data preservation and conversion

2. **Production Deployment** (1 week)
   - License server deployment and configuration
   - SSL certificates and security hardening

3. **User Testing and Refinement** (1 week)
   - Beta testing with real users
   - Bug fixes and user experience improvements

**Total Estimated Timeline: 11-16 weeks**

## 12. Risk Assessment and Mitigation

### 12.1 Technical Risks
| Risk | Impact | Probability | Mitigation |
|------|---------|-------------|------------|
| Platform-specific hardware detection fails | High | Medium | Multi-platform fallback methods, extensive testing |
| Network connectivity issues | Medium | High | Offline grace period, cached validation |
| Cryptographic library compatibility | Medium | Low | Use JUCE built-in crypto, platform-specific APIs |
| Performance impact on startup | Medium | Medium | Asynchronous initialization, caching |
| Memory/resource leaks | Low | Low | RAII patterns, smart pointers, automated testing |

### 12.2 Security Risks
| Risk | Impact | Probability | Mitigation |
|------|---------|-------------|------------|
| License key generation/cracking | High | Medium | Strong encryption, hardware binding, server validation |
| Reverse engineering | High | Medium | Code obfuscation, anti-debugging, binary protection |
| Cache tampering | Medium | Medium | Integrity hashes, encryption, tamper detection |
| Network man-in-the-middle | Medium | Low | Certificate pinning, TLS 1.3, hash verification |
| Hardware spoofing | Medium | Low | Multiple hardware components, consistency checking |

### 12.3 Business Risks
| Risk | Impact | Probability | Mitigation |
|------|---------|-------------|------------|
| License server downtime | High | Low | Offline grace period, redundant servers, monitoring |
| Customer migration issues | Medium | Medium | Migration tools, customer support, documentation |
| Performance complaints | Medium | Medium | Optimization, caching, user testing |
| Feature compatibility issues | Low | Medium | Extensive testing, backward compatibility |

## 13. Success Metrics

### 13.1 Technical Metrics
- **Security**: Zero successful license bypasses in first 6 months
- **Performance**: <200ms license validation startup overhead
- **Reliability**: 99.9% license server uptime
- **Compatibility**: 100% feature parity with SM4 system
- **Migration**: 95% successful automatic data migration rate

### 13.2 User Experience Metrics
- **Activation Success Rate**: >98% first-attempt activation success
- **Support Tickets**: <5% license-related support tickets
- **User Satisfaction**: >4.0/5.0 rating for license experience
- **Migration Complaints**: <2% user complaints about migration

### 13.3 Business Metrics
- **License Compliance**: >99% license validation compliance
- **Revenue Protection**: Maintain current revenue levels during transition
- **Customer Retention**: >95% customer retention through migration
- **Time to Value**: <24 hours for complete system deployment

## Conclusion

This comprehensive plan provides a roadmap for successfully porting the SM4 license system to C++ for SAMPLAR. The plan maintains all security features and user experience aspects while leveraging modern C++ and JUCE framework capabilities.

Key advantages of this approach:
- **Security**: Enhanced with modern C++ cryptographic libraries
- **Performance**: Native C++ implementation with optimized caching
- **Integration**: Seamless integration with JUCE and SAMPLAR architecture
- **Maintainability**: Clean, modular design following BIBLE.MD principles
- **Scalability**: Designed for enterprise-scale deployment

The phased implementation approach minimizes risk while ensuring thorough testing and validation at each stage. With proper execution, this plan will deliver a robust, secure, and user-friendly licensing system that protects SAMPLAR's intellectual property while providing an excellent user experience.
