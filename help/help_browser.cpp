#include "help_browser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
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
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QMutexLocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QRegularExpression>
#include <QDebug>

namespace powsys365::help {

// ============================================================================
// SearchResultsPanel
// ============================================================================

SearchResultsPanel::SearchResultsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void SearchResultsPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_statusLabel = new QLabel(tr("No results"), this);
    m_statusLabel->setStyleSheet("QLabel { color: #888888; font-style: italic; }");
    layout->addWidget(m_statusLabel);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked,
            this, &SearchResultsPanel::onItemClicked);
}

void SearchResultsPanel::setResults(const QList<SearchResult>& results) {
    m_list->clear();
    m_docIdMap.clear();

    if (results.isEmpty()) {
        m_statusLabel->setText(tr("No results found"));
        m_statusLabel->setVisible(true);
        return;
    }

    m_statusLabel->setText(tr("%1 result(s)").arg(results.size()));

    for (const auto& result : results) {
        QString label = QString("%1 (%2%)").arg(result.title).arg(
            static_cast<int>(result.relevance * 10));
        QListWidgetItem* item = new QListWidgetItem(m_list);
        item->setText(label);
        item->setToolTip(result.content.left(200));

        // Set icon based on section
        if (result.section == "user_guide") {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogHelpButton));
        } else if (result.section == "api_reference") {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
        } else if (result.section == "tutorials") {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        }

        m_docIdMap[label] = result.documentId;
        m_list->addItem(item);
    }
}

void SearchResultsPanel::clear() {
    m_list->clear();
    m_docIdMap.clear();
    m_statusLabel->setText(tr("No results"));
}

void SearchResultsPanel::onItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString docId = m_docIdMap.value(item->text());
    if (!docId.isEmpty()) {
        Q_EMIT resultClicked(docId);
    }
}

// ============================================================================
// OutlinePanel
// ============================================================================

OutlinePanel::OutlinePanel(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setRootIsDecorated(true);
    setAlternatingRowColors(false);

    connect(this, &QTreeWidget::itemClicked,
            this, &OutlinePanel::onItemClicked);
}

void OutlinePanel::setOutline(const QStringList& headings) {
    clearOutline();
    for (int i = 0; i < headings.size(); ++i) {
        QTreeWidgetItem* item = new QTreeWidgetItem(this);
        item->setText(0, headings[i]);
        item->setData(0, Qt::UserRole, i);
        item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_ArrowRight));
    }
}

void OutlinePanel::clearOutline() {
    clear();
}

void OutlinePanel::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (item) {
        int line = item->data(0, Qt::UserRole).toInt();
        Q_EMIT headingClicked(line);
    }
}

// ============================================================================
// SectionBrowser
// ============================================================================

SectionBrowser::SectionBrowser(QWidget* parent)
    : QTreeWidget(parent)
{
    setupUI();
}

void SectionBrowser::setupUI() {
    setHeaderHidden(true);
    setColumnCount(1);
    setRootIsDecorated(true);
    setAlternatingRowColors(true);

    connect(this, &QTreeWidget::itemClicked,
            this, &SectionBrowser::onItemClicked);
}

