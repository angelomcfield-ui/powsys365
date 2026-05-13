#include "ai_config.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace powsys365::config {

/* ================================================================
   Provider string helpers
   ================================================================ */

std::string
AIConfigPanel::providerToString(LLMProvider p)
{
    switch (p) {
        case LLMProvider::OpenAI:   return "openai";
        case LLMProvider::Anthropic:return "anthropic";
        case LLMProvider::Google:   return "google";
        case LLMProvider::Cohere:   return "cohere";
        case LLMProvider::Mistral:  return "mistral";
        case LLMProvider::Local:    return "local";
        case LLMProvider::Ollama:   return "ollama";
        case LLMProvider::Custom:   return "custom";
    }
    return "unknown";
}

AIConfigPanel::LLMProvider
AIConfigPanel::stringToProvider(const std::string& s)
{
    std::string lower;
    for (char c : s) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "openai")    return LLMProvider::OpenAI;
    if (lower == "anthropic") return LLMProvider::Anthropic;
    if (lower == "google")    return LLMProvider::Google;
    if (lower == "cohere")    return LLMProvider::Cohere;
    if (lower == "mistral")   return LLMProvider::Mistral;
    if (lower == "local")     return LLMProvider::Local;
    if (lower == "ollama")    return LLMProvider::Ollama;
    if (lower == "custom")    return LLMProvider::Custom;
    return LLMProvider::OpenAI; // default
}

/* ================================================================
   PIMPL implementation
   ================================================================ */

class AIConfigPanel::Impl {
public:
    LLMProvider provider_ = LLMProvider::OpenAI;
    ModelConfig currentModel_;
    std::vector<ModelConfig> models_;
    RAGConfig rag_;
    ToolConfig tools_;
    bool hasChanges_ = false;

    // Simple XOR-based encryption for API key (obfuscation, not true security)
    std::string encryptedKey_;
    static constexpr uint8_t KEY_XOR = 0xA7;

    Impl() {
        // Default model presets
        models_.push_back({"GPT-4o",       LLMProvider::OpenAI,   "gpt-4o",
                           "https://api.openai.com/v1",       "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"GPT-4o-mini",  LLMProvider::OpenAI,   "gpt-4o-mini",
                           "https://api.openai.com/v1",       "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"Claude 3 Opus",LLMProvider::Anthropic,"claude-3-opus-20240229",
                           "https://api.anthropic.com/v1",    "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"Claude 3 Sonnet",LLMProvider::Anthropic,"claude-3-sonnet-20240229",
                           "https://api.anthropic.com/v1",    "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"Gemini Pro",   LLMProvider::Google,   "gemini-pro",
                           "https://generativelanguage.googleapis.com/v1", "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"Mistral Large",LLMProvider::Mistral,  "mistral-large-latest",
                           "https://api.mistral.ai/v1",       "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"Local Model",  LLMProvider::Local,    "local",
                           "http://localhost:8080/v1",        "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        models_.push_back({"Ollama",       LLMProvider::Ollama,   "llama3",
                           "http://localhost:11434",          "", 0.7, 4096, 8192,
                           0.9, 0.0, 0.0, true});
        currentModel_ = models_[0];
    }

    std::string encryptKey(const std::string& key) {
        std::string result;
        result.reserve(key.size());
        for (size_t i = 0; i < key.size(); ++i) {
            result += static_cast<char>(key[i] ^ (KEY_XOR + static_cast<uint8_t>(i % 256)));
        }
        return result;
    }

    std::string decryptKey(const std::string& encrypted) {
        std::string result;
        result.reserve(encrypted.size());
        for (size_t i = 0; i < encrypted.size(); ++i) {
            result += static_cast<char>(encrypted[i] ^ (KEY_XOR + static_cast<uint8_t>(i % 256)));
        }
        return result;
    }
};

/* ================================================================
   Construction
   ================================================================ */

