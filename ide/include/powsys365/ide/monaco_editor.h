#ifndef MONACO_EDITOR_H
#define MONACO_EDITOR_H

#include <QObject>
#include <QWebEngineView>

class MonacoEditor : public QWebEngineView
{
    Q_OBJECT

public:
    explicit MonacoEditor(QWidget* parent = nullptr);

public slots:
    void setContent(const QString& content);
    QString getContent();
};

#endif // MONACO_EDITOR_H