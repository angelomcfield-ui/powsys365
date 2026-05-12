// terminal_widget.cpp - Implementacion de Terminal Widget para POWSYS365
// Copyright (c) 2025 POWSYS365. All rights reserved.

#include "terminal_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QDebug>
#include <QDir>
#include <QSplitter>
#include <QPushButton>

namespace powsys365 {
namespace ide {

// ============================
// ANSI Color Map
// ============================

namespace {
    const char* CSI = "\x1B[";

    // Colores ANSI estandar (0-15)
    QColor ansiBasicColor(int code) {
        switch (code) {
            case 0:  return QColor("#2e3436");  // Black
            case 1:  return QColor("#cc0000");  // Red
            case 2:  return QColor("#4e9a06");  // Green
            case 3:  return QColor("#c4a000");  // Yellow
            case 4:  return QColor("#3465a4");  // Blue
            case 5:  return QColor("#75507b");  // Magenta
            case 6:  return QColor("#06989a");  // Cyan
            case 7:  return QColor("#d3d7cf");  // White
            case 8:  return QColor("#555753");  // Bright Black
            case 9:  return QColor("#ef2929");  // Bright Red
            case 10: return QColor("#8ae234");  // Bright Green
            case 11: return QColor("#fce94f");  // Bright Yellow
            case 12: return QColor("#729fcf");  // Bright Blue
            case 13: return QColor("#ad7fa8");  // Bright Magenta
            case 14: return QColor("#34e2e2");  // Bright Cyan
            case 15: return QColor("#eeeeec");  // Bright White
            default: return QColor("#d3d7cf");
        }
    }

    // Colores de 256 (16-231 colores cubicos, 232-255 grises)
    QColor ansi256Color(int code) {
        if (code < 16) {
            return ansiBasicColor(code);
        } else if (code < 232) {
            int c = code - 16;
            int r = (c / 36) % 6;
            int g = (c / 6) % 6;
            int b = c % 6;
            int vals[] = {0, 95, 135, 175, 215, 255};
            return QColor(vals[r], vals[g], vals[b]);
        } else {
            int gray = code - 232;
            int v = 8 + gray * 10;
            return QColor(v, v, v);
        }
    }
}

// ============================
// Private Data
// ============================

struct TerminalWidget::PrivateData {
    QProcess* shellProcess = nullptr;
    QTextEdit* outputDisplay = nullptr;
    TerminalInput* inputLine = nullptr;
    bool isRunning = false;
    QString currentShellPath;
    QString currentPrompt;
    int historySize = 1000;
    QStringList commandHistory;
    QMap<int, QColor> foregroundColors;
    QMap<int, QColor> backgroundColors;

    // Estado de formato actual
    QColor currentFg = QColor("#d3d7cf");
    QColor currentBg = QColor("#1e1e1e");
    bool currentBold = false;
    bool currentItalic = false;
    bool currentUnderline = false;
    bool inEscapeSequence = false;
    QString escapeBuffer;
};

// ============================
// Constructor / Destructor
// ============================

TerminalWidget::TerminalWidget(QWidget* parent)
    : QWidget(parent)
    , d(std::make_unique<PrivateData>())
{
    setupUI();
    setupConnections();
    setTheme();
}

TerminalWidget::~TerminalWidget() {
    if (d->shellProcess && d->shellProcess->state() != QProcess::NotRunning) {
        d->shellProcess->terminate();
        if (!d->shellProcess->waitForFinished(3000)) {
            d->shellProcess->kill();
        }
    }
}

// ============================
// Q_PROPERTY getters/setters
// ============================

bool TerminalWidget::isRunning() const {
    return d->isRunning;
}

QString TerminalWidget::currentShell() const {
    return d->currentShellPath;
}

int TerminalWidget::historySize() const {
    return d->historySize;
}

void TerminalWidget::setHistorySize(int size) {
    d->historySize = size;
    if (d->inputLine) {
        d->inputLine->setHistory(d->commandHistory);
    }
}

// ============================
// Setup UI
// ============================

void TerminalWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Barra de herramientas superior
    QHBoxLayout* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(4, 2, 4, 2);
    toolbarLayout->setSpacing(4);

    QPushButton* clearBtn = new QPushButton("Clear", this);
    clearBtn->setMaximumWidth(60);
    clearBtn->setStyleSheet("QPushButton { background: #3c3c3c; color: #d4d4d4; "
                            "border: 1px solid #555; border-radius: 3px; padding: 2px 8px; "
                            "font-size: 11px; }"
                            "QPushButton:hover { background: #505050; }");
    connect(clearBtn, &QPushButton::clicked, this, &TerminalWidget::clearOutput);

