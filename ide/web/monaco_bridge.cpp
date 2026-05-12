// monaco_bridge.cpp - Implementacion de Monaco Editor Bridge para POWSYS365
// Copyright (c) 2025 POWSYS365. All rights reserved.

#include "monaco_bridge.h"

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include <QJsonDocument>

namespace powsys365 {
namespace ide {

// ============================
// Private Data
// ============================

struct MonacoBridge::PrivateData {
    QWebEngineView* webView = nullptr;
    QWebChannel* webChannel = nullptr;
    MonacoEditorPage* editorPage = nullptr;
    bool editorReady = false;
    QString currentFilePath;
    QMap<QString, QString> openFiles; // filePath -> language
    QJsonArray customCompletions;
    QJsonArray breakpoints;
    QStringList pendingScripts;
};

// ============================
// Constructor / Destructor
// ============================

MonacoBridge::MonacoBridge(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<PrivateData>())
{
}

MonacoBridge::~MonacoBridge() {
}

// ============================
// Inicializacion
// ============================

void MonacoBridge::initialize(QWebEngineView* webView) {
    if (!webView) return;

    d->webView = webView;

    // Crear perfil y pagina personalizada
    QWebEngineProfile* profile = new QWebEngineProfile("MonacoEditorProfile", this);
    d->editorPage = new MonacoEditorPage(profile, this);
    d->webView->setPage(d->editorPage);

    // Configurar WebChannel
    d->webChannel = new QWebChannel(this);
    d->webChannel->registerObject("monacoBridge", this);
    d->editorPage->setWebChannel(d->webChannel);

    // Conectar senales
    connect(d->editorPage, &QWebEnginePage::loadFinished,
            this, &MonacoBridge::onPageLoadFinished);

    // Inyectar la pagina del editor
    buildEditorPage();
}

bool MonacoBridge::isReady() const {
    return d->editorReady;
}

// ============================
// Gestion de archivos
// ============================

void MonacoBridge::openFile(const QString& filePath, const QString& content) {
    QString lang = detectLanguage(filePath);
    d->openFiles[filePath] = lang;

    QJsonObject msg;
    msg["action"] = "openFile";
    msg["filePath"] = filePath;
    msg["content"] = content;
    msg["language"] = lang;

    QString script = QString("window.monacoEditor && window.monacoEditor.openFile(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);

    d->currentFilePath = filePath;
}

void MonacoBridge::closeFile(const QString& filePath) {
    d->openFiles.remove(filePath);

    QJsonObject msg;
    msg["action"] = "closeFile";
    msg["filePath"] = filePath;

    QString script = QString("window.monacoEditor && window.monacoEditor.closeFile(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);

    if (d->currentFilePath == filePath) {
        d->currentFilePath.clear();
    }
}

void MonacoBridge::setContent(const QString& filePath, const QString& content) {
    QJsonObject msg;
    msg["action"] = "setContent";
    msg["filePath"] = filePath;
    msg["content"] = content;

    QString script = QString("window.monacoEditor && window.monacoEditor.setContent(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

QString MonacoBridge::getContent(const QString& filePath) const {
    // Este metodo es sincrono; en la practica se usa requestContent + signal
    return QString();
}

void MonacoBridge::requestContent(const QString& filePath) {
    QJsonObject msg;
    msg["action"] = "getContent";
    msg["filePath"] = filePath;

    QString script = QString("window.monacoEditor && window.monacoEditor.requestContent(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::switchToFile(const QString& filePath) {
    if (!d->openFiles.contains(filePath)) return;

    QJsonObject msg;
    msg["action"] = "switchToFile";
    msg["filePath"] = filePath;

    QString script = QString("window.monacoEditor && window.monacoEditor.switchToFile(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);

    d->currentFilePath = filePath;
    emit fileSwitched(filePath);
}

QString MonacoBridge::currentFile() const {
    return d->currentFilePath;
}

QStringList MonacoBridge::openFiles() const {
    return d->openFiles.keys();
}

// ============================
// Editor operations
// ============================

void MonacoBridge::setTheme(const QString& themeName) {
    QString script = QString("window.monacoEditor && window.monacoEditor.setTheme('%1')")
                     .arg(themeName);
    executeJs(script);
}

void MonacoBridge::setLanguage(const QString& filePath, const QString& language) {
    QJsonObject msg;
    msg["action"] = "setLanguage";
    msg["filePath"] = filePath;
    msg["language"] = language;

    QString script = QString("window.monacoEditor && window.monacoEditor.setLanguage(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);

    if (d->openFiles.contains(filePath)) {
        d->openFiles[filePath] = language;
    }
}

void MonacoBridge::insertText(const QString& text) {
    QString escaped = text;
    escaped.replace("\\", "\\\\");
    escaped.replace("'", "\\'");
    escaped.replace("\n", "\\n");
    escaped.replace("\r", "\\r");
    escaped.replace("\t", "\\t");

    QString script = QString("window.monacoEditor && window.monacoEditor.insertText('%1')")
                     .arg(escaped);
    executeJs(script);
}

void MonacoBridge::replaceSelection(const QString& text) {
    QString escaped = text;
    escaped.replace("\\", "\\\\");
    escaped.replace("'", "\\'");
    escaped.replace("\n", "\\n");
    escaped.replace("\r", "\\r");
    escaped.replace("\t", "\\t");

    QString script = QString("window.monacoEditor && window.monacoEditor.replaceSelection('%1')")
                     .arg(escaped);
    executeJs(script);
}

void MonacoBridge::undo() {
    executeJs("window.monacoEditor && window.monacoEditor.undo()");
}

void MonacoBridge::redo() {
    executeJs("window.monacoEditor && window.monacoEditor.redo()");
}

void MonacoBridge::goToLine(int lineNumber) {
    QString script = QString("window.monacoEditor && window.monacoEditor.goToLine(%1)")
                     .arg(lineNumber);
    executeJs(script);
}

void MonacoBridge::find(const QString& query, bool caseSensitive, bool wholeWord) {
    QJsonObject msg;
    msg["query"] = query;
    msg["caseSensitive"] = caseSensitive;
    msg["wholeWord"] = wholeWord;

    QString script = QString("window.monacoEditor && window.monacoEditor.find(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::formatDocument() {
    executeJs("window.monacoEditor && window.monacoEditor.formatDocument()");
}

// ============================
// Autocompletion
// ============================

void MonacoBridge::registerCompletionProvider(const QString& language,
                                               const QJsonArray& suggestions) {
    QJsonObject msg;
    msg["language"] = language;
    msg["suggestions"] = suggestions;

    QString script = QString("window.monacoEditor && window.monacoEditor.registerCompletionProvider(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::addCustomCompletion(const QString& label, const QString& detail,
                                        const QString& insertText, int kind) {
    QJsonObject completion;
    completion["label"] = label;
    completion["detail"] = detail;
    completion["insertText"] = insertText;
    completion["kind"] = kind;
    d->customCompletions.append(completion);

    QJsonObject msg;
    msg["suggestions"] = d->customCompletions;

    QString script = QString("window.monacoEditor && window.monacoEditor.updateCompletions(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::clearCustomCompletions() {
    d->customCompletions = QJsonArray();
    QJsonObject msg;
    msg["suggestions"] = d->customCompletions;

    QString script = QString("window.monacoEditor && window.monacoEditor.updateCompletions(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

// ============================
// Marcadores y decoraciones
// ============================

void MonacoBridge::addBreakpoint(int line) {
    QJsonObject msg;
    msg["action"] = "addBreakpoint";
    msg["line"] = line;

    QString script = QString("window.monacoEditor && window.monacoEditor.breakpointAction(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::removeBreakpoint(int line) {
    QJsonObject msg;
    msg["action"] = "removeBreakpoint";
    msg["line"] = line;

    QString script = QString("window.monacoEditor && window.monacoEditor.breakpointAction(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::clearBreakpoints() {
    executeJs("window.monacoEditor && window.monacoEditor.clearBreakpoints()");
}

void MonacoBridge::highlightLine(int line, const QString& color) {
    QJsonObject msg;
    msg["line"] = line;
    msg["color"] = color;

    QString script = QString("window.monacoEditor && window.monacoEditor.highlightLine(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::clearHighlights() {
    executeJs("window.monacoEditor && window.monacoEditor.clearHighlights()");
}

void MonacoBridge::setErrorMarkers(const QJsonArray& markers) {
    QJsonObject msg;
    msg["type"] = "error";
    msg["markers"] = markers;

    QString script = QString("window.monacoEditor && window.monacoEditor.setMarkers(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

void MonacoBridge::setWarningMarkers(const QJsonArray& markers) {
    QJsonObject msg;
    msg["type"] = "warning";
    msg["markers"] = markers;

    QString script = QString("window.monacoEditor && window.monacoEditor.setMarkers(%1)")
                     .arg(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    executeJs(script);
}

// ============================
// Slots privados
// ============================

void MonacoBridge::onPageLoadFinished(bool ok) {
    d->editorReady = ok;
    if (ok) {
        // Inyectar WebChannel
        QString channelScript = R"(
            new QWebChannel(qt.webChannelTransport, function(channel) {
                window.monacoBridge = channel.objects.monacoBridge;
                window.postMessage({type: 'webChannelReady'}, '*');
            });
        )";
        d->editorPage->runJavaScript(channelScript);

        emit editorReady();
    } else {
        emit editorError("Fallo al cargar el editor Monaco");
    }
}

void MonacoBridge::onJavaScriptConsoleMessage(
    QWebEnginePage::JavaScriptConsoleMessageLevel level,
    const QString& message, int lineNumber, const QString& sourceID) {
    Q_UNUSED(level)
    Q_UNUSED(lineNumber)
    Q_UNUSED(sourceID)
    // Log para debugging
    if (message.startsWith("[MonacoContent]")) {
        // Parsear contenido del editor
        QString data = message.mid(15);
        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString filePath = obj.value("filePath").toString();
            QString content = obj.value("content").toString();
            emit contentChanged(filePath, content);
        }
    }
}

// ============================
// Helpers privados
// ============================

void MonacoBridge::injectEditorHtml() {
    // La pagina se construye en buildEditorPage()
}

void MonacoBridge::setupWebChannel() {
    // Configurado en initialize()
}

void MonacoBridge::executeJs(const QString& script) {
    if (!d->editorReady || !d->editorPage) {
        d->pendingScripts.append(script);
        return;
    }
    d->editorPage->runJavaScript(script);
}

QString MonacoBridge::detectLanguage(const QString& filePath) const {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    if (ext == "py") return "python";
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "h" || ext == "hpp") return "cpp";
    if (ext == "js") return "javascript";
    if (ext == "json") return "json";
    if (ext == "xml" || ext == "xlm") return "xml";
    if (ext == "html" || ext == "htm") return "html";
    if (ext == "css") return "css";
    if (ext == "md") return "markdown";
    if (ext == "sql") return "sql";
    if (ext == "sh" || ext == "bash") return "shell";
    if (ext == "yaml" || ext == "yml") return "yaml";
    if (ext == "cmake" || ext == "txt") return "plaintext";

    // Deteccion por contenido del nombre
    if (filePath.contains("CMakeLists")) return "cmake";
    if (filePath.contains("Makefile")) return "makefile";

    return "plaintext";
}

void MonacoBridge::buildEditorPage() {
    QString html = R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>POWSYS365 Monaco Editor</title>
<style>
    html, body { margin: 0; padding: 0; width: 100%; height: 100%; overflow: hidden; }
    #editor-container { width: 100%; height: 100%; }
    #tabs { height: 32px; background: #2d2d30; display: flex; align-items: center;
            border-bottom: 1px solid #3e3e42; overflow-x: auto; }
    .tab { padding: 6px 16px; color: #969696; font-family: 'Segoe UI', sans-serif;
           font-size: 12px; cursor: pointer; border-right: 1px solid #3e3e42;
           white-space: nowrap; user-select: none; }
    .tab.active { color: #ffffff; background: #1e1e1e; border-bottom: 2px solid #007acc; }
    .tab:hover { color: #ffffff; background: #2a2d2e; }
    #editor { width: 100%; height: calc(100% - 32px); }
</style>
</head>
<body>
<div id="tabs"></div>
<div id="editor-container">
    <div id="editor"></div>
</div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.44.0/min/vs/loader.min.js"></script>
<script>
    require.config({ paths: { 'vs': 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.44.0/min/vs' }});

    window.POWSYS365 = {
        files: {},
        currentFile: null,
        editor: null,
        completions: [],
        breakpoints: new Set()
    };

    require(['vs/editor/editor.main'], function() {
        window.POWSYS365.editor = monaco.editor.create(document.getElementById('editor'), {
            value: '// POWSYS365 IDE - Bienvenido',
            language: 'python',
            theme: 'vs-dark',
            automaticLayout: true,
            minimap: { enabled: true },
            fontSize: 13,
            fontFamily: 'Consolas, "Courier New", monospace',
            scrollBeyondLastLine: false,
            renderWhitespace: 'selection',
            bracketPairColorization: { enabled: true },
            wordWrap: 'on',
            lineNumbers: 'on',
            folding: true,
            suggestOnTriggerCharacters: true,
            quickSuggestions: true,
            tabSize: 4,
            insertSpaces: true
        });

        // Completado personalizado
        monaco.languages.registerCompletionItemProvider('python', {
            provideCompletionItems: function(model, position) {
                var suggestions = window.POWSYS365.completions.map(function(c) {
                    return {
                        label: c.label,
                        kind: monaco.languages.CompletionItemKind[c.kind || 'Text'],
                        detail: c.detail,
                        insertText: c.insertText || c.label,
                        insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet
                    };
                });
                return { suggestions: suggestions };
            }
        });

        // Evento de cambio de contenido
        window.POWSYS365.editor.onDidChangeModelContent(function() {
            if (window.POWSYS365.currentFile && window.monacoBridge) {
                var content = window.POWSYS365.editor.getValue();
                window.monacoBridge.onEditorContentChanged(
                    window.POWSYS365.currentFile, content);
            }
        });

        // Evento de cambio de cursor
        window.POWSYS365.editor.onDidChangeCursorPosition(function(e) {
            if (window.monacoBridge) {
                window.monacoBridge.onCursorPositionChanged(e.position.lineNumber,
                    e.position.column);
            }
        });

        // Notificar que el editor esta listo
        if (window.monacoBridge) {
            window.monacoBridge.onEditorReady();
        }
    });

    // API expuesta al C++ via WebChannel
    window.monacoEditor = {
        openFile: function(data) {
            window.POWSYS365.files[data.filePath] = {
                content: data.content,
                language: data.language
            };
            window.POWSYS365.currentFile = data.filePath;
            if (window.POWSYS365.editor) {
                monaco.editor.setModelLanguage(
                    window.POWSYS365.editor.getModel(), data.language);
                window.POWSYS365.editor.setValue(data.content);
            }
            updateTabs();
        },

        closeFile: function(data) {
            delete window.POWSYS365.files[data.filePath];
            if (window.POWSYS365.currentFile === data.filePath) {
                var keys = Object.keys(window.POWSYS365.files);
                window.POWSYS365.currentFile = keys.length > 0 ? keys[0] : null;
            }
            updateTabs();
        },

        setContent: function(data) {
            if (window.POWSYS365.files[data.filePath]) {
                window.POWSYS365.files[data.filePath].content = data.content;
            }
            if (window.POWSYS365.currentFile === data.filePath &&
                window.POWSYS365.editor) {
                window.POWSYS365.editor.setValue(data.content);
            }
        },

        requestContent: function(data) {
            if (window.POWSYS365.editor && window.monacoBridge) {
                var content = window.POWSYS365.editor.getValue();
                window.monacoBridge.onContentResponse(data.filePath, content);
            }
        },

        switchToFile: function(data) {
            if (window.POWSYS365.files[data.filePath]) {
                window.POWSYS365.currentFile = data.filePath;
                var file = window.POWSYS365.files[data.filePath];
                if (window.POWSYS365.editor) {
                    monaco.editor.setModelLanguage(
                        window.POWSYS365.editor.getModel(), file.language);
                    window.POWSYS365.editor.setValue(file.content);
                }
                updateTabs();
            }
        },

        setTheme: function(theme) {
            monaco.editor.setTheme(theme);
        },

        setLanguage: function(data) {
            if (window.POWSYS365.files[data.filePath]) {
                window.POWSYS365.files[data.filePath].language = data.language;
                if (window.POWSYS365.currentFile === data.filePath &&
                    window.POWSYS365.editor) {
                    monaco.editor.setModelLanguage(
                        window.POWSYS365.editor.getModel(), data.language);
                }
            }
        },

        insertText: function(text) {
            if (window.POWSYS365.editor) {
                window.POWSYS365.editor.trigger('keyboard', 'type', {text: text});
            }
        },

        replaceSelection: function(text) {
            if (window.POWSYS365.editor) {
                var sel = window.POWSYS365.editor.getSelection();
                window.POWSYS365.editor.executeEdits('bridge', [{
                    range: sel,
                    text: text
                }]);
            }
        },

        undo: function() {
            if (window.POWSYS365.editor) window.POWSYS365.editor.trigger('','undo');
        },

        redo: function() {
            if (window.POWSYS365.editor) window.POWSYS365.editor.trigger('','redo');
        },

        goToLine: function(line) {
            if (window.POWSYS365.editor) {
                window.POWSYS365.editor.revealLineInCenter(line);
                window.POWSYS365.editor.setPosition({lineNumber: line, column: 1});
            }
        },

        find: function(data) {
            if (window.POWSYS365.editor) {
                window.POWSYS365.editor.getAction('actions.find').run();
            }
        },

        formatDocument: function() {
            if (window.POWSYS365.editor) {
                window.POWSYS365.editor.getAction('editor.action.formatDocument').run();
            }
        },

        registerCompletionProvider: function(data) {
            monaco.languages.registerCompletionItemProvider(data.language, {
                provideCompletionItems: function(model, position) {
                    return {
                        suggestions: data.suggestions.map(function(s) {
                            return {
                                label: s.label,
                                kind: s.kind || monaco.languages.CompletionItemKind.Text,
                                detail: s.detail,
                                insertText: s.insertText || s.label
                            };
                        })
                    };
                }
            });
        },

        updateCompletions: function(data) {
            window.POWSYS365.completions = data.suggestions;
        },

        breakpointAction: function(data) {
            if (data.action === 'addBreakpoint') {
                window.POWSYS365.breakpoints.add(data.line);
            } else if (data.action === 'removeBreakpoint') {
                window.POWSYS365.breakpoints.delete(data.line);
            }
        },

        clearBreakpoints: function() {
            window.POWSYS365.breakpoints.clear();
        },

        highlightLine: function(data) {
            if (window.POWSYS365.editor) {
                window.POWSYS365.editor.deltaDecorations([], [{
                    range: new monaco.Range(data.line, 1, data.line, 1),
                    options: { isWholeLine: true, className: 'highlight-line',
                               inlineClassName: 'highlight-line' }
                }]);
            }
        },

        clearHighlights: function() {
            if (window.POWSYS365.editor) {
                window.POWSYS365.editor.deltaDecorations(
                    window.POWSYS365.editor.getModel().getAllDecorations()
                        .map(function(d) { return d.id; }), []);
            }
        },

        setMarkers: function(data) {
            var markers = data.markers.map(function(m) {
                return {
                    severity: data.type === 'error'
                        ? monaco.MarkerSeverity.Error
                        : monaco.MarkerSeverity.Warning,
                    message: m.message,
                    startLineNumber: m.line,
                    startColumn: m.column || 1,
                    endLineNumber: m.endLine || m.line,
                    endColumn: m.endColumn || 1000
                };
            });
            monaco.editor.setModelMarkers(
                window.POWSYS365.editor.getModel(), 'powsy365', markers);
        }
    };

    function updateTabs() {
        var tabsEl = document.getElementById('tabs');
        tabsEl.innerHTML = '';
        for (var path in window.POWSYS365.files) {
            var tab = document.createElement('div');
            tab.className = 'tab' + (path === window.POWSYS365.currentFile ? ' active' : '');
            tab.textContent = path.split('/').pop();
            tab.onclick = (function(p) {
                return function() { window.monacoEditor.switchToFile({filePath: p}); };
            })(path);
            tabsEl.appendChild(tab);
        }
    }
</script>
</body>
</html>
)";

    d->editorPage->setHtml(html, QUrl("qrc:/monaco-editor/"));
}

// ============================
// MonacoEditorPage
// ============================

MonacoEditorPage::MonacoEditorPage(QWebEngineProfile* profile, QObject* parent)
    : QWebEnginePage(profile, parent)
{
}

bool MonacoEditorPage::acceptNavigationRequest(const QUrl& url,
                                                QWebEnginePage::NavigationType type,
                                                bool isMainFrame) {
    Q_UNUSED(type)
    Q_UNUSED(isMainFrame)

    // Solo permitir URLs locales y el CDN de Monaco
    QString urlStr = url.toString();
    if (urlStr.startsWith("qrc:") ||
        urlStr.startsWith("https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/") ||
        urlStr.startsWith("data:")) {
        return true;
    }

    // Bloquear navegacion externa
    return false;
}

} // namespace ide
} // namespace powsys365
