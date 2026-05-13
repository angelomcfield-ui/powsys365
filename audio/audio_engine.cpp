#include "audio_engine.h"
#include <QLibrary>
#include <QFile>
#include <QDebug>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QTimer>
#include <QThread>
#include <cmath>

// OpenAL headers
#ifdef _WIN32
#include <al.h>
#include <alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

namespace powsys365::audio {

// WAV file header structure
struct WavHeader {
    char riff[4];
    quint32 fileSize;
    char wave[4];
    char fmt[4];
    quint32 fmtSize;
    quint16 audioFormat;
    quint16 channels;
    quint32 sampleRate;
    quint32 byteRate;
    quint16 blockAlign;
    quint16 bitsPerSample;
};

class AudioEngine::Impl {
public:
    AudioEngine* q;
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    bool initialized = false;
    QMap<QString, ALuint> soundBuffers; // id -> OpenAL buffer
    QMap<ALuint, PlayingSound> activeSources; // sourceId -> playing sound info
    QMap<QString, QList<ALuint>> soundToSources; // sound id -> list of source IDs
    float masterVol = 1.0f;
    int maxVoiceCount = 64;
    int totalPlayed = 0;
    mutable QMutex mutex;
    QString lastError;
    QTimer* updateTimer = nullptr;

    explicit Impl(AudioEngine* parent) : q(parent) {}

    bool checkAlError(const QString& operation) {
        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            lastError = QString("OpenAL error in %1: %2").arg(operation).arg(err);
            qWarning() << lastError;
            return false;
        }
        return true;
    }

