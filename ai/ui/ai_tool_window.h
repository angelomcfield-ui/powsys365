#pragma once

#include <QDockWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QThread>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <memory>
#include <vector>

// Forward declarations Qt
class QTextEdit;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTabWidget;
class QListWidget;
class QLabel;
class QProgressBar;
class QSplitter;
class QFileDialog;
class QMenu;
class QAction;
class QToolBar;
class QStackedWidget;
class QScrollArea;
class QFrame;
class QGridLayout;

namespace powsys365 {

// Forward declarations
class AIPromptWidget;

/**
 * @brief Modelo de proveedor de LLM soportado.
 */
enum class LLMProvider {
    DeepSeek,       ///< DeepSeek AI (deepseek-chat, deepseek-reasoner)
    Kimi,           ///< Moonshot Kimi
    GPT4o,          ///< OpenAI GPT-4o
    Claude,         ///< Anthropic Claude
    Llama,          ///< Meta Llama (via local/cloud)
    Gemini,         ///< Google Gemini
    Custom          ///< Proveedor personalizado
};

QString providerToString(LLMProvider provider);
LLMProvider stringToProvider(const QString& str);

/**
 * @brief Configuracion de un proveedor LLM.
 */
struct LLMProviderConfig {
    LLMProvider provider;
    QString name;
    QString api_base_url;
    QString api_key;
    QString default_model;
    QStringList available_models;
    double max_tokens = 4096;
    double temperature = 0.7;
    double top_p = 1.0;
    double cost_per_1k_input_tokens = 0.0;   ///< USD por 1K tokens de entrada
    double cost_per_1k_output_tokens = 0.0;  ///< USD por 1K tokens de salida
    int timeout_ms = 60000;
    bool supports_streaming = true;
    bool supports_vision = false;
    bool supports_system_messages = true;
    QJsonObject extra_headers;
    QJsonObject extra_params;
};

/**
 * @brief Mensaje de conversacion.
 */
struct ChatMessage {
    QString id;
    QString role;           ///< "system", "user", "assistant"
    QString content;
    QDateTime timestamp;
    QString model_used;
    LLMProvider provider;
    int input_tokens = 0;
    int output_tokens = 0;
    double estimated_cost_usd = 0.0;
    QList<QJsonObject> attachments; ///< Archivos adjuntos
    QString status;         ///< "sending", "streaming", "complete", "error"
};

/**
 * @brief Conversacion completa (sesion).
 */
struct Conversation {
    QString id;
    QString title;
    QDateTime created_at;
    QDateTime updated_at;
    std::vector<ChatMessage> messages;
    LLMProvider provider;
    QString model_used;
    int total_input_tokens = 0;
    int total_output_tokens = 0;
    double total_cost_usd = 0.0;
    QString project_context;
    bool is_pinned = false;
};

/**
 * @brief Widget de chat con streaming LLM.
 */
class AIChatWidget : public QFrame {
    Q_OBJECT
public:
    explicit AIChatWidget(QWidget* parent = nullptr);
    ~AIChatWidget();

    void appendMessage(const ChatMessage& message);
    void appendStreamingChunk(const QString& chunk, const QString& message_id);
    void finalizeStreamingMessage(const QString& message_id);
    void clearChat();
    void setSystemMessageVisible(bool visible);
    QString exportToMarkdown() const;
    QString exportToJSON() const;

signals:
    void messageClicked(const QString& message_id);
    void copyToClipboardRequested(const QString& text);

private:
    void setupUI();
    QString formatMessageContent(const QString& content) const;
    QString markdownToHtml(const QString& markdown) const;
    QString escapeHtml(const QString& text) const;
    void applyCodeHighlighting(QString& html) const;

    QScrollArea* scroll_area_ = nullptr;
    QFrame* messages_container_ = nullptr;
    QVBoxLayout* messages_layout_ = nullptr;
    QLabel* system_message_label_ = nullptr;
    QHash<QString, QFrame*> message_widgets_;
    QHash<QString, QLabel*> streaming_labels_;
    QHash<QString, QString> streaming_buffers_;
};

/**
 * @brief Panel de historial de conversaciones.
 */
class AIHistoryPanel : public QFrame {
    Q_OBJECT
public:
    explicit AIHistoryPanel(QWidget* parent = nullptr);
    ~AIHistoryPanel();

