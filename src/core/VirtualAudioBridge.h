#pragma once

#include <QObject>
#include <QByteArray>
#include <atomic>
#include <algorithm>
#include <cstdint>

class QTimer;

namespace AetherSDR {

// Shared memory layout for one DAX audio channel.
// Used by both AetherSDR (writer) and the HAL plugin (reader).
// Sample rate matches FLEX-8600 fw v1.4.0.0 native DAX rate: 24 kHz stereo.
struct DaxShmBlock {
    std::atomic<uint32_t> writePos{0};
    std::atomic<uint32_t> readPos{0};
    uint32_t sampleRate{24000};
    uint32_t channels{2};           // stereo
    uint32_t active{0};             // 1 = AetherSDR is feeding data
    uint32_t reserved[3]{};
    // Ring buffer: 2 seconds @ 24kHz stereo = 96000 float samples
    static constexpr uint32_t RING_SIZE = 24000 * 2 * 2;
    float ringBuffer[RING_SIZE]{};
};

// Bridge between AetherSDR and the HAL plugin via POSIX shared memory.
// Creates 4 RX shared memory segments (/aethersdr-dax-1 through /aethersdr-dax-4)
// for DAX audio from radio to apps, plus 1 TX segment (/aethersdr-dax-tx)
// for audio from apps to radio.
class VirtualAudioBridge : public QObject {
    Q_OBJECT

public:
    static constexpr int NUM_CHANNELS = 4;

    explicit VirtualAudioBridge(QObject* parent = nullptr);
    ~VirtualAudioBridge() override;

    bool open();
    void close();
    bool isOpen() const { return m_open; }

    // DAX output gain (0.0–1.0). Default 0.25 (≈ -12 dB) to avoid
    // overloading digital-mode software like WSJT-X.
    void setGain(float g) { m_gain = std::clamp(g, 0.0f, 1.0f); }
    float gain() const { return m_gain; }

    // Read TX audio from shared memory (apps → radio).
    // Returns float32 stereo PCM, or empty if no data available.
    QByteArray readTxAudio(int maxFrames = 480);

public slots:
    // Feed decoded DAX audio for a channel (1-4).
    // pcm format: int16 stereo, 24 kHz, little-endian.
    void feedDaxAudio(int channel, const QByteArray& pcm);

signals:
    void txAudioReady(const QByteArray& pcm);

private:
    bool m_open{false};
    float m_gain{0.5f};  // -6 dB default

    // RX channels (radio → apps)
    int  m_shmFds[NUM_CHANNELS]{-1, -1, -1, -1};
    DaxShmBlock* m_blocks[NUM_CHANNELS]{};

    // TX channel (apps → radio)
    int m_txShmFd{-1};
    DaxShmBlock* m_txBlock{nullptr};
    ::QTimer* m_txPollTimer{nullptr};

    static QString shmName(int channel);
};

} // namespace AetherSDR
