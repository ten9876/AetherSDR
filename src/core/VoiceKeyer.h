#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QByteArray>
#include <vector>

namespace AetherSDR {

class AudioEngine;
class TransmitModel;

// Client-side voice keyer: plays a WAV file over the TX audio path by
// feeding decoded PCM into AudioEngine::feedDaxTxAudio().  Automatically
// asserts PTT before playback and releases it when done or stopped.
//
// All WAV files are resampled to 24 kHz mono float32 internally.
class VoiceKeyer : public QObject {
    Q_OBJECT

public:
    explicit VoiceKeyer(QObject* parent = nullptr);

    void setAudioEngine(AudioEngine* engine) { m_audio = engine; }
    void setTransmitModel(TransmitModel* model) { m_txModel = model; }

    bool isPlaying() const { return m_playing; }

    // Start playback of the given WAV file.  Returns false if the file
    // could not be loaded or decoded.
    bool play(const QString& wavPath);

    // Stop playback immediately and release PTT.
    void stop();

signals:
    void playbackStarted();
    void playbackFinished();

private slots:
    void feedNextChunk();

private:
    bool loadWav(const QString& path);

    AudioEngine*   m_audio{nullptr};
    TransmitModel* m_txModel{nullptr};
    QTimer         m_feedTimer;
    bool           m_playing{false};

    // Decoded PCM: 24 kHz mono float32
    std::vector<float> m_samples;
    int                m_playPos{0};

    // Feed 128 samples per tick at 24 kHz → ~5.3 ms per tick
    static constexpr int kSamplesPerChunk = 128;
    // Timer interval in ms — slightly under 5.3 ms to avoid underruns
    static constexpr int kFeedIntervalMs = 5;
};

} // namespace AetherSDR
