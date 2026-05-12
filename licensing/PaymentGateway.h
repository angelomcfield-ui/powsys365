#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <functional>
#include <memory>
#include <mutex>

namespace powsys365 {
namespace licensing {

// ============================================================
// PaymentProvider
// ============================================================
enum class PaymentProvider {
    PAYPAL = 0,
    STRIPE = 1
};

std::string providerToString(PaymentProvider p);
PaymentProvider providerFromString(const std::string& s);

// ============================================================
// SubscriptionTier (pricing)
// ============================================================
enum class SubscriptionTier {
    BASIC      = 0,
    PRO        = 1,
    ENTERPRISE = 2,
    LIFETIME   = 3
};

// Prices in USD
constexpr double PRICE_BASIC_USD      = 299.0;
constexpr double PRICE_PRO_USD        = 799.0;
constexpr double PRICE_ENTERPRISE_USD = 2499.0;
constexpr double PRICE_LIFETIME_USD   = 4999.0;

double getPriceForTier(SubscriptionTier tier);
std::string tierToLicensePrefix(SubscriptionTier tier);

// ============================================================
// SubscriptionPeriod
// ============================================================
enum class SubscriptionPeriod {
    MONTHLY  = 0,
    YEARLY   = 1,
    LIFETIME = 2
};

// ============================================================
// PaymentStatus
// ============================================================
enum class PaymentStatus {
    PENDING    = 0,
    COMPLETED  = 1,
    FAILED     = 2,
    REFUNDED   = 3,
    CANCELLED  = 4,
    DISPUTED   = 5
};

std::string paymentStatusToString(PaymentStatus s);

// ============================================================
// SubscriptionInfo
// ============================================================
struct SubscriptionInfo {
    std::string subscriptionId;      // Provider-specific ID
    std::string licenseKey;           // Generated license key
    PaymentProvider provider;
    SubscriptionTier tier;
    SubscriptionPeriod period;
    PaymentStatus status;
    double amount = 0.0;
    std::string currency = "USD";
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point currentPeriodStart;
    std::chrono::system_clock::time_point currentPeriodEnd;
    bool autoRenew = true;
    std::string customerEmail;
    std::string customerId;           // Provider customer ID
    std::string transactionId;
    std::string receiptUrl;
};

// ============================================================
// WebhookEvent
// ============================================================
struct WebhookEvent {
    std::string id;
    std::string type;               // e.g., "payment.completed", "subscription.cancelled"
    PaymentProvider provider;
    std::string rawPayload;         // Raw JSON body
    std::map<std::string, std::string> headers;
    std::chrono::system_clock::time_point receivedAt;
    bool signatureValid = false;
};

// ============================================================
// PaymentResult
// ============================================================
struct PaymentResult {
    bool success = false;
    std::string subscriptionId;
    std::string licenseKey;
    std::string checkoutUrl;        // For redirect-based payments
    std::string errorCode;
    std::string errorMessage;
    std::string transactionId;
    std::string receiptUrl;
};

// ============================================================
// QRCodeData
// ============================================================
struct QRCodeData {
    std::string licenseKey;
    std::string tier;
    std::string expiresAt;
    std::string deviceFingerprint;
    std::string qrSvg;              // SVG string
};

// ============================================================
// PaymentGateway - Unified payment handling
// ============================================================
class PaymentGateway {
public:
    PaymentGateway();
    ~PaymentGateway();

    // No copy
    PaymentGateway(const PaymentGateway&) = delete;
    PaymentGateway& operator=(const PaymentGateway&) = delete;

    // --- Configuration ---
    void configurePayPal(const std::string& clientId,
                          const std::string& clientSecret,
                          bool sandbox = false);
    void configureStripe(const std::string& publishableKey,
                          const std::string& secretKey);

    // --- Subscriptions ---
    PaymentResult createSubscription(SubscriptionTier tier,
                                       SubscriptionPeriod period,
                                       const std::string& customerEmail,
                                       PaymentProvider provider);

    bool cancelSubscription(const std::string& subscriptionId,
                            PaymentProvider provider);

    PaymentResult getSubscriptionStatus(const std::string& subscriptionId,
                                         PaymentProvider provider);

    // --- One-Time Payments ---
    PaymentResult createOneTimePayment(SubscriptionTier tier,
                                        const std::string& customerEmail,
                                        PaymentProvider provider);

    // --- Webhooks ---
    bool processWebhook(const WebhookEvent& event);
    bool verifyWebhookSignature(const WebhookEvent& event);

    // --- QR Code ---
    QRCodeData generateLicenseQRCode(const std::string& licenseKey);

    // --- Queries ---
    std::vector<SubscriptionInfo> listActiveSubscriptions() const;
    std::optional<SubscriptionInfo> getSubscription(const std::string& subscriptionId) const;

    // --- License Generation ---
    static std::string generateLicenseKeyForTier(SubscriptionTier tier);

private:
    mutable std::mutex m_mutex;

    // Provider configurations
    struct PayPalConfig {
        std::string clientId;
        std::string clientSecret;
        bool sandbox = false;
        bool configured = false;
    } m_paypal;

    struct StripeConfig {
        std::string publishableKey;
        std::string secretKey;
        bool configured = false;
    } m_stripe;

    // Active subscriptions
    std::vector<SubscriptionInfo> m_subscriptions;

    // Internal methods
    PaymentResult createPayPalSubscription(SubscriptionTier tier,
                                             SubscriptionPeriod period,
                                             const std::string& customerEmail);
    PaymentResult createStripeSubscription(SubscriptionTier tier,
                                             SubscriptionPeriod period,
                                             const std::string& customerEmail);
    PaymentResult createPayPalOneTime(SubscriptionTier tier,
                                        const std::string& customerEmail);
    PaymentResult createStripeOneTime(SubscriptionTier tier,
                                        const std::string& customerEmail);

    bool cancelPayPalSubscription(const std::string& subscriptionId);
    bool cancelStripeSubscription(const std::string& subscriptionId);

    bool verifyPayPalSignature(const WebhookEvent& event);
    bool verifyStripeSignature(const WebhookEvent& event);

    // HTTP helpers
    std::string httpPost(const std::string& url,
                          const std::string& body,
                          const std::map<std::string, std::string>& headers);
    std::string httpGet(const std::string& url,
                         const std::map<std::string, std::string>& headers);
    std::string httpDelete(const std::string& url,
                            const std::map<std::string, std::string>& headers);

    // Base64 encode
    static std::string base64Encode(const std::string& input);
};

} // namespace licensing
} // namespace powsys365
