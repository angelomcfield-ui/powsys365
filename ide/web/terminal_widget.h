// terminal_widget.h - Terminal Widget para POWSYS365 IDE
// Copyright (c) 2025 POWSYS365. All rights reserved.
#pragma once

#include <QWidget>
#include <QProcess>
#include <QTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QString>
#include <QVector>
#include <QRegularExpression>
#include <QMap>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QTimer>
#include <QDir>
#include <QList>
#include <memory>

namespace powsys365 {
namespace ide {

/**
 * @brief TerminalWidget - Widget de terminal integrado embebido.
 *
 * Proporciona una terminal interactiva con shell nativo del sistema,
 * soporte de colores ANSI, historial de comandos, y salida de procesos.
 * Utiliza QProcess para ejecutar el shell y QTextEdit para la visualizacion.
 */
class TerminalWidget : public QWidget {
    Q_OBJECT

    Q_PROPERTY(bool isRunning READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString currentShell READ currentShell NOTIFY shellChanged)
    Q_PROPERTY(int historySize READ historySize WRITE setHistorySize)

public:
    explicit TerminalWidget(QWidget* parent = nullptr);
    ~TerminalWidget();

    bool isRunning() const;
    QString currentShell() const;
    int historySize() const;
    void setHistorySize(int size);

    // ============================
    // Control del proceso
    // ============================
public slots:
    void startShell(const QString& shellPath = QString());
    void stopShell();
    void sendCommand(const QString& command);
    void clearOutput();
    void copySelection();
    void pasteClipboard();

    // ============================
    // Historial
    // ============================
    QStringList getHistory() const;
    void clearHistory();

signals:
    void runningChanged(bool running);
    void shellChanged(const QString& shell);
    void commandExecuted(const QString& command);
    void outputReceived(const QString& output);
    void shellFinished(int exitCode);
    void shellError(const QString& error);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void updateCursorPosition();

private:
    void setupUI();
    void setupConnections();
    void appendOutput(const QString& text, bool isError = false);
    void processAnsiCodes(const QString& text);
    void applyAnsiCode(int code);
    void resetFormatting();
    QString detectShell() const;
    QString translateShell(const QString& shell) const;
    void ensureVisible();
    void insertPrompt();

    // Colores ANSI
    QColor ansiColor(int code) const;
    void setForegroundColor(int ansiCode);
    void setBackgroundColor(int ansiCode);
    void setTextAttribute(int ansiCode);

    struct PrivateData;
    std::unique_ptr<PrivateData> d;
};

/**
 * @brief TerminalInput - Linea de entrada de la terminal.
 */
class TerminalInput : public QLineEdit {
    Q_OBJECT

public:
    explicit TerminalInput(QWidget* parent = nullptr);

    void setHistory(const QStringList& history);
    void addToHistory(const QString& command);
    void clearHistory();

signals:
    void commandSubmitted(const QString& command);
    void navigateHistoryUp();
    void navigateHistoryDown();
    void terminalKeyEvent(QKeyEvent* event);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QStringList m_history;
    int m_historyIndex = -1;
    QString m_currentInput;
    int m_maxHistorySize = 1000;
};

} // namespace ide
} // namespace powsys365
