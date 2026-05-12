#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace powsys365 {

// ============================================================================
// License Tier Enum
// ============================================================================

enum class LicenseTier {
    LIFE_TIME,
    ENTERPRISE,
    PRO,
    BASIC,
    TRIAL,
    STUDENT
};

// ============================================================================
// Pricing Plan Structure
// ============================================================================

struct PricingPlan {
    LicenseTier tier;
    std::string name;
    std::string display_name;
    double price_usd;
    std::string billing_period;   // "lifetime", "annual", "30-days", "1-year"
    std::string description;
    std::vector<std::string> features;
    int max_devices;
    bool is_edu_discount;
};

// ============================================================================
// Hardcoded Pricing Table
// ============================================================================

class PricingTable {
public:
    // ------------------------------------------------------------------
    // Get all pricing plans
    // ------------------------------------------------------------------
    static std::vector<PricingPlan> getAllPlans();

    // ------------------------------------------------------------------
    // Get a specific plan by tier
    // ------------------------------------------------------------------
    static PricingPlan getPlan(LicenseTier tier);

    // ------------------------------------------------------------------
    // Get plan by name string
    // ------------------------------------------------------------------
    static PricingPlan getPlanByName(const std::string& tier_name);

    // ------------------------------------------------------------------
    // Get price for a tier
    // ------------------------------------------------------------------
    static double getPrice(LicenseTier tier);

    // ------------------------------------------------------------------
    // Get features for a tier
    // ------------------------------------------------------------------
    static std::vector<std::string> getFeatures(LicenseTier tier);

    // ------------------------------------------------------------------
    // Format price as USD string
    // ------------------------------------------------------------------
    static std::string formatPrice(double price);

    // ------------------------------------------------------------------
    // Convert tier enum to string
    // ------------------------------------------------------------------
    static std::string tierToString(LicenseTier tier);

    // ------------------------------------------------------------------
    // Convert string to tier enum
    // ------------------------------------------------------------------
    static LicenseTier stringToTier(const std::string& str);

    // ------------------------------------------------------------------
    // Check if tier is free
    // ------------------------------------------------------------------
    static bool isFreeTier(LicenseTier tier);

    // ------------------------------------------------------------------
    // Check if tier requires education verification
    // ------------------------------------------------------------------
    static bool requiresEduVerification(LicenseTier tier);

    // ------------------------------------------------------------------
    // Get plan recommendation based on bus count
    // ------------------------------------------------------------------
    static LicenseTier recommendTier(int bus_count);

    // ------------------------------------------------------------------
    // Serialize pricing table to JSON
    // ------------------------------------------------------------------
    static std::string toJson();

    // ------------------------------------------------------------------
    // Attempt to fetch pricing from website (fallback to hardcoded)
    // ------------------------------------------------------------------
    static std::vector<PricingPlan> fetchFromWebsite(
        const std::string& url = "https://www.powsys365.com"
    );

private:
    static PricingPlan buildLifeTimePlan();
    static PricingPlan buildEnterprisePlan();
    static PricingPlan buildProPlan();
    static PricingPlan buildBasicPlan();
    static PricingPlan buildTrialPlan();
    static PricingPlan buildStudentPlan();
};

// ============================================================================
// Pricing Exception
// ============================================================================

class PricingException : public std::runtime_error {
public:
    explicit PricingException(const std::string& msg) : std::runtime_error(msg) {}
    explicit PricingException(const char* msg) : std::runtime_error(msg) {}
};

} // namespace powsys365