void SectionBrowser::loadSections(const QList<SectionItem>& sections) {
    clearSections();

    QMap<QString, QTreeWidgetItem*> topLevelItems;

    for (const auto& section : sections) {
        QTreeWidgetItem* parent = nullptr;

        // Determine parent from section name
        if (section.id.startsWith("ug_")) {
            if (!topLevelItems.contains("user_guide")) {
                topLevelItems["user_guide"] = new QTreeWidgetItem(this);
                topLevelItems["user_guide"]->setText(0, tr("User Guide"));
                topLevelItems["user_guide"]->setIcon(0,
                    QApplication::style()->standardIcon(QStyle::SP_DialogHelpButton));
                topLevelItems["user_guide"]->setExpanded(true);
            }
            parent = topLevelItems["user_guide"];
        } else if (section.id.startsWith("api_")) {
            if (!topLevelItems.contains("api_reference")) {
                topLevelItems["api_reference"] = new QTreeWidgetItem(this);
                topLevelItems["api_reference"]->setText(0, tr("API Reference"));
                topLevelItems["api_reference"]->setIcon(0,
                    QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
                topLevelItems["api_reference"]->setExpanded(true);
            }
            parent = topLevelItems["api_reference"];
        } else if (section.id.startsWith("tut_")) {
            if (!topLevelItems.contains("tutorials")) {
                topLevelItems["tutorials"] = new QTreeWidgetItem(this);
                topLevelItems["tutorials"]->setText(0, tr("Tutorials"));
                topLevelItems["tutorials"]->setIcon(0,
                    QApplication::style()->standardIcon(QStyle::SP_FileIcon));
                topLevelItems["tutorials"]->setExpanded(true);
            }
            parent = topLevelItems["tutorials"];
        }

        QTreeWidgetItem* item;
        if (parent) {
            item = new QTreeWidgetItem(parent);
        } else {
            item = new QTreeWidgetItem(this);
        }

        item->setText(0, section.title);
        item->setData(0, Qt::UserRole, section.documentId);
        item->setToolTip(0, section.title);

        m_docIdToItem[section.documentId] = item;
    }
}

void SectionBrowser::selectSection(const QString& documentId) {
    auto it = m_docIdToItem.find(documentId);
    if (it != m_docIdToItem.end()) {
        setCurrentItem(it.value());
    }
}

void SectionBrowser::clearSections() {
    m_docIdToItem.clear();
    clear();
}

void SectionBrowser::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    QString docId = item->data(0, Qt::UserRole).toString();
    if (!docId.isEmpty()) {
        Q_EMIT sectionSelected(docId);
    }
}

// ============================================================================
// HelpContentView
// ============================================================================

HelpContentView::HelpContentView(QWidget* parent)
    : QTextBrowser(parent)
{
    setOpenExternalLinks(true);
    setOpenLinks(false);
}

void HelpContentView::setDocumentContent(const QString& html) {
    m_currentContent = html;
    setHtml(html);
}

void HelpContentView::highlightSearchTerm(const QString& term) {
    m_highlightTerm = term;
    if (term.isEmpty()) return;

    QTextDocument* doc = document();
    QTextCursor cursor(doc);

    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QColor(255, 255, 0, 128));

    // Clear previous selections
    cursor.select(QTextCursor::Document);
    cursor.setCharFormat(QTextCharFormat());

    // Find and highlight
    cursor.movePosition(QTextCursor::Start);
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = doc->find(term, cursor);
        if (!cursor.isNull()) {
            cursor.mergeCharFormat(highlightFormat);
        }
    }
}

void HelpContentView::clearHighlight() {
    m_highlightTerm.clear();
    QTextDocument* doc = document();
    QTextCursor cursor(doc);
    cursor.select(QTextCursor::Document);
    cursor.setCharFormat(QTextCharFormat());
}

void HelpContentView::mousePressEvent(QMouseEvent* event) {
    QTextBrowser::mousePressEvent(event);
}

void HelpContentView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* copyAct = menu.addAction(tr("Copy"));
    QAction* selectAllAct = menu.addAction(tr("Select All"));
    menu.addSeparator();
    QAction* findAct = menu.addAction(tr("Find in Page..."));

    connect(copyAct, &QAction::triggered, this, &QTextEdit::copy);
    connect(selectAllAct, &QAction::triggered, this, &QTextEdit::selectAll);

    QAction* selected = menu.exec(event->globalPos());
    if (selected == findAct) {
        bool ok;
        QString text = QInputDialog::getText(this, tr("Find in Page"),
            tr("Search term:"), QLineEdit::Normal, m_highlightTerm, &ok);
        if (ok && !text.isEmpty()) {
            highlightSearchTerm(text);
        }
    }
}

