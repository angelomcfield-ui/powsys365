#include "pricing.h"

#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <regex>

namespace powsys365 {

// ============================================================================
// PricingTable Implementation
// ============================================================================

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;       break;
        }
    }
    return out;
}

static std::string jsonStringField(const std::string& name, const std::string& value, bool last = false) {
    return "\"" + name + "\":\"" + jsonEscape(value) + "\"" + (last ? "" : ",");
}

static std::string jsonDoubleField(const std::string& name, double value, bool last = false) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return "\"" + name + "\":" + oss.str() + (last ? "" : ",");
}

static std::string jsonStringArray(const std::vector<std::string>& arr) {
    std::string json = "[";
    for (size_t i = 0; i < arr.size(); ++i) {
        json += "\"" + jsonEscape(arr[i]) + "\"";
        if (i + 1 < arr.size()) json += ",";
    }
    json += "]";
    return json;
}

// ============================================================================
// Plan Builders
// ============================================================================

PricingPlan PricingTable::buildLifeTimePlan() {
    return {
        LicenseTier::LIFE_TIME,
        "LIFE_TIME",
        "Lifetime",
        16999.99,
        "lifetime",
        "Todos los modulos, acceso de por vida. Pago unico.",
        {
            "all_modules", "power_flow", "short_circuit", "arc_flash",
            "transient", "motor_starting", "harmonics", "relay_coordination",
            "cable_sizing", "generator_sizing", "transformer_sizing",
            "grounding", "lightning", "reporting", "api_access",
            "cloud_sync", "multi_user", "priority_support"
        },
        9999,
        false
    };
}

PricingPlan PricingTable::buildEnterprisePlan() {
    return {
        LicenseTier::ENTERPRISE,
        "ENTERPRISE",
        "Enterprise",
        12999.99,
        "annual",
        "Todos los modulos enterprise. Facturacion anual.",
        {
            "power_flow", "short_circuit", "arc_flash", "transient",
            "motor_starting", "harmonics", "relay_coordination",
            "cable_sizing", "generator_sizing", "transformer_sizing",
            "grounding", "reporting", "api_access", "cloud_sync",
            "multi_user", "priority_support"
        },
        100,
        false
    };
}

PricingPlan PricingTable::buildProPlan() {
    return {
        LicenseTier::PRO,
        "PRO",
        "Pro",
        9999.99,
        "annual",
        "Modulos profesionales. Facturacion anual.",
        {
            "power_flow", "short_circuit", "arc_flash",
            "harmonics", "cable_sizing", "reporting", "api_access"
        },
        20,
        false
    };
}

PricingPlan PricingTable::buildBasicPlan() {
    return {
        LicenseTier::BASIC,
        "BASIC",
        "Basic",
        6999.99,
        "annual",
        "Modulos basicos. Facturacion anual.",
        {
            "power_flow", "short_circuit", "reporting"
        },
        5,
        false
    };
}

PricingPlan PricingTable::buildTrialPlan() {
    return {
        LicenseTier::TRIAL,
        "TRIAL",
        "Trial",
        0.00,
        "30-days",
        "30 dias de prueba, funcionalidad limitada.",
        {
            "power_flow", "short_circuit"
        },
        1,
        false
    };
}

PricingPlan PricingTable::buildStudentPlan() {
    return {
        LicenseTier::STUDENT,
        "STUDENT",
        "Student",
        0.00,
        "1-year",
        "1 anio para estudiantes con email .edu. Maximo 50 buses.",
        {
            "power_flow", "short_circuit", "reporting"
        },
        1,
        true
    };
}

// ============================================================================
// Public Methods
// ============================================================================

std::vector<PricingPlan> PricingTable::getAllPlans() {
    return {
        buildLifeTimePlan(),
        buildEnterprisePlan(),
        buildProPlan(),
        buildBasicPlan(),
        buildTrialPlan(),
        buildStudentPlan(),
    };
}

PricingPlan PricingTable::getPlan(LicenseTier tier) {
    switch (tier) {
        case LicenseTier::LIFE_TIME:  return buildLifeTimePlan();
        case LicenseTier::ENTERPRISE: return buildEnterprisePlan();
        case LicenseTier::PRO:        return buildProPlan();
        case LicenseTier::BASIC:      return buildBasicPlan();
        case LicenseTier::TRIAL:      return buildTrialPlan();
        case LicenseTier::STUDENT:    return buildStudentPlan();
        default: throw PricingException("Unknown license tier");
    }
}

