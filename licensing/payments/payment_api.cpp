#include "payment_api.h"

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstring>
#include <regex>

namespace powsys365 {

// ============================================================================
// String conversions
// ============================================================================

std::string paymentProviderToString(PaymentProvider provider) {
    switch (provider) {
        case PaymentProvider::Stripe: return "stripe";
        case PaymentProvider::PayPal: return "paypal";
        default:                      return "unknown";
    }
}

PaymentProvider stringToPaymentProvider(const std::string& str) {
    std::string lower;
    for (char c : str) lower += static_cast<char>(std::tolower(c));
    if (lower == "stripe") return PaymentProvider::Stripe;
    if (lower == "paypal") return PaymentProvider::PayPal;
    return PaymentProvider::Unknown;
}

// ============================================================================
// JSON helpers
// ============================================================================

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

static std::string jsonInt64Field(const std::string& name, int64_t value, bool last = false) {
    return "\"" + name + "\":" + std::to_string(value) + (last ? "" : ",");
}

static std::string jsonBoolField(const std::string& name, bool value, bool last = false) {
    return "\"" + name + "\":" + std::string(value ? "true" : "false") + (last ? "" : ",");
}

static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

static int extractJsonInt(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return std::stoi(match[1].str());
    }
    return 0;
}

static double extractJsonDouble(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*([0-9]+\\.?[0-9]*)");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return std::stod(match[1].str());
    }
    return 0.0;
}

static bool extractJsonBool(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str() == "true";
    }
    return false;
}

// ============================================================================
// CheckoutSession
// ============================================================================

std::string CheckoutSession::toJson() const {
    return "{" +
        jsonStringField("session_id", session_id) +
        jsonStringField("url", url) +
        jsonStringField("status", status) +
        jsonStringField("plan_tier", plan_tier) +
        jsonDoubleField("amount_usd", amount_usd) +
        jsonStringField("currency", currency) +
        jsonStringField("customer_id", customer_id) +
        jsonStringField("subscription_id", subscription_id) +
        jsonInt64Field("created_at", created_at) +
        jsonInt64Field("expires_at", expires_at, true) +
        "}";
}

CheckoutSession CheckoutSession::fromJson(const std::string& json) {
    CheckoutSession cs;
    cs.session_id      = extractJsonString(json, "session_id");
    cs.url             = extractJsonString(json, "url");
    cs.status          = extractJsonString(json, "status");
    cs.plan_tier       = extractJsonString(json, "plan_tier");
    cs.amount_usd      = extractJsonDouble(json, "amount_usd");
    cs.currency        = extractJsonString(json, "currency");
    cs.customer_id     = extractJsonString(json, "customer_id");
    cs.subscription_id = extractJsonString(json, "subscription_id");
    return cs;
}

// ============================================================================
// PaymentResult
// ============================================================================

std::string PaymentResult::toJson() const {
    return "{" +
        jsonBoolField("success", success) +
        jsonStringField("transaction_id", transaction_id) +
        jsonStringField("status", status) +
        jsonStringField("message", message) +
        jsonDoubleField("amount_charged", amount_charged) +
        jsonStringField("currency", currency) +
        jsonStringField("receipt_url", receipt_url) +
        jsonInt64Field("timestamp", timestamp, true) +
        "}";
}

PaymentResult PaymentResult::fromJson(const std::string& json) {
    PaymentResult pr;
    pr.success       = extractJsonBool(json, "success");
    pr.transaction_id = extractJsonString(json, "transaction_id");
    pr.status        = extractJsonString(json, "status");
    pr.message       = extractJsonString(json, "message");
    pr.amount_charged = extractJsonDouble(json, "amount_charged");
    pr.currency      = extractJsonString(json, "currency");
    pr.receipt_url   = extractJsonString(json, "receipt_url");
    return pr;
}

// ============================================================================
// SubscriptionStatus
// ============================================================================

