#include "payment_api.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/hmac.h>

#include <sstream>
#include <iomanip>
#include <chrono>
#include <regex>
#include <cstring>
#include <iostream>

namespace powsys365 {

// ============================================================================
// PayPalAPI - Full PayPal REST v2 Implementation
// ============================================================================

namespace paypal {

    static std::string extractJsonString(const std::string& json, const std::string& key);
    static std::string extractNestedJsonString(const std::string& json, const std::string& parent, const std::string& key);

    /**
     * @brief PayPalAPIClient - Low-level PayPal REST API client
     */
    class PayPalAPIClient {
    public:
        PayPalAPIClient(const std::string& client_id, const std::string& client_secret, bool sandbox = true)
            : client_id_(client_id), client_secret_(client_secret), sandbox_(sandbox) {
            base_url_ = sandbox
                ? "https://api-m.sandbox.paypal.com"
                : "https://api-m.paypal.com";
        }

        // ------------------------------------------------------------------
        // Authentication
        // ------------------------------------------------------------------

        bool authenticate() {
            std::string credentials = client_id_ + ":" + client_secret_;
            std::string auth_b64 = base64Encode(credentials);

            CURL* curl = curl_easy_init();
            if (!curl) return false;

            std::string response_body;
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "Accept-Language: en_US");
            headers = curl_slist_append(headers, ("Authorization: Basic " + auth_b64).c_str());

            curl_easy_setopt(curl, CURLOPT_URL, (base_url_ + "/v1/oauth2/token").c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "grant_type=client_credentials");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

            CURLcode res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) return false;

            access_token_ = extractJsonString(response_body, "access_token");
            std::string expires_str = extractJsonString(response_body, "expires_in");
            int expires_in = 3200; // default
            try {
                if (!expires_str.empty()) expires_in = std::stoi(expires_str);
            } catch (...) {}

            token_expiry_ = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + expires_in - 60;

            return !access_token_.empty();
        }

        bool ensureAuthenticated() {
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (access_token_.empty() || now >= token_expiry_) {
                return authenticate();
            }
            return true;
        }

        // ------------------------------------------------------------------
        // Orders (Payments)
        // ------------------------------------------------------------------

        std::string createOrder(double amount, const std::string& currency = "USD",
                                 const std::string& description = "POWSYS365 License",
                                 const std::string& return_url = "https://www.powsys365.com/success",
                                 const std::string& cancel_url = "https://www.powsys365.com/cancel") {
            if (!ensureAuthenticated()) {
                throw std::runtime_error("PayPal authentication failed");
            }

            std::ostringstream amount_str;
            amount_str << std::fixed << std::setprecision(2) << amount;

            std::ostringstream body;
            body << "{"
                 << "\"intent\":\"CAPTURE\","
                 << "\"purchase_units\":[{"
                 << "\"amount\":{"
                 << "\"currency_code\":\"" << currency << "\","
                 << "\"value\":\"" << amount_str.str() << "\""
                 << "},"
                 << "\"description\":\"" << jsonEscape(description) << "\""
                 << "}],"
                 << "\"application_context\":{"
                 << "\"brand_name\":\"POWSYS365\","
                 << "\"landing_page\":\"BILLING\","
                 << "\"user_action\":\"PAY_NOW\","
                 << "\"return_url\":\"" << jsonEscape(return_url) << "\","
                 << "\"cancel_url\":\"" << jsonEscape(cancel_url) << "\""
                 << "}}";

            return request("/v2/checkout/orders", "POST", body.str());
        }

        std::string getOrder(const std::string& order_id) {
            if (!ensureAuthenticated()) {
                throw std::runtime_error("PayPal authentication failed");
            }
            return request("/v2/checkout/orders/" + order_id, "GET");
        }

        std::string captureOrder(const std::string& order_id) {
            if (!ensureAuthenticated()) {
                throw std::runtime_error("PayPal authentication failed");
            }
            return request("/v2/checkout/orders/" + order_id + "/capture", "POST", "{}");
        }

        std::string confirmOrderPaymentSource(const std::string& order_id,
                                                const std::string& token_id) {
            if (!ensureAuthenticated()) {
                throw std::runtime_error("PayPal authentication failed");
            }
            std::string body = "{\"payment_source\":{\"token\":{\"id\":\"" +
                               jsonEscape(token_id) + "\",\"type\":\"BILLING_AGREEMENT\"}}}";
            return request("/v2/checkout/orders/" + order_id + "/confirm-payment-source", "POST", body);
        }

