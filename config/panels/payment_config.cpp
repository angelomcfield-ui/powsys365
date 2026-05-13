#include "payment_config.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>

namespace powsys365::config {

/* ================================================================
   String helpers
   ================================================================ */

std::string PaymentConfigPanel::gatewayToString(Gateway g) {
    switch (g) { case Gateway::Stripe: return "stripe"; case Gateway::PayPal: return "paypal";
        case Gateway::Both: return "both"; }
    return "stripe";
}
PaymentConfigPanel::Gateway PaymentConfigPanel::stringToGateway(const std::string& s) {
    std::string l; for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "paypal") return Gateway::PayPal; if (l == "both") return Gateway::Both;
    return Gateway::Stripe;
}

std::string PaymentConfigPanel::planToString(PlanTier p) {
    switch (p) { case PlanTier::Free: return "free"; case PlanTier::Basic: return "basic";
        case PlanTier::Professional: return "professional"; case PlanTier::Enterprise: return "enterprise";
        case PlanTier::Custom: return "custom"; }
    return "free";
}
PaymentConfigPanel::PlanTier PaymentConfigPanel::stringToPlan(const std::string& s) {
    std::string l; for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "basic") return PlanTier::Basic; if (l == "professional") return PlanTier::Professional;
    if (l == "enterprise") return PlanTier::Enterprise; if (l == "custom") return PlanTier::Custom;
    return PlanTier::Free;
}

std::string PaymentConfigPanel::cycleToString(BillingCycle c) {
    switch (c) { case BillingCycle::Monthly: return "monthly"; case BillingCycle::Quarterly: return "quarterly";
        case BillingCycle::Yearly: return "yearly"; case BillingCycle::Lifetime: return "lifetime"; }
    return "monthly";
}
PaymentConfigPanel::BillingCycle PaymentConfigPanel::stringToCycle(const std::string& s) {
    std::string l; for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "quarterly") return BillingCycle::Quarterly; if (l == "yearly") return BillingCycle::Yearly;
    if (l == "lifetime") return BillingCycle::Lifetime;
    return BillingCycle::Monthly;
}

/* ================================================================
   PIMPL
   ================================================================ */

class PaymentConfigPanel::Impl {
public:
    Gateway          gateway_     = Gateway::Stripe;
    StripeConfig     stripe_;
    PayPalConfig     paypal_;
    PlanTier         currentPlan_ = PlanTier::Free;
    BillingCycle     billingCycle_ = BillingCycle::Monthly;
    LicenseInfo      license_;
    std::vector<PricingInfo> pricing_;
    bool             hasChanges_  = false;

