#include "ai_tool_window.h"
#include "ai_prompt_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QListWidget>
#include <QLabel>
#include <QProgressBar>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFrame>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QCloseEvent>
#include <QPalette>
#include <QApplication>
#include <QRegularExpression>
#include <cmath>

namespace powsys365 {

// ============================================================================
// Utilidades
// ============================================================================

QString providerToString(LLMProvider provider)
{
    switch (provider) {
        case LLMProvider::DeepSeek: return "DeepSeek";
        case LLMProvider::Kimi: return "Kimi";
        case LLMProvider::GPT4o: return "GPT-4o";
        case LLMProvider::Claude: return "Claude";
        case LLMProvider::Llama: return "Llama";
        case LLMProvider::Gemini: return "Gemini";
        case LLMProvider::Custom: return "Custom";
    }
    return "Unknown";
}

LLMProvider stringToProvider(const QString& str)
{
    if (str == "DeepSeek") return LLMProvider::DeepSeek;
    if (str == "Kimi") return LLMProvider::Kimi;
    if (str == "GPT-4o") return LLMProvider::GPT4o;
    if (str == "Claude") return LLMProvider::Claude;
    if (str == "Llama") return LLMProvider::Llama;
    if (str == "Gemini") return LLMProvider::Gemini;
    if (str == "Custom") return LLMProvider::Custom;
    return LLMProvider::DeepSeek;
}

// ============================================================================
// AIChatWidget
// ============================================================================

AIChatWidget::AIChatWidget(QWidget* parent)
    : QFrame(parent)
{
    setupUI();
}

AIChatWidget::~AIChatWidget() = default;

void AIChatWidget::setupUI()
{
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(4, 4, 4, 4);
    main_layout->setSpacing(4);

    // System message
    system_message_label_ = new QLabel(this);
    system_message_label_->setWordWrap(true);
    system_message_label_->setStyleSheet(
        "QLabel { background-color: #2d3748; color: #e2e8f0; "
        "padding: 8px; border-radius: 4px; font-size: 11px; }"
    );
    system_message_label_->setVisible(false);
    main_layout->addWidget(system_message_label_);

    // Scroll area for messages
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setFrameStyle(QFrame::NoFrame);

    messages_container_ = new QFrame();
    messages_container_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    messages_layout_ = new QVBoxLayout(messages_container_);
    messages_layout_->setAlignment(Qt::AlignTop);
    messages_layout_->setSpacing(8);
    messages_layout_->addStretch();

    scroll_area_->setWidget(messages_container_);
    main_layout->addWidget(scroll_area_, 1);

    setStyleSheet(
        "QFrame { background-color: #1a202c; }"
        "QLabel { color: #e2e8f0; }"
    );
}

void AIChatWidget::appendMessage(const ChatMessage& message)
{
    auto* msg_frame = new QFrame(messages_container_);
    msg_frame->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);

    QString bg_color = (message.role == "user") ? "#2b6cb0" :
                       (message.role == "assistant") ? "#2d3748" : "#4a5568";
    msg_frame->setStyleSheet(
        QString("QFrame { background-color: %1; border-radius: 8px; padding: 4px; }").arg(bg_color)
    );

    auto* msg_layout = new QVBoxLayout(msg_frame);
    msg_layout->setContentsMargins(8, 6, 8, 6);
    msg_layout->setSpacing(4);

    // Header
    auto* header_layout = new QHBoxLayout();
    QString role_icon = (message.role == "user") ? "You" :
                        (message.role == "assistant") ? providerToString(message.provider) : "System";
    auto* role_label = new QLabel(QString("<b>%1</b>").arg(role_icon), msg_frame);
    role_label->setStyleSheet("color: #a0aec0; font-size: 10px;");
    header_layout->addWidget(role_label);

    auto* time_label = new QLabel(message.timestamp.toString("hh:mm:ss"), msg_frame);
    time_label->setStyleSheet("color: #718096; font-size: 9px;");
    header_layout->addStretch();
    header_layout->addWidget(time_label);
    msg_layout->addLayout(header_layout);

    // Content
    auto* content_label = new QLabel(msg_frame);
    content_label->setWordWrap(true);
    content_label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    content_label->setTextFormat(Qt::RichText);
    content_label->setText(formatMessageContent(message.content));
    content_label->setStyleSheet("color: #e2e8f0; font-size: 12px; padding: 4px;");
    msg_layout->addWidget(content_label);

    // Token info
    if (message.input_tokens > 0 || message.output_tokens > 0) {
        auto* token_label = new QLabel(
            QString("in: %1 | out: %2 | $%3").arg(message.input_tokens)
                .arg(message.output_tokens).arg(message.estimated_cost_usd, 0, 'f', 4),
            msg_frame
        );
        token_label->setStyleSheet("color: #718096; font-size: 9px;");
        msg_layout->addWidget(token_label);
    }

    // Insert before stretch
    messages_layout_->insertWidget(messages_layout_->count() - 1, msg_frame);
    message_widgets_[message.id] = msg_frame;

    // Auto scroll
    QTimer::singleShot(50, [this]() {
        scroll_area_->verticalScrollBar()->setValue(
            scroll_area_->verticalScrollBar()->maximum()
        );
    });
}

void AIChatWidget::appendStreamingChunk(const QString& chunk, const QString& message_id)
{
    if (!streaming_labels_.contains(message_id)) {
        // Crear nuevo mensaje streaming
        ChatMessage msg;
        msg.id = message_id;
        msg.role = "assistant";
        msg.timestamp = QDateTime::currentDateTime();
        appendMessage(msg);

        // Encontrar el label de contenido
        auto* frame = message_widgets_[message_id];
        if (frame) {
            auto* content = frame->findChild<QLabel*>();
            if (content) {
                streaming_labels_[message_id] = content;
                streaming_buffers_[message_id] = "";
            }
        }
    }

    if (streaming_labels_.contains(message_id)) {
        streaming_buffers_[message_id] += chunk;
        streaming_labels_[message_id]->setText(
            formatMessageContent(streaming_buffers_[message_id] + "<span style='color:#63b3ed'>|</span>")
        );
    }
}

void AIChatWidget::finalizeStreamingMessage(const QString& message_id)
{
    if (streaming_labels_.contains(message_id)) {
        streaming_labels_[message_id]->setText(
            formatMessageContent(streaming_buffers_[message_id])
        );
        streaming_labels_.remove(message_id);
        streaming_buffers_.remove(message_id);
    }
}

void AIChatWidget::clearChat()
{
    // Limpiar widgets
    QLayoutItem* item;
    while ((item = messages_layout_->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
    messages_layout_->addStretch();

    message_widgets_.clear();
    streaming_labels_.clear();
    streaming_buffers_.clear();
}

void AIChatWidget::setSystemMessageVisible(bool visible)
{
    system_message_label_->setVisible(visible);
}

QString AIChatWidget::formatMessageContent(const QString& content) const
{
    QString html = content;

    // Escapar caracteres HTML excepto tags
    html.replace("&", "&amp;");
    html.replace("<", "&lt;");
    html.replace(">", "&gt;");

    // Bold
    html.replace(QRegularExpression("&lt;b&gt;(.+?)&lt;/b&gt;"), "<b>\1</b>");
    // Restaurar tags permitidos
    html.replace("&lt;b&gt;", "<b>");
    html.replace("&lt;/b&gt;", "</b>");
    html.replace("&lt;i&gt;", "<i>");
    html.replace("&lt;/i&gt;", "</i>");

    // Code blocks
    QRegularExpression code_block_re("```(.*?)```", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = code_block_re.globalMatch(html);
    while (it.hasNext()) {
        auto match = it.next();
        QString code = match.captured(1);
        QString replacement = QString("<pre style='background:#1a1a2e;color:#e0e0e0;padding:8px;"
            "border-radius:4px;overflow-x:auto;font-family:monospace;font-size:11px;'>%1</pre>")
            .arg(code);
        html.replace(match.capturedStart(), match.capturedLength(), replacement);
    }

    // Inline code
    html.replace(QRegularExpression("`(.+?)`"),
        "<code style='background:#2d3748;color:#e2e8f0;padding:1px 4px;border-radius:3px;"
        "font-family:monospace;font-size:11px;'>\1</code>");

    // Headers
    html.replace(QRegularExpression("^### (.+)$", QRegularExpression::MultilineOption),
        "<h3 style='color:#63b3ed;margin:4px 0;'>\1</h3>");
    html.replace(QRegularExpression("^## (.+)$", QRegularExpression::MultilineOption),
        "<h2 style='color:#63b3ed;margin:6px 0;'>\1</h2>");
    html.replace(QRegularExpression("^# (.+)$", QRegularExpression::MultilineOption),
        "<h1 style='color:#63b3ed;margin:8px 0;'>\1</h1>");

    // Bullet lists
    html.replace(QRegularExpression("^- (.+)$", QRegularExpression::MultilineOption),
        "<li style='margin-left:16px;'>\1</li>");

    // Numbered lists
    html.replace(QRegularExpression("^(\\d+)\\. (.+)$", QRegularExpression::MultilineOption),
        "<li style='margin-left:16px;' value='\1'>\2</li>");

    // Blockquotes
    html.replace(QRegularExpression("^&gt; (.+)$", QRegularExpression::MultilineOption),
        "<div style='border-left:3px solid #4a5568;padding-left:8px;color:#a0aec0;margin:4px 0;'>\1</div>");

    // Links
    html.replace(QRegularExpression("\\[([^\\]]+)\\]\\(([^)]+)\\)"),
        "<a href='\2' style='color:#63b3ed;text-decoration:none;'>\1</a>");

    // Line breaks
    html.replace("\n", "<br>");

    return html;
}

QString AIChatWidget::escapeHtml(const QString& text) const
{
    QString result = text;
    result.replace("&", "&amp;");
    result.replace("<", "&lt;");
    result.replace(">", "&gt;");
    return result;
}

void AIChatWidget::applyCodeHighlighting(QString& html) const
{
    (void)html;
    // Implementacion completa requerira QSyntaxHighlighter
}

QString AIChatWidget::exportToMarkdown() const
{
    QString md;
    md += "# POWSYS365 AI Conversation\n\n";
    md += QString("_Export: %1_\n\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    md += "---\n\n";
    // Nota: Para exportacion completa, se necesita acceso a los mensajes originales
    return md;
}

QString AIChatWidget::exportToJSON() const
{
    QJsonObject root;
    root["export_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["application"] = "POWSYS365";
    root["version"] = "1.0";

    QJsonArray messages;
    root["messages"] = messages;

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

// ============================================================================
// AIHistoryPanel
// ============================================================================

AIHistoryPanel::AIHistoryPanel(QWidget* parent)
    : QFrame(parent)
{
    setupUI();
}

AIHistoryPanel::~AIHistoryPanel() = default;

void AIHistoryPanel::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Search
    search_edit_ = new QLineEdit(this);
    search_edit_->setPlaceholderText(tr("Search conversations..."));
    layout->addWidget(search_edit_);

    // New conversation button
    new_conv_btn_ = new QPushButton(tr("+ New Chat"), this);
    layout->addWidget(new_conv_btn_);

    // History list
    history_list_ = new QListWidget(this);
    layout->addWidget(history_list_, 1);

    connect(new_conv_btn_, &QPushButton::clicked, this, &AIHistoryPanel::newConversationRequested);
    connect(history_list_, &QListWidget::itemClicked, [this](QListWidgetItem* item) {
        if (item) emit conversationSelected(item->data(Qt::UserRole).toString());
    });
    connect(search_edit_, &QLineEdit::textChanged, this, &AIHistoryPanel::searchTextChanged);
}

void AIHistoryPanel::addConversation(const Conversation& conv)
{
    conversations_.push_back(conv);

    auto* item = new QListWidgetItem(history_list_);
    item->setText(conv.title.isEmpty() ? tr("New Conversation") : conv.title);
    item->setData(Qt::UserRole, conv.id);
    item->setToolTip(QString("%1 messages | $%2")
        .arg(conv.messages.size()).arg(conv.total_cost_usd, 0, 'f', 4));
    if (conv.is_pinned) {
        item->setIcon(QIcon::fromTheme("pin"));
    }
    history_list_->insertItem(0, item);
}

void AIHistoryPanel::removeConversation(const QString& conv_id)
{
    conversations_.erase(
        std::remove_if(conversations_.begin(), conversations_.end(),
            [&](const Conversation& c) { return c.id == conv_id.toStdString(); }),
        conversations_.end()
    );

    for (int i = 0; i < history_list_->count(); ++i) {
        if (history_list_->item(i)->data(Qt::UserRole).toString() == conv_id) {
            delete history_list_->takeItem(i);
            break;
        }
    }
}

void AIHistoryPanel::updateConversation(const Conversation& conv)
{
    for (auto& c : conversations_) {
        if (c.id == conv.id) {
            c = conv;
            break;
        }
    }

    for (int i = 0; i < history_list_->count(); ++i) {
        auto* item = history_list_->item(i);
        if (item->data(Qt::UserRole).toString() == QString::fromStdString(conv.id)) {
            item->setText(conv.title.isEmpty() ? tr("New Conversation") : conv.title);
            break;
        }
    }
}

void AIHistoryPanel::clearHistory()
{
    conversations_.clear();
    history_list_->clear();
}

std::vector<Conversation> AIHistoryPanel::getConversations() const
{
    return conversations_;
}

void AIHistoryPanel::setConversations(const std::vector<Conversation>& conversations)
{
    clearHistory();
    for (const auto& conv : conversations) {
        addConversation(conv);
    }
}

// ============================================================================
// AIProviderPanel
// ============================================================================

AIProviderPanel::AIProviderPanel(QWidget* parent)
    : QFrame(parent)
{
    setupUI();
    loadDefaults();
}

AIProviderPanel::~AIProviderPanel() = default;

void AIProviderPanel::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    // Provider
    layout->addWidget(new QLabel(tr("Provider:")));
    provider_combo_ = new QComboBox(this);
    provider_combo_->addItems({"DeepSeek", "Kimi", "GPT-4o", "Claude", "Llama", "Gemini", "Custom"});
    layout->addWidget(provider_combo_);

    // Model
    layout->addWidget(new QLabel(tr("Model:")));
    model_combo_ = new QComboBox(this);
    model_combo_->setEditable(true);
    layout->addWidget(model_combo_);

    // API Key
    layout->addWidget(new QLabel(tr("API Key:")));
    api_key_edit_ = new QLineEdit(this);
    api_key_edit_->setEchoMode(QLineEdit::Password);
    layout->addWidget(api_key_edit_);

    // API URL
    layout->addWidget(new QLabel(tr("API Base URL:")));
    api_url_edit_ = new QLineEdit(this);
    layout->addWidget(api_url_edit_);

    // Temperature
    layout->addWidget(new QLabel(tr("Temperature:")));
    temperature_edit_ = new QLineEdit(this);
    temperature_edit_->setText("0.7");
    layout->addWidget(temperature_edit_);

    // Max tokens
    layout->addWidget(new QLabel(tr("Max Tokens:")));
    max_tokens_edit_ = new QLineEdit(this);
    max_tokens_edit_->setText("4096");
    layout->addWidget(max_tokens_edit_);

    // Cost info
    cost_label_ = new QLabel(tr("Cost: $0.00 / 1K tokens"), this);
    cost_label_->setStyleSheet("color: #718096; font-size: 11px;");
    layout->addWidget(cost_label_);

    layout->addStretch();

    connect(provider_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        (void)idx;
        // Actualizar modelos disponibles
        QString provider = provider_combo_->currentText();
        model_combo_->clear();
        if (provider == "DeepSeek") {
            model_combo_->addItems({"deepseek-chat", "deepseek-reasoner"});
        } else if (provider == "GPT-4o") {
            model_combo_->addItems({"gpt-4o", "gpt-4o-mini", "gpt-4-turbo"});
        } else if (provider == "Claude") {
            model_combo_->addItems({"claude-3-5-sonnet", "claude-3-opus", "claude-3-haiku"});
        } else if (provider == "Kimi") {
            model_combo_->addItems({"moonshot-v1-128k", "moonshot-v1-32k", "moonshot-v1-8k"});
        } else if (provider == "Gemini") {
            model_combo_->addItems({"gemini-1.5-pro", "gemini-1.5-flash"});
        } else if (provider == "Llama") {
            model_combo_->addItems({"llama-3.1-70b", "llama-3.1-8b", "llama-3-70b"});
        }
    });
}

void AIProviderPanel::setProviderConfig(const LLMProviderConfig& config)
{
    provider_combo_->setCurrentText(providerToString(config.provider));
    model_combo_->setCurrentText(config.default_model);
    api_key_edit_->setText(config.api_key);
    api_url_edit_->setText(config.api_base_url);
    temperature_edit_->setText(QString::number(config.temperature));
    max_tokens_edit_->setText(QString::number(config.max_tokens));
    cost_label_->setText(QString("In: $%1/1K | Out: $%2/1K")
        .arg(config.cost_per_1k_input_tokens, 0, 'f', 4)
        .arg(config.cost_per_1k_output_tokens, 0, 'f', 4));
}

LLMProviderConfig AIProviderPanel::getProviderConfig() const
{
    LLMProviderConfig config;
    config.provider = stringToProvider(provider_combo_->currentText());
    config.default_model = model_combo_->currentText();
    config.api_key = api_key_edit_->text();
    config.api_base_url = api_url_edit_->text();
    config.temperature = temperature_edit_->text().toDouble();
    config.max_tokens = max_tokens_edit_->text().toDouble();
    config.name = provider_combo_->currentText();

    // Costos por proveedor
    switch (config.provider) {
        case LLMProvider::GPT4o:
            config.cost_per_1k_input_tokens = 0.005;
            config.cost_per_1k_output_tokens = 0.015;
            break;
        case LLMProvider::Claude:
            config.cost_per_1k_input_tokens = 0.003;
            config.cost_per_1k_output_tokens = 0.015;
            break;
        case LLMProvider::DeepSeek:
            config.cost_per_1k_input_tokens = 0.00014;
            config.cost_per_1k_output_tokens = 0.00028;
            break;
        case LLMProvider::Kimi:
            config.cost_per_1k_input_tokens = 0.003;
            config.cost_per_1k_output_tokens = 0.003;
            break;
        case LLMProvider::Gemini:
            config.cost_per_1k_input_tokens = 0.00125;
            config.cost_per_1k_output_tokens = 0.005;
            break;
        default:
            break;
    }

    return config;
}

void AIProviderPanel::loadDefaults()
{
    provider_combo_->setCurrentText("DeepSeek");
    emit configChanged(getProviderConfig());
}

// ============================================================================
// AIStatsPanel
// ============================================================================

AIStatsPanel::AIStatsPanel(QWidget* parent)
    : QFrame(parent)
{
    setupUI();
}

AIStatsPanel::~AIStatsPanel() = default;

void AIStatsPanel::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    model_label_ = new QLabel(tr("Model: -"), this);
    layout->addWidget(model_label_);

    tokens_input_label_ = new QLabel(tr("Input tokens: 0"), this);
    layout->addWidget(tokens_input_label_);

    tokens_output_label_ = new QLabel(tr("Output tokens: 0"), this);
    layout->addWidget(tokens_output_label_);

    total_tokens_label_ = new QLabel(tr("Total: 0"), this);
    layout->addWidget(total_tokens_label_);

    cost_label_ = new QLabel(tr("Cost: $0.0000"), this);
    layout->addWidget(cost_label_);

    token_budget_bar_ = new QProgressBar(this);
    token_budget_bar_->setRange(0, 100000);
    token_budget_bar_->setValue(0);
    token_budget_bar_->setFormat("%v / %m tokens");
    layout->addWidget(token_budget_bar_);

    layout->addStretch();
}

void AIStatsPanel::updateStats(const Conversation& conv)
{
    model_label_->setText(QString("Model: %1").arg(QString::fromStdString(conv.model_used)));
    tokens_input_label_->setText(QString("Input: %1").arg(conv.total_input_tokens));
    tokens_output_label_->setText(QString("Output: %1").arg(conv.total_output_tokens));
    total_tokens_label_->setText(QString("Total: %1").arg(
        conv.total_input_tokens + conv.total_output_tokens));
    cost_label_->setText(QString("Cost: $%1").arg(conv.total_cost_usd, 0, 'f', 4));
}

void AIStatsPanel::updateSessionStats(int total_input, int total_output, double total_cost)
{
    tokens_input_label_->setText(QString("Input: %1").arg(total_input));
    tokens_output_label_->setText(QString("Output: %1").arg(total_output));
    total_tokens_label_->setText(QString("Total: %1").arg(total_input + total_output));
    cost_label_->setText(QString("Cost: $%1").arg(total_cost, 0, 'f', 4));
    token_budget_bar_->setValue(total_input + total_output);
}

void AIStatsPanel::clearStats()
{
    model_label_->setText(tr("Model: -"));
    tokens_input_label_->setText(tr("Input: 0"));
    tokens_output_label_->setText(tr("Output: 0"));
    total_tokens_label_->setText(tr("Total: 0"));
    cost_label_->setText(tr("Cost: $0.0000"));
    token_budget_bar_->setValue(0);
}

// ============================================================================
// AIToolWindow
// ============================================================================

AIToolWindow::AIToolWindow(QWidget* parent)
    : QDockWidget(tr("AI Assistant - POWSYS365"), parent)
{
    setObjectName("AIToolWindow");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                     Qt::BottomDockWidgetArea);
    setMinimumWidth(400);
    setMinimumHeight(500);

    setupUI();
    setupConnections();
    setupProviders();
    setupToolbar();

    network_manager_ = new QNetworkAccessManager(this);

    startNewConversation();
}

AIToolWindow::~AIToolWindow()
{
    if (current_reply_) {
        current_reply_->abort();
        current_reply_->deleteLater();
    }
}

void AIToolWindow::setupUI()
{
    auto* central = new QFrame(this);
    auto* main_layout = new QHBoxLayout(central);
    main_layout->setContentsMargins(4, 4, 4, 4);
    main_layout->setSpacing(4);

    // Splitter principal
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Panel izquierdo: historial + configuracion
    side_panel_ = new QTabWidget(this);
    side_panel_->setMaximumWidth(280);
    side_panel_->setMinimumWidth(180);

    history_panel_ = new AIHistoryPanel(this);
    provider_panel_ = new AIProviderPanel(this);
    stats_panel_ = new AIStatsPanel(this);

    side_panel_->addTab(history_panel_, tr("History"));
    side_panel_->addTab(provider_panel_, tr("Provider"));
    side_panel_->addTab(stats_panel_, tr("Stats"));

    splitter->addWidget(side_panel_);

    // Panel derecho: chat + prompt
    auto* right_panel = new QFrame(this);
    auto* right_layout = new QVBoxLayout(right_panel);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(4);

    // Status bar
    auto* status_layout = new QHBoxLayout();
    status_label_ = new QLabel(tr("Ready"), right_panel);
    status_label_->setStyleSheet("color: #718096; font-size: 11px;");
    status_layout->addWidget(status_label_);

    progress_bar_ = new QProgressBar(right_panel);
    progress_bar_->setMaximumWidth(120);
    progress_bar_->setMaximumHeight(14);
    progress_bar_->setRange(0, 0);
    progress_bar_->setVisible(false);
    status_layout->addWidget(progress_bar_);
    status_layout->addStretch();

    // Provider selector
    provider_selector_ = new QComboBox(right_panel);
    provider_selector_->addItems({"DeepSeek", "Kimi", "GPT-4o", "Claude", "Llama", "Gemini"});
    provider_selector_->setMaximumWidth(120);
    status_layout->addWidget(new QLabel(tr("Model:")));
    status_layout->addWidget(provider_selector_);
    right_layout->addLayout(status_layout);

    // Chat widget
    chat_widget_ = new AIChatWidget(right_panel);
    right_layout->addWidget(chat_widget_, 1);

    // Prompt widget
    prompt_widget_ = new AIPromptWidget(right_panel);
    prompt_widget_->setMaximumHeight(300);
    right_layout->addWidget(prompt_widget_);

    splitter->addWidget(right_panel);
    splitter->setSizes(QList<int>{220, 600});

    main_layout->addWidget(splitter);
    setWidget(central);
}

void AIToolWindow::setupConnections()
{
    connect(prompt_widget_, &AIPromptWidget::sendRequested,
            this, [this](const QString& text, const QList<PromptAttachment>& attachments) {
        (void)attachments;
        sendMessage(text);
    });

    connect(history_panel_, &AIHistoryPanel::conversationSelected,
            this, &AIToolWindow::onHistoryItemSelected);
    connect(history_panel_, &AIHistoryPanel::newConversationRequested,
            this, &AIToolWindow::onNewConversation);

    connect(provider_panel_, &AIProviderPanel::configChanged,
            this, [this](const LLMProviderConfig& config) {
        provider_configs_[config.provider] = config;
    });

    connect(provider_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AIToolWindow::onProviderChanged);

    connect(network_manager_, &QNetworkAccessManager::finished,
            this, &AIToolWindow::handleNetworkReply);
}

void AIToolWindow::setupProviders()
{
    // DeepSeek (default)
    LLMProviderConfig deepseek;
    deepseek.provider = LLMProvider::DeepSeek;
    deepseek.name = "DeepSeek";
    deepseek.api_base_url = "https://api.deepseek.com/v1/chat/completions";
    deepseek.default_model = "deepseek-chat";
    deepseek.available_models = {"deepseek-chat", "deepseek-reasoner"};
    deepseek.max_tokens = 4096;
    deepseek.temperature = 0.7;
    deepseek.cost_per_1k_input_tokens = 0.00014;
    deepseek.cost_per_1k_output_tokens = 0.00028;
    deepseek.supports_streaming = true;
    provider_configs_[LLMProvider::DeepSeek] = deepseek;

    // GPT-4o
    LLMProviderConfig gpt4o;
    gpt4o.provider = LLMProvider::GPT4o;
    gpt4o.name = "GPT-4o";
    gpt4o.api_base_url = "https://api.openai.com/v1/chat/completions";
    gpt4o.default_model = "gpt-4o";
    gpt4o.available_models = {"gpt-4o", "gpt-4o-mini", "gpt-4-turbo"};
    gpt4o.max_tokens = 4096;
    gpt4o.temperature = 0.7;
    gpt4o.cost_per_1k_input_tokens = 0.005;
    gpt4o.cost_per_1k_output_tokens = 0.015;
    gpt4o.supports_streaming = true;
    gpt4o.supports_vision = true;
    provider_configs_[LLMProvider::GPT4o] = gpt4o;

    // Claude
    LLMProviderConfig claude;
    claude.provider = LLMProvider::Claude;
    claude.name = "Claude";
    claude.api_base_url = "https://api.anthropic.com/v1/messages";
    claude.default_model = "claude-3-5-sonnet-20241022";
    claude.available_models = {"claude-3-5-sonnet", "claude-3-opus", "claude-3-haiku"};
    claude.max_tokens = 4096;
    claude.temperature = 0.7;
    claude.cost_per_1k_input_tokens = 0.003;
    claude.cost_per_1k_output_tokens = 0.015;
    claude.supports_streaming = true;
    claude.supports_vision = true;
    provider_configs_[LLMProvider::Claude] = claude;

    // Kimi
    LLMProviderConfig kimi;
    kimi.provider = LLMProvider::Kimi;
    kimi.name = "Kimi";
    kimi.api_base_url = "https://api.moonshot.cn/v1/chat/completions";
    kimi.default_model = "moonshot-v1-128k";
    kimi.available_models = {"moonshot-v1-128k", "moonshot-v1-32k", "moonshot-v1-8k"};
    kimi.max_tokens = 4096;
    kimi.temperature = 0.7;
    kimi.cost_per_1k_input_tokens = 0.003;
    kimi.cost_per_1k_output_tokens = 0.003;
    kimi.supports_streaming = true;
    provider_configs_[LLMProvider::Kimi] = kimi;

    // Llama
    LLMProviderConfig llama;
    llama.provider = LLMProvider::Llama;
    llama.name = "Llama";
    llama.api_base_url = "http://localhost:11434/v1/chat/completions";
    llama.default_model = "llama-3.1-70b";
    llama.available_models = {"llama-3.1-70b", "llama-3.1-8b", "llama-3-70b"};
    llama.max_tokens = 4096;
    llama.temperature = 0.7;
    llama.cost_per_1k_input_tokens = 0.0;
    llama.cost_per_1k_output_tokens = 0.0;
    llama.supports_streaming = true;
    provider_configs_[LLMProvider::Llama] = llama;

    // Gemini
    LLMProviderConfig gemini;
    gemini.provider = LLMProvider::Gemini;
    gemini.name = "Gemini";
    gemini.api_base_url = "https://generativelanguage.googleapis.com/v1beta/models";
    gemini.default_model = "gemini-1.5-pro";
    gemini.available_models = {"gemini-1.5-pro", "gemini-1.5-flash"};
    gemini.max_tokens = 4096;
    gemini.temperature = 0.7;
    gemini.cost_per_1k_input_tokens = 0.00125;
    gemini.cost_per_1k_output_tokens = 0.005;
    gemini.supports_streaming = true;
    gemini.supports_vision = true;
    provider_configs_[LLMProvider::Gemini] = gemini;

    active_provider_ = LLMProvider::DeepSeek;
}

void AIToolWindow::setupToolbar()
{
    toolbar_ = new QToolBar(this);

    new_conv_btn_ = new QPushButton(QIcon::fromTheme("document-new"), "", this);
    new_conv_btn_->setToolTip(tr("New Conversation"));
    toolbar_->addWidget(new_conv_btn_);

    export_btn_ = new QPushButton(QIcon::fromTheme("document-save"), "", this);
    export_btn_->setToolTip(tr("Export Conversation"));
    toolbar_->addWidget(export_btn_);

    settings_btn_ = new QPushButton(QIcon::fromTheme("preferences-system"), "", this);
    settings_btn_->setToolTip(tr("Settings"));
    toolbar_->addWidget(settings_btn_);

    connect(new_conv_btn_, &QPushButton::clicked, this, &AIToolWindow::onNewConversation);
    connect(export_btn_, &QPushButton::clicked, this, &AIToolWindow::onExportMarkdown);
    connect(settings_btn_, &QPushButton::clicked, this, &AIToolWindow::onSettingsClicked);
}

QString AIToolWindow::generateConversationId() const
{
    return QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz")
           + "_" + QString::number(QRandomGenerator::global()->bounded(10000));
}

void AIToolWindow::startNewConversation()
{
    saveConversationToHistory();

    Conversation conv;
    conv.id = generateConversationId().toStdString();
    conv.created_at = QDateTime::currentDateTime();
    conv.updated_at = QDateTime::currentDateTime();
    conv.provider = active_provider_;
    conv.title = tr("New Conversation");

    conversations_.push_back(conv);
    current_conversation_id_ = QString::fromStdString(conv.id);
    history_panel_->addConversation(conv);
    chat_widget_->clearChat();

    // System message
    ChatMessage sys_msg;
    sys_msg.id = "system_" + generateConversationId();
    sys_msg.role = "system";
    sys_msg.content = buildSystemMessage();
    sys_msg.timestamp = QDateTime::currentDateTime();
    conv.messages.push_back(sys_msg);

    stats_panel_->clearStats();
    status_label_->setText(tr("New conversation started"));
}

void AIToolWindow::loadConversation(const QString& conv_id)
{
    for (const auto& conv : conversations_) {
        if (QString::fromStdString(conv.id) == conv_id) {
            current_conversation_id_ = conv_id;
            chat_widget_->clearChat();
            for (const auto& msg : conv.messages) {
                chat_widget_->appendMessage(msg);
            }
            stats_panel_->updateStats(conv);
            break;
        }
    }
}

void AIToolWindow::sendMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) return;
    if (is_streaming_) return;

    is_streaming_ = true;
    prompt_widget_->setTypingIndicator(true);
    progress_bar_->setVisible(true);
    status_label_->setText(tr("Sending..."));

    // Auto-seleccion de proveedor
    LLMProvider selected_provider = autoSelectProvider(message);
    if (selected_provider != active_provider_) {
        active_provider_ = selected_provider;
        int idx = provider_selector_->findText(providerToString(selected_provider));
        if (idx >= 0) provider_selector_->setCurrentIndex(idx);
    }

    // Crear mensaje de usuario
    ChatMessage user_msg;
    user_msg.id = "user_" + generateConversationId();
    user_msg.role = "user";
    user_msg.content = message;
    user_msg.timestamp = QDateTime::currentDateTime();
    user_msg.provider = active_provider_;
    user_msg.input_tokens = estimateTokens(message);

    // Agregar contexto RAG si aplica
    QString enhanced_message = message;
    if (!rag_context_.isEmpty()) {
        QString rag = queryStandardsRAG(message);
        if (!rag.isEmpty()) {
            enhanced_message = QString("Context:\n%1\n\nQuestion:\n%2").arg(rag, message);
        }
    }
    if (!project_context_.isEmpty()) {
        enhanced_message = QString("Project Context:\n%1\n\n%2")
            .arg(project_context_, enhanced_message);
    }
    user_msg.content = enhanced_message;

    chat_widget_->appendMessage(user_msg);

    // Guardar en conversacion
    for (auto& conv : conversations_) {
        if (QString::fromStdString(conv.id) == current_conversation_id_) {
            conv.messages.push_back(user_msg);
            conv.total_input_tokens += user_msg.input_tokens;
            break;
        }
    }

    // Enviar request
    sendStreamingRequest(user_msg);
}