// ============================================================================
// HelpBrowser (Main)
// ============================================================================

HelpBrowser::HelpBrowser(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
    setupSections();
}

HelpBrowser::~HelpBrowser() = default;

void HelpBrowser::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    // === Toolbar ===
    setupToolbar();
    mainLayout->addWidget(m_toolbar);

    // === Breadcrumb ===
    m_breadcrumbLabel = new QLabel(tr("Ready"), this);
    m_breadcrumbLabel->setStyleSheet("QLabel { padding: 4px; background: #f0f0f0; border: 1px solid #ccc; }");
    m_breadcrumbLabel->setFrameStyle(QFrame::StyledPanel);
    mainLayout->addWidget(m_breadcrumbLabel);

    // === Main Splitter ===
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left: Tabs for Sections and Outline
    m_leftTabs = new QTabWidget(this);
    m_sectionBrowser = new SectionBrowser(this);
    m_outlinePanel = new OutlinePanel(this);
    m_leftTabs->addTab(m_sectionBrowser, tr("Contents"));
    m_leftTabs->addTab(m_outlinePanel, tr("Outline"));
    m_mainSplitter->addWidget(m_leftTabs);

    // Center: Content view
    m_contentView = new HelpContentView(this);
    m_mainSplitter->addWidget(m_contentView);

    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setSizes({250, 750});

    mainLayout->addWidget(m_mainSplitter, 1);

    // Bottom: Search results
    m_bottomTabs = new QTabWidget(this);
    m_searchResults = new SearchResultsPanel(this);
    m_bottomTabs->addTab(m_searchResults, tr("Search Results"));
    m_bottomTabs->setVisible(false);
    mainLayout->addWidget(m_bottomTabs);
}

void HelpBrowser::setupToolbar() {
    m_toolbar = new QToolBar(tr("Help"), this);
    m_toolbar->setFloatable(false);

    m_backAct = m_toolbar->addAction(QIcon::fromTheme("go-previous"), tr("Back"));
    m_backAct->setShortcut(QKeySequence::Back);
    m_backAct->setEnabled(false);

    m_forwardAct = m_toolbar->addAction(QIcon::fromTheme("go-next"), tr("Forward"));
    m_forwardAct->setShortcut(QKeySequence::Forward);
    m_forwardAct->setEnabled(false);

    m_homeAct = m_toolbar->addAction(QIcon::fromTheme("go-home"), tr("Home"));

    m_refreshAct = m_toolbar->addAction(QIcon::fromTheme("view-refresh"), tr("Refresh"));

    m_toolbar->addSeparator();

    // Search
    QLabel* searchLabel = new QLabel(tr("Search:"), this);
    m_toolbar->addWidget(searchLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search help..."));
    m_searchEdit->setMaximumWidth(300);
    m_searchEdit->setMinimumWidth(200);
    m_toolbar->addWidget(m_searchEdit);

    m_searchBtn = new QPushButton(tr("Search"), this);
    m_toolbar->addWidget(m_searchBtn);

    m_toolbar->addSeparator();

    // Section filter
    QLabel* sectionLabel = new QLabel(tr("Section:"), this);
    m_toolbar->addWidget(sectionLabel);

    m_sectionFilter = new QComboBox(this);
    m_sectionFilter->addItem(tr("All Sections"), QString());
    m_sectionFilter->addItem(tr("User Guide"), "user_guide");
    m_sectionFilter->addItem(tr("API Reference"), "api_reference");
    m_sectionFilter->addItem(tr("Tutorials"), "tutorials");
    m_sectionFilter->setMinimumWidth(150);
    m_toolbar->addWidget(m_sectionFilter);
}

void HelpBrowser::setupConnections() {
    // Navigation
    connect(m_backAct, &QAction::triggered, this, &HelpBrowser::back);
    connect(m_forwardAct, &QAction::triggered, this, &HelpBrowser::forward);
    connect(m_homeAct, &QAction::triggered, this, [this]() {
        loadSection("ug_intro");
    });
    connect(m_refreshAct, &QAction::triggered, this, [this]() {
        if (!m_currentDocumentId.isEmpty()) {
            loadSection(m_currentDocumentId);
        }
    });

    // Search
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        QString query = m_searchEdit->text().trimmed();
        if (!query.isEmpty()) {
            search(query);
        }
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, m_searchBtn, &QPushButton::click);

    // Section browser
    connect(m_sectionBrowser, &SectionBrowser::sectionSelected,
            this, &HelpBrowser::onSectionSelected);

    // Search results
    connect(m_searchResults, &SearchResultsPanel::resultClicked,
            this, &HelpBrowser::onSearchResultClicked);

    // Outline
    connect(m_outlinePanel, &OutlinePanel::headingClicked,
            this, [this](int /*line*/) {
        // Scroll to heading position
    });

    // Content view
    connect(m_contentView, &HelpContentView::anchorClicked,
            this, [this](const QString& anchor) {
        if (m_engine && m_engine->documentExists(anchor)) {
            loadSection(anchor);
        }
    });
}

void HelpBrowser::setupSections() {
    m_userGuideSections = {
        {"ug_intro", tr("Introduction"), "ug_intro", {}, 1},
        {"ug_install", tr("Installation"), "ug_install", {}, 2},
        {"ug_workspace", tr("Workspace"), "ug_workspace", {}, 3},
        {"ug_editor", tr("Code Editor"), "ug_editor", {}, 4},
        {"ug_debugging", tr("Debugging"), "ug_debugging", {}, 5},
        {"ug_git", tr("Version Control"), "ug_git", {}, 6},
        {"ug_audio", tr("Audio Feedback"), "ug_audio", {}, 7},
        {"ug_search", tr("Help Search"), "ug_search", {}, 8},
        {"ug_electrical", tr("Electrical Systems"), "ug_electrical", {}, 9},
        {"ug_keyboard", tr("Keyboard Shortcuts"), "ug_keyboard", {}, 10},
        {"ug_lsp_config", tr("LSP Configuration"), "ug_lsp_config", {}, 11},
        {"ug_troubleshoot", tr("Troubleshooting"), "ug_troubleshoot", {}, 12},
    };

    m_apiReferenceSections = {
        {"api_lsp", tr("LSP API"), "api_lsp", {}, 1},
        {"api_dap", tr("DAP API"), "api_dap", {}, 2},
        {"api_git", tr("Git API"), "api_git", {}, 3},
        {"api_audio", tr("Audio API"), "api_audio", {}, 4},
        {"api_help", tr("Help API"), "api_help", {}, 5},
        {"api_jsonrpc", tr("JSON-RPC Protocol"), "api_jsonrpc", {}, 6},
    };

    m_tutorialSections = {
        {"tut_first_project", tr("First Project"), "tut_first_project", {}, 1},
        {"tut_debugging", tr("Debugging Tutorial"), "tut_debugging", {}, 2},
        {"tut_git_workflow", tr("Git Workflow"), "tut_git_workflow", {}, 3},
        {"tut_lsp_custom", tr("LSP Customization"), "tut_lsp_custom", {}, 4},
        {"tut_audio_custom", tr("Audio Customization"), "tut_audio_custom", {}, 5},
        {"tut_power_analysis", tr("Power Analysis"), "tut_power_analysis", {}, 6},
        {"tut_keyboard", tr("Productivity Tips"), "tut_keyboard", {}, 7},
    };
}

void HelpBrowser::setHelpEngine(HelpEngine* engine) {
    m_engine = engine;

    if (m_engine) {
        connect(m_engine, &HelpEngine::error, this, [this](const QString& msg) {
            m_breadcrumbLabel->setText(tr("Error: %1").arg(msg));
        });

        // Load initial documentation
        m_engine->loadDefaultDocumentation();
        loadSectionTree();

        // Show intro page
        loadSection("ug_intro");
    }
}

HelpEngine* HelpBrowser::helpEngine() const {
    return m_engine;
}

bool HelpBrowser::loadSection(const QString& id) {
    if (!m_engine) return false;

    QMutexLocker lock(&m_mutex);

    HelpDocument doc = m_engine->getDocument(id);
    if (doc.id.isEmpty()) {
        // Show a "not found" page
        doc.id = id;
        doc.title = tr("Document Not Found");
        doc.content = tr("The requested document '%1' was not found in the help index. "
                          "Try searching for related topics or browse the table of contents.").arg(id);
        doc.section = "error";
    }

    pushHistory(id, doc.title);
    displayDocument(doc);

    // Select in section browser
    m_sectionBrowser->selectSection(id);

    Q_EMIT documentLoaded(id, doc.title);
    return true;
}

bool HelpBrowser::loadDocument(const QString& documentId) {
    return loadSection(documentId);
}

void HelpBrowser::search(const QString& text) {
    if (!m_engine || text.isEmpty()) return;

    QString section = m_sectionFilter->currentData().toString();
    searchInSection(text, section);
}

void HelpBrowser::searchInSection(const QString& text, const QString& section) {
    if (!m_engine) return;

    Q_EMIT searchStarted(text);

    QList<SearchResult> results;
    if (section.isEmpty()) {
        results = m_engine->search(text);
    } else {
        results = m_engine->searchBySection(text, section);
    }

    m_searchResults->setResults(results);
    m_bottomTabs->setVisible(true);
    m_bottomTabs->setCurrentIndex(0);

    // Highlight search term in content if current doc matches
    m_contentView->highlightSearchTerm(text);

    Q_EMIT searchFinished(results.size());
}

void HelpBrowser::back() {
    QMutexLocker lock(&m_mutex);

    if (m_backStack.isEmpty()) return;

    // Push current to forward stack
    if (!m_currentDocumentId.isEmpty()) {
        HistoryEntry current;
        current.documentId = m_currentDocumentId;
        current.title = m_contentView->documentTitle();
        m_forwardStack.push(current);
    }

    HistoryEntry entry = m_backStack.pop();
    m_currentDocumentId = entry.documentId;

    lock.unlock();

    if (m_engine) {
        HelpDocument doc = m_engine->getDocument(entry.documentId);
        if (!doc.id.isEmpty()) {
            displayDocument(doc);
        }
    }

    updateNavigationActions();
}

void HelpBrowser::forward() {
    QMutexLocker lock(&m_mutex);

    if (m_forwardStack.isEmpty()) return;

    // Push current to back stack
    if (!m_currentDocumentId.isEmpty()) {
        HistoryEntry current;
        current.documentId = m_currentDocumentId;
        current.title = m_contentView->documentTitle();
        m_backStack.push(current);
    }

    HistoryEntry entry = m_forwardStack.pop();
    m_currentDocumentId = entry.documentId;

    lock.unlock();

    if (m_engine) {
        HelpDocument doc = m_engine->getDocument(entry.documentId);
        if (!doc.id.isEmpty()) {
            displayDocument(doc);
        }
    }

    updateNavigationActions();
}

bool HelpBrowser::canGoBack() const {
    QMutexLocker lock(&m_mutex);
    return !m_backStack.isEmpty();
}

bool HelpBrowser::canGoForward() const {
    QMutexLocker lock(&m_mutex);
    return !m_forwardStack.isEmpty();
}

void HelpBrowser::clearHistory() {
    QMutexLocker lock(&m_mutex);
    m_backStack.clear();
    m_forwardStack.clear();
    updateNavigationActions();
}