    Impl() {
        // Default pricing tiers
        PricingInfo freePlan;
        freePlan.tier = PlanTier::Free;
        freePlan.name = "Free";
        freePlan.description = "Basic access with limited features";
        freePlan.monthlyPrice = 0.0;
        freePlan.yearlyPrice = 0.0;
        freePlan.lifetimePrice = 0.0;
        freePlan.currency = "USD";
        freePlan.features = {"1 project", "Basic analysis", "Community support", "500 MB storage"};
        freePlan.maxUsers = 1; freePlan.maxProjects = 1;
        freePlan.apiAccess = false; freePlan.prioritySupport = false;
        pricing_.push_back(freePlan);

        PricingInfo basicPlan;
        basicPlan.tier = PlanTier::Basic;
        basicPlan.name = "Basic";
        basicPlan.description = "Essential tools for small teams";
        basicPlan.monthlyPrice = 29.99;
        basicPlan.yearlyPrice = 299.90;
        basicPlan.quarterlyPrice = 79.99;
        basicPlan.lifetimePrice = 999.0;
        basicPlan.currency = "USD";
        basicPlan.features = {"5 projects", "Advanced analysis", "Email support", "5 GB storage", "Export reports"};
        basicPlan.maxUsers = 3; basicPlan.maxProjects = 5;
        basicPlan.apiAccess = false; basicPlan.prioritySupport = false;
        pricing_.push_back(basicPlan);

        PricingInfo proPlan;
        proPlan.tier = PlanTier::Professional;
        proPlan.name = "Professional";
        proPlan.description = "Full-featured for professional use";
        proPlan.monthlyPrice = 79.99;
        proPlan.yearlyPrice = 799.90;
        proPlan.quarterlyPrice = 209.99;
        proPlan.lifetimePrice = 2499.0;
        proPlan.currency = "USD";
        proPlan.features = {"Unlimited projects", "Full analysis suite", "Priority support", "50 GB storage",
                           "API access", "Custom integrations", "Team collaboration"};
        proPlan.maxUsers = 10; proPlan.maxProjects = 999;
        proPlan.apiAccess = true; proPlan.prioritySupport = true;
        pricing_.push_back(proPlan);

        PricingInfo entPlan;
        entPlan.tier = PlanTier::Enterprise;
        entPlan.name = "Enterprise";
        entPlan.description = "Tailored solutions for large organizations";
        entPlan.monthlyPrice = 249.99;
        entPlan.yearlyPrice = 2499.90;
        entPlan.quarterlyPrice = 649.99;
        entPlan.lifetimePrice = 0.0; // Contact sales
        entPlan.currency = "USD";
        entPlan.features = {"Unlimited everything", "Dedicated account manager", "24/7 phone support",
                           "500 GB storage", "Full API access", "SSO/SAML", "On-premise option", "Custom SLA"};
        entPlan.maxUsers = 999; entPlan.maxProjects = 999;
        entPlan.apiAccess = true; entPlan.prioritySupport = true;
        pricing_.push_back(entPlan);
    }

    std::string currentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        #ifdef _WIN32
        localtime_s(&tm, &time);
        #else
        localtime_r(&time, &tm);
        #endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    bool validateLicenseKeyFormat(const std::string& key) {
        if (key.empty() || key.length() < 16) return false;
        // Format: XXXX-XXXX-XXXX-XXXX or similar
        int dashCount = 0;
        int alphaNumCount = 0;
        for (char c : key) {
            if (c == '-') { ++dashCount; continue; }
            if (std::isalnum(static_cast<unsigned char>(c))) { ++alphaNumCount; continue; }
            return false;
        }
        return alphaNumCount >= 12;
    }
};

/* ================================================================
   Construction
   ================================================================ */

PaymentConfigPanel::PaymentConfigPanel() : pImpl(std::make_unique<Impl>()) {}
PaymentConfigPanel::~PaymentConfigPanel() = default;
PaymentConfigPanel::PaymentConfigPanel(PaymentConfigPanel&&) noexcept = default;
PaymentConfigPanel& PaymentConfigPanel::operator=(PaymentConfigPanel&&) noexcept = default;

/* ================================================================
   Gateway
   ================================================================ */

void PaymentConfigPanel::setGateway(Gateway g)       { pImpl->gateway_ = g; pImpl->hasChanges_ = true; }
PaymentConfigPanel::Gateway PaymentConfigPanel::gateway() const { return pImpl->gateway_; }

void PaymentConfigPanel::setStripeConfig(const StripeConfig& c) { pImpl->stripe_ = c; pImpl->hasChanges_ = true; }
PaymentConfigPanel::StripeConfig PaymentConfigPanel::stripeConfig() const { return pImpl->stripe_; }

void PaymentConfigPanel::setPayPalConfig(const PayPalConfig& c) { pImpl->paypal_ = c; pImpl->hasChanges_ = true; }
PaymentConfigPanel::PayPalConfig PaymentConfigPanel::paypalConfig() const { return pImpl->paypal_; }

/* ================================================================
   Plan & Pricing
   ================================================================ */

void PaymentConfigPanel::setCurrentPlan(PlanTier p)  { pImpl->currentPlan_ = p; pImpl->hasChanges_ = true; }
PaymentConfigPanel::PlanTier PaymentConfigPanel::currentPlan() const { return pImpl->currentPlan_; }

void PaymentConfigPanel::setBillingCycle(BillingCycle c) { pImpl->billingCycle_ = c; pImpl->hasChanges_ = true; }
PaymentConfigPanel::BillingCycle PaymentConfigPanel::billingCycle() const { return pImpl->billingCycle_; }

