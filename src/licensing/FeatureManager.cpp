#include "FeatureManager.h"

#include <algorithm>
#include <initializer_list>

namespace samplar {

namespace {
using FT = FeatureManager::FeatureType;

FeatureManager::LicenseFeatures makeLicenseFeatures(std::initializer_list<FT> features,
                                                    int maxExports,
                                                    int maxLibrary,
                                                    int maxProject,
                                                    bool hasWatermark)
{
    FeatureManager::LicenseFeatures lf;
    lf.enabledFeatures = features;
    lf.maxExportsPerSession = maxExports;
    lf.maxSampleLibrarySize = maxLibrary;
    lf.maxProjectSize = maxProject;
    lf.hasWatermark = hasWatermark;
    return lf;
}

} // namespace

FeatureManager::FeatureManager() = default;

bool FeatureManager::isFeatureEnabled(FeatureType feature, const std::string& licenseType) const
{
    const auto& defs = licenseDefinitions();
    const auto it = defs.find(licenseType);
    if (it == defs.end()) {
        return false;
    }
    return it->second.enabledFeatures.count(feature) > 0;
}

bool FeatureManager::canPerformAction(const std::string& action, const std::string& licenseType) const
{
    const auto& defs = licenseDefinitions();
    const auto it = defs.find(licenseType);
    if (it == defs.end()) {
        return false;
    }

    if (action == "export") {
        return it->second.maxExportsPerSession != 0;
    }
    if (action == "batch_process") {
        return it->second.enabledFeatures.count(FeatureType::BatchProcessing) > 0;
    }
    if (action == "cloud_sync") {
        return it->second.enabledFeatures.count(FeatureType::CloudSync) > 0;
    }

    return true;
}

FeatureManager::LicenseFeatures FeatureManager::getFeaturesForLicense(const std::string& licenseType) const
{
    const auto& defs = licenseDefinitions();
    const auto it = defs.find(licenseType);
    if (it == defs.end()) {
        return LicenseFeatures{};
    }
    return it->second;
}

void FeatureManager::recordFeatureUsage(FeatureType feature)
{
    std::scoped_lock lock(featureMutex_);
    ++usageCounters_[feature];
}

bool FeatureManager::hasReachedUsageLimit(const std::string& action, const std::string& licenseType) const
{
    const auto& defs = licenseDefinitions();
    const auto it = defs.find(licenseType);
    if (it == defs.end()) {
        return true;
    }

    if (action == "export") {
        const auto allowed = it->second.maxExportsPerSession;
        if (allowed <= 0) {
            return false;
        }
        std::scoped_lock lock(featureMutex_);
        const auto counterIt = usageCounters_.find(FeatureType::UnlimitedExports);
        const int usage = (counterIt != usageCounters_.end()) ? counterIt->second : 0;
        return usage >= allowed;
    }

    return false;
}

const std::map<std::string, FeatureManager::LicenseFeatures>& FeatureManager::licenseDefinitions()
{
    static const std::map<std::string, LicenseFeatures> defs = [] {
        std::map<std::string, LicenseFeatures> map;
        map["TRL"] = makeLicenseFeatures({ FT::PremiumSamples }, 3, 1024, 1, true);
        map["BAS"] = makeLicenseFeatures({ FT::PremiumSamples, FT::CustomPresets }, 10, 4096, 4, true);
        map["STD"] = makeLicenseFeatures({ FT::PremiumSamples, FT::CustomPresets, FT::AdvancedEffects }, 25, 8192, 8, false);
        map["PRO"] = makeLicenseFeatures({ FT::PremiumSamples, FT::CustomPresets, FT::AdvancedEffects, FT::BatchProcessing, FT::UnlimitedExports }, 0, 16384, 16, false);
        map["PROMO"] = makeLicenseFeatures({ FT::PremiumSamples, FT::CustomPresets, FT::AdvancedEffects, FT::BatchProcessing, FT::UnlimitedExports }, 0, 16384, 16, false);  // Promotional - same as PRO
        map["EDU"] = makeLicenseFeatures({ FT::PremiumSamples, FT::CustomPresets, FT::AdvancedEffects }, 15, 8192, 8, true);
        map["ENT"] = makeLicenseFeatures({ FT::PremiumSamples, FT::CustomPresets, FT::AdvancedEffects, FT::BatchProcessing, FT::UnlimitedExports, FT::CloudSync, FT::PrioritySupport, FT::CommercialLicense }, 0, 32768, 32, false);
        return map;
    }();

    return defs;
}

} // namespace samplar

