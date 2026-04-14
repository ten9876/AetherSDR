#include "VirtualAudioBridge.h"
#include "LogManager.h"

#include <QTimer>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cmath>
#include <algorithm>

namespace AetherSDR {

VirtualAudioBridge::VirtualAudioBridge(QObject* parent)
    : QObject(parent)
{}

VirtualAudioBridge::~VirtualAudioBridge()
{
    close();
}

QString VirtualAudioBridge::shmName(int channel)
{
    return QStringLiteral("/aethersdr-dax-%1").arg(channel);
}

static bool openShmSegment(const char* name, int& fd, DaxShmBlock*& block)
{
    // Try to open existing segment first (HAL plugin may already have it mapped).
    fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) {
        // Does not exist yet — create it.
        fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            qCWarning(lcDax) << "VirtualAudioBridge: shm_open failed for" << name;
            return false;
        }
        if (ftruncate(fd, sizeof(DaxShmBlock)) != 0) {
            qCWarning(lcDax) << "VirtualAudioBridge: ftruncate failed for" << name;
            ::close(fd);
            fd = -1;
            return false;
        }
    }
    // else: segment already exists at the right size — reuse it so the
    // HAL plugin's existing mmap stays valid.

    void* ptr = mmap(nullptr, sizeof(DaxShmBlock), PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        qCWarning(lcDax) << "VirtualAudioBridge: mmap failed for" << name;
        ::close(fd);
        fd = -1;
        return false;
    }

    // Re-initialize the header fields (resets write/read positions).
    auto* b = static_cast<DaxShmBlock*>(ptr);
    b->writePos.store(0, std::memory_order_relaxed);
    b->readPos.store(0, std::memory_order_relaxed);
    b->sampleRate = 24000;
    b->channels = 2;
    block = b;
    return true;
}

bool VirtualAudioBridge::open()
{
    if (m_open) return true;

    // Open 4 RX shared memory segments
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        int ch = i + 1;
        QByteArray name = shmName(ch).toUtf8();

        if (!openShmSegment(name.constData(), m_shmFds[i], m_blocks[i])) {
            close();
            return false;
        }
        m_blocks[i]->active = 1;
    }

    // Open TX shared memory segment
    if (!openShmSegment("/aethersdr-dax-tx", m_txShmFd, m_txBlock)) {
        close();
        return false;
    }
    m_txBlock->active = 0;  // HAL plugin sets this to 1 when apps write

    // Poll TX shared memory for incoming audio.
    // Use a short interval and fixed-size reads so TX stays close to real-time.
    m_txPollTimer = new QTimer(this);
    m_txPollTimer->setInterval(2);
    connect(m_txPollTimer, &QTimer::timeout, this, [this]() {
        static int pollNum = 0;
        ++pollNum;

        // Diagnostic: log shm state every second regardless of data
        if (m_txBlock && pollNum % 500 == 0) {
            uint32_t wp = m_txBlock->writePos.load(std::memory_order_relaxed);
            uint32_t rp = m_txBlock->readPos.load(std::memory_order_relaxed);
            qCDebug(lcDax) << "TX shm poll#" << pollNum
                     << "wp=" << wp << "rp=" << rp
                     << "avail=" << (wp - rp) << "active=" << m_txBlock->active;
        }

        // Drain multiple small chunks each tick to avoid stale backlog.
        constexpr int FRAMES_PER_READ = 128;   // ~5.3ms @ 24kHz
        constexpr int MAX_CHUNKS_PER_TICK = 8; // keep UI responsive under load
        for (int i = 0; i < MAX_CHUNKS_PER_TICK; ++i) {
            QByteArray audio = readTxAudio(FRAMES_PER_READ);
            if (audio.isEmpty()) break;

            static int txPollCount = 0;
            ++txPollCount;
            if (txPollCount <= 10 || txPollCount % 200 == 0)
                qCDebug(lcDax) << "VirtualAudioBridge: TX audio from shm, bytes=" << audio.size()
                         << "(chunk #" << txPollCount << ")"
                         << "wp=" << m_txBlock->writePos.load(std::memory_order_relaxed)
                         << "active=" << m_txBlock->active;
            emit txAudioReady(audio);
        }
    });
    m_txPollTimer->start();

    m_open = true;
    qCInfo(lcDax) << "VirtualAudioBridge: opened 4 RX + 1 TX shared memory segments";
    return true;
}

