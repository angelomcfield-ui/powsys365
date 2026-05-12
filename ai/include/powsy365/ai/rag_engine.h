/**
 * @file rag_engine.h
 * @brief Retrieval-Augmented Generation (RAG) Engine for POWSYS365.
 *
 * Provides semantic search over technical documentation using
 * vector embeddings and optional pgvector (PostgreSQL) storage.
 *
 * Pipeline:
 *   1. Index documents (chunking + embedding)
 *   2. Store vectors (in-memory or pgvector)
 *   3. Retrieve top-k relevant chunks for a query
 *   4. Augment LLM context with retrieved chunks
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace powsy365::ai {

/* ------------------------------------------------------------------ */
/*  Document chunk                                                     */
/* ------------------------------------------------------------------ */

struct DocumentChunk {
    std::string id;              // Unique chunk ID
    std::string source;          // Source document path/name
    std::string content;         // Text content
    std::string title;           // Document title
    int chunk_index = 0;         // Position within document
    int total_chunks = 1;        // Total chunks for this doc
    std::vector<float> embedding; // Dense embedding vector
    std::map<std::string, std::string> metadata; // Extra key-value pairs
};

/* ------------------------------------------------------------------ */
/*  Search result                                                      */
/* ------------------------------------------------------------------ */

struct SearchResult {
    DocumentChunk chunk;
    float similarity = 0.0f;     // Cosine similarity score
    int rank = 0;                // Result rank (1-based)
};

/* ------------------------------------------------------------------ */
/*  Embedding provider interface                                       */
/* ------------------------------------------------------------------ */

class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;

    /**
     * Compute embeddings for a batch of text strings.
     *
     * @param texts List of input texts.
     * @return Vector of embedding vectors (one per input text).
     */
    virtual std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts
    ) = 0;

    /**
     * @return Dimensionality of the embedding vectors.
     */
    virtual int dimension() const = 0;

    /**
     * @return Provider name.
     */
    virtual std::string name() const = 0;
};

/* ------------------------------------------------------------------ */
/*  Vector store interface                                             */
/* ------------------------------------------------------------------ */

class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    /**
     * Store a collection of document chunks with their embeddings.
     */
    virtual void store(const std::vector<DocumentChunk>& chunks) = 0;

    /**
     * Search for the top-k most similar chunks to the query embedding.
     */
    virtual std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 5
    ) = 0;

    /**
     * Delete all chunks from a specific source document.
     */
    virtual void deleteBySource(const std::string& source) = 0;

    /**
     * Clear all stored vectors.
     */
    virtual void clear() = 0;

    /**
     * @return Number of stored chunks.
     */
    virtual size_t size() const = 0;
};

/* ------------------------------------------------------------------ */
/*  In-memory vector store (cosine similarity)                         */
/* ------------------------------------------------------------------ */

class InMemoryVectorStore : public IVectorStore {
public:
    explicit InMemoryVectorStore(int embedding_dim = 1536);

    void store(const std::vector<DocumentChunk>& chunks) override;
    std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 5
    ) override;
    void deleteBySource(const std::string& source) override;
    void clear() override;
    size_t size() const override;

private:
    std::vector<DocumentChunk> chunks_;
    int embedding_dim_;
    mutable std::mutex mutex_;

    float cosineSimilarity(
        const std::vector<float>& a,
        const std::vector<float>& b
    ) const;
};

/* ------------------------------------------------------------------ */
/*  pgvector store (PostgreSQL)                                        */
/* ------------------------------------------------------------------ */

class PgVectorStore : public IVectorStore {
public:
    struct ConnectionConfig {
        std::string host = "localhost";
        int port = 5432;
        std::string database = "powsy365";
        std::string user = "powsy365";
        std::string password;
        std::string table = "document_chunks";
    };

    explicit PgVectorStore(const ConnectionConfig& config);
    ~PgVectorStore();

    bool connect();
    bool isConnected() const;
    void initializeSchema();

    void store(const std::vector<DocumentChunk>& chunks) override;
    std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 5
    ) override;
    void deleteBySource(const std::string& source) override;
    void clear() override;
    size_t size() const override;

private:
    ConnectionConfig config_;
    void* conn_ = nullptr;  // PGconn* (opaque to avoid pg dependency in header)
    bool connected_ = false;

    bool execute(const std::string& sql);
    std::string embeddingToPgVector(const std::vector<float>& embedding) const;
};

/* ------------------------------------------------------------------ */
/*  Text splitter                                                      */
/* ------------------------------------------------------------------ */

class TextSplitter {
public:
    struct Config {
        int chunk_size = 512;       // Target chunk size in tokens/characters
        int chunk_overlap = 50;     // Overlap between consecutive chunks
        std::string separator = "\n\n"; // Primary separator
    };

    explicit TextSplitter(const Config& config = {});

    /**
     * Split text into overlapping chunks.
     */
    std::vector<std::string> split(const std::string& text) const;

    /**
     * Split with source tracking.
     */
    std::vector<DocumentChunk> splitDocument(
        const std::string& source,
        const std::string& text,
        const std::string& title = ""
    ) const;

private:
    Config config_;
};

/* ------------------------------------------------------------------ */
/*  RAG Engine                                                         */
/* ------------------------------------------------------------------ */

class RAGEngine {
public:
    /**
     * Create a RAG engine with the specified embedding provider
     * and vector store.
     *
     * @param embedder Provider for computing text embeddings.
     * @param store Storage backend for vector search.
     */
    RAGEngine(
        std::shared_ptr<IEmbeddingProvider> embedder,
        std::shared_ptr<IVectorStore> store
    );

    // -- Document indexing --

    /**
     * Index a document from text content.
     *
     * @param source Document identifier (filename, URL, etc.).
     * @param text Full document text.
     * @param title Optional document title.
     * @param metadata Optional key-value metadata.
     */
    void indexDocument(
        const std::string& source,
        const std::string& text,
        const std::string& title = "",
        const std::map<std::string, std::string>& metadata = {}
    );

    /**
     * Index multiple documents at once (batch processing).
     */
    void indexDocuments(
        const std::vector<std::tuple<std::string, std::string, std::string>>&
            docs  // (source, text, title)
    );

    /**
     * Load and index a text file.
     */
    void indexFile(
        const std::string& filepath,
        const std::string& title = ""
    );

    // -- Querying --

    /**
     * Retrieve the top-k most relevant chunks for a query.
     *
     * @param query User query string.
     * @param top_k Number of results to return.
     * @return Ranked list of search results.
     */
    std::vector<SearchResult> retrieve(
        const std::string& query,
        int top_k = 5
    );

    /**
     * Retrieve and format as an augmented context string.
     *
     * @param query User query string.
     * @param top_k Number of chunks to include.
     * @return Formatted context string ready for LLM prompt injection.
     */
    std::string retrieveContext(
        const std::string& query,
        int top_k = 5
    );

    // -- Management --

    /**
     * Remove all chunks from a source document.
     */
    void removeDocument(const std::string& source);

    /**
     * Clear the entire index.
     */
    void clearIndex();

    /**
     * @return Number of indexed chunks.
     */
    size_t indexSize() const;

    // -- Configuration --

    void setSplitterConfig(const TextSplitter::Config& config);
    TextSplitter::Config getSplitterConfig() const;

private:
    std::shared_ptr<IEmbeddingProvider> embedder_;
    std::shared_ptr<IVectorStore> store_;
    TextSplitter splitter_;
};

} // namespace powsy365::ai
