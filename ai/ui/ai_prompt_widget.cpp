#include "ai_prompt_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QKeyEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextDocument>
#include <QTextCursor>
#include <QScrollBar>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>
#include <QMimeDatabase>
#include <QPixmap>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QTextBlock>
#include <QRegularExpression>
#include <QDebug>

namespace powsys365 {

// ============================================================================
// MarkdownTextEdit
// ============================================================================

MarkdownTextEdit::MarkdownTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setAcceptRichText(false);
    document()->setDocumentMargin(8);
}

void MarkdownTextEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab) {
        insertPlainText("    ");
        return;
    }
    if (event->key() == Qt::Key_Return && event->modifiers() & Qt::ControlModifier) {
        // Ctrl+Enter se maneja en AIPromptWidget
        QTextEdit::keyPressEvent(event);
        return;
    }
    QTextEdit::keyPressEvent(event);
    highlightMarkdown();
}

void MarkdownTextEdit::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);
}

void MarkdownTextEdit::highlightMarkdown()
{
    // Simplified markdown highlighting via stylesheet
    // Full implementation would use QSyntaxHighlighter
}

bool MarkdownTextEdit::isInCodeBlock() const
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::StartOfBlock);
    QString line = cursor.block().text();
    return line.startsWith("```") || line.startsWith("    ");
}

// ============================================================================
// AIPromptWidget
// ============================================================================

AIPromptWidget::AIPromptWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupToolbar();
    setupCompleter();
    setupConnections();
}

AIPromptWidget::~AIPromptWidget() = default;

void AIPromptWidget::setupUI()
{
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(4, 4, 4, 4);
    main_layout->setSpacing(4);

    // Toolbar
    toolbar_ = new QToolBar(this);
    toolbar_->setIconSize(QSize(16, 16));
    main_layout->addWidget(toolbar_);

    // Attachments frame
    attachments_frame_ = new QFrame(this);
    attachments_frame_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    attachments_frame_->setVisible(false);
    auto* attachments_outer = new QHBoxLayout(attachments_frame_);
    attachments_outer->setContentsMargins(4, 4, 4, 4);
    attachments_layout_ = new QHBoxLayout();
    attachments_layout_->setSpacing(4);
    attachments_outer->addLayout(attachments_layout_);
    attachments_outer->addStretch();
    main_layout->addWidget(attachments_frame_);

    // Text edit
    text_edit_ = new MarkdownTextEdit(this);
    text_edit_->setPlaceholderText(tr("Type your message... (Ctrl+Enter to send)"));
    text_edit_->setMinimumHeight(60);
    text_edit_->setMaximumHeight(max_height_);
    text_edit_->setAcceptRichText(false);
    QFont font = text_edit_->font();
    font.setPointSize(10);
    text_edit_->setFont(font);
    main_layout->addWidget(text_edit_, 1);

    // Preview (hidden by default)
    preview_edit_ = new QTextEdit(this);
    preview_edit_->setReadOnly(true);
    preview_edit_->setVisible(false);
    preview_edit_->setMaximumHeight(200);
    main_layout->addWidget(preview_edit_);

    // Bottom bar
    auto* bottom_layout = new QHBoxLayout();

    attach_btn_ = new QPushButton(tr("Attach"), this);
    attach_btn_->setToolTip(tr("Attach files (PDF, Excel, Word, Images)"));
    attach_btn_->setMaximumWidth(80);
    bottom_layout->addWidget(attach_btn_);

    token_count_label_ = new QLabel("0 tokens", this);
    token_count_label_->setStyleSheet("QLabel { color: #888; font-size: 11px; }");
    bottom_layout->addWidget(token_count_label_);

    char_count_label_ = new QLabel("0 chars", this);
    char_count_label_->setStyleSheet("QLabel { color: #888; font-size: 11px; }");
    bottom_layout->addWidget(char_count_label_);

    bottom_layout->addStretch();

    send_btn_ = new QPushButton(tr("Send"), this);
    send_btn_->setMaximumWidth(80);
    send_btn_->setDefault(true);
    send_btn_->setEnabled(false);
    bottom_layout->addWidget(send_btn_);

    main_layout->addLayout(bottom_layout);

    setAcceptDrops(true);
    setMinimumHeight(min_height_);
}

