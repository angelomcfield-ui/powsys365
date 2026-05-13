#include "help_engine.h"
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QDebug>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <algorithm>
#include <cmath>

namespace powsys365::help {

// Tokenizer for in-memory search
static QStringList tokenize(const QString& text) {
    QStringList tokens;
    static const QRegularExpression wordRe("\\b[a-zA-Z0-9_\\-]{2,}\\b");
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text.toLower());
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        tokens.append(match.captured());
    }
    return tokens;
}

static QStringList tokenizeWithStemming(const QString& text) {
    QStringList tokens = tokenize(text);
    // Simple stemming: remove common suffixes
    QStringList stemmed;
    for (QString token : tokens) {
        if (token.endsWith("ing") && token.length() > 5) token.chop(3);
        else if (token.endsWith("ed") && token.length() > 4) token.chop(2);
        else if (token.endsWith("s") && token.length() > 3) token.chop(1);
        else if (token.endsWith("es") && token.length() > 4) token.chop(2);
        else if (token.endsWith("ies") && token.length() > 5) { token.chop(3); token.append('y'); }
        else if (token.endsWith("ly") && token.length() > 4) token.chop(2);
        else if (token.endsWith("ment") && token.length() > 6) token.chop(4);
        else if (token.endsWith("tion") && token.length() > 6) token.chop(4);
        stemmed.append(token);
    }
    return stemmed;
}

static float calculateRelevance(const HelpDocument& doc, const QStringList& queryTokens) {
    float score = 0.0f;

    QString titleLower = doc.title.toLower();
    QString contentLower = doc.content.toLower();
    QStringList docTags;
    for (const QString& tag : doc.tags) docTags.append(tag.toLower());

    for (const QString& token : queryTokens) {
        // Title match (highest weight)
        if (titleLower.contains(token)) {
            score += 10.0f;
            if (titleLower.startsWith(token)) score += 5.0f;
        }

        // Content match
        int contentMatches = contentLower.count(token);
        score += contentMatches * 1.0f;

        // Tag match
        for (const QString& tag : docTags) {
            if (tag.contains(token)) score += 8.0f;
        }

        // Section/category match
        if (doc.section.toLower().contains(token)) score += 3.0f;
        if (doc.category.toLower().contains(token)) score += 3.0f;
    }

    // Boost by recency
    int daysSinceModified = doc.lastModified.daysTo(QDateTime::currentDateTime());
    if (daysSinceModified < 30) score *= 1.1f;
    if (daysSinceModified < 7) score *= 1.2f;

    return score;
}

class HelpEngine::Impl {
public:
    HelpEngine* q;
    SearchBackend backend = SearchBackend::Hybrid;
    bool initialized = false;
    mutable QMutex mutex;

    // MeiliSearch
    QString meiliUrl = "http://127.0.0.1:7700";
    QString meiliApiKey;
    std::unique_ptr<QNetworkAccessManager> networkManager;

    // Lucene FTS5
    QString lucenePath;
    QSqlDatabase luceneDb;
    bool luceneAvailable = false;

    // In-memory index
    QMap<QString, HelpDocument> documents;
    QMap<QString, QSet<QString>> invertedIndex; // token -> set of doc IDs
    QMap<QString, int> searchFrequency; // query -> count

    // Statistics
    int totalSearches = 0;
    int totalIndexings = 0;

    explicit Impl(HelpEngine* parent) : q(parent) {}

    bool initializeLucene() {
        lucenePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/help_index.db";
        QDir().mkpath(QFileInfo(lucenePath).path());

        luceneDb = QSqlDatabase::addDatabase("QSQLITE", "lucene_help");
        luceneDb.setDatabaseName(lucenePath);

        if (!luceneDb.open()) {
            qWarning() << "Cannot open Lucene SQLite DB:" << luceneDb.lastError().text();
            luceneAvailable = false;
            return false;
        }

        // Create FTS5 table if not exists
        QSqlQuery query(luceneDb);
        bool ok = query.exec(
            "CREATE VIRTUAL TABLE IF NOT EXISTS help_docs USING fts5("
            "  id UNINDEXED,"
            "  title,"
            "  content,"
            "  section,"
            "  category,"
            "  tags,"
            "  metadata UNINDEXED"
            ")"
        );

        if (!ok) {
            qWarning() << "FTS5 not available, falling back to in-memory:" << query.lastError().text();
            luceneAvailable = false;
            // Create regular table as fallback
            query.exec(
                "CREATE TABLE IF NOT EXISTS help_docs ("
                "  id TEXT PRIMARY KEY,"
                "  title TEXT,"
                "  content TEXT,"
                "  section TEXT,"
                "  category TEXT,"
                "  tags TEXT,"
                "  last_modified TEXT,"
                "  metadata TEXT"
                ")"
            );
            return true; // Still usable with LIKE queries
        }

        luceneAvailable = true;
        return true;
    }

    bool initializeMeiliSearch() {
        networkManager = std::make_unique<QNetworkAccessManager>(q);
        return true;
    }