    bool loadWavFile(const QString& filePath, ALuint* outBuffer) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            lastError = QString("Cannot open WAV file: %1").arg(filePath);
            return false;
        }

        QByteArray data = file.readAll();
        file.close();

        if (data.size() < 44) {
            lastError = "WAV file too small";
            return false;
        }

        const WavHeader* header = reinterpret_cast<const WavHeader*>(data.constData());

        if (strncmp(header->riff, "RIFF", 4) != 0 ||
            strncmp(header->wave, "WAVE", 4) != 0 ||
            strncmp(header->fmt, "fmt ", 4) != 0) {
            lastError = "Invalid WAV file header";
            return false;
        }

        // Find data chunk
        int offset = 12 + 8 + header->fmtSize;
        quint32 dataSize = 0;
        const char* dataPtr = nullptr;

        while (offset < data.size() - 8) {
            const char* chunkId = data.constData() + offset;
            quint32 chunkSize = *reinterpret_cast<const quint32*>(data.constData() + offset + 4);
            if (strncmp(chunkId, "data", 4) == 0) {
                dataSize = chunkSize;
                dataPtr = data.constData() + offset + 8;
                break;
            }
            offset += 8 + chunkSize;
            if (chunkSize % 2 == 1) offset++; // padding
        }

        if (!dataPtr) {
            lastError = "No data chunk found in WAV file";
            return false;
        }

        // Determine OpenAL format
        ALenum format;
        if (header->channels == 1) {
            format = (header->bitsPerSample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
        } else if (header->channels == 2) {
            format = (header->bitsPerSample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
        } else {
            lastError = QString("Unsupported channel count: %1").arg(header->channels);
            return false;
        }

        ALuint buffer;
        alGenBuffers(1, &buffer);
        if (!checkAlError("gen buffer")) return false;

        alBufferData(buffer, format, dataPtr, dataSize, header->sampleRate);
        if (!checkAlError("buffer data")) {
            alDeleteBuffers(1, &buffer);
            return false;
        }

        *outBuffer = buffer;
        return true;
    }

    ALuint createSource(ALuint buffer, const SoundProperties& props) {
        ALuint source;
        alGenSources(1, &source);
        if (alGetError() != AL_NO_ERROR) return 0;

        alSourcei(source, AL_BUFFER, buffer);
        alSourcef(source, AL_GAIN, props.volume * masterVol);
        alSourcef(source, AL_PITCH, props.pitch);
        alSourcei(source, AL_LOOPING, props.looping ? AL_TRUE : AL_FALSE);
        alSource3f(source, AL_POSITION, props.position.x(), props.position.y(), props.position.z());
        alSource3f(source, AL_VELOCITY, props.velocity.x(), props.velocity.y(), props.velocity.z());
        alSourcef(source, AL_REFERENCE_DISTANCE, props.referenceDistance);
        alSourcef(source, AL_MAX_DISTANCE, props.maxDistance);
        alSourcef(source, AL_ROLLOFF_FACTOR, props.rolloffFactor);

        // Apply pan by adjusting source position
        if (props.pan != 0.0f) {
            alSource3f(source, AL_POSITION, props.pan, 0.0f, 0.0f);
            alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
        }

        if (alGetError() != AL_NO_ERROR) {
            alDeleteSources(1, &source);
            return 0;
        }

        return source;
    }

    void cleanupFinishedSources() {
        QMutexLocker lock(&mutex);
        QList<ALuint> toRemove;
        for (auto it = activeSources.begin(); it != activeSources.end(); ++it) {
            ALuint source = it.key();
            ALint state;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                QString soundId = it.value().id;
                toRemove.append(source);
                alDeleteSources(1, &source);
                Q_EMIT q->soundFinished(soundId);

                // Remove from sound-to-sources mapping
                auto sit = soundToSources.find(soundId);
                if (sit != soundToSources.end()) {
                    sit.value().removeAll(source);
                    if (sit.value().isEmpty()) {
                        soundToSources.erase(sit);
                    }
                }
            }
        }
        for (ALuint src : toRemove) {
            activeSources.remove(src);
        }
    }

    bool generateTone(const QString& id, float frequency, float duration, int sampleRate = 44100) {
        int numSamples = static_cast<int>(duration * sampleRate);
        QByteArray data;
        data.resize(numSamples * 2); // 16-bit mono

        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            // Generate a pleasant tone with harmonics
            float sample = std::sin(2.0f * M_PI * frequency * t) * 0.5f;
            sample += std::sin(2.0f * M_PI * frequency * 2.0f * t) * 0.25f * std::exp(-t * 3.0f);
            sample += std::sin(2.0f * M_PI * frequency * 0.5f * t) * 0.15f * std::exp(-t * 1.5f);

            // Apply envelope
            float envelope = 1.0f;
            if (i < sampleRate * 0.05f) {
                envelope = i / (sampleRate * 0.05f);
            } else if (i > numSamples - sampleRate * 0.1f) {
                envelope = (numSamples - i) / (sampleRate * 0.1f);
            }
            sample *= envelope;

            // Clamp and convert to 16-bit
            sample = std::max(-1.0f, std::min(1.0f, sample));
            qint16 value = static_cast<qint16>(sample * 32767.0f);
            *reinterpret_cast<qint16*>(data.data() + i * 2) = value;
        }

        ALuint buffer;
        alGenBuffers(1, &buffer);
        if (!checkAlError("gen tone buffer")) return false;

        alBufferData(buffer, AL_FORMAT_MONO16, data.constData(), data.size(), sampleRate);
        if (!checkAlError("tone buffer data")) {
            alDeleteBuffers(1, &buffer);
            return false;
        }

        soundBuffers[id] = buffer;
        return true;
    }

    bool generateClick(float frequency, int sampleRate = 44100) {
        int numSamples = static_cast<int>(0.05f * sampleRate);
        QByteArray data;
        data.resize(numSamples * 2);

        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float sample = std::sin(2.0f * M_PI * frequency * t) * std::exp(-t * 80.0f);
            sample = std::max(-1.0f, std::min(1.0f, sample));
            qint16 value = static_cast<qint16>(sample * 32767.0f);
            *reinterpret_cast<qint16*>(data.data() + i * 2) = value;
        }

        ALuint buffer;
        alGenBuffers(1, &buffer);
        if (!checkAlError("gen click buffer")) return false;

        alBufferData(buffer, AL_FORMAT_MONO16, data.constData(), data.size(), sampleRate);
        if (!checkAlError("click buffer data")) {
            alDeleteBuffers(1, &buffer);
            return false;
        }

        soundBuffers["click"] = buffer;
        return true;
    }

    bool generateHover(float frequency, int sampleRate = 44100) {
        int numSamples = static_cast<int>(0.08f * sampleRate);
        QByteArray data;
        data.resize(numSamples * 2);

        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float sample = std::sin(2.0f * M_PI * frequency * t) * std::exp(-t * 30.0f);
            sample *= 0.3f;
            sample = std::max(-1.0f, std::min(1.0f, sample));
            qint16 value = static_cast<qint16>(sample * 32767.0f);
            *reinterpret_cast<qint16*>(data.data() + i * 2) = value;
        }

        ALuint buffer;
        alGenBuffers(1, &buffer);
        if (!checkAlError("gen hover buffer")) return false;

        alBufferData(buffer, AL_FORMAT_MONO16, data.constData(), data.size(), sampleRate);
        if (!checkAlError("hover buffer data")) {
            alDeleteBuffers(1, &buffer);
            return false;
        }

        soundBuffers["hover"] = buffer;
        return true;
    }

    bool generateAlarm(float freq1, float freq2, float duration, int sampleRate = 44100) {
        int numSamples = static_cast<int>(duration * sampleRate);
        QByteArray data;
        data.resize(numSamples * 2);

        float cycleLen = 0.15f; // 150ms alternation
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float cyclePos = std::fmod(t, cycleLen) / cycleLen;
            float freq = (cyclePos < 0.5f) ? freq1 : freq2;
            float sample = std::sin(2.0f * M_PI * freq * t) * 0.7f;
            sample = std::max(-1.0f, std::min(1.0f, sample));
            qint16 value = static_cast<qint16>(sample * 32767.0f);
            *reinterpret_cast<qint16*>(data.data() + i * 2) = value;
        }

        ALuint buffer;
        alGenBuffers(1, &buffer);
        if (!checkAlError("gen alarm buffer")) return false;

        alBufferData(buffer, AL_FORMAT_MONO16, data.constData(), data.size(), sampleRate);
        if (!checkAlError("alarm buffer data")) {
            alDeleteBuffers(1, &buffer);
            return false;
        }

        soundBuffers["alarm"] = buffer;
        return true;
    }
};

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this))
{
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    QMutexLocker lock(&d->mutex);

    if (d->initialized) return true;

    d->device = alcOpenDevice(nullptr);
    if (!d->device) {
        d->lastError = "Failed to open OpenAL device";
        Q_EMIT error(d->lastError);
        return false;
    }

    d->context = alcCreateContext(d->device, nullptr);
    if (!d->context) {
        d->lastError = "Failed to create OpenAL context";
        alcCloseDevice(d->device);
        d->device = nullptr;
        Q_EMIT error(d->lastError);
        return false;
    }

    alcMakeContextCurrent(d->context);

    // Set default listener
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat orientation[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, orientation);

    d->checkAlError("initialize");
    d->initialized = true;

    // Load system sounds
    loadSystemSounds();

    // Start cleanup timer
    d->updateTimer = new QTimer(this);
    d->updateTimer->setInterval(100);
    connect(d->updateTimer, &QTimer::timeout, this, [this]() {
        d->cleanupFinishedSources();
    });
    d->updateTimer->start();

    return true;
}

