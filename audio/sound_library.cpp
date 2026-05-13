#include "sound_library.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QMutexLocker>
#include <QRegularExpression>

namespace powsys365::audio {

class SoundLibrary::Impl {
public:
    QMap<QString, SoundDefinition> sounds;
    mutable QMutex mutex;
    QStringList validationErrors;

    void buildDefaultDefinitions() {
        sounds.clear();

        // === System Sounds ===
        sounds.insert("success", SoundDefinition{
            "success", "", "system", 0.5f, 1.0f, false,
            "Operation completed successfully", "ui,feedback,positive", true, true, 100
        });
        sounds.insert("error", SoundDefinition{
            "error", "", "system", 0.6f, 1.0f, false,
            "Error occurred", "ui,feedback,negative", true, true, 100
        });
        sounds.insert("warning", SoundDefinition{
            "warning", "", "system", 0.5f, 1.0f, false,
            "Warning notification", "ui,feedback,caution", true, true, 90
        });
        sounds.insert("click", SoundDefinition{
            "click", "", "system", 0.3f, 1.0f, false,
            "UI button click", "ui,interaction", true, true, 80
        });
        sounds.insert("hover", SoundDefinition{
            "hover", "", "system", 0.3f, 1.0f, false,
            "UI hover sound", "ui,interaction", true, true, 70
        });
        sounds.insert("alarm", SoundDefinition{
            "alarm", "", "system", 0.8f, 1.0f, false,
            "Critical alarm", "alert,urgent", true, true, 100
        });
        sounds.insert("notification", SoundDefinition{
            "notification", "", "system", 0.5f, 1.0f, false,
            "General notification", "ui,feedback,info", true, true, 85
        });
        sounds.insert("info", SoundDefinition{
            "info", "", "system", 0.4f, 1.0f, false,
            "Information notification", "ui,feedback,info", true, true, 75
        });
        sounds.insert("startup", SoundDefinition{
            "startup", "", "system", 0.6f, 1.0f, false,
            "Application startup", "ui,system", true, true, 95
        });
        sounds.insert("shutdown", SoundDefinition{
            "shutdown", "", "system", 0.5f, 1.0f, false,
            "Application shutdown", "ui,system", true, true, 95
        });
        sounds.insert("save", SoundDefinition{
            "save", "", "system", 0.4f, 1.0f, false,
            "File saved", "ui,file,positive", true, true, 80
        });
        sounds.insert("open", SoundDefinition{
            "open", "", "system", 0.4f, 1.0f, false,
            "File opened", "ui,file", true, true, 75
        });
        sounds.insert("close", SoundDefinition{
            "close", "", "system", 0.3f, 1.0f, false,
            "File or tab closed", "ui,file", true, true, 70
        });
        sounds.insert("compile_ok", SoundDefinition{
            "compile_ok", "", "system", 0.5f, 1.0f, false,
            "Compilation successful", "build,positive,feedback", true, true, 95
        });
        sounds.insert("compile_err", SoundDefinition{
            "compile_err", "", "system", 0.6f, 1.0f, false,
            "Compilation failed", "build,negative,feedback", true, true, 95
        });
        sounds.insert("breakpoint", SoundDefinition{
            "breakpoint", "", "system", 0.6f, 1.0f, false,
            "Breakpoint hit", "debug,notification", true, true, 100
        });
        sounds.insert("step", SoundDefinition{
            "step", "", "system", 0.3f, 1.0f, false,
            "Debugger step", "debug,interaction", true, true, 80
        });
        sounds.insert("search", SoundDefinition{
            "search", "", "system", 0.4f, 1.0f, false,
            "Search completed", "ui,search", true, true, 75
        });
        sounds.insert("connect", SoundDefinition{
            "connect", "", "system", 0.5f, 1.0f, false,
            "Connection established", "network,positive", true, true, 85
        });
        sounds.insert("disconnect", SoundDefinition{
            "disconnect", "", "system", 0.5f, 1.0f, false,
            "Connection lost", "network,negative", true, true, 85
        });
        sounds.insert("commit", SoundDefinition{
            "commit", "", "system", 0.4f, 1.0f, false,
            "Git commit done", "git,vcs,positive", true, true, 80
        });
        sounds.insert("sync", SoundDefinition{
            "sync", "", "system", 0.4f, 1.0f, false,
            "Synchronization complete", "sync,positive", true, true, 75
        });
        sounds.insert("bell", SoundDefinition{
            "bell", "", "system", 0.4f, 1.0f, false,
            "Terminal bell", "terminal,alert", true, true, 70
        });

        // === Ambient Sounds ===
        sounds.insert("ambient_office", SoundDefinition{
            "ambient_office", "sounds/ambient/office.wav", "ambient", 0.3f, 1.0f, true,
            "Office ambient background", "background,office", false, false, 30
        });
        sounds.insert("ambient_lab", SoundDefinition{
            "ambient_lab", "sounds/ambient/lab.wav", "ambient", 0.2f, 1.0f, true,
            "Electrical lab ambient", "background,lab,electrical", false, false, 30
        });

        // === Alert Sounds ===
        sounds.insert("alert_low", SoundDefinition{
            "alert_low", "sounds/alerts/low.wav", "alert", 0.6f, 1.0f, false,
            "Low priority alert", "alert,low", false, false, 60
        });
        sounds.insert("alert_high", SoundDefinition{
            "alert_high", "sounds/alerts/high.wav", "alert", 0.8f, 1.0f, false,
            "High priority alert", "alert,high,urgent", false, false, 90
        });
        sounds.insert("alert_critical", SoundDefinition{
            "alert_critical", "sounds/alerts/critical.wav", "alert", 1.0f, 1.0f, false,
            "Critical alert", "alert,critical,urgent", false, false, 100
        });

        // === Feedback Sounds ===
        sounds.insert("feedback_ok", SoundDefinition{
            "feedback_ok", "sounds/feedback/ok.wav", "feedback", 0.4f, 1.0f, false,
            "OK confirmation", "feedback,positive", false, false, 70
        });
        sounds.insert("feedback_cancel", SoundDefinition{
            "feedback_cancel", "sounds/feedback/cancel.wav", "feedback", 0.4f, 0.9f, false,
            "Cancel/negative feedback", "feedback,negative", false, false, 65
        });
    }
};

SoundLibrary::SoundLibrary(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
}

SoundLibrary::~SoundLibrary() = default;

bool SoundLibrary::loadLibrary(const QString& jsonFilePath) {
    QFile file(jsonFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT error(QString("Cannot open library file: %1").arg(jsonFilePath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    return loadLibraryFromData(data);
}

bool SoundLibrary::loadLibraryFromJson(const QJsonObject& libraryJson) {
    QMutexLocker lock(&d->mutex);

    QJsonArray soundsArray = libraryJson.value("sounds").toArray();
    if (soundsArray.isEmpty()) {
        QJsonObject soundsObj = libraryJson.value("sounds").toObject();
        if (!soundsObj.isEmpty()) {
            // Handle object format: {"soundId": {...}, ...}
            for (auto it = soundsObj.begin(); it != soundsObj.end(); ++it) {
                SoundDefinition def = SoundDefinition::fromJson(it.value().toObject());
                if (def.id.isEmpty()) def.id = it.key();
                d->sounds[def.id] = def;
            }
        } else {
            // Try parsing as array of definitions directly
            for (const auto& val : libraryJson) {
                if (val.isObject()) {
                    SoundDefinition def = SoundDefinition::fromJson(val.toObject());
                    if (!def.id.isEmpty()) {
                        d->sounds[def.id] = def;
                    }
                }
            }
        }
    } else {
        for (const auto& val : soundsArray) {
            SoundDefinition def = SoundDefinition::fromJson(val.toObject());
            if (!def.id.isEmpty()) {
                d->sounds[def.id] = def;
            }
        }
    }

    Q_EMIT libraryLoaded();
    return true;
}

bool SoundLibrary::loadLibraryFromData(const QByteArray& data) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        Q_EMIT error(QString("JSON parse error: %1").arg(parseError.errorString()));
        return false;
    }
    return loadLibraryFromJson(doc.object());
}

bool SoundLibrary::saveLibrary(const QString& jsonFilePath) const {
    QMutexLocker lock(&d->mutex);

    QJsonObject libraryJson = toJson();
    QJsonDocument doc(libraryJson);

    QFile file(jsonFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        Q_EMIT error(QString("Cannot write library file: %1").arg(jsonFilePath));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonObject SoundLibrary::toJson() const {
    QMutexLocker lock(&d->mutex);

    QJsonObject root;
    QJsonArray soundsArray;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        soundsArray.append(it.value().toJson());
    }
    root["sounds"] = soundsArray;
    root["version"] = "1.0";
    root["count"] = d->sounds.size();

    // Add categories summary
    QJsonArray categories;
    QStringList cats = listCategories();
    for (const QString& cat : cats) {
        QJsonObject catObj;
        catObj["name"] = cat;
        catObj["count"] = listSoundsByCategory(cat).size();
        categories.append(catObj);
    }
    root["categories"] = categories;

    return root;
}

bool SoundLibrary::loadDefaultLibrary() {
    buildDefaultLibrary();
    Q_EMIT libraryLoaded();
    return true;
}

void SoundLibrary::buildDefaultLibrary() {
    QMutexLocker lock(&d->mutex);
    d->buildDefaultDefinitions();
}

bool SoundLibrary::addSound(const SoundDefinition& definition) {
    QMutexLocker lock(&d->mutex);

    if (definition.id.isEmpty()) {
        Q_EMIT error("Sound ID cannot be empty");
        return false;
    }

    if (d->sounds.contains(definition.id)) {
        Q_EMIT error(QString("Sound already exists: %1").arg(definition.id));
        return false;
    }

    d->sounds[definition.id] = definition;
    Q_EMIT soundAdded(definition.id);
    return true;
}

bool SoundLibrary::removeSound(const QString& id) {
    QMutexLocker lock(&d->mutex);

    auto it = d->sounds.find(id);
    if (it == d->sounds.end()) {
        Q_EMIT error(QString("Sound not found: %1").arg(id));
        return false;
    }

    d->sounds.erase(it);
    Q_EMIT soundRemoved(id);
    return true;
}

bool SoundLibrary::updateSound(const QString& id, const SoundDefinition& definition) {
    QMutexLocker lock(&d->mutex);

    if (!d->sounds.contains(id)) {
        Q_EMIT error(QString("Sound not found: %1").arg(id));
        return false;
    }

    d->sounds[id] = definition;
    Q_EMIT soundUpdated(id);
    return true;
}

bool SoundLibrary::hasSound(const QString& id) const {
    QMutexLocker lock(&d->mutex);
    return d->sounds.contains(id);
}

SoundDefinition SoundLibrary::getSound(const QString& id) const {
    QMutexLocker lock(&d->mutex);
    return d->sounds.value(id);
}

QStringList SoundLibrary::listSounds() const {
    QMutexLocker lock(&d->mutex);
    return d->sounds.keys();
}

QStringList SoundLibrary::listSoundsByCategory(const QString& category) const {
    QMutexLocker lock(&d->mutex);
    QStringList result;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        if (it.value().category == category) {
            result.append(it.key());
        }
    }
    return result;
}

QStringList SoundLibrary::listCategories() const {
    QMutexLocker lock(&d->mutex);
    QSet<QString> cats;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        cats.insert(it.value().category);
    }
    QStringList result = cats.values();
    result.sort();
    return result;
}

QList<SoundDefinition> SoundLibrary::getSoundsByCategory(const QString& category) const {
    QMutexLocker lock(&d->mutex);
    QList<SoundDefinition> result;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        if (it.value().category == category) {
            result.append(it.value());
        }
    }
    return result;
}