void AIPromptWidget::setupToolbar()
{
    auto* bold_act = toolbar_->addAction(QIcon::fromTheme("format-text-bold"), tr("Bold"));
    auto* italic_act = toolbar_->addAction(QIcon::fromTheme("format-text-italic"), tr("Italic"));
    toolbar_->addSeparator();
    auto* code_act = toolbar_->addAction(QIcon::fromTheme("format-text-code"), tr("Inline Code"));
    auto* code_block_act = toolbar_->addAction(QIcon(), tr("Code Block"));
    toolbar_->addSeparator();
    auto* list_act = toolbar_->addAction(QIcon::fromTheme("format-list-unordered"), tr("Bullet List"));
    auto* num_list_act = toolbar_->addAction(QIcon::fromTheme("format-list-ordered"), tr("Numbered List"));
    toolbar_->addSeparator();
    auto* quote_act = toolbar_->addAction(QIcon::fromTheme("format-text-blockquote"), tr("Quote"));
    auto* link_act = toolbar_->addAction(QIcon::fromTheme("insert-link"), tr("Link"));

    connect(bold_act, &QAction::triggered, this, &AIPromptWidget::onBoldClicked);
    connect(italic_act, &QAction::triggered, this, &AIPromptWidget::onItalicClicked);
    connect(code_act, &QAction::triggered, this, &AIPromptWidget::onCodeClicked);
    connect(code_block_act, &QAction::triggered, this, &AIPromptWidget::onCodeBlockClicked);
    connect(list_act, &QAction::triggered, this, &AIPromptWidget::onListClicked);
    connect(num_list_act, &QAction::triggered, this, &AIPromptWidget::onNumberedListClicked);
    connect(quote_act, &QAction::triggered, this, &AIPromptWidget::onQuoteClicked);
    connect(link_act, &QAction::triggered, this, &AIPromptWidget::onLinkClicked);
}

