#include "PipeWireAudioBridge.h"
#include "LogManager.h"

#include <QTimer>
#include <QProcess>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace AetherSDR {

PipeWireAudioBridge::PipeWireAudioBridge(QObject* parent)
    : QObject(parent)
{}

PipeWireAudioBridge::~PipeWireAudioBridge()
{
    close();
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

// Unload any stale aethersdr pipe modules from a previous crashed session
static void cleanupStaleModules()
{
    QProcess proc;
    proc.start("pactl", {"list", "modules", "short"});
    if (!proc.waitForFinished(3000)) return;

    for (const auto& line : proc.readAllStandardOutput().split('\n')) {
        if (line.contains("aethersdr-")) {
            auto parts = line.split('\t');
            if (parts.size() >= 1) {
                QProcess::execute("pactl", {"unload-module", parts[0].trimmed()});
            }
        }
    }
}

bool PipeWireAudioBridge::open()
{
    if (m_open) return true;

    cleanupStaleModules();

    // Create 4 RX pipe sources (radio → apps)
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (!loadPipeSource(i)) {
            qCWarning(lcDax) << "PipeWireAudioBridge: failed to create RX pipe" << (i + 1);
            close();
            return false;
        }
    }

    // Create TX pipe sink (apps → radio)
    if (!loadPipeSink()) {
        qCWarning(lcDax) << "PipeWireAudioBridge: failed to create TX pipe";
        close();
        return false;
    }

    // Poll TX pipe for incoming audio from apps
    m_txReadTimer = new QTimer(this);
    m_txReadTimer->setInterval(5);
    m_txReadTimer->setTimerType(Qt::PreciseTimer);
    connect(m_txReadTimer, &QTimer::timeout, this, &PipeWireAudioBridge::readTxPipe);
    m_txReadTimer->start();

    m_open = true;
    qCInfo(lcDax) << "PipeWireAudioBridge: opened — 4 RX sources + 1 TX sink";
    return true;
}

void PipeWireAudioBridge::close()
{
    if (m_silenceTimer) {
        m_silenceTimer->stop();
        delete m_silenceTimer;
        m_silenceTimer = nullptr;
    }
    m_transmitting = false;

    if (m_txReadTimer) {
        m_txReadTimer->stop();
        delete m_txReadTimer;
        m_txReadTimer = nullptr;
    }

    // Close pipe file descriptors
    for (auto& rx : m_rx) {
        if (rx.fd >= 0) { ::close(rx.fd); rx.fd = -1; }
    }
    if (m_tx.fd >= 0) { ::close(m_tx.fd); m_tx.fd = -1; }

    unloadModules();

    // Remove pipe files
    for (auto& rx : m_rx) {
        if (!rx.pipePath.isEmpty()) {
            ::unlink(rx.pipePath.toUtf8().constData());
            rx.pipePath.clear();
        }
    }
    if (!m_tx.pipePath.isEmpty()) {
        ::unlink(m_tx.pipePath.toUtf8().constData());
        m_tx.pipePath.clear();
    }

    m_open = false;
    qCInfo(lcDax) << "PipeWireAudioBridge: closed";
}

// ── Module loading via pactl ─────────────────────────────────────────────────

static uint32_t runPactl(const QStringList& args)
{
    QProcess proc;
    proc.start("pactl", args);
    if (!proc.waitForFinished(5000)) {
        qCWarning(lcDax) << "PipeWireAudioBridge: pactl timed out:" << args;
        return 0;
    }
    if (proc.exitCode() != 0) {
        qCWarning(lcDax) << "PipeWireAudioBridge: pactl failed:" << proc.readAllStandardError().trimmed();
        return 0;
    }
    // pactl load-module returns the module index
    bool ok = false;
    uint32_t idx = proc.readAllStandardOutput().trimmed().toUInt(&ok);
    return ok ? idx : 0;
}

