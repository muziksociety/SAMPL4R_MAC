#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>

namespace samplar {

class FeatureManager {
public:
    enum class FeatureType {
        PremiumSamples,
        UnlimitedExports,
        CloudSync,
        AdvancedEffects,
        BatchProcessing,
        CustomPresets,
        PrioritySupport,
        CommercialLicense
    };

    struct LicenseFeatures {
        std::set<FeatureType> enabledFeatures;
        int maxExportsPerSession{0};
        int maxSampleLibrarySize{0};
        int maxProjectSize{0};
        bool hasWatermark{true};
    };

    FeatureManager();

    bool isFeatureEnabled(FeatureType feature, const std::string& licenseType) const;
    bool canPerformAction(const std::string& action, const std::string& licenseType) const;
    LicenseFeatures getFeaturesForLicense(const std::string& licenseType) const;

    void recordFeatureUsage(FeatureType feature);
    bool hasReachedUsageLimit(const std::string& action, const std::string& licenseType) const;

    static const std::map<std::string, LicenseFeatures>& licenseDefinitions();

private:
    std::map<FeatureType, int> usageCounters_;
    mutable std::mutex featureMutex_;
};

} // namespace samplar

