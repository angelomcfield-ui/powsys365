#ifndef EDITOR_H
#define EDITOR_H

#include <QObject>

class Editor : public QObject
{
    Q_OBJECT

public:
    explicit Editor(QObject* parent = nullptr);

public slots:
    void openFile(const QString& filePath);
    void saveFile(const QString& filePath);
    void runScript();
};

#endif // EDITOR_H