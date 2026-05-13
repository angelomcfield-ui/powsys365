#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <memory>

namespace powsys365::help {

/**
 * @brief Search result from help engine query
 */
struct SearchResult {
    QString documentId;
    QString title;
    QString content;
    QString section;
    QString category;
    float relevance = 0.0f;
    int matchCount = 0;
    QDateTime lastModified;
    QJsonObject metadata;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["documentId"] = documentId;
        obj["title"] = title;
        obj["content"] = content;
        obj["section"] = section;
        obj["category"] = category;
        obj["relevance"] = relevance;
        obj["matchCount"] = matchCount;
        obj["lastModified"] = lastModified.toString(Qt::ISODate);
        obj["metadata"] = metadata;
        return obj;
    }
};

/**
 * @brief Document to be indexed by the help engine
 */
struct HelpDocument {
    QString id;
    QString title;
    QString content;
    QString section;     // "user_guide", "api_reference", "tutorials"
    QString category;
    QStringList tags;
    QDateTime lastModified;
    QJsonObject metadata;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["title"] = title;
        obj["content"] = content;
        obj["section"] = section;
        obj["category"] = category;
        obj["tags"] = QJsonArray::fromStringList(tags);
        obj["lastModified"] = lastModified.toString(Qt::ISODate);
        obj["metadata"] = metadata;
        return obj;
    }
};

/**
 * @brief Search engine backend type
 */
enum class SearchBackend {
    MeiliSearch,
    LuceneFTS5,
    InMemory,
    Hybrid
};

/**
 * @brief Help search engine with MeiliSearch + Lucene FTS5 integration
 *
 * Provides full-text search across documentation with:
 * - Document indexing with relevance scoring
 * - Multi-field search (title, content, tags)
 * - Fuzzy matching support
 * - Faceted search by section/category
 * - MeiliSearch remote server support
 * - Lucene FTS5 SQLite extension support
 * - In-memory fallback indexing
 */
class HelpEngine : public QObject {
    Q_OBJECT

public:
    explicit HelpEngine(QObject* parent = nullptr);
    ~HelpEngine();

    // === Engine Configuration ===
    bool initialize(SearchBackend backend = SearchBackend::Hybrid);
    void shutdown();
    bool isInitialized() const;
    void setSearchBackend(SearchBackend backend);
    SearchBackend currentBackend() const;

    // === MeiliSearch Configuration ===
    void setMeiliSearchUrl(const QString& url);
    void setMeiliSearchApiKey(const QString& apiKey);
    QString meiliSearchUrl() const;

    // === Lucene Configuration ===
    void setLuceneIndexPath(const QString& path);
    QString luceneIndexPath() const;

    // === Document Indexing ===
    bool indexDocument(const HelpDocument& doc);
    bool indexDocuments(const QList<HelpDocument>& docs);
    bool removeDocument(const QString& documentId);
    bool updateDocument(const HelpDocument& doc);
    bool clearIndex();

    // === Pre-built Documentation ===
    void loadDefaultDocumentation();
    bool loadUserGuide();
    bool loadApiReference();
    bool loadTutorials();

    // === Search ===
    QList<SearchResult> search(const QString& query) const;
    QList<SearchResult> search(const QString& query, const QString& section) const;
    QList<SearchResult> search(const QString& query, const QStringList& sections) const;
    QList<SearchResult> searchAdvanced(const QString& query,
                                        const QString& section = QString(),
                                        const QString& category = QString(),
                                        const QStringList& tags = QStringList(),
                                        int maxResults = 50) const;

    // === Faceted Search ===
    QList<SearchResult> searchBySection(const QString& query, const QString& section) const;
    QList<SearchResult> searchByCategory(const QString& query, const QString& category) const;
    QList<SearchResult> searchByTag(const QString& tag) const;

    // === Autocomplete / Suggestions ===
    QStringList suggest(const QString& prefix, int maxSuggestions = 10) const;
    QStringList getPopularSearches(int count = 10) const;

    // === Index Statistics ===
    int documentCount() const;
    int indexSize() const;
    QStringList indexedSections() const;
    QStringList indexedCategories() const;

    // === Utility ===
    QString getDocumentContent(const QString& documentId) const;
    HelpDocument getDocument(const QString& documentId) const;
    bool documentExists(const QString& documentId) const;

Q_SIGNALS:
    void initialized();
    void documentIndexed(const QString& documentId);
    void documentRemoved(const QString& documentId);
    void searchCompleted(const QString& query, int resultCount);
    void error(const QString& message);
    void indexCleared();

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::help
