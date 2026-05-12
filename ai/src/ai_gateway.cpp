/**
 * @file ai_gateway.cpp
 * @brief Implementation of the multi-LLM AI Gateway.
 */

#include "powsy365/ai/ai_gateway.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

// HTTP client - using libcurl
#include <curl/curl.h>

// JSON parsing
#include <nlohmann/json.hpp>

namespace powsy365::ai {

using json = nlohmann::json;
using namespace std::chrono;

/* ------------------------------------------------------------------ */
/*  Utility: provider name                                             */
/* ------------------------------------------------------------------ */

std::string providerName(LLMProvider p) {
    switch (p) {
        case LLMProvider::DeepSeek: return "DeepSeek";
        case LLMProvider::Kimi:     return "Kimi";
        case LLMProvider::GPT4:     return "GPT-4";
        case LLMProvider::Claude:   return "Claude";
    }
    return "Unknown";
}

/* ------------------------------------------------------------------ */
/*  RateLimiter                                                        */
/* ------------------------------------------------------------------ */

RateLimiter::RateLimiter(int requests_per_minute)
    : last_request_(steady_clock::now() - minutes(1)),
      requests_per_minute_(requests_per_minute) {
    setRate(requests_per_minute);
}

void RateLimiter::setRate(int requests_per_minute) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_per_minute_ = requests_per_minute;
    if (requests_per_minute > 0) {
        min_interval_ = duration<double>(60.0 / requests_per_minute);
    } else {
        min_interval_ = duration<double>::zero();
    }
}

double RateLimiter::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = steady_clock::now();
    auto elapsed = now - last_request_;

    if (elapsed < min_interval_) {
        auto wait_time = min_interval_ - elapsed;
        auto wait_ms = duration_cast<milliseconds>(wait_time).count();
        std::this_thread::sleep_for(wait_time);
        last_request_ = steady_clock::now();
        return static_cast<double>(wait_ms);
    }

    last_request_ = now;
    return 0.0;
}

/* ------------------------------------------------------------------ */
/*  AIGateway construction                                             */
/* ------------------------------------------------------------------ */

AIGateway::AIGateway(const RetryPolicy& retry_policy)
    : retry_policy_(retry_policy) {
    // Default models per provider
    models_[LLMProvider::DeepSeek] = "deepseek-chat";
    models_[LLMProvider::Kimi]     = "moonshot-v1-8k";
    models_[LLMProvider::GPT4]     = "gpt-4-turbo-preview";
    models_[LLMProvider::Claude]   = "claude-3-opus-20240229";

    // Default rate limits (requests per minute)
    rate_limiters_[LLMProvider::DeepSeek] = std::make_unique<RateLimiter>(60);
    rate_limiters_[LLMProvider::Kimi]     = std::make_unique<RateLimiter>(30);
    rate_limiters_[LLMProvider::GPT4]     = std::make_unique<RateLimiter>(40);
    rate_limiters_[LLMProvider::Claude]   = std::make_unique<RateLimiter>(40);
}

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

void AIGateway::setApiKey(LLMProvider provider, const std::string& api_key) {
    api_keys_[provider] = api_key;
}

bool AIGateway::hasApiKey(LLMProvider provider) const {
    auto it = api_keys_.find(provider);
    return it != api_keys_.end() && !it->second.empty();
}

void AIGateway::setModel(LLMProvider provider, const std::string& model_name) {
    models_[provider] = model_name;
}

std::string AIGateway::getModel(LLMProvider provider) const {
    auto it = models_.find(provider);
    return it != models_.end() ? it->second : "";
}

void AIGateway::setRateLimit(LLMProvider provider, int requests_per_minute) {
    auto it = rate_limiters_.find(provider);
    if (it != rate_limiters_.end()) {
        it->second->setRate(requests_per_minute);
    } else {
        rate_limiters_[provider] = std::make_unique<RateLimiter>(requests_per_minute);
    }
}

void AIGateway::setRetryPolicy(const RetryPolicy& policy) {
    retry_policy_ = policy;
}

/* ------------------------------------------------------------------ */
/*  Core query methods                                                 */
/* ------------------------------------------------------------------ */

ChatResponse AIGateway::query(const ChatRequest& request) {
    return chat(request);
}