    QPushButton* copyBtn = new QPushButton("Copy", this);
    copyBtn->setMaximumWidth(60);
    copyBtn->setStyleSheet("QPushButton { background: #3c3c3c; color: #d4d4d4; "
                           "border: 1px solid #555; border-radius: 3px; padding: 2px 8px; "
                           "font-size: 11px; }"
                           "QPushButton:hover { background: #505050; }");
    connect(copyBtn, &QPushButton::clicked, this, &TerminalWidget::copySelection);

    toolbarLayout->addWidget(clearBtn);
    toolbarLayout->addWidget(copyBtn);
    toolbarLayout->addStretch();

    mainLayout->addLayout(toolbarLayout);

    // Area de salida
    d->outputDisplay = new QTextEdit(this);
    d->outputDisplay->setReadOnly(false); // Permite seleccion
    d->outputDisplay->setAcceptRichText(false);
    d->outputDisplay->setWordWrapMode(QTextOption::WrapAnywhere);

    // Fuente monoespaciada
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(11);
    monoFont.setStyleHint(QFont::Monospace);
    d->outputDisplay->setFont(monoFont);

    d->outputDisplay->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #d4d4d4; "
        "border: none; selection-background-color: #264f78; }");

    d->outputDisplay->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    d->outputDisplay->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    mainLayout->addWidget(d->outputDisplay, 1);

    // Linea de entrada
    d->inputLine = new TerminalInput(this);
    d->inputLine->setFont(monoFont);
    d->inputLine->setStyleSheet(
        "QLineEdit { background-color: #252526; color: #d4d4d4; "
        "border: 1px solid #3e3e42; border-radius: 2px; padding: 4px 8px; }");
    d->inputLine->setPlaceholderText("Escriba un comando...");

    mainLayout->addWidget(d->inputLine);

    setLayout(mainLayout);
}

void TerminalWidget::setTheme() {
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor("#1e1e1e"));
    setPalette(p);
    setAutoFillBackground(true);
}

void TerminalWidget::setupConnections() {
    connect(d->inputLine, &TerminalInput::commandSubmitted,
            this, &TerminalWidget::sendCommand);

    // Deshabilitar input hasta que arranque el shell
    d->inputLine->setEnabled(false);
}

// ============================
// Control del proceso
// ============================

void TerminalWidget::startShell(const QString& shellPath) {
    if (d->isRunning) {
        return;
    }

    if (d->shellProcess) {
        delete d->shellProcess;
    }

    d->shellProcess = new QProcess(this);
    d->shellProcess->setProcessChannelMode(QProcess::MergedChannels);

    // Entorno personalizado
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("POWSYS365_IDE", "1");
    env.insert("POWSYS365_TERMINAL", "1");
    env.insert("TERM", "xterm-256color");
    env.insert("FORCE_COLOR", "1");
    env.insert("COLORTERM", "truecolor");
    d->shellProcess->setProcessEnvironment(env);

    QString shell = shellPath.isEmpty() ? detectShell() : shellPath;
    d->currentShellPath = shell;

    // Shell interactivo
    QStringList args;
#ifdef Q_OS_WIN
    if (shell.contains("cmd.exe", Qt::CaseInsensitive)) {
        args << "/Q" << "/K";
    } else if (shell.contains("powershell", Qt::CaseInsensitive)) {
        args << "-NoExit" << "-Command" << "-";
    }
#endif

    connect(d->shellProcess, &QProcess::readyReadStandardOutput,
            this, &TerminalWidget::onReadyReadStandardOutput);
    connect(d->shellProcess, &QProcess::readyReadStandardError,
            this, &TerminalWidget::onReadyReadStandardError);
    connect(d->shellProcess, QOverload<int>::of(&QProcess::finished),
            this, &TerminalWidget::onProcessFinished);
    connect(d->shellProcess, &QProcess::errorOccurred,
            this, &TerminalWidget::onProcessErrorOccurred);

    d->shellProcess->start(shell, args);

    if (!d->shellProcess->waitForStarted(5000)) {
        appendOutput(QString("[ERROR] No se pudo iniciar el shell: %1\n").arg(shell), true);
        return;
    }

    d->isRunning = true;
    d->inputLine->setEnabled(true);
    d->inputLine->setFocus();
    emit runningChanged(true);
    emit shellChanged(shell);

    appendOutput(QString("[Shell iniciado: %1]\n").arg(shell));
}