bool PipeWireAudioBridge::loadPipeSource(int index)
{
    auto pipePath = QStringLiteral("/tmp/aethersdr-dax-%1.pipe").arg(index + 1);
    auto sourceName = QStringLiteral("aethersdr-dax-%1").arg(index + 1);
    auto sourceDesc = QStringLiteral("AetherSDR DAX %1").arg(index + 1);

    // Create the named pipe (FIFO)
    ::unlink(pipePath.toUtf8().constData());
    if (::mkfifo(pipePath.toUtf8().constData(), 0666) != 0) {
        qCWarning(lcDax) << "PipeWireAudioBridge: mkfifo failed:" << strerror(errno);
        return false;
    }

    // Load PulseAudio pipe-source module
    uint32_t modIdx = runPactl({
        "load-module", "module-pipe-source",
        QStringLiteral("file=%1").arg(pipePath),
        QStringLiteral("source_name=%1").arg(sourceName),
        QStringLiteral("source_properties=device.description=\"%1\"").arg(sourceDesc),
        QStringLiteral("format=s16le"),
        QStringLiteral("rate=%1").arg(SAMPLE_RATE),
        QStringLiteral("channels=%1").arg(CHANNELS),
    });

    if (modIdx == 0) {
        ::unlink(pipePath.toUtf8().constData());
        return false;
    }

    // Open the pipe for writing (non-blocking to avoid hanging if no reader)
    int fd = ::open(pipePath.toUtf8().constData(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        qCWarning(lcDax) << "PipeWireAudioBridge: open pipe failed:" << strerror(errno);
        runPactl({"unload-module", QString::number(modIdx)});
        ::unlink(pipePath.toUtf8().constData());
        return false;
    }

    m_rx[index].fd = fd;
    m_rx[index].moduleIndex = modIdx;
    m_rx[index].pipePath = pipePath;

    qCDebug(lcDax) << "PipeWireAudioBridge: RX" << (index + 1) << "pipe source loaded, module" << modIdx;
    return true;
}

bool PipeWireAudioBridge::loadPipeSink()
{
    auto pipePath = QStringLiteral("/tmp/aethersdr-tx.pipe");
    auto sinkName = QStringLiteral("aethersdr-tx");
    auto sinkDesc = QStringLiteral("AetherSDR TX");

    ::unlink(pipePath.toUtf8().constData());
    if (::mkfifo(pipePath.toUtf8().constData(), 0666) != 0) {
        qCWarning(lcDax) << "PipeWireAudioBridge: mkfifo failed:" << strerror(errno);
        return false;
    }

    // Use a small pipe buffer (2048 bytes ~ 42ms at 24kHz mono s16le)
    // to keep latency low for digital modes like FT8/FT4.
    uint32_t modIdx = runPactl({
        "load-module", "module-pipe-sink",
        QStringLiteral("file=%1").arg(pipePath),
        QStringLiteral("sink_name=%1").arg(sinkName),
        QStringLiteral("sink_properties=device.description=\"%1\"").arg(sinkDesc),
        QStringLiteral("format=s16le"),
        QStringLiteral("rate=%1").arg(SAMPLE_RATE),
        QStringLiteral("channels=%1").arg(CHANNELS),
        QStringLiteral("pipe_size=2048"),
    });

    if (modIdx == 0) {
        ::unlink(pipePath.toUtf8().constData());
        return false;
    }

    // Open the pipe for reading (non-blocking)
    int fd = ::open(pipePath.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        qCWarning(lcDax) << "PipeWireAudioBridge: open TX pipe failed:" << strerror(errno);
        runPactl({"unload-module", QString::number(modIdx)});
        ::unlink(pipePath.toUtf8().constData());
        return false;
    }

    m_tx.fd = fd;
    m_tx.moduleIndex = modIdx;
    m_tx.pipePath = pipePath;

    qCDebug(lcDax) << "PipeWireAudioBridge: TX pipe sink loaded, module" << modIdx;
    return true;
}

void PipeWireAudioBridge::unloadModules()
{
    for (auto& rx : m_rx) {
        if (rx.moduleIndex > 0) {
            runPactl({"unload-module", QString::number(rx.moduleIndex)});
            rx.moduleIndex = 0;
        }
    }
    if (m_tx.moduleIndex > 0) {
        runPactl({"unload-module", QString::number(m_tx.moduleIndex)});
        m_tx.moduleIndex = 0;
    }
}

// ── Audio I/O ────────────────────────────────────────────────────────────────

void PipeWireAudioBridge::setGain(float g)
{
    m_gain = std::clamp(g, 0.0f, 1.0f);
    for (int i = 0; i < NUM_CHANNELS; ++i)
        m_channelGain[i] = m_gain;
}

void PipeWireAudioBridge::setChannelGain(int channel, float g)
{
    if (channel >= 1 && channel <= NUM_CHANNELS)
        m_channelGain[channel - 1] = std::clamp(g, 0.0f, 1.0f);
}

void PipeWireAudioBridge::setTxGain(float g)
{
    m_txGain = std::clamp(g, 0.0f, 1.0f);
}

void PipeWireAudioBridge::feedDaxAudio(int channel, const QByteArray& pcm)
{
    if (!m_open) return;
    if (channel < 1 || channel > NUM_CHANNELS) return;

    // Real DAX audio has arrived — stop the silence fill timer if running.
    if (m_transmitting && m_silenceTimer && m_silenceTimer->isActive()) {
        m_silenceTimer->stop();
        m_transmitting = false;
    }

    auto& rx = m_rx[channel - 1];
    if (rx.fd < 0) return;

    // Input is float32 stereo — convert to mono int16 (average L+R) with gain
    const auto* src = reinterpret_cast<const float*>(pcm.constData());
    int stereoFloats = pcm.size() / static_cast<int>(sizeof(float));
    int monoSamples = stereoFloats / 2;
    float chGain = m_channelGain[channel - 1];

    QByteArray mono(monoSamples * static_cast<int>(sizeof(int16_t)), Qt::Uninitialized);
    auto* dst = reinterpret_cast<int16_t*>(mono.data());
    for (int i = 0; i < monoSamples; ++i) {
        float avg = (src[i * 2] + src[i * 2 + 1]) * 0.5f * chGain;
        dst[i] = static_cast<int16_t>(std::clamp(avg * 32768.0f, -32768.0f, 32767.0f));
    }
    ::write(rx.fd, mono.constData(), mono.size());

    // Calculate RMS for meter display (every ~100ms = ~10 calls at 24kHz)
    static int meterCount[NUM_CHANNELS]{};
    if (++meterCount[channel - 1] % 10 == 0) {
        float sum = 0;
        for (int i = 0; i < monoSamples; ++i)
            sum += (dst[i] / 32768.0f) * (dst[i] / 32768.0f);
        float rms = std::sqrt(sum / std::max(1, monoSamples));
        emit daxRxLevel(channel, rms);
    }
}

void PipeWireAudioBridge::setTransmitting(bool tx)
{
    m_transmitting = tx;

    if (tx) {
        // Start a timer that feeds silence into all RX pipes so the
        // module-pipe-source clock keeps advancing during TX.  Without this,
        // the pipe underruns and WSJT-X sees a timing gap equal to the TX
        // duration, causing cumulative DT drift.
        if (!m_silenceTimer) {
            m_silenceTimer = new QTimer(this);
            m_silenceTimer->setInterval(20);
            m_silenceTimer->setTimerType(Qt::PreciseTimer);
            connect(m_silenceTimer, &QTimer::timeout,
                    this, &PipeWireAudioBridge::feedSilenceToAllPipes);
        }
        m_silenceElapsed.start();
        m_silenceTimer->start();
    }
    // NOTE: silence timer is stopped in feedDaxAudio() when real RX audio
    // arrives, not here — the radio hasn't resumed DAX RX audio yet at
    // this point (interlock is still transitioning).
}

void PipeWireAudioBridge::feedSilenceToAllPipes()
{
    if (!m_open) return;

    // Compute the exact number of mono int16 silence samples based on
    // elapsed wall-clock time.  This avoids cumulative drift from QTimer
    // jitter (same approach as the macOS VirtualAudioBridge fix).
    const qint64 elapsedNs = m_silenceElapsed.nsecsElapsed();
    m_silenceElapsed.start();

    // mono samples = elapsed_s * sampleRate
    const int monoSamples = static_cast<int>(
        elapsedNs * SAMPLE_RATE / 1000000000LL);
    if (monoSamples <= 0) return;

    // Write silence (zero int16 samples) to each active RX pipe.
    QByteArray silence(monoSamples * static_cast<int>(sizeof(int16_t)), '\0');
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (m_rx[i].fd >= 0) {
            ::write(m_rx[i].fd, silence.constData(), silence.size());
        }
    }
}

void PipeWireAudioBridge::readTxPipe()
{
    if (m_tx.fd < 0) return;

    // Drain all available data from the TX pipe (int16 mono from apps).
    // Reading in a loop avoids bufferbloat when the timer fires late.
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(m_tx.fd, buf, sizeof(buf));
        if (n <= 0) break;

        // Convert int16 mono → float32 stereo with TX gain
        int monoSamples = n / sizeof(int16_t);
        const auto* src = reinterpret_cast<const int16_t*>(buf);
        QByteArray out(monoSamples * 2 * sizeof(float), Qt::Uninitialized);  // stereo
        auto* dst = reinterpret_cast<float*>(out.data());
        for (int i = 0; i < monoSamples; ++i) {
            float v = (src[i] / 32768.0f) * m_txGain;
            dst[i * 2]     = v;  // left
            dst[i * 2 + 1] = v;  // right (duplicate)
        }

        emit txAudioReady(out);

        // TX level meter (every ~100ms)
        static int txMeterCount = 0;
        if (++txMeterCount % 10 == 0) {
            float sum = 0;
            for (int i = 0; i < monoSamples; ++i)
                sum += (src[i] / 32768.0f) * (src[i] / 32768.0f);
            emit daxTxLevel(std::sqrt(sum / std::max(1, monoSamples)));
        }
    }
}

} // namespace AetherSDR
