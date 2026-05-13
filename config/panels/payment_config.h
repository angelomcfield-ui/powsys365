#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace powsys365::config {

/**
 * @brief Payment & license configuration panel.
 *
 * Supports Stripe and PayPal payment gateways, subscription
 * management, pricing plan selection, and license key activation.
 */
class PaymentConfigPanel {
public:
    /**
     * @brief Supported payment gateway.
     */
    enum class Gateway {
        Stripe,
        PayPal,
        Both
    };

    static std::string gatewayToString(Gateway g);
    static Gateway     stringToGateway(const std::string& s);

    /**
     * @brief Subscription tier.
     */
    enum class PlanTier {
        Free,
        Basic,
        Professional,
        Enterprise,
        Custom
    };

    static std::string planToString(PlanTier p);
    static PlanTier    stringToPlan(const std::string& s);

    /**
     * @brief Billing cycle.
     */
    enum class BillingCycle {
        Monthly,
        Quarterly,
        Yearly,
        Lifetime
    };

    static std::string cycleToString(BillingCycle c);
    static BillingCycle stringToCycle(const std::string& s);

    /**
     * @brief Pricing for a plan tier.
     */
    struct PricingInfo {
        PlanTier    tier;
        std::string name;           // Display name
        std::string description;
        double      monthlyPrice    = 0.0;
        double      yearlyPrice     = 0.0;
        double      quarterlyPrice  = 0.0;
        double      lifetimePrice   = 0.0;
        std::string currency        = "USD";
        std::vector<std::string> features;
        int         maxUsers        = 1;
        int         maxProjects     = 1;
        bool        apiAccess       = false;
        bool        prioritySupport = false;
    };

    /**
     * @brief License key information.
     */
    struct LicenseInfo {
        std::string key;
        std::string activationDate;
        std::string expiryDate;
        bool        isActive       = false;
        bool        isPerpetual    = false;
        PlanTier    plan           = PlanTier::Free;
        std::string activatedBy;
        std::string machineId;
        int         maxActivations = 1;
        int         currentActivations = 0;
    };

    /**
     * @brief Stripe-specific configuration.
     */
    struct StripeConfig {
        std::string publishableKey;
        std::string secretKey;        // Encrypted at rest
        std::string webhookSecret;
        std::string apiVersion       = "2024-06-01";
        std::string successUrl       = "/payment/success";
        std::string cancelUrl        = "/payment/cancel";
        bool        testMode         = true;
    };

    /**
     * @brief PayPal-specific configuration.
     */
    struct PayPalConfig {
        std::string clientId;
        std::string clientSecret;     // Encrypted at rest
        std::string webhookId;
        std::string apiBaseUrl       = "https://api-m.paypal.com";
        bool        sandbox          = true;
    };

    PaymentConfigPanel();
    ~PaymentConfigPanel() = default;

    PaymentConfigPanel(const PaymentConfigPanel&) = delete;
    PaymentConfigPanel& operator=(const PaymentConfigPanel&) = delete;
    PaymentConfigPanel(PaymentConfigPanel&&) noexcept;
    PaymentConfigPanel& operator=(PaymentConfigPanel&&) noexcept;

    // ----------------------------------------------------------------
    //  Gateway
    // ----------------------------------------------------------------

    void setGateway(Gateway g);
    Gateway gateway() const;

    void setStripeConfig(const StripeConfig& config);
    StripeConfig stripeConfig() const;

    void setPayPalConfig(const PayPalConfig& config);
    PayPalConfig paypalConfig() const;

    // ----------------------------------------------------------------
    //  Plan & Pricing
    // ----------------------------------------------------------------

    void setCurrentPlan(PlanTier plan);
    PlanTier currentPlan() const;

    void setBillingCycle(BillingCycle cycle);
    BillingCycle billingCycle() const;

    void setAvailablePricing(const std::vector<PricingInfo>& pricing);
    std::vector<PricingInfo> availablePricing() const;

    PricingInfo pricingForTier(PlanTier tier) const;

    double currentPrice() const; // Price based on plan + cycle

    // ----------------------------------------------------------------
    //  License
    // ----------------------------------------------------------------

    void setLicense(const LicenseInfo& license);
    LicenseInfo license() const;

    void activateLicense(const std::string& key);
    void deactivateLicense();
    bool isLicenseValid() const;

    bool hasLicenseKey() const;
    void clearLicenseKey();

    // ----------------------------------------------------------------
    //  Serialisation
    // ----------------------------------------------------------------

    std::string toJSON() const;
    void fromJSON(const std::string& json);

    // ----------------------------------------------------------------
    //  Validation
    // ----------------------------------------------------------------

    std::vector<std::string> validate() const;

    void resetToDefaults();
    bool hasChanges() const;
    void markSaved();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace powsys365::config
