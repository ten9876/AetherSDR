#pragma once

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <atomic>
#include <array>

class QTimer;
class QFile;

namespace AetherSDR {

// DAX virtual audio bridge for Linux using PulseAudio pipe modules.
// Creates named pipes in /tmp and loads module-pipe-source (RX, 4 channels)
// and module-pipe-sink (TX, 1 channel) so that apps like WSJT-X, VARA, fldigi
// see "AetherSDR DAX 1-4" as audio input and "AetherSDR TX" as audio output.
// Works with both PulseAudio and PipeWire (via pipewire-pulse).
class PipeWireAudioBridge : public QObject {
    Q_OBJECT

public:
    static constexpr int NUM_CHANNELS = 4;
    static constexpr int SAMPLE_RATE  = 24000;
    static constexpr int CHANNELS     = 1;  // mono — ham radio DAX is single-channel

    explicit PipeWireAudioBridge(QObject* parent = nullptr);
    ~PipeWireAudioBridge() override;

    bool open();
    void close();
    bool isOpen() const { return m_open; }

    void setGain(float g);                     // global gain (all RX channels)
    void setChannelGain(int channel, float g);  // per-channel RX gain (1-4)
    void setTxGain(float g);                    // TX gain
    float gain() const { return m_gain; }

public slots:
    void feedDaxAudio(int channel, const QByteArray& pcm);
    void setTransmitting(bool tx);

signals:
    void txAudioReady(const QByteArray& pcm);
    void daxRxLevel(int channel, float rms);  // 0.0–1.0 RMS for meter display
    void daxTxLevel(float rms);

private:
    bool loadPipeSource(int index);
    bool loadPipeSink();
    void unloadModules();

    // RX: we write int16 mono PCM to these pipes → PulseAudio reads them
    struct RxPipe {
        int fd{-1};
        uint32_t moduleIndex{0};
        QString pipePath;
    };
    std::array<RxPipe, NUM_CHANNELS> m_rx;

    // TX: PulseAudio writes to this pipe → we read from it
    struct TxPipe {
        int fd{-1};
        uint32_t moduleIndex{0};
        QString pipePath;
    };
    TxPipe m_tx;

    QTimer* m_txReadTimer{nullptr};
    void readTxPipe();

    // Silence fill during TX — keeps pipe-source clock advancing so
    // WSJT-X / VARA don't see a stalled audio source.
    QTimer*       m_silenceTimer{nullptr};
    QElapsedTimer m_silenceElapsed;
    void feedSilenceToAllPipes();

    bool m_open{false};
    float m_gain{0.5f};
    float m_channelGain[NUM_CHANNELS]{0.5f, 0.5f, 0.5f, 0.5f};
    float m_txGain{0.5f};
    bool m_transmitting{false};
};

} // namespace AetherSDR