ChatResponse AIGateway::query(
    LLMProvider provider,
    const std::string& prompt,
    float temperature,
    int max_tokens
) {
    ChatRequest req;
    req.provider = provider;
    req.model = getModel(provider);
    req.messages = {{"user", prompt, ""}};
    req.temperature = temperature;
    req.max_tokens = max_tokens;
    return chat(req);
}

ChatResponse AIGateway::chat(const ChatRequest& request) {
    stats_.total_requests++;

    auto fn = [this, &request](const ChatRequest& r) -> ChatResponse {
        switch (r.provider) {
            case LLMProvider::DeepSeek: return queryDeepSeek(r);
            case LLMProvider::Kimi:     return queryKimi(r);
            case LLMProvider::GPT4:     return queryGPT4(r);
            case LLMProvider::Claude:   return queryClaude(r);
        }
        ChatResponse resp;
        resp.success = false;
        resp.error_message = "Unknown provider";
        return resp;
    };

    ChatResponse result = executeWithRetry(request.provider, request, fn);
    stats_.total_latency_ms += result.latency_ms;

    if (result.success) {
        stats_.successful_requests++;
    } else {
        stats_.failed_requests++;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/*  Streaming                                                          */
/* ------------------------------------------------------------------ */

void AIGateway::streamQuery(const ChatRequest& request, StreamCallback callback) {
    if (!hasApiKey(request.provider)) {
        callback("", true);
        return;
    }

    // Apply rate limit
    auto it = rate_limiters_.find(request.provider);
    if (it != rate_limiters_.end()) {
        it->second->acquire();
    }

    std::string url;
    std::map<std::string, std::string> headers = buildHeaders(request.provider);

    switch (request.provider) {
        case LLMProvider::DeepSeek:
            url = "https://api.deepseek.com/v1/chat/completions";
            break;
        case LLMProvider::Kimi:
            url = "https://api.moonshot.cn/v1/chat/completions";
            break;
        case LLMProvider::GPT4:
            url = "https://api.openai.com/v1/chat/completions";
            break;
        case LLMProvider::Claude:
            url = "https://api.anthropic.com/v1/messages";
            break;
    }

    ChatRequest stream_req = request;
    stream_req.stream = true;
    std::string body = buildRequestBody(stream_req);

    try {
        httpPostStream(url, body, headers, callback, 120000);
    } catch (const std::exception& e) {
        callback(std::string("[ERROR: ") + e.what() + "]", true);
    }
}

/* ------------------------------------------------------------------ */
/*  Power-system specific analysis                                     */
/* ------------------------------------------------------------------ */

ChatResponse AIGateway::analyzePowerSystem(
    LLMProvider provider,
    const std::string& system_description,
    const std::string& question,
    float temperature
) {
    std::string system_prompt =
        "You are an expert power systems engineer with decades of experience "
        "in power flow analysis, fault studies, and grid stability. "
        "Provide concise, technically accurate answers with specific "
        "numerical values and actionable recommendations.";

    std::string user_prompt = system_description;
    if (!question.empty()) {
        user_prompt += "\n\nQuestion: " + question;
    }

    ChatRequest req;
    req.provider = provider;
    req.model = getModel(provider);
    req.messages = {
        {"system", system_prompt, ""},
        {"user", user_prompt, ""}
    };
    req.temperature = temperature;
    req.max_tokens = 4096;

    return chat(req);
}

ChatResponse AIGateway::suggestCorrectiveActions(
    LLMProvider provider,
    const std::string& fault_description,
    float temperature
) {
    std::string system_prompt =
        "You are a power system protection and control specialist. "
        "Given a fault scenario, suggest specific corrective actions, "
        "relay settings, and operational procedures to restore stable "
        "operation. Prioritize actions by urgency.";

    ChatRequest req;
    req.provider = provider;
    req.model = getModel(provider);
    req.messages = {
        {"system", system_prompt, ""},
        {"user", fault_description, ""}
    };
    req.temperature = temperature;
    req.max_tokens = 4096;

    return chat(req);
}

/* ------------------------------------------------------------------ */
/*  Multi-provider fallback                                            */
/* ------------------------------------------------------------------ */

ChatResponse AIGateway::queryWithFallback(
    const std::vector<LLMProvider>& provider_priority,
    const ChatRequest& request
) {
    for (auto provider : provider_priority) {
        if (!hasApiKey(provider)) continue;

        ChatRequest req = request;
        req.provider = provider;
        req.model = getModel(provider);

        ChatResponse resp = chat(req);
        if (resp.success) {
            return resp;
        }
    }

    ChatResponse resp;
    resp.success = false;
    resp.error_message = "All providers failed or no API keys configured";
    return resp;
}

/* ------------------------------------------------------------------ */
/*  Statistics                                                         */
/* ------------------------------------------------------------------ */

AIGateway::Stats AIGateway::getStats() const {
    return {stats_.total_requests.load(),
            stats_.successful_requests.load(),
            stats_.failed_requests.load(),
            stats_.retried_requests.load(),
            stats_.total_latency_ms.load()};
}

void AIGateway::resetStats() {
    stats_.total_requests = 0;
    stats_.successful_requests = 0;
    stats_.failed_requests = 0;
    stats_.retried_requests = 0;
    stats_.total_latency_ms = 0.0;
}

/* ------------------------------------------------------------------ */
/*  Provider-specific implementations                                  */
/* ------------------------------------------------------------------ */

ChatResponse AIGateway::queryDeepSeek(const ChatRequest& req) {
    ChatResponse resp;
    auto it = api_keys_.find(LLMProvider::DeepSeek);
    if (it == api_keys_.end() || it->second.empty()) {
        resp.error_message = "DeepSeek API key not configured";
        return resp;
    }

    std::string url = "https://api.deepseek.com/v1/chat/completions";
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + it->second},
        {"Content-Type", "application/json"}
    };

    json body;
    body["model"] = req.model.empty() ? models_[LLMProvider::DeepSeek] : req.model;
    body["messages"] = json::array();
    for (const auto& msg : req.messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        body["messages"].push_back(m);
    }
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.max_tokens;
    body["top_p"] = req.top_p;
    if (req.stream) body["stream"] = true;

    auto t0 = steady_clock::now();
    try {
        std::string raw = httpPost(url, body.dump(), headers);
        resp = parseResponse(raw, LLMProvider::DeepSeek);
    } catch (const std::exception& e) {
        resp.error_message = std::string("DeepSeek HTTP error: ") + e.what();
    }
    resp.latency_ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    return resp;
}

