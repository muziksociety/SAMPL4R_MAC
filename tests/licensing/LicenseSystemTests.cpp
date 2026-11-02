// Licence cache / hardware fingerprint / feature matrix. Moved out of the shipping
// plugin sources into SamplarTests (audit D21). Every file goes to a sandboxed
// SAMPLAR_CACHE_DIR_OVERRIDE folder, never to the real %APPDATA%\Samplar.
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cctype>

#include "security/HardwareFingerprintGenerator.h"
#include "security/LicenseCache.h"
#include "licensing/FeatureManager.h"
#include "TestHelpers.h"

namespace samplar {

namespace {

juce::var buildLicenseVar (const juce::String& type)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("license_type", type);
    object->setProperty ("customer_email", "test@example.com");
    object->setProperty ("max_activations", 3);
    object->setProperty ("current_activations", 1);
    object->setProperty ("expires", juce::Time::getCurrentTime().toISO8601 (true));
    object->setProperty ("license_payload", "v2|SAMPL4R|test@example.com|PRO|HW|2026-10-21T03:43:28Z|perpetual|active|n");
    return juce::var (object);
}

} // namespace

class LicenseSystemTests : public juce::UnitTest
{
public:
    LicenseSystemTests() : juce::UnitTest ("License System", "SAMPLAR Licensing") {}

    void runTest() override
    {
        beginTest ("Hardware fingerprint consistency");
        testHardwareFingerprint();

        beginTest ("License cache: encrypted round trip");
        testLicenseCacheRoundTrip();

        beginTest ("License cache: other machine / tampered file / clear");
        testLicenseCacheRejects();

        beginTest ("Offline grace period tracking");
        testGracePeriod();

        beginTest ("Failed attempt lockout window");
        testLockoutBehaviour();

        beginTest ("Feature manager licensing matrix");
        testFeatureManager();
    }

private:
    void testHardwareFingerprint()
    {
        test::ScopedLicenseSandbox sandbox;
        HardwareFingerprintGenerator generator;
        // The licence machine id (v2) is the only hardware id SAMPLAR uses.
        const auto id = HardwareFingerprintGenerator::licenceMachineId();
        expectEquals (static_cast<int> (id.length()), 32, "licence machine id is 32 hex chars");
        expect (std::all_of (id.begin(), id.end(), [] (auto ch) { return std::isxdigit (static_cast<unsigned char> (ch)) != 0; }),
                "licence machine id is hexadecimal");
        expect (id == HardwareFingerprintGenerator::licenceMachineId(), "cached per process: stable");

        const auto v2 = generator.generateHardwareIdV2();
        if (! v2.empty())
        {
            expectEquals (static_cast<int> (v2.length()), 32, "v2 is 32 hex chars");
            expect (v2 == generator.generateHardwareIdV2(), "v2 is stable");
            expect (v2 == id, "the licence id is v2 when the OS has a machine id");
        }
    }

    void testLicenseCacheRoundTrip()
    {
        test::ScopedLicenseSandbox sandbox;
        expectEquals (LicenseCache::cacheDirectory().getFullPathName(), sandbox.dir().getFullPathName(), "override honoured");

        LicenseCache cache;
        const std::string hardwareId = "TEST-HW-ID";
        expect (cache.storeLicense ("SAMPL4R-TEST-KEY", buildLicenseVar ("PRO"), hardwareId), "License should store successfully");

        const auto onDisk = sandbox.dir().getChildFile ("license_cache.json").loadFileAsString();
        expect (onDisk.isNotEmpty());
        expect (! onDisk.contains ("test@example.com"), "licence data is not stored in clear text");

        auto retrieved = LicenseCache().retrieveLicense (hardwareId);   // fresh instance = next launch
        expect (retrieved.has_value(), "Stored license should be retrievable");
        if (! retrieved.has_value())
            return;
        expect (retrieved->licenseData.isObject(), "Retrieved license should deserialize to object");
        expectEquals (juce::String (retrieved->licenseKey), juce::String ("SAMPL4R-TEST-KEY"));

        if (auto* obj = retrieved->licenseData.getDynamicObject())
        {
            expectEquals (obj->getProperty ("license_type").toString(), juce::String ("PRO"), "License type should round-trip");
            expectEquals (obj->getProperty ("customer_email").toString(), juce::String ("test@example.com"), "License email should round-trip");
            expect (obj->getProperty ("license_payload").toString().startsWith ("v2|SAMPL4R|"), "signed payload round-trips");
        }

        cache.clearCache();
    }

