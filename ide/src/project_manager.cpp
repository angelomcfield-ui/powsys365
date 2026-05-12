#include "powsys365/ide/project_manager.h"
#include <QDebug>

ProjectManager::ProjectManager(QObject* parent)
    : QObject(parent)
{
}

QStringList ProjectManager::getProjectFiles()
{
    // Return list of project files
    return QStringList();
}

void ProjectManager::createProject(const QString& name)
{
    qDebug() << "Creating project:" << name;
    // Implement project creation
}

void ProjectManager::openProject(const QString& path)
{
    qDebug() << "Opening project:" << path;
    // Implement project opening
}