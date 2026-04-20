#include "ClientPuduMonitor.h"

#include <QDir>
#include <QFile>
#include <QIODevice>

#include <algorithm>
#include <cstring>

namespace AetherSDR {

namespace {

// 10 ms playback cadence — 240 frames × 2 ch × 2 B = 960 B per chunk.
// Match QsoRecorder exactly: longer intervals starve the audio sink
// when Qt's timer scheduler coarsens (typical real intervals of
// 16-33 ms under light load), producing dropouts / garbled output.
constexpr int kPlaybackIntervalMs = 10;
constexpr int kPlaybackChunkBytes =
    kPlaybackIntervalMs * ClientPuduMonitor::kSampleRate / 1000
    * ClientPuduMonitor::kBytesPerFrame;

// Standard 44-byte RIFF/WAVE header for int16 stereo 24 kHz PCM.
QByteArray makeWavHeader(quint32 payloadBytes)
{
    QByteArray h;
    h.reserve(44);
    auto u32 = [&](quint32 v) {
        h.append(char(v & 0xFF));
        h.append(char((v >> 8) & 0xFF));
        h.append(char((v >> 16) & 0xFF));
        h.append(char((v >> 24) & 0xFF));
    };
    auto u16 = [&](quint16 v) {
        h.append(char(v & 0xFF));
        h.append(char((v >> 8) & 0xFF));
    };

    h.append("RIFF", 4);
    u32(36 + payloadBytes);
    h.append("WAVE", 4);
    h.append("fmt ", 4);
    u32(16);                                                  // fmt chunk size
    u16(1);                                                    // PCM
    u16(ClientPuduMonitor::kChannels);
    u32(ClientPuduMonitor::kSampleRate);
    u32(ClientPuduMonitor::kSampleRate
        * ClientPuduMonitor::kBytesPerFrame);                  // byte rate
    u16(ClientPuduMonitor::kBytesPerFrame);                    // block align
    u16(16);                                                   // bits / sample
    h.append("data", 4);
    u32(payloadBytes);
    return h;
}

} // namespace

ClientPuduMonitor::ClientPuduMonitor(QObject* parent) : QObject(parent)
{
    // Pre-size the buffer so the audio thread never reallocates.  Qt's
    // QByteArray grows on demand; fill with zeros to force the
    // allocation now rather than on first write.
    m_buffer.fill('\0', kMaxBytes);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(kPlaybackIntervalMs);
    m_playTimer->setTimerType(Qt::PreciseTimer);
    connect(m_playTimer, &QTimer::timeout,
            this, &ClientPuduMonitor::onPlaybackTick);
}

int ClientPuduMonitor::recordedMs() const noexcept
{
    return m_recordedMs;
}

void ClientPuduMonitor::startRecording()
{
    // Idempotent.  Can't record while playing — caller should stop
    // playback first; we just bail to avoid trampling playback state.
    if (m_recording.load(std::memory_order_acquire)) return;
    if (m_playing) return;

    m_writeBytes.store(0, std::memory_order_release);
    m_recordedBytes = 0;
    m_recordedMs = 0;
    m_recElapsed.restart();
    m_recording.store(true, std::memory_order_release);
    // Disconnect live RX audio for the entire record+playback cycle
    // so we don't hear radio audio mixed with our capture/playback.
    // Restored on playback end (not on record end — auto-play starts
    // immediately after, and we want the mute to persist across the
    // transition).
    emit muteRxRequested(true);
    emit recordingStarted();
}

void ClientPuduMonitor::stopRecording()
{
    // Try to flip the recording flag from true→false atomically.
    // Whoever wins (user click, auto-stop handler, or external
    // transition like mic-source change) finishes the stop work;
    // later callers no-op.
    bool wasRecording = true;
    if (!m_recording.compare_exchange_strong(wasRecording, false,
            std::memory_order_acq_rel)) {
        return;
    }

    // Audio thread sees m_recording=false before any further writes
    // via the acquire load in feedTxPostDsp().  Safe to read the
    // write count.
    m_recordedBytes = m_writeBytes.load(std::memory_order_acquire);
    m_recordedMs    = static_cast<int>(m_recElapsed.elapsed());

    writeWavFile();
    emit recordingStopped(m_recordedMs);
}

void ClientPuduMonitor::onAutoStop()
{
    // Audio thread hit the 30-s cap and flipped m_recording=false
    // itself.  Do the UI-side finalisation + emit signal.
    if (m_recordedBytes == 0) {
        m_recordedBytes = m_writeBytes.load(std::memory_order_acquire);
        m_recordedMs    = static_cast<int>(m_recElapsed.elapsed());
    }
    writeWavFile();
    emit recordingStopped(m_recordedMs);
}

void ClientPuduMonitor::startPlayback()
{
    if (m_playing) return;
    if (m_recordedBytes <= 0) return;
    m_playPos = 0;
    m_playing = true;
    m_playTimer->start();
    // When playback is triggered as part of auto-play after a
    // record (the record → stop → play transition), muteRxRequested
    // was already fired at record start and the mute is still held,
    // so this is a no-op.  When the user triggers playback manually
    // from idle, this is the one that installs the mute.  Either
    // way it's safe — MainWindow's handler is idempotent.
    emit muteRxRequested(true);
    emit playbackStarted();
}

void ClientPuduMonitor::stopPlayback()
{
    if (!m_playing) return;
    m_playing = false;
    m_playTimer->stop();
    // End of the record+playback cycle — restore live RX audio.
    emit muteRxRequested(false);
    emit playbackStopped();
}

void ClientPuduMonitor::onPlaybackTick()
{
    if (!m_playing) return;
    const int remaining = m_recordedBytes - m_playPos;
    if (remaining <= 0) {
        stopPlayback();
        return;
    }
    const int take = std::min(remaining, kPlaybackChunkBytes);

    // AudioEngine::feedDecodedSpeech() expects float32 interleaved
    // stereo — not int16 (the sink is configured for float samples).
    // QsoRecorder::onPlaybackTick does the same conversion.  Emitting
    // raw int16 bytes would get reinterpreted as float32 by the sink
    // and blow out speakers with garbage values.
    const auto* src = reinterpret_cast<const int16_t*>(
        m_buffer.constData() + m_playPos);
    const int numSamples = take / static_cast<int>(sizeof(int16_t));
    QByteArray floatPcm(numSamples * static_cast<int>(sizeof(float)),
                        Qt::Uninitialized);
    auto* dst = reinterpret_cast<float*>(floatPcm.data());
    for (int i = 0; i < numSamples; ++i) {
        dst[i] = static_cast<float>(src[i]) / 32768.0f;
    }
    emit playbackAudio(floatPcm);

    m_playPos += take;
    if (m_playPos >= m_recordedBytes) {
        stopPlayback();
    }
}

void ClientPuduMonitor::feedTxPostDsp(const QByteArray& pcm) noexcept
{
    // Fast-path skip when not recording — acquire load so writes that
    // happened before we started recording are visible too.
    if (!m_recording.load(std::memory_order_acquire)) return;
    if (pcm.isEmpty()) return;

    const int writeAt = m_writeBytes.load(std::memory_order_relaxed);
    const int avail   = kMaxBytes - writeAt;
    if (avail <= 0) return;
    const int take = std::min(static_cast<int>(pcm.size()), avail);
    std::memcpy(m_buffer.data() + writeAt, pcm.constData(), take);
    const int newTotal = writeAt + take;
    m_writeBytes.store(newTotal, std::memory_order_release);

    if (newTotal >= kMaxBytes) {
        // Cap hit — stop accepting further feeds and tell the UI
        // thread to finalise + auto-start playback.
        m_recording.store(false, std::memory_order_release);
        QMetaObject::invokeMethod(this, "onAutoStop", Qt::QueuedConnection);
    }
}

void ClientPuduMonitor::writeWavFile()
{
    // /tmp/pudu_monitor.wav — overwritten on each recording.  Purely
    // for offline inspection; playback uses the in-memory buffer.
    const QString path = QDir::temp().filePath("pudu_monitor.wav");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    const QByteArray header = makeWavHeader(
        static_cast<quint32>(m_recordedBytes));
    f.write(header);
    f.write(m_buffer.constData(), m_recordedBytes);
    f.close();
}

} // namespace AetherSDR
