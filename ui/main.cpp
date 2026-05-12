#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QDir>
#include <QStandardPaths>
#include <QIcon>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("POWSYS365");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("POWSYS365");
    app.setOrganizationDomain("powsys365.com");

    // Set application icon
    app.setWindowIcon(QIcon(":/icons/app_icon.png"));

    // Set style to macOS style
    QQuickStyle::setStyle("macOS");

    QQmlApplicationEngine engine;

    // Add import path for QML modules
    engine.addImportPath("qrc:/");

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}