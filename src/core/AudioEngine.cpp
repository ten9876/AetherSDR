#include "AudioEngine.h"
#include "LogManager.h"
#include "SpectralNR.h"
#include "RNNoiseFilter.h"
#include "Resampler.h"

#include <QMediaDevices>
#include <QAudioDevice>
#include <QDir>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace AetherSDR {

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{}

AudioEngine::~AudioEngine()
{
    stopRxStream();
    stopTxStream();
}

QAudioFormat AudioEngine::makeFormat() const
{
    QAudioFormat fmt;
    fmt.setSampleRate(DEFAULT_SAMPLE_RATE);
    fmt.setChannelCount(2);                        // stereo
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

// ─── RX stream ───────────────────────────────────────────────────────────────

bool AudioEngine::startRxStream()
{
    if (m_audioSink) return true;   // already running

    QAudioFormat fmt = makeFormat();
    const QAudioDevice dev = m_outputDevice.isNull()
        ? QMediaDevices::defaultAudioOutput() : m_outputDevice;

    if (!dev.isFormatSupported(fmt)) {
        qCWarning(lcAudio) << "AudioEngine: output device does not support 24kHz stereo Int16, trying 48kHz";
        fmt.setSampleRate(48000);
        m_resampleTo48k = true;
        if (!dev.isFormatSupported(fmt)) {
            qCWarning(lcAudio) << "AudioEngine: output device does not support 48kHz stereo Int16 either";
            qCWarning(lcAudio) << "No audio device detected";
            return false;
        }
    } else {
        m_resampleTo48k = false;
    }

    m_audioSink   = new QAudioSink(dev, fmt, this);
    m_audioSink->setVolume(m_rxVolume);
    m_audioDevice = m_audioSink->start();   // push-mode

    if (!m_audioDevice) {
        qCWarning(lcAudio) << "AudioEngine: failed to open audio sink";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    qCDebug(lcAudio) << "AudioEngine: RX stream started";
    emit rxStarted();
    return true;
}

void AudioEngine::stopRxStream()
{
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink   = nullptr;
        m_audioDevice = nullptr;
    }
    emit rxStopped();
}

void AudioEngine::setRxVolume(float v)
{
    m_rxVolume = qBound(0.0f, v, 2.0f);  // allow up to +6 dB boost
    if (m_audioSink)
        m_audioSink->setVolume(std::min(m_rxVolume, 1.0f));
}

void AudioEngine::setMuted(bool muted)
{
    m_muted = muted;
    if (m_audioSink)
        m_audioSink->setVolume(muted ? 0.0f : m_rxVolume);
}

// Resample 24kHz stereo int16 → 48kHz stereo int16 via r8brain.
QByteArray AudioEngine::resampleStereo(const QByteArray& pcm)
{
    if (!m_rxResampler)
        m_rxResampler = std::make_unique<Resampler>(24000, 48000);
    const auto* src = reinterpret_cast<const int16_t*>(pcm.constData());
    return m_rxResampler->processStereoToStereo(src, pcm.size() / 4);
}

void AudioEngine::feedAudioData(const QByteArray& pcm)
{
    if (!m_audioSink) return;  // PC audio disabled
    // Note: m_radeMode no longer blocks feedAudioData globally.
    // The RADE slice's raw OFDM noise is muted at the slice level (audio_mute=1)
    // so it doesn't reach the speaker. Other slices' audio plays normally.

    auto writeAudio = [this](const QByteArray& data) {
        if (!m_audioDevice || !m_audioDevice->isOpen()) return;
        const QByteArray& out = (m_rxVolume > 1.0f) ? applyBoost(data, m_rxVolume) : data;
        if (m_resampleTo48k)
            m_audioDevice->write(resampleStereo(out));
        else
            m_audioDevice->write(out);
    };

    if (m_rn2Enabled && m_rn2) {
        QByteArray processed = m_rn2->process(pcm);
        writeAudio(processed);
        emit levelChanged(computeRMS(processed));
    } else if (m_nr2Enabled && m_nr2) {
        processNr2(pcm);
        writeAudio(m_nr2Output);
        emit levelChanged(computeRMS(m_nr2Output));
    } else {
        writeAudio(pcm);
        emit levelChanged(computeRMS(pcm));
    }
}

static QString wisdomDir()
{
#ifdef _WIN32
    // Windows: use %APPDATA%/AetherSDR/
    QString dir = QDir::homePath() + "/AppData/Roaming/AetherSDR/";
#else
    QString dir = QDir::homePath() + "/.config/AetherSDR/AetherSDR/";
#endif
    QDir().mkpath(dir);
    return dir;
}

bool AudioEngine::needsWisdomGeneration()
{
    QString path = wisdomDir() + "aethersdr_fftw_wisdom";
    return !QFile::exists(path);
}

void AudioEngine::generateWisdom(std::function<void(int,int,const std::string&)> progress)
{
    SpectralNR::generateWisdom(wisdomDir().toStdString(), std::move(progress));
}

void AudioEngine::setNr2Enabled(bool on)
{
    if (m_nr2Enabled == on) return;
    m_nr2Enabled = on;
    if (on) {
        // Disable RN2 if it was on — they're mutually exclusive
        if (m_rn2Enabled) setRn2Enabled(false);
        // Load wisdom (should already exist — MainWindow generates it on first NR2 press)
        static bool wisdomLoaded = false;
        if (!wisdomLoaded) {
            SpectralNR::generateWisdom(wisdomDir().toStdString());
            wisdomLoaded = true;
        }
        m_nr2 = std::make_unique<SpectralNR>(256, DEFAULT_SAMPLE_RATE);
        if (m_nr2->hasPlanFailed()) {
            qCWarning(lcAudio) << "AudioEngine: NR2 FFTW plan creation failed — disabling";
            m_nr2.reset();
            m_nr2Enabled = false;
            emit nr2EnabledChanged(false);
            return;
        }
    } else {
        m_nr2.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: NR2" << (on ? "enabled" : "disabled");
    emit nr2EnabledChanged(on);
}

void AudioEngine::setRn2Enabled(bool on)
{
    if (m_rn2Enabled == on) return;
    m_rn2Enabled = on;
    if (on) {
        m_rn2 = std::make_unique<RNNoiseFilter>();
        if (!m_rn2->isValid()) {
            qCWarning(lcAudio) << "AudioEngine: RN2 rnnoise_create() failed — disabling";
            m_rn2.reset();
            m_rn2Enabled = false;
            emit rn2EnabledChanged(false);
            return;
        }
        // Disable NR2 if it was on — they're mutually exclusive
        if (m_nr2Enabled) setNr2Enabled(false);
    } else {
        m_rn2.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: RN2 (RNNoise)" << (on ? "enabled" : "disabled");
    emit rn2EnabledChanged(on);
}

void AudioEngine::processNr2(const QByteArray& stereoPcm)
{
    const int totalSamples = stereoPcm.size() / 2;       // int16 count
    const int stereoFrames = totalSamples / 2;            // L+R pairs
    const auto* src = reinterpret_cast<const int16_t*>(stereoPcm.constData());

    // Resize pre-allocated buffers if needed (only re-allocates on size increase)
    if (static_cast<int>(m_nr2Mono.size()) < stereoFrames) {
        m_nr2Mono.resize(stereoFrames);
        m_nr2Processed.resize(stereoFrames);
    }

    // Stereo -> mono (average L+R)
    for (int i = 0; i < stereoFrames; ++i)
        m_nr2Mono[i] = static_cast<int16_t>((src[2 * i] + src[2 * i + 1]) / 2);

    // Process through SpectralNR
    m_nr2->process(m_nr2Mono.data(), m_nr2Processed.data(), stereoFrames);

    // Mono -> stereo (duplicate) into pre-allocated output buffer
    const int outBytes = stereoFrames * 4;  // stereoFrames x 2ch x 2bytes
    m_nr2Output.resize(outBytes);
    auto* dst = reinterpret_cast<int16_t*>(m_nr2Output.data());
    for (int i = 0; i < stereoFrames; ++i) {
        dst[2 * i]     = m_nr2Processed[i];
        dst[2 * i + 1] = m_nr2Processed[i];
    }
}

QByteArray AudioEngine::applyBoost(const QByteArray& pcm, float gain) const
{
    const int nSamples = pcm.size() / sizeof(int16_t);
    const auto* src = reinterpret_cast<const int16_t*>(pcm.constData());
    QByteArray out(pcm.size(), Qt::Uninitialized);
    auto* dst = reinterpret_cast<int16_t*>(out.data());
    for (int i = 0; i < nSamples; ++i) {
        float s = src[i] * gain;
        // Soft clamp to avoid harsh digital clipping
        if (s > 32767.0f) s = 32767.0f;
        else if (s < -32767.0f) s = -32767.0f;
        dst[i] = static_cast<int16_t>(s);
    }
    return out;
}

float AudioEngine::computeRMS(const QByteArray& pcm) const
{
    const int samples = pcm.size() / 2;  // 16-bit samples
    if (samples == 0) return 0.0f;

    const int16_t* data = reinterpret_cast<const int16_t*>(pcm.constData());
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double s = data[i] / 32768.0;
        sum += s * s;
    }
    return static_cast<float>(std::sqrt(sum / samples));
}

// ─── TX stream ────────────────────────────────────────────────────────────────

bool AudioEngine::startTxStream(const QHostAddress& radioAddress, quint16 radioPort)
{
    if (m_audioSource) return true;  // already running

    m_txAddress = radioAddress;
    m_txPort    = radioPort;
    m_txPacketCount = 0;
    m_txAccumulator.clear();

    QAudioFormat fmt = makeFormat();
    const QAudioDevice dev = m_inputDevice.isNull()
        ? QMediaDevices::defaultAudioInput() : m_inputDevice;

    if (dev.isNull()) {
        qCWarning(lcAudio) << "AudioEngine: no audio input device available";
        return false;
    }

    // Sample rate: macOS needs 48kHz for USB mics, Linux can try 24kHz first
#ifdef Q_OS_MAC
    fmt.setSampleRate(48000);
    m_txDownsampleFrom48k = true;
    if (!dev.isFormatSupported(fmt)) {
        fmt.setSampleRate(24000);
        m_txDownsampleFrom48k = false;
        if (!dev.isFormatSupported(fmt)) {
            qCWarning(lcAudio) << "AudioEngine: input device supports neither 48kHz nor 24kHz";
            return false;
        }
    }
#else
    if (!dev.isFormatSupported(fmt)) {
        qCWarning(lcAudio) << "AudioEngine: input device does not support 24kHz, trying 48kHz";
        fmt.setSampleRate(48000);
        m_txDownsampleFrom48k = true;
        if (!dev.isFormatSupported(fmt)) {
            qCWarning(lcAudio) << "AudioEngine: input device does not support 48kHz either";
            return false;
        }
    } else {
        m_txDownsampleFrom48k = false;
    }
#endif

    qCDebug(lcAudio) << "AudioEngine: input device:" << dev.description()
             << "rate:" << fmt.sampleRate() << "ch:" << fmt.channelCount();

#ifdef Q_OS_MAC
    // macOS: QAudioSource pull mode broken — use push mode with QBuffer
    m_micBuffer = new QBuffer(this);
    m_micBuffer->open(QIODevice::ReadWrite);
    m_audioSource = new QAudioSource(dev, fmt, this);
    m_audioSource->start(m_micBuffer);

    if (m_audioSource->state() == QAudio::StoppedState) {
        qCWarning(lcAudio) << "AudioEngine: failed to start audio source";
        delete m_audioSource; m_audioSource = nullptr;
        delete m_micBuffer; m_micBuffer = nullptr;
        return false;
    }

    // Poll push-mode buffer
    m_txPollTimer = new QTimer(this);
    m_txPollTimer->setInterval(5);
    connect(m_txPollTimer, &QTimer::timeout, this, &AudioEngine::onTxAudioReady);
    m_txPollTimer->start();
#else
    // Linux: pull mode works fine
    m_audioSource = new QAudioSource(dev, fmt, this);
    m_micDevice = m_audioSource->start();
    if (!m_micDevice) {
        qCWarning(lcAudio) << "AudioEngine: failed to open audio source";
        delete m_audioSource; m_audioSource = nullptr;
        return false;
    }
    connect(m_micDevice, &QIODevice::readyRead, this, &AudioEngine::onTxAudioReady);
#endif

    qCDebug(lcAudio) << "AudioEngine: TX stream started ->" << radioAddress.toString()
             << ":" << radioPort << "streamId:" << Qt::hex << m_txStreamId;
    return true;
}

void AudioEngine::stopTxStream()
{
#ifdef Q_OS_MAC
    if (m_txPollTimer) {
        m_txPollTimer->stop();
        delete m_txPollTimer;
        m_txPollTimer = nullptr;
    }
#endif
    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
        m_micDevice   = nullptr;
    }
#ifdef Q_OS_MAC
    if (m_micBuffer) {
        delete m_micBuffer;
        m_micBuffer = nullptr;
    }
#endif
    m_txSocket.close();
    m_txAccumulator.clear();
    m_txFloatAccumulator.clear();
}

void AudioEngine::onTxAudioReady()
{
#ifdef Q_OS_MAC
    if (!m_micBuffer || m_txStreamId == 0) return;
    qint64 avail = m_micBuffer->pos();
    if (avail <= 0) return;
    QByteArray data = m_micBuffer->data();
    m_micBuffer->buffer().clear();
    m_micBuffer->seek(0);
    if (data.isEmpty()) return;
#else
    if (!m_micDevice || m_txStreamId == 0) return;
    QByteArray data = m_micDevice->readAll();
    if (data.isEmpty()) return;
#endif

    // Downsample 48kHz → 24kHz: drop every other stereo sample pair
    if (m_txDownsampleFrom48k && data.size() >= 8) {
        const auto* src = reinterpret_cast<const qint16*>(data.constData());
        const int stereoSamples = data.size() / 4;
        QByteArray ds(stereoSamples / 2 * 4, Qt::Uninitialized);
        auto* dst = reinterpret_cast<qint16*>(ds.data());
        for (int i = 0; i < stereoSamples / 2; ++i) {
            dst[i * 2 + 0] = src[i * 4 + 0];  // L (take every other pair)
            dst[i * 2 + 1] = src[i * 4 + 1];  // R
        }
        data = ds;
    }

    // RADE mode: emit raw PCM for RADEEngine instead of sending VITA-49
    if (m_radeMode) {
        emit txRawPcmReady(data);  // data is int16 stereo 24kHz
        return;
    }

    // DAX TX mode: VirtualAudioBridge is the TX audio source.
    if (m_daxTxMode) return;

    // Don't send mic audio when not transmitting — it accumulates in
    // the radio's DAX TX buffer and plays back when TX starts, causing
    // mic bleed into digital modes.
    if (!m_transmitting) return;

    m_txAccumulator.append(data);

    // Process complete packets (128 stereo pairs = 512 bytes of int16 PCM)
    while (m_txAccumulator.size() >= TX_PCM_BYTES_PER_PACKET) {
        const int16_t* pcm = reinterpret_cast<const int16_t*>(m_txAccumulator.constData());

        // Convert int16 stereo → float32 stereo (128 pairs = 256 floats)
        // Apply +24 dB gain: USB mic input levels are typically -20 to -30 dBFS,
        // but SmartSDR expects near full-scale float audio on the DAX TX stream.
        // Without this gain, SSB voice output is barely audible.
        constexpr float TX_GAIN = 16.0f;
        float floatBuf[TX_SAMPLES_PER_PACKET * 2];
        for (int i = 0; i < TX_SAMPLES_PER_PACKET * 2; ++i)
            floatBuf[i] = std::clamp(pcm[i] / 32768.0f * TX_GAIN, -1.0f, 1.0f);

        // Build VITA-49 packet and send via registered UDP socket
        QByteArray packet = buildVitaTxPacket(floatBuf, TX_SAMPLES_PER_PACKET);
        emit txPacketReady(packet);

        // Advance accumulator
        m_txAccumulator.remove(0, TX_PCM_BYTES_PER_PACKET);
    }
}

QByteArray AudioEngine::buildVitaTxPacket(const float* samples, int numStereoSamples)
{
    const int payloadBytes = numStereoSamples * 2 * 4;  // stereo × sizeof(float)
    const int packetWords = (payloadBytes / 4) + VITA_HEADER_WORDS;
    const int packetBytes = packetWords * 4;

    QByteArray packet(packetBytes, '\0');
    quint32* words = reinterpret_cast<quint32*>(packet.data());

    // ── Word 0: Header ────────────────────────────────────────────────────
    // Bits 31-28: packet type = 1 (IFDataWithStream) — DAX TX format
    // Bit  27:    C = 1 (class ID present)
    // Bit  26:    T = 0 (no trailer)
    // Bits 25-24: reserved = 0
    // Bits 23-22: TSI = 2 (Other)
    // Bits 21-20: TSF = 1 (SampleCount)
    // Bits 19-16: packet count (4-bit)
    // Bits 15-0:  packet size (in 32-bit words)
    quint32 hdr = 0;
    hdr |= (0x1u << 28);          // pkt_type = IFDataWithStream (DAX TX)
    hdr |= (1u << 27);            // C = 1
    // T = 0 (bit 26)
    hdr |= (0x2u << 22);          // TSI = Other
    hdr |= (0x1u << 20);          // TSF = SampleCount
    hdr |= ((m_txPacketCount & 0xF) << 16);
    hdr |= (packetWords & 0xFFFF);
    words[0] = qToBigEndian(hdr);

    // ── Word 1: Stream ID ─────────────────────────────────────────────────
    words[1] = qToBigEndian(m_txStreamId);

    // ── Word 2: Class ID OUI (24-bit, right-justified in 32-bit word) ────
    words[2] = qToBigEndian(FLEX_OUI);

    // ── Word 3: InformationClassCode (upper 16) | PacketClassCode (lower 16)
    words[3] = qToBigEndian(
        (static_cast<quint32>(FLEX_INFO_CLASS) << 16) | PCC_IF_NARROW);

    // ── Words 4-6: Timestamps ─────────────────────────────────────────────
    words[4] = 0;  // integer timestamp
    words[5] = 0;  // fractional timestamp high
    words[6] = 0;  // fractional timestamp low (sample count)

    // ── Payload: float32 stereo, big-endian ───────────────────────────────
    quint32* payload = words + VITA_HEADER_WORDS;
    for (int i = 0; i < numStereoSamples * 2; ++i) {
        quint32 raw;
        std::memcpy(&raw, &samples[i], 4);
        payload[i] = qToBigEndian(raw);
    }

    // Increment packet count (4-bit, mod 16)
    m_txPacketCount = (m_txPacketCount + 1) & 0xF;

    return packet;
}

void AudioEngine::setOutputDevice(const QAudioDevice& dev)
{
    m_outputDevice = dev;
    qCDebug(lcAudio) << "AudioEngine: output device set to" << dev.description();
    // Restart RX stream if running
    if (m_audioSink) {
        stopRxStream();
        startRxStream();
    }
}

void AudioEngine::setInputDevice(const QAudioDevice& dev)
{
    m_inputDevice = dev;
    qCDebug(lcAudio) << "AudioEngine: input device set to" << dev.description();
    // Restart TX stream if running
    if (m_audioSource) {
        QHostAddress addr = m_txAddress;
        quint16 port = m_txPort;
        stopTxStream();
        startTxStream(addr, port);
    }
}

// ─── RADE digital voice support ──────────────────────────────────────────────

void AudioEngine::setRadeMode(bool on)
{
    m_radeMode = on;
}

void AudioEngine::sendModemTxAudio(const QByteArray& float32pcm)
{
    if (m_txStreamId == 0) return;

    m_txFloatAccumulator.append(float32pcm);

    constexpr int FLOAT_BYTES_PER_PKT = TX_SAMPLES_PER_PACKET * 2 * sizeof(float); // 1024
    while (m_txFloatAccumulator.size() >= FLOAT_BYTES_PER_PKT) {
        auto* samples = reinterpret_cast<const float*>(m_txFloatAccumulator.constData());
        QByteArray pkt = buildVitaTxPacket(samples, TX_SAMPLES_PER_PACKET);
        emit txPacketReady(pkt);
        m_txFloatAccumulator.remove(0, FLOAT_BYTES_PER_PKT);
    }
}

void AudioEngine::feedDaxTxAudio(const QByteArray& float32pcm)
{
    if (m_txStreamId == 0) return;

    // Block DAX audio when mic is actively sending (voice mode TX).
    // This prevents dual-source jitter on the same VITA-49 stream.
    // During RX and digital TX, DAX flows freely.
    if (m_transmitting && !m_daxTxMode) return;
    m_txFloatAccumulator.append(float32pcm);
    constexpr int FLOAT_BYTES_PER_PKT = TX_SAMPLES_PER_PACKET * 2 * sizeof(float);
    while (m_txFloatAccumulator.size() >= FLOAT_BYTES_PER_PKT) {
        auto* samples = reinterpret_cast<const float*>(m_txFloatAccumulator.constData());
        QByteArray pkt = buildVitaTxPacket(samples, TX_SAMPLES_PER_PACKET);
        emit txPacketReady(pkt);
        m_txFloatAccumulator.remove(0, FLOAT_BYTES_PER_PKT);
    }
}

void AudioEngine::feedDecodedSpeech(const QByteArray& pcm)
{
    if (!m_audioSink || !m_audioDevice || !m_audioDevice->isOpen()) return;
    if (m_resampleTo48k)
        m_audioDevice->write(resampleStereo(pcm));
    else
        m_audioDevice->write(pcm);
}

} // namespace AetherSDR