AIConfigPanel::AIConfigPanel() : pImpl(std::make_unique<Impl>()) {}
AIConfigPanel::AIConfigPanel(AIConfigPanel&&) noexcept = default;
AIConfigPanel& AIConfigPanel::operator=(AIConfigPanel&&) noexcept = default;

/* ================================================================
   Provider
   ================================================================ */

void AIConfigPanel::setProvider(LLMProvider p)     { pImpl->provider_ = p; pImpl->hasChanges_ = true; }
LLMProvider AIConfigPanel::provider() const         { return pImpl->provider_; }

void AIConfigPanel::setModel(const ModelConfig& m)  { pImpl->currentModel_ = m; pImpl->hasChanges_ = true; }
AIConfigPanel::ModelConfig AIConfigPanel::model() const { return pImpl->currentModel_; }

std::vector<AIConfigPanel::ModelConfig>
AIConfigPanel::availableModels() const { return pImpl->models_; }

void AIConfigPanel::addModel(const ModelConfig& model) {
    pImpl->models_.push_back(model);
    pImpl->hasChanges_ = true;
}

void AIConfigPanel::removeModel(const std::string& modelId) {
    pImpl->models_.erase(
        std::remove_if(pImpl->models_.begin(), pImpl->models_.end(),
            [&modelId](const ModelConfig& m) { return m.modelId == modelId; }),
        pImpl->models_.end());
    pImpl->hasChanges_ = true;
}

void AIConfigPanel::selectModel(const std::string& modelId) {
    for (const auto& m : pImpl->models_) {
        if (m.modelId == modelId) {
            pImpl->currentModel_ = m;
            pImpl->hasChanges_ = true;
            break;
        }
    }
}

/* ================================================================
   API Key (encrypted storage)
   ================================================================ */

void AIConfigPanel::setApiKey(const std::string& key) {
    pImpl->encryptedKey_ = pImpl->encryptKey(key);
    pImpl->hasChanges_ = true;
}

bool AIConfigPanel::hasApiKey() const {
    return !pImpl->encryptedKey_.empty();
}

std::string AIConfigPanel::apiKey() const {
    if (pImpl->encryptedKey_.empty()) return "";
    return pImpl->decryptKey(pImpl->encryptedKey_);
}

void AIConfigPanel::clearApiKey() {
    pImpl->encryptedKey_.clear();
    pImpl->hasChanges_ = true;
}

/* ================================================================
   RAG
   ================================================================ */

void AIConfigPanel::setRAG(const RAGConfig& config) { pImpl->rag_ = config; pImpl->hasChanges_ = true; }
AIConfigPanel::RAGConfig AIConfigPanel::rag() const { return pImpl->rag_; }

void AIConfigPanel::setEmbeddingModel(const std::string& m) { pImpl->rag_.embeddingModel = m; pImpl->hasChanges_ = true; }
std::string AIConfigPanel::embeddingModel() const { return pImpl->rag_.embeddingModel; }

void AIConfigPanel::setChunkSize(int s)     { pImpl->rag_.chunkSize = s; pImpl->hasChanges_ = true; }
int  AIConfigPanel::chunkSize() const       { return pImpl->rag_.chunkSize; }

void AIConfigPanel::setChunkOverlap(int o)  { pImpl->rag_.chunkOverlap = o; pImpl->hasChanges_ = true; }
int  AIConfigPanel::chunkOverlap() const    { return pImpl->rag_.chunkOverlap; }

void AIConfigPanel::setTopK(int k)          { pImpl->rag_.topK = k; pImpl->hasChanges_ = true; }
int  AIConfigPanel::topK() const            { return pImpl->rag_.topK; }

void AIConfigPanel::setSimilarityThreshold(double t) { pImpl->rag_.similarityThreshold = t; pImpl->hasChanges_ = true; }
double AIConfigPanel::similarityThreshold() const { return pImpl->rag_.similarityThreshold; }

/* ================================================================
   Inference parameters
   ================================================================ */

void AIConfigPanel::setTemperature(double t) { pImpl->currentModel_.temperature = t; pImpl->hasChanges_ = true; }
double AIConfigPanel::temperature() const    { return pImpl->currentModel_.temperature; }

void AIConfigPanel::setMaxTokens(int t)      { pImpl->currentModel_.maxTokens = t; pImpl->hasChanges_ = true; }
int  AIConfigPanel::maxTokens() const        { return pImpl->currentModel_.maxTokens; }

void AIConfigPanel::setContextWindow(int w)  { pImpl->currentModel_.contextWindow = w; pImpl->hasChanges_ = true; }
int  AIConfigPanel::contextWindow() const    { return pImpl->currentModel_.contextWindow; }

void AIConfigPanel::setTopP(double p)        { pImpl->currentModel_.topP = p; pImpl->hasChanges_ = true; }
double AIConfigPanel::topP() const           { return pImpl->currentModel_.topP; }

void AIConfigPanel::setFrequencyPenalty(double p) { pImpl->currentModel_.frequencyPenalty = p; pImpl->hasChanges_ = true; }
double AIConfigPanel::frequencyPenalty() const   { return pImpl->currentModel_.frequencyPenalty; }

void AIConfigPanel::setPresencePenalty(double p) { pImpl->currentModel_.presencePenalty = p; pImpl->hasChanges_ = true; }
double AIConfigPanel::presencePenalty() const    { return pImpl->currentModel_.presencePenalty; }

void AIConfigPanel::setStreaming(bool s)     { pImpl->currentModel_.streaming = s; pImpl->hasChanges_ = true; }
bool AIConfigPanel::streaming() const          { return pImpl->currentModel_.streaming; }

/* ================================================================
   Tools
   ================================================================ */

void AIConfigPanel::setToolConfig(const ToolConfig& config) { pImpl->tools_ = config; pImpl->hasChanges_ = true; }
AIConfigPanel::ToolConfig AIConfigPanel::toolConfig() const { return pImpl->tools_; }

/* ================================================================
   JSON Serialisation (lightweight manual JSON)
   ================================================================ */

namespace {
    std::string jsonEsc(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) {
                case '"': r += "\\\""; break;
                case '\\': r += "\\\\"; break;
                case '\n': r += "\\n"; break;
                case '\r': r += "\\r"; break;
                case '\t': r += "\\t"; break;
                default: r += c; break;
            }
        }
        return r;
    }
}

std::string
AIConfigPanel::toJSON() const
{
    std::ostringstream oss;
    oss << "{";
    oss << "\\"provider\\":\\"" << providerToString(pImpl->provider_) << "\\",";
    oss << "\\"model\\":{";
    oss << "\\"name\\":\\"" << jsonEsc(pImpl->currentModel_.name) << "\\",";
    oss << "\\"provider\\":\\"" << providerToString(pImpl->currentModel_.provider) << "\\",";
    oss << "\\"modelId\\":\\"" << jsonEsc(pImpl->currentModel_.modelId) << "\\",";
    oss << "\\"apiEndpoint\\":\\"" << jsonEsc(pImpl->currentModel_.apiEndpoint) << "\\",";
    oss << "\\"temperature\\":" << pImpl->currentModel_.temperature << ",";
    oss << "\\"maxTokens\\":" << pImpl->currentModel_.maxTokens << ",";
    oss << "\\"contextWindow\\":" << pImpl->currentModel_.contextWindow << ",";
    oss << "\\"topP\\":" << pImpl->currentModel_.topP << ",";
    oss << "\\"frequencyPenalty\\":" << pImpl->currentModel_.frequencyPenalty << ",";
    oss << "\\"presencePenalty\\":" << pImpl->currentModel_.presencePenalty << ",";
    oss << "\\"streaming\\":" << (pImpl->currentModel_.streaming ? "true" : "false");
    oss << "},";

    // Available models
    oss << "\\"availableModels\\":[";
    for (size_t i = 0; i < pImpl->models_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\\"name\\":\\"" << jsonEsc(pImpl->models_[i].name) << "\\",";
        oss << "\\"modelId\\":\\"" << jsonEsc(pImpl->models_[i].modelId) << "\\",";
        oss << "\\"provider\\":\\"" << providerToString(pImpl->models_[i].provider) << "\\"}";
    }
    oss << "],";

    // RAG
    oss << "\\"rag\\":{";
    oss << "\\"enabled\\":" << (pImpl->rag_.enabled ? "true" : "false") << ",";
    oss << "\\"embeddingModel\\":\\"" << jsonEsc(pImpl->rag_.embeddingModel) << "\\",";
    oss << "\\"chunkSize\\":" << pImpl->rag_.chunkSize << ",";
    oss << "\\"chunkOverlap\\":" << pImpl->rag_.chunkOverlap << ",";
    oss << "\\"topK\\":" << pImpl->rag_.topK << ",";
    oss << "\\"similarityThreshold\\":" << pImpl->rag_.similarityThreshold << ",";
    oss << "\\"vectorStorePath\\":\\"" << jsonEsc(pImpl->rag_.vectorStorePath) << "\\",";
    oss << "\\"indexType\\":\\"" << jsonEsc(pImpl->rag_.indexType) << "\\"";
    oss << "},";

    // Tools
    oss << "\\"tools\\":{";
    oss << "\\"enabled\\":" << (pImpl->tools_.enabled ? "true" : "false") << ",";
    oss << "\\"maxToolCalls\\":" << pImpl->tools_.maxToolCalls << ",";
    oss << "\\"toolTimeout\\":" << pImpl->tools_.toolTimeout << ",";
    oss << "\\"allowedTools\\":[";
    for (size_t i = 0; i < pImpl->tools_.allowedTools.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\\"" << jsonEsc(pImpl->tools_.allowedTools[i]) << "\\"";
    }
    oss << "]}";

    oss << "}";
    return oss.str();
}

void
AIConfigPanel::fromJSON(const std::string& json)
{
    // Parse key fields from JSON string
    auto extractString = [&](const std::string& json, const std::string& key) -> std::string {
        size_t pos = json.find("\\"" + key + "\\":");
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos + key.length() + 3);
        if (pos == std::string::npos) return "";
        ++pos;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };
    auto extractBool = [&](const std::string& json, const std::string& key) -> bool {
        size_t pos = json.find("\\"" + key + "\\":");
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos + key.length() + 3);
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && json[pos] == ' ') ++pos;
        return json.substr(pos, 4) == "true";
    };
    auto extractDouble = [&](const std::string& json, const std::string& key) -> double {
        size_t pos = json.find("\\"" + key + "\\":");
        if (pos == std::string::npos) return 0.0;
        pos = json.find(':', pos + key.length() + 3);
        if (pos == std::string::npos) return 0.0;
        ++pos;
        while (pos < json.size() && json[pos] == ' ') ++pos;
        return std::strtod(json.c_str() + pos, nullptr);
    };
    auto extractInt = [&](const std::string& json, const std::string& key) -> int {
        return static_cast<int>(extractDouble(json, key));
    };

    // Provider
    std::string prov = extractString(json, "provider");
    if (!prov.empty()) pImpl->provider_ = stringToProvider(prov);

    // Model
    pImpl->currentModel_.name         = extractString(json, "name");
    pImpl->currentModel_.provider     = stringToProvider(extractString(json, "provider"));
    pImpl->currentModel_.modelId      = extractString(json, "modelId");
    pImpl->currentModel_.apiEndpoint  = extractString(json, "apiEndpoint");
    pImpl->currentModel_.temperature  = extractDouble(json, "temperature");
    if (pImpl->currentModel_.temperature < 0.01) pImpl->currentModel_.temperature = 0.7;
    pImpl->currentModel_.maxTokens    = extractInt(json, "maxTokens");
    if (pImpl->currentModel_.maxTokens < 1) pImpl->currentModel_.maxTokens = 4096;
    pImpl->currentModel_.contextWindow= extractInt(json, "contextWindow");
    if (pImpl->currentModel_.contextWindow < 1) pImpl->currentModel_.contextWindow = 8192;
    pImpl->currentModel_.topP         = extractDouble(json, "topP");
    pImpl->currentModel_.frequencyPenalty = extractDouble(json, "frequencyPenalty");
    pImpl->currentModel_.presencePenalty  = extractDouble(json, "presencePenalty");
    pImpl->currentModel_.streaming    = extractBool(json, "streaming");

    // RAG
    pImpl->rag_.enabled               = extractBool(json, "enabled");
    pImpl->rag_.embeddingModel        = extractString(json, "embeddingModel");
    if (pImpl->rag_.embeddingModel.empty()) pImpl->rag_.embeddingModel = "text-embedding-3-small";
    pImpl->rag_.chunkSize             = extractInt(json, "chunkSize");
    if (pImpl->rag_.chunkSize < 1) pImpl->rag_.chunkSize = 512;
    pImpl->rag_.chunkOverlap          = extractInt(json, "chunkOverlap");
    if (pImpl->rag_.chunkOverlap < 1) pImpl->rag_.chunkOverlap = 128;
    pImpl->rag_.topK                  = extractInt(json, "topK");
    if (pImpl->rag_.topK < 1) pImpl->rag_.topK = 5;
    pImpl->rag_.similarityThreshold   = extractDouble(json, "similarityThreshold");
    if (pImpl->rag_.similarityThreshold < 0.01) pImpl->rag_.similarityThreshold = 0.7;
    pImpl->rag_.vectorStorePath       = extractString(json, "vectorStorePath");
    if (pImpl->rag_.vectorStorePath.empty()) pImpl->rag_.vectorStorePath = "./vectors";
    pImpl->rag_.indexType             = extractString(json, "indexType");
    if (pImpl->rag_.indexType.empty()) pImpl->rag_.indexType = "hnsw";

    // Tools
    pImpl->tools_.enabled             = extractBool(json, "enabled");
    pImpl->tools_.maxToolCalls        = extractInt(json, "maxToolCalls");
    if (pImpl->tools_.maxToolCalls < 1) pImpl->tools_.maxToolCalls = 10;
    pImpl->tools_.toolTimeout         = extractDouble(json, "toolTimeout");
    if (pImpl->tools_.toolTimeout < 0.1) pImpl->tools_.toolTimeout = 30.0;

    pImpl->hasChanges_ = false;
}

/* ================================================================
   Validation
   ================================================================ */

std::vector<std::string>
AIConfigPanel::validate() const
{
    std::vector<std::string> errors;
    if (pImpl->currentModel_.temperature < 0.0 || pImpl->currentModel_.temperature > 2.0) {
        errors.push_back("Temperature must be between 0.0 and 2.0");
    }
    if (pImpl->currentModel_.maxTokens < 1 || pImpl->currentModel_.maxTokens > 100000) {
        errors.push_back("maxTokens must be between 1 and 100000");
    }
    if (pImpl->currentModel_.contextWindow < 256 || pImpl->currentModel_.contextWindow > 200000) {
        errors.push_back("contextWindow must be between 256 and 200000");
    }
    if (pImpl->rag_.chunkSize < 1 || pImpl->rag_.chunkSize > 8192) {
        errors.push_back("chunkSize must be between 1 and 8192");
    }
    if (pImpl->rag_.similarityThreshold < 0.0 || pImpl->rag_.similarityThreshold > 1.0) {
        errors.push_back("similarityThreshold must be between 0.0 and 1.0");
    }
    return errors;
}

/* ================================================================
   Defaults
   ================================================================ */

void
AIConfigPanel::resetToDefaults()
{
    pImpl = std::make_unique<Impl>();
}

bool AIConfigPanel::hasChanges() const { return pImpl->hasChanges_; }
void AIConfigPanel::markSaved()        { pImpl->hasChanges_ = false; }

} // namespace powsys365::config
