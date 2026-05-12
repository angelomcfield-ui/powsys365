#include "payment_api.h"

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <sstream>
#include <iomanip>
#include <chrono>
#include <regex>
#include <cstring>

namespace powsys365 {

// ============================================================================
// StripeAPI - Full Stripe Implementation
// ============================================================================

// Forward declarations for internal helpers
namespace stripe {

    static std::string extractJsonString(const std::string& json, const std::string& key);
    static int extractJsonInt(const std::string& json, const std::string& key);
    static double extractJsonDouble(const std::string& json, const std::string& key);
    static bool extractJsonBool(const std::string& json, const std::string& key);

    /**
     * @brief StripeAPIClient - Low-level Stripe API client
     */
    class StripeAPIClient {
    public:
        explicit StripeAPIClient(const std::string& secret_key)
            : secret_key_(secret_key), base_url_("https://api.stripe.com/v1") {}

        // ------------------------------------------------------------------
        // Customer Management
        // ------------------------------------------------------------------

        std::string createCustomer(const std::string& email,
                                    const std::string& name = "",
                                    const std::string& description = "") {
            std::ostringstream body;
            body << "email=" << urlEncode(email);
            if (!name.empty()) {
                body << "&name=" << urlEncode(name);
            }
            if (!description.empty()) {
                body << "&description=" << urlEncode(description);
            }
            body << "&metadata[source]=POWSYS365";

            return request("/customers", "POST", body.str());
        }

        std::string getCustomer(const std::string& customer_id) {
            return request("/customers/" + customer_id, "GET");
        }

        std::string updateCustomer(const std::string& customer_id,
                                    const std::string& email = "",
                                    const std::string& name = "") {
            std::ostringstream body;
            if (!email.empty()) body << "email=" << urlEncode(email) << "&";
            if (!name.empty()) body << "name=" << urlEncode(name) << "&";
            return request("/customers/" + customer_id, "POST", body.str());
        }

        std::string deleteCustomer(const std::string& customer_id) {
            return request("/customers/" + customer_id, "DELETE");
        }

        // ------------------------------------------------------------------
        // Product & Price Management
        // ------------------------------------------------------------------

        std::string createProduct(const std::string& name,
                                   const std::string& description = "") {
            std::ostringstream body;
            body << "name=" << urlEncode(name);
            if (!description.empty()) {
                body << "&description=" << urlEncode(description);
            }
            body << "&type=service";
            return request("/products", "POST", body.str());
        }

        std::string createPrice(const std::string& product_id,
                                 int64_t amount_cents,
                                 const std::string& currency = "usd",
                                 const std::string& interval = "year") {
            std::ostringstream body;
            body << "product=" << product_id
                 << "&unit_amount=" << amount_cents
                 << "&currency=" << currency
                 << "&recurring[interval]=" << interval;
            return request("/prices", "POST", body.str());
        }

        // ------------------------------------------------------------------
        // Checkout Sessions
        // ------------------------------------------------------------------

        std::string createCheckoutSession(const std::string& price_id,
                                           const std::string& customer_id = "",
                                           const std::string& tier = "",
                                           bool is_one_time = false) {
            std::ostringstream body;
            body << "payment_method_types[]=card"
                 << "&line_items[0][price]=" << price_id
                 << "&line_items[0][quantity]=1"
                 << "&mode=" << (is_one_time ? "payment" : "subscription")
                 << "&success_url=https://www.powsys365.com/success?session_id={CHECKOUT_SESSION_ID}"
                 << "&cancel_url=https://www.powsys365.com/cancel";

            if (!customer_id.empty()) {
                body << "&customer=" << customer_id;
            }
            if (!tier.empty()) {
                body << "&metadata[tier]=" << tier;
            }
            body << "&metadata[product]=POWSYS365";

            return request("/checkout/sessions", "POST", body.str());
        }

        std::string createCheckoutSessionWithLineItems(
            const std::vector<std::pair<std::string, int>>& line_items,  // name, cents
            const std::string& customer_id = "",
            const std::string& tier = "",
            bool is_one_time = false) {
            std::ostringstream body;
            body << "payment_method_types[]=card";

            for (size_t i = 0; i < line_items.size(); ++i) {
                body << "&line_items[" << i << "][price_data][currency]=usd"
                     << "&line_items[" << i << "][price_data][product_data][name]="
                     << urlEncode(line_items[i].first)
                     << "&line_items[" << i << "][price_data][unit_amount]="
                     << line_items[i].second
                     << "&line_items[" << i << "][quantity]=1";
            }

            body << "&mode=" << (is_one_time ? "payment" : "subscription")
                 << "&success_url=https://www.powsys365.com/success?session_id={CHECKOUT_SESSION_ID}"
                 << "&cancel_url=https://www.powsys365.com/cancel";

            if (!customer_id.empty()) {
                body << "&customer=" << customer_id;
            }
            if (!tier.empty()) {
                body << "&metadata[tier]=" << tier;
            }

            return request("/checkout/sessions", "POST", body.str());
        }

