/**
 * @file main.cpp
 * @brief POWSYS365 Application Entry Point
 *
 * Initializes Qt 6 with:
 *   - High DPI support
 *   - SF Pro font registration
 *   - macOS style
 *   - C++ controllers exposed to QML
 *   - QQmlApplicationEngine loading main.qml
 */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFontDatabase>
#include <QFont>
#include <QDir>
#include <QQuickStyle>
#include <QIcon>

#include "cpp/main_window_controller.h"
#include "cpp/sld_scene.h"
#include "cpp/theme_manager.h"

int main(int argc, char *argv[])
{
    // ── High DPI & Application attributes ────────────────────────────────
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Enable RHI by default for optimal rendering
    qputenv("QSG_RHI", "1");
    // Prefer OpenGL or Metal depending on platform
#if defined(Q_OS_MACOS)
    qputenv("QSG_RHI_BACKEND", "metal");
#elif defined(Q_OS_WIN)
    qputenv("QSG_RHI_BACKEND", "d3d11");
#else
    qputenv("QSG_RHI_BACKEND", "opengl");
#endif
#endif

    QGuiApplication app(argc, argv);

    // ── Application metadata ─────────────────────────────────────────────
    app.setOrganizationName("POWSYS365");
    app.setOrganizationDomain("powsys365.dev");
    app.setApplicationName("POWSYS365");
    app.setApplicationVersion("1.0.0");

    // ── Style ────────────────────────────────────────────────────────────
    QQuickStyle::setStyle("macOS");

    // ── Font Registration ────────────────────────────────────────────────
    // Attempt to register SF Pro fonts (will use system fonts on macOS,
    // fallbacks on other platforms)
    QStringList fontPaths = {
        "/System/Library/Fonts/",
        "/Library/Fonts/",
        QDir::homePath() + "/Library/Fonts/",
        ":/fonts/"
    };

    int registeredFonts = 0;

    // SF Pro Display
    for (const QString &path : fontPaths) {
        QDir dir(path);
        QStringList sfProDisplay = dir.entryList(
            {"*SFProDisplay*", "SF-Pro-Display*", "SFProDisplay-Regular.otf"},
            QDir::Files);
        for (const QString &fontFile : sfProDisplay) {
            int id = QFontDatabase::addApplicationFont(dir.absoluteFilePath(fontFile));
            if (id != -1) registeredFonts++;
        }
    }

    // SF Pro Text
    for (const QString &path : fontPaths) {
        QDir dir(path);
        QStringList sfProText = dir.entryList(
            {"*SFProText*", "SF-Pro-Text*", "SFProText-Regular.otf"},
            QDir::Files);
        for (const QString &fontFile : sfProText) {
            int id = QFontDatabase::addApplicationFont(dir.absoluteFilePath(fontFile));
            if (id != -1) registeredFonts++;
        }
    }

    // SF Mono
    for (const QString &path : fontPaths) {
        QDir dir(path);
        QStringList sfMono = dir.entryList(
            {"*SFMono*", "SF-Mono*", "SFMono-Regular.otf"},
            QDir::Files);
        for (const QString &fontFile : sfMono) {
            int id = QFontDatabase::addApplicationFont(dir.absoluteFilePath(fontFile));
            if (id != -1) registeredFonts++;
        }
    }

    // Set default application font (SF Pro Text, fallback to system sans-serif)
    QFont defaultFont;
    QStringList preferredFonts = {"SF Pro Text", "SFProText", "SF Pro Display",
                                   "SFProDisplay", ".SF NS Text", "Helvetica Neue"};
    for (const QString &fontName : preferredFonts) {
        QFont testFont(fontName);
        if (testFont.exactMatch() || QFontDatabase::hasFamily(fontName)) {
            defaultFont.setFamily(fontName);
            break;
        }
    }
    defaultFont.setPointSize(13);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(defaultFont);

    qDebug() << "POWSYS365: Registered" << registeredFonts << "custom fonts";
    qDebug() << "POWSYS365: Default font:" << app.font().family();

    // ── Register QML types ───────────────────────────────────────────────
    // Register controllers as QML types (can also be used via qmlRegisterType)
    qmlRegisterType<MainWindowController>("POWSYS365.Controllers", 1, 0, "MainWindowController");
    qmlRegisterType<SLDSceneController>("POWSYS365.Controllers", 1, 0, "SLDSceneController");
    qmlRegisterType<ThemeManager>("POWSYS365.Controllers", 1, 0, "ThemeManager");

    // ── Create C++ controllers ───────────────────────────────────────────
    MainWindowController mainController;
    SLDSceneController sldController;
    ThemeManager themeManager;

    // ── Setup QML Engine ─────────────────────────────────────────────────
    QQmlApplicationEngine engine;

    // Expose controllers as context properties (accessible from any QML)
    engine.rootContext()->setContextProperty("mainWindowController", &mainController);
    engine.rootContext()->setContextProperty("sldSceneController", &sldController);
    engine.rootContext()->setContextProperty("themeMgr", &themeManager);

    // Also expose with shorter names for convenience
    engine.rootContext()->setContextProperty("mainCtrl", &mainController);
    engine.rootContext()->setContextProperty("sldCtrl", &sldController);

    // Connect SLD controller to main controller data flow
    QObject::connect(&mainController, &MainWindowController::busDataReady,
                     &sldController, &SLDSceneController::setBusData);
    QObject::connect(&mainController, &MainWindowController::lineDataReady,
                     &sldController, &SLDSceneController::setLineData);
    QObject::connect(&mainController, &MainWindowController::generatorDataReady,
                     &sldController, &SLDSceneController::setGeneratorData);
    QObject::connect(&mainController, &MainWindowController::loadDataReady,
                     &sldController, &SLDSceneController::setLoadData);

    // Auto-layout when bus data changes
    QObject::connect(&mainController, &MainWindowController::busDataReady,
                     [&sldController]() {
                         sldController.computeAutoLayout(1200, 800);
                     });

    // ── Load main QML ────────────────────────────────────────────────────
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            qCritical() << "Failed to create main QML object";
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "No root QML objects loaded";
        return -1;
    }

    return app.exec();
}