void TerminalWidget::stopShell() {
    if (!d->shellProcess || d->shellProcess->state() == QProcess::NotRunning) {
        d->isRunning = false;
        d->inputLine->setEnabled(false);
        emit runningChanged(false);
        return;
    }

    d->shellProcess->terminate();
    if (!d->shellProcess->waitForFinished(3000)) {
        d->shellProcess->kill();
        d->shellProcess->waitForFinished(1000);
    }

    d->isRunning = false;
    d->inputLine->setEnabled(false);
    emit runningChanged(false);
}

void TerminalWidget::sendCommand(const QString& command) {
    if (!d->isRunning || !d->shellProcess) {
        appendOutput("[ERROR] Shell no esta en ejecucion.\n", true);
        return;
    }

    // Escribir el comando al stdin del shell
    d->shellProcess->write(command.toUtf8());
    d->shellProcess->write("\n");
    d->shellProcess->waitForBytesWritten(1000);

    emit commandExecuted(command);

    // Agregar al historial
    if (!command.trimmed().isEmpty()) {
        d->commandHistory.append(command);
        if (d->commandHistory.size() > d->historySize) {
            d->commandHistory.removeFirst();
        }
        d->inputLine->addToHistory(command);
    }
}

void TerminalWidget::clearOutput() {
    d->outputDisplay->clear();
}

void TerminalWidget::copySelection() {
    d->outputDisplay->copy();
}

void TerminalWidget::pasteClipboard() {
    QClipboard* clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    if (!text.isEmpty()) {
        d->inputLine->insert(text);
    }
}

// ============================
// Historial
// ============================

QStringList TerminalWidget::getHistory() const {
    return d->commandHistory;
}

void TerminalWidget::clearHistory() {
    d->commandHistory.clear();
}

// ============================
// Eventos
// ============================

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier) {
        copySelection();
        return;
    }
    if (event->key() == Qt::Key_V && event->modifiers() == Qt::ControlModifier) {
        pasteClipboard();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Asegurar que el scroll este al final
    QTimer::singleShot(10, this, &TerminalWidget::ensureVisible);
}

// ============================
// Slots privados
// ============================

void TerminalWidget::onReadyReadStandardOutput() {
    if (!d->shellProcess) return;
    QByteArray output = d->shellProcess->readAllStandardOutput();
    QString text = QString::fromUtf8(output);
    appendOutput(text);
    emit outputReceived(text);
}

void TerminalWidget::onReadyReadStandardError() {
    if (!d->shellProcess) return;
    QByteArray output = d->shellProcess->readAllStandardError();
    appendOutput(QString::fromUtf8(output), true);
}

void TerminalWidget::onProcessFinished(int exitCode) {
    d->isRunning = false;
    d->inputLine->setEnabled(false);
    emit runningChanged(false);
    emit shellFinished(exitCode);
    appendOutput(QString("\n[Shell finalizado con codigo %1]\n").arg(exitCode), true);
}

void TerminalWidget::onProcessErrorOccurred(QProcess::ProcessError error) {
    QString errorStr;
    switch (error) {
        case QProcess::FailedToStart:
            errorStr = "Fallo al iniciar el proceso"; break;
        case QProcess::Crashed:
            errorStr = "El proceso termino inesperadamente"; break;
        case QProcess::Timedout:
            errorStr = "Timeout del proceso"; break;
        case QProcess::WriteError:
            errorStr = "Error de escritura"; break;
        case QProcess::ReadError:
            errorStr = "Error de lectura"; break;
        case QProcess::UnknownError:
        default:
            errorStr = "Error desconocido"; break;
    }
    appendOutput(QString("\n[ERROR] %1\n").arg(errorStr), true);
    emit shellError(errorStr);
}

void TerminalWidget::updateCursorPosition() {
    // Cursor siempre al final
}

// ============================
// Salida con colores ANSI
// ============================

void TerminalWidget::appendOutput(const QString& text, bool isError) {
    QTextCursor cursor(d->outputDisplay->textCursor());
    cursor.movePosition(QTextCursor::End);

    if (isError) {
        QTextCharFormat fmt;
        fmt.setForeground(QColor("#f48771"));
        cursor.setCharFormat(fmt);
        cursor.insertText(text);
    } else {
        processAnsiCodes(text);
    }

    ensureVisible();
}