        std::string getCheckoutSession(const std::string& session_id) {
            return request("/checkout/sessions/" + session_id, "GET");
        }

        std::string expireCheckoutSession(const std::string& session_id) {
            return request("/checkout/sessions/" + session_id + "/expire", "POST");
        }

        // ------------------------------------------------------------------
        // Subscriptions
        // ------------------------------------------------------------------

        std::string createSubscription(const std::string& customer_id,
                                        const std::string& price_id,
                                        const std::string& tier = "") {
            std::ostringstream body;
            body << "customer=" << customer_id
                 << "&items[0][price]=" << price_id
                 << "&payment_behavior=default_incomplete"
                 << "&expand[]=latest_invoice.payment_intent";
            if (!tier.empty()) {
                body << "&metadata[tier]=" << tier;
            }
            return request("/subscriptions", "POST", body.str());
        }

        std::string getSubscription(const std::string& subscription_id) {
            return request("/subscriptions/" + subscription_id, "GET");
        }

        std::string updateSubscription(const std::string& subscription_id,
                                        const std::string& price_id = "") {
            std::ostringstream body;
            if (!price_id.empty()) {
                body << "items[0][price]=" << price_id;
            }
            body << "&proration_behavior=create_prorations";
            return request("/subscriptions/" + subscription_id, "POST", body.str());
        }

        std::string cancelSubscription(const std::string& subscription_id,
                                        bool cancel_immediately = false) {
            if (cancel_immediately) {
                return request("/subscriptions/" + subscription_id, "DELETE");
            } else {
                std::string body = "cancel_at_period_end=true";
                return request("/subscriptions/" + subscription_id, "POST", body);
            }
        }

        std::string listSubscriptions(const std::string& customer_id = "",
                                       int limit = 10) {
            std::string endpoint = "/subscriptions?limit=" + std::to_string(limit);
            if (!customer_id.empty()) {
                endpoint += "&customer=" + customer_id;
            }
            return request(endpoint, "GET");
        }

        // ------------------------------------------------------------------
        // Payment Intents
        // ------------------------------------------------------------------

        std::string createPaymentIntent(int64_t amount_cents,
                                          const std::string& currency = "usd",
                                          const std::string& customer_id = "") {
            std::ostringstream body;
            body << "amount=" << amount_cents
                 << "&currency=" << currency
                 << "&automatic_payment_methods[enabled]=true";
            if (!customer_id.empty()) {
                body << "&customer=" << customer_id;
            }
            return request("/payment_intents", "POST", body.str());
        }

        std::string getPaymentIntent(const std::string& payment_intent_id) {
            return request("/payment_intents/" + payment_intent_id, "GET");
        }

        std::string confirmPaymentIntent(const std::string& payment_intent_id,
                                          const std::string& payment_method = "") {
            std::string body;
            if (!payment_method.empty()) {
                body = "payment_method=" + payment_method;
            }
            return request("/payment_intents/" + payment_intent_id + "/confirm", "POST", body);
        }

        // ------------------------------------------------------------------
        // Invoicing
        // ------------------------------------------------------------------

        std::string createInvoice(const std::string& customer_id,
                                   const std::string& subscription_id = "") {
            std::ostringstream body;
            body << "customer=" << customer_id
                 << "&auto_advance=true";
            if (!subscription_id.empty()) {
                body << "&subscription=" << subscription_id;
            }
            return request("/invoices", "POST", body.str());
        }

        std::string getInvoice(const std::string& invoice_id) {
            return request("/invoices/" + invoice_id, "GET");
        }

        std::string finalizeInvoice(const std::string& invoice_id) {
            return request("/invoices/" + invoice_id + "/finalize", "POST");
        }

        std::string listInvoices(const std::string& customer_id = "",
                                  int limit = 10) {
            std::string endpoint = "/invoices?limit=" + std::to_string(limit);
            if (!customer_id.empty()) {
                endpoint += "&customer=" + customer_id;
            }
            return request(endpoint, "GET");
        }

