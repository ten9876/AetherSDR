#pragma once

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>
#include <QUdpSocket>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <QBuffer>
#include <QByteArray>
#include <QElapsedTimer>

class QMediaDevices;

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace AetherSDR {

class SpectralNR;
class SpecbleachFilter;
class RNNoiseFilter;
class NvidiaBnrFilter;
class DeepFilterFilter;
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
    // Q_INVOKABLE: must run on the audio worker thread (#502)
    Q_INVOKABLE bool startRxStream();
    Q_INVOKABLE void stopRxStream();

    // TX (microphone) – capture audio and send VITA-49 packets to radio
    Q_INVOKABLE bool startTxStream(const QHostAddress& radioAddress, quint16 radioPort);
    Q_INVOKABLE void stopTxStream();

    // Set the DAX TX stream ID (from radio's response to "stream create type=dax_tx")
    void setTxStreamId(quint32 id) { m_txStreamId = id; }
    quint32 txStreamId() const { return m_txStreamId; }
    // Set the remote audio TX stream ID (for voice TX and VOX monitoring)
    void setRemoteTxStreamId(quint32 id) { m_remoteTxStreamId = id; }

    float rxVolume() const  { return m_rxVolume.load(); }
    void  setRxVolume(float v);

    bool isMuted() const       { return m_muted.load(); }
    void setMuted(bool m);
    bool isTxStreaming() const { return m_audioSource != nullptr; }

    // Client-side PC mic gain (0-100 → 0.0-1.0, applied before Opus encoding)
    void setPcMicGain(int level) { m_pcMicGain.store(qBound(0, level, 100) / 100.0f); }

    // Opus TX encoding for SmartLink compressed audio
    void setOpusTxEnabled(bool on) { m_opusTxEnabled.store(on); }
    bool isOpusTxEnabled() const { return m_opusTxEnabled.load(); }

    // RADE digital voice mode
    void setRadeMode(bool on);
    bool isRadeMode() const { return m_radeMode; }

    // Sends RADE modem output (float32 PCM) as VITA-49 packets via m_txSocket
    void sendModemTxAudio(const QByteArray& float32pcm);

    // DAX TX: VirtualAudioBridge feeds float32 PCM for VITA-49 TX
    void setDaxTxMode(bool on);
    bool isDaxTxMode() const { return m_daxTxMode.load(); }
    // true: radio DAX TX route (transmit dax=1, PCC 0x0123 int16 mono)
    // false: low-latency PC mic route (transmit dax=0, PCC 0x03E3 float32 stereo)
    void setDaxTxUseRadioRoute(bool on);
    bool daxTxUseRadioRoute() const { return m_daxTxUseRadioRoute.load(); }
    void setTransmitting(bool tx);
    void setRadioTransmitting(bool tx);  // raw interlock state (regardless of TX ownership)
    void clearTxAccumulators() { m_txAccumulator.clear(); m_txFloatAccumulator.clear(); m_daxPreTxBuffer.clear(); }
    Q_INVOKABLE void feedDaxTxAudio(const QByteArray& float32pcm);

    // Plays RADE decoded speech (int16 stereo 24kHz) bypassing m_radeMode block
    void feedDecodedSpeech(const QByteArray& pcm);

    // Client-side NR2 (spectral noise reduction)
    // Q_INVOKABLE: called from main thread, runs on audio worker thread (#502)
    Q_INVOKABLE void setNr2Enabled(bool on);
    bool nr2Enabled() const { return m_nr2Enabled.load(); }
    // NR2 user-adjustable parameters (thread-safe via atomic in SpectralNR)
    void setNr2GainMax(float v);
    void setNr2Qspp(float v);
    void setNr2GainSmooth(float v);
    void setNr2GainMethod(int method);
    void setNr2NpeMethod(int method);
    void setNr2AeFilter(bool on);
    // Client-side RN2 (RNNoise neural noise suppression)
    Q_INVOKABLE void setRn2Enabled(bool on);
    bool rn2Enabled() const { return m_rn2Enabled.load(); }

    // Client-side NR4 (libspecbleach spectral noise reduction)
    Q_INVOKABLE void setNr4Enabled(bool on);
    bool nr4Enabled() const { return m_nr4Enabled.load(); }
    void setNr4ReductionAmount(float dB);
    void setNr4SmoothingFactor(float pct);
    void setNr4WhiteningFactor(float pct);
    void setNr4AdaptiveNoise(bool on);
    void setNr4NoiseEstimationMethod(int method);
    void setNr4MaskingDepth(float v);
    void setNr4SuppressionStrength(float v);

    // Client-side BNR (NVIDIA NIM GPU noise removal)
    Q_INVOKABLE void setBnrEnabled(bool on);
    bool bnrEnabled() const { return m_bnrEnabled.load(); }
    void setBnrAddress(const QString& addr);
    QString bnrAddress() const { return m_bnrAddress; }
    void setBnrIntensity(float ratio);
    float bnrIntensity() const;
    bool bnrConnected() const;

    // Client-side DFNR (DeepFilterNet3 neural noise reduction)
    Q_INVOKABLE void setDfnrEnabled(bool on);
    bool dfnrEnabled() const { return m_dfnrEnabled.load(); }
    void setDfnrAttenLimit(float db);
    float dfnrAttenLimit() const;
    void setDfnrPostFilterBeta(float beta);

    // Ensure FFTW wisdom is loaded/generated. Returns true if wisdom
    // needs to be generated (slow). Call generateWisdom() in that case.
    static bool needsWisdomGeneration();
    static QString wisdomFilePath();
    // Must be called from a worker thread — blocks for several minutes.
    static void generateWisdom(std::function<void(int,int,const std::string&)> progress = nullptr);

    // Device selection (restarts the stream if currently running)
    void setOutputDevice(const QAudioDevice& dev);
    void setInputDevice(const QAudioDevice& dev);
    QAudioDevice outputDevice() const { return m_outputDevice; }
    QAudioDevice inputDevice()  const { return m_inputDevice; }
    qsizetype rxBufferBytes() const { return m_rxBufferBytes.load(); }
    qsizetype rxBufferPeakBytes() const { return m_rxBufferPeakBytes.load(); }
    quint64 rxBufferUnderrunCount() const { return m_rxBufferUnderrunCount.load(); }
    int rxBufferSampleRate() const { return m_rxBufferSampleRate.load(); }

public slots:
    // Receives stripped PCM from PanadapterStream::audioDataReady().
    void feedAudioData(const QByteArray& pcm);

signals:
    void rxStarted();
    void rxStopped();
    void levelChanged(float rms);  // audio level for VU meter, 0.0–1.0
    void nr2EnabledChanged(bool on);
    void nr4EnabledChanged(bool on);
    void rn2EnabledChanged(bool on);
    void bnrEnabledChanged(bool on);
    void bnrConnectionChanged(bool connected);
    void dfnrEnabledChanged(bool on);
    void txRawPcmReady(const QByteArray& pcm);  // raw 24kHz stereo int16 PCM for RADEEngine
    void txPacketReady(const QByteArray& vitaPacket);  // VITA-49 TX packet for PanadapterStream
    void pcMicLevelChanged(float peakDbfs, float avgDbfs);  // client-side PC mic metering

private slots:
    void onTxAudioReady();

private:
    QAudioFormat makeFormat() const;
    float computeRMS(const QByteArray& pcm) const;
    QByteArray applyBoost(const QByteArray& pcm, float gain) const;
    QByteArray buildVitaTxPacket(const float* samples, int numStereoSamples);
    void sendVoiceTxPacket(const QByteArray& pcmData, quint32 streamId);
    QByteArray resampleStereo(const QByteArray& pcm);
    void processNr2(const QByteArray& stereoPcm);
    void updateRxBufferStats();

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
    quint32       m_txStreamId{0};         // DAX TX stream
    quint32       m_remoteTxStreamId{0};  // remote_audio_tx (voice/VOX)
    quint8        m_txPacketCount{0};    // 4-bit, mod 16
    QByteArray    m_txAccumulator;       // accumulate PCM until 128 stereo pairs
    QByteArray    m_voxAccumulator;     // accumulate PCM for VOX/met_in_rx stream
    QByteArray    m_txFloatAccumulator;  // accumulate float32 PCM for RADE modem TX
    QByteArray    m_daxPreTxBuffer;      // short rolling pre-TX buffer for low-latency DAX mode
    std::atomic<bool> m_radeMode{false}; // RADE digital voice mode active (atomic: cross-thread)
    std::atomic<float> m_pcMicGain{1.0f};     // client-side PC mic gain (0.0-1.0)
    std::atomic<bool>  m_daxTxMode{false};    // DAX TX mode: VirtualAudioBridge handles TX
    std::atomic<bool>  m_daxTxUseRadioRoute{false}; // false = low-latency route (dax=0)
    std::atomic<bool>  m_transmitting{false}; // true when radio is in TX AND we own TX
    std::atomic<bool>  m_radioTransmitting{false}; // true when radio is in TX (any owner)
    std::atomic<bool>  m_opusTxEnabled{false}; // Opus TX encoding for SmartLink
    std::unique_ptr<class OpusCodec> m_opusTxCodec; // lazy-init on first TX with Opus
    QByteArray    m_opusTxAccumulator;  // accumulate stereo samples for Opus frame
    QVector<QByteArray> m_opusTxQueue;  // pacing queue for even 10ms packet delivery
    QTimer*       m_opusTxPaceTimer{nullptr};

    // Client-side PC mic metering (accumulated over ~50ms window)
    float         m_pcMicPeak{0.0f};
    double        m_pcMicSumSq{0.0};
    int           m_pcMicSampleCount{0};
    static constexpr int kMicMeterWindowSamples = 24000 / 20;  // ~50ms at 24kHz

    QAudioDevice m_outputDevice;
    QAudioDevice m_inputDevice;
    std::atomic<float> m_rxVolume{1.0f};
    std::atomic<bool>  m_muted{false};
    bool  m_resampleTo48k{false};      // RX: upsample 24kHz → 48kHz output
    std::unique_ptr<Resampler> m_rxResampler;  // 24k stereo → 48k stereo (lazy init)
    bool  m_txNeedsResample{false};      // TX: input rate != 24kHz, needs resampling
    bool  m_txInputMono{false};          // TX: input device is mono
    int   m_txInputRate{24000};          // TX: actual input sample rate
    std::unique_ptr<Resampler> m_txResampler;  // e.g. 48k→24k (lazy init)

    // DSP lifecycle mutex: held during feedAudioData() DSP section AND
    // during enable/disable to prevent use-after-free (#502)
    std::recursive_mutex m_dspMutex;

    // Client-side NR2 (spectral)
    std::unique_ptr<SpectralNR> m_nr2;
    std::atomic<bool> m_nr2Enabled{false};
    // Client-side NR4 (libspecbleach)
#ifdef HAVE_SPECBLEACH
    std::unique_ptr<SpecbleachFilter> m_nr4;
#endif
    std::atomic<bool> m_nr4Enabled{false};

    // Client-side RN2 (RNNoise)
    std::unique_ptr<RNNoiseFilter> m_rn2;
    std::atomic<bool> m_rn2Enabled{false};

    // Client-side BNR (NVIDIA NIM)
    std::unique_ptr<NvidiaBnrFilter> m_bnr;
    std::unique_ptr<Resampler> m_bnrUp;    // 24k→48k mono
    std::unique_ptr<Resampler> m_bnrDown;  // 48k→24k mono
    std::atomic<bool> m_bnrEnabled{false};
    QString m_bnrAddress{"localhost:8001"};
    QByteArray m_bnrOutBuf;  // jitter buffer: denoised 24kHz stereo int16
    bool m_bnrPrimed{false}; // true after enough denoised data accumulated
    void processBnr(const QByteArray& stereoPcm);

    // Client-side DFNR (DeepFilterNet3)
#ifdef HAVE_DFNR
    std::unique_ptr<DeepFilterFilter> m_dfnr;
#endif
    std::atomic<bool> m_dfnrEnabled{false};

    // Pre-allocated NR2 work buffers (avoid per-call heap allocation)
    std::vector<float> m_nr2Mono;
    std::vector<float> m_nr2Processed;
    QByteArray m_nr2Output;

    // Audio device change detection — restarts RX when USB devices
    // power-cycle or WASAPI sessions reset after idle (#1361)
    QMediaDevices* m_mediaDevices{nullptr};
    bool           m_rxStreamStarted{false};  // guard: ignore device changes before first start

    // Zombie sink watchdog: tracks consecutive RX timer ticks where we have
    // data to write but bytesFree() == 0, indicating a stale WASAPI handle.
    // After ~2 seconds (200 ticks × 10ms), force a restart. (#1361)
    int m_rxZombieTickCount{0};
    static constexpr int kZombieTickThreshold = 200;  // 200 × 10ms = 2s

    // RX audio buffer handling
    QTimer*       m_rxTimer{nullptr};
    QByteArray    m_rxBuffer;
    std::atomic<qsizetype> m_rxBufferBytes{0};
    std::atomic<qsizetype> m_rxBufferPeakBytes{0};
    std::atomic<quint64>   m_rxBufferUnderrunCount{0};
    std::atomic<int>       m_rxBufferSampleRate{DEFAULT_SAMPLE_RATE};

    // VITA-49 TX constants
    static constexpr int    TX_SAMPLES_PER_PACKET = 128;  // audio frames per packet
    static constexpr int    TX_PCM_BYTES_PER_PACKET = TX_SAMPLES_PER_PACKET * 2 * 2; // 128 frames × 2ch × int16
    static constexpr int    VITA_HEADER_WORDS = 7;
    static constexpr int    VITA_HEADER_BYTES = VITA_HEADER_WORDS * 4;  // 28 bytes
    static constexpr quint32 FLEX_OUI = 0x001C2D;
    static constexpr quint16 FLEX_INFO_CLASS = 0x534C;
    static constexpr quint16 PCC_IF_NARROW = 0x03E3;
    static constexpr quint16 PCC_DAX_REDUCED = 0x0123;  // reduced BW DAX (24kHz int16 mono)
};

} // namespace AetherSDR