void PaymentConfigPanel::setAvailablePricing(const std::vector<PricingInfo>& p) { pImpl->pricing_ = p; pImpl->hasChanges_ = true; }
std::vector<PaymentConfigPanel::PricingInfo> PaymentConfigPanel::availablePricing() const { return pImpl->pricing_; }

PaymentConfigPanel::PricingInfo
PaymentConfigPanel::pricingForTier(PlanTier tier) const {
    for (const auto& p : pImpl->pricing_) {
        if (p.tier == tier) return p;
    }
    return PricingInfo{};
}

double PaymentConfigPanel::currentPrice() const {
    auto p = pricingForTier(pImpl->currentPlan_);
    switch (pImpl->billingCycle_) {
        case BillingCycle::Monthly:    return p.monthlyPrice;
        case BillingCycle::Quarterly:  return p.quarterlyPrice;
        case BillingCycle::Yearly:     return p.yearlyPrice;
        case BillingCycle::Lifetime:   return p.lifetimePrice;
    }
    return 0.0;
}

/* ================================================================
   License
   ================================================================ */

void PaymentConfigPanel::setLicense(const LicenseInfo& l) { pImpl->license_ = l; pImpl->hasChanges_ = true; }
PaymentConfigPanel::LicenseInfo PaymentConfigPanel::license() const { return pImpl->license_; }

void PaymentConfigPanel::activateLicense(const std::string& key) {
    if (!pImpl->validateLicenseKeyFormat(key)) return;
    pImpl->license_.key = key;
    pImpl->license_.isActive = true;
    pImpl->license_.activationDate = pImpl->currentTimestamp();
    // Set expiry: 1 year from now for non-perpetual
    if (!pImpl->license_.isPerpetual) {
        auto now = std::chrono::system_clock::now();
        auto oneYear = now + std::chrono::hours(24 * 365);
        auto time = std::chrono::system_clock::to_time_t(oneYear);
        std::tm tm{};
        #ifdef _WIN32
        localtime_s(&tm, &time);
        #else
        localtime_r(&time, &tm);
        #endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        pImpl->license_.expiryDate = oss.str();
    }
    pImpl->license_.currentActivations = 1;
    pImpl->hasChanges_ = true;
}

void PaymentConfigPanel::deactivateLicense() {
    pImpl->license_.isActive = false;
    pImpl->license_.currentActivations = 0;
    pImpl->hasChanges_ = true;
}

bool PaymentConfigPanel::isLicenseValid() const {
    if (!pImpl->license_.isActive) return false;
    if (pImpl->license_.isPerpetual) return true;
    // Check expiry
    if (pImpl->license_.expiryDate.empty()) return true;
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm nowTm{};
    #ifdef _WIN32
    gmtime_s(&nowTm, &time);
    #else
    gmtime_r(&time, &nowTm);
    #endif
    std::tm expTm{};
    std::istringstream iss(pImpl->license_.expiryDate);
    iss >> std::get_time(&expTm, "%Y-%m-%dT%H:%M:%SZ");
    if (iss.fail()) return true; // can't parse, assume valid
    auto nowT = std::mktime(&nowTm);
    auto expT = std::mktime(&expTm);
    return nowT < expT;
}

bool PaymentConfigPanel::hasLicenseKey() const {
    return !pImpl->license_.key.empty();
}

void PaymentConfigPanel::clearLicenseKey() {
    pImpl->license_.key.clear();
    pImpl->hasChanges_ = true;
}

/* ================================================================
   JSON Serialisation
   ================================================================ */

namespace {
    std::string pjesc(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) { case '"': r += "\\\""; break; case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break; case '\r': r += "\\r"; break; case '\t': r += "\\t"; break;
            default: r += c; break; }
        }
        return r;
    }
}

