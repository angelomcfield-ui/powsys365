/**
 * @file ai_gateway.h
 * @brief Multi-LLM AI Gateway for POWSYS365.
 *
 * Provides a unified interface to query multiple Large Language Model
 * providers: DeepSeek, Kimi (Moonshot), GPT-4, and Claude.
 *
 * Features:
 * - Unified query/chat interface
 * - Configurable API keys per provider
 * - Built-in rate limiting and exponential-backoff retries
 * - Streaming response support
 * - Domain-specific analysis for power systems
 */

#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace powsy365::ai {

/* ------------------------------------------------------------------ */
/*  Provider enumeration                                               */
/* ------------------------------------------------------------------ */

enum class LLMProvider {
    DeepSeek,
    Kimi,      // Moonshot AI
    GPT4,      // OpenAI
    Claude,    // Anthropic
};

std::string providerName(LLMProvider p);

/* ------------------------------------------------------------------ */
/*  Request / Response structures                                      */
/* ------------------------------------------------------------------ */

struct Message {
    std::string role;    // "system", "user", "assistant", "tool"
    std::string content;
    std::string name;    // optional tool name
};

struct ChatRequest {
    LLMProvider provider = LLMProvider::DeepSeek;
    std::string model;               // e.g. "deepseek-chat"
    std::vector<Message> messages;
    float temperature = 0.7f;
    int max_tokens = 4096;
    float top_p = 1.0f;
    bool stream = false;
    std::string json_schema;         // optional structured output schema
};

struct ChatResponse {
    bool success = false;
    std::string content;
    std::string finish_reason;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    std::string model;
    double latency_ms = 0.0;
    std::string error_message;
    std::vector<Message> tool_calls;
};

/* ------------------------------------------------------------------ */
/*  Rate limiter                                                       */
/* ------------------------------------------------------------------ */

class RateLimiter {
public:
    explicit RateLimiter(int requests_per_minute = 60);

    /**
     * Block until a request slot is available.
     * Returns the wait time in milliseconds.
     */
    double acquire();

    void setRate(int requests_per_minute);

private:
    std::mutex mutex_;
    std::chrono::steady_clock::time_point last_request_;
    std::chrono::duration<double> min_interval_;
    int requests_per_minute_;
};

/* ------------------------------------------------------------------ */
/*  Retry policy                                                       */
/* ------------------------------------------------------------------ */

struct RetryPolicy {
    int max_retries = 3;
    double base_delay_ms = 500.0;
    double max_delay_ms = 30000.0;
    double backoff_multiplier = 2.0;
    std::vector<int> retry_status_codes = {429, 500, 502, 503, 504};
};

/* ------------------------------------------------------------------ */
/*  AI Gateway                                                         */
/* ------------------------------------------------------------------ */

class AIGateway {
public:
    explicit AIGateway(const RetryPolicy& retry_policy = {});
    ~AIGateway() = default;

    // -- Configuration --

    void setApiKey(LLMProvider provider, const std::string& api_key);
    bool hasApiKey(LLMProvider provider) const;

    void setModel(LLMProvider provider, const std::string& model_name);
    std::string getModel(LLMProvider provider) const;

    void setRateLimit(LLMProvider provider, int requests_per_minute);
    void setRetryPolicy(const RetryPolicy& policy);

    // -- Synchronous queries --

    ChatResponse query(const ChatRequest& request);

    ChatResponse query(
        LLMProvider provider,
        const std::string& prompt,
        float temperature = 0.7f,
        int max_tokens = 4096
    );

    // -- Chat with message history --

    ChatResponse chat(const ChatRequest& request);

    // -- Streaming (callback-based) --

    using StreamCallback = std::function<void(const std::string& chunk, bool done)>;

    void streamQuery(const ChatRequest& request, StreamCallback callback);

    // -- Power-system specific analysis --

    ChatResponse analyzePowerSystem(
        LLMProvider provider,
        const std::string& system_description,
        const std::string& question = "",
        float temperature = 0.3f
    );

    ChatResponse suggestCorrectiveActions(
        LLMProvider provider,
        const std::string& fault_description,
        float temperature = 0.2f
    );

    // -- Multi-provider fallback --

    ChatResponse queryWithFallback(
        const std::vector<LLMProvider>& provider_priority,
        const ChatRequest& request
    );

    // -- Statistics --

    struct Stats {
        std::atomic<int64_t> total_requests{0};
        std::atomic<int64_t> successful_requests{0};
        std::atomic<int64_t> failed_requests{0};
        std::atomic<int64_t> retried_requests{0};
        std::atomic<double> total_latency_ms{0.0};
    };

    Stats getStats() const;
    void resetStats();

private:
    // -- Provider-specific HTTP backends --

    ChatResponse queryDeepSeek(const ChatRequest& req);
    ChatResponse queryKimi(const ChatRequest& req);
    ChatResponse queryGPT4(const ChatRequest& req);
    ChatResponse queryClaude(const ChatRequest& req);

    // -- HTTP helpers --

    std::string httpPost(
        const std::string& url,
        const std::string& json_body,
        const std::map<std::string, std::string>& headers,
        int timeout_ms = 60000
    );

    std::string httpPostStream(
        const std::string& url,
        const std::string& json_body,
        const std::map<std::string, std::string>& headers,
        StreamCallback callback,
        int timeout_ms = 120000
    );

    // -- Internal --

    ChatResponse executeWithRetry(
        LLMProvider provider,
        const ChatRequest& req,
        std::function<ChatResponse(const ChatRequest&)> fn
    );

    std::map<std::string, std::string> buildHeaders(LLMProvider provider) const;
    std::string buildRequestBody(const ChatRequest& req) const;
    ChatResponse parseResponse(const std::string& raw, LLMProvider provider);

    // -- Members --

    std::map<LLMProvider, std::string> api_keys_;
    std::map<LLMProvider, std::string> models_;
    std::map<LLMProvider, std::unique_ptr<RateLimiter>> rate_limiters_;
    RetryPolicy retry_policy_;
    mutable Stats stats_;
    mutable std::mutex stats_mutex_;
};

} // namespace powsy365::ai