ChatResponse AIGateway::queryKimi(const ChatRequest& req) {
    ChatResponse resp;
    auto it = api_keys_.find(LLMProvider::Kimi);
    if (it == api_keys_.end() || it->second.empty()) {
        resp.error_message = "Kimi (Moonshot) API key not configured";
        return resp;
    }

    std::string url = "https://api.moonshot.cn/v1/chat/completions";
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + it->second},
        {"Content-Type", "application/json"}
    };

    json body;
    body["model"] = req.model.empty() ? models_[LLMProvider::Kimi] : req.model;
    body["messages"] = json::array();
    for (const auto& msg : req.messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        body["messages"].push_back(m);
    }
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.max_tokens;

    auto t0 = steady_clock::now();
    try {
        std::string raw = httpPost(url, body.dump(), headers);
        resp = parseResponse(raw, LLMProvider::Kimi);
    } catch (const std::exception& e) {
        resp.error_message = std::string("Kimi HTTP error: ") + e.what();
    }
    resp.latency_ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    return resp;
}

ChatResponse AIGateway::queryGPT4(const ChatRequest& req) {
    ChatResponse resp;
    auto it = api_keys_.find(LLMProvider::GPT4);
    if (it == api_keys_.end() || it->second.empty()) {
        resp.error_message = "OpenAI API key not configured";
        return resp;
    }

    std::string url = "https://api.openai.com/v1/chat/completions";
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + it->second},
        {"Content-Type", "application/json"}
    };

    json body;
    body["model"] = req.model.empty() ? models_[LLMProvider::GPT4] : req.model;
    body["messages"] = json::array();
    for (const auto& msg : req.messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        body["messages"].push_back(m);
    }
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.max_tokens;
    body["top_p"] = req.top_p;

    auto t0 = steady_clock::now();
    try {
        std::string raw = httpPost(url, body.dump(), headers);
        resp = parseResponse(raw, LLMProvider::GPT4);
    } catch (const std::exception& e) {
        resp.error_message = std::string("OpenAI HTTP error: ") + e.what();
    }
    resp.latency_ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    return resp;
}