void TerminalWidget::processAnsiCodes(const QString& text) {
    QTextCursor cursor(d->outputDisplay->textCursor());
    cursor.movePosition(QTextCursor::End);

    QString buffer;

    for (int i = 0; i < text.length(); ++i) {
        QChar c = text[i];

        if (c == '\x1B') {
            // Escribir buffer pendiente
            if (!buffer.isEmpty()) {
                QTextCharFormat fmt;
                fmt.setForeground(d->currentFg);
                fmt.setBackground(d->currentBg);
                fmt.setFontWeight(d->currentBold ? QFont::Bold : QFont::Normal);
                fmt.setFontItalic(d->currentItalic);
                fmt.setFontUnderline(d->currentUnderline);
                cursor.setCharFormat(fmt);
                cursor.insertText(buffer);
                buffer.clear();
            }
            d->inEscapeSequence = true;
            d->escapeBuffer.clear();
            continue;
        }

        if (d->inEscapeSequence) {
            d->escapeBuffer.append(c);

            // Verificar si la secuencia esta completa
            if (c.isLetter() || c == '~') {
                d->inEscapeSequence = false;

                // Parsear CSI sequences: ESC [ ... m (SGR)
                if (d->escapeBuffer.startsWith("[")) {
                    QString params = d->escapeBuffer.mid(1, d->escapeBuffer.length() - 2);
                    QStringList codes = params.split(';');

                    if (c == 'm') { // SGR - Select Graphic Rendition
                        for (const QString& codeStr : codes) {
                            bool ok;
                            int code = codeStr.toInt(&ok);
                            if (!ok) continue;
                            applyAnsiCode(code);
                        }
                    } else if (c == 'K') { // Clear line
                        // NOP - QTextEdit no soporta borrado parcial facilmente
                    } else if (c == 'J') { // Clear screen
                        if (params == "2" || params == "3") {
                            d->outputDisplay->clear();
                        }
                    } else if (c == 'H' || c == 'f') { // Cursor position
                        // NOP
                    }
                }

                d->escapeBuffer.clear();
            }

            // Secuencias muy largas son invalidas
            if (d->escapeBuffer.length() > 32) {
                d->inEscapeSequence = false;
                d->escapeBuffer.clear();
            }
            continue;
        }

        // Caracter normal
        if (c == '\r') {
            // Escribir buffer
            if (!buffer.isEmpty()) {
                QTextCharFormat fmt;
                fmt.setForeground(d->currentFg);
                fmt.setBackground(d->currentBg);
                fmt.setFontWeight(d->currentBold ? QFont::Bold : QFont::Normal);
                fmt.setFontItalic(d->currentItalic);
                fmt.setFontUnderline(d->currentUnderline);
                cursor.setCharFormat(fmt);
                cursor.insertText(buffer);
                buffer.clear();
            }
            // Mover al inicio de la linea
            cursor.movePosition(QTextCursor::StartOfLine);
            continue;
        }

        buffer.append(c);
    }

    // Escribir buffer restante
    if (!buffer.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setForeground(d->currentFg);
        fmt.setBackground(d->currentBg);
        fmt.setFontWeight(d->currentBold ? QFont::Bold : QFont::Normal);
        fmt.setFontItalic(d->currentItalic);
        fmt.setFontUnderline(d->currentUnderline);
        cursor.setCharFormat(fmt);
        cursor.insertText(buffer);
    }
}

void TerminalWidget::applyAnsiCode(int code) {
    if (code == 0) {
        resetFormatting();
    } else if (code == 1) {
        d->currentBold = true;
    } else if (code == 3) {
        d->currentItalic = true;
    } else if (code == 4) {
        d->currentUnderline = true;
    } else if (code >= 30 && code <= 37) {
        d->currentFg = ansiBasicColor(code - 30);
    } else if (code == 38) {
        // Foreground 256 color - necesita parametro adicional
    } else if (code == 39) {
        d->currentFg = QColor("#d4d4d4");
    } else if (code >= 40 && code <= 47) {
        d->currentBg = ansiBasicColor(code - 40);
    } else if (code == 48) {
        // Background 256 color
    } else if (code == 49) {
        d->currentBg = QColor("#1e1e1e");
    } else if (code >= 90 && code <= 97) {
        d->currentFg = ansiBasicColor(code - 90 + 8); // Bright colors
    } else if (code >= 100 && code <= 107) {
        d->currentBg = ansiBasicColor(code - 100 + 8); // Bright background
    }
}

void TerminalWidget::resetFormatting() {
    d->currentFg = QColor("#d4d4d4");
    d->currentBg = QColor("#1e1e1e");
    d->currentBold = false;
    d->currentItalic = false;
    d->currentUnderline = false;
}

// ============================
// Deteccion de shell
// ============================