void VirtualAudioBridge::close()
{
    if (m_silenceTimer) {
        m_silenceTimer->stop();
        delete m_silenceTimer;
        m_silenceTimer = nullptr;
    }
    m_transmitting = false;

    if (m_txPollTimer) {
        m_txPollTimer->stop();
        delete m_txPollTimer;
        m_txPollTimer = nullptr;
    }

    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (m_blocks[i]) {
            m_blocks[i]->active = 0;
            munmap(m_blocks[i], sizeof(DaxShmBlock));
            m_blocks[i] = nullptr;
        }
        if (m_shmFds[i] >= 0) {
            ::close(m_shmFds[i]);
            m_shmFds[i] = -1;
        }
    }

    if (m_txBlock) {
        munmap(m_txBlock, sizeof(DaxShmBlock));
        m_txBlock = nullptr;
    }
    if (m_txShmFd >= 0) {
        ::close(m_txShmFd);
        m_txShmFd = -1;
    }

    m_open = false;
}

void VirtualAudioBridge::setTransmitting(bool tx)
{
    m_transmitting = tx;

    if (tx) {
        // Start a timer that feeds silence into all RX shared memory channels
        // at ~20 ms intervals (~480 stereo samples per tick @ 24 kHz).
        // This keeps the HAL plugin's ring buffer advancing so CoreAudio and
        // WSJT-X/VARA don't see a stalled audio source.
        if (!m_silenceTimer) {
            m_silenceTimer = new QTimer(this);
            m_silenceTimer->setInterval(20);
            connect(m_silenceTimer, &QTimer::timeout,
                    this, &VirtualAudioBridge::feedSilenceToAllChannels);
        }
        m_silenceElapsed.start();
        m_silenceTimer->start();
    }
    // NOTE: we do NOT stop the silence timer on TX→RX here.
    // The radio hasn't resumed DAX RX audio yet at this point — the
    // interlock is still transitioning (UNKEY_REQUESTED → READY).
    // The timer is stopped in feedDaxAudio() when real audio arrives.
}

void VirtualAudioBridge::feedSilenceToAllChannels()
{
    if (!m_open) return;

    // Compute the exact number of stereo samples needed based on elapsed
    // wall-clock time since the last tick.  This eliminates cumulative drift
    // caused by QTimer jitter (the old code wrote a fixed 960 samples per
    // 20 ms tick, but late ticks produced fewer samples than real-time).
    const qint64 elapsedNs = m_silenceElapsed.nsecsElapsed();
    m_silenceElapsed.start();

    // stereo samples = elapsed_s * sampleRate * 2 channels
    const int silenceSamples = static_cast<int>(
        elapsedNs * 24000LL * 2 / 1000000000LL);
    if (silenceSamples <= 0) return;

    for (int i = 0; i < NUM_CHANNELS; ++i) {
        auto* block = m_blocks[i];
        if (!block || !block->active) continue;
        uint32_t wp = block->writePos.load(std::memory_order_relaxed);
        for (int s = 0; s < silenceSamples; ++s) {
            block->ringBuffer[wp % DaxShmBlock::RING_SIZE] = 0.0f;
            ++wp;
        }
        block->writePos.store(wp, std::memory_order_release);
    }
}

void VirtualAudioBridge::feedDaxAudio(int channel, const QByteArray& pcm)
{
    if (channel < 1 || channel > NUM_CHANNELS) return;

    auto* block = m_blocks[channel - 1];
    if (!block) return;

    // Real DAX audio has arrived from the radio — stop the silence fill timer.
    // This bridges the gap between "we asked radio to stop TX" and "radio
    // actually resumed sending RX audio".
    if (m_silenceTimer && m_silenceTimer->isActive()) {
        m_silenceTimer->stop();
        m_transmitting = false;

        // Skip all buffered silence so the modem hears real audio immediately.
        // During TX, the silence timer accumulated samples in the ring buffer.
        // Without this reset, the HAL plugin would read through all that silence
        // before reaching real audio — causing a perceivable delay after TX.
        for (int i = 0; i < NUM_CHANNELS; ++i) {
            if (m_blocks[i] && m_blocks[i]->active) {
                uint32_t wp = m_blocks[i]->writePos.load(std::memory_order_relaxed);
                m_blocks[i]->readPos.store(wp, std::memory_order_release);
            }
        }
    }

    // Input: float32 stereo PCM @ 24 kHz from the radio (native DAX rate).
    //        After the float32 migration (502b934, b0c49d7), both PCC_IF_NARROW
    //        and PCC_IF_NARROW_REDUCED emit float32 stereo from PanadapterStream.
    // Output: float32 stereo @ 24 kHz into the ring buffer — direct 1:1 copy.
    // HAL plugin also runs at 24 kHz, so no resampling needed.
    const auto* samples = reinterpret_cast<const float*>(pcm.constData());
    const int numSamples = pcm.size() / static_cast<int>(sizeof(float));  // total float count (L,R,L,R,...)

    const float chGain = m_channelGain[channel - 1];
    uint32_t wp = block->writePos.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i) {
        block->ringBuffer[wp % DaxShmBlock::RING_SIZE] = samples[i] * chGain;
        ++wp;
    }

    block->writePos.store(wp, std::memory_order_release);

    // RX level meter (every ~100ms)
    static int meterCount[NUM_CHANNELS]{};
    if (++meterCount[channel - 1] % 10 == 0) {
        float sum = 0;
        for (int i = 0; i < numSamples; i += 2)
            sum += samples[i] * samples[i];
        float rms = std::sqrt(sum / std::max(1, numSamples / 2));
        emit daxRxLevel(channel, rms);
    }
}

QByteArray VirtualAudioBridge::readTxAudio(int maxFrames)
{
    if (!m_txBlock || !m_txBlock->active) return {};

    uint32_t rp = m_txBlock->readPos.load(std::memory_order_acquire);
    uint32_t wp = m_txBlock->writePos.load(std::memory_order_acquire);

    uint32_t available = wp - rp;
    if (available == 0) return {};

    // If writer has lapped the reader, skip ahead to avoid stale data.
    if (available > DaxShmBlock::RING_SIZE) {
        rp = wp - DaxShmBlock::RING_SIZE / 2;  // jump to recent data
        available = wp - rp;
    }

    // Low-latency guard: if backlog grows, skip stale audio and keep only
    // a small recent window near the writer head.
    constexpr uint32_t TARGET_BACKLOG_SAMPLES = 256 * 2; // 256 frames ≈ 10.7ms
    constexpr uint32_t MAX_BACKLOG_SAMPLES = 768 * 2;    // 768 frames ≈ 32ms
    if (available > MAX_BACKLOG_SAMPLES) {
        rp = wp - TARGET_BACKLOG_SAMPLES;
        available = wp - rp;
    }

    uint32_t totalSamples = available;

    if (maxFrames > 0)
        totalSamples = std::min(totalSamples, static_cast<uint32_t>(maxFrames * 2));

    QByteArray result(static_cast<int>(totalSamples * sizeof(float)), Qt::Uninitialized);
    auto* dst = reinterpret_cast<float*>(result.data());

    for (uint32_t i = 0; i < totalSamples; ++i) {
        dst[i] = m_txBlock->ringBuffer[rp % DaxShmBlock::RING_SIZE];
        ++rp;
    }

    m_txBlock->readPos.store(rp, std::memory_order_release);

    // Apply TX gain
    if (m_txGain != 1.0f) {
        for (uint32_t i = 0; i < totalSamples; ++i)
            dst[i] *= m_txGain;
    }

    // TX level meter
    static int txMeterCount = 0;
    if (++txMeterCount % 10 == 0 && totalSamples > 0) {
        float sum = 0;
        for (uint32_t i = 0; i < totalSamples; i += 2)
            sum += dst[i] * dst[i];
        emit daxTxLevel(std::sqrt(sum / std::max(1u, totalSamples / 2)));
    }

    return result;
}

} // namespace AetherSDR