QList<SoundDefinition> SoundLibrary::searchSounds(const QString& query) const {
    QMutexLocker lock(&d->mutex);
    QList<SoundDefinition> result;

    if (query.isEmpty()) {
        for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
            result.append(it.value());
        }
        return result;
    }

    QString lowerQuery = query.toLower();
    QRegularExpression re(lowerQuery, QRegularExpression::CaseInsensitiveOption);

    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        const SoundDefinition& def = it.value();
        if (def.id.contains(lowerQuery, Qt::CaseInsensitive) ||
            def.description.contains(lowerQuery, Qt::CaseInsensitive) ||
            def.tags.contains(lowerQuery, Qt::CaseInsensitive) ||
            def.category.contains(lowerQuery, Qt::CaseInsensitive)) {
            result.append(def);
        }
    }

    return result;
}

QList<SoundDefinition> SoundLibrary::getSystemSounds() const {
    QMutexLocker lock(&d->mutex);
    QList<SoundDefinition> result;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        if (it.value().isSystemSound) {
            result.append(it.value());
        }
    }
    return result;
}

QList<SoundDefinition> SoundLibrary::getPreloadSounds() const {
    QMutexLocker lock(&d->mutex);
    QList<SoundDefinition> result;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        if (it.value().isPreload) {
            result.append(it.value());
        }
    }
    return result;
}