    void addConversation(const Conversation& conv);
    void removeConversation(const QString& conv_id);
    void updateConversation(const Conversation& conv);
    void clearHistory();
    std::vector<Conversation> getConversations() const;
    void setConversations(const std::vector<Conversation>& conversations);

signals:
    void conversationSelected(const QString& conv_id);
    void conversationDeleted(const QString& conv_id);
    void conversationPinned(const QString& conv_id, bool pinned);
    void newConversationRequested();
    void searchTextChanged(const QString& text);

private:
    void setupUI();

    QListWidget* history_list_ = nullptr;
    QLineEdit* search_edit_ = nullptr;
    QPushButton* new_conv_btn_ = nullptr;
    std::vector<Conversation> conversations_;
};

/**
 * @brief Panel de configuracion de proveedores.
 */
class AIProviderPanel : public QFrame {
    Q_OBJECT
public:
    explicit AIProviderPanel(QWidget* parent = nullptr);
    ~AIProviderPanel();

    void setProviderConfig(const LLMProviderConfig& config);
    LLMProviderConfig getProviderConfig() const;
    void loadDefaults();

signals:
    void configChanged(const LLMProviderConfig& config);
    void testConnectionRequested();

private:
    void setupUI();

    QComboBox* provider_combo_ = nullptr;
    QComboBox* model_combo_ = nullptr;
    QLineEdit* api_key_edit_ = nullptr;
    QLineEdit* api_url_edit_ = nullptr;
    QLineEdit* temperature_edit_ = nullptr;
    QLineEdit* max_tokens_edit_ = nullptr;
    QLabel* cost_label_ = nullptr;
};

/**
 * @brief Panel de estadisticas y costos.
 */
class AIStatsPanel : public QFrame {
    Q_OBJECT
public:
    explicit AIStatsPanel(QWidget* parent = nullptr);
    ~AIStatsPanel();

    void updateStats(const Conversation& conv);
    void updateSessionStats(int total_input, int total_output, double total_cost);
    void clearStats();

private:
    void setupUI();

    QLabel* tokens_input_label_ = nullptr;
    QLabel* tokens_output_label_ = nullptr;
    QLabel* total_tokens_label_ = nullptr;
    QLabel* cost_label_ = nullptr;
    QLabel* model_label_ = nullptr;
    QProgressBar* token_budget_bar_ = nullptr;
};

/**
 * @brief Ventana de herramientas AI integrada (QDockWidget).
 *
 * Caracteristicas:
 * - Chat con multiples proveedores LLM
 * - Streaming de respuestas en tiempo real
 * - Historial de conversaciones
 * - Export a JSON/Markdown
 * - Contador de tokens y costos estimados
 * - Soporte de adjuntos (PDF, Excel, Word, imagenes)
 * - Contexto de proyecto
 * - RAG para normativas IEEE/IEC
 * - Auto-seleccion de motor basado en prompt
 */
class AIToolWindow : public QDockWidget {
    Q_OBJECT
public:
    explicit AIToolWindow(QWidget* parent = nullptr);
    ~AIToolWindow();

    // Configuracion de proveedores
    void addProviderConfig(const LLMProviderConfig& config);
    void setActiveProvider(LLMProvider provider);
    LLMProvider getActiveProvider() const { return active_provider_; }

    // Conversacion
    void startNewConversation();
    void loadConversation(const QString& conv_id);
    void sendMessage(const QString& message);
    void sendMessageWithAttachments(const QString& message,
                                     const QStringList& file_paths);

    // Contexto
    void setProjectContext(const QString& context);
    void setRAGContext(const QString& standard_code); ///< "IEEE_1547", "IEC_61850", etc.

    // Export
    void exportCurrentConversationToMarkdown(const QString& file_path);
    void exportCurrentConversationToJSON(const QString& file_path);
    void exportAllConversationsToJSON(const QString& file_path);

