#ifndef PROJECT_MANAGER_H
#define PROJECT_MANAGER_H

#include <QObject>
#include <QStringList>

class ProjectManager : public QObject
{
    Q_OBJECT

public:
    explicit ProjectManager(QObject* parent = nullptr);

    QStringList getProjectFiles();

public slots:
    void createProject(const QString& name);
    void openProject(const QString& path);
};

#endif // PROJECT_MANAGER_H