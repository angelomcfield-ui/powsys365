// monaco_bridge.h - Monaco Editor Bridge para POWSYS365 IDE
// Copyright (c) 2025 POWSYS365. All rights reserved.
#pragma once

#include <QObject>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebChannel>
#include <QWebChannelAbstractTransport>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QFile>
#include <QDir>

namespace powsys365 {
namespace ide {

/**
 * @brief MonacoBridge - Puente de comunicacion entre Qt y Monaco Editor.
 *
 * Integra el editor Monaco (VS Code Editor) dentro de un QWebEngineView,
 * proporcionando comunicacion bidireccional C++/JavaScript. Soporta
 * syntax highlighting para Python y C++, autocompletion basico, y
 * manejo de multiples archivos abiertos.
 */
class MonacoBridge : public QObject {
    Q_OBJECT

public:
    explicit MonacoBridge(QObject* parent = nullptr);
    ~MonacoBridge();

    // Inicializacion
    void initialize(QWebEngineView* webView);
    bool isReady() const;

    // ============================
    // Gestion de archivos
    // ============================
public slots:
    void openFile(const QString& filePath, const QString& content);
    void closeFile(const QString& filePath);
    void setContent(const QString& filePath, const QString& content);
    QString getContent(const QString& filePath) const;
    void requestContent(const QString& filePath);
    void switchToFile(const QString& filePath);
    QString currentFile() const;
    QStringList openFiles() const;

    // ============================
    // Editor operations
    // ============================
    void setTheme(const QString& themeName); // "vs", "vs-dark", "hc-black"
    void setLanguage(const QString& filePath, const QString& language);
    void insertText(const QString& text);
    void replaceSelection(const QString& text);
    void undo();
    void redo();
    void goToLine(int lineNumber);
    void find(const QString& query, bool caseSensitive = false,
              bool wholeWord = false);
    void formatDocument();

    // ============================
    // Autocompletion
    // ============================
    void registerCompletionProvider(const QString& language,
                                    const QJsonArray& suggestions);
    void addCustomCompletion(const QString& label, const QString& detail,
                             const QString& insertText, int kind = 8);
    void clearCustomCompletions();

    // ============================
    // Marcadores y decoraciones
    // ============================
    void addBreakpoint(int line);
    void removeBreakpoint(int line);
    void clearBreakpoints();
    void highlightLine(int line, const QString& color = "rgba(255,255,0,0.3)");
    void clearHighlights();
    void setErrorMarkers(const QJsonArray& markers);
    void setWarningMarkers(const QJsonArray& markers);

signals:
    void editorReady();
    void contentChanged(const QString& filePath, const QString& content);
    void fileSwitched(const QString& filePath);
    void cursorPositionChanged(int line, int column);
    void breakpointToggled(int line);
    void completionRequested(const QString& prefix);
    void editorError(const QString& error);

private slots:
    void onPageLoadFinished(bool ok);
    void onJavaScriptConsoleMessage(QWebEnginePage::JavaScriptConsoleMessageLevel level,
                                     const QString& message, int lineNumber,
                                     const QString& sourceID);

private:
    void injectEditorHtml();
    void setupWebChannel();
    void executeJs(const QString& script);
    QString detectLanguage(const QString& filePath) const;
    void buildEditorPage();

    struct PrivateData;
    std::unique_ptr<PrivateData> d;
};

/**
 * @brief MonacoEditorPage - Pagina web personalizada para Monaco.
 */
class MonacoEditorPage : public QWebEnginePage {
    Q_OBJECT

public:
    explicit MonacoEditorPage(QWebEngineProfile* profile, QObject* parent = nullptr);

protected:
    bool acceptNavigationRequest(const QUrl& url,
                                  QWebEnginePage::NavigationType type,
                                  bool isMainFrame) override;
};

} // namespace ide
} // namespace powsys365
