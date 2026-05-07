#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "../../src/security/HardwareFingerprintGenerator.h"
#include "../../src/security/LicenseCache.h"
#include "../../src/licensing/FeatureManager.h"

namespace samplar {

namespace {
juce::File createTempCacheDirectory()
{
    auto tempRoot = juce::File::createTempFile("SAMPLAR_license_tests");
    if (tempRoot.exists())
        tempRoot.deleteRecursively();
    tempRoot.createDirectory();
    return tempRoot;
}

void setCacheOverride(const juce::File& dir)
{
#if JUCE_WINDOWS
    _wputenv_s(L"SAMPLAR_CACHE_DIR_OVERRIDE", dir.getFullPathName().toWideCharPointer());
#else
    setenv("SAMPLAR_CACHE_DIR_OVERRIDE", dir.getFullPathName().toRawUTF8(), 1);
#endif
}

void clearCacheOverride()
{
#if JUCE_WINDOWS
    _wputenv_s(L"SAMPLAR_CACHE_DIR_OVERRIDE", L"");
#else
    unsetenv("SAMPLAR_CACHE_DIR_OVERRIDE");
#endif
}

juce::var buildLicenseVar(const juce::String& type)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("license_type", type);
    object->setProperty("customer_email", "test@example.com");
    object->setProperty("max_activations", 3);
    object->setProperty("current_activations", 1);
    object->setProperty("expires", juce::Time::getCurrentTime().toISO8601(true));
    return juce::var(object);
}

} // namespace

class LicenseSystemTests : public juce::UnitTest
{
public:
    LicenseSystemTests() : juce::UnitTest("License System Tests") {}

    void runTest() override
    {
        beginTest("Hardware fingerprint consistency");
        testHardwareFingerprint();

        beginTest("License cache encryption and retrieval");
        testLicenseCacheRoundTrip();

        beginTest("Offline grace period tracking");
        testGracePeriod();

        beginTest("Failed attempt lockout window");
        testLockoutBehaviour();

        beginTest("Feature manager licensing matrix");
        testFeatureManager();
    }

private:
    void testHardwareFingerprint()
    {
        HardwareFingerprintGenerator generator;
        expect(generator.verifyHardwareIdConsistency(), "Fingerprint should remain stable across iterations");

        const auto fingerprint = generator.generateHardwareId();
        expectEquals(static_cast<int>(fingerprint.length()), 64, "Fingerprint should be 64 hex chars");
        expect(std::all_of(fingerprint.begin(), fingerprint.end(), [](auto ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; }),
               "Fingerprint should be hexadecimal");
    }

    void testLicenseCacheRoundTrip()
    {
        const auto cacheDir = createTempCacheDirectory();
        setCacheOverride(cacheDir);

        LicenseCache cache;
        const juce::String hardwareId = "TEST-HW-ID";
        const auto licenseVar = buildLicenseVar("PRO");

        expect(cache.storeLicense("TEST-KEY", licenseVar, hardwareId.toStdString()), "License should store successfully");

        auto retrieved = cache.retrieveLicense(hardwareId.toStdString());
        expect(retrieved.has_value(), "Stored license should be retrievable");
        expect(retrieved->licenseData.isObject(), "Retrieved license should deserialize to object");

        if (auto* obj = retrieved->licenseData.getDynamicObject())
        {
            expectEquals(obj->getProperty("license_type").toString(), juce::String("PRO"), "License type should round-trip");
            expectEquals(obj->getProperty("customer_email").toString(), juce::String("test@example.com"), "License email should round-trip");
        }

        cache.clearCache();
        cacheDir.deleteRecursively();
        clearCacheOverride();
    }

    void testGracePeriod()
    {
        const auto cacheDir = createTempCacheDirectory();
        setCacheOverride(cacheDir);

        LicenseCache cache;
        const juce::String hardwareId = "GRACE-HW";
        cache.storeLicense("GRACE-KEY", buildLicenseVar("STD"), hardwareId.toStdString());

        auto grace = cache.getOfflineGraceStatus(hardwareId.toStdString());
        expect(grace.hasGrace, "Freshly validated license should have grace period");
        expect(grace.remainingTime.count() > 0, "Grace period should report remaining minutes");

        cache.clearCache();
        cacheDir.deleteRecursively();
        clearCacheOverride();
    }

    void testLockoutBehaviour()
    {
        const auto cacheDir = createTempCacheDirectory();
        setCacheOverride(cacheDir);

        LicenseCache cache;
        const juce::String hardwareId = "LOCKOUT-HW";
        const juce::String user = "lockout@example.com";

        for (int i = 0; i < 5; ++i)
            cache.recordFailedAttempt(user.toStdString(), hardwareId.toStdString());

        expect(cache.isUserLockedOut(user.toStdString(), hardwareId.toStdString()), "User should lock after exceeding attempts");

        cache.clearCache();
        cacheDir.deleteRecursively();
        clearCacheOverride();
    }

    void testFeatureManager()
    {
        FeatureManager featureManager;
        using Feature = FeatureManager::FeatureType;

        expect(featureManager.isFeatureEnabled(Feature::PremiumSamples, "PRO"), "PRO license should include premium samples");
        expect(featureManager.isFeatureEnabled(Feature::CloudSync, "ENT"), "ENT license should include cloud sync");
        expect(!featureManager.isFeatureEnabled(Feature::CloudSync, "STD"), "STD license should not include cloud sync");

        auto features = featureManager.getFeaturesForLicense("TRL");
        expect(features.hasWatermark, "Trial license should enable watermark");
        expectEquals(features.maxExportsPerSession, 3, "Trial exports should be limited");
    }
};

static LicenseSystemTests licenseSystemTests;

} // namespace samplar
