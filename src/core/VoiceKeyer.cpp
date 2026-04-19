#include "VoiceKeyer.h"
#include "AudioEngine.h"
#include "models/TransmitModel.h"

#include <QFile>
#include <QDataStream>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcVoiceKeyer, "aethersdr.voicekeyer")

namespace AetherSDR {

VoiceKeyer::VoiceKeyer(QObject* parent)
    : QObject(parent)
{
    m_feedTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_feedTimer, &QTimer::timeout, this, &VoiceKeyer::feedNextChunk);
}

bool VoiceKeyer::play(const QString& wavPath)
{
    if (m_playing) {
        stop();
    }

    if (!m_audio || !m_txModel) {
        qCWarning(lcVoiceKeyer) << "VoiceKeyer not wired to AudioEngine/TransmitModel";
        return false;
    }

    if (!loadWav(wavPath)) {
        qCWarning(lcVoiceKeyer) << "Failed to load WAV:" << wavPath;
        return false;
    }

    m_playPos = 0;
    m_playing = true;

    // Assert PTT
    m_txModel->setMox(true);

    // Small delay before first audio to let PTT settle
    QTimer::singleShot(50, this, [this]() {
        if (m_playing) {
            m_feedTimer.start(kFeedIntervalMs);
            emit playbackStarted();
        }
    });

    return true;
}

void VoiceKeyer::stop()
{
    if (!m_playing) return;

    m_feedTimer.stop();
    m_playing = false;
    m_samples.clear();
    m_playPos = 0;

    // Release PTT
    if (m_txModel) {
        m_txModel->setMox(false);
    }

    emit playbackFinished();
}

void VoiceKeyer::feedNextChunk()
{
    if (!m_playing || !m_audio) {
        stop();
        return;
    }

    int remaining = static_cast<int>(m_samples.size()) - m_playPos;
    if (remaining <= 0) {
        stop();
        return;
    }

    int count = qMin(remaining, kSamplesPerChunk);

    // Build stereo float32 interleaved (L=R=mono sample)
    QByteArray stereo(count * 2 * static_cast<int>(sizeof(float)), Qt::Uninitialized);
    float* out = reinterpret_cast<float*>(stereo.data());
    const float* src = m_samples.data() + m_playPos;

    for (int i = 0; i < count; ++i) {
        out[i * 2]     = src[i];  // L
        out[i * 2 + 1] = src[i];  // R
    }

    QMetaObject::invokeMethod(m_audio, "feedDaxTxAudio",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, stereo));

    m_playPos += count;
}

bool VoiceKeyer::loadWav(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 44) {
        qCWarning(lcVoiceKeyer) << "File too small for WAV header";
        return false;
    }

    // Parse RIFF/WAVE header
    const char* d = data.constData();
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        qCWarning(lcVoiceKeyer) << "Not a valid RIFF/WAVE file";
        return false;
    }

    // Find "fmt " chunk
    int pos = 12;
    int fmtOffset = -1;
    int dataOffset = -1;
    int dataSize = 0;
    int fmtSize = 0;

    while (pos + 8 <= data.size()) {
        quint32 chunkSize = 0;
        memcpy(&chunkSize, d + pos + 4, 4);

        if (memcmp(d + pos, "fmt ", 4) == 0) {
            fmtOffset = pos + 8;
            fmtSize = static_cast<int>(chunkSize);
        } else if (memcmp(d + pos, "data", 4) == 0) {
            dataOffset = pos + 8;
            dataSize = static_cast<int>(chunkSize);
        }

        pos += 8 + static_cast<int>((chunkSize + 1) & ~1u);  // pad to even
    }

    if (fmtOffset < 0 || dataOffset < 0 || fmtSize < 16) {
        qCWarning(lcVoiceKeyer) << "Missing fmt or data chunk";
        return false;
    }

    quint16 audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    quint32 sampleRate = 0;
    memcpy(&audioFormat, d + fmtOffset, 2);
    memcpy(&numChannels, d + fmtOffset + 2, 2);
    memcpy(&sampleRate, d + fmtOffset + 4, 4);
    memcpy(&bitsPerSample, d + fmtOffset + 14, 2);

    if (audioFormat != 1 && audioFormat != 3) {
        qCWarning(lcVoiceKeyer) << "Unsupported WAV format:" << audioFormat
                                << "(only PCM int16/float32 supported)";
        return false;
    }

    if (numChannels < 1 || numChannels > 2) {
        qCWarning(lcVoiceKeyer) << "Unsupported channel count:" << numChannels;
        return false;
    }

    int bytesPerSample = bitsPerSample / 8;
    int frameSize = bytesPerSample * numChannels;
    int numFrames = dataSize / frameSize;

    if (numFrames < 1) {
        qCWarning(lcVoiceKeyer) << "No audio frames in WAV";
        return false;
    }

    // Decode to mono float32 at source sample rate
    std::vector<float> mono(numFrames);
    const char* audioData = d + dataOffset;

    for (int i = 0; i < numFrames; ++i) {
        float sample = 0.0f;
        const char* frame = audioData + i * frameSize;

        if (audioFormat == 1 && bitsPerSample == 16) {
            // PCM int16
            int16_t s16 = 0;
            memcpy(&s16, frame, 2);
            sample = s16 / 32768.0f;
            if (numChannels == 2) {
                int16_t s16r = 0;
                memcpy(&s16r, frame + 2, 2);
                sample = (sample + s16r / 32768.0f) * 0.5f;
            }
        } else if (audioFormat == 3 && bitsPerSample == 32) {
            // IEEE float32
            memcpy(&sample, frame, 4);
            if (numChannels == 2) {
                float r = 0.0f;
                memcpy(&r, frame + 4, 4);
                sample = (sample + r) * 0.5f;
            }
        } else {
            qCWarning(lcVoiceKeyer) << "Unsupported bit depth:" << bitsPerSample;
            return false;
        }

        mono[i] = sample;
    }

    // Resample to 24 kHz if needed (simple linear interpolation)
    if (sampleRate == 24000) {
        m_samples = std::move(mono);
    } else {
        double ratio = 24000.0 / sampleRate;
        int outLen = static_cast<int>(numFrames * ratio);
        m_samples.resize(outLen);

        for (int i = 0; i < outLen; ++i) {
            double srcIdx = i / ratio;
            int idx0 = static_cast<int>(srcIdx);
            double frac = srcIdx - idx0;
            int idx1 = qMin(idx0 + 1, numFrames - 1);
            m_samples[i] = static_cast<float>(
                mono[idx0] * (1.0 - frac) + mono[idx1] * frac);
        }
    }

    qCInfo(lcVoiceKeyer) << "Loaded WAV:" << path
                         << numFrames << "frames @" << sampleRate << "Hz →"
                         << m_samples.size() << "samples @ 24kHz";
    return true;
}

} // namespace AetherSDR