ChatResponse AIGateway::queryClaude(const ChatRequest& req) {
    ChatResponse resp;
    auto it = api_keys_.find(LLMProvider::Claude);
    if (it == api_keys_.end() || it->second.empty()) {
        resp.error_message = "Anthropic API key not configured";
        return resp;
    }

    std::string url = "https://api.anthropic.com/v1/messages";
    std::map<std::string, std::string> headers = {
        {"x-api-key", it->second},
        {"anthropic-version", "2023-06-01"},
        {"Content-Type", "application/json"}
    };

    json body;
    body["model"] = req.model.empty() ? models_[LLMProvider::Claude] : req.model;
    body["max_tokens"] = req.max_tokens;
    body["temperature"] = req.temperature;

    // Claude uses "system" as top-level field, not in messages
    json messages = json::array();
    for (const auto& msg : req.messages) {
        if (msg.role == "system") {
            body["system"] = msg.content;
        } else {
            json m;
            m["role"] = msg.role;
            m["content"] = msg.content;
            messages.push_back(m);
        }
    }
    body["messages"] = messages;

    auto t0 = steady_clock::now();
    try {
        std::string raw = httpPost(url, body.dump(), headers);
        resp = parseResponse(raw, LLMProvider::Claude);
    } catch (const std::exception& e) {
        resp.error_message = std::string("Anthropic HTTP error: ") + e.what();
    }
    resp.latency_ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    return resp;
}

/* ------------------------------------------------------------------ */
/*  Retry logic                                                        */
/* ------------------------------------------------------------------ */

ChatResponse AIGateway::executeWithRetry(
    LLMProvider provider,
    const ChatRequest& req,
    std::function<ChatResponse(const ChatRequest&)> fn
) {
    double delay_ms = retry_policy_.base_delay_ms;
    ChatResponse last_resp;

    for (int attempt = 0; attempt <= retry_policy_.max_retries; ++attempt) {
        // Rate limiting
        auto rlit = rate_limiters_.find(provider);
        if (rlit != rate_limiters_.end()) {
            rlit->second->acquire();
        }

        last_resp = fn(req);

        if (last_resp.success) {
            return last_resp;
        }

        // Check if error is retryable
        bool should_retry = false;
        for (int code : retry_policy_.retry_status_codes) {
            if (last_resp.error_message.find(std::to_string(code)) != std::string::npos) {
                should_retry = true;
                break;
            }
        }
        // Retry on timeout / connection errors
        if (last_resp.error_message.find("HTTP error") != std::string::npos ||
            last_resp.error_message.find("Timeout") != std::string::npos ||
            last_resp.error_message.find("Connection") != std::string::npos) {
            should_retry = true;
        }

        if (!should_retry || attempt == retry_policy_.max_retries) {
            return last_resp;
        }

        stats_.retried_requests++;
        std::this_thread::sleep_for(milliseconds(static_cast<int>(delay_ms)));
        delay_ms = std::min(delay_ms * retry_policy_.backoff_multiplier,
                           retry_policy_.max_delay_ms);
    }

    return last_resp;
}

/* ------------------------------------------------------------------ */
/*  HTTP helpers                                                       */
/* ------------------------------------------------------------------ */

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