void AIToolWindow::sendMessageWithAttachments(const QString& message,
                                                const QStringList& file_paths)
{
    (void)file_paths;
    sendMessage(message);
}

void AIToolWindow::sendStreamingRequest(const ChatMessage& message)
{
    if (!provider_configs_.contains(active_provider_)) return;

    auto config = provider_configs_[active_provider_];

    QNetworkRequest request;
    request.setUrl(QUrl(config.api_base_url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + config.api_key.toUtf8());

    // Headers adicionales por proveedor
    if (active_provider_ == LLMProvider::Claude) {
        request.setRawHeader("x-api-key", config.api_key.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
        request.setRawHeader("anthropic-dangerous-direct-browser-access", "true");
    }

    QJsonObject payload = buildRequestPayload(message);
    QJsonDocument doc(payload);

    current_reply_ = network_manager_->post(request, doc.toJson());

    connect(current_reply_, &QNetworkReply::readyRead, this, [this]() {
        if (!current_reply_) return;
        QByteArray data = current_reply_->readAll();
        QString buffer;
        processSSEChunk(data, buffer);
    });

    connect(current_reply_, &QNetworkReply::finished, this, [this]() {
        progress_bar_->setVisible(false);
        is_streaming_ = false;
        prompt_widget_->setTypingIndicator(false);
        status_label_->setText(tr("Response complete"));

        if (current_reply_) {
            current_reply_->deleteLater();
            current_reply_ = nullptr;
        }
    });

    connect(current_reply_, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError err) {
        (void)err;
        if (current_reply_) {
            QString error = current_reply_->errorString();
            status_label_->setText(tr("Error: %1").arg(error));
            emit errorOccurred(error);
            is_streaming_ = false;
            prompt_widget_->setTypingIndicator(false);
            progress_bar_->setVisible(false);
        }
    });
}

QJsonObject AIToolWindow::buildRequestPayload(const ChatMessage& message) const
{
    QJsonObject payload;

    auto it = provider_configs_.find(active_provider_);
    if (it == provider_configs_.end()) return payload;

    auto config = it.value();

    if (active_provider_ == LLMProvider::Claude) {
        // Formato Anthropic
        payload["model"] = config.default_model;
        payload["max_tokens"] = static_cast<int>(config.max_tokens);

        QJsonArray messages;
        messages.append(QJsonObject{
            {"role", "user"},
            {"content", message.content}
        });
        payload["messages"] = messages;

        // System message
        QString sys_msg = buildSystemMessage();
        payload["system"] = sys_msg;
        payload["stream"] = true;
    } else {
        // Formato OpenAI-compatible (DeepSeek, GPT-4o, Kimi, Llama)
        payload["model"] = config.default_model;
        payload["temperature"] = config.temperature;
        payload["max_tokens"] = static_cast<int>(config.max_tokens);
        payload["top_p"] = config.top_p;
        payload["stream"] = true;

        QJsonArray messages;

        // System message
        messages.append(QJsonObject{
            {"role", "system"},
            {"content", buildSystemMessage()}
        });

        // Contexto del proyecto
        if (!project_context_.isEmpty()) {
            messages.append(QJsonObject{
                {"role", "system"},
                {"content", "Project Context:\n" + project_context_}
            });
        }

        // Historial de conversacion (ultimos 10 mensajes)
        for (const auto& conv : conversations_) {
            if (QString::fromStdString(conv.id) == current_conversation_id_) {
                int start = std::max(0, static_cast<int>(conv.messages.size()) - 10);
                for (size_t i = start; i < conv.messages.size(); ++i) {
                    const auto& msg = conv.messages[i];
                    if (msg.role != "system") {
                        messages.append(QJsonObject{
                            {"role", QString::fromStdString(msg.role)},
                            {"content", msg.content}
                        });
                    }
                }
                break;
            }
        }

        // Mensaje actual
        messages.append(QJsonObject{
            {"role", "user"},
            {"content", message.content}
        });

        payload["messages"] = messages;
    }

    // Merge extra params
    for (auto it_p = config.extra_params.begin(); it_p != config.extra_params.end(); ++it_p) {
        payload[it_p.key()] = it_p.value();
    }

    return payload;
}

void AIToolWindow::processSSEChunk(const QByteArray& chunk, QString& buffer)
{
    buffer += QString::fromUtf8(chunk);

    // Procesar lineas SSE: data: {...}
    QStringList lines = buffer.split("\n");
    buffer.clear();

    for (const QString& line : lines) {
        if (line.startsWith("data: ")) {
            QString data = line.mid(6);
            if (data == "[DONE]") {
                continue;
            }

            QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
            if (!doc.isObject()) {
                buffer = line; // Acumular linea incompleta
                continue;
            }

            QJsonObject obj = doc.object();
            QString content;

            // DeepSeek / OpenAI format
            if (obj.contains("choices")) {
                QJsonArray choices = obj["choices"].toArray();
                if (!choices.isEmpty()) {
                    QJsonObject delta = choices[0].toObject()["delta"].toObject();
                    content = delta["content"].toString();
                }
            }
            // Claude format
            else if (obj.contains("type") && obj["type"].toString() == "content_block_delta") {
                QJsonObject delta = obj["delta"].toObject();
                content = delta["text"].toString();
            }
            else if (obj.contains("delta")) {
                QJsonObject delta = obj["delta"].toObject();
                content = delta["text"].toString();
            }

            if (!content.isEmpty()) {
                QString msg_id = "assistant_" + current_conversation_id_;
                chat_widget_->appendStreamingChunk(content, msg_id);
            }
        } else if (!line.isEmpty()) {
            buffer = line; // Acumular linea incompleta
        }
    }
}

void AIToolWindow::handleNetworkReply(QNetworkReply* reply)
{
    if (reply != current_reply_) return;
    if (reply->error() != QNetworkReply::NoError) return;

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject response = doc.object();
    QString content = extractResponseContent(response);

    if (!content.isEmpty()) {
        ChatMessage assistant_msg;
        assistant_msg.id = "assistant_" + generateConversationId();
        assistant_msg.role = "assistant";
        assistant_msg.content = content;
        assistant_msg.timestamp = QDateTime::currentDateTime();
        assistant_msg.provider = active_provider_;
        assistant_msg.model_used = providerToString(active_provider_).toStdString();
        assistant_msg.output_tokens = estimateTokens(content);

        // Cost estimation
        auto it = provider_configs_.find(active_provider_);
        if (it != provider_configs_.end()) {
            assistant_msg.estimated_cost_usd =
                (assistant_msg.input_tokens * it.value().cost_per_1k_input_tokens +
                 assistant_msg.output_tokens * it.value().cost_per_1k_output_tokens) / 1000.0;
        }

        chat_widget_->appendMessage(assistant_msg);

        // Guardar en conversacion
        for (auto& conv : conversations_) {
            if (QString::fromStdString(conv.id) == current_conversation_id_) {
                conv.messages.push_back(assistant_msg);
                conv.total_output_tokens += assistant_msg.output_tokens;
                conv.total_cost_usd += assistant_msg.estimated_cost_usd;
                conv.model_used = assistant_msg.model_used;
                break;
            }
        }

        // Actualizar stats
        session_input_tokens_ += assistant_msg.input_tokens;
        session_output_tokens_ += assistant_msg.output_tokens;
        session_cost_usd_ += assistant_msg.estimated_cost_usd;
        stats_panel_->updateSessionStats(session_input_tokens_, session_output_tokens_,
                                          session_cost_usd_);

        emit assistantResponseReceived(content);
    }

    // Finalizar streaming
    QString msg_id = "assistant_" + current_conversation_id_;
    chat_widget_->finalizeStreamingMessage(msg_id);
}

QString AIToolWindow::extractResponseContent(const QJsonObject& response) const
{
    // OpenAI / DeepSeek format
    if (response.contains("choices")) {
        QJsonArray choices = response["choices"].toArray();
        if (!choices.isEmpty()) {
            QJsonObject choice = choices[0].toObject();
            QJsonObject message = choice["message"].toObject();
            return message["content"].toString();
        }
    }
    // Claude format
    if (response.contains("content")) {
        QJsonArray content = response["content"].toArray();
        if (!content.isEmpty()) {
            return content[0].toObject()["text"].toString();
        }
    }
    return QString();
}

QString AIToolWindow::buildSystemMessage() const
{
    return QString(
        "You are POWSYS365 AI, an expert assistant specialized in power systems engineering, "
        "electrical grid analysis, reliability assessment, state estimation, and power flow computation.\n\n"
        "Your capabilities include:\n"
        "- Power system analysis and simulation\n"
        "- Reliability indices (SAIDI, SAIFI, CAIDI, LOLP, EENS)\n"
        "- Weighted Least Squares state estimation\n"
        "- Power flow computation (Newton-Raphson, Gauss-Seidel)\n"
        "- IEEE and IEC standards compliance\n"
        "- C++ code generation for power systems\n"
        "- Monte Carlo reliability simulation\n\n"
        "Always provide accurate, technically precise responses. "
        "When generating code, use C++17 with Eigen3 for linear algebra. "
        "Use namespace powsys365 for all components."
    );
}

LLMProvider AIToolWindow::autoSelectProvider(const QString& prompt) const
{
    // Heuristicas de seleccion automatica
    QString lower = prompt.toLower();

    // Vision / image analysis -> GPT-4o o Claude
    if (lower.contains("image") || lower.contains("diagram") || lower.contains("figure")) {
        return LLMProvider::GPT4o;
    }

    // Long context -> Kimi (128k) o Gemini
    if (prompt.length() > 8000) {
        return LLMProvider::Kimi;
    }

    // Code generation -> DeepSeek (mejor para codigo)
    if (lower.contains("code") || lower.contains("c++") || lower.contains("implement") ||
        lower.contains("function") || lower.contains("class")) {
        return LLMProvider::DeepSeek;
    }

    // Complex reasoning -> Claude
    if (lower.contains("analyze") || lower.contains("explain") || lower.contains("why")) {
        return LLMProvider::Claude;
    }

    // Cost-sensitive -> DeepSeek (mas barato)
    return active_provider_;
}

int AIToolWindow::estimateTokens(const QString& text) const
{
    int total = 0;
    for (const QChar& c : text) {
        ushort unicode = c.unicode();
        if (unicode >= 0x4E00 && unicode <= 0x9FFF) {
            total += 2;
        } else if (unicode >= 0x0600 && unicode <= 0x06FF) {
            total += 2;
        } else {
            total += 1;
        }
    }
    return std::max(1, total / 4);
}

double AIToolWindow::estimateQueryCost(const QString& text, LLMProvider provider) const
{
    auto it = provider_configs_.find(provider);
    if (it == provider_configs_.end()) return 0.0;

    int tokens = estimateTokens(text);
    double input_cost = tokens * it.value().cost_per_1k_input_tokens / 1000.0;
    // Estimacion conservadora: output = 2x input
    double output_cost = tokens * 2 * it.value().cost_per_1k_output_tokens / 1000.0;
    return input_cost + output_cost;
}

void AIToolWindow::setProjectContext(const QString& context)
{
    project_context_ = context;
}

void AIToolWindow::setRAGContext(const QString& standard_code)
{
    rag_context_ = standard_code;
}

void AIToolWindow::exportCurrentConversationToMarkdown(const QString& file_path)
{
    QString md = chat_widget_->exportToMarkdown();

    for (const auto& conv : conversations_) {
        if (QString::fromStdString(conv.id) != current_conversation_id_) continue;

        md += QString("# %1\n\n").arg(conv.title.isEmpty() ? "Conversation" : conv.title);
        md += QString("_Provider: %1 | Model: %2 | Date: %3_\n\n")
            .arg(providerToString(conv.provider))
            .arg(QString::fromStdString(conv.model_used))
            .arg(conv.created_at.toString(Qt::ISODate));
        md += "---\n\n";

        for (const auto& msg : conv.messages) {
            if (msg.role == "system") continue;
            md += QString("**%1** (%2)\n\n")
                .arg(QString::fromStdString(msg.role))
                .arg(msg.timestamp.toString("hh:mm:ss"));
            md += msg.content + "\n\n---\n\n";
        }

        md += QString("\n_Total Input: %1 tokens | Total Output: %2 tokens | Cost: $%3_\n")
            .arg(conv.total_input_tokens)
            .arg(conv.total_output_tokens)
            .arg(conv.total_cost_usd, 0, 'f', 4);
    }

    QFile file(file_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << md;
        file.close();
    }
}

void AIToolWindow::exportCurrentConversationToJSON(const QString& file_path)
{
    QJsonObject root;
    root["export_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["application"] = "POWSYS365";

    for (const auto& conv : conversations_) {
        if (QString::fromStdString(conv.id) != current_conversation_id_) continue;

        QJsonObject conv_obj;
        conv_obj["id"] = QString::fromStdString(conv.id);
        conv_obj["title"] = conv.title;
        conv_obj["provider"] = providerToString(conv.provider);
        conv_obj["model"] = QString::fromStdString(conv.model_used);
        conv_obj["created_at"] = conv.created_at.toString(Qt::ISODate);
        conv_obj["total_input_tokens"] = conv.total_input_tokens;
        conv_obj["total_output_tokens"] = conv.total_output_tokens;
        conv_obj["total_cost_usd"] = conv.total_cost_usd;

        QJsonArray messages;
        for (const auto& msg : conv.messages) {
            QJsonObject msg_obj;
            msg_obj["id"] = msg.id;
            msg_obj["role"] = QString::fromStdString(msg.role);
            msg_obj["content"] = msg.content;
            msg_obj["timestamp"] = msg.timestamp.toString(Qt::ISODate);
            msg_obj["input_tokens"] = msg.input_tokens;
            msg_obj["output_tokens"] = msg.output_tokens;
            msg_obj["estimated_cost"] = msg.estimated_cost_usd;
            messages.append(msg_obj);
        }
        conv_obj["messages"] = messages;
        root["conversation"] = conv_obj;
        break;
    }

    QJsonDocument doc(root);
    QFile file(file_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void AIToolWindow::exportAllConversationsToJSON(const QString& file_path)
{
    QJsonObject root;
    root["export_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["application"] = "POWSYS365";

    QJsonArray convs;
    for (const auto& conv : conversations_) {
        QJsonObject conv_obj;
        conv_obj["id"] = QString::fromStdString(conv.id);
        conv_obj["title"] = conv.title;
        conv_obj["provider"] = providerToString(conv.provider);
        conv_obj["model"] = QString::fromStdString(conv.model_used);
        conv_obj["message_count"] = static_cast<int>(conv.messages.size());
        conv_obj["total_cost"] = conv.total_cost_usd;
        convs.append(conv_obj);
    }
    root["conversations"] = convs;
    root["total_conversations"] = static_cast<int>(conversations_.size());

    QJsonDocument doc(root);
    QFile file(file_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void AIToolWindow::saveSession(const QString& file_path)
{
    exportAllConversationsToJSON(file_path);
}

void AIToolWindow::loadSession(const QString& file_path)
{
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray convs = root["conversations"].toArray();

    conversations_.clear();
    for (const auto& conv_val : convs) {
        QJsonObject conv_obj = conv_val.toObject();
        Conversation conv;
        conv.id = conv_obj["id"].toString().toStdString();
        conv.title = conv_obj["title"].toString();
        conv.provider = stringToProvider(conv_obj["provider"].toString());
        conv.model_used = conv_obj["model"].toString().toStdString();
        conv.total_cost_usd = conv_obj["total_cost"].toDouble();
        conversations_.push_back(conv);
        history_panel_->addConversation(conv);
    }
}

void AIToolWindow::loadStandardsDatabase(const QString& directory)
{
    QDir dir(directory);
    QStringList files = dir.entryList({"*.md", "*.txt", "*.json"}, QDir::Files);

    for (const QString& file : files) {
        StandardDocument doc;
        doc.id = file;
        doc.title = file;

        QFile f(dir.absoluteFilePath(file));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            doc.content = QString::fromUtf8(f.readAll());
            f.close();
        }

        // Extraer codigo de estandar del nombre
        if (file.contains("IEEE")) {
            doc.code = "IEEE";
        } else if (file.contains("IEC")) {
            doc.code = "IEC";
        } else if (file.contains("NESC")) {
            doc.code = "NESC";
        }

        standards_db_.push_back(doc);
    }
}

QString AIToolWindow::queryStandardsRAG(const QString& query) const
{
    if (standards_db_.empty()) return QString();

    // Simple keyword matching (para RAG completo usar embeddings)
    QString result;
    for (const auto& doc : standards_db_) {
        if (doc.content.contains(query, Qt::CaseInsensitive) ||
            query.contains(doc.code, Qt::CaseInsensitive)) {
            result += QString("### %1\n%2\n\n").arg(doc.title).arg(doc.content.left(2000));
        }
    }

    return result;
}

void AIToolWindow::saveConversationToHistory()
{
    if (current_conversation_id_.isEmpty()) return;

    for (auto& conv : conversations_) {
        if (QString::fromStdString(conv.id) == current_conversation_id_) {
            history_panel_->updateConversation(conv);
            break;
        }
    }
}

// ============================================================================
// Slots publicos
// ============================================================================

void AIToolWindow::onSendClicked()
{
    QString text = prompt_widget_->getText().trimmed();
    if (!text.isEmpty()) {
        sendMessage(text);
    }
}

void AIToolWindow::onProviderChanged(int index)
{
    (void)index;
    QString name = provider_selector_->currentText();
    active_provider_ = stringToProvider(name);
    status_label_->setText(tr("Provider: %1").arg(name));
}

void AIToolWindow::onModelChanged(int index)
{
    (void)index;
}

void AIToolWindow::onNewConversation()
{
    startNewConversation();
}

void AIToolWindow::onHistoryItemSelected(const QString& conv_id)
{
    loadConversation(conv_id);
}

void AIToolWindow::onExportMarkdown()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export to Markdown"), "",
                                                  tr("Markdown (*.md)"));
    if (!path.isEmpty()) {
        exportCurrentConversationToMarkdown(path);
    }
}

void AIToolWindow::onExportJSON()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export to JSON"), "",
                                                  tr("JSON (*.json)"));
    if (!path.isEmpty()) {
        exportCurrentConversationToJSON(path);
    }
}

void AIToolWindow::onSettingsClicked()
{
    side_panel_->setCurrentIndex(1); // Provider tab
}

void AIToolWindow::onAttachmentClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Attach Files"), "",
        tr("All Files (*.*)"));
    for (const QString& file : files) {
        prompt_widget_->addAttachment(file);
    }
}