        // ------------------------------------------------------------------
        // Payments
        // ------------------------------------------------------------------

        std::string getCapturedPayment(const std::string& capture_id) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v2/payments/captures/" + capture_id, "GET");
        }

        std::string refundCapturedPayment(const std::string& capture_id,
                                           double amount = 0.0,
                                           const std::string& currency = "USD") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string body;
            if (amount > 0) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << "{\"amount\":{\"value\":\"" << amount << "\",\"currency_code\":\"" << currency << "\"}}";
                body = oss.str();
            } else {
                body = "{}";
            }
            return request("/v2/payments/captures/" + capture_id + "/refund", "POST", body);
        }

        // ------------------------------------------------------------------
        // Subscription Plans
        // ------------------------------------------------------------------

        std::string createProduct(const std::string& name,
                                   const std::string& description = "",
                                   const std::string& type = "SERVICE") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");

            std::ostringstream body;
            body << "{"
                 << "\"name\":\"" << jsonEscape(name) << "\","
                 << "\"description\":\"" << jsonEscape(description) << "\","
                 << "\"type\":\"" << type << "\","
                 << "\"category\":\"SOFTWARE\""
                 << "}";

            return request("/v1/catalogs/products", "POST", body.str());
        }

        std::string createSubscriptionPlan(const std::string& product_id,
                                            const std::string& plan_name,
                                            const std::string& billing_interval = "YEAR",
                                            int billing_cycles = 0, // 0 = unlimited
                                            double price = 0.0,
                                            const std::string& currency = "USD") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");

            std::ostringstream amount_str;
            amount_str << std::fixed << std::setprecision(2) << price;

            std::string cycles_json;
            if (billing_cycles > 0) {
                cycles_json = std::to_string(billing_cycles);
            } else {
                cycles_json = "0"; // infinite
            }

            std::ostringstream body;
            body << "{"
                 << "\"product_id\":\"" << product_id << "\","
                 << "\"name\":\"" << jsonEscape(plan_name) << "\","
                 << "\"description\":\"POWSYS365 subscription plan\","
                 << "\"status\":\"ACTIVE\","
                 << "\"billing_cycles\":[{"
                 << "\"frequency\":{"
                 << "\"interval_unit\":\"" << billing_interval << "\","
                 << "\"interval_count\":1"
                 << "},"
                 << "\"tenure_type\":\"REGULAR\","
                 << "\"sequence\":1,"
                 << "\"total_cycles\":" << cycles_json << ","
                 << "\"pricing_scheme\":{"
                 << "\"fixed_price\":{"
                 << "\"value\":\"" << amount_str.str() << "\","
                 << "\"currency_code\":\"" << currency << "\""
                 << "}}}],"
                 << "\"payment_preferences\":{"
                 << "\"auto_bill_outstanding\":true,"
                 << "\"setup_fee_failure_action\":\"CONTINUE\","
                 << "\"payment_failure_threshold\":3"
                 << "}}";

            return request("/v1/billing/plans", "POST", body.str());
        }

        std::string listPlans() {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v1/billing/plans", "GET");
        }

        std::string getPlan(const std::string& plan_id) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v1/billing/plans/" + plan_id, "GET");
        }

        std::string updatePlan(const std::string& plan_id, const std::string& new_status = "ACTIVE") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string body = "[{\"op\":\"replace\",\"path\":\"/status\",\"value\":\"" + new_status + "\"}]";
            return request("/v1/billing/plans/" + plan_id, "PATCH", body);
        }

        // ------------------------------------------------------------------
        // Subscriptions
        // ------------------------------------------------------------------

        std::string createSubscription(const std::string& plan_id,
                                        const std::string& subscriber_email = "",
                                        const std::string& subscriber_name = "",
                                        const std::string& return_url = "https://www.powsys365.com/success",
                                        const std::string& cancel_url = "https://www.powsys365.com/cancel") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");

            std::ostringstream body;
            body << "{"
                 << "\"plan_id\":\"" << plan_id << "\","
                 << "\"application_context\":{"
                 << "\"brand_name\":\"POWSYS365\","
                 << "\"shipping_preference\":\"NO_SHIPPING\","
                 << "\"user_action\":\"SUBSCRIBE_NOW\","
                 << "\"return_url\":\"" << jsonEscape(return_url) << "\","
                 << "\"cancel_url\":\"" << jsonEscape(cancel_url) << "\"";

            if (!subscriber_email.empty() || !subscriber_name.empty()) {
                body << "},\"subscriber\":{"
                     << "\"name\":{\"given_name\":\"" << jsonEscape(subscriber_name) << "\"},";
                if (!subscriber_email.empty()) {
                    body << "\"email_address\":\"" << jsonEscape(subscriber_email) << "\"";
                }
            }
            body << "}}";

            return request("/v1/billing/subscriptions", "POST", body.str());
        }

        std::string getSubscription(const std::string& subscription_id) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v1/billing/subscriptions/" + subscription_id, "GET");
        }

        std::string updateSubscription(const std::string& subscription_id,
                                        const std::string& plan_id = "") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::ostringstream body;
            if (!plan_id.empty()) {
                body << "[{\"op\":\"replace\",\"path\":\"/plan_id\",\"value\":\"" << plan_id << "\"}]";
            }
            return request("/v1/billing/subscriptions/" + subscription_id, "PATCH", body.str());
        }

        std::string cancelSubscription(const std::string& subscription_id,
                                        const std::string& reason = "Requested by customer") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string body = "{\"reason\":\"" + jsonEscape(reason) + "\"}";
            return request("/v1/billing/subscriptions/" + subscription_id + "/cancel", "POST", body);
        }

        std::string suspendSubscription(const std::string& subscription_id,
                                         const std::string& reason = "Administrative suspension") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string body = "{\"reason\":\"" + jsonEscape(reason) + "\"}";
            return request("/v1/billing/subscriptions/" + subscription_id + "/suspend", "POST", body);
        }

        std::string activateSubscription(const std::string& subscription_id,
                                          const std::string& reason = "Customer request") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string body = "{\"reason\":\"" << jsonEscape(reason) << "\"}";
            return request("/v1/billing/subscriptions/" + subscription_id + "/activate", "POST", body);
        }

        std::string listSubscriptionTransactions(const std::string& subscription_id,
                                                   int page = 1,
                                                   int page_size = 10) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string endpoint = "/v1/billing/subscriptions/" + subscription_id +
                                    "/transactions?page=" + std::to_string(page) +
                                    "&page_size=" + std::to_string(page_size);
            return request(endpoint, "GET");
        }

        // ------------------------------------------------------------------
        // Webhook Management
        // ------------------------------------------------------------------

        std::string createWebhook(const std::string& url,
                                   const std::vector<std::string>& event_types) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");

            std::ostringstream body;
            body << "{"
                 << "\"url\":\"" << jsonEscape(url) << "\","
                 << "\"event_types\":[";
            for (size_t i = 0; i < event_types.size(); ++i) {
                body << "{\"name\":\"" << jsonEscape(event_types[i]) << "\"}";
                if (i + 1 < event_types.size()) body << ",";
            }
            body << "]}";

            return request("/v1/notifications/webhooks", "POST", body.str());
        }

        std::string listWebhooks() {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v1/notifications/webhooks", "GET");
        }

        std::string deleteWebhook(const std::string& webhook_id) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v1/notifications/webhooks/" + webhook_id, "DELETE");
        }

        std::string getWebhookEventTypes() {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v1/notifications/webhooks-event-types", "GET");
        }

        // ------------------------------------------------------------------
        // Webhook Signature Verification
        // ------------------------------------------------------------------

        bool verifyWebhookSignature(const std::string& auth_algo,
                                     const std::string& auth_nonce,
                                     long auth_timestamp,
                                     const std::string& cert_url,
                                     const std::string& transmission_id,
                                     const std::string& transmission_sig,
                                     const std::string& transmission_time,
                                     const std::string& webhook_id,
                                     const std::string& event_body) {
            if (!ensureAuthenticated()) return false;

            std::ostringstream body;
            body << "{"
                 << "\"auth_algo\":\"" << jsonEscape(auth_algo) << "\","
                 << "\"cert_url\":\"" << jsonEscape(cert_url) << "\","
                 << "\"transmission_id\":\"" << jsonEscape(transmission_id) << "\","
                 << "\"transmission_sig\":\"" << jsonEscape(transmission_sig) << "\","
                 << "\"transmission_time\":\"" << jsonEscape(transmission_time) << "\","
                 << "\"webhook_id\":\"" << jsonEscape(webhook_id) << "\","
                 << "\"webhook_event\":" << event_body
                 << "}";

            try {
                std::string response = request("/v1/notifications/verify-webhook-signature", "POST", body.str());
                std::string status = extractJsonString(response, "verification_status");
                return status == "SUCCESS";
            } catch (...) {
                return false;
            }
        }

        // ------------------------------------------------------------------
        // Invoicing
        // ------------------------------------------------------------------

        std::string createInvoiceDraft(const std::string& customer_email,
                                        const std::vector<std::tuple<std::string, std::string, double>>& items,
                                        const std::string& currency = "USD",
                                        const std::string& note = "POWSYS365 License Invoice") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");

            std::ostringstream body;
            body << "{"
                 << "\"detail\":{"
                 << "\"invoice_number\":\"POWSYS365-" << generateInvoiceNumber() << "\","
                 << "\"currency_code\":\"" << currency << "\","
                 << "\"note\":\"" << jsonEscape(note) << "\","
                 << "\"payment_term\":{\"term_type\":\"DUE_ON_RECEIPT\"}"
                 << "},"
                 << "\"invoicer\":{"
                 << "\"email_address\":\"billing@powsys365.com\","
                 << "\"business_name\":\"POWSYS365\""
                 << "},"
                 << "\"primary_recipients\":[{"
                 << "\"billing_info\":{\"email_address\":\"" << jsonEscape(customer_email) << "\"}"
                 << "}],";

            body << "\"items\":[";
            for (size_t i = 0; i < items.size(); ++i) {
                std::ostringstream amount_str;
                amount_str << std::fixed << std::setprecision(2) << std::get<2>(items[i]);
                body << "{"
                     << "\"name\":\"" << jsonEscape(std::get<0>(items[i])) << "\","
                     << "\"description\":\"" << jsonEscape(std::get<1>(items[i])) << "\","
                     << "\"quantity\":\"1\","
                     << "\"unit_amount\":{"
                     << "\"currency_code\":\"" << currency << "\","
                     << "\"value\":\"" << amount_str.str() << "\""
                     << "}}";
                if (i + 1 < items.size()) body << ",";
            }
            body << "]}";

            return request("/v2/invoicing/invoices", "POST", body.str());
        }

        std::string sendInvoice(const std::string& invoice_id) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v2/invoicing/invoices/" + invoice_id + "/send", "POST");
        }

        std::string getInvoice(const std::string& invoice_id) {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v2/invoicing/invoices/" + invoice_id, "GET");
        }

        std::string listInvoices() {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            return request("/v2/invoicing/invoices", "GET");
        }

        std::string cancelSentInvoice(const std::string& invoice_id,
                                       const std::string& note = "Cancelled per request") {
            if (!ensureAuthenticated()) throw std::runtime_error("PayPal authentication failed");
            std::string body = "{\"note\":\"" + jsonEscape(note) << "\",\"send_to_invoicer\":true,\"send_to_recipient\":true}";
            return request("/v2/invoicing/invoices/" + invoice_id + "/cancel", "POST", body);
        }

    private:
        std::string client_id_;
        std::string client_secret_;
        bool sandbox_;
        std::string base_url_;
        std::string access_token_;
        int64_t token_expiry_ = 0;

        static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            size_t totalSize = size * nmemb;
            auto* response = static_cast<std::string*>(userp);
            response->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        }

        std::string request(const std::string& endpoint, const std::string& method,
                            const std::string& body = "") {
            CURL* curl = curl_easy_init();
            if (!curl) {
                throw std::runtime_error("Failed to initialize CURL");
            }

            std::string url = base_url_ + endpoint;
            std::string response_body;

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token_).c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "PayPal-Request-Source: POWSYS365-CPP-SDK");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

            if (method == "POST") {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            } else if (method == "PATCH") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            } else if (method == "DELETE") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
                if (!body.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                }
            }

            CURLcode res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
            }

            if (http_code < 200 || http_code >= 300) {
                throw std::runtime_error("PayPal API error (HTTP " + std::to_string(http_code) + "): " + response_body);
            }

            return response_body;
        }

        static std::string base64Encode(const std::string& data) {
            BIO* bio = BIO_new(BIO_s_mem());
            BIO* b64 = BIO_new(BIO_f_base64());
            bio = BIO_push(b64, bio);
            BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
            BIO_write(bio, data.c_str(), static_cast<int>(data.size()));
            BIO_flush(bio);
            BUF_MEM* bufferPtr;
            BIO_get_mem_ptr(bio, &bufferPtr);
            std::string result(bufferPtr->data, bufferPtr->length);
            BIO_free_all(bio);
            return result;
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

        static std::string generateInvoiceNumber() {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            return std::to_string(ms);
        }
    };

    // =========================================================================
    // JSON helpers
    // =========================================================================

    std::string extractJsonString(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return match[1].str();
        }
        // Try without quotes
        std::regex re2("\"" + key + "\"\\s*:\\s*\"?([^\"\\s,}]+)\"?");
        if (std::regex_search(json, match, re2) && match.size() > 1) {
            return match[1].str();
        }
        return "";
    }

    std::string extractNestedJsonString(const std::string& json, const std::string& parent, const std::string& key) {
        std::regex re("\"" + parent + "\"\\s*:\\s*\\{([^}]*)\\}");
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            std::string nested = match[1].str();
            return extractJsonString(nested, key);
        }
        return "";
    }

} // namespace paypal

