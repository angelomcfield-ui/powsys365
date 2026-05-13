#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>

namespace powsys365::config {

/**
 * @brief AI panel configuration – LLM selection, API keys, RAG.
 *
 * Qt6-compatible: uses standard types, no Qt dependencies in header.
 * JSON serialisation/deserialisation provided.
 */
class AIConfigPanel {
public:
    /**
     * @brief Supported LLM provider.
     */
    enum class LLMProvider {
        OpenAI,
        Anthropic,
        Google,
        Cohere,
        Mistral,
        Local,
        Ollama,
        Custom
    };

    static std::string providerToString(LLMProvider p);
    static LLMProvider stringToProvider(const std::string& s);

    /**
     * @brief RAG configuration.
     */
    struct RAGConfig {
        bool        enabled      = true;
        std::string embeddingModel = "text-embedding-3-small";
        int         chunkSize      = 512;
        int         chunkOverlap   = 128;
        int         topK           = 5;
        double      similarityThreshold = 0.7;
        std::string vectorStorePath    = "./vectors";
        std::string indexType      = "hnsw"; // "hnsw", "flat", "ivf"
    };

    /**
     * @brief Model configuration.
     */
    struct ModelConfig {
        std::string name;
        LLMProvider provider;
        std::string modelId;        // e.g. "gpt-4o", "claude-3-opus"
        std::string apiEndpoint;
        std::string apiKey;         // Stored encrypted at rest
        double      temperature    = 0.7;
        int         maxTokens      = 4096;
        int         contextWindow  = 8192;
        double      topP           = 0.9;
        double      frequencyPenalty = 0.0;
        double      presencePenalty  = 0.0;
        bool        streaming        = true;
    };

    /**
     * @brief Function-call / tool configuration.
     */
    struct ToolConfig {
        bool        enabled = true;
        std::vector<std::string> allowedTools; // e.g. "file_read", "web_search"
        int         maxToolCalls = 10;
        double      toolTimeout  = 30.0; // seconds
    };

    AIConfigPanel();
    ~AIConfigPanel() = default;

    // Non-copyable (contains sensitive data)
    AIConfigPanel(const AIConfigPanel&) = delete;
    AIConfigPanel& operator=(const AIConfigPanel&) = delete;

    // Movable
    AIConfigPanel(AIConfigPanel&&) noexcept;
    AIConfigPanel& operator=(AIConfigPanel&&) noexcept;

    // ----------------------------------------------------------------
    //  LLM Provider
    // ----------------------------------------------------------------

    void setProvider(LLMProvider p);
    LLMProvider provider() const;

    void setModel(const ModelConfig& model);
    ModelConfig model() const;

    std::vector<ModelConfig> availableModels() const;
    void addModel(const ModelConfig& model);
    void removeModel(const std::string& modelId);
    void selectModel(const std::string& modelId);

    // ----------------------------------------------------------------
    //  API Key management (AES-encrypted in memory)
    // ----------------------------------------------------------------

    void setApiKey(const std::string& key);
    bool hasApiKey() const;
    std::string apiKey() const; // Returns decrypted key
    void clearApiKey();

    // ----------------------------------------------------------------
    //  RAG
    // ----------------------------------------------------------------

    void setRAG(const RAGConfig& config);
    RAGConfig rag() const;

    void setEmbeddingModel(const std::string& model);
    std::string embeddingModel() const;

    void setChunkSize(int size);
    int  chunkSize() const;

    void setChunkOverlap(int overlap);
    int  chunkOverlap() const;

    void setTopK(int k);
    int  topK() const;

    void setSimilarityThreshold(double threshold);
    double similarityThreshold() const;

    // ----------------------------------------------------------------
    //  Inference parameters
    // ----------------------------------------------------------------

    void setTemperature(double t);
    double temperature() const;

    void setMaxTokens(int tokens);
    int  maxTokens() const;

    void setContextWindow(int window);
    int  contextWindow() const;

    void setTopP(double p);
    double topP() const;

    void setFrequencyPenalty(double penalty);
    double frequencyPenalty() const;

    void setPresencePenalty(double penalty);
    double presencePenalty() const;

    void setStreaming(bool enabled);
    bool streaming() const;

    // ----------------------------------------------------------------
    //  Tools
    // ----------------------------------------------------------------

    void setToolConfig(const ToolConfig& config);
    ToolConfig toolConfig() const;

    // ----------------------------------------------------------------
    //  Serialisation
    // ----------------------------------------------------------------

    std::string toJSON() const;
    void fromJSON(const std::string& json);

    // ----------------------------------------------------------------
    //  Validation
    // ----------------------------------------------------------------

    std::vector<std::string> validate() const;

    // ----------------------------------------------------------------
    //  Defaults
    // ----------------------------------------------------------------

    void resetToDefaults();

    /**
     * @brief Check if configuration has unsaved changes.
     */
    bool hasChanges() const;
    void markSaved();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace powsys365::config