void AIPromptWidget::setupCompleter()
{
    completer_model_ = new QStringListModel(this);
    completer_ = new QCompleter(completer_model_, this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setWidget(text_edit_);
}

void AIPromptWidget::setupConnections()
{
    connect(text_edit_, &QTextEdit::textChanged, this, &AIPromptWidget::onTextChanged);
    connect(send_btn_, &QPushButton::clicked, this, &AIPromptWidget::onSendClicked);
    connect(attach_btn_, &QPushButton::clicked, this, &AIPromptWidget::onAttachmentClicked);
    connect(completer_, QOverload<const QString&>::of(&QCompleter::activated),
            this, &AIPromptWidget::onCompleterActivated);

    token_timer_ = new QTimer(this);
    token_timer_->setInterval(500);
    token_timer_->setSingleShot(true);
    connect(token_timer_, &QTimer::timeout, this, &AIPromptWidget::onTokenCountTimer);
}

QString AIPromptWidget::getText() const
{
    return text_edit_->toPlainText();
}

void AIPromptWidget::setText(const QString& text)
{
    text_edit_->setPlainText(text);
}

void AIPromptWidget::clear()
{
    text_edit_->clear();
    clearAttachments();
    current_token_count_ = 0;
    token_count_label_->setText("0 tokens");
    char_count_label_->setText("0 chars");
}

void AIPromptWidget::setPlaceholderText(const QString& text)
{
    text_edit_->setPlaceholderText(text);
}

void AIPromptWidget::setEnabled(bool enabled)
{
    QWidget::setEnabled(enabled);
    text_edit_->setEnabled(enabled);
    send_btn_->setEnabled(enabled && !text_edit_->toPlainText().trimmed().isEmpty());
    toolbar_->setEnabled(enabled);
    attach_btn_->setEnabled(enabled);
}

void AIPromptWidget::setSendButtonEnabled(bool enabled)
{
    send_btn_->setEnabled(enabled);
}

void AIPromptWidget::setTypingIndicator(bool typing)
{
    is_typing_ = typing;
    if (typing) {
        send_btn_->setText(tr("Sending..."));
        send_btn_->setEnabled(false);
    } else {
        send_btn_->setText(tr("Send"));
        onTextChanged();
    }
}

void AIPromptWidget::setPreviewVisible(bool visible)
{
    preview_edit_->setVisible(visible);
    if (visible) updatePreview();
}

void AIPromptWidget::addAttachment(const QString& file_path)
{
    QFileInfo info(file_path);
    if (!info.exists()) return;

    // Verificar si ya existe
    for (const auto& att : attachments_) {
        if (att.file_path == file_path) return;
    }

    PromptAttachment att;
    att.file_path = file_path;
    att.file_name = info.fileName();
    att.file_size = info.size();

    QMimeDatabase mime_db;
    att.mime_type = mime_db.mimeTypeForFile(info).name();
    att.is_image = att.mime_type.startsWith("image/");

    // Extraer texto si es documento
    if (att.mime_type == "application/pdf") {
        att.extracted_text = QJsonObject{{"text", extractTextFromPDF(file_path)}};
    } else if (att.mime_type.contains("wordprocessingml") || att.mime_type.contains("msword")) {
        att.extracted_text = QJsonObject{{"text", extractTextFromWord(file_path)}};
    } else if (att.mime_type.contains("spreadsheet") || att.mime_type.contains("excel")) {
        att.extracted_text = QJsonObject{{"text", extractTextFromExcel(file_path)}};
    }

    // Thumbnail para imagenes
    if (att.is_image) {
        QPixmap pixmap(file_path);
        if (!pixmap.isNull()) {
            att.thumbnail = pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    attachments_.append(att);

    // Boton de attachment
    auto* btn = new QPushButton(att.file_name, attachments_frame_);
    btn->setMaximumWidth(150);
    btn->setToolTip(QString("%1 (%2 bytes)").arg(att.file_path).arg(att.file_size));
    if (!att.thumbnail.isNull()) {
        btn->setIcon(QIcon(att.thumbnail));
    }
    connect(btn, &QPushButton::clicked, this, [this, name = att.file_name]() {
        onRemoveAttachment(name);
    });
    attachments_layout_->addWidget(btn);
    attachment_buttons_[att.file_name] = btn;

    attachments_frame_->setVisible(true);
    emit attachmentAdded(att);
}

void AIPromptWidget::removeAttachment(const QString& file_name)
{
    onRemoveAttachment(file_name);
}

void AIPromptWidget::onRemoveAttachment(const QString& file_name)
{
    for (int i = 0; i < attachments_.size(); ++i) {
        if (attachments_[i].file_name == file_name) {
            attachments_.removeAt(i);
            break;
        }
    }

    auto it = attachment_buttons_.find(file_name);
    if (it != attachment_buttons_.end()) {
        it.value()->deleteLater();
        attachment_buttons_.erase(it);
    }

    if (attachments_.isEmpty()) {
        attachments_frame_->setVisible(false);
    }
    emit attachmentRemoved(file_name);
}

void AIPromptWidget::clearAttachments()
{
    attachments_.clear();
    for (auto* btn : attachment_buttons_) {
        btn->deleteLater();
    }
    attachment_buttons_.clear();
    attachments_frame_->setVisible(false);
}

void AIPromptWidget::setSuggestions(const QStringList& suggestions)
{
    completer_model_->setStringList(suggestions);
}

void AIPromptWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return && (event->modifiers() & Qt::ControlModifier)) {
        onSendClicked();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AIPromptWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AIPromptWidget::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) return;

    for (const QUrl& url : mime->urls()) {
        QString file_path = url.toLocalFile();
        if (!file_path.isEmpty()) {
            addAttachment(file_path);
        }
    }
    event->acceptProposedAction();
}

void AIPromptWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    adjustHeight();
}

