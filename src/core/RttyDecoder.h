#pragma once

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QThread>

#include <atomic>
#include <cmath>

namespace AetherSDR {

// Client-side RTTY (Baudot/ITA2) decoder using mark/space bandpass filters.
// Runs decoding on a worker thread. Feed it 24kHz stereo float32 PCM
// and it emits decoded text character by character.
//
// Usage:
//   decoder.start();
//   connect(audioSource, &Source::audioReady, &decoder, &RttyDecoder::feedAudio);
//   connect(&decoder, &RttyDecoder::textDecoded, panel, &Panel::appendText);

class RttyDecoder : public QObject {
    Q_OBJECT

public:
    explicit RttyDecoder(QObject* parent = nullptr);
    ~RttyDecoder() override;

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // Decoder parameters
    float baudRate() const { return m_baudRate; }
    int shiftHz() const { return m_shiftHz; }
    int markFreqHz() const { return m_markFreqHz; }
    bool reversePolarity() const { return m_reverse; }

public slots:
    // Feed 24kHz stereo float32 PCM (same format as CwDecoder receives).
    void feedAudio(const QByteArray& pcm24kStereo);

    void setBaudRate(float baud);
    void setShiftHz(int hz);
    void setMarkFreqHz(int hz);
    void setReversePolarity(bool rev);

signals:
    void textDecoded(const QString& text, float confidence);
    void statsUpdated(float snrDb, bool locked);

private:
    void decodeLoop();
    void recalcFilterCoeffs();

    // 2nd-order IIR bandpass filter state
    struct BiquadState {
        double x1{0}, x2{0}, y1{0}, y2{0};
    };
    struct BiquadCoeffs {
        double b0{0}, b1{0}, b2{0}, a1{0}, a2{0};
    };

    static BiquadCoeffs designBandpass(double centerHz, double bwHz, double sampleRate);
    static double processBiquad(BiquadCoeffs& c, BiquadState& s, double x);

    // Baudot/ITA2 decode
    static constexpr int BAUDOT_LTRS = 0x1F;
    static constexpr int BAUDOT_FIGS = 0x1B;
    static constexpr int BAUDOT_NULL = 0x00;

    static char baudotToAscii(int code, bool figs);

    QThread*      m_workerThread{nullptr};

    // Ring buffer for audio samples (mono float at 24kHz)
    QMutex        m_bufMutex;
    QByteArray    m_ringBuf;
    static constexpr int RING_CAPACITY = 24000 * sizeof(float) * 4; // 4 seconds

    std::atomic<bool> m_running{false};

    // Decoder parameters (atomic for cross-thread access)
    std::atomic<float> m_baudRate{45.45f};
    std::atomic<int>   m_shiftHz{170};
    std::atomic<int>   m_markFreqHz{2125};
    std::atomic<bool>  m_reverse{false};
    std::atomic<bool>  m_paramsChanged{true}; // trigger filter recalc

    // Filter state (worker thread only)
    BiquadCoeffs m_markCoeffs, m_spaceCoeffs;
    BiquadState  m_markState, m_spaceState;

    // Envelope detector state (worker thread only)
    double m_markEnv{0};
    double m_spaceEnv{0};

    // Bit clock recovery state (worker thread only)
    double m_bitClock{0};
    int    m_prevBit{-1};
    int    m_shiftReg{0};
    int    m_bitCount{0};
    bool   m_inChar{false};
    bool   m_figsMode{false};

    static constexpr double SAMPLE_RATE = 24000.0;
};

} // namespace AetherSDR