QString TerminalWidget::detectShell() const {
#ifdef Q_OS_MACOS
    QString shell = qEnvironmentVariable("SHELL", "/bin/zsh");
    if (!QFile::exists(shell)) shell = "/bin/bash";
    if (!QFile::exists(shell)) shell = "/bin/sh";
    return shell;
#elif defined(Q_OS_LINUX)
    QString shell = qEnvironmentVariable("SHELL", "/bin/bash");
    if (!QFile::exists(shell)) shell = "/bin/bash";
    if (!QFile::exists(shell)) shell = "/bin/sh";
    return shell;
#elif defined(Q_OS_WIN)
    QString shell = qEnvironmentVariable("COMSPEC", "cmd.exe");
    QString fullPath = QDir::rootPath() + "Windows/System32/" + shell;
    if (QFile::exists(fullPath)) return fullPath;
    // Intentar PowerShell
    QString psPath = QDir::rootPath() + "Windows/System32/WindowsPowerShell/v1.0/powershell.exe";
    if (QFile::exists(psPath)) return psPath;
    return shell;
#else
    return "/bin/sh";
#endif
}

QString TerminalWidget::translateShell(const QString& shell) const {
    if (shell.contains("zsh", Qt::CaseInsensitive)) return "zsh";
    if (shell.contains("bash", Qt::CaseInsensitive)) return "bash";
    if (shell.contains("cmd.exe", Qt::CaseInsensitive)) return "cmd";
    if (shell.contains("powershell", Qt::CaseInsensitive)) return "powershell";
    return shell;
}

void TerminalWidget::ensureVisible() {
    QScrollBar* scrollBar = d->outputDisplay->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void TerminalWidget::insertPrompt() {
    // NOP - el shell maneja su propio prompt
}

// ============================
// Colores ANSI
// ============================

QColor TerminalWidget::ansiColor(int code) const {
    return ansi256Color(code);
}

void TerminalWidget::setForegroundColor(int ansiCode) {
    d->currentFg = ansi256Color(ansiCode);
}

void TerminalWidget::setBackgroundColor(int ansiCode) {
    d->currentBg = ansi256Color(ansiCode);
}

void TerminalWidget::setTextAttribute(int ansiCode) {
    if (ansiCode == 1) d->currentBold = true;
    else if (ansiCode == 3) d->currentItalic = true;
    else if (ansiCode == 4) d->currentUnderline = true;
}

// ============================
// TerminalInput Implementation
// ============================

TerminalInput::TerminalInput(QWidget* parent)
    : QLineEdit(parent)
{
}

void TerminalInput::setHistory(const QStringList& history) {
    m_history = history;
    m_historyIndex = -1;
}

void TerminalInput::addToHistory(const QString& command) {
    if (command.trimmed().isEmpty()) return;
    m_history.append(command);
    while (m_history.size() > m_maxHistorySize) {
        m_history.removeFirst();
    }
    m_historyIndex = -1;
}

void TerminalInput::clearHistory() {
    m_history.clear();
    m_historyIndex = -1;
}

void TerminalInput::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!text().isEmpty()) {
                emit commandSubmitted(text());
                clear();
            }
            return;

        case Qt::Key_Up:
            if (!m_history.isEmpty()) {
                if (m_historyIndex < 0) {
                    m_currentInput = text();
                    m_historyIndex = m_history.size() - 1;
                } else if (m_historyIndex > 0) {
                    --m_historyIndex;
                }
                setText(m_history[m_historyIndex]);
                setCursorPosition(text().length());
            }
            emit navigateHistoryUp();
            return;

        case Qt::Key_Down:
            if (m_historyIndex >= 0) {
                ++m_historyIndex;
                if (m_historyIndex >= m_history.size()) {
                    m_historyIndex = -1;
                    setText(m_currentInput);
                } else {
                    setText(m_history[m_historyIndex]);
                }
                setCursorPosition(text().length());
            }
            emit navigateHistoryDown();
            return;

        case Qt::Key_C:
            if (event->modifiers() & Qt::ControlModifier) {
                emit terminalKeyEvent(event);
                return;
            }
            break;

        case Qt::Key_L:
            if (event->modifiers() & Qt::ControlModifier) {
                emit terminalKeyEvent(event);
                return;
            }
            break;

        case Qt::Key_D:
            if (event->modifiers() & Qt::ControlModifier) {
                // Ctrl+D = EOF
                emit commandSubmitted(QString("\x04"));
                return;
            }
            break;

        case Qt::Key_U:
            if (event->modifiers() & Qt::ControlModifier) {
                clear();
                return;
            }
            break;

        default:
            break;
    }

    QLineEdit::keyPressEvent(event);
}

} // namespace ide
} // namespace powsys365