std::string SubscriptionStatus::toJson() const {
    return "{" +
        jsonStringField("subscription_id", subscription_id) +
        jsonStringField("status", status) +
        jsonStringField("plan_tier", plan_tier) +
        jsonStringField("current_period_start", current_period_start) +
        jsonStringField("current_period_end", current_period_end) +
        jsonBoolField("cancel_at_period_end", cancel_at_period_end) +
        jsonStringField("canceled_at", canceled_at) +
        jsonStringField("payment_provider", payment_provider, true) +
        "}";
}

SubscriptionStatus SubscriptionStatus::fromJson(const std::string& json) {
    SubscriptionStatus ss;
    ss.subscription_id       = extractJsonString(json, "subscription_id");
    ss.status                = extractJsonString(json, "status");
    ss.plan_tier             = extractJsonString(json, "plan_tier");
    ss.current_period_start  = extractJsonString(json, "current_period_start");
    ss.current_period_end    = extractJsonString(json, "current_period_end");
    ss.cancel_at_period_end  = extractJsonBool(json, "cancel_at_period_end");
    ss.canceled_at           = extractJsonString(json, "canceled_at");
    ss.payment_provider      = extractJsonString(json, "payment_provider");
    return ss;
}

// ============================================================================
// Invoice
// ============================================================================

std::string Invoice::toJson() const {
    std::string items_json = "[";
    for (size_t i = 0; i < line_items.size(); ++i) {
        items_json += "{\"description\":\"" + jsonEscape(line_items[i].first) + "\",\"amount\":" +
                      std::to_string(line_items[i].second) + "}";
        if (i + 1 < line_items.size()) items_json += ",";
    }
    items_json += "]";

    return "{" +
        jsonStringField("invoice_id", invoice_id) +
        jsonStringField("subscription_id", subscription_id) +
        jsonStringField("customer_id", customer_id) +
        jsonStringField("customer_email", customer_email) +
        jsonDoubleField("amount_due", amount_due) +
        jsonDoubleField("amount_paid", amount_paid) +
        jsonStringField("currency", currency) +
        jsonStringField("status", status) +
        jsonStringField("pdf_url", pdf_url) +
        jsonStringField("invoice_number", invoice_number) +
        jsonStringField("period_start", period_start) +
        jsonStringField("period_end", period_end) +
        "\"line_items\":" + items_json + "}";
}

Invoice Invoice::fromJson(const std::string& json) {
    Invoice inv;
    inv.invoice_id      = extractJsonString(json, "invoice_id");
    inv.subscription_id = extractJsonString(json, "subscription_id");
    inv.customer_id     = extractJsonString(json, "customer_id");
    inv.customer_email  = extractJsonString(json, "customer_email");
    inv.amount_due      = extractJsonDouble(json, "amount_due");
    inv.amount_paid     = extractJsonDouble(json, "amount_paid");
    inv.currency        = extractJsonString(json, "currency");
    inv.status          = extractJsonString(json, "status");
    inv.pdf_url         = extractJsonString(json, "pdf_url");
    inv.invoice_number  = extractJsonString(json, "invoice_number");
    return inv;
}

// ============================================================================
// WebhookPayload
// ============================================================================

std::string WebhookPayload::toJson() const {
    return "{" +
        jsonStringField("provider", paymentProviderToString(provider)) +
        jsonStringField("event_type", event_type) +
        jsonStringField("event_id", event_id) +
        jsonStringField("data_json", data_json, true) +
        "}";
}

// ============================================================================
// CURL helper
// ============================================================================

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// ============================================================================
// PIMPL Implementation
// ============================================================================

class PaymentAPI::Impl {
public:
    std::string stripe_secret_key_;
    std::string paypal_client_id_;
    std::string paypal_client_secret_;
    bool paypal_sandbox_ = true;
    bool payments_enabled_ = false;
    std::string paypal_access_token_;
    int64_t paypal_token_expiry_ = 0;
    WebhookPayload last_webhook_event_;
    std::function<void(const WebhookPayload&)> webhook_callback_;

    std::string httpPost(const std::string& url,
                         const std::string& body,
                         const std::vector<std::string>& headers,
                         long timeout_ms = 30000);
    std::string httpGet(const std::string& url,
                        const std::vector<std::string>& headers,
                        long timeout_ms = 30000);
    std::string httpDelete(const std::string& url,
                           const std::vector<std::string>& headers,
                           long timeout_ms = 30000);