void AudioEngine::shutdown() {
    QMutexLocker lock(&d->mutex);

    if (!d->initialized) return;

    if (d->updateTimer) {
        d->updateTimer->stop();
        delete d->updateTimer;
        d->updateTimer = nullptr;
    }

    // Stop and delete all sources
    for (auto it = d->activeSources.begin(); it != d->activeSources.end(); ++it) {
        ALuint source = it.key();
        alSourceStop(source);
        alDeleteSources(1, &source);
    }
    d->activeSources.clear();
    d->soundToSources.clear();

    // Delete all buffers
    for (auto it = d->soundBuffers.begin(); it != d->soundBuffers.end(); ++it) {
        alDeleteBuffers(1, &it.value());
    }
    d->soundBuffers.clear();

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(d->context);
    d->context = nullptr;
    alcCloseDevice(d->device);
    d->device = nullptr;
    d->initialized = false;
}

bool AudioEngine::isInitialized() const {
    return d->initialized;
}

bool AudioEngine::loadSound(const QString& id, const QString& filePath) {
    QMutexLocker lock(&d->mutex);
    if (!d->initialized) {
        d->lastError = "Engine not initialized";
        return false;
    }

    // Unload existing sound with same ID
    if (d->soundBuffers.contains(id)) {
        unloadSound(id);
    }

    ALuint buffer;
    bool ok = false;

    if (isWavFile(filePath)) {
        ok = d->loadWavFile(filePath, &buffer);
    } else if (isOggFile(filePath)) {
        // OGG support would require libvorbis
        d->lastError = "OGG support requires libvorbis";
        Q_EMIT error(d->lastError);
        return false;
    } else if (isMp3File(filePath)) {
        // MP3 support would require libmpg123 or similar
        d->lastError = "MP3 support requires libmpg123";
        Q_EMIT error(d->lastError);
        return false;
    } else {
        // Try WAV as default
        ok = d->loadWavFile(filePath, &buffer);
    }

    if (!ok) return false;

    d->soundBuffers[id] = buffer;
    Q_EMIT soundLoaded(id);
    return true;
}