    void buildInvertedIndex() {
        invertedIndex.clear();
        for (auto it = documents.begin(); it != documents.end(); ++it) {
            const HelpDocument& doc = it.value();
            QString text = doc.title + " " + doc.content + " " + doc.section + " "
                + doc.category + " " + doc.tags.join(" ");
            QStringList tokens = tokenizeWithStemming(text);
            for (const QString& token : tokens) {
                invertedIndex[token].insert(doc.id);
            }
        }
    }

    bool indexInMemory(const HelpDocument& doc) {
        documents[doc.id] = doc;

        // Update inverted index
        QString text = doc.title + " " + doc.content + " " + doc.section + " "
            + doc.category + " " + doc.tags.join(" ");
        QStringList tokens = tokenizeWithStemming(text);
        for (const QString& token : tokens) {
            invertedIndex[token].insert(doc.id);
        }

        totalIndexings++;
        return true;
    }

    bool indexLucene(const HelpDocument& doc) {
        if (!luceneDb.isOpen()) return false;

        QSqlQuery query(luceneDb);
        if (luceneAvailable) {
            query.prepare(
                "INSERT INTO help_docs (id, title, content, section, category, tags, metadata) "
                "VALUES (:id, :title, :content, :section, :category, :tags, :metadata) "
                "ON CONFLICT(id) DO UPDATE SET "
                "title=excluded.title, content=excluded.content, "
                "section=excluded.section, category=excluded.category, "
                "tags=excluded.tags, metadata=excluded.metadata"
            );
        } else {
            query.prepare(
                "INSERT OR REPLACE INTO help_docs (id, title, content, section, category, "
                "tags, last_modified, metadata) VALUES (:id, :title, :content, :section, "
                ":category, :tags, :last_modified, :metadata)"
            );
            query.bindValue(":last_modified", doc.lastModified.toString(Qt::ISODate));
        }

        query.bindValue(":id", doc.id);
        query.bindValue(":title", doc.title);
        query.bindValue(":content", doc.content);
        query.bindValue(":section", doc.section);
        query.bindValue(":category", doc.category);
        query.bindValue(":tags", doc.tags.join(","));
        query.bindValue(":metadata", QJsonDocument(doc.metadata).toJson(QJsonDocument::Compact));

        return query.exec();
    }

    bool indexMeiliSearch(const HelpDocument& doc) {
        if (!networkManager) return false;

        QJsonObject docObj = doc.toJson();
        QJsonArray docs;
        docs.append(docObj);

        QJsonObject body;
        body["documents"] = docs;

        QNetworkRequest request(QUrl(meiliUrl + "/indexes/help/documents"));
        if (!meiliApiKey.isEmpty()) {
            request.setRawHeader("Authorization", "Bearer " + meiliApiKey.toUtf8());
        }
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = networkManager->post(
            request, QJsonDocument(body).toJson());

        // For simplicity, we don't wait for the reply in this sync-style function
        // In production, this should be async with callbacks
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
        return true;
    }

    QList<SearchResult> searchInMemory(const QString& query,
                                        const QString& section = QString(),
                                        const QString& category = QString(),
                                        const QStringList& tags = QStringList(),
                                        int maxResults = 50) const {
        QList<SearchResult> results;
        if (documents.isEmpty()) return results;

        QStringList queryTokens = tokenizeWithStemming(query);
        if (queryTokens.isEmpty()) {
            // Return all documents sorted by section
            queryTokens = tokenizeWithStemming("*");
        }

        // Find matching documents using inverted index
        QSet<QString> candidateIds;
        bool firstToken = true;
        for (const QString& token : queryTokens) {
            auto it = invertedIndex.find(token);
            if (it != invertedIndex.end()) {
                if (firstToken) {
                    candidateIds = it.value();
                    firstToken = false;
                } else {
                    candidateIds.intersect(it.value());
                }
            }
        }

        // If no intersection, use union
        if (candidateIds.isEmpty()) {
            for (const QString& token : queryTokens) {
                auto it = invertedIndex.find(token);
                if (it != invertedIndex.end()) {
                    candidateIds.unite(it.value());
                }
            }
        }

        // Score and filter candidates
        QList<QPair<float, SearchResult>> scored;
        for (const QString& docId : candidateIds) {
            auto dit = documents.find(docId);
            if (dit == documents.end()) continue;

            const HelpDocument& doc = dit.value();

            // Apply filters
            if (!section.isEmpty() && doc.section != section) continue;
            if (!category.isEmpty() && doc.category != category) continue;
            if (!tags.isEmpty()) {
                bool hasTag = false;
                for (const QString& tag : tags) {
                    if (doc.tags.contains(tag, Qt::CaseInsensitive)) {
                        hasTag = true;
                        break;
                    }
                }
                if (!hasTag) continue;
            }

            float score = calculateRelevance(doc, queryTokens);
            if (score > 0) {
                SearchResult sr;
                sr.documentId = doc.id;
                sr.title = doc.title;
                sr.section = doc.section;
                sr.category = doc.category;
                sr.relevance = score;
                sr.lastModified = doc.lastModified;
                sr.metadata = doc.metadata;

                // Extract content snippet
                QString contentLower = doc.content.toLower();
                int matchPos = -1;
                for (const QString& token : queryTokens) {
                    matchPos = contentLower.indexOf(token);
                    if (matchPos >= 0) break;
                }
                if (matchPos >= 0) {
                    int start = qMax(0, matchPos - 80);
                    int end = qMin(doc.content.length(), matchPos + 200);
                    sr.content = doc.content.mid(start, end - start);
                    if (start > 0) sr.content.prepend("...");
                    if (end < doc.content.length()) sr.content.append("...");
                    sr.matchCount = 1;
                } else {
                    sr.content = doc.content.left(200) + "...";
                }

                scored.append({score, sr});
            }
        }

        // Sort by relevance descending
        std::sort(scored.begin(), scored.end(),
            [](const QPair<float, SearchResult>& a, const QPair<float, SearchResult>& b) {
                return a.first > b.first;
            });

        int count = qMin(maxResults, scored.size());
        for (int i = 0; i < count; ++i) {
            results.append(scored[i].second);
        }

        return results;
    }