        // ------------------------------------------------------------------
        // Webhook Events
        // ------------------------------------------------------------------

        std::string constructEvent(const std::string& payload,
                                    const std::string& sig_header,
                                    const std::string& webhook_secret) {
            // Verify the webhook signature
            if (!verifySignature(payload, sig_header, webhook_secret)) {
                throw std::runtime_error("Webhook signature verification failed");
            }
            return payload;
        }

        bool verifySignature(const std::string& payload,
                              const std::string& sig_header,
                              const std::string& webhook_secret) {
            // Parse Stripe-Signature header
            // Format: t=timestamp,v1=signature,v0=...
            std::string timestamp;
            std::string v1_signature;

            std::regex t_re("t=(\\d+)");
            std::regex v1_re("v1=([a-f0-9]+)");
            std::smatch match;

            if (std::regex_search(sig_header, match, t_re)) {
                timestamp = match[1].str();
            }
            if (std::regex_search(sig_header, match, v1_re)) {
                v1_signature = match[1].str();
            }

            if (timestamp.empty() || v1_signature.empty()) {
                return false;
            }

            // Compute expected signature
            std::string signed_payload = timestamp + "." + payload;
            unsigned char* mac = nullptr;
            unsigned int mac_len = 0;

            mac = HMAC(
                EVP_sha256(),
                webhook_secret.c_str(),
                static_cast<int>(webhook_secret.size()),
                reinterpret_cast<const unsigned char*>(signed_payload.c_str()),
                signed_payload.size(),
                nullptr,
                &mac_len
            );

            if (!mac || mac_len == 0) return false;

            std::stringstream expected_sig;
            for (unsigned int i = 0; i < mac_len; ++i) {
                expected_sig << std::hex << std::setw(2) << std::setfill('0')
                             << static_cast<int>(mac[i]);
            }

            // Constant-time comparison
            const std::string expected = expected_sig.str();
            if (expected.length() != v1_signature.length()) {
                return false;
            }
            unsigned char result = 0;
            for (size_t i = 0; i < expected.length(); ++i) {
                result |= static_cast<unsigned char>(expected[i] ^ v1_signature[i]);
            }
            return result == 0;
        }

        // ------------------------------------------------------------------
        // Refunds
        // ------------------------------------------------------------------

        std::string createRefund(const std::string& payment_intent_id,
                                  int64_t amount_cents = 0) {
            std::ostringstream body;
            body << "payment_intent=" << payment_intent_id;
            if (amount_cents > 0) {
                body << "&amount=" << amount_cents;
            }
            return request("/refunds", "POST", body.str());
        }

        // ------------------------------------------------------------------
        // Billing Portal
        // ------------------------------------------------------------------

        std::string createPortalSession(const std::string& customer_id,
                                         const std::string& return_url = "https://www.powsys365.com/account") {
            std::ostringstream body;
            body << "customer=" << customer_id
                 << "&return_url=" << urlEncode(return_url);
            return request("/billing_portal/sessions", "POST", body.str());
        }

    private:
        std::string secret_key_;
        std::string base_url_;

        static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            size_t totalSize = size * nmemb;
            auto* response = static_cast<std::string*>(userp);
            response->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        }

        std::string request(const std::string& endpoint,
                            const std::string& method,
                            const std::string& body = "") {
            CURL* curl = curl_easy_init();
            if (!curl) {
                throw std::runtime_error("Failed to initialize CURL");
            }

            std::string url = base_url_ + endpoint;
            std::string response_body;

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("Authorization: Bearer " + secret_key_).c_str());

            if (method == "POST" || method == "DELETE") {
                headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            } else {
                headers = curl_slist_append(headers, "Content-Type: application/json");
            }

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

            if (method == "POST") {
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
                std::string err_msg = extractJsonString(response_body, "message");
                if (err_msg.empty()) err_msg = response_body;
                throw std::runtime_error("Stripe API error (HTTP " + std::to_string(http_code) + "): " + err_msg);
            }

            return response_body;
        }

        static std::string urlEncode(const std::string& value) {
            std::ostringstream escaped;
            escaped.fill('0');
            escaped << std::hex;
            for (char c : value) {
                if (std::isalnum(static_cast<unsigned char>(c)) ||
                    c == '-' || c == '_' || c == '.' || c == '~') {
                    escaped << c;
                } else {
                    escaped << std::uppercase;
                    escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
                    escaped << std::nouppercase;
                }
            }
            return escaped.str();
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
        // Try matching without quotes for nested values
        std::regex re2("\"" + key + "\"\\s*:\\s*\"?([^\"\\s,}]+)\"?");
        if (std::regex_search(json, match, re2) && match.size() > 1) {
            return match[1].str();
        }
        return "";
    }