bool AudioEngine::loadSoundFromMemory(const QString& id, const QByteArray& data,
                                       int sampleRate, int channels, int bitsPerSample) {
    QMutexLocker lock(&d->mutex);
    if (!d->initialized) {
        d->lastError = "Engine not initialized";
        return false;
    }

    // Unload existing sound with same ID
    if (d->soundBuffers.contains(id)) {
        unloadSound(id);
    }

    ALenum format;
    if (channels == 1) {
        format = (bitsPerSample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    } else if (channels == 2) {
        format = (bitsPerSample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    } else {
        d->lastError = QString("Unsupported channel count: %1").arg(channels);
        return false;
    }

    ALuint buffer;
    alGenBuffers(1, &buffer);
    if (!d->checkAlError("gen buffer")) return false;

    alBufferData(buffer, format, data.constData(), data.size(), sampleRate);
    if (!d->checkAlError("buffer data")) {
        alDeleteBuffers(1, &buffer);
        return false;
    }

    d->soundBuffers[id] = buffer;
    Q_EMIT soundLoaded(id);
    return true;
}

bool AudioEngine::unloadSound(const QString& id) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundBuffers.find(id);
    if (it == d->soundBuffers.end()) return false;

    // Stop all playing instances of this sound
    auto sit = d->soundToSources.find(id);
    if (sit != d->soundToSources.end()) {
        for (ALuint source : sit.value()) {
            alSourceStop(source);
            alDeleteSources(1, &source);
            d->activeSources.remove(source);
        }
        d->soundToSources.erase(sit);
    }

    alDeleteBuffers(1, &it.value());
    d->soundBuffers.erase(it);
    Q_EMIT soundUnloaded(id);
    return true;
}

bool AudioEngine::isSoundLoaded(const QString& id) const {
    QMutexLocker lock(&d->mutex);
    return d->soundBuffers.contains(id);
}

QStringList AudioEngine::loadedSounds() const {
    QMutexLocker lock(&d->mutex);
    return d->soundBuffers.keys();
}

bool AudioEngine::playSound(const QString& id) {
    SoundProperties props;
    return playSound(id, props);
}

bool AudioEngine::playSound(const QString& id, const SoundProperties& props) {
    QMutexLocker lock(&d->mutex);
    if (!d->initialized) {
        d->lastError = "Engine not initialized";
        return false;
    }

    auto it = d->soundBuffers.find(id);
    if (it == d->soundBuffers.end()) {
        d->lastError = QString("Sound not loaded: %1").arg(id);
        return false;
    }

    // Check voice limit
    d->cleanupFinishedSources();
    if (d->activeSources.size() >= static_cast<size_t>(d->maxVoiceCount)) {
        d->lastError = "Maximum voice count reached";
        return false;
    }

    ALuint buffer = it.value();
    ALuint source = d->createSource(buffer, props);
    if (source == 0) {
        d->lastError = "Failed to create OpenAL source";
        return false;
    }

    alSourcePlay(source);
    if (!d->checkAlError("play source")) {
        alDeleteSources(1, &source);
        return false;
    }

    PlayingSound ps;
    ps.id = id;
    ps.sourceId = source;
    ps.bufferId = buffer;
    ps.state = PlayState::Playing;
    ps.props = props;
    ps.startTimeMs = QDateTime::currentMSecsSinceEpoch();

    d->activeSources[source] = ps;
    d->soundToSources[id].append(source);
    d->totalPlayed++;

    Q_EMIT soundStarted(id);
    return true;
}

bool AudioEngine::stopSound(const QString& id) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        alSourceStop(source);
        alDeleteSources(1, &source);
        d->activeSources.remove(source);
    }
    d->soundToSources.erase(it);

    Q_EMIT soundStopped(id);
    return true;
}