// ============================================================================
// Public PayPal API Functions
// ============================================================================

inline std::string paypalCreateOrder(const std::string& client_id,
                                      const std::string& client_secret,
                                      bool sandbox,
                                      double amount,
                                      const std::string& currency = "USD",
                                      const std::string& description = "POWSYS365 License") {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    return client.createOrder(amount, currency, description);
}

inline std::string paypalCaptureOrder(const std::string& client_id,
                                       const std::string& client_secret,
                                       bool sandbox,
                                       const std::string& order_id) {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    return client.captureOrder(order_id);
}

inline std::string paypalCreateSubscriptionPlan(const std::string& client_id,
                                                  const std::string& client_secret,
                                                  bool sandbox,
                                                  const std::string& product_name,
                                                  const std::string& billing_interval,
                                                  double price) {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    // First create product
    std::string product_resp = client.createProduct(product_name, "POWSYS365 license subscription");
    std::string product_id = paypal::extractJsonString(product_resp, "id");
    if (product_id.empty()) {
        throw std::runtime_error("Failed to create PayPal product");
    }
    // Then create plan
    return client.createSubscriptionPlan(product_id, product_name, billing_interval, 0, price);
}

inline std::string paypalCreateSubscription(const std::string& client_id,
                                              const std::string& client_secret,
                                              bool sandbox,
                                              const std::string& plan_id,
                                              const std::string& subscriber_email = "",
                                              const std::string& subscriber_name = "") {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    return client.createSubscription(plan_id, subscriber_email, subscriber_name);
}

inline bool paypalVerifyWebhookSignature(const std::string& client_id,
                                          const std::string& client_secret,
                                          bool sandbox,
                                          const std::string& transmission_id,
                                          const std::string& transmission_sig,
                                          const std::string& transmission_time,
                                          const std::string& webhook_id,
                                          const std::string& event_body,
                                          const std::string& cert_url,
                                          const std::string& auth_algo,
                                          const std::string& auth_nonce,
                                          long auth_timestamp) {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    return client.verifyWebhookSignature(auth_algo, auth_nonce, auth_timestamp,
                                          cert_url, transmission_id, transmission_sig,
                                          transmission_time, webhook_id, event_body);
}

inline std::string paypalCancelSubscription(const std::string& client_id,
                                              const std::string& client_secret,
                                              bool sandbox,
                                              const std::string& subscription_id,
                                              const std::string& reason = "Requested by customer") {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    return client.cancelSubscription(subscription_id, reason);
}

inline std::string paypalGetSubscription(const std::string& client_id,
                                          const std::string& client_secret,
                                          bool sandbox,
                                          const std::string& subscription_id) {
    paypal::PayPalAPIClient client(client_id, client_secret, sandbox);
    return client.getSubscription(subscription_id);
}

} // namespace powsys365
