/**
 * @file rag_engine.cpp
 * @brief Implementation of the RAG Engine for POWSYS365.
 */

#include "powsy365/ai/rag_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>

// For pgvector, we include libpq here in the .cpp
#ifdef POWSY365_HAS_PGVECTOR
#include <libpq-fe.h>
#endif

namespace powsy365::ai {

/* ------------------------------------------------------------------ */
/*  InMemoryVectorStore                                                */
/* ------------------------------------------------------------------ */

InMemoryVectorStore::InMemoryVectorStore(int embedding_dim)
    : embedding_dim_(embedding_dim) {}

void InMemoryVectorStore::store(const std::vector<DocumentChunk>& chunks) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& chunk : chunks) {
        if (static_cast<int>(chunk.embedding.size()) != embedding_dim_) {
            throw std::invalid_argument(
                "Embedding dimension mismatch: expected " +
                std::to_string(embedding_dim_) + ", got " +
                std::to_string(chunk.embedding.size())
            );
        }
        chunks_.push_back(chunk);
    }
}

std::vector<SearchResult> InMemoryVectorStore::search(
    const std::vector<float>& query_embedding,
    int top_k
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (static_cast<int>(query_embedding.size()) != embedding_dim_) {
        throw std::invalid_argument("Query embedding dimension mismatch");
    }

    // Compute cosine similarity for all chunks
    std::vector<std::pair<float, size_t>> scores;
    scores.reserve(chunks_.size());

    for (size_t i = 0; i < chunks_.size(); ++i) {
        float sim = cosineSimilarity(query_embedding, chunks_[i].embedding);
        scores.emplace_back(sim, i);
    }

    // Partial sort for top-k
    auto cmp = [](const auto& a, const auto& b) { return a.first > b.first; };
    if (top_k < static_cast<int>(scores.size())) {
        std::partial_sort(
            scores.begin(),
            scores.begin() + top_k,
            scores.end(),
            cmp
        );
        scores.resize(top_k);
    } else {
        std::sort(scores.begin(), scores.end(), cmp);
    }

    std::vector<SearchResult> results;
    results.reserve(scores.size());
    int rank = 1;
    for (const auto& [sim, idx] : scores) {
        results.push_back({chunks_[idx], sim, rank++});
    }
    return results;
}

void InMemoryVectorStore::deleteBySource(const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.erase(
        std::remove_if(
            chunks_.begin(), chunks_.end(),
            [&source](const DocumentChunk& c) { return c.source == source; }
        ),
        chunks_.end()
    );
}

void InMemoryVectorStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
}

size_t InMemoryVectorStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

float InMemoryVectorStore::cosineSimilarity(
    const std::vector<float>& a,
    const std::vector<float>& b
) const {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

/* ------------------------------------------------------------------ */
/*  PgVectorStore                                                      */
/* ------------------------------------------------------------------ */

PgVectorStore::PgVectorStore(const ConnectionConfig& config)
    : config_(config) {}

PgVectorStore::~PgVectorStore() {
#ifdef POWSY365_HAS_PGVECTOR
    if (conn_) {
        PQfinish(static_cast<PGconn*>(conn_));
    }
#endif
}

bool PgVectorStore::connect() {
#ifdef POWSY365_HAS_PGVECTOR
    std::string conninfo =
        "host=" + config_.host +
        " port=" + std::to_string(config_.port) +
        " dbname=" + config_.database +
        " user=" + config_.user +
        " password=" + config_.password;

    conn_ = PQconnectdb(conninfo.c_str());
    connected_ = PQstatus(static_cast<PGconn*>(conn_)) == CONNECTION_OK;

    if (connected_) {
        // Enable pgvector extension
        execute("CREATE EXTENSION IF NOT EXISTS vector");
    }
    return connected_;
#else
    throw std::runtime_error(
        "POWSYS365 was compiled without pgvector support. "
        "Rebuild with -DPOWSY365_HAS_PGVECTOR=ON and libpq-dev installed."
    );
#endif
}

bool PgVectorStore::isConnected() const {
    return connected_;
}

void PgVectorStore::initializeSchema() {
#ifdef POWSY365_HAS_PGVECTOR
    if (!connected_) {
        throw std::runtime_error("Not connected to PostgreSQL");
    }

    std::string sql = R"sql(
        CREATE TABLE IF NOT EXISTS )sql" + config_.table + R"sql(
        (
            id TEXT PRIMARY KEY,
            source TEXT NOT NULL,
            content TEXT NOT NULL,
            title TEXT,
            chunk_index INTEGER DEFAULT 0,
            total_chunks INTEGER DEFAULT 1,
            embedding VECTOR(1536),
            metadata JSONB DEFAULT '{}',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_ )sql" + config_.table +
        R"sql(_source ON )sql" + config_.table + R"sql( (source);
        CREATE INDEX IF NOT EXISTS idx_ )sql" + config_.table +
        R"sql(_embedding ON )sql" + config_.table +
        R"sql( USING ivfflat (embedding vector_cosine_ops);
    )sql";

    execute(sql);
#endif
}

void PgVectorStore::store(const std::vector<DocumentChunk>& chunks) {
#ifdef POWSY365_HAS_PGVECTOR
    if (!connected_) {
        throw std::runtime_error("Not connected to PostgreSQL");
    }

    for (const auto& chunk : chunks) {
        std::string sql =
            "INSERT INTO " + config_.table +
            " (id, source, content, title, chunk_index, total_chunks, embedding) " +
            "VALUES ('" + chunk.id + "', '" + chunk.source + "', " +
            "'" + chunk.content + "', '" + chunk.title + "', " +
            std::to_string(chunk.chunk_index) + ", " +
            std::to_string(chunk.total_chunks) + ", " +
            embeddingToPgVector(chunk.embedding) + ") " +
            "ON CONFLICT (id) DO UPDATE SET " +
            "content = EXCLUDED.content, " +
            "embedding = EXCLUDED.embedding;";

        execute(sql);
    }
#endif
}

std::vector<SearchResult> PgVectorStore::search(
    const std::vector<float>& query_embedding,
    int top_k
) {
    std::vector<SearchResult> results;

#ifdef POWSY365_HAS_PGVECTOR
    if (!connected_) {
        throw std::runtime_error("Not connected to PostgreSQL");
    }

    std::string sql =
        "SELECT id, source, content, title, chunk_index, total_chunks, " +
        std::string("1 - (embedding <=> ") +
        embeddingToPgVector(query_embedding) +
        ") AS similarity " +
        "FROM " + config_.table + " " +
        "ORDER BY embedding <=> " + embeddingToPgVector(query_embedding) + " " +
        "LIMIT " + std::to_string(top_k) + ";";

    PGresult* res = PQexec(static_cast<PGconn*>(conn_), sql.c_str());
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            SearchResult sr;
            sr.chunk.id = PQgetvalue(res, i, 0);
            sr.chunk.source = PQgetvalue(res, i, 1);
            sr.chunk.content = PQgetvalue(res, i, 2);
            sr.chunk.title = PQgetvalue(res, i, 3);
            sr.chunk.chunk_index = std::stoi(PQgetvalue(res, i, 4));
            sr.chunk.total_chunks = std::stoi(PQgetvalue(res, i, 5));
            sr.similarity = std::stof(PQgetvalue(res, i, 6));
            sr.rank = i + 1;
            results.push_back(sr);
        }
    }
    PQclear(res);
#endif

    return results;
}

void PgVectorStore::deleteBySource(const std::string& source) {
#ifdef POWSY365_HAS_PGVECTOR
    if (!connected_) return;
    std::string sql =
        "DELETE FROM " + config_.table +
        " WHERE source = '" + source + "';";
    execute(sql);
#endif
}

void PgVectorStore::clear() {
#ifdef POWSY365_HAS_PGVECTOR
    if (!connected_) return;
    execute("TRUNCATE TABLE " + config_.table + ";");
#endif
}

size_t PgVectorStore::size() const {
#ifdef POWSY365_HAS_PGVECTOR
    if (!connected_) return 0;
    PGresult* res = PQexec(
        static_cast<PGconn*>(conn_),
        ("SELECT COUNT(*) FROM " + config_.table + ";").c_str()
    );
    size_t count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        count = std::stoull(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return count;
#else
    return 0;
#endif
}

bool PgVectorStore::execute(const std::string& sql) {
#ifdef POWSY365_HAS_PGVECTOR
    if (!conn_) return false;
    PGresult* res = PQexec(static_cast<PGconn*>(conn_), sql.c_str());
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK ||
              PQresultStatus(res) == PGRES_TUPLES_OK;
    if (!ok) {
        std::cerr << "PostgreSQL error: " << PQerrorMessage(static_cast<PGconn*>(conn_)) << "\n";
    }
    PQclear(res);
    return ok;
#else
    (void)sql;
    return false;
#endif
}

std::string PgVectorStore::embeddingToPgVector(
    const std::vector<float>& embedding
) const {
    std::ostringstream oss;
    oss << "'[";
    for (size_t i = 0; i < embedding.size(); ++i) {
        if (i > 0) oss << ",";
        oss << embedding[i];
    }
    oss << "]'::vector";
    return oss.str();
}

/* ------------------------------------------------------------------ */
/*  TextSplitter                                                       */
/* ------------------------------------------------------------------ */

TextSplitter::TextSplitter(const Config& config) : config_(config) {}

std::vector<std::string> TextSplitter::split(const std::string& text) const {
    std::vector<std::string> chunks;

    if (text.empty()) return chunks;
    if (text.length() <= static_cast<size_t>(config_.chunk_size)) {
        chunks.push_back(text);
        return chunks;
    }

    // Try splitting by separator first
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < text.length()) {
        size_t sep_pos = text.find(config_.separator, pos);
        if (sep_pos == std::string::npos) {
            parts.push_back(text.substr(pos));
            break;
        }
        parts.push_back(text.substr(pos, sep_pos - pos));
        pos = sep_pos + config_.separator.length();
    }

    // Merge parts into chunks of target size
    std::string current;
    for (const auto& part : parts) {
        if (current.length() + part.length() + config_.separator.length()
            > static_cast<size_t>(config_.chunk_size)
            && !current.empty()) {
            chunks.push_back(current);
            // Apply overlap
            if (config_.chunk_overlap > 0 && current.length()
                > static_cast<size_t>(config_.chunk_overlap)) {
                current = current.substr(current.length() - config_.chunk_overlap);
            } else {
                current.clear();
            }
        }
        if (!current.empty()) current += config_.separator;
        current += part;
    }
    if (!current.empty()) {
        chunks.push_back(current);
    }

    // Fallback: character-level splitting if still too large
    std::vector<std::string> final_chunks;
    for (const auto& chunk : chunks) {
        if (chunk.length() <= static_cast<size_t>(config_.chunk_size)) {
            final_chunks.push_back(chunk);
        } else {
            size_t start = 0;
            while (start < chunk.length()) {
                size_t len = std::min(
                    static_cast<size_t>(config_.chunk_size),
                    chunk.length() - start
                );
                final_chunks.push_back(chunk.substr(start, len));
                start += len - config_.chunk_overlap;
                if (len < static_cast<size_t>(config_.chunk_size)) break;
            }
        }
    }

    return final_chunks;
}

std::vector<DocumentChunk> TextSplitter::splitDocument(
    const std::string& source,
    const std::string& text,
    const std::string& title
) const {
    auto texts = split(text);
    std::vector<DocumentChunk> chunks;
    chunks.reserve(texts.size());

    for (size_t i = 0; i < texts.size(); ++i) {
        DocumentChunk chunk;
        chunk.id = source + "_" + std::to_string(i);
        chunk.source = source;
        chunk.content = texts[i];
        chunk.title = title;
        chunk.chunk_index = static_cast<int>(i);
        chunk.total_chunks = static_cast<int>(texts.size());
        chunks.push_back(chunk);
    }

    return chunks;
}

/* ------------------------------------------------------------------ */
/*  RAGEngine                                                          */
/* ------------------------------------------------------------------ */

RAGEngine::RAGEngine(
    std::shared_ptr<IEmbeddingProvider> embedder,
    std::shared_ptr<IVectorStore> store
) : embedder_(std::move(embedder)),
    store_(std::move(store)),
    splitter_() {}

void RAGEngine::indexDocument(
    const std::string& source,
    const std::string& text,
    const std::string& title,
    const std::map<std::string, std::string>& metadata
) {
    // 1. Split into chunks
    auto chunks = splitter_.splitDocument(source, text, title);

    // Attach metadata
    for (auto& chunk : chunks) {
        chunk.metadata = metadata;
    }

    // 2. Compute embeddings in batch
    std::vector<std::string> texts;
    texts.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        texts.push_back(chunk.content);
    }

    auto embeddings = embedder_->embed(texts);
    if (embeddings.size() != chunks.size()) {
        throw std::runtime_error(
            "Embedding batch size mismatch: got " +
            std::to_string(embeddings.size()) + ", expected " +
            std::to_string(chunks.size())
        );
    }

    for (size_t i = 0; i < chunks.size(); ++i) {
        chunks[i].embedding = std::move(embeddings[i]);
    }

    // 3. Store in vector DB
    store_->store(chunks);
}

void RAGEngine::indexDocuments(
    const std::vector<std::tuple<std::string, std::string, std::string>>& docs
) {
    for (const auto& [source, text, title] : docs) {
        indexDocument(source, text, title);
    }
}

void RAGEngine::indexFile(const std::string& filepath, const std::string& title) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    std::string text(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    file.close();

    std::string doc_title = title.empty() ? filepath : title;
    indexDocument(filepath, text, doc_title);
}

std::vector<SearchResult> RAGEngine::retrieve(
    const std::string& query,
    int top_k
) {
    // Embed the query
    auto query_embeddings = embedder_->embed({query});
    if (query_embeddings.empty()) {
        return {};
    }

    // Search vector store
    return store_->search(query_embeddings[0], top_k);
}

std::string RAGEngine::retrieveContext(const std::string& query, int top_k) {
    auto results = retrieve(query, top_k);
    if (results.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "=== Relevant Technical Documentation ===\n\n";
    for (const auto& result : results) {
        oss << "--- Source: " << result.chunk.source;
        if (!result.chunk.title.empty()) {
            oss << " (" << result.chunk.title << ")";
        }
        oss << " [similarity: " << std::fixed << std::setprecision(3)
            << result.similarity << "] ---\n";
        oss << result.chunk.content << "\n\n";
    }

    return oss.str();
}

void RAGEngine::removeDocument(const std::string& source) {
    store_->deleteBySource(source);
}

void RAGEngine::clearIndex() {
    store_->clear();
}

size_t RAGEngine::indexSize() const {
    return store_->size();
}

void RAGEngine::setSplitterConfig(const TextSplitter::Config& config) {
    splitter_ = TextSplitter(config);
}

TextSplitter::Config RAGEngine::getSplitterConfig() const {
    return {splitter_.split(""), 0}; // Cannot directly expose config; would need accessor
}

} // namespace powsy365::ai