    QList<SearchResult> searchLucene(const QString& query,
                                      const QString& section = QString(),
                                      const QString& category = QString(),
                                      const QStringList& tags = QStringList(),
                                      int maxResults = 50) const {
        QList<SearchResult> results;
        if (!luceneDb.isOpen()) return results;

        QSqlQuery query_sql(luceneDb);
        QString queryStr;

        if (luceneAvailable) {
            // FTS5 query
            QString ftsQuery = query.toLower();
            ftsQuery.replace("'", "''");
            // Convert to FTS5 syntax: each word gets a * suffix for prefix matching
            QStringList words = tokenize(ftsQuery);
            QStringList ftsWords;
            for (const QString& w : words) {
                ftsWords.append(w + "*");
            }
            QString ftsExpr = ftsWords.join(" ");

            queryStr = QString(
                "SELECT id, title, content, section, category, "
                "  rank AS relevance "
                "FROM help_docs "
                "WHERE help_docs MATCH '%1' "
            ).arg(ftsExpr);

            if (!section.isEmpty()) {
                queryStr += QString(" AND section='%1' ").arg(section);
            }
            if (!category.isEmpty()) {
                queryStr += QString(" AND category='%1' ").arg(category);
            }
            queryStr += QString(" ORDER BY rank LIMIT %1").arg(maxResults);
        } else {
            // Fallback to LIKE queries
            QString likeQuery = "%" + query.toLower() + "%";
            queryStr = QString(
                "SELECT id, title, content, section, category "
                "FROM help_docs "
                "WHERE (LOWER(title) LIKE '%1' OR LOWER(content) LIKE '%1') "
            ).arg(likeQuery);

            if (!section.isEmpty()) {
                queryStr += QString(" AND section='%1' ").arg(section);
            }
            if (!category.isEmpty()) {
                queryStr += QString(" AND category='%1' ").arg(category);
            }
            queryStr += QString(" LIMIT %1").arg(maxResults);
        }

        if (!query_sql.exec(queryStr)) {
            qWarning() << "Lucene search error:" << query_sql.lastError().text();
            return results;
        }

        while (query_sql.next()) {
            SearchResult sr;
            sr.documentId = query_sql.value(0).toString();
            sr.title = query_sql.value(1).toString();
            sr.content = query_sql.value(2).toString();
            sr.section = query_sql.value(3).toString();
            sr.category = query_sql.value(4).toString();

            if (luceneAvailable) {
                sr.relevance = query_sql.value(5).toFloat();
            }

            // Truncate content for snippet
            if (sr.content.length() > 300) {
                sr.content = sr.content.left(300) + "...";
            }

            results.append(sr);
        }

        return results;
    }
};

HelpEngine::HelpEngine(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this))
{
}

HelpEngine::~HelpEngine() {
    shutdown();
}

bool HelpEngine::initialize(SearchBackend backend) {
    QMutexLocker lock(&d->mutex);

    d->backend = backend;

    bool meiliOk = true;
    bool luceneOk = true;
    bool memoryOk = true;

    if (backend == SearchBackend::MeiliSearch || backend == SearchBackend::Hybrid) {
        meiliOk = d->initializeMeiliSearch();
    }

    if (backend == SearchBackend::LuceneFTS5 || backend == SearchBackend::Hybrid) {
        luceneOk = d->initializeLucene();
    }

    if (backend == SearchBackend::InMemory || backend == SearchBackend::Hybrid) {
        memoryOk = true; // Always available
    }

    d->initialized = true;
    Q_EMIT initialized();
    return meiliOk || luceneOk || memoryOk;
}

void HelpEngine::shutdown() {
    QMutexLocker lock(&d->mutex);

    if (d->luceneDb.isOpen()) {
        d->luceneDb.close();
    }

    d->documents.clear();
    d->invertedIndex.clear();
    d->networkManager.reset();
    d->initialized = false;
}

bool HelpEngine::isInitialized() const {
    return d->initialized;
}