PricingPlan PricingTable::getPlanByName(const std::string& tier_name) {
    return getPlan(stringToTier(tier_name));
}

double PricingTable::getPrice(LicenseTier tier) {
    return getPlan(tier).price_usd;
}

std::vector<std::string> PricingTable::getFeatures(LicenseTier tier) {
    return getPlan(tier).features;
}

std::string PricingTable::formatPrice(double price) {
    if (price == 0.0) return "$0.00";
    std::ostringstream oss;
    oss << "$" << std::fixed << std::setprecision(2) << price;
    return oss.str();
}

std::string PricingTable::tierToString(LicenseTier tier) {
    switch (tier) {
        case LicenseTier::LIFE_TIME:  return "LIFE_TIME";
        case LicenseTier::ENTERPRISE: return "ENTERPRISE";
        case LicenseTier::PRO:        return "PRO";
        case LicenseTier::BASIC:      return "BASIC";
        case LicenseTier::TRIAL:      return "TRIAL";
        case LicenseTier::STUDENT:    return "STUDENT";
        default:                      return "UNKNOWN";
    }
}

LicenseTier PricingTable::stringToTier(const std::string& str) {
    std::string upper;
    for (char c : str) upper += static_cast<char>(std::toupper(c));
    if (upper == "LIFE_TIME"  || upper == "LIFETIME")  return LicenseTier::LIFE_TIME;
    if (upper == "ENTERPRISE") return LicenseTier::ENTERPRISE;
    if (upper == "PRO")        return LicenseTier::PRO;
    if (upper == "BASIC")      return LicenseTier::BASIC;
    if (upper == "TRIAL")      return LicenseTier::TRIAL;
    if (upper == "STUDENT")    return LicenseTier::STUDENT;
    throw PricingException("Invalid tier string: " + str);
}

bool PricingTable::isFreeTier(LicenseTier tier) {
    return (tier == LicenseTier::TRIAL || tier == LicenseTier::STUDENT);
}

bool PricingTable::requiresEduVerification(LicenseTier tier) {
    return (tier == LicenseTier::STUDENT);
}

LicenseTier PricingTable::recommendTier(int bus_count) {
    if (bus_count <= 50)   return LicenseTier::STUDENT;
    if (bus_count <= 100)  return LicenseTier::BASIC;
    if (bus_count <= 500)  return LicenseTier::PRO;
    if (bus_count <= 2000) return LicenseTier::ENTERPRISE;
    return LicenseTier::LIFE_TIME;
}

std::string PricingTable::toJson() {
    std::vector<PricingPlan> plans = getAllPlans();
    std::string json = "{\"plans\":[";
    for (size_t i = 0; i < plans.size(); ++i) {
        const auto& p = plans[i];
        json += "{" +
            jsonStringField("tier", tierToString(p.tier)) +
            jsonStringField("name", p.name) +
            jsonStringField("display_name", p.display_name) +
            jsonDoubleField("price_usd", p.price_usd) +
            jsonStringField("billing_period", p.billing_period) +
            jsonStringField("description", p.description) +
            "\"features\":" + jsonStringArray(p.features) + "," +
            jsonStringField("max_devices", std::to_string(p.max_devices)) +
            jsonStringField("is_edu_discount", p.is_edu_discount ? "true" : "false", true) +
            "}";
        if (i + 1 < plans.size()) json += ",";
    }
    json += "]}";
    return json;
}

std::vector<PricingPlan> PricingTable::fetchFromWebsite(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        // Fallback to hardcoded pricing
        return getAllPlans();
    }

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "POWSYS365-PricingClient/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        // Fallback to hardcoded pricing
        return getAllPlans();
    }

    // Attempt to parse pricing data from HTML
    // Look for JSON-LD or embedded pricing data
    std::regex pricing_re("\"?price\"?\\s*[:=]\\s*\"?\\$?([0-9,]+\\.?[0-9]*)\"?");
    std::regex tier_re("\"?(tier|plan)\"?\\s*[:=]\\s*\"?([A-Za-z_]+)\"?");

    std::smatch match;
    std::string::const_iterator search_start(response_body.cbegin());

    // If no embedded pricing found, fallback to hardcoded
    // The website would need to serve structured pricing data
    // for this to work automatically
    return getAllPlans();
}

} // namespace powsys365
