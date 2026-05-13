#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector3D>
#include <QMutex>
#include <memory>

// Forward declarations for OpenAL
struct ALCdevice;
struct ALCcontext;

typedef unsigned int ALuint;
typedef int ALint;
typedef int ALsizei;
typedef float ALfloat;

namespace powsys365::audio {

/**
 * @brief Sound instance playing state
 */
enum class PlayState {
    Stopped,
    Playing,
    Paused,
    FadingIn,
    FadingOut
};

/**
 * @brief Sound playback properties
 */
struct SoundProperties {
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    bool looping = false;
    float fadeInDuration = 0.0f;
    float fadeOutDuration = 0.0f;
    QVector3D position;
    QVector3D velocity;
    float referenceDistance = 1.0f;
    float maxDistance = 100.0f;
    float rolloffFactor = 1.0f;
};

/**
 * @brief Playing sound instance
 */
struct PlayingSound {
    QString id;
    ALuint sourceId = 0;
    ALuint bufferId = 0;
    PlayState state = PlayState::Stopped;
    SoundProperties props;
    qint64 startTimeMs = 0;
    qint64 pauseTimeMs = 0;
};

/**
 * @brief OpenAL-based audio engine for POWSYS365
 *
 * Provides complete audio playback with:
 * - 3D positional audio
 * - Volume/pitch/pan control
 * - Looping support
 * - Fade in/out
 * - Simultaneous voice management
 * - 23 built-in system sounds
 */
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine();

    // === Engine Lifecycle ===
    bool initialize();
    void shutdown();
    bool isInitialized() const;

    // === Sound Loading ===
    bool loadSound(const QString& id, const QString& filePath);
    bool loadSoundFromMemory(const QString& id, const QByteArray& data, int sampleRate = 44100,
                              int channels = 1, int bitsPerSample = 16);
    bool unloadSound(const QString& id);
    bool isSoundLoaded(const QString& id) const;
    QStringList loadedSounds() const;

    // === Playback ===
    bool playSound(const QString& id);
    bool playSound(const QString& id, const SoundProperties& props);
    bool stopSound(const QString& id);
    bool stopSoundInstance(ALuint sourceId);
    bool pauseSound(const QString& id);
    bool resumeSound(const QString& id);
    void stopAllSounds();
    void pauseAllSounds();
    void resumeAllSounds();

    // === Sound Properties ===
    bool setVolume(const QString& id, float volume);
    bool setPitch(const QString& id, float pitch);
    bool setLooping(const QString& id, bool looping);
    bool setPan(const QString& id, float pan);
    bool setPosition(const QString& id, const QVector3D& position);
    bool setVelocity(const QString& id, const QVector3D& velocity);
    float getVolume(const QString& id) const;
    float getPitch(const QString& id) const;
    bool isLooping(const QString& id) const;
    PlayState getState(const QString& id) const;

    // === Global Settings ===
    void setMasterVolume(float volume);
    float masterVolume() const;
    void setListenerPosition(const QVector3D& position);
    void setListenerOrientation(const QVector3D& forward, const QVector3D& up);
    void setListenerVelocity(const QVector3D& velocity);
    void setDopplerFactor(float factor);
    void setSpeedOfSound(float speed);
    void setDistanceModel(const QString& model); // "inverse", "inverse_clamped", "linear", "linear_clamped", "exponent", "exponent_clamped", "none"

    // === 23 Built-in System Sounds ===
    bool loadSystemSounds();
    bool playSystemSound(const QString& systemSoundId);

    // System sound IDs:
    // "success"     - Operation completed successfully
    // "error"       - Error occurred
    // "warning"     - Warning notification
    // "click"       - UI button click
    // "hover"       - UI hover
    // "alarm"       - Critical alarm
    // "notification" - General notification
    // "info"        - Information notification
    // "startup"     - Application startup
    // "shutdown"    - Application shutdown
    // "save"        - File saved
    // "open"        - File opened
    // "close"       - File/tab closed
    // "compile_ok"  - Compilation successful
    // "compile_err" - Compilation failed
    // "breakpoint"  - Breakpoint hit
    // "step"        - Debugger step
    // "search"      - Search completed
    // "connect"     - Connection established
    // "disconnect"  - Connection lost
    // "commit"      - Git commit done
    // "sync"        - Synchronization done
    // "bell"        - Terminal bell

    // === Voice Management ===
    int maxVoices() const;
    void setMaxVoices(int max);
    int activeVoiceCount() const;
    int totalSoundsPlayed() const;

    // === Utility ===
    static bool isWavFile(const QString& filePath);
    static bool isOggFile(const QString& filePath);
    static bool isMp3File(const QString& filePath);
    QString getLastError() const;

Q_SIGNALS:
    void soundLoaded(const QString& id);
    void soundUnloaded(const QString& id);
    void soundStarted(const QString& id);
    void soundStopped(const QString& id);
    void soundPaused(const QString& id);
    void soundResumed(const QString& id);
    void soundFinished(const QString& id);
    void error(const QString& message);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace powsys365::audio
