#pragma once

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QUdpSocket>
#include <QByteArray>

namespace AetherSDR {

class PanadapterStream;

// AudioEngine handles audio playback (RX) and capture (TX).
//
// RX path:
//   Audio PCM arrives via PanadapterStream::audioDataReady() — the radio sends
//   VITA-49 IF-Data packets to the single "client udpport" socket owned by
//   PanadapterStream. PanadapterStream strips the header and emits the raw PCM;
//   connect that signal to feedAudioData() then call startRxStream() to open
//   the QAudioSink.
//
// TX path:
//   Captures mic/input audio via QAudioSource, frames it as VITA-49
//   ExtDataWithStream packets (PCC 0x03E3, float32 stereo big-endian),
//   and sends to the radio via UDP.

class AudioEngine : public QObject {
    Q_OBJECT

public:
    static constexpr int DEFAULT_SAMPLE_RATE = 24000;

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // Open the QAudioSink. Must be called once when connected.
    bool startRxStream();
    void stopRxStream();

    // TX (microphone) – capture audio and send VITA-49 packets to radio
    bool startTxStream(const QHostAddress& radioAddress, quint16 radioPort);
    void stopTxStream();

    // Set the TX stream ID (from radio's response to "stream create type=dax_tx")
    void setTxStreamId(quint32 id) { m_txStreamId = id; }

    // Set target address/port for TX VITA-49 packets (without starting mic capture)
    void setTxTarget(const QHostAddress& addr, quint16 port) {
        m_txAddress = addr; m_txPort = port;
    }

    // Set the PanadapterStream used to send TX packets via the registered UDP port.
    void setPanStream(PanadapterStream* ps) { m_panStream = ps; }

    float rxVolume() const  { return m_rxVolume; }
    void  setRxVolume(float v);

    bool isMuted() const   { return m_muted; }
    void setMuted(bool m);

    // Send DAX TX audio from VirtualAudioBridge (float32 stereo, 48 kHz).
    // Internally decimates 2:1 to 24 kHz before building VITA-49 packets.
    void sendDaxTxAudio(const QByteArray& floatPcm);

    // TX gain for DAX audio (0.0–1.0). Default 0.05 (≈ −26 dB).
    void setDaxTxGain(float g) { m_daxTxGain = qBound(0.0f, g, 1.0f); }
    float daxTxGain() const { return m_daxTxGain; }

    // Gate DAX TX audio: only send packets while MOX is active.
    void setMoxActive(bool active);

public slots:
    // Receives stripped PCM from PanadapterStream::audioDataReady().
    void feedAudioData(const QByteArray& pcm);

signals:
    void rxStarted();
    void rxStopped();
    void levelChanged(float rms);  // audio level for VU meter, 0.0–1.0

private slots:
    void onTxAudioReady();

private:
    QAudioFormat makeFormat() const;
    float computeRMS(const QByteArray& pcm) const;
    QByteArray buildVitaTxPacket(const float* samples, int numStereoSamples);
    void flushTxWithSilence();

    // RX
    QAudioSink*   m_audioSink{nullptr};
    QIODevice*    m_audioDevice{nullptr};   // raw device from QAudioSink

    // TX
    QUdpSocket    m_txSocket;
    QAudioSource* m_audioSource{nullptr};
    QIODevice*    m_micDevice{nullptr};
    QHostAddress  m_txAddress;
    quint16       m_txPort{0};
    quint32       m_txStreamId{0};
    quint8        m_txPacketCount{0};    // 4-bit, mod 16
    QByteArray    m_txAccumulator;       // accumulate PCM until 128 stereo pairs
    QByteArray    m_daxTxAccumulator;    // accumulate float32 from VirtualAudioBridge

    PanadapterStream* m_panStream{nullptr};
    bool  m_moxActive{false};
    float m_daxTxGain{0.5f};    // DAX TX gain (default −6 dB, adjustable via CatApplet)
    float m_rxVolume{1.0f};
    bool  m_muted{false};

    // VITA-49 TX constants
    static constexpr int    TX_SAMPLES_PER_PACKET = 128;  // stereo pairs
    static constexpr int    TX_PCM_BYTES_PER_PACKET = TX_SAMPLES_PER_PACKET * 2 * 2; // 128 pairs × 2ch × 2bytes
    static constexpr int    VITA_HEADER_WORDS = 7;
    static constexpr int    VITA_HEADER_BYTES = VITA_HEADER_WORDS * 4;  // 28 bytes
    static constexpr quint32 FLEX_OUI = 0x001C2D;
    static constexpr quint16 FLEX_INFO_CLASS = 0x534C;
    static constexpr quint16 PCC_IF_NARROW = 0x03E3;
};

} // namespace AetherSDR