    // Utilidades
    int estimateTokens(const QString& text) const;
    double estimateQueryCost(const QString& text, LLMProvider provider) const;
    QString buildSystemMessage() const;
    LLMProvider autoSelectProvider(const QString& prompt) const;

    // Sesion
    void saveSession(const QString& file_path);
    void loadSession(const QString& file_path);

    // Normativas RAG
    void loadStandardsDatabase(const QString& directory);
    QString queryStandardsRAG(const QString& query) const;

public slots:
    void onSendClicked();
    void onProviderChanged(int index);
    void onModelChanged(int index);
    void onNewConversation();
    void onHistoryItemSelected(const QString& conv_id);
    void onExportMarkdown();
    void onExportJSON();
    void onSettingsClicked();
    void onAttachmentClicked();
    void onClearChat();
    void onStreamingChunk(const QString& chunk, const QString& message_id);
    void onStreamingFinished(const QString& message_id);
    void onNetworkError(const QString& error);
    void onTokenCountUpdated(int input_tokens, int output_tokens);

signals:
    void assistantResponseReceived(const QString& response);
    void codeGenerated(const QString& code, const QString& language);
    void analysisComplete(const QString& analysis);
    void errorOccurred(const QString& error);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUI();
    void setupConnections();
    void setupProviders();
    void setupToolbar();

    // Network
    void sendLLMRequest(const ChatMessage& message);
    void sendStreamingRequest(const ChatMessage& message);
    void handleNetworkReply(QNetworkReply* reply);
    QJsonObject buildRequestPayload(const ChatMessage& message) const;
    QString extractResponseContent(const QJsonObject& response) const;

    // Streaming
    void processSSEChunk(const QByteArray& chunk, QString& buffer);

    // Gestores
    QString generateConversationId() const;
    void saveConversationToHistory();

    // RAG
    struct StandardDocument {
        QString id;
        QString title;
        QString code;       // "IEEE 1547", "IEC 61850-7-4"
        QString content;
        QDateTime last_updated;
        std::vector<double> embedding;
    };
    std::vector<StandardDocument> standards_db_;
    QString retrieveRelevantContext(const QString& query) const;
    double cosineSimilarity(const std::vector<double>& a, const std::vector<double>& b) const;

    // Miembros UI
    AIChatWidget* chat_widget_ = nullptr;
    AIPromptWidget* prompt_widget_ = nullptr;
    AIHistoryPanel* history_panel_ = nullptr;
    AIProviderPanel* provider_panel_ = nullptr;
    AIStatsPanel* stats_panel_ = nullptr;
    QTabWidget* side_panel_ = nullptr;
    QComboBox* provider_selector_ = nullptr;
    QComboBox* model_selector_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* new_conv_btn_ = nullptr;
    QPushButton* settings_btn_ = nullptr;
    QPushButton* export_btn_ = nullptr;
    QPushButton* attach_btn_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QToolBar* toolbar_ = nullptr;

    // Miembros datos
    QNetworkAccessManager* network_manager_ = nullptr;
    QNetworkReply* current_reply_ = nullptr;
    QHash<LLMProvider, LLMProviderConfig> provider_configs_;
    LLMProvider active_provider_ = LLMProvider::DeepSeek;
    std::vector<Conversation> conversations_;
    QString current_conversation_id_;
    QString project_context_;
    QString rag_context_;
    QStringList pending_attachments_;
    bool is_streaming_ = false;
    int session_input_tokens_ = 0;
    int session_output_tokens_ = 0;
    double session_cost_usd_ = 0.0;
    QTimer* typing_indicator_timer_ = nullptr;
};

/**
 * @brief Thread para procesamiento de embeddings RAG.
 */
class RAGEmbeddingThread : public QThread {
    Q_OBJECT
public:
    explicit RAGEmbeddingThread(QObject* parent = nullptr);
    void setDocuments(const std::vector<std::pair<QString, QString>>& docs);

signals:
    void embeddingsReady(const QHash<QString, std::vector<double>>& embeddings);

protected:
    void run() override;

private:
    std::vector<std::pair<QString, QString>> documents_;
};

} // namespace powsys365
