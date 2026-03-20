#pragma once

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>
#include <QUdpSocket>
#include <QTimer>
#include <QBuffer>
#include <QByteArray>

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace AetherSDR {

class SpectralNR;
class RNNoiseFilter;
class Resampler;

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

    // Set the TX stream ID (from radio's response to "stream create type=remote_audio_tx")
    void setTxStreamId(quint32 id) { m_txStreamId = id; }
    quint32 txStreamId() const { return m_txStreamId; }

    float rxVolume() const  { return m_rxVolume; }
    void  setRxVolume(float v);

    bool isMuted() const       { return m_muted; }
    void setMuted(bool m);
    bool isTxStreaming() const { return m_audioSource != nullptr; }

    // RADE digital voice mode
    void setRadeMode(bool on);
    bool isRadeMode() const { return m_radeMode; }

    // Sends RADE modem output (float32 PCM) as VITA-49 packets via m_txSocket
    void sendModemTxAudio(const QByteArray& float32pcm);

    // DAX TX: VirtualAudioBridge feeds float32 PCM for VITA-49 TX
    void setDaxTxMode(bool on) { m_daxTxMode = on; }
    bool isDaxTxMode() const { return m_daxTxMode; }
    void setTransmitting(bool tx) { m_transmitting = tx; }
    void clearTxAccumulators() { m_txAccumulator.clear(); m_txFloatAccumulator.clear(); }
    void feedDaxTxAudio(const QByteArray& float32pcm);

    // Plays RADE decoded speech (int16 stereo 24kHz) bypassing m_radeMode block
    void feedDecodedSpeech(const QByteArray& pcm);

    // Client-side NR2 (spectral noise reduction)
    void setNr2Enabled(bool on);
    bool nr2Enabled() const { return m_nr2Enabled; }

    // Client-side RN2 (RNNoise neural noise suppression)
    void setRn2Enabled(bool on);
    bool rn2Enabled() const { return m_rn2Enabled; }

    // Ensure FFTW wisdom is loaded/generated. Returns true if wisdom
    // needs to be generated (slow). Call generateWisdom() in that case.
    static bool needsWisdomGeneration();
    // Must be called from a worker thread — blocks for several minutes.
    static void generateWisdom(std::function<void(int,int,const std::string&)> progress = nullptr);

    // Device selection (restarts the stream if currently running)
    void setOutputDevice(const QAudioDevice& dev);
    void setInputDevice(const QAudioDevice& dev);
    QAudioDevice outputDevice() const { return m_outputDevice; }
    QAudioDevice inputDevice()  const { return m_inputDevice; }

public slots:
    // Receives stripped PCM from PanadapterStream::audioDataReady().
    void feedAudioData(const QByteArray& pcm);

signals:
    void rxStarted();
    void rxStopped();
    void levelChanged(float rms);  // audio level for VU meter, 0.0–1.0
    void nr2EnabledChanged(bool on);
    void rn2EnabledChanged(bool on);
    void txRawPcmReady(const QByteArray& pcm);  // raw 24kHz stereo int16 PCM for RADEEngine
    void txPacketReady(const QByteArray& vitaPacket);  // VITA-49 TX packet for PanadapterStream

private slots:
    void onTxAudioReady();

private:
    QAudioFormat makeFormat() const;
    float computeRMS(const QByteArray& pcm) const;
    QByteArray buildVitaTxPacket(const float* samples, int numStereoSamples);
    QByteArray resampleStereo(const QByteArray& pcm);
    void processNr2(const QByteArray& stereoPcm);

    // RX
    QAudioSink*   m_audioSink{nullptr};
    QIODevice*    m_audioDevice{nullptr};   // raw device from QAudioSink

    // TX
    QUdpSocket    m_txSocket;
    QAudioSource* m_audioSource{nullptr};
    QIODevice*    m_micDevice{nullptr};
#ifdef Q_OS_MAC
    QTimer*       m_txPollTimer{nullptr};
    QBuffer*      m_micBuffer{nullptr};
#endif
    QHostAddress  m_txAddress;
    quint16       m_txPort{0};
    quint32       m_txStreamId{0};
    quint8        m_txPacketCount{0};    // 4-bit, mod 16
    QByteArray    m_txAccumulator;       // accumulate PCM until 128 stereo pairs
    QByteArray    m_txFloatAccumulator;  // accumulate float32 PCM for RADE modem TX
    bool          m_radeMode{false};     // RADE digital voice mode active
    bool          m_daxTxMode{false};    // DAX TX mode: VirtualAudioBridge handles TX
    bool          m_transmitting{false}; // true when radio is in TX (MOX on)

    QAudioDevice m_outputDevice;
    QAudioDevice m_inputDevice;
    float m_rxVolume{1.0f};
    bool  m_muted{false};
    bool  m_resampleTo48k{false};      // RX: upsample 24kHz → 48kHz output
    std::unique_ptr<Resampler> m_rxResampler;  // 24k stereo → 48k stereo (lazy init)
    bool  m_txDownsampleFrom48k{false}; // TX: downsample 48kHz → 24kHz input

    // Client-side NR2 (spectral)
    std::unique_ptr<SpectralNR> m_nr2;
    bool m_nr2Enabled{false};

    // Client-side RN2 (RNNoise)
    std::unique_ptr<RNNoiseFilter> m_rn2;
    bool m_rn2Enabled{false};

    // Pre-allocated NR2 work buffers (avoid per-call heap allocation)
    std::vector<int16_t> m_nr2Mono;
    std::vector<int16_t> m_nr2Processed;
    QByteArray m_nr2Output;

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