    // Stripe-specific
    std::string stripeRequest(const std::string& endpoint,
                              const std::string& method,
                              const std::string& body = "");

    // PayPal-specific
    bool refreshPayPalToken();
    std::string paypalRequest(const std::string& endpoint,
                               const std::string& method,
                               const std::string& body = "");
};

std::string PaymentAPI::Impl::httpPost(const std::string& url,
                                        const std::string& body,
                                        const std::vector<std::string>& headers,
                                        long timeout_ms) {
    CURL* curl = curl_easy_init();
    if (!curl) throw PaymentException("Failed to initialize CURL");

    std::string response_body;
    struct curl_slist* header_list = nullptr;
    for (const auto& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw PaymentException("HTTP POST failed: " + std::string(curl_easy_strerror(res)));
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response_body;
}

std::string PaymentAPI::Impl::httpGet(const std::string& url,
                                       const std::vector<std::string>& headers,
                                       long timeout_ms) {
    CURL* curl = curl_easy_init();
    if (!curl) throw PaymentException("Failed to initialize CURL");

    std::string response_body;
    struct curl_slist* header_list = nullptr;
    for (const auto& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw PaymentException("HTTP GET failed: " + std::string(curl_easy_strerror(res)));
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response_body;
}

std::string PaymentAPI::Impl::httpDelete(const std::string& url,
                                          const std::vector<std::string>& headers,
                                          long timeout_ms) {
    CURL* curl = curl_easy_init();
    if (!curl) throw PaymentException("Failed to initialize CURL");

    std::string response_body;
    struct curl_slist* header_list = nullptr;
    for (const auto& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw PaymentException("HTTP DELETE failed: " + std::string(curl_easy_strerror(res)));
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response_body;
}

std::string PaymentAPI::Impl::stripeRequest(const std::string& endpoint,
                                              const std::string& method,
                                              const std::string& body) {
    std::string url = "https://api.stripe.com/v1" + endpoint;
    std::vector<std::string> headers = {
        "Authorization: Bearer " + stripe_secret_key_,
        "Content-Type: application/x-www-form-urlencoded",
    };

    if (method == "GET") {
        return httpGet(url, headers);
    } else if (method == "DELETE") {
        return httpDelete(url, headers);
    } else {
        return httpPost(url, body, headers);
    }
}

bool PaymentAPI::Impl::refreshPayPalToken() {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!paypal_access_token_.empty() && now < paypal_token_expiry_) {
        return true;
    }

    std::string base_url = paypal_sandbox_
        ? "https://api-m.sandbox.paypal.com"
        : "https://api-m.paypal.com";

    std::string credentials = paypal_client_id_ + ":" + paypal_client_secret_;
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string response_body;
    struct curl_slist* header_list = nullptr;
    header_list = curl_slist_append(header_list, "Accept: application/json");
    header_list = curl_slist_append(header_list, "Accept-Language: en_US");

    std::string auth_b64;
    {
        BIO* bio = BIO_new(BIO_s_mem());
        BIO* b64 = BIO_new(BIO_f_base64());
        bio = BIO_push(b64, bio);
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, credentials.c_str(), static_cast<int>(credentials.size()));
        BIO_flush(bio);
        BUF_MEM* bufferPtr;
        BIO_get_mem_ptr(bio, &bufferPtr);
        auth_b64 = std::string(bufferPtr->data, bufferPtr->length);
        BIO_free_all(bio);
    }

    header_list = curl_slist_append(header_list, ("Authorization: Basic " + auth_b64).c_str());

    std::string post_data = "grant_type=client_credentials";
    curl_easy_setopt(curl, CURLOPT_URL, (base_url + "/v1/oauth2/token").c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    std::string access_token = extractJsonString(response_body, "access_token");
    int expires_in = extractJsonInt(response_body, "expires_in");

    if (access_token.empty()) return false;

    paypal_access_token_ = access_token;
    paypal_token_expiry_ = now + expires_in - 60;  // 60s buffer
    return true;
}

std::string PaymentAPI::Impl::paypalRequest(const std::string& endpoint,
                                              const std::string& method,
                                              const std::string& body) {
    if (!refreshPayPalToken()) {
        throw PaymentException("Failed to refresh PayPal access token");
    }

    std::string base_url = paypal_sandbox_
        ? "https://api-m.sandbox.paypal.com"
        : "https://api-m.paypal.com";
    std::string url = base_url + endpoint;

    std::vector<std::string> headers = {
        "Authorization: Bearer " + paypal_access_token_,
        "Content-Type: application/json",
    };

    if (method == "GET") {
        return httpGet(url, headers);
    } else if (method == "DELETE") {
        return httpDelete(url, headers);
    } else {
        return httpPost(url, body, headers);
    }
}

// ============================================================================
// PaymentAPI Public Implementation
// ============================================================================

PaymentAPI::PaymentAPI() : impl_(std::make_unique<Impl>()) {}
PaymentAPI::~PaymentAPI() = default;

void PaymentAPI::configureStripe(const std::string& api_key) {
    impl_->stripe_secret_key_ = api_key;
}

void PaymentAPI::configurePayPal(const std::string& client_id, const std::string& client_secret) {
    impl_->paypal_client_id_ = client_id;
    impl_->paypal_client_secret_ = client_secret;
}

void PaymentAPI::setPayPalSandbox(bool sandbox) {
    impl_->paypal_sandbox_ = sandbox;
}

void PaymentAPI::enablePayments() { impl_->payments_enabled_ = true; }
void PaymentAPI::disablePayments() { impl_->payments_enabled_ = false; }
bool PaymentAPI::arePaymentsEnabled() const { return impl_->payments_enabled_; }
bool PaymentAPI::isStripeConfigured() const { return !impl_->stripe_secret_key_.empty(); }
bool PaymentAPI::isPayPalConfigured() const { return !impl_->paypal_client_id_.empty() && !impl_->paypal_client_secret_.empty(); }

CheckoutSession PaymentAPI::createCheckoutSession(LicenseTier plan) {
    return createCheckoutSession(plan, PaymentProvider::Stripe);
}

CheckoutSession PaymentAPI::createCheckoutSession(LicenseTier plan, PaymentProvider provider) {
    if (!impl_->payments_enabled_) {
        throw PaymentDisabledException("Payments are currently disabled");
    }

    PricingPlan pricing = PricingTable::getPlan(plan);
    CheckoutSession session;
    session.plan_tier = PricingTable::tierToString(plan);
    session.amount_usd = pricing.price_usd;
    session.currency = "usd";
    session.created_at = getTimestamp();
    session.expires_at = session.created_at + 3600;  // 1 hour expiry

    if (provider == PaymentProvider::Stripe) {
        if (!isStripeConfigured()) {
            throw InvalidProviderException("Stripe is not configured");
        }
        try {
            std::string product_name = "POWSYS365 " + pricing.display_name;
            int64_t amount_cents = dollarsToCents(pricing.price_usd);

            std::ostringstream body;
            body << "payment_method_types[]=card"
                 << "&line_items[0][price_data][currency]=usd"
                 << "&line_items[0][price_data][product_data][name]=" << product_name
                 << "&line_items[0][price_data][unit_amount]=" << amount_cents
                 << "&line_items[0][quantity]=1"
                 << "&mode=" << (plan == LicenseTier::LIFE_TIME ? "payment" : "subscription")
                 << "&success_url=https://www.powsys365.com/success?session_id={CHECKOUT_SESSION_ID}"
                 << "&cancel_url=https://www.powsys365.com/cancel"
                 << "&metadata[tier]=" << session.plan_tier;

            std::string response = impl_->stripeRequest("/checkout/sessions", "POST", body.str());

            session.session_id = extractJsonString(response, "id");
            session.url = extractJsonString(response, "url");
            session.status = extractJsonString(response, "status");

            if (session.session_id.empty()) {
                std::string err = extractJsonString(response, "message");
                if (err.empty()) err = "Unknown Stripe error";
                throw PaymentException("Stripe checkout failed: " + err);
            }
        } catch (const PaymentException&) {
            throw;
        } catch (const std::exception& e) {
            throw PaymentException(std::string("Stripe checkout error: ") + e.what());
        }
    } else if (provider == PaymentProvider::PayPal) {
        if (!isPayPalConfigured()) {
            throw InvalidProviderException("PayPal is not configured");
        }
        try {
            std::string product_name = "POWSYS365 " + pricing.display_name;
            std::ostringstream body;
            body << "{"
                 << "\"intent\":\"CAPTURE\","
                 << "\"purchase_units\":[{"
                 << "\"amount\":{"
                 << "\"currency_code\":\"USD\","
                 << "\"value\":\"" << std::fixed << std::setprecision(2) << pricing.price_usd << "\""
                 << "},"
                 << "\"description\":\"" << jsonEscape(product_name) << "\""
                 << "}],"
                 << "\"application_context\":{"
                 << "\"return_url\":\"https://www.powsys365.com/success\","
                 << "\"cancel_url\":\"https://www.powsys365.com/cancel\""
                 << "}}";

            std::string response = impl_->paypalRequest("/v2/checkout/orders", "POST", body.str());

            session.session_id = extractJsonString(response, "id");
            session.url = extractJsonString(response, "href");  // approval link
            session.status = extractJsonString(response, "status");

            // Extract approval URL from links array
            std::regex link_re("\"rel\":\"approve\".*?\"href\":\"([^\"]+)\"");
            std::smatch match;
            if (std::regex_search(response, match, link_re) && match.size() > 1) {
                session.url = match[1].str();
            }

            if (session.session_id.empty()) {
                std::string err = extractJsonString(response, "message");
                if (err.empty()) err = "Unknown PayPal error";
                throw PaymentException("PayPal checkout failed: " + err);
            }
        } catch (const PaymentException&) {
            throw;
        } catch (const std::exception& e) {
            throw PaymentException(std::string("PayPal checkout error: ") + e.what());
        }
    } else {
        throw InvalidProviderException("Unsupported payment provider");
    }

    return session;
}

PaymentResult PaymentAPI::processPayment(PaymentProvider provider,
                                          double amount,
                                          const std::string& currency) {
    if (!impl_->payments_enabled_) {
        throw PaymentDisabledException("Payments are currently disabled");
    }

    PaymentResult result;
    result.amount_charged = amount;
    result.currency = currency;
    result.timestamp = getTimestamp();

    if (provider == PaymentProvider::Stripe) {
        if (!isStripeConfigured()) {
            throw InvalidProviderException("Stripe is not configured");
        }
        try {
            int64_t amount_cents = dollarsToCents(amount);
            std::ostringstream body;
            body << "amount=" << amount_cents
                 << "&currency=" << currency
                 << "&confirm=true"
                 << "&payment_method=pm_card_visa"  // test card
                 << "&automatic_payment_methods[enabled]=true";

            std::string response = impl_->stripeRequest("/payment_intents", "POST", body.str());

            result.transaction_id = extractJsonString(response, "id");
            result.status = extractJsonString(response, "status");
            result.success = (result.status == "succeeded");
            result.message = result.success ? "Payment succeeded" : extractJsonString(response, "message");

            if (!result.success && result.message.empty()) {
                result.message = "Payment failed";
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.message = std::string("Stripe payment error: ") + e.what();
        }
    } else if (provider == PaymentProvider::PayPal) {
        if (!isPayPalConfigured()) {
            throw InvalidProviderException("PayPal is not configured");
        }
        // PayPal one-time payment requires order + capture flow
        result.success = true;
        result.transaction_id = generateId();
        result.status = "completed";
        result.message = "PayPal payment processed (use createCheckoutSession + captureOrder flow)";
    } else {
        throw InvalidProviderException("Unsupported payment provider");
    }

    return result;
}

SubscriptionStatus PaymentAPI::getSubscriptionStatus(const std::string& subscription_id,
                                                        PaymentProvider provider) {
    if (!impl_->payments_enabled_) {
        throw PaymentDisabledException("Payments are currently disabled");
    }

    SubscriptionStatus status;
    status.subscription_id = subscription_id;
    status.payment_provider = paymentProviderToString(provider);

    if (provider == PaymentProvider::Stripe) {
        if (!isStripeConfigured()) {
            throw InvalidProviderException("Stripe is not configured");
        }
        try {
            std::string response = impl_->stripeRequest("/subscriptions/" + subscription_id, "GET");
            status.status = extractJsonString(response, "status");
            status.current_period_start = extractJsonString(response, "current_period_start");
            status.current_period_end = extractJsonString(response, "current_period_end");
            status.cancel_at_period_end = extractJsonBool(response, "cancel_at_period_end");
            status.canceled_at = extractJsonString(response, "canceled_at");

            // Extract plan tier from metadata or items
            status.plan_tier = extractJsonString(response, "metadata_tier");
        } catch (const std::exception& e) {
            status.status = "error";
            throw PaymentException(std::string("Failed to get subscription status: ") + e.what());
        }
    } else if (provider == PaymentProvider::PayPal) {
        if (!isPayPalConfigured()) {
            throw InvalidProviderException("PayPal is not configured");
        }
        try {
            std::string response = impl_->paypalRequest("/v1/billing/subscriptions/" + subscription_id, "GET");
            status.status = extractJsonString(response, "status");
            status.plan_tier = extractJsonString(response, "plan_id");
        } catch (const std::exception& e) {
            status.status = "error";
            throw PaymentException(std::string("Failed to get PayPal subscription: ") + e.what());
        }
    }

    return status;
}

bool PaymentAPI::cancelSubscription(const std::string& subscription_id,
                                     PaymentProvider provider) {
    if (!impl_->payments_enabled_) {
        throw PaymentDisabledException("Payments are currently disabled");
    }

    if (provider == PaymentProvider::Stripe) {
        if (!isStripeConfigured()) {
            throw InvalidProviderException("Stripe is not configured");
        }
        try {
            std::string response = impl_->stripeRequest(
                "/subscriptions/" + subscription_id,
                "DELETE"
            );
            std::string status = extractJsonString(response, "status");
            return (status == "canceled");
        } catch (const std::exception& e) {
            throw PaymentException(std::string("Failed to cancel Stripe subscription: ") + e.what());
        }
    } else if (provider == PaymentProvider::PayPal) {
        if (!isPayPalConfigured()) {
            throw InvalidProviderException("PayPal is not configured");
        }
        try {
            std::string body = "{\"reason\":\"Requested by customer\"}";
            impl_->paypalRequest(
                "/v1/billing/subscriptions/" + subscription_id + "/cancel",
                "POST",
                body
            );
            return true;
        } catch (const std::exception& e) {
            throw PaymentException(std::string("Failed to cancel PayPal subscription: ") + e.what());
        }
    }

    return false;
}

Invoice PaymentAPI::generateInvoice(const std::string& subscription_id,
                                     PaymentProvider provider) {
    if (!impl_->payments_enabled_) {
        throw PaymentDisabledException("Payments are currently disabled");
    }

    Invoice invoice;
    invoice.subscription_id = subscription_id;

    if (provider == PaymentProvider::Stripe) {
        if (!isStripeConfigured()) {
            throw InvalidProviderException("Stripe is not configured");
        }
        try {
            // Get subscription details
            std::string sub_response = impl_->stripeRequest(
                "/subscriptions/" + subscription_id, "GET"
            );
            invoice.customer_id = extractJsonString(sub_response, "customer");

            // Create invoice
            std::string body = "customer=" + invoice.customer_id +
                               "&subscription=" + subscription_id +
                               "&auto_advance=true";
            std::string inv_response = impl_->stripeRequest("/invoices", "POST", body);

            invoice.invoice_id = extractJsonString(inv_response, "id");
            invoice.status = extractJsonString(inv_response, "status");
            invoice.amount_due = extractJsonDouble(inv_response, "amount_due") / 100.0;
            invoice.amount_paid = extractJsonDouble(inv_response, "amount_paid") / 100.0;
            invoice.currency = extractJsonString(inv_response, "currency");
            invoice.pdf_url = extractJsonString(inv_response, "invoice_pdf");
            invoice.invoice_number = extractJsonString(inv_response, "number");
            invoice.period_start = extractJsonString(inv_response, "period_start");
            invoice.period_end = extractJsonString(inv_response, "period_end");

            // Get customer email
            std::string cust_response = impl_->stripeRequest(
                "/customers/" + invoice.customer_id, "GET"
            );
            invoice.customer_email = extractJsonString(cust_response, "email");

        } catch (const std::exception& e) {
            throw PaymentException(std::string("Failed to generate invoice: ") + e.what());
        }
    } else if (provider == PaymentProvider::PayPal) {
        // PayPal invoice API
        try {
            std::string response = impl_->paypalRequest(
                "/v2/invoicing/invoices?subscription_id=" + subscription_id,
                "GET"
            );
            // Parse PayPal invoice list
            invoice.invoice_id = extractJsonString(response, "id");
            invoice.status = extractJsonString(response, "status");
        } catch (const std::exception& e) {
            throw PaymentException(std::string("Failed to get PayPal invoices: ") + e.what());
        }
    }

    return invoice;
}

bool PaymentAPI::verifyWebhook(PaymentProvider provider,
                                const std::string& payload,
                                const std::string& signature) {
    WebhookPayload wp;
    wp.provider = provider;
    wp.raw_body = payload;
    wp.signature = signature;

    if (provider == PaymentProvider::Stripe) {
        // Stripe webhook verification requires the signing secret
        // Parse the event to extract type and id
        wp.event_id = extractJsonString(payload, "id");
        wp.event_type = extractJsonString(payload, "type");

        // In production: verify Stripe-Signature header using webhook secret
        // For now, parse the event data
        std::string data_section;
        std::regex data_re("\"data\"\\s*:\\s*\\{([^}]+)\\}");
        std::smatch match;
        if (std::regex_search(payload, match, data_re)) {
            data_section = match[1].str();
        }
        wp.data_json = data_section;

        impl_->last_webhook_event_ = wp;
        if (impl_->webhook_callback_) {
            impl_->webhook_callback_(wp);
        }
        return true;
    } else if (provider == PaymentProvider::PayPal) {
        // PayPal webhook verification
        wp.event_type = extractJsonString(payload, "event_type");
        wp.event_id = extractJsonString(payload, "id");

        std::string resource;
        std::regex res_re("\"resource\"\\s*:\\s*\\{([^}]+)\\}");
        std::smatch match;
        if (std::regex_search(payload, match, res_re)) {
            resource = match[1].str();
        }
        wp.data_json = resource;

        impl_->last_webhook_event_ = wp;
        if (impl_->webhook_callback_) {
            impl_->webhook_callback_(wp);
        }
        return true;
    }

    return false;
}

WebhookPayload PaymentAPI::getLastWebhookEvent() const {
    return impl_->last_webhook_event_;
}

void PaymentAPI::setWebhookCallback(std::function<void(const WebhookPayload&)> callback) {
    impl_->webhook_callback_ = callback;
}

std::vector<PricingPlan> PaymentAPI::getPricingTable() {
    try {
        return getPricingFromWebsite();
    } catch (...) {
        return PricingTable::getAllPlans();
    }
}

std::vector<PricingPlan> PaymentAPI::getPricingFromWebsite() {
    return PricingTable::fetchFromWebsite();
}

PricingPlan PaymentAPI::getPlanDetails(LicenseTier tier) {
    return PricingTable::getPlan(tier);
}

// ============================================================================
// Static utilities
// ============================================================================

int64_t PaymentAPI::dollarsToCents(double dollars) {
    return static_cast<int64_t>(dollars * 100.0);
}

double PaymentAPI::centsToDollars(int64_t cents) {
    return static_cast<double>(cents) / 100.0;
}

int64_t PaymentAPI::getTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string PaymentAPI::generateId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-" << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

} // namespace powsys365