std::string PaymentConfigPanel::toJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\\"gateway\\":\\"" << gatewayToString(pImpl->gateway_) << "\\",";
    // Stripe
    oss << "\\"stripe\\":{";
    oss << "\\"publishableKey\\":\\"" << pjesc(pImpl->stripe_.publishableKey) << "\\",";
    oss << "\\"apiVersion\\":\\"" << pjesc(pImpl->stripe_.apiVersion) << "\\",";
    oss << "\\"successUrl\\":\\"" << pjesc(pImpl->stripe_.successUrl) << "\\",";
    oss << "\\"cancelUrl\\":\\"" << pjesc(pImpl->stripe_.cancelUrl) << "\\",";
    oss << "\\"testMode\\":" << (pImpl->stripe_.testMode ? "true" : "false");
    oss << "},";
    // PayPal
    oss << "\\"paypal\\":{";
    oss << "\\"clientId\\":\\"" << pjesc(pImpl->paypal_.clientId) << "\\",";
    oss << "\\"apiBaseUrl\\":\\"" << pjesc(pImpl->paypal_.apiBaseUrl) << "\\",";
    oss << "\\"sandbox\\":" << (pImpl->paypal_.sandbox ? "true" : "false");
    oss << "},";
    oss << "\\"currentPlan\\":\\"" << planToString(pImpl->currentPlan_) << "\\",";
    oss << "\\"billingCycle\\":\\"" << cycleToString(pImpl->billingCycle_) << "\\",";
    // Pricing
    oss << "\\"pricing\\":[";
    for (size_t i = 0; i < pImpl->pricing_.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& p = pImpl->pricing_[i];
        oss << "{";
        oss << "\\"tier\\":\\"" << planToString(p.tier) << "\\",";
        oss << "\\"name\\":\\"" << pjesc(p.name) << "\\",";
        oss << "\\"description\\":\\"" << pjesc(p.description) << "\\",";
        oss << "\\"monthlyPrice\\":" << std::fixed << std::setprecision(2) << p.monthlyPrice << ",";
        oss << "\\"yearlyPrice\\":" << p.yearlyPrice << ",";
        oss << "\\"quarterlyPrice\\":" << p.quarterlyPrice << ",";
        oss << "\\"lifetimePrice\\":" << p.lifetimePrice << ",";
        oss << "\\"currency\\":\\"" << pjesc(p.currency) << "\\",";
        oss << "\\"maxUsers\\":" << p.maxUsers << ",";
        oss << "\\"maxProjects\\":" << p.maxProjects << ",";
        oss << "\\"apiAccess\\":" << (p.apiAccess ? "true" : "false") << ",";
        oss << "\\"prioritySupport\\":" << (p.prioritySupport ? "true" : "false") << ",";
        oss << "\\"features\\":[";
        for (size_t j = 0; j < p.features.size(); ++j) {
            if (j > 0) oss << ",";
            oss << "\\"" << pjesc(p.features[j]) << "\\"";
        }
        oss << "]}";
    }
    oss << "],";
    // License
    oss << "\\"license\\":{";
    oss << "\\"key\\":\\"" << pjesc(pImpl->license_.key) << "\\",";
    oss << "\\"isActive\\":" << (pImpl->license_.isActive ? "true" : "false") << ",";
    oss << "\\"isPerpetual\\":" << (pImpl->license_.isPerpetual ? "true" : "false") << ",";
    oss << "\\"plan\\":\\"" << planToString(pImpl->license_.plan) << "\\",";
    oss << "\\"activationDate\\":\\"" << pjesc(pImpl->license_.activationDate) << "\\",";
    oss << "\\"expiryDate\\":\\"" << pjesc(pImpl->license_.expiryDate) << "\\",";
    oss << "\\"maxActivations\\":" << pImpl->license_.maxActivations << ",";
    oss << "\\"currentActivations\\":" << pImpl->license_.currentActivations;
    oss << "}}";
    return oss.str();
}