void HelpBrowser::loadSectionTree() {
    QList<SectionItem> allSections;
    allSections.append(m_userGuideSections);
    allSections.append(m_apiReferenceSections);
    allSections.append(m_tutorialSections);
    m_sectionBrowser->loadSections(allSections);
}

QString HelpBrowser::currentSection() const {
    return m_currentSection;
}

QStringList HelpBrowser::availableSections() const {
    return QStringList{tr("User Guide"), tr("API Reference"), tr("Tutorials")};
}

void HelpBrowser::setShowOutline(bool show) {
    m_leftTabs->setTabVisible(1, show);
}

void HelpBrowser::setShowSearchPanel(bool show) {
    m_bottomTabs->setVisible(show);
}

void HelpBrowser::addBookmark(const QString& documentId, const QString& title) {
    if (!m_bookmarks.contains(documentId)) {
        m_bookmarks.append(documentId);
    }
}

void HelpBrowser::removeBookmark(const QString& documentId) {
    m_bookmarks.removeAll(documentId);
}

QStringList HelpBrowser::bookmarks() const {
    return m_bookmarks;
}

void HelpBrowser::pushHistory(const QString& documentId, const QString& title) {
    QMutexLocker lock(&m_mutex);

    // Push current to back stack
    if (!m_currentDocumentId.isEmpty() && m_currentDocumentId != documentId) {
        HistoryEntry current;
        current.documentId = m_currentDocumentId;
        current.title = m_contentView->documentTitle();
        if (current.title.isEmpty()) current.title = m_currentDocumentId;
        m_backStack.push(current);
    }

    m_currentDocumentId = documentId;
    m_forwardStack.clear(); // Clear forward stack on new navigation

    updateNavigationActions();
}

void HelpBrowser::displayDocument(const HelpDocument& doc) {
    QString html = documentToHtml(doc);
    m_contentView->setDocumentContent(html);

    m_currentSection = doc.section;
    updateBreadcrumb(doc.section, doc.title);

    // Update outline with heading extraction
    QStringList headings = extractHeadings(doc.content);
    m_outlinePanel->setOutline(headings);
}

QString HelpBrowser::documentToHtml(const HelpDocument& doc) const {
    QString html;
    html += "<html><head><style>";
    html += "body { font-family: 'Segoe UI', Arial, sans-serif; font-size: 14px; "
            "line-height: 1.6; color: #333; max-width: 900px; margin: 20px auto; padding: 0 20px; }";
    html += "h1 { color: #1565C0; border-bottom: 2px solid #1565C0; padding-bottom: 8px; }";
    html += "h2 { color: #1976D2; border-bottom: 1px solid #e0e0e0; padding-bottom: 4px; margin-top: 24px; }";
    html += "h3 { color: #2196F3; margin-top: 20px; }";
    html += "p { margin: 12px 0; }";
    html += "code { background: #f5f5f5; padding: 2px 6px; border-radius: 3px; font-family: Consolas, monospace; }";
    html += "pre { background: #f5f5f5; padding: 12px; border-radius: 4px; overflow-x: auto; }";
    html += "ul, ol { margin: 8px 0; padding-left: 24px; }";
    html += "li { margin: 4px 0; }";
    html += "a { color: #1565C0; text-decoration: none; }";
    html += "a:hover { text-decoration: underline; }";
    html += ".tag { display: inline-block; background: #e3f2fd; color: #1565C0; "
            "padding: 2px 8px; border-radius: 12px; font-size: 12px; margin: 2px; }";
    html += ".section-badge { display: inline-block; background: #1565C0; color: white; "
            "padding: 4px 12px; border-radius: 4px; font-size: 12px; font-weight: bold; }";
    html += "</style></head><body>";

    // Section badge
    QString sectionLabel;
    if (doc.section == "user_guide") sectionLabel = tr("User Guide");
    else if (doc.section == "api_reference") sectionLabel = tr("API Reference");
    else if (doc.section == "tutorials") sectionLabel = tr("Tutorials");
    else sectionLabel = doc.section;

    html += QString("<div><span class=\"section-badge\">%1</span>").arg(sectionLabel);
    if (!doc.category.isEmpty()) {
        html += QString(" <span style=\"color: #888; font-size: 12px;\">/ %1</span>").arg(doc.category);
    }
    html += "</div>";

    // Title
    html += QString("<h1>%1</h1>").arg(doc.title);

    // Tags
    if (!doc.tags.isEmpty()) {
        html += "<div style=\"margin: 12px 0;\">";
        for (const QString& tag : doc.tags) {
            html += QString("<span class=\"tag\">%1</span>").arg(tag);
        }
        html += "</div>";
    }

    // Convert plain text content to HTML
    QString content = doc.content;
    content.replace("&", "&amp;");
    content.replace("<", "&lt;");
    content.replace(">", "&gt;");

    // Convert numbered items (e.g., "Step 1:", "1.")
    static const QRegularExpression stepRe("^(Step\\s+\\d+[.:]|\\d+\\.\\s)");

    // Format as HTML paragraphs
    QStringList paragraphs = content.split("\n\n");
    for (QString& para : paragraphs) {
        para = para.trimmed();
        if (para.isEmpty()) continue;

        // Detect and format code blocks
        if (para.startsWith("  ") || para.startsWith("\t")) {
            html += QString("<pre>%1</pre>").arg(para.trimmed());
        }
        // Detect headings
        else if (para.startsWith("=== ") && para.endsWith(" ===")) {
            QString title = para.mid(4, para.length() - 8);
            html += QString("<h2>%1</h2>").arg(title);
        }
        // Regular paragraphs
        else {
            // Convert inline code
            para.replace(QRegularExpression("`([^`]+)`"), "<code>\\1</code>");
            // Convert bold
            para.replace(QRegularExpression("\\*\\*([^*]+)\\*\\*"), "<b>\\1</b>");
            // Convert italic
            para.replace(QRegularExpression("\\*([^*]+)\\*"), "<i>\\1</i>");
            // Convert line breaks
            para.replace("\n", "<br/>");
            html += QString("<p>%1</p>").arg(para);
        }
    }

    html += "<hr/><div style=\"color: #888; font-size: 12px; text-align: center;\">";
    html += tr("POWSYS365 Help System - Document ID: %1").arg(doc.id);
    html += "</div>";

    html += "</body></html>";
    return html;
}

QStringList HelpBrowser::extractHeadings(const QString& content) const {
    QStringList headings;
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("=== ") && trimmed.endsWith(" ===")) {
            headings.append(trimmed.mid(4, trimmed.length() - 8));
        }
        else if (trimmed.startsWith("Step ")) {
            headings.append(trimmed);
        }
    }

    return headings;
}

void HelpBrowser::onSearchResultClicked(const QString& documentId) {
    loadSection(documentId);
}

void HelpBrowser::onSectionSelected(const QString& documentId) {
    loadSection(documentId);
}

void HelpBrowser::updateBreadcrumb(const QString& section, const QString& title) {
    QString sectionLabel;
    if (section == "user_guide") sectionLabel = tr("User Guide");
    else if (section == "api_reference") sectionLabel = tr("API Reference");
    else if (section == "tutorials") sectionLabel = tr("Tutorials");
    else if (section == "error") sectionLabel = tr("Error");
    else sectionLabel = section;

    m_breadcrumbLabel->setText(QString("%1 > %2").arg(sectionLabel, title));
}

void HelpBrowser::updateNavigationActions() {
    m_backAct->setEnabled(!m_backStack.isEmpty());
    m_forwardAct->setEnabled(!m_forwardStack.isEmpty());
    Q_EMIT historyChanged(canGoBack(), canGoForward());
}

} // namespace powsys365::help