void HelpEngine::setSearchBackend(SearchBackend backend) {
    QMutexLocker lock(&d->mutex);
    d->backend = backend;
}

SearchBackend HelpEngine::currentBackend() const {
    return d->backend;
}

void HelpEngine::setMeiliSearchUrl(const QString& url) {
    QMutexLocker lock(&d->mutex);
    d->meiliUrl = url;
}

void HelpEngine::setMeiliSearchApiKey(const QString& apiKey) {
    QMutexLocker lock(&d->mutex);
    d->meiliApiKey = apiKey;
}

QString HelpEngine::meiliSearchUrl() const {
    return d->meiliUrl;
}

void HelpEngine::setLuceneIndexPath(const QString& path) {
    QMutexLocker lock(&d->mutex);
    d->lucenePath = path;
}

QString HelpEngine::luceneIndexPath() const {
    return d->lucenePath;
}

bool HelpEngine::indexDocument(const HelpDocument& doc) {
    QMutexLocker lock(&d->mutex);
    if (!d->initialized) return false;

    bool ok = true;

    // Always index in memory
    d->indexInMemory(doc);

    if (d->backend == SearchBackend::LuceneFTS5 || d->backend == SearchBackend::Hybrid) {
        ok &= d->indexLucene(doc);
    }

    if (d->backend == SearchBackend::MeiliSearch || d->backend == SearchBackend::Hybrid) {
        ok &= d->indexMeiliSearch(doc);
    }

    Q_EMIT documentIndexed(doc.id);
    return ok;
}

bool HelpEngine::indexDocuments(const QList<HelpDocument>& docs) {
    bool allOk = true;
    for (const auto& doc : docs) {
        allOk &= indexDocument(doc);
    }
    return allOk;
}

bool HelpEngine::removeDocument(const QString& documentId) {
    QMutexLocker lock(&d->mutex);

    // Remove from memory
    d->documents.remove(documentId);
    for (auto it = d->invertedIndex.begin(); it != d->invertedIndex.end();) {
        it.value().remove(documentId);
        if (it.value().isEmpty()) {
            it = d->invertedIndex.erase(it);
        } else {
            ++it;
        }
    }

    // Remove from Lucene
    if (d->luceneDb.isOpen()) {
        QSqlQuery query(d->luceneDb);
        query.prepare("DELETE FROM help_docs WHERE id = :id");
        query.bindValue(":id", documentId);
        query.exec();
    }

    Q_EMIT documentRemoved(documentId);
    return true;
}

bool HelpEngine::updateDocument(const HelpDocument& doc) {
    // Remove and re-index
    removeDocument(doc.id);
    return indexDocument(doc);
}

bool HelpEngine::clearIndex() {
    QMutexLocker lock(&d->mutex);

    d->documents.clear();
    d->invertedIndex.clear();
    d->totalIndexings = 0;

    if (d->luceneDb.isOpen()) {
        QSqlQuery query(d->luceneDb);
        query.exec("DELETE FROM help_docs");
    }

    Q_EMIT indexCleared();
    return true;
}

void HelpEngine::loadDefaultDocumentation() {
    loadUserGuide();
    loadApiReference();
    loadTutorials();
}