void PaymentConfigPanel::fromJSON(const std::string& json) {
    auto extractStr = [&](const std::string& k) -> std::string {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return "";
        p = json.find('"', p + k.length() + 3);
        if (p == std::string::npos) return "";
        ++p; size_t e = json.find('"', p);
        return (e == std::string::npos) ? "" : json.substr(p, e - p);
    };
    auto extractBool = [&](const std::string& k) -> bool {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return false;
        p = json.find(':', p + k.length() + 3); if (p == std::string::npos) return false;
        ++p; while (p < json.size() && json[p] == ' ') ++p;
        return json.substr(p, 4) == "true";
    };
    auto extractDouble = [&](const std::string& k) -> double {
        size_t p = json.find("\\"" + k + "\\":");
        if (p == std::string::npos) return 0.0;
        p = json.find(':', p + k.length() + 3); if (p == std::string::npos) return 0.0;
        ++p; while (p < json.size() && json[p] == ' ') ++p;
        return std::strtod(json.c_str() + p, nullptr);
    };
    auto extractInt = [&](const std::string& k) -> int {
        return static_cast<int>(extractDouble(k));
    };

    std::string gw = extractStr("gateway");    if (!gw.empty()) pImpl->gateway_ = stringToGateway(gw);
    pImpl->stripe_.publishableKey = extractStr("publishableKey");
    pImpl->stripe_.apiVersion     = extractStr("apiVersion");
    if (pImpl->stripe_.apiVersion.empty()) pImpl->stripe_.apiVersion = "2024-06-01";
    pImpl->stripe_.successUrl     = extractStr("successUrl");
    pImpl->stripe_.cancelUrl      = extractStr("cancelUrl");
    pImpl->stripe_.testMode       = extractBool("testMode");

    pImpl->paypal_.clientId       = extractStr("clientId");
    pImpl->paypal_.apiBaseUrl     = extractStr("apiBaseUrl");
    if (pImpl->paypal_.apiBaseUrl.empty()) pImpl->paypal_.apiBaseUrl = "https://api-m.paypal.com";
    pImpl->paypal_.sandbox        = extractBool("sandbox");

    std::string cp = extractStr("currentPlan"); if (!cp.empty()) pImpl->currentPlan_ = stringToPlan(cp);
    std::string bc = extractStr("billingCycle"); if (!bc.empty()) pImpl->billingCycle_ = stringToCycle(bc);

    // License
    pImpl->license_.key           = extractStr("key");
    pImpl->license_.isActive      = extractBool("isActive");
    pImpl->license_.isPerpetual   = extractBool("isPerpetual");
    std::string lp = extractStr("plan"); if (!lp.empty()) pImpl->license_.plan = stringToPlan(lp);
    pImpl->license_.activationDate= extractStr("activationDate");
    pImpl->license_.expiryDate    = extractStr("expiryDate");
    int ma = extractInt("maxActivations"); if (ma > 0) pImpl->license_.maxActivations = ma;
    int ca = extractInt("currentActivations"); if (ca >= 0) pImpl->license_.currentActivations = ca;

    pImpl->hasChanges_ = false;
}

/* ================================================================
   Validation
   ================================================================ */

std::vector<std::string> PaymentConfigPanel::validate() const {
    std::vector<std::string> errors;
    if (!pImpl->stripe_.publishableKey.empty() && pImpl->stripe_.publishableKey.find("pk_") != 0) {
        errors.push_back("Stripe publishable key must start with 'pk_'");
    }
    if (!pImpl->paypal_.clientId.empty() && pImpl->paypal_.clientId.length() < 10) {
        errors.push_back("PayPal client ID appears too short");
    }
    if (!pImpl->license_.key.empty() && !pImpl->validateLicenseKeyFormat(pImpl->license_.key)) {
        errors.push_back("License key format is invalid");
    }
    for (const auto& p : pImpl->pricing_) {
        if (p.monthlyPrice < 0 || p.yearlyPrice < 0 || p.lifetimePrice < 0) {
            errors.push_back("Pricing for " + p.name + " contains negative values");
        }
    }
    return errors;
}

void PaymentConfigPanel::resetToDefaults() { pImpl = std::make_unique<Impl>(); }
bool PaymentConfigPanel::hasChanges() const { return pImpl->hasChanges_; }
void PaymentConfigPanel::markSaved()        { pImpl->hasChanges_ = false; }

} // namespace powsys365::config