std::string AIGateway::httpPost(
    const std::string& url,
    const std::string& json_body,
    const std::map<std::string, std::string>& headers,
    int timeout_ms
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    struct curl_slist* curl_headers = nullptr;
    for (const auto& [key, value] : headers) {
        curl_headers = curl_slist_append(curl_headers, (key + ": " + value).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
    }

    if (http_code >= 400) {
        throw std::runtime_error(
            "HTTP " + std::to_string(http_code) + ": " + response
        );
    }

    return response;
}

std::string AIGateway::httpPostStream(
    const std::string& url,
    const std::string& json_body,
    const std::map<std::string, std::string>& headers,
    StreamCallback callback,
    int timeout_ms
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    struct curl_slist* curl_headers = nullptr;
    for (const auto& [key, value] : headers) {
        curl_headers = curl_slist_append(curl_headers, (key + ": " + value).c_str());
    }

    // For streaming, we accumulate and parse SSE chunks
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        callback(std::string("[STREAM ERROR: ") + curl_easy_strerror(res) + "]", true);
        return "";
    }

    // Parse SSE format (simplified)
    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("data: ") == 0) {
            std::string data = line.substr(6);
            if (data == "[DONE]") {
                callback("", true);
                break;
            }
            try {
                json j = json::parse(data);
                if (j.contains("choices") && !j["choices"].empty()) {
                    auto delta = j["choices"][0]["delta"];
                    if (delta.contains("content") && delta["content"].is_string()) {
                        callback(delta["content"].get<std::string>(), false);
                    }
                }
            } catch (...) {
                // Skip malformed chunks
            }
        }
    }
    callback("", true);
    return response;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

std::map<std::string, std::string> AIGateway::buildHeaders(LLMProvider provider) const {
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    auto it = api_keys_.find(provider);
    if (it != api_keys_.end() && !it->second.empty()) {
        switch (provider) {
            case LLMProvider::Claude:
                headers["x-api-key"] = it->second;
                headers["anthropic-version"] = "2023-06-01";
                break;
            default:
                headers["Authorization"] = "Bearer " + it->second;
                break;
        }
    }
    return headers;
}

std::string AIGateway::buildRequestBody(const ChatRequest& req) const {
    json body;
    body["model"] = req.model;
    body["messages"] = json::array();
    for (const auto& msg : req.messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        body["messages"].push_back(m);
    }
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.max_tokens;
    body["top_p"] = req.top_p;
    body["stream"] = req.stream;
    return body.dump();
}

ChatResponse AIGateway::parseResponse(const std::string& raw, LLMProvider provider) {
    ChatResponse resp;
    resp.success = false;

    try {
        json j = json::parse(raw);

        // Check for API error
        if (j.contains("error")) {
            auto err = j["error"];
            resp.error_message = err.value("message", "Unknown API error");
            resp.finish_reason = "error";
            return resp;
        }

        // Claude format
        if (provider == LLMProvider::Claude) {
            if (j.contains("content") && j["content"].is_array()) {
                std::string content;
                for (const auto& block : j["content"]) {
                    if (block.value("type", "") == "text") {
                        content += block.value("text", "");
                    }
                }
                resp.content = content;
                resp.model = j.value("model", "");
                resp.prompt_tokens = j.value("usage", json::object()).value("input_tokens", 0);
                resp.completion_tokens = j.value("usage", json::object()).value("output_tokens", 0);
                resp.total_tokens = resp.prompt_tokens + resp.completion_tokens;
                resp.finish_reason = j.value("stop_reason", "");
                resp.success = !resp.content.empty();
                return resp;
            }
        }

        // OpenAI-compatible format (DeepSeek, GPT-4, Kimi)
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            const auto& choice = j["choices"][0];

            if (choice.contains("message") && choice["message"].contains("content")) {
                resp.content = choice["message"]["content"].get<std::string>();
            }

            // Tool calls
            if (choice["message"].contains("tool_calls")) {
                for (const auto& tc : choice["message"]["tool_calls"]) {
                    Message msg;
                    msg.role = "assistant";
                    msg.name = tc.value("function", json::object()).value("name", "");
                    msg.content = tc.value("function", json::object()).value("arguments", "");
                    resp.tool_calls.push_back(msg);
                }
            }

            resp.finish_reason = choice.value("finish_reason", "");
            resp.model = j.value("model", "");

            if (j.contains("usage")) {
                auto usage = j["usage"];
                resp.prompt_tokens = usage.value("prompt_tokens", 0);
                resp.completion_tokens = usage.value("completion_tokens", 0);
                resp.total_tokens = usage.value("total_tokens", 0);
            }

            resp.success = !resp.content.empty() || !resp.tool_calls.empty();
        }

    } catch (const json::exception& e) {
        resp.error_message = std::string("JSON parse error: ") + e.what();
        resp.content = raw; // Return raw for debugging
    }

    return resp;
}

} // namespace powsy365::ai