bool AudioEngine::stopSoundInstance(ALuint sourceId) {
    QMutexLocker lock(&d->mutex);

    auto it = d->activeSources.find(sourceId);
    if (it == d->activeSources.end()) return false;

    QString id = it.value().id;
    alSourceStop(sourceId);
    alDeleteSources(1, &sourceId);
    d->activeSources.erase(it);

    auto sit = d->soundToSources.find(id);
    if (sit != d->soundToSources.end()) {
        sit.value().removeAll(sourceId);
        if (sit.value().isEmpty()) {
            d->soundToSources.erase(sit);
        }
    }

    Q_EMIT soundStopped(id);
    return true;
}

bool AudioEngine::pauseSound(const QString& id) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        alSourcePause(source);
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().state = PlayState::Paused;
            ait.value().pauseTimeMs = QDateTime::currentMSecsSinceEpoch();
        }
    }

    Q_EMIT soundPaused(id);
    return true;
}

bool AudioEngine::resumeSound(const QString& id) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        alSourcePlay(source);
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().state = PlayState::Playing;
        }
    }

    Q_EMIT soundResumed(id);
    return true;
}

void AudioEngine::stopAllSounds() {
    QMutexLocker lock(&d->mutex);

    for (auto it = d->activeSources.begin(); it != d->activeSources.end(); ++it) {
        ALuint source = it.key();
        alSourceStop(source);
        alDeleteSources(1, &source);
        Q_EMIT soundStopped(it.value().id);
    }
    d->activeSources.clear();
    d->soundToSources.clear();
}

void AudioEngine::pauseAllSounds() {
    QMutexLocker lock(&d->mutex);

    for (auto it = d->activeSources.begin(); it != d->activeSources.end(); ++it) {
        alSourcePause(it.key());
        it.value().state = PlayState::Paused;
    }
}

void AudioEngine::resumeAllSounds() {
    QMutexLocker lock(&d->mutex);

    for (auto it = d->activeSources.begin(); it != d->activeSources.end(); ++it) {
        alSourcePlay(it.key());
        it.value().state = PlayState::Playing;
    }
}

bool AudioEngine::setVolume(const QString& id, float volume) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    volume = std::max(0.0f, std::min(1.0f, volume));
    for (ALuint source : it.value()) {
        alSourcef(source, AL_GAIN, volume * d->masterVol);
    }

    // Update stored properties
    for (ALuint source : it.value()) {
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().props.volume = volume;
        }
    }

    return true;
}

bool AudioEngine::setPitch(const QString& id, float pitch) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    pitch = std::max(0.125f, std::min(4.0f, pitch));
    for (ALuint source : it.value()) {
        alSourcef(source, AL_PITCH, pitch);
    }

    for (ALuint source : it.value()) {
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().props.pitch = pitch;
        }
    }

    return true;
}

bool AudioEngine::setLooping(const QString& id, bool looping) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        alSourcei(source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
    }

    for (ALuint source : it.value()) {
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().props.looping = looping;
        }
    }

    return true;
}

bool AudioEngine::setPan(const QString& id, float pan) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    pan = std::max(-1.0f, std::min(1.0f, pan));
    for (ALuint source : it.value()) {
        alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(source, AL_POSITION, pan, 0.0f, 0.0f);
    }

    for (ALuint source : it.value()) {
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().props.pan = pan;
        }
    }

    return true;
}

bool AudioEngine::setPosition(const QString& id, const QVector3D& position) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        alSource3f(source, AL_POSITION, position.x(), position.y(), position.z());
    }

    for (ALuint source : it.value()) {
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().props.position = position;
        }
    }

    return true;
}

bool AudioEngine::setVelocity(const QString& id, const QVector3D& velocity) {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        alSource3f(source, AL_VELOCITY, velocity.x(), velocity.y(), velocity.z());
    }

    for (ALuint source : it.value()) {
        auto ait = d->activeSources.find(source);
        if (ait != d->activeSources.end()) {
            ait.value().props.velocity = velocity;
        }
    }

    return true;
}

float AudioEngine::getVolume(const QString& id) const {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return 0.0f;

    for (ALuint source : it.value()) {
        float vol;
        alGetSourcef(source, AL_GAIN, &vol);
        return vol / d->masterVol;
    }
    return 0.0f;
}

float AudioEngine::getPitch(const QString& id) const {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return 1.0f;

    for (ALuint source : it.value()) {
        float pitch;
        alGetSourcef(source, AL_PITCH, &pitch);
        return pitch;
    }
    return 1.0f;
}

bool AudioEngine::isLooping(const QString& id) const {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return false;

    for (ALuint source : it.value()) {
        ALint looping;
        alGetSourcei(source, AL_LOOPING, &looping);
        return looping == AL_TRUE;
    }
    return false;
}

PlayState AudioEngine::getState(const QString& id) const {
    QMutexLocker lock(&d->mutex);

    auto it = d->soundToSources.find(id);
    if (it == d->soundToSources.end()) return PlayState::Stopped;

    for (ALuint source : it.value()) {
        ALint state;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state == AL_PLAYING) return PlayState::Playing;
        if (state == AL_PAUSED) return PlayState::Paused;
    }
    return PlayState::Stopped;
}

void AudioEngine::setMasterVolume(float volume) {
    QMutexLocker lock(&d->mutex);
    d->masterVol = std::max(0.0f, std::min(1.0f, volume));

    // Update all active sources
    for (auto it = d->activeSources.begin(); it != d->activeSources.end(); ++it) {
        alSourcef(it.key(), AL_GAIN, it.value().props.volume * d->masterVol);
    }
}

float AudioEngine::masterVolume() const {
    return d->masterVol;
}

void AudioEngine::setListenerPosition(const QVector3D& position) {
    alListener3f(AL_POSITION, position.x(), position.y(), position.z());
}

void AudioEngine::setListenerOrientation(const QVector3D& forward, const QVector3D& up) {
    ALfloat orientation[] = { forward.x(), forward.y(), forward.z(),
                              up.x(), up.y(), up.z() };
    alListenerfv(AL_ORIENTATION, orientation);
}

void AudioEngine::setListenerVelocity(const QVector3D& velocity) {
    alListener3f(AL_VELOCITY, velocity.x(), velocity.y(), velocity.z());
}

void AudioEngine::setDopplerFactor(float factor) {
    alDopplerFactor(factor);
}

void AudioEngine::setSpeedOfSound(float speed) {
    alSpeedOfSound(speed);
}

void AudioEngine::setDistanceModel(const QString& model) {
    ALenum alModel = AL_INVERSE_DISTANCE_CLAMPED;
    if (model == "inverse") alModel = AL_INVERSE_DISTANCE;
    else if (model == "inverse_clamped") alModel = AL_INVERSE_DISTANCE_CLAMPED;
    else if (model == "linear") alModel = AL_LINEAR_DISTANCE;
    else if (model == "linear_clamped") alModel = AL_LINEAR_DISTANCE_CLAMPED;
    else if (model == "exponent") alModel = AL_EXPONENT_DISTANCE;
    else if (model == "exponent_clamped") alModel = AL_EXPONENT_DISTANCE_CLAMPED;
    else if (model == "none") alModel = AL_NONE;
    alDistanceModel(alModel);
}

bool AudioEngine::loadSystemSounds() {
    QMutexLocker lock(&d->mutex);
    if (!d->initialized) return false;

    bool allOk = true;

    // 1. success - ascending happy chime (C-E-G-C)
    allOk &= d->generateTone("success", 523.25f, 0.3f);    // C5

    // 2. error - low descending tone
    allOk &= d->generateTone("error", 220.0f, 0.4f);       // A3

    // 3. warning - two-tone alert
    allOk &= d->generateTone("warning", 440.0f, 0.3f);     // A4

    // 4. click - short click sound
    allOk &= d->generateClick(2000.0f);

    // 5. hover - soft hover
    allOk &= d->generateHover(1500.0f);

    // 6. alarm - alternating high frequency
    allOk &= d->generateAlarm(880.0f, 1100.0f, 2.0f);

    // 7. notification - gentle ping
    allOk &= d->generateTone("notification", 659.25f, 0.25f); // E5

    // 8. info - soft info sound
    allOk &= d->generateTone("info", 783.99f, 0.2f);       // G5

    // 9. startup - ascending scale
    allOk &= d->generateTone("startup", 349.23f, 0.5f);    // F4

    // 10. shutdown - descending note
    allOk &= d->generateTone("shutdown", 293.66f, 0.5f);   // D4

    // 11. save - short confirm
    allOk &= d->generateTone("save", 587.33f, 0.15f);      // D5

    // 12. open - brief tone
    allOk &= d->generateTone("open", 440.0f, 0.15f);       // A4

    // 13. close - soft close
    allOk &= d->generateTone("close", 329.63f, 0.15f);     // E4

    // 14. compile_ok - success chime
    allOk &= d->generateTone("compile_ok", 698.46f, 0.3f); // F5

    // 15. compile_err - error buzz
    allOk &= d->generateTone("compile_err", 164.81f, 0.5f); // E3

    // 16. breakpoint - distinctive ping
    allOk &= d->generateTone("breakpoint", 1046.5f, 0.2f); // C6

    // 17. step - quiet tick
    allOk &= d->generateTone("step", 2000.0f, 0.05f);

    // 18. search - sweep
    allOk &= d->generateTone("search", 880.0f, 0.15f);     // A5

    // 19. connect - connection established
    allOk &= d->generateTone("connect", 523.25f, 0.3f);    // C5

    // 20. disconnect - disconnection
    allOk &= d->generateTone("disconnect", 311.13f, 0.3f); // Eb4

    // 21. commit - git commit sound
    allOk &= d->generateTone("commit", 392.0f, 0.2f);      // G4

    // 22. sync - sync complete
    allOk &= d->generateTone("sync", 659.25f, 0.2f);       // E5

    // 23. bell - terminal bell
    allOk &= d->generateTone("bell", 440.0f, 0.1f);        // A4

    return allOk;
}

bool AudioEngine::playSystemSound(const QString& systemSoundId) {
    if (!isSoundLoaded(systemSoundId)) {
        d->lastError = QString("System sound not loaded: %1").arg(systemSoundId);
        return false;
    }

    SoundProperties props;
    // Customize properties for certain sounds
    if (systemSoundId == "alarm") {
        props.looping = false;
        props.volume = 0.8f;
    } else if (systemSoundId == "error" || systemSoundId == "compile_err") {
        props.volume = 0.6f;
    } else if (systemSoundId == "click" || systemSoundId == "hover" || systemSoundId == "step") {
        props.volume = 0.3f;
    } else if (systemSoundId == "bell") {
        props.volume = 0.4f;
    } else {
        props.volume = 0.5f;
    }

    return playSound(systemSoundId, props);
}

int AudioEngine::maxVoices() const {
    return d->maxVoiceCount;
}

void AudioEngine::setMaxVoices(int max) {
    d->maxVoiceCount = std::max(1, max);
}

int AudioEngine::activeVoiceCount() const {
    QMutexLocker lock(&d->mutex);
    return d->activeSources.size();
}

int AudioEngine::totalSoundsPlayed() const {
    return d->totalPlayed;
}

bool AudioEngine::isWavFile(const QString& filePath) {
    return filePath.endsWith(".wav", Qt::CaseInsensitive);
}

bool AudioEngine::isOggFile(const QString& filePath) {
    return filePath.endsWith(".ogg", Qt::CaseInsensitive);
}

bool AudioEngine::isMp3File(const QString& filePath) {
    return filePath.endsWith(".mp3", Qt::CaseInsensitive);
}

QString AudioEngine::getLastError() const {
    return d->lastError;
}

} // namespace powsys365::audio
