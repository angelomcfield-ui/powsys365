#pragma once

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTreeWidget>
#include <QListWidget>
#include <QTabWidget>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QProgressBar>
#include <QStack>
#include <QMutex>
#include <memory>

#include "help_engine.h"

namespace powsys365::help {

/**
 * @brief Navigation history entry
 */
struct HistoryEntry {
    QString documentId;
    QString title;
    int scrollPosition = 0;
};

/**
 * @brief Section tree item data
 */
struct SectionItem {
    QString id;
    QString title;
    QString documentId;
    QStringList subSections;
    int order = 0;
};

/**
 * @brief Search results panel
 */
class SearchResultsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchResultsPanel(QWidget* parent = nullptr);
    void setResults(const QList<SearchResult>& results);
    void clear();

Q_SIGNALS:
    void resultClicked(const QString& documentId);
    void resultHovered(const QString& documentId);

private:
    void setupUI();
    void onItemClicked(QListWidgetItem* item);

    QListWidget* m_list;
    QLabel* m_statusLabel;
    QMap<QString, QString> m_docIdMap; // item text -> doc id
};

/**
 * @brief Document outline / table of contents panel
 */
class OutlinePanel : public QTreeWidget {
    Q_OBJECT
public:
    explicit OutlinePanel(QWidget* parent = nullptr);
    void setOutline(const QStringList& headings);
    void clearOutline();

Q_SIGNALS:
    void headingClicked(int line);

private:
    void onItemClicked(QTreeWidgetItem* item, int column);
};

/**
 * @brief Section browser for navigating document sections
 */
class SectionBrowser : public QTreeWidget {
    Q_OBJECT
public:
    explicit SectionBrowser(QWidget* parent = nullptr);
    void loadSections(const QList<SectionItem>& sections);
    void selectSection(const QString& documentId);
    void clearSections();

Q_SIGNALS:
    void sectionSelected(const QString& documentId);

private:
    void setupUI();
    void onItemClicked(QTreeWidgetItem* item, int column);
    QMap<QString, QTreeWidgetItem*> m_docIdToItem;
};

/**
 * @brief Custom QTextBrowser for help content with syntax highlighting support
 */
class HelpContentView : public QTextBrowser {
    Q_OBJECT
public:
    explicit HelpContentView(QWidget* parent = nullptr);

    void setDocumentContent(const QString& html);
    void highlightSearchTerm(const QString& term);
    void clearHighlight();

Q_SIGNALS:
    void anchorClicked(const QString& anchor);
    void headingClicked(int line);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QString m_currentContent;
    QString m_highlightTerm;
};

/**
 * @brief Main help browser widget
 *
 * Provides a complete help browsing experience with:
 * - Section navigation (User Guide, API Reference, Tutorials)
 * - Full-text search with results panel
 * - Back/forward navigation history
 * - Document outline / table of contents
 * - Breadcrumb navigation
 */
class HelpBrowser : public QWidget {
    Q_OBJECT
public:
    explicit HelpBrowser(QWidget* parent = nullptr);
    ~HelpBrowser();

    // === Engine ===
    void setHelpEngine(HelpEngine* engine);
    HelpEngine* helpEngine() const;

    // === Navigation ===
    bool loadSection(const QString& id);
    bool loadDocument(const QString& documentId);

    // === Search ===
    void search(const QString& text);
    void searchInSection(const QString& text, const QString& section);

    // === History ===
    void back();
    void forward();
    bool canGoBack() const;
    bool canGoForward() const;
    void clearHistory();

    // === Sections ===
    void loadSectionTree();
    QString currentSection() const;
    QStringList availableSections() const;

    // === Display ===
    void setShowOutline(bool show);
    void setShowSearchPanel(bool show);

    // === Bookmarks ===
    void addBookmark(const QString& documentId, const QString& title);
    void removeBookmark(const QString& documentId);
    QStringList bookmarks() const;

Q_SIGNALS:
    void documentLoaded(const QString& documentId, const QString& title);
    void searchStarted(const QString& query);
    void searchFinished(int resultCount);
    void historyChanged(bool canBack, bool canForward);

private:
    void setupUI();
    void setupConnections();
    void setupToolbar();
    void setupSections();

    void updateBreadcrumb(const QString& section, const QString& title);
    void pushHistory(const QString& documentId, const QString& title);
    void displayDocument(const HelpDocument& doc);
    QString documentToHtml(const HelpDocument& doc) const;
    void onSearchResultClicked(const QString& documentId);
    void onSectionSelected(const QString& documentId);
    void updateNavigationActions();

    HelpEngine* m_engine = nullptr;

    // Navigation history
    QStack<HistoryEntry> m_backStack;
    QStack<HistoryEntry> m_forwardStack;
    QString m_currentDocumentId;
    QString m_currentSection;

    // UI Components
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_leftSplitter = nullptr;

    // Toolbar
    QToolBar* m_toolbar = nullptr;
    QAction* m_backAct = nullptr;
    QAction* m_forwardAct = nullptr;
    QAction* m_homeAct = nullptr;
    QAction* m_refreshAct = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QComboBox* m_sectionFilter = nullptr;
    QLabel* m_breadcrumbLabel = nullptr;

    // Left panel
    QTabWidget* m_leftTabs = nullptr;
    SectionBrowser* m_sectionBrowser = nullptr;
    OutlinePanel* m_outlinePanel = nullptr;

    // Center
    HelpContentView* m_contentView = nullptr;

    // Right/bottom panel
    QTabWidget* m_bottomTabs = nullptr;
    SearchResultsPanel* m_searchResults = nullptr;

    // Sections data
    QList<SectionItem> m_userGuideSections;
    QList<SectionItem> m_apiReferenceSections;
    QList<SectionItem> m_tutorialSections;
    QStringList m_bookmarks;

    mutable QMutex m_mutex;
};

} // namespace powsys365::help
