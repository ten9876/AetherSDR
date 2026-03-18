#include "AudioEngine.h"
#include "SpectralNR.h"

#include <QMediaDevices>
#include <QAudioDevice>
#include <QDir>
#include <QtEndian>
#include <QDebug>
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

    const QAudioFormat fmt = makeFormat();
    const QAudioDevice dev = m_outputDevice.isNull()
        ? QMediaDevices::defaultAudioOutput() : m_outputDevice;

    if (!dev.isFormatSupported(fmt))
        qWarning() << "AudioEngine: output device does not support 24kHz stereo Int16";

    m_audioSink   = new QAudioSink(dev, fmt, this);
    m_audioSink->setVolume(m_rxVolume);
    m_audioDevice = m_audioSink->start();   // push-mode

    if (!m_audioDevice) {
        qWarning() << "AudioEngine: failed to open audio sink";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    qDebug() << "AudioEngine: RX stream started";
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
    m_rxVolume = qBound(0.0f, v, 1.0f);
    if (m_audioSink)
        m_audioSink->setVolume(m_rxVolume);
}

void AudioEngine::setMuted(bool muted)
{
    m_muted = muted;
    if (m_audioSink)
        m_audioSink->setVolume(muted ? 0.0f : m_rxVolume);
}

void AudioEngine::feedAudioData(const QByteArray& pcm)
{
    if (m_nr2Enabled && m_nr2) {
        processNr2(pcm);
        if (m_audioDevice && m_audioDevice->isOpen())
            m_audioDevice->write(m_nr2Output);
        emit levelChanged(computeRMS(m_nr2Output));
    } else {
        if (m_audioDevice && m_audioDevice->isOpen())
            m_audioDevice->write(pcm);
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
        // Load wisdom (should already exist — MainWindow generates it on first NR2 press)
        static bool wisdomLoaded = false;
        if (!wisdomLoaded) {
            SpectralNR::generateWisdom(wisdomDir().toStdString());
            wisdomLoaded = true;
        }
        m_nr2 = std::make_unique<SpectralNR>(256, DEFAULT_SAMPLE_RATE);
    } else {
        m_nr2.reset();
    }
    qDebug() << "AudioEngine: NR2" << (on ? "enabled" : "disabled");
    emit nr2EnabledChanged(on);
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

    const QAudioFormat fmt = makeFormat();
    const QAudioDevice dev = m_inputDevice.isNull()
        ? QMediaDevices::defaultAudioInput() : m_inputDevice;

    if (dev.isNull()) {
        qWarning() << "AudioEngine: no audio input device available";
        return false;
    }

    m_audioSource = new QAudioSource(dev, fmt, this);
    m_micDevice   = m_audioSource->start();

    if (!m_micDevice) {
        qWarning() << "AudioEngine: failed to open audio source";
        delete m_audioSource;
        m_audioSource = nullptr;
        return false;
    }

    connect(m_micDevice, &QIODevice::readyRead,
            this, &AudioEngine::onTxAudioReady);

    qDebug() << "AudioEngine: TX stream started ->" << radioAddress.toString()
             << ":" << radioPort << "streamId:" << Qt::hex << m_txStreamId;
    return true;
}

void AudioEngine::stopTxStream()
{
    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
        m_micDevice   = nullptr;
    }
    m_txSocket.close();
    m_txAccumulator.clear();
}

void AudioEngine::onTxAudioReady()
{
    if (!m_micDevice || m_txStreamId == 0) return;

    // Read all available PCM data (int16 stereo, 24 kHz)
    const QByteArray data = m_micDevice->readAll();
    if (data.isEmpty()) return;

    m_txAccumulator.append(data);

    // Process complete packets (128 stereo pairs = 512 bytes of int16 PCM)
    while (m_txAccumulator.size() >= TX_PCM_BYTES_PER_PACKET) {
        const int16_t* pcm = reinterpret_cast<const int16_t*>(m_txAccumulator.constData());

        // Convert int16 stereo → float32 stereo (128 pairs = 256 floats)
        float floatBuf[TX_SAMPLES_PER_PACKET * 2];
        for (int i = 0; i < TX_SAMPLES_PER_PACKET * 2; ++i)
            floatBuf[i] = pcm[i] / 32768.0f;

        // Build and send VITA-49 packet
        QByteArray packet = buildVitaTxPacket(floatBuf, TX_SAMPLES_PER_PACKET);
        m_txSocket.writeDatagram(packet, m_txAddress, m_txPort);

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
    qDebug() << "AudioEngine: output device set to" << dev.description();
    // Restart RX stream if running
    if (m_audioSink) {
        stopRxStream();
        startRxStream();
    }
}

void AudioEngine::setInputDevice(const QAudioDevice& dev)
{
    m_inputDevice = dev;
    qDebug() << "AudioEngine: input device set to" << dev.description();
    // Restart TX stream if running
    if (m_audioSource) {
        QHostAddress addr = m_txAddress;
        quint16 port = m_txPort;
        stopTxStream();
        startTxStream(addr, port);
    }
}

} // namespace AetherSDR