int SoundLibrary::soundCount() const {
    QMutexLocker lock(&d->mutex);
    return d->sounds.size();
}

int SoundLibrary::categoryCount() const {
    QMutexLocker lock(&d->mutex);
    QSet<QString> cats;
    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        cats.insert(it.value().category);
    }
    return cats.size();
}

void SoundLibrary::clear() {
    QMutexLocker lock(&d->mutex);
    d->sounds.clear();
}

bool SoundLibrary::importFromDirectory(const QString& directory, const QString& category) {
    QDir dir(directory);
    if (!dir.exists()) {
        Q_EMIT error(QString("Directory not found: %1").arg(directory));
        return false;
    }

    QStringList filters;
    filters << "*.wav" << "*.ogg" << "*.mp3" << "*.flac";
    QDirIterator it(directory, filters, QDir::Files, QDirIterator::Subdirectories);

    int count = 0;
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();

        SoundDefinition def;
        def.id = fi.baseName();
        def.path = fi.absoluteFilePath();
        def.category = category.isEmpty() ? fi.dir().dirName() : category;
        def.description = fi.baseName();
        def.volume = 1.0f;
        def.pitch = 1.0f;
        def.looping = false;

        // Check for duplicates
        if (d->sounds.contains(def.id)) {
            def.id = QString("%1_%2").arg(def.id).arg(count);
        }

        addSound(def);
        count++;
    }

    return count > 0;
}

QStringList SoundLibrary::getValidationErrors() const {
    QMutexLocker lock(&d->mutex);
    QStringList errors;

    for (auto it = d->sounds.begin(); it != d->sounds.end(); ++it) {
        const SoundDefinition& def = it.value();

        if (def.id.isEmpty()) {
            errors.append("Sound with empty ID found");
        }
        if (!def.path.isEmpty() && !QFile::exists(def.path)) {
            errors.append(QString("Sound '%1': file not found: %2")
                .arg(def.id, def.path));
        }
        if (def.volume < 0.0f || def.volume > 1.0f) {
            errors.append(QString("Sound '%1': volume out of range [0,1]")
                .arg(def.id));
        }
        if (def.pitch < 0.125f || def.pitch > 4.0f) {
            errors.append(QString("Sound '%1': pitch out of range [0.125,4.0]")
                .arg(def.id));
        }
    }

    return errors;
}

} // namespace powsys365::audio