void AIPromptWidget::onTextChanged()
{
    QString text = text_edit_->toPlainText().trimmed();
    send_btn_->setEnabled(!text.isEmpty());

    int chars = text_edit_->toPlainText().length();
    char_count_label_->setText(QString("%1 chars").arg(chars));

    token_timer_->stop();
    token_timer_->start();

    emit textChanged(text_edit_->toPlainText());
    updatePreview();
}

void AIPromptWidget::onSendClicked()
{
    QString text = text_edit_->toPlainText().trimmed();
    if (text.isEmpty()) return;

    emit sendRequested(text, attachments_);
    clear();
}

void AIPromptWidget::onBoldClicked()
{
    applyFormat("**", "**");
}

void AIPromptWidget::onItalicClicked()
{
    applyFormat("*", "*");
}

void AIPromptWidget::onCodeClicked()
{
    applyFormat("`", "`");
}

void AIPromptWidget::onCodeBlockClicked()
{
    QTextCursor cursor = text_edit_->textCursor();
    QString selected = cursor.selectedText();
    if (selected.isEmpty()) {
        cursor.insertText("\n```cpp\n// Your code here\n```\n");
    } else {
        cursor.insertText("\n```\n" + selected + "\n```\n");
    }
    text_edit_->setTextCursor(cursor);
}

void AIPromptWidget::onListClicked()
{
    QTextCursor cursor = text_edit_->textCursor();
    QString selected = cursor.selectedText();
    if (selected.isEmpty()) {
        cursor.insertText("\n- Item 1\n- Item 2\n- Item 3\n");
    } else {
        QStringList lines = selected.split('\n');
        QString formatted;
        for (const QString& line : lines) {
            formatted += "- " + line + "\n";
        }
        cursor.insertText(formatted);
    }
    text_edit_->setTextCursor(cursor);
}

void AIPromptWidget::onNumberedListClicked()
{
    QTextCursor cursor = text_edit_->textCursor();
    QString selected = cursor.selectedText();
    if (selected.isEmpty()) {
        cursor.insertText("\n1. Item 1\n2. Item 2\n3. Item 3\n");
    } else {
        QStringList lines = selected.split('\n');
        QString formatted;
        for (int i = 0; i < lines.size(); ++i) {
            formatted += QString("%1. %2\n").arg(i + 1).arg(lines[i]);
        }
        cursor.insertText(formatted);
    }
    text_edit_->setTextCursor(cursor);
}

void AIPromptWidget::onQuoteClicked()
{
    QTextCursor cursor = text_edit_->textCursor();
    QString selected = cursor.selectedText();
    if (selected.isEmpty()) {
        cursor.insertText("\n> Quote text here\n");
    } else {
        QStringList lines = selected.split('\n');
        QString formatted;
        for (const QString& line : lines) {
            formatted += "> " + line + "\n";
        }
        cursor.insertText(formatted);
    }
    text_edit_->setTextCursor(cursor);
}

void AIPromptWidget::onLinkClicked()
{
    QTextCursor cursor = text_edit_->textCursor();
    QString selected = cursor.selectedText();
    if (selected.isEmpty()) {
        cursor.insertText("[link text](https://example.com)");
    } else {
        cursor.insertText(QString("[%1](https://example.com)").arg(selected));
    }
    text_edit_->setTextCursor(cursor);
}

void AIPromptWidget::onAttachmentClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Attach Files"),
        QString(),
        tr("Documents (*.pdf *.doc *.docx *.xls *.xlsx);;"
           "Images (*.png *.jpg *.jpeg *.gif *.bmp *.svg);;"
           "Text Files (*.txt *.csv *.json *.xml);;"
           "All Files (*.*)")
    );

    for (const QString& file : files) {
        addAttachment(file);
    }
}

void AIPromptWidget::onCompleterActivated(const QString& text)
{
    QTextCursor cursor = text_edit_->textCursor();
    cursor.insertText(text);
    text_edit_->setTextCursor(cursor);
}

void AIPromptWidget::onTokenCountTimer()
{
    QString text = text_edit_->toPlainText();
    current_token_count_ = estimateTokens(text);
    token_count_label_->setText(QString("%1 tokens").arg(current_token_count_));
    emit tokenCountChanged(current_token_count_);
}