    int extractJsonInt(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return std::stoi(match[1].str());
        }
        return 0;
    }

    double extractJsonDouble(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*([0-9]+\\.?[0-9]*)");
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return std::stod(match[1].str());
        }
        return 0.0;
    }

    bool extractJsonBool(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return match[1].str() == "true";
        }
        return false;
    }

} // namespace stripe

// ============================================================================
// Public Stripe API Functions (C wrappers for easy usage)
// ============================================================================

/**
 * @brief Create a Stripe customer
 * @param secret_key Stripe secret API key
 * @param email Customer email
 * @param name Customer name
 * @param description Customer description
 * @return JSON response string with customer details
 */
inline std::string stripeCreateCustomer(const std::string& secret_key,
                                         const std::string& email,
                                         const std::string& name = "",
                                         const std::string& description = "") {
    stripe::StripeAPIClient client(secret_key);
    return client.createCustomer(email, name, description);
}

/**
 * @brief Create a Stripe checkout session
 * @param secret_key Stripe secret API key
 * @param price_id Stripe Price ID
 * @param customer_id Optional customer ID
 * @param tier POWSYS365 tier name
 * @param is_one_time Whether this is a one-time payment
 * @return JSON response string with session details
 */
inline std::string stripeCreateCheckoutSession(const std::string& secret_key,
                                                 const std::string& price_id,
                                                 const std::string& customer_id = "",
                                                 const std::string& tier = "",
                                                 bool is_one_time = false) {
    stripe::StripeAPIClient client(secret_key);
    return client.createCheckoutSession(price_id, customer_id, tier, is_one_time);
}

/**
 * @brief Create a Stripe subscription
 * @param secret_key Stripe secret API key
 * @param customer_id Stripe customer ID
 * @param price_id Stripe Price ID
 * @param tier POWSYS365 tier name
 * @return JSON response string with subscription details
 */
inline std::string stripeCreateSubscription(const std::string& secret_key,
                                              const std::string& customer_id,
                                              const std::string& price_id,
                                              const std::string& tier = "") {
    stripe::StripeAPIClient client(secret_key);
    return client.createSubscription(customer_id, price_id, tier);
}

/**
 * @brief Get a Stripe subscription
 * @param secret_key Stripe secret API key
 * @param subscription_id Stripe subscription ID
 * @return JSON response string with subscription details
 */
inline std::string stripeGetSubscription(const std::string& secret_key,
                                           const std::string& subscription_id) {
    stripe::StripeAPIClient client(secret_key);
    return client.getSubscription(subscription_id);
}

/**
 * @brief Cancel a Stripe subscription
 * @param secret_key Stripe secret API key
 * @param subscription_id Stripe subscription ID
 * @param cancel_immediately Cancel now vs at period end
 * @return JSON response string
 */
inline std::string stripeCancelSubscription(const std::string& secret_key,
                                               const std::string& subscription_id,
                                               bool cancel_immediately = false) {
    stripe::StripeAPIClient client(secret_key);
    return client.cancelSubscription(subscription_id, cancel_immediately);
}

/**
 * @brief Verify a Stripe webhook signature
 * @param payload Raw webhook body
 * @param sig_header Stripe-Signature header value
 * @param webhook_secret Webhook signing secret
 * @return true if signature is valid
 */
inline bool stripeVerifyWebhookSignature(const std::string& payload,
                                          const std::string& sig_header,
                                          const std::string& webhook_secret) {
    stripe::StripeAPIClient client("");
    return client.verifySignature(payload, sig_header, webhook_secret);
}

/**
 * @brief Construct and verify a Stripe webhook event
 * @param secret_key Stripe secret API key (not used for verification but kept for API consistency)
 * @param payload Raw webhook body
 * @param sig_header Stripe-Signature header value
 * @param webhook_secret Webhook signing secret
 * @return JSON payload if verification succeeds
 * @throws std::runtime_error if verification fails
 */
inline std::string stripeConstructEvent(const std::string& secret_key,
                                         const std::string& payload,
                                         const std::string& sig_header,
                                         const std::string& webhook_secret) {
    (void)secret_key; // Not needed for verification
    stripe::StripeAPIClient client("");
    return client.constructEvent(payload, sig_header, webhook_secret);
}

} // namespace powsys365