void AIToolWindow::onClearChat()
{
    chat_widget_->clearChat();
}

void AIToolWindow::onStreamingChunk(const QString& chunk, const QString& message_id)
{
    chat_widget_->appendStreamingChunk(chunk, message_id);
}

void AIToolWindow::onStreamingFinished(const QString& message_id)
{
    chat_widget_->finalizeStreamingMessage(message_id);
    is_streaming_ = false;
    prompt_widget_->setTypingIndicator(false);
}

void AIToolWindow::onNetworkError(const QString& error)
{
    status_label_->setText(tr("Error: %1").arg(error));
    is_streaming_ = false;
    prompt_widget_->setTypingIndicator(false);
    progress_bar_->setVisible(false);
}

void AIToolWindow::onTokenCountUpdated(int input_tokens, int output_tokens)
{
    (void)input_tokens;
    (void)output_tokens;
}

void AIToolWindow::addProviderConfig(const LLMProviderConfig& config)
{
    provider_configs_[config.provider] = config;
}

void AIToolWindow::setActiveProvider(LLMProvider provider)
{
    active_provider_ = provider;
}

void AIToolWindow::closeEvent(QCloseEvent* event)
{
    saveConversationToHistory();
    QDockWidget::closeEvent(event);
}

// ============================================================================
// RAGEmbeddingThread
// ============================================================================

RAGEmbeddingThread::RAGEmbeddingThread(QObject* parent)
    : QThread(parent)
{
}

void RAGEmbeddingThread::setDocuments(
    const std::vector<std::pair<QString, QString>>& docs)
{
    documents_ = docs;
}

void RAGEmbeddingThread::run()
{
    QHash<QString, std::vector<double>> embeddings;

    for (const auto& doc : documents_) {
        // Simple bag-of-words embedding (para produccion usar modelo real)
        std::vector<double> vec(128, 0.0);
        QString text = doc.second.toLower();
        for (int i = 0; i < text.length() && i < 128; ++i) {
            vec[i % 128] = static_cast<double>(text[i].unicode()) / 65536.0;
        }
        embeddings[doc.first] = vec;
    }

    emit embeddingsReady(embeddings);
}

} // namespace powsys365