void AIPromptWidget::applyFormat(const QString& prefix, const QString& suffix)
{
    QTextCursor cursor = text_edit_->textCursor();
    if (cursor.hasSelection()) {
        QString selected = cursor.selectedText();
        cursor.insertText(prefix + selected + suffix);
    } else {
        cursor.insertText(prefix + suffix);
        cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, suffix.length());
    }
    text_edit_->setTextCursor(cursor);
}

QString AIPromptWidget::extractTextFromPDF(const QString& file_path)
{
    (void)file_path;
    // Para implementacion completa, usar libpoppler o similar
    return tr("[PDF content extraction requires poppler library]");
}

QString AIPromptWidget::extractTextFromWord(const QString& file_path)
{
    (void)file_path;
    // Para implementacion completa, usar libzip + xml parsing para docx
    return tr("[Word document content extraction]");
}

QString AIPromptWidget::extractTextFromExcel(const QString& file_path)
{
    (void)file_path;
    // Para implementacion completa, usar libxlnt o similar
    return tr("[Excel content extraction]");
}

int AIPromptWidget::estimateTokens(const QString& text) const
{
    // Estimacion aproximada: ~4 caracteres por token para ingles
    // Para CJK y otros idiomas: ~1-2 caracteres por token
    int total = 0;
    for (const QChar& c : text) {
        ushort unicode = c.unicode();
        if (unicode >= 0x4E00 && unicode <= 0x9FFF) {
            total += 2; // CJK: ~2 chars por token
        } else if (unicode >= 0x0600 && unicode <= 0x06FF) {
            total += 2; // Arabic
        } else {
            total += 1;
        }
    }
    return std::max(1, total / 4);
}

QString AIPromptWidget::markdownToHtmlPreview(const QString& markdown) const
{
    QString html = markdown;

    // Escapar HTML
    html.replace("&", "&amp;");
    html.replace("<", "&lt;");
    html.replace(">", "&gt;");

    // Bold
    html.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<b>\\1</b>");
    // Italic
    html.replace(QRegularExpression("\\*(.+?)\\*"), "<i>\\1</i>");
    // Code inline
    html.replace(QRegularExpression("`(.+?)`"), "<code style='background:#f0f0f0;padding:1px 3px;border-radius:3px;'>\\1</code>");
    // Code blocks
    html.replace(QRegularExpression("```(.+?)```"), "<pre style='background:#f5f5f5;padding:8px;border-radius:4px;'><code>\\1</code></pre>");
    // Headers
    html.replace(QRegularExpression("^### (.+)$", QRegularExpression::MultilineOption), "<h3>\\1</h3>");
    html.replace(QRegularExpression("^## (.+)$", QRegularExpression::MultilineOption), "<h2>\\1</h2>");
    html.replace(QRegularExpression("^# (.+)$", QRegularExpression::MultilineOption), "<h1>\\1</h1>");
    // Lists
    html.replace(QRegularExpression("^- (.+)$", QRegularExpression::MultilineOption), "<li>\\1</li>");
    // Quotes
    html.replace(QRegularExpression("^&gt; (.+)$", QRegularExpression::MultilineOption), "<blockquote style='border-left:3px solid #ccc;padding-left:8px;color:#666;'>\\1</blockquote>");
    // Links
    html.replace(QRegularExpression("\\[([^\\]]+)\\]\\(([^)]+)\\)"), "<a href='\\2'>\\1</a>");
    // Line breaks
    html.replace("\n", "<br>");

    return "<html><body style='font-family:sans-serif;font-size:12px;'" + html + "</body></html>";
}

void AIPromptWidget::updatePreview()
{
    if (preview_edit_->isVisible()) {
        preview_edit_->setHtml(markdownToHtmlPreview(text_edit_->toPlainText()));
    }
}

void AIPromptWidget::adjustHeight()
{
    int doc_height = text_edit_->document()->size().height();
    int new_height = std::min(max_height_, std::max(min_height_, doc_height + 40));
    text_edit_->setMinimumHeight(new_height);
    emit heightChanged(new_height);
}

} // namespace powsys365