    void testLicenseCacheRejects()
    {
        test::ScopedLicenseSandbox sandbox;
        LicenseCache cache;
        const std::string hardwareId = "MACHINE-A";
        cache.storeLicense ("SAMPL4R-KEY", buildLicenseVar ("PRO"), hardwareId);

        expect (! cache.retrieveLicense ("MACHINE-B").has_value(), "a cache from another machine is not used");
        expect (! cache.isCacheValid ("MACHINE-B"));

        const auto file = sandbox.dir().getChildFile ("license_cache.json");
        auto json = juce::JSON::parse (file.loadFileAsString());
        expect (json.isObject(), "cache file is JSON");
        if (auto* obj = json.getDynamicObject())
        {
            obj->setProperty ("licenseKey", "SAMPL4R-FORGED");
            file.replaceWithText (juce::JSON::toString (json));
            expect (! LicenseCache().retrieveLicense (hardwareId).has_value(), "edited cache fails its integrity check");
        }

        file.replaceWithText ("{ not json");
        expect (! LicenseCache().retrieveLicense (hardwareId).has_value(), "corrupt cache is ignored");

        cache.storeLicense ("SAMPL4R-KEY", buildLicenseVar ("PRO"), hardwareId);
        expect (cache.retrieveLicense (hardwareId).has_value());
        cache.clearCache();
        expect (! cache.retrieveLicense (hardwareId).has_value(), "cleared");
    }

    void testGracePeriod()
    {
        test::ScopedLicenseSandbox sandbox;
        LicenseCache cache;
        const std::string hardwareId = "GRACE-HW";
        cache.storeLicense ("GRACE-KEY", buildLicenseVar ("STD"), hardwareId);

        auto grace = cache.getOfflineGraceStatus (hardwareId);
        expect (grace.hasGrace, "Freshly validated license should have grace period");
        expect (grace.remainingTime.count() > 0, "Grace period should report remaining minutes");
        expect (! cache.needsPeriodicValidation (hardwareId, 24), "just validated");
        expect (! cache.getOfflineGraceStatus ("OTHER-HW").hasGrace, "no grace for another machine");

        cache.clearCache();
    }

    void testLockoutBehaviour()
    {
        test::ScopedLicenseSandbox sandbox;
        LicenseCache cache;
        const std::string hardwareId = "LOCKOUT-HW";
        const std::string user = "lockout@example.com";

        expect (! cache.isUserLockedOut (user, hardwareId));
        for (int i = 0; i < 5; ++i)
            cache.recordFailedAttempt (user, hardwareId);

        expect (cache.isUserLockedOut (user, hardwareId), "User should lock after exceeding attempts");
        expect (! cache.isUserLockedOut ("other@example.com", hardwareId), "lockout is per user");

        cache.clearCache();
    }

    void testFeatureManager()
    {
        FeatureManager featureManager;
        using Feature = FeatureManager::FeatureType;

        expect (featureManager.isFeatureEnabled (Feature::PremiumSamples, "PRO"), "PRO license should include premium samples");
        expect (featureManager.isFeatureEnabled (Feature::CloudSync, "ENT"), "ENT license should include cloud sync");
        expect (! featureManager.isFeatureEnabled (Feature::CloudSync, "STD"), "STD license should not include cloud sync");

        auto features = featureManager.getFeaturesForLicense ("TRL");
        expect (features.hasWatermark, "Trial license should enable watermark");
        expectEquals (features.maxExportsPerSession, 3, "Trial exports should be limited");
    }
};

static LicenseSystemTests licenseSystemTests;

} // namespace samplar
