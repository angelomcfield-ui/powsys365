#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <memory>

namespace powsys365::audio {

/**
 * @brief Sound definition with metadata
 */
struct SoundDefinition {
    QString id;
    QString path;
    QString category;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool looping = false;
    QString description;
    QString tags;
    bool isSystemSound = false;
    bool isPreload = false;
    int priority = 0; // Higher = more important

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["path"] = path;
        obj["category"] = category;
        obj["volume"] = volume;
        obj["pitch"] = pitch;
        obj["looping"] = looping;
        obj["description"] = description;
        obj["tags"] = tags;
        obj["isSystemSound"] = isSystemSound;
        obj["isPreload"] = isPreload;
        obj["priority"] = priority;
        return obj;
    }

    static SoundDefinition fromJson(const QJsonObject& obj) {
        SoundDefinition def;
        def.id = obj.value("id").toString();
        def.path = obj.value("path").toString();
        def.category = obj.value("category").toString();
        def.volume = obj.value("volume").toDouble(1.0);
        def.pitch = obj.value("pitch").toDouble(1.0);
        def.looping = obj.value("looping").toBool(false);
        def.description = obj.value("description").toString();
        def.tags = obj.value("tags").toString();
        def.isSystemSound = obj.value("isSystemSound").toBool(false);
        def.isPreload = obj.value("isPreload").toBool(false);
        def.priority = obj.value("priority").toInt(0);
        return def;
    }
};

/**
 * @brief Sound library manager - catalogs and organizes sounds
 *
 * Provides a centralized registry for all sounds in the application,
 * organized by category with metadata for each sound.
 */
class SoundLibrary : public QObject {
    Q_OBJECT

public:
    explicit SoundLibrary(QObject* parent = nullptr);
    ~SoundLibrary();

    // === Library Management ===
    bool loadLibrary(const QString& jsonFilePath);
    bool loadLibraryFromJson(const QJsonObject& libraryJson);
    bool loadLibraryFromData(const QByteArray& data);
    bool saveLibrary(const QString& jsonFilePath) const;
    QJsonObject toJson() const;

    // === Default Library ===
    bool loadDefaultLibrary();
    void buildDefaultLibrary();

    // === Sound Registration ===
    bool addSound(const SoundDefinition& definition);
    bool removeSound(const QString& id);
    bool updateSound(const QString& id, const SoundDefinition& definition);
    bool hasSound(const QString& id) const;
    SoundDefinition getSound(const QString& id) const;

    // === Queries ===
    QStringList listSounds() const;
    QStringList listSoundsByCategory(const QString& category) const;
    QStringList listCategories() const;
    QList<SoundDefinition> getSoundsByCategory(const QString& category) const;
    QList<SoundDefinition> searchSounds(const QString& query) const;
    QList<SoundDefinition> getSystemSounds() const;
    QList<SoundDefinition> getPreloadSounds() const;

    // === Library Info ===
    int soundCount() const;
    int categoryCount() const;
    void clear();

    // === Export/Import ===
    bool importFromDirectory(const QString& directory, const QString& category = QString());
    QStringList getValidationErrors() const;

Q_SIGNALS:
    void libraryLoaded();
    soundAdded(const QString& id);
    soundRemoved(const QString& id);
    soundUpdated(const QString& id);
    error(const QString& message);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::audio