bool HelpEngine::loadUserGuide() {
    QMutexLocker lock(&d->mutex);

    QList<HelpDocument> docs = {
        {
            "ug_intro", "Introduction to POWSYS365",
            "POWSYS365 is a comprehensive power systems engineering IDE designed for "
            "electrical engineers working with power system analysis, design, and simulation. "
            "This guide will walk you through the main features and capabilities of the application. "
            "POWSYS365 integrates LSP-based code editing, OpenAL audio feedback, Git version control, "
            "debugging via DAP, and a full-text searchable help system.",
            "user_guide", "getting_started", {"intro", "overview", "welcome"},
            QDateTime::currentDateTime()
        },
        {
            "ug_install", "Installation and Setup",
            "This section covers installation requirements for POWSYS365. System requirements: "
            "Operating System: Windows 10/11, Linux (Ubuntu 20.04+), or macOS 12+. "
            "Memory: Minimum 4GB RAM, recommended 8GB+. "
            "Storage: At least 2GB free space. "
            "Dependencies: Qt6, OpenAL, Git, MeiliSearch (optional). "
            "Installation steps: Download the installer from the official website, "
            "run the setup wizard, and follow the on-screen instructions.",
            "user_guide", "getting_started", {"install", "setup", "requirements"},
            QDateTime::currentDateTime()
        },
        {
            "ug_workspace", "Workspace Management",
            "The POWSYS365 workspace organizes your projects, files, and configurations. "
            "Create a new workspace via File > New Workspace or open an existing one. "
            "Workspaces support multiple projects, custom layouts, and session restoration. "
            "You can switch between workspaces quickly using the workspace switcher in the toolbar.",
            "user_guide", "basics", {"workspace", "project", "files"},
            QDateTime::currentDateTime()
        },
        {
            "ug_editor", "Code Editor",
            "The integrated code editor supports LSP (Language Server Protocol) for intelligent "
            "code completion, hover information, go-to-definition, find references, and diagnostics. "
            "Supported languages include C, C++, Python, JavaScript, TypeScript, Rust, Go, Java, "
            "Kotlin, C#, and over 60 more languages. The editor features syntax highlighting, "
            "code folding, auto-indentation, bracket matching, and multi-cursor editing.",
            "user_guide", "editor", {"editor", "lsp", "coding", "languages"},
            QDateTime::currentDateTime()
        },
        {
            "ug_debugging", "Debugging with DAP",
            "POWSYS365 integrates the Debug Adapter Protocol (DAP) for debugging support. "
            "Supported debuggers include GDB, LLDB, Python debugpy, Node.js, Java, Go Delve, "
            ".NET Core, and 20+ more. Features include breakpoints, watch expressions, "
            "call stack visualization, variable inspection, and step-through execution. "
            "Configure debugging via Run > Debug Configuration.",
            "user_guide", "debugging", {"debug", "dap", "gdb", "lldb", "breakpoints"},
            QDateTime::currentDateTime()
        },
        {
            "ug_git", "Version Control with Git",
            "Git integration provides full version control capabilities. Features include: "
            "repository initialization and cloning, commit staging and management, "
            "branching and merging, remote operations (fetch, pull, push), "
            "stash management, tag operations, diff viewing, and blame annotation. "
            "Access Git features via the Git panel or context menus.",
            "user_guide", "version_control", {"git", "vcs", "repository", "branch", "commit"},
            QDateTime::currentDateTime()
        },
        {
            "ug_audio", "Audio Feedback System",
            "The OpenAL-based audio engine provides auditory feedback for IDE operations. "
            "23 built-in system sounds cover success/error notifications, UI interactions, "
            "debug events, and Git operations. Configure audio preferences in Settings > Audio. "
            "The system supports 3D positional audio, volume control, pitch adjustment, "
            "and looping playback for ambient sounds.",
            "user_guide", "audio", {"audio", "sound", "openal", "feedback"},
            QDateTime::currentDateTime()
        },
        {
            "ug_search", "Help and Documentation Search",
            "The integrated help system uses MeiliSearch and Lucene FTS5 for fast full-text "
            "search across all documentation. Features include fuzzy matching, faceted search "
            "by section or category, autocomplete suggestions, and relevance-ranked results. "
            "Access help via F1 or the Help menu.",
            "user_guide", "help", {"search", "help", "documentation", "meilisearch"},
            QDateTime::currentDateTime()
        },
        {
            "ug_electrical", "Electrical Systems Overview",
            "POWSYS365 is designed for power systems engineering applications. This section "
            "covers fundamental concepts: AC and DC power systems, three-phase systems, "
            "power factor correction, load flow analysis, short circuit calculations, "
            "protection coordination, and harmonic analysis. Understanding these concepts "
            "is essential for effective use of the IDE.",
            "user_guide", "electrical", {"electrical", "power", "ac", "dc", "three-phase"},
            QDateTime::currentDateTime()
        },
        {
            "ug_keyboard", "Keyboard Shortcuts",
            "Master keyboard shortcuts for efficient workflow. Essential shortcuts: "
            "Ctrl+N: New file, Ctrl+O: Open file, Ctrl+S: Save, F5: Start debugging, "
            "F10: Step over, F11: Step into, Shift+F11: Step out, Ctrl+Shift+F: Find in files, "
            "Ctrl+B: Build, F1: Help search, Ctrl+G: Go to line, Ctrl+Shift+G: Git panel. "
            "Customize shortcuts in Settings > Keyboard.",
            "user_guide", "reference", {"keyboard", "shortcuts", "hotkeys", "productivity"},
            QDateTime::currentDateTime()
        },
        {
            "ug_lsp_config", "Configuring Language Servers",
            "POWSYS365 supports 60+ languages through LSP. Each language server can be "
            "configured with custom initialization options, workspace settings, and "
            "execution parameters. Access configuration via Settings > Language Servers. "
            "Common configurations include compiler flags for C/C++ (clangd), "
            "Python interpreter paths (pylsp), and TypeScript options (tsserver).",
            "user_guide", "configuration", {"lsp", "language-server", "config", "setup"},
            QDateTime::currentDateTime()
        },
        {
            "ug_troubleshoot", "Troubleshooting",
            "Common issues and solutions: If LSP features are not working, verify the "
            "language server executable is in PATH. For audio issues, check OpenAL "
            "installation. If Git operations fail, verify Git is installed and configured. "
            "For search issues, ensure the help index is built (Help > Rebuild Index). "
            "Check the log files in %APPDATA%/POWSYS365/logs for detailed error information.",
            "user_guide", "troubleshooting", {"troubleshoot", "debug", "issues", "faq"},
            QDateTime::currentDateTime()
        }
    };

    for (const auto& doc : docs) {
        d->indexInMemory(doc);
        d->indexLucene(doc);
    }

    return true;
}

bool HelpEngine::loadApiReference() {
    QMutexLocker lock(&d->mutex);

    QList<HelpDocument> docs = {
        {
            "api_lsp", "LSP API Reference",
            "LanguageServerManager provides the main interface for LSP operations. "
            "Key methods: startServer(language) - starts a language server for the given language. "
            "stopServer(language) - stops the language server. "
            "initialize(language) - sends LSP initialize request with client capabilities. "
            "didOpen(uri, text, languageId) - notifies server that a document was opened. "
            "didChange(uri, changes) - sends document content changes. "
            "requestCompletion(uri, position, callback) - requests code completion. "
            "requestHover(uri, position, callback) - requests hover information. "
            "requestDefinition(uri, position, callback) - requests go-to-definition.",
            "api_reference", "lsp", {"api", "lsp", "language-server", "reference"},
            QDateTime::currentDateTime()
        },
        {
            "api_dap", "DAP API Reference",
            "DebugAdapterManager implements the Debug Adapter Protocol. Key methods: "
            "startDebugging(config) - starts a debug session with the given configuration. "
            "stopDebugging() - terminates the debug session. "
            "continue_() - resumes execution. pause() - pauses execution. "
            "next() - step over. stepIn() - step into. stepOut() - step out. "
            "setBreakpoint(file, line, condition) - sets a breakpoint. "
            "getStackTrace(threadId, levels) - retrieves the call stack. "
            "getVariables(variablesReference) - inspects variables. "
            "evaluateWatch(expression, frameId) - evaluates watch expressions.",
            "api_reference", "dap", {"api", "dap", "debug", "reference"},
            QDateTime::currentDateTime()
        },
        {
            "api_git", "Git Manager API Reference",
            "GitManager provides comprehensive Git operations. Repository methods: "
            "init(path), clone(url, localPath, branch), openRepository(path). "
            "Commit methods: stage(file), unstage(file), commit(message). "
            "Branch methods: branch(name), checkout(name), merge(branchName). "
            "Remote methods: fetch(remote), pull(remote, branch), push(remote, branch). "
            "Info methods: status(), log(count, branch), diff(from, to), blame(file, line). "
            "Utility: setConfig(key, value), getConfig(key), resetSoft/ Mixed/ Hard(hash).",
            "api_reference", "git", {"api", "git", "vcs", "reference"},
            QDateTime::currentDateTime()
        },
        {
            "api_audio", "Audio Engine API Reference",
            "AudioEngine provides OpenAL-based audio playback. Key methods: "
            "initialize() - initializes the OpenAL context and loads system sounds. "
            "loadSound(id, filePath) - loads an audio file. "
            "playSound(id) / playSound(id, properties) - plays a sound. "
            "stopSound(id) - stops all instances of a sound. "
            "setVolume(id, volume), setPitch(id, pitch), setLooping(id, looping). "
            "setMasterVolume(volume), setListenerPosition(position). "
            "loadSystemSounds() - generates 23 built-in system sounds. "
            "playSystemSound(id) - plays a system sound (success, error, click, etc.).",
            "api_reference", "audio", {"api", "audio", "openal", "sound", "reference"},
            QDateTime::currentDateTime()
        },
        {
            "api_help", "Help Engine API Reference",
            "HelpEngine provides full-text search capabilities. Key methods: "
            "initialize(backend) - initializes with MeiliSearch, Lucene FTS5, InMemory, or Hybrid. "
            "indexDocument(doc) - adds a document to the search index. "
            "search(query) - performs a full-text search. "
            "searchAdvanced(query, section, category, tags, maxResults) - faceted search. "
            "suggest(prefix) - provides autocomplete suggestions. "
            "loadDefaultDocumentation() - indexes built-in documentation.",
            "api_reference", "help", {"api", "help", "search", "meilisearch", "lucene"},
            QDateTime::currentDateTime()
        },
        {
            "api_jsonrpc", "JSON-RPC Protocol Reference",
            "JsonRpc implements the JSON-RPC 2.0 protocol used by LSP. Key methods: "
            "sendRequest(method, params, id) - sends a request and returns request ID. "
            "sendNotification(method, params) - sends a notification (no response). "
            "sendResponse(id, result) - sends a response to a request. "
            "sendError(id, code, message, data) - sends an error response. "
            "sendRequestSync(method, params, timeoutMs) - synchronous request. "
            "encodeMessage(msg) / decodeMessage(data) - LSP content-length framing. "
            "Error codes: ParseError (-32700), InvalidRequest (-32600), "
            "MethodNotFound (-32601), ServerNotInitialized (-32002).",
            "api_reference", "protocols", {"api", "json-rpc", "lsp", "protocol"},
            QDateTime::currentDateTime()
        }
    };

    for (const auto& doc : docs) {
        d->indexInMemory(doc);
        d->indexLucene(doc);
    }

    return true;
}

bool HelpEngine::loadTutorials() {
    QMutexLocker lock(&d->mutex);

    QList<HelpDocument> docs = {
        {
            "tut_first_project", "Your First Project",
            "Step 1: Create a new project via File > New Project. Select your project type "
            "(C/C++, Python, etc.) and configure the build system. Step 2: Add source files "
            "by right-clicking on the project and selecting Add New File. Step 3: Write your "
            "code with full LSP support - enjoy autocomplete, diagnostics, and hover info. "
            "Step 4: Build your project with Ctrl+B. Step 5: Run or debug with F5.",
            "tutorials", "beginner", {"tutorial", "beginner", "project", "first"},
            QDateTime::currentDateTime()
        },
        {
            "tut_debugging", "Debugging Tutorial",
            "Learn to use the debugger effectively. 1. Set breakpoints by clicking the gutter "
            "or pressing F9. 2. Start debugging with F5. When a breakpoint is hit, the "
            "debugger panel shows the call stack, local variables, and watch expressions. "
            "3. Step through code with F10 (step over) and F11 (step into). 4. Add watch "
            "expressions to monitor variable values. 5. Use the console to evaluate expressions. "
            "6. Continue execution with F5 or stop with Shift+F5.",
            "tutorials", "intermediate", {"tutorial", "debugging", "debugger", "breakpoints"},
            QDateTime::currentDateTime()
        },
        {
            "tut_git_workflow", "Git Workflow Tutorial",
            "Master Git in POWSYS365. 1. Initialize a repository: Git > Initialize Repository. "
            "2. Stage changes: right-click files and select Stage, or use Git > Stage All. "
            "3. Commit: write a message and press Ctrl+Enter. 4. Create branches for features: "
            "Git > New Branch. 5. Switch between branches from the branch selector. 6. Push "
            "to remote: Git > Push. 7. Pull updates: Git > Pull. 8. Resolve merge conflicts "
            "using the built-in merge tool.",
            "tutorials", "intermediate", {"tutorial", "git", "workflow", "branch", "commit"},
            QDateTime::currentDateTime()
        },
        {
            "tut_lsp_custom", "Customizing Language Servers",
            "Configure language servers for your needs. 1. Open Settings > Language Servers. "
            "2. Select a language from the list of 60+ supported languages. 3. Adjust the "
            "executable path and command-line arguments. 4. Configure initialization options "
            "in JSON format. 5. Set workspace-specific overrides in .powsys365/settings.json. "
            "6. Restart the language server to apply changes. Common customizations include "
            "compiler flags for C/C++ (clangd), Python virtual environments, and TypeScript config.",
            "tutorials", "advanced", {"tutorial", "lsp", "configuration", "advanced"},
            QDateTime::currentDateTime()
        },
        {
            "tut_audio_custom", "Customizing Audio Feedback",
            "Personalize the audio experience. 1. Access Settings > Audio. 2. Adjust master "
            "volume and per-sound volumes. 3. Enable/disable specific system sounds. "
            "4. Load custom WAV files for any system sound. 5. Configure 3D audio positioning "
            "for spatial feedback. 6. Set up ambient sounds for different workspace modes. "
            "The audio engine supports WAV files natively and provides programmatic tone "
            "generation for all system sounds.",
            "tutorials", "advanced", {"tutorial", "audio", "customization", "openal"},
            QDateTime::currentDateTime()
        },
        {
            "tut_power_analysis", "Power Systems Analysis Tutorial",
            "This tutorial introduces power system analysis using POWSYS365. Topics covered: "
            "Load flow analysis using Newton-Raphson and Gauss-Seidel methods. Short circuit "
            "calculations for symmetrical and unsymmetrical faults. Protection coordination "
            "studies for relay settings. Harmonic analysis and filter design. Stability analysis "
            "including transient and voltage stability. Each topic includes example projects "
            "and step-by-step instructions.",
            "tutorials", "electrical", {"tutorial", "power", "analysis", "electrical", "engineering"},
            QDateTime::currentDateTime()
        },
        {
            "tut_keyboard", "Productivity Tips",
            "Boost your productivity with these tips. 1. Use Ctrl+P for quick file navigation. "
            "2. Use Ctrl+Shift+P for command palette access. 3. Multi-cursor editing with "
            "Alt+Click or Ctrl+D. 4. Navigate symbols with Ctrl+Shift+O. 5. Use code snippets "
            "with Tab completion. 6. Toggle comments with Ctrl+/. 7. Format code with "
            "Ctrl+Shift+I. 8. Split editor with Ctrl+\\. 9. Zen mode with F11. "
            "10. Customize keybindings in settings for your workflow.",
            "tutorials", "productivity", {"tutorial", "productivity", "shortcuts", "tips"},
            QDateTime::currentDateTime()
        }
    };

    for (const auto& doc : docs) {
        d->indexInMemory(doc);
        d->indexLucene(doc);
    }

    return true;
}

QList<SearchResult> HelpEngine::search(const QString& query) const {
    return searchAdvanced(query, QString(), QString(), QStringList(), 50);
}

QList<SearchResult> HelpEngine::search(const QString& query, const QString& section) const {
    return searchAdvanced(query, section, QString(), QStringList(), 50);
}

QList<SearchResult> HelpEngine::search(const QString& query, const QStringList& sections) const {
    QList<SearchResult> allResults;
    for (const QString& section : sections) {
        allResults.append(search(query, section));
    }
    // Re-sort by relevance
    std::sort(allResults.begin(), allResults.end(),
        [](const SearchResult& a, const SearchResult& b) { return a.relevance > b.relevance; });
    return allResults;
}

QList<SearchResult> HelpEngine::searchAdvanced(const QString& query,
                                                 const QString& section,
                                                 const QString& category,
                                                 const QStringList& tags,
                                                 int maxResults) const {
    QMutexLocker lock(&d->mutex);
    d->totalSearches++;
    d->searchFrequency[query.toLower()]++;

    QList<SearchResult> results;

    // Try Lucene first if available
    if ((d->backend == SearchBackend::LuceneFTS5 || d->backend == SearchBackend::Hybrid)
        && d->luceneDb.isOpen()) {
        results = d->searchLucene(query, section, category, tags, maxResults);
    }

    // If no results from Lucene, try in-memory
    if (results.isEmpty() || d->backend == SearchBackend::InMemory) {
        results = d->searchInMemory(query, section, category, tags, maxResults);
    }

    Q_EMIT searchCompleted(query, results.size());
    return results;
}

QList<SearchResult> HelpEngine::searchBySection(const QString& query,
                                                 const QString& section) const {
    return searchAdvanced(query, section, QString(), QStringList(), 50);
}

QList<SearchResult> HelpEngine::searchByCategory(const QString& query,
                                                  const QString& category) const {
    return searchAdvanced(query, QString(), category, QStringList(), 50);
}

QList<SearchResult> HelpEngine::searchByTag(const QString& tag) const {
    return searchAdvanced(QString(), QString(), QString(), QStringList{tag}, 50);
}

QStringList HelpEngine::suggest(const QString& prefix, int maxSuggestions) const {
    QMutexLocker lock(&d->mutex);
    if (prefix.isEmpty()) return QStringList();

    QStringList suggestions;
    QString lowerPrefix = prefix.toLower();

    // Collect from document titles and content
    QSet<QString> candidates;
    for (auto it = d->documents.begin(); it != d->documents.end(); ++it) {
        candidates.insert(it.value().title);
        // Add individual words from title
        for (const QString& word : it.value().title.split(' ', Qt::SkipEmptyParts)) {
            if (word.length() >= lowerPrefix.length()) {
                candidates.insert(word);
            }
        }
    }

    // Also use inverted index tokens
    for (auto it = d->invertedIndex.begin(); it != d->invertedIndex.end(); ++it) {
        if (it.key().startsWith(lowerPrefix)) {
            candidates.insert(it.key());
        }
    }

    // Sort and return top suggestions
    QStringList sorted = candidates.values();
    sorted.sort();

    for (const QString& s : sorted) {
        if (s.startsWith(lowerPrefix, Qt::CaseInsensitive) && !suggestions.contains(s)) {
            suggestions.append(s);
            if (suggestions.size() >= maxSuggestions) break;
        }
    }

    return suggestions;
}

QStringList HelpEngine::getPopularSearches(int count) const {
    QMutexLocker lock(&d->mutex);

    QList<QPair<int, QString>> ranked;
    for (auto it = d->searchFrequency.begin(); it != d->searchFrequency.end(); ++it) {
        ranked.append({it.value(), it.key()});
    }

    std::sort(ranked.begin(), ranked.end(),
        [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
            return a.first > b.first;
        });

    QStringList result;
    int limit = qMin(count, ranked.size());
    for (int i = 0; i < limit; ++i) {
        result.append(ranked[i].second);
    }
    return result;
}

int HelpEngine::documentCount() const {
    QMutexLocker lock(&d->mutex);
    return d->documents.size();
}

int HelpEngine::indexSize() const {
    QMutexLocker lock(&d->mutex);
    return d->invertedIndex.size();
}

QStringList HelpEngine::indexedSections() const {
    QMutexLocker lock(&d->mutex);
    QSet<QString> sections;
    for (auto it = d->documents.begin(); it != d->documents.end(); ++it) {
        sections.insert(it.value().section);
    }
    QStringList result = sections.values();
    result.sort();
    return result;
}

QStringList HelpEngine::indexedCategories() const {
    QMutexLocker lock(&d->mutex);
    QSet<QString> categories;
    for (auto it = d->documents.begin(); it != d->documents.end(); ++it) {
        categories.insert(it.value().category);
    }
    QStringList result = categories.values();
    result.sort();
    return result;
}

QString HelpEngine::getDocumentContent(const QString& documentId) const {
    QMutexLocker lock(&d->mutex);
    auto it = d->documents.find(documentId);
    if (it != d->documents.end()) {
        return it.value().content;
    }
    return QString();
}

HelpDocument HelpEngine::getDocument(const QString& documentId) const {
    QMutexLocker lock(&d->mutex);
    return d->documents.value(documentId);
}

bool HelpEngine::documentExists(const QString& documentId) const {
    QMutexLocker lock(&d->mutex);
    return d->documents.contains(documentId);
}

} // namespace powsys365::help
