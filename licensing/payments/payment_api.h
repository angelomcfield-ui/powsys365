#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stdexcept>

#include "pricing.h"

namespace powsys365 {

// ============================================================================
// Payment Provider Enum
// ============================================================================

enum class PaymentProvider {
    Stripe,
    PayPal,
    Unknown
};

std::string paymentProviderToString(PaymentProvider provider);
PaymentProvider stringToPaymentProvider(const std::string& str);

// ============================================================================
// Data Structures
// ============================================================================

struct CheckoutSession {
    std::string session_id;
    std::string url;
    std::string status;           // open, complete, expired
    std::string plan_tier;
    double amount_usd;
    std::string currency;
    std::string customer_id;
    std::string subscription_id;
    int64_t created_at;
    int64_t expires_at;

    std::string toJson() const;
    static CheckoutSession fromJson(const std::string& json);
};

struct PaymentResult {
    bool success;
    std::string transaction_id;
    std::string status;           // succeeded, pending, failed
    std::string message;
    double amount_charged;
    std::string currency;
    std::string receipt_url;
    int64_t timestamp;

    std::string toJson() const;
    static PaymentResult fromJson(const std::string& json);
};

struct SubscriptionStatus {
    std::string subscription_id;
    std::string status;           // active, canceled, past_due, unpaid, trialing
    std::string plan_tier;
    std::string current_period_start;
    std::string current_period_end;
    bool cancel_at_period_end;
    std::string canceled_at;
    std::string payment_provider;

    std::string toJson() const;
    static SubscriptionStatus fromJson(const std::string& json);
};

struct Invoice {
    std::string invoice_id;
    std::string subscription_id;
    std::string customer_id;
    std::string customer_email;
    double amount_due;
    double amount_paid;
    std::string currency;
    std::string status;           // draft, open, paid, void, uncollectible
    std::string pdf_url;
    std::string invoice_number;
    std::string period_start;
    std::string period_end;
    std::vector<std::pair<std::string, double>> line_items;

    std::string toJson() const;
    static Invoice fromJson(const std::string& json);
};

struct WebhookPayload {
    PaymentProvider provider;
    std::string raw_body;
    std::string signature;
    std::string event_type;
    std::string event_id;
    std::string data_json;

    std::string toJson() const;
};

// ============================================================================
// Payment Exceptions
// ============================================================================

class PaymentException : public std::runtime_error {
public:
    explicit PaymentException(const std::string& msg) : std::runtime_error(msg) {}
    explicit PaymentException(const char* msg) : std::runtime_error(msg) {}
};

class InvalidProviderException : public PaymentException {
public:
    explicit InvalidProviderException(const std::string& msg) : PaymentException(msg) {}
};

class WebhookVerificationException : public PaymentException {
public:
    explicit WebhookVerificationException(const std::string& msg) : PaymentException(msg) {}
};

class PaymentDisabledException : public PaymentException {
public:
    explicit PaymentDisabledException(const std::string& msg) : PaymentException(msg) {}
};

// ============================================================================
// PaymentAPI - Unified Payment Gateway
// ============================================================================

class PaymentAPI {
public:
    // Constructor
    PaymentAPI();
    ~PaymentAPI();

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    // Configure Stripe with secret API key
    void configureStripe(const std::string& api_key);

    // Configure PayPal with client ID and secret
    void configurePayPal(const std::string& client_id, const std::string& client_secret);

    // Set PayPal sandbox mode
    void setPayPalSandbox(bool sandbox);

    // Enable/disable all payments (SWITCH)
    void enablePayments();
    void disablePayments();
    bool arePaymentsEnabled() const;

    // Check if Stripe is configured
    bool isStripeConfigured() const;

    // Check if PayPal is configured
    bool isPayPalConfigured() const;

    // ------------------------------------------------------------------
    // Checkout & Payments
    // ------------------------------------------------------------------

    // Create a checkout session for a plan
    CheckoutSession createCheckoutSession(LicenseTier plan);

    // Create a checkout session with a specific provider
    CheckoutSession createCheckoutSession(LicenseTier plan, PaymentProvider provider);

    // Process a one-time payment
    PaymentResult processPayment(PaymentProvider provider,
                                  double amount,
                                  const std::string& currency);

    // ------------------------------------------------------------------
    // Subscriptions
    // ------------------------------------------------------------------

    // Get subscription status
    SubscriptionStatus getSubscriptionStatus(const std::string& subscription_id,
                                              PaymentProvider provider);

    // Cancel a subscription
    bool cancelSubscription(const std::string& subscription_id,
                            PaymentProvider provider);

    // ------------------------------------------------------------------
    // Invoicing
    // ------------------------------------------------------------------

    // Generate an invoice for a subscription
    Invoice generateInvoice(const std::string& subscription_id,
                            PaymentProvider provider);

    // ------------------------------------------------------------------
    // Webhooks
    // ------------------------------------------------------------------

    // Verify and parse a webhook payload
    bool verifyWebhook(PaymentProvider provider,
                        const std::string& payload,
                        const std::string& signature);

    // Get the last verified webhook event
    WebhookPayload getLastWebhookEvent() const;

    // Set webhook event callback
    void setWebhookCallback(std::function<void(const WebhookPayload&)> callback);

    // ------------------------------------------------------------------
    // Pricing
    // ------------------------------------------------------------------

    // Get pricing table (hardcoded with fallback to web)
    std::vector<PricingPlan> getPricingTable();

    // Get current pricing from website
    std::vector<PricingPlan> getPricingFromWebsite();

    // Get plan details
    PricingPlan getPlanDetails(LicenseTier tier);

    // ------------------------------------------------------------------
    // Utilities
    // ------------------------------------------------------------------

    // Convert amount to cents (for Stripe)
    static int64_t dollarsToCents(double dollars);

    // Convert cents to dollars
    static double centsToDollars(int64_t cents);

    // Get current timestamp
    static int64_t getTimestamp();

    // Generate unique ID
    static std::string generateId();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace powsys365
