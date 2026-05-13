#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QCompleter>
#include <QStringListModel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QToolBar>
#include <QHash>
#include <QList>
#include <QJsonObject>

// Forward declarations
class QPushButton;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QFileInfo;

namespace powsys365 {

/**
 * @brief Attachment adjunto a un prompt.
 */
struct PromptAttachment {
    QString file_path;
    QString file_name;
    QString mime_type;
    qint64 file_size = 0;
    QJsonObject extracted_text;  ///< Texto extraido de PDF/Word/Excel
    bool is_image = false;
    QPixmap thumbnail;
};

/**
 * @brief Widget de entrada de prompts con formato Markdown.
 *
 * Caracteristicas:
 * - Resaltado de sintaxis Markdown
 * - Toolbar de formato (bold, italic, code, list)
 * - Drag & drop de archivos adjuntos
 * - Ctrl+Enter para enviar
 * - Completer con sugerencias de comandos
 * - Vista previa de adjuntos
 */
class AIPromptWidget : public QWidget {
    Q_OBJECT
public:
    explicit AIPromptWidget(QWidget* parent = nullptr);
    ~AIPromptWidget();

    QString getText() const;
    void setText(const QString& text);
    void clear();
    void setPlaceholderText(const QString& text);

    // Attachments
    void addAttachment(const QString& file_path);
    void removeAttachment(const QString& file_name);
    void clearAttachments();
    QList<PromptAttachment> getAttachments() const { return attachments_; }
    bool hasAttachments() const { return !attachments_.isEmpty(); }

    // Sugerencias
    void setSuggestions(const QStringList& suggestions);

    // Estado
    void setEnabled(bool enabled);
    void setSendButtonEnabled(bool enabled);
    void setTypingIndicator(bool typing);

    // Preview
    void setPreviewVisible(bool visible);

signals:
    void sendRequested(const QString& text, const QList<PromptAttachment>& attachments);
    void textChanged(const QString& text);
    void attachmentAdded(const PromptAttachment& attachment);
    void attachmentRemoved(const QString& file_name);
    void tokenCountChanged(int token_count);
    void heightChanged(int new_height);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTextChanged();
    void onSendClicked();
    void onBoldClicked();
    void onItalicClicked();
    void onCodeClicked();
    void onCodeBlockClicked();
    void onListClicked();
    void onNumberedListClicked();
    void onQuoteClicked();
    void onLinkClicked();
    void onAttachmentClicked();
    void onRemoveAttachment(const QString& file_name);
    void onCompleterActivated(const QString& text);
    void onTokenCountTimer();
    void updatePreview();

private:
    void setupUI();
    void setupToolbar();
    void setupCompleter();
    void setupConnections();
    void applyFormat(const QString& prefix, const QString& suffix = QString());
    QString extractTextFromPDF(const QString& file_path);
    QString extractTextFromWord(const QString& file_path);
    QString extractTextFromExcel(const QString& file_path);
    int estimateTokens(const QString& text) const;
    QString markdownToHtmlPreview(const QString& markdown) const;
    void updateAttachmentPreviews();
    void adjustHeight();

    // UI components
    QToolBar* toolbar_ = nullptr;
    QTextEdit* text_edit_ = nullptr;
    QTextEdit* preview_edit_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* attach_btn_ = nullptr;
    QLabel* token_count_label_ = nullptr;
    QLabel* char_count_label_ = nullptr;
    QFrame* attachments_frame_ = nullptr;
    QHBoxLayout* attachments_layout_ = nullptr;
    QCompleter* completer_ = nullptr;
    QStringListModel* completer_model_ = nullptr;
    QTimer* token_timer_ = nullptr;

    // Data
    QList<PromptAttachment> attachments_;
    QHash<QString, QPushButton*> attachment_buttons_;
    QString placeholder_text_;
    bool is_typing_ = false;
    int current_token_count_ = 0;
    int max_height_ = 400;
    int min_height_ = 80;
};

/**
 * @brief TextEdit con resaltado de sintaxis Markdown.
 */
class MarkdownTextEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit MarkdownTextEdit(QWidget* parent = nullptr);

signals:
    void cursorLineChanged(int line);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void highlightMarkdown();
    bool isInCodeBlock() const;
};

} // namespace powsys365
