#include "AudioEngine.h"
#include "AppSettings.h"
#include "LogManager.h"
#include "OpusCodec.h"
#include "SpectralNR.h"
#ifdef HAVE_SPECBLEACH
#include "SpecbleachFilter.h"
#endif
#include "RNNoiseFilter.h"
#include "NvidiaBnrFilter.h"
#ifdef HAVE_DFNR
#include "DeepFilterFilter.h"
#endif
#include "Resampler.h"

#include <cmath>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDir>
#include <QtEndian>
#include <QThread>
#include <algorithm>
#include <cstring>

namespace AetherSDR {

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
    // Restore saved audio device selections
    auto& s = AppSettings::instance();
    QByteArray savedOutId = s.value("AudioOutputDeviceId", "").toByteArray();
    QByteArray savedInId  = s.value("AudioInputDeviceId",  "").toByteArray();

    if (!savedOutId.isEmpty()) {
        for (const auto& dev : QMediaDevices::audioOutputs()) {
            if (dev.id() == savedOutId) { m_outputDevice = dev; break; }
        }
    }
    if (!savedInId.isEmpty()) {
        for (const auto& dev : QMediaDevices::audioInputs()) {
            if (dev.id() == savedInId) { m_inputDevice = dev; break; }
        }
    }

    // Opus TX pacing timer — sends one queued packet every 10ms for even
    // delivery timing. Without this, QAudioSource delivers bursts of samples
    // that get Opus-encoded and sent back-to-back, causing jitter-induced
    // crackling on SmartLink/WAN connections.
    m_opusTxPaceTimer = new QTimer(this);
    m_opusTxPaceTimer->setTimerType(Qt::PreciseTimer);
    m_opusTxPaceTimer->setInterval(10);
    connect(m_opusTxPaceTimer, &QTimer::timeout, this, [this]() {
        if (m_opusTxQueue.isEmpty()) return;
        emit txPacketReady(m_opusTxQueue.takeFirst());
    });
    m_opusTxPaceTimer->start();

    // RX pacing timer -- drains m_rxBuffer into QAudioSink at regular intervals.
    // Includes latency management: caps buffer at ~200ms to prevent unbounded
    // growth when network packets arrive in bursts (common on Windows WASAPI
    // with virtual audio routers like Voicemeeter).
    m_rxTimer = new QTimer(this);
    m_rxTimer->setTimerType(Qt::PreciseTimer);
    m_rxTimer->setInterval(10);
    connect(m_rxTimer, &QTimer::timeout, this, [this]() {
        if (!m_audioSink || !m_audioDevice || !m_audioDevice->isOpen() || m_audioSink->state() == QAudio::StoppedState) return;

        // Cap buffer at ~200ms of audio to bound latency.
        // At 24kHz stereo int16 = 96000 bytes/sec → 200ms = 19200 bytes.
        // At 48kHz stereo int16 = 192000 bytes/sec → 200ms = 38400 bytes.
        const int sampleRate = m_resampleTo48k ? 48000 : DEFAULT_SAMPLE_RATE;
        const qsizetype maxBufBytes = sampleRate * 2 * 2 / 5; // 200ms worth
        if (m_rxBuffer.size() > maxBufBytes) {
            // Drop oldest samples to keep latency bounded
            m_rxBuffer.remove(0, m_rxBuffer.size() - maxBufBytes);
        }

        qsizetype len = m_audioSink->bytesFree();
        len = std::min(len, m_rxBuffer.size());
        if (len > 0)
        {
            len = m_audioDevice->write(m_rxBuffer.left(len));
            m_rxBuffer.remove(0, len);
        }
    });
    m_rxTimer->start();
}

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
    fmt.setSampleFormat(QAudioFormat::Float);
    return fmt;
}

// ─── RX stream ───────────────────────────────────────────────────────────────

bool AudioEngine::startRxStream()
{
    if (m_audioSink) return true;   // already running

    QAudioFormat fmt = makeFormat();
    const QAudioDevice dev = m_outputDevice.isNull()
        ? QMediaDevices::defaultAudioOutput() : m_outputDevice;

    // Windows WASAPI shared mode handles sample rate conversion transparently,
    // but Qt's isFormatSupported() often returns false for valid formats (e.g.
    // Voicemeeter, FlexRadio DAX). Try opening the sink directly at each rate
    // and fall back only if start() actually fails.
#ifdef Q_OS_WIN
    m_resampleTo48k = false;
    m_audioSink = new QAudioSink(dev, fmt, this);
    m_audioSink->setVolume(m_rxVolume.load());
    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice) {
        qCWarning(lcAudio) << "AudioEngine: 24kHz sink failed to open, trying 48kHz";
        delete m_audioSink;
        fmt.setSampleRate(48000);
        m_resampleTo48k = true;
        m_audioSink = new QAudioSink(dev, fmt, this);
        m_audioSink->setVolume(m_rxVolume.load());
        m_audioDevice = m_audioSink->start();
        if (!m_audioDevice) {
            qCWarning(lcAudio) << "AudioEngine: 48kHz sink also failed";
            delete m_audioSink;
            m_audioSink = nullptr;
            return false;
        }
    }
    qCWarning(lcAudio) << "AudioEngine: RX stream started at" << fmt.sampleRate() << "Hz"
                       << "device:" << dev.description();
    emit rxStarted();
    return true;
#else
    if (!dev.isFormatSupported(fmt)) {
        qCWarning(lcAudio) << "AudioEngine: output device does not support 24kHz stereo Int16, trying 48kHz";
        fmt.setSampleRate(48000);
        m_resampleTo48k = true;
        if (!dev.isFormatSupported(fmt)) {
            qCWarning(lcAudio) << "AudioEngine: output device does not support 48kHz stereo Int16 either";
            qCWarning(lcAudio) << "No audio device detected";
            return false;
        }
    } else {
        m_resampleTo48k = false;
    }
#endif

    m_audioSink   = new QAudioSink(dev, fmt, this);
    m_audioSink->setVolume(m_rxVolume.load());
    m_audioDevice = m_audioSink->start();   // push-mode

    if (!m_audioDevice) {
        qCWarning(lcAudio) << "AudioEngine: failed to open audio sink";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    qCDebug(lcAudio) << "AudioEngine: RX stream started";
    emit rxStarted();
    return true;
}

void AudioEngine::stopRxStream()
{
    if (m_audioSink) {
        // Guard: same stale-device-handle crash can occur on the RX side (#1059).
        if (m_audioSink->state() != QAudio::StoppedState)
            m_audioSink->stop();
        delete m_audioSink;
        m_audioSink   = nullptr;
        m_audioDevice = nullptr;
    }
    emit rxStopped();
}

void AudioEngine::setRxVolume(float v)
{
    m_rxVolume.store(qBound(0.0f, v, 1.0f));
    if (m_audioSink)
        m_audioSink->setVolume(m_muted.load() ? 0.0f : m_rxVolume.load());
}

void AudioEngine::setMuted(bool muted)
{
    m_muted.store(muted);
    if (m_audioSink)
        m_audioSink->setVolume(muted ? 0.0f : m_rxVolume.load());
}

// Resample 24kHz stereo float32 → 48kHz stereo float32 via r8brain.
QByteArray AudioEngine::resampleStereo(const QByteArray& pcm)
{
    if (!m_rxResampler)
        m_rxResampler = std::make_unique<Resampler>(24000, 48000);
    const auto* src = reinterpret_cast<const float*>(pcm.constData());
    return m_rxResampler->processStereoToStereo(src, pcm.size() / (2 * static_cast<int>(sizeof(float))));
}

void AudioEngine::feedAudioData(const QByteArray& pcm)
{
    if (!m_audioSink) return;  // PC audio disabled
    // Note: m_radeMode no longer blocks feedAudioData globally.
    // The RADE slice's raw OFDM noise is muted at the slice level (audio_mute=1)
    // so it doesn't reach the speaker. Other slices' audio plays normally.

    auto writeAudio = [this](const QByteArray& data) {
        if (!m_audioDevice || !m_audioDevice->isOpen()) return;
        if (m_resampleTo48k)
            m_rxBuffer.append(resampleStereo(data));
        else
            m_rxBuffer.append(data); 
    };

    // Bypass client-side DSP during TX (#367). NR2/RN2/BNR adapt their
    // internal state (noise floor, RNN hidden state) to silence during TX,
    // causing distorted audio for several seconds after returning to RX.
    // DSP mutex: prevents use-after-free if enable/disable runs concurrently (#502)
    {
        std::lock_guard<std::recursive_mutex> dspLock(m_dspMutex);
        if (m_transmitting) {
            writeAudio(pcm);
            emit levelChanged(computeRMS(pcm));
        } else if (m_rn2Enabled && m_rn2) {
            QByteArray processed = m_rn2->process(pcm);
            writeAudio(processed);
            emit levelChanged(computeRMS(processed));
        } else if (m_nr2Enabled && m_nr2) {
            processNr2(pcm);
            writeAudio(m_nr2Output);
            emit levelChanged(computeRMS(m_nr2Output));

#ifdef HAVE_SPECBLEACH
        } else if (m_nr4Enabled && m_nr4) {
            QByteArray processed = m_nr4->process(pcm);
            writeAudio(processed);
            emit levelChanged(computeRMS(processed));
#endif
#ifdef HAVE_DFNR
        } else if (m_dfnrEnabled && m_dfnr) {
            QByteArray processed = m_dfnr->process(pcm);
            writeAudio(processed);
            emit levelChanged(computeRMS(processed));
#endif
        } else if (m_bnrEnabled && m_bnr && m_bnr->isConnected()) {
            processBnr(pcm);
            // processBnr writes audio and emits level internally
        } else if (!m_radeMode) {
            writeAudio(pcm);
            emit levelChanged(computeRMS(pcm));
        }
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

QString AudioEngine::wisdomFilePath()
{
    return wisdomDir() + "aethersdr_fftw_wisdom";
}

bool AudioEngine::needsWisdomGeneration()
{
    return !QFile::exists(wisdomFilePath());
}

void AudioEngine::generateWisdom(std::function<void(int,int,const std::string&)> progress)
{
    SpectralNR::generateWisdom(wisdomDir().toStdString(), std::move(progress));
}

void AudioEngine::setNr2Enabled(bool on)
{
    if (m_nr2Enabled == on) return;
    // RADE outputs decoded speech — client-side DSP has no effect
    if (on && m_radeMode) return;
    std::lock_guard<std::recursive_mutex> lock(m_dspMutex);
    if (on) {
        // Disable RN2/BNR/NR4/DFNR if on — they're mutually exclusive
        if (m_rn2Enabled) setRn2Enabled(false);
        if (m_bnrEnabled) setBnrEnabled(false);
        if (m_nr4Enabled) setNr4Enabled(false);
        if (m_dfnrEnabled) setDfnrEnabled(false);
        // Wisdom should already be generated by MainWindow::enableNr2WithWisdom().
        if (!needsWisdomGeneration())
            SpectralNR::generateWisdom(wisdomDir().toStdString());
        m_nr2 = std::make_unique<SpectralNR>(256, DEFAULT_SAMPLE_RATE);
        if (m_nr2->hasPlanFailed()) {
            qCWarning(lcAudio) << "AudioEngine: NR2 FFTW plan creation failed — disabling";
            m_nr2.reset();
            emit nr2EnabledChanged(false);
            return;
        }
        // Restore user-adjusted parameters from AppSettings
        auto& s = AppSettings::instance();
        m_nr2->setGainMax(s.value("NR2GainMax", "1.50").toFloat());
        m_nr2->setGainSmooth(s.value("NR2GainSmooth", "0.85").toFloat());
        m_nr2->setQspp(s.value("NR2Qspp", "0.20").toFloat());
        m_nr2->setGainMethod(s.value("NR2GainMethod", "2").toInt());
        m_nr2->setNpeMethod(s.value("NR2NpeMethod", "0").toInt());
        m_nr2->setAeFilter(s.value("NR2AeFilter", "True").toString() == "True");
        m_nr2Enabled = true;
    } else {
        m_nr2Enabled = false;
        m_nr2.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: NR2" << (on ? "enabled" : "disabled");
    emit nr2EnabledChanged(on);
}

void AudioEngine::setNr2GainMax(float v)    { if (m_nr2) m_nr2->setGainMax(v); }
void AudioEngine::setNr2Qspp(float v)      { if (m_nr2) m_nr2->setQspp(v); }
void AudioEngine::setNr2GainSmooth(float v) { if (m_nr2) m_nr2->setGainSmooth(v); }
void AudioEngine::setNr2GainMethod(int m)   { if (m_nr2) m_nr2->setGainMethod(m); }
void AudioEngine::setNr2NpeMethod(int m)    { if (m_nr2) m_nr2->setNpeMethod(m); }
void AudioEngine::setNr2AeFilter(bool on)   { if (m_nr2) m_nr2->setAeFilter(on); }

#ifdef HAVE_SPECBLEACH

void AudioEngine::setNr4Enabled(bool on)
{
    if (m_nr4Enabled == on) return;
    std::lock_guard<std::recursive_mutex> lock(m_dspMutex);
    if (on) {
        if (m_radeMode) return;
        if (m_nr2Enabled) setNr2Enabled(false);
        if (m_rn2Enabled) setRn2Enabled(false);
        if (m_bnrEnabled) setBnrEnabled(false);
        if (m_dfnrEnabled) setDfnrEnabled(false);
        m_nr4 = std::make_unique<SpecbleachFilter>();
        if (!m_nr4->isValid()) {
            qCWarning(lcAudio) << "AudioEngine: NR4 initialization failed";
            m_nr4.reset();
            emit nr4EnabledChanged(false);
            return;
        }
        // Restore all saved params
        auto& s = AppSettings::instance();
        m_nr4->setReductionAmount(s.value("NR4ReductionAmount", "10.0").toFloat());
        m_nr4->setSmoothingFactor(s.value("NR4SmoothingFactor", "0.0").toFloat());
        m_nr4->setWhiteningFactor(s.value("NR4WhiteningFactor", "0.0").toFloat());
        m_nr4->setAdaptiveNoise(s.value("NR4AdaptiveNoise", "True").toString() == "True");
        m_nr4->setNoiseEstimationMethod(s.value("NR4NoiseEstimationMethod", "0").toInt());
        m_nr4->setMaskingDepth(s.value("NR4MaskingDepth", "0.50").toFloat());
        m_nr4->setSuppressionStrength(s.value("NR4SuppressionStrength", "0.50").toFloat());
        m_nr4Enabled = true;
    } else {
        m_nr4Enabled = false;
        m_nr4.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: NR4" << (on ? "enabled" : "disabled");
    emit nr4EnabledChanged(on);
}

void AudioEngine::setNr4ReductionAmount(float dB) { if (m_nr4) m_nr4->setReductionAmount(dB); }
void AudioEngine::setNr4SmoothingFactor(float pct) { if (m_nr4) m_nr4->setSmoothingFactor(pct); }
void AudioEngine::setNr4WhiteningFactor(float pct) { if (m_nr4) m_nr4->setWhiteningFactor(pct); }
void AudioEngine::setNr4AdaptiveNoise(bool on) { if (m_nr4) m_nr4->setAdaptiveNoise(on); }
void AudioEngine::setNr4NoiseEstimationMethod(int m) { if (m_nr4) m_nr4->setNoiseEstimationMethod(m); }
void AudioEngine::setNr4MaskingDepth(float v) { if (m_nr4) m_nr4->setMaskingDepth(v); }
void AudioEngine::setNr4SuppressionStrength(float v) { if (m_nr4) m_nr4->setSuppressionStrength(v); }
#else // !HAVE_SPECBLEACH — stubs
void AudioEngine::setNr4Enabled(bool) {}
void AudioEngine::setNr4ReductionAmount(float) {}
void AudioEngine::setNr4SmoothingFactor(float) {}
void AudioEngine::setNr4WhiteningFactor(float) {}
void AudioEngine::setNr4AdaptiveNoise(bool) {}
void AudioEngine::setNr4NoiseEstimationMethod(int) {}
void AudioEngine::setNr4MaskingDepth(float) {}
void AudioEngine::setNr4SuppressionStrength(float) {}
#endif // HAVE_SPECBLEACH

void AudioEngine::setRn2Enabled(bool on)
{
    if (m_rn2Enabled == on) return;
    if (on && m_radeMode) return;
    std::lock_guard<std::recursive_mutex> lock(m_dspMutex);
    if (on) {
        // Disable NR2/BNR/NR4/DFNR first — they're mutually exclusive
        if (m_nr2Enabled) setNr2Enabled(false);
        if (m_bnrEnabled) setBnrEnabled(false);
        if (m_nr4Enabled) setNr4Enabled(false);
        if (m_dfnrEnabled) setDfnrEnabled(false);
        m_rn2 = std::make_unique<RNNoiseFilter>();
        if (!m_rn2->isValid()) {
            qCWarning(lcAudio) << "AudioEngine: RN2 rnnoise_create() failed — disabling";
            m_rn2.reset();
            emit rn2EnabledChanged(false);
            return;
        }
        // Set flag AFTER object is fully constructed
        m_rn2Enabled = true;
    } else {
        m_rn2Enabled = false;
        m_rn2.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: RN2 (RNNoise)" << (on ? "enabled" : "disabled");
    emit rn2EnabledChanged(on);
}

// ─── BNR (NVIDIA NIM GPU noise removal) ──────────────────────────────────────

void AudioEngine::setBnrEnabled(bool on)
{
    if (m_bnrEnabled == on) return;
    if (on && m_radeMode) return;
    std::lock_guard<std::recursive_mutex> lock(m_dspMutex);
    if (on) {
        // Mutual exclusion with NR2, RN2, NR4, and DFNR
        if (m_nr2Enabled) setNr2Enabled(false);
        if (m_rn2Enabled) setRn2Enabled(false);
        if (m_nr4Enabled) setNr4Enabled(false);
        if (m_dfnrEnabled) setDfnrEnabled(false);

        m_bnr = std::make_unique<NvidiaBnrFilter>(this);
        connect(m_bnr.get(), &NvidiaBnrFilter::connectionChanged,
                this, &AudioEngine::bnrConnectionChanged);

        // Resamplers: 24kHz mono ↔ 48kHz mono
        // BNR returns variable-sized chunks (up to 200ms = 9600 samples at 48kHz),
        // so use a large maxBlockSamples to avoid r8brain buffer overflow.
        m_bnrUp   = std::make_unique<Resampler>(24000, 48000, 16384);
        m_bnrDown = std::make_unique<Resampler>(48000, 24000, 16384);
        m_bnrOutBuf.clear();
        m_bnrPrimed = false;
        // Set flag AFTER objects are fully constructed
        m_bnrEnabled = true;

        // Try connecting — if the container is still booting, retry with a timer.
        if (!m_bnr->connectToServer(m_bnrAddress)) {
            // Retry up to 5 times, 2s apart
            auto* retryTimer = new QTimer(this);
            retryTimer->setInterval(2000);
            auto retryCount = std::make_shared<int>(0);
            connect(retryTimer, &QTimer::timeout, this,
                    [this, retryTimer, retryCount]() {
                if (!m_bnr || *retryCount >= 5) {
                    retryTimer->stop();
                    retryTimer->deleteLater();
                    if (m_bnr && !m_bnr->isConnected()) {
                        qCWarning(lcAudio) << "AudioEngine: BNR connect failed after retries";
                        m_bnr.reset();
                        m_bnrUp.reset();
                        m_bnrDown.reset();
                        m_bnrEnabled = false;
                        emit bnrEnabledChanged(false);
                    }
                    return;
                }
                ++(*retryCount);
                qDebug() << "AudioEngine: BNR connect retry" << *retryCount << "of 5";
                if (m_bnr->connectToServer(m_bnrAddress)) {
                    retryTimer->stop();
                    retryTimer->deleteLater();
                }
            });
            retryTimer->start();
        }
    } else {
        m_bnrEnabled = false;
        if (m_bnr) m_bnr->disconnect();
        m_bnr.reset();
        m_bnrUp.reset();
        m_bnrDown.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: BNR (NVIDIA NIM)" << (on ? "enabled" : "disabled");
    emit bnrEnabledChanged(on);
}

void AudioEngine::setBnrAddress(const QString& addr)
{
    m_bnrAddress = addr;
}

void AudioEngine::setBnrIntensity(float ratio)
{
    if (m_bnr) m_bnr->setIntensityRatio(ratio);
}

float AudioEngine::bnrIntensity() const
{
    return m_bnr ? m_bnr->intensityRatio() : 1.0f;
}

bool AudioEngine::bnrConnected() const
{
    return m_bnr && m_bnr->isConnected();
}

void AudioEngine::processBnr(const QByteArray& stereoPcm)
{
    // ── Feed input to BNR container (non-blocking) ───────────────────────

    // 1. 24kHz stereo float32 → 24kHz mono float32 (average L+R)
    const auto* src = reinterpret_cast<const float*>(stereoPcm.constData());
    const int stereoFrames = stereoPcm.size() / (2 * static_cast<int>(sizeof(float)));

    if (static_cast<int>(m_nr2Mono.size()) < stereoFrames)
        m_nr2Mono.resize(stereoFrames);
    for (int i = 0; i < stereoFrames; ++i)
        m_nr2Mono[i] = (src[2 * i] + src[2 * i + 1]) * 0.5f;

    // 2. 24kHz mono float32 → 48kHz mono float32 (r8brain)
    QByteArray mono48k = m_bnrUp->process(m_nr2Mono.data(), stereoFrames);

    // 3. Already float32 — pass directly to BNR
    const auto* mono48kSrc = reinterpret_cast<const float*>(mono48k.constData());
    const int mono48kSamples = mono48k.size() / static_cast<int>(sizeof(float));

    // 4. Push to BNR container (non-blocking), pull any denoised data
    QByteArray denoised = m_bnr->process(mono48kSrc, mono48kSamples);

    // ── Convert denoised data and add to jitter buffer ───────────────────

    if (!denoised.isEmpty()) {
        // 5. BNR returns float32 48kHz mono — downsample to 24kHz mono float32
        const auto* df = reinterpret_cast<const float*>(denoised.constData());
        const int dn = denoised.size() / static_cast<int>(sizeof(float));

        QByteArray mono24k = m_bnrDown->process(df, dn);

        // 6. Mono float32 → stereo float32 (duplicate L=R)
        const auto* m24 = reinterpret_cast<const float*>(mono24k.constData());
        const int n24 = mono24k.size() / static_cast<int>(sizeof(float));
        QByteArray stereo(n24 * 2 * static_cast<int>(sizeof(float)), Qt::Uninitialized);
        auto* ds = reinterpret_cast<float*>(stereo.data());
        for (int i = 0; i < n24; ++i) {
            ds[2 * i]     = m24[i];
            ds[2 * i + 1] = m24[i];
        }

        m_bnrOutBuf.append(stereo);

        // Cap jitter buffer at ~500ms (24kHz stereo float32 = 192000 bytes/sec)
        constexpr int maxBufBytes = 96000;  // 500ms
        if (m_bnrOutBuf.size() > maxBufBytes)
            m_bnrOutBuf.remove(0, m_bnrOutBuf.size() - maxBufBytes);
    }

    // ── Play from jitter buffer ──────────────────────────────────────────

    // Wait for ~50ms of buffered audio before starting playback (priming)
    constexpr int primeBytes = 9600;  // 50ms of 24kHz stereo float32
    if (!m_bnrPrimed) {
        if (m_bnrOutBuf.size() >= primeBytes)
            m_bnrPrimed = true;
        else
            return;  // still priming — silence (no audio output)
    }

    // Play the same amount of audio as the incoming chunk to maintain sync
    const int wantBytes = stereoPcm.size();
    if (m_bnrOutBuf.size() >= wantBytes) {
        QByteArray chunk = m_bnrOutBuf.left(wantBytes);
        m_bnrOutBuf.remove(0, wantBytes);

        if (m_audioDevice && m_audioDevice->isOpen()) {
            if (m_resampleTo48k)
                m_rxBuffer.append(resampleStereo(chunk));
            else
                m_rxBuffer.append(chunk); 
        }
        emit levelChanged(computeRMS(chunk));
    }
    // If buffer underrun, skip this callback (brief silence, not choppy)
}

// ─── DFNR (DeepFilterNet3 neural noise reduction) ────────────────────────────

#ifdef HAVE_DFNR

void AudioEngine::setDfnrEnabled(bool on)
{
    if (m_dfnrEnabled == on) return;
    if (on && m_radeMode) return;
    std::lock_guard<std::recursive_mutex> lock(m_dspMutex);
    if (on) {
        // Mutual exclusion with NR2, RN2, NR4, and BNR
        if (m_nr2Enabled) setNr2Enabled(false);
        if (m_rn2Enabled) setRn2Enabled(false);
        if (m_nr4Enabled) setNr4Enabled(false);
        if (m_bnrEnabled) setBnrEnabled(false);
        m_dfnr = std::make_unique<DeepFilterFilter>();
        if (!m_dfnr->isValid()) {
            qCWarning(lcAudio) << "AudioEngine: DFNR df_create() failed — disabling";
            m_dfnr.reset();
            emit dfnrEnabledChanged(false);
            return;
        }
        // Restore saved attenuation limit
        auto& s = AppSettings::instance();
        m_dfnr->setAttenLimit(s.value("DfnrAttenLimit", "100").toFloat());
        m_dfnr->setPostFilterBeta(s.value("DfnrPostFilterBeta", "0.0").toFloat());
        // Set flag AFTER object is fully constructed
        m_dfnrEnabled = true;
    } else {
        m_dfnrEnabled = false;
        m_dfnr.reset();
    }
    qCDebug(lcAudio) << "AudioEngine: DFNR (DeepFilterNet3)" << (on ? "enabled" : "disabled");
    emit dfnrEnabledChanged(on);
}

void AudioEngine::setDfnrAttenLimit(float db)
{
    if (m_dfnr) m_dfnr->setAttenLimit(db);
}

float AudioEngine::dfnrAttenLimit() const
{
    return m_dfnr ? m_dfnr->attenLimit() : 100.0f;
}

void AudioEngine::setDfnrPostFilterBeta(float beta)
{
    if (m_dfnr) m_dfnr->setPostFilterBeta(beta);
}

#else // !HAVE_DFNR — stubs
void AudioEngine::setDfnrEnabled(bool) {}
void AudioEngine::setDfnrAttenLimit(float) {}
float AudioEngine::dfnrAttenLimit() const { return 100.0f; }
void AudioEngine::setDfnrPostFilterBeta(float) {}
#endif // HAVE_DFNR

void AudioEngine::processNr2(const QByteArray& stereoPcm)
{
    const int totalFloats = stereoPcm.size() / static_cast<int>(sizeof(float));
    const int stereoFrames = totalFloats / 2;
    const auto* src = reinterpret_cast<const float*>(stereoPcm.constData());

    // Resize pre-allocated buffers if needed
    if (static_cast<int>(m_nr2Mono.size()) < stereoFrames) {
        m_nr2Mono.resize(stereoFrames);
        m_nr2Processed.resize(stereoFrames);
    }

    // Stereo float32 → mono float32 (average L+R)
    for (int i = 0; i < stereoFrames; ++i)
        m_nr2Mono[i] = (src[2 * i] + src[2 * i + 1]) * 0.5f;

    // Process through SpectralNR (float32 I/O)
    m_nr2->process(m_nr2Mono.data(), m_nr2Processed.data(), stereoFrames);

    // Mono float32 → stereo float32 (duplicate)
    const int outBytes = stereoFrames * 2 * static_cast<int>(sizeof(float));
    m_nr2Output.resize(outBytes);
    auto* dst = reinterpret_cast<float*>(m_nr2Output.data());
    for (int i = 0; i < stereoFrames; ++i) {
        dst[2 * i]     = m_nr2Processed[i];
        dst[2 * i + 1] = m_nr2Processed[i];
    }
}

QByteArray AudioEngine::applyBoost(const QByteArray& pcm, float gain) const
{
    const int nSamples = pcm.size() / sizeof(int16_t);
    const auto* src = reinterpret_cast<const int16_t*>(pcm.constData());
    QByteArray out(pcm.size(), Qt::Uninitialized);
    auto* dst = reinterpret_cast<int16_t*>(out.data());
    for (int i = 0; i < nSamples; ++i) {
        float s = src[i] * gain;
        // Soft clamp to avoid harsh digital clipping
        if (s > 32767.0f) s = 32767.0f;
        else if (s < -32767.0f) s = -32767.0f;
        dst[i] = static_cast<int16_t>(s);
    }
    return out;
}

float AudioEngine::computeRMS(const QByteArray& pcm) const
{
    const int samples = pcm.size() / static_cast<int>(sizeof(float));
    if (samples == 0) return 0.0f;

    const float* data = reinterpret_cast<const float*>(pcm.constData());
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        sum += static_cast<double>(data[i]) * data[i];
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

    // TX mic capture uses Int16 — we convert to float32 after capture.
    // (makeFormat() returns Float for the RX sink, but mic hardware is Int16.)
    QAudioFormat fmt;
    fmt.setSampleRate(DEFAULT_SAMPLE_RATE);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice dev = m_inputDevice.isNull()
        ? QMediaDevices::defaultAudioInput() : m_inputDevice;

    if (dev.isNull()) {
        qCWarning(lcAudio) << "AudioEngine: no audio input device available";
        return false;
    }

    qCDebug(lcAudio) << "AudioEngine: input device caps:"
        << dev.minimumSampleRate() << "-" << dev.maximumSampleRate() << "Hz"
        << dev.minimumChannelCount() << "-" << dev.maximumChannelCount() << "ch";

    // Negotiate the best sample rate for TX mic input.
    // macOS: prefer 48kHz — Core Audio claims 24kHz support but its internal
    // resampler produces gravelly artifacts at non-standard rates. Let r8brain
    // handle the 48k→24k conversion instead (clean 2:1 integer-ratio downsample).
    // Linux/Windows: prefer 24kHz (radio native — no resampling needed).
    bool formatFound = false;
#ifdef Q_OS_MAC
    constexpr int rates[] = {48000, 44100, 24000};
#else
    constexpr int rates[] = {24000, 48000, 44100};
#endif
#ifdef Q_OS_WIN
    // Windows WASAPI shared mode handles rate conversion transparently,
    // but Qt's isFormatSupported() returns false for many valid devices
    // (Voicemeeter, FlexRadio DAX, etc.). Default to 48kHz stereo and
    // let WASAPI handle it — only fall back if open actually fails later.
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    formatFound = true;
#else
    for (int channels : {2, 1}) {
        for (int rate : rates) {
            fmt.setChannelCount(channels);
            fmt.setSampleRate(rate);
            if (dev.isFormatSupported(fmt)) {
                formatFound = true;
                break;
            }
        }
        if (formatFound) break;
    }
#endif

    if (!formatFound) {
        qCWarning(lcAudio) << "AudioEngine: input device supports no usable format"
            << "(tried 24/48/44.1 kHz, stereo and mono)";
        return false;
    }

    qCInfo(lcAudio) << "AudioEngine: selected TX input format:"
        << fmt.sampleRate() << "Hz" << fmt.channelCount() << "ch";

    // Record actual negotiated input format for resampling in onTxAudioReady
    m_txInputRate = fmt.sampleRate();
    m_txInputMono = (fmt.channelCount() == 1);
    m_txNeedsResample = (m_txInputRate != 24000);

    // Create polyphase resampler for high-quality rate conversion
    if (m_txNeedsResample)
        m_txResampler = std::make_unique<Resampler>(m_txInputRate, 24000, 16384);
    else
        m_txResampler.reset();

    qCDebug(lcAudio) << "AudioEngine: TX input device:" << dev.description()
             << "rate:" << fmt.sampleRate() << "ch:" << fmt.channelCount()
             << "resample:" << m_txNeedsResample;

#ifdef Q_OS_MAC
    // macOS: QAudioSource pull mode broken — use push mode with QBuffer
    m_micBuffer = new QBuffer(this);
    m_micBuffer->open(QIODevice::ReadWrite);
    m_audioSource = new QAudioSource(dev, fmt, this);
    m_audioSource->start(m_micBuffer);

    if (m_audioSource->state() == QAudio::StoppedState) {
        qCWarning(lcAudio) << "AudioEngine: failed to start audio source";
        delete m_audioSource; m_audioSource = nullptr;
        delete m_micBuffer; m_micBuffer = nullptr;
        return false;
    }

    // Poll push-mode buffer
    m_txPollTimer = new QTimer(this);
    m_txPollTimer->setInterval(5);
    connect(m_txPollTimer, &QTimer::timeout, this, &AudioEngine::onTxAudioReady);
    m_txPollTimer->start();

    // Guard against CoreAudio silently stopping the source after extended
    // runtime (~16h). Detect the silent stop, pause the timer, and restart
    // cleanly so onTxAudioReady never touches a stale m_micBuffer. (#1149)
    connect(m_audioSource, &QAudioSource::stateChanged, this,
            [this](QAudio::State state) {
        if (state != QAudio::StoppedState) return;
        if (!m_txPollTimer) return;  // intentional stop already handled
        m_txPollTimer->stop();
        QHostAddress addr = m_txAddress;
        quint16 port = m_txPort;
        QMetaObject::invokeMethod(this, [this, addr, port]() {
            qCWarning(lcAudio) << "AudioEngine: QAudioSource stopped silently (#1149), restarting TX";
            stopTxStream();
            startTxStream(addr, port);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
#else
    // Linux/Windows: pull mode works fine
    m_audioSource = new QAudioSource(dev, fmt, this);
    m_micDevice = m_audioSource->start();
    if (!m_micDevice) {
        qCWarning(lcAudio) << "AudioEngine: failed to open audio source at"
                           << fmt.sampleRate() << "Hz" << fmt.channelCount() << "ch"
                           << "error:" << m_audioSource->error()
                           << "device:" << dev.description();
#ifdef Q_OS_WIN
        // Windows: WASAPI may reject our negotiated format at open time.
        // Try additional rates before giving up.
        delete m_audioSource; m_audioSource = nullptr;
        bool txOpened = false;
        constexpr int fallbackRates[] = {48000, 44100, 24000, 16000};
        for (int rate : fallbackRates) {
            if (rate == fmt.sampleRate()) continue;
            for (int ch : {2, 1}) {
                fmt.setSampleRate(rate);
                fmt.setChannelCount(ch);
                m_audioSource = new QAudioSource(dev, fmt, this);
                m_micDevice = m_audioSource->start();
                if (m_micDevice) {
                    qCInfo(lcAudio) << "AudioEngine: TX source opened at fallback"
                                    << rate << "Hz" << ch << "ch";
                    m_txInputRate = rate;
                    m_txInputMono = (ch == 1);
                    m_txNeedsResample = (rate != 24000);
                    if (m_txNeedsResample) {
                        m_txResampler = std::make_unique<Resampler>(rate, 24000, 16384);
                    } else {
                        m_txResampler.reset();
                    }
                    txOpened = true;
                    break;
                }
                delete m_audioSource; m_audioSource = nullptr;
            }
            if (txOpened) break;
        }
        if (!txOpened) {
            qCWarning(lcAudio) << "AudioEngine: all TX source formats failed";
            return false;
        }
#else
        delete m_audioSource; m_audioSource = nullptr;
        return false;
#endif
    }
    connect(m_micDevice, &QIODevice::readyRead, this, &AudioEngine::onTxAudioReady);
#endif

    qCWarning(lcAudio) << "AudioEngine: TX stream started ->" << radioAddress.toString()
             << ":" << radioPort << "streamId:" << Qt::hex << m_txStreamId
             << "device:" << dev.description() << "rate:" << fmt.sampleRate()
             << "ch:" << fmt.channelCount();
    return true;
}

void AudioEngine::stopTxStream()
{
#ifdef Q_OS_MAC
    if (m_txPollTimer) {
        m_txPollTimer->stop();
        delete m_txPollTimer;
        m_txPollTimer = nullptr;
    }
#endif
    if (m_audioSource) {
        // Guard: calling stop() on an already-stopped QAudioSource on macOS causes
        // AudioOutputUnitStop to dereference a stale CoreAudio device handle,
        // producing EXC_ARM_DA_ALIGN / EXC_BAD_ACCESS (#1059).
        if (m_audioSource->state() != QAudio::StoppedState)
            m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
        m_micDevice   = nullptr;
    }
#ifdef Q_OS_MAC
    if (m_micBuffer) {
        delete m_micBuffer;
        m_micBuffer = nullptr;
    }
#endif
    m_txSocket.close();
    m_txAccumulator.clear();
    m_txFloatAccumulator.clear();
    m_txResampler.reset();
}

void AudioEngine::onTxAudioReady()
{
#ifdef Q_OS_MAC
    if (!m_micBuffer || !m_audioSource) return;
    if (m_audioSource->state() == QAudio::StoppedState) return;
    if (!m_micBuffer->isOpen()) return;
    if (m_txStreamId == 0 && m_remoteTxStreamId == 0) return;
    qint64 avail = m_micBuffer->pos();
    if (avail <= 0) return;
    QByteArray data = m_micBuffer->data();
    m_micBuffer->buffer().clear();
    m_micBuffer->seek(0);
    if (data.isEmpty()) return;
#else
    if (!m_micDevice || (m_txStreamId == 0 && m_remoteTxStreamId == 0)) return;
    QByteArray data = m_micDevice->readAll();
    if (data.isEmpty()) return;
#endif

    // Resample int16 to 24kHz stereo if needed, then convert to float32
    // for RADE. Normal TX path stays int16 (Opus requires int16).
    if (m_txNeedsResample && m_txResampler) {
        // Convert int16 → float32 for float32 Resampler
        const auto* i16 = reinterpret_cast<const int16_t*>(data.constData());
        const int numSamples = data.size() / static_cast<int>(sizeof(int16_t));
        QByteArray f32(numSamples * static_cast<int>(sizeof(float)), Qt::Uninitialized);
        auto* fd = reinterpret_cast<float*>(f32.data());
        for (int i = 0; i < numSamples; ++i)
            fd[i] = i16[i] / 32768.0f;

        if (m_txInputMono) {
            f32 = m_txResampler->processMonoToStereo(
                reinterpret_cast<const float*>(f32.constData()),
                f32.size() / static_cast<int>(sizeof(float)));
        } else {
            f32 = m_txResampler->processStereoToStereo(
                reinterpret_cast<const float*>(f32.constData()),
                f32.size() / (2 * static_cast<int>(sizeof(float))));
        }

        // Convert back to int16 for the rest of the TX path
        const auto* rsrc = reinterpret_cast<const float*>(f32.constData());
        const int rcount = f32.size() / static_cast<int>(sizeof(float));
        data.resize(rcount * static_cast<int>(sizeof(int16_t)));
        auto* rdst = reinterpret_cast<int16_t*>(data.data());
        for (int i = 0; i < rcount; ++i)
            rdst[i] = static_cast<int16_t>(std::clamp(rsrc[i] * 32768.0f, -32768.0f, 32767.0f));
    } else if (m_txInputMono) {
        // 24kHz mono int16 (no resample needed) → duplicate to stereo
        const auto* src = reinterpret_cast<const int16_t*>(data.constData());
        const int monoSamples = data.size() / static_cast<int>(sizeof(int16_t));
        QByteArray stereo(monoSamples * 2 * static_cast<int>(sizeof(int16_t)), Qt::Uninitialized);
        auto* dst = reinterpret_cast<int16_t*>(stereo.data());
        for (int i = 0; i < monoSamples; ++i) {
            dst[i * 2] = src[i];
            dst[i * 2 + 1] = src[i];
        }
        data = stereo;
    }

    // RADE mode: convert int16 → float32 and emit for RADEEngine
    if (m_radeMode) {
        const auto* i16 = reinterpret_cast<const int16_t*>(data.constData());
        const int ns = data.size() / static_cast<int>(sizeof(int16_t));
        QByteArray f32(ns * static_cast<int>(sizeof(float)), Qt::Uninitialized);
        auto* fd = reinterpret_cast<float*>(f32.data());
        for (int i = 0; i < ns; ++i)
            fd[i] = i16[i] / 32768.0f;
        emit txRawPcmReady(f32);
        return;
    }

    // DAX TX mode: VirtualAudioBridge handles TX audio via feedDaxTxAudio().
    // Don't send mic audio — it would conflict with the DAX stream.
    if (m_daxTxMode) return;

    // ── Apply client-side PC mic gain (int16) ───────────────────────────
    const float gain = m_pcMicGain.load();
    if (gain < 0.999f) {
        auto* pcm = reinterpret_cast<int16_t*>(data.data());
        int sampleCount = data.size() / static_cast<int>(sizeof(int16_t));
        for (int i = 0; i < sampleCount; ++i)
            pcm[i] = static_cast<int16_t>(std::clamp(
                static_cast<int>(pcm[i] * gain), -32768, 32767));
    }

    // ── Client-side PC mic level metering (int16) ───────────────────────
    {
        const auto* pcm = reinterpret_cast<const int16_t*>(data.constData());
        int sampleCount = data.size() / static_cast<int>(sizeof(int16_t));
        for (int i = 0; i < sampleCount; i += 2) {  // stereo: use L channel
            float s = std::abs(pcm[i]) / 32768.0f;
            if (s > m_pcMicPeak) m_pcMicPeak = s;
            m_pcMicSumSq += static_cast<double>(s) * s;
            m_pcMicSampleCount++;
        }
        if (m_pcMicSampleCount >= kMicMeterWindowSamples) {
            float rms = static_cast<float>(std::sqrt(m_pcMicSumSq / m_pcMicSampleCount));
            float peakDb = (m_pcMicPeak > 1e-10f) ? 20.0f * std::log10(m_pcMicPeak) : -150.0f;
            float rmsDb  = (rms > 1e-10f)         ? 20.0f * std::log10(rms)          : -150.0f;
            emit pcMicLevelChanged(peakDb, rmsDb);
            m_pcMicPeak = 0.0f;
            m_pcMicSumSq = 0.0;
            m_pcMicSampleCount = 0;
        }
    }

    // ── Opus TX path: always active for remote_audio_tx ────────────────
    // Sends Opus during both RX (VOX/met_in_rx metering) and TX (voice).
    // The radio requires Opus on remote_audio_tx (enforces compression=OPUS).
    // Data is int16 stereo — accumulate directly for Opus encoding.
    if (m_opusTxEnabled) {
        m_opusTxAccumulator.append(data);
        // 240 stereo sample frames × 2 channels × 2 bytes = 960 bytes per 10ms frame
        constexpr int OPUS_FRAME_BYTES = 240 * 2 * sizeof(int16_t);

        while (m_opusTxAccumulator.size() >= OPUS_FRAME_BYTES) {
            if (!m_opusTxCodec) {
                m_opusTxCodec = std::make_unique<OpusCodec>();
                if (!m_opusTxCodec->isValid()) {
                    qCWarning(lcAudio) << "AudioEngine: Opus TX codec init failed, falling back to uncompressed";
                    m_opusTxEnabled = false;
                    m_opusTxCodec.reset();
                    break;
                }
            }

            QByteArray frame = m_opusTxAccumulator.left(OPUS_FRAME_BYTES);
            m_opusTxAccumulator.remove(0, OPUS_FRAME_BYTES);

            QByteArray opus = m_opusTxCodec->encode(frame);
            if (opus.isEmpty()) continue;

            // Build VITA-49 Opus packet matching SmartSDR exactly:
            // Header: 28 bytes + opus payload, NO trailer.
            // FlexLib Opus packets are byte-centric — payload is NOT
            // padded to 32-bit word alignment. Size field in header
            // is still in 32-bit words (rounded up) per VITA-49 spec.
            const int pktBytes = 28 + opus.size();  // exact, no padding
            const int sizeWords = (pktBytes + 3) / 4;  // for header field only
            QByteArray pkt(pktBytes, '\0');
            auto* p = reinterpret_cast<quint32*>(pkt.data());

            // Word 0: type=3 (ExtDataWithStream), C=1, T=0, TSI=3, TSF=1
            p[0] = qToBigEndian<quint32>(
                (3u << 28) | (1u << 27) | (3u << 22) | (1u << 20)
                | ((m_txPacketCount & 0x0F) << 16) | sizeWords);
            m_txPacketCount = (m_txPacketCount + 1) & 0x0F;
            p[1] = qToBigEndian(m_remoteTxStreamId);    // remote_audio_tx stream
            p[2] = qToBigEndian<quint32>(0x00001C2D);   // OUI (FlexRadio)
            p[3] = qToBigEndian<quint32>(0x534C0000 | 0x8005);  // ICC=0x534C, PCC=0x8005
            p[4] = 0; p[5] = 0; p[6] = 0;              // timestamps (all zero)

            memcpy(pkt.data() + 28, opus.constData(), opus.size());

            // Queue for paced delivery instead of sending immediately.
            // The 10ms pacing timer drains one packet per tick for even
            // timing over SmartLink/WAN. Cap queue to ~200ms to prevent
            // runaway growth if the mic delivers faster than real-time.
            m_opusTxQueue.append(pkt);
            if (m_opusTxQueue.size() > 20)
                m_opusTxQueue.removeFirst();
        }
        return;
    }

    // ── Uncompressed TX path (not used — radio forces Opus) ────────────
    m_txAccumulator.append(data);

    while (m_txAccumulator.size() >= TX_PCM_BYTES_PER_PACKET) {
        const int16_t* pcm = reinterpret_cast<const int16_t*>(m_txAccumulator.constData());

        // Convert int16 → float32 for VITA-49 packet (radio expects float32)
        float floatBuf[TX_SAMPLES_PER_PACKET * 2];
        for (int i = 0; i < TX_SAMPLES_PER_PACKET * 2; ++i)
            floatBuf[i] = pcm[i] / 32768.0f;

        QByteArray packet = buildVitaTxPacket(floatBuf, TX_SAMPLES_PER_PACKET);
        emit txPacketReady(packet);

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

    // ── Word 0: Header (DAX TX format, matches FlexLib DAXTXAudioStream) ─
    // Bits 31-28: packet type = 1 (IFDataWithStream)
    // Bit  27:    C = 1 (class ID present)
    // Bit  26:    T = 0 (no trailer)
    // Bits 25-24: reserved = 0
    // Bits 23-22: TSI = 3 (Other)
    // Bits 21-20: TSF = 1 (SampleCount)
    // Bits 19-16: packet count (4-bit)
    // Bits 15-0:  packet size (in 32-bit words)
    quint32 hdr = 0;
    hdr |= (0x1u << 28);          // pkt_type = IFDataWithStream (DAX TX)
    hdr |= (1u << 27);            // C = 1
    // T = 0 (bit 26)
    hdr |= (0x3u << 22);          // TSI = 3 (Other) — matches FlexLib/nDAX
    hdr |= (0x1u << 20);          // TSF = SampleCount
    hdr |= ((m_txPacketCount & 0xF) << 16);
    hdr |= (packetWords & 0xFFFF);
    words[0] = qToBigEndian(hdr);

    // ── Word 1: Stream ID (dax_tx stream for DAX TX audio) ──────────────
    words[1] = qToBigEndian(m_txStreamId);

    // ── Word 2: Class ID OUI (24-bit, right-justified in 32-bit word) ────
    words[2] = qToBigEndian(FLEX_OUI);

    // ── Word 3: InformationClassCode (upper 16) | PacketClassCode (lower 16)
    words[3] = qToBigEndian(
        (static_cast<quint32>(FLEX_INFO_CLASS) << 16) | PCC_IF_NARROW);

    // ── Words 4-6: Timestamps ─────────────────────────────────────────────
    // ── Words 4-6: Timestamps ─────────────────────────────────────────────
    words[4] = 0;  // integer timestamp
    words[5] = 0;  // fractional timestamp high
    words[6] = 0;  // fractional timestamp low

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

void AudioEngine::sendVoiceTxPacket(const QByteArray& pcmData, quint32 streamId)
{
    // Accumulate into a separate buffer for VOX/met_in_rx audio
    m_voxAccumulator.append(pcmData);

    while (m_voxAccumulator.size() >= TX_PCM_BYTES_PER_PACKET) {
        const int16_t* pcm = reinterpret_cast<const int16_t*>(m_voxAccumulator.constData());

        float floatBuf[TX_SAMPLES_PER_PACKET * 2];
        for (int i = 0; i < TX_SAMPLES_PER_PACKET * 2; ++i)
            floatBuf[i] = pcm[i] / 32768.0f;

        // Build packet using the remote_audio_tx stream ID
        quint32 savedId = m_txStreamId;
        m_txStreamId = streamId;
        QByteArray packet = buildVitaTxPacket(floatBuf, TX_SAMPLES_PER_PACKET);
        m_txStreamId = savedId;

        emit txPacketReady(packet);
        m_voxAccumulator.remove(0, TX_PCM_BYTES_PER_PACKET);
    }
}

void AudioEngine::setOutputDevice(const QAudioDevice& dev)
{
    m_outputDevice = dev;
    qCDebug(lcAudio) << "AudioEngine: output device set to" << dev.description();

    // Persist selection
    auto& s = AppSettings::instance();
    s.setValue("AudioOutputDeviceId", dev.id());
    s.save();

    // Restart RX stream if running
    if (m_audioSink) {
        stopRxStream();
        startRxStream();
    }
}

void AudioEngine::setInputDevice(const QAudioDevice& dev)
{
    m_inputDevice = dev;
    qCDebug(lcAudio) << "AudioEngine: input device set to" << dev.description();

    // Persist selection
    auto& s = AppSettings::instance();
    s.setValue("AudioInputDeviceId", dev.id());
    s.save();

    // Restart TX stream if running
    if (m_audioSource) {
        QHostAddress addr = m_txAddress;
        quint16 port = m_txPort;
        stopTxStream();
        startTxStream(addr, port);
    }
}

// ─── RADE digital voice support ──────────────────────────────────────────────

void AudioEngine::setRadeMode(bool on)
{
    m_radeMode = on;
    // RADE outputs decoded speech — client-side DSP has no effect.
    // Disable any active DSP when entering RADE mode.
    if (on) {
        if (m_nr2Enabled) setNr2Enabled(false);
        if (m_rn2Enabled) setRn2Enabled(false);
        if (m_nr4Enabled) setNr4Enabled(false);
        if (m_bnrEnabled) setBnrEnabled(false);
#ifdef HAVE_DFNR
        if (m_dfnrEnabled) setDfnrEnabled(false);
#endif
    }
}

void AudioEngine::sendModemTxAudio(const QByteArray& float32pcm)
{
    if (m_txStreamId == 0) return;

    m_txFloatAccumulator.append(float32pcm);

    constexpr int FLOAT_BYTES_PER_PKT = TX_SAMPLES_PER_PACKET * 2 * sizeof(float); // 1024
    while (m_txFloatAccumulator.size() >= FLOAT_BYTES_PER_PKT) {
        auto* samples = reinterpret_cast<const float*>(m_txFloatAccumulator.constData());
        QByteArray pkt = buildVitaTxPacket(samples, TX_SAMPLES_PER_PACKET);
        emit txPacketReady(pkt);
        m_txFloatAccumulator.remove(0, FLOAT_BYTES_PER_PKT);
    }
}

void AudioEngine::setDaxTxMode(bool on)
{
    m_daxTxMode = on;
}

void AudioEngine::setTransmitting(bool tx)
{
    if (m_transmitting == tx) return;
    m_transmitting = tx;

    if (!tx) {
        // On unkey: drop any partial packet residue so next burst starts cleanly.
        m_txAccumulator.clear();
        m_txFloatAccumulator.clear();
        m_daxPreTxBuffer.clear();
        m_opusTxQueue.clear();
    }
}

void AudioEngine::setRadioTransmitting(bool tx)
{
    m_radioTransmitting = tx;
}

void AudioEngine::setDaxTxUseRadioRoute(bool on)
{
    if (m_daxTxUseRadioRoute == on) return;
    m_daxTxUseRadioRoute = on;
    // Switching route changes payload format; drop partial buffered samples.
    m_txFloatAccumulator.clear();
    m_daxPreTxBuffer.clear();
}

void AudioEngine::feedDaxTxAudio(const QByteArray& float32pcm)
{
    if (m_txStreamId == 0 || float32pcm.isEmpty()) return;

    // Measure DAX TX input level and emit via pcMicLevelChanged so the
    // P/CW mic gauge shows DAX audio level regardless of mic profile (#517)
    {
        const auto* src = reinterpret_cast<const float*>(float32pcm.constData());
        const int samples = float32pcm.size() / sizeof(float);
        float peak = 0.0f;
        double sumSq = 0.0;
        for (int i = 0; i < samples; ++i) {
            float s = std::abs(src[i]);
            if (s > peak) peak = s;
            sumSq += static_cast<double>(src[i]) * src[i];
        }
        m_pcMicPeak = std::max(m_pcMicPeak, peak);
        m_pcMicSumSq += sumSq;
        m_pcMicSampleCount += samples;
        if (m_pcMicSampleCount >= kMicMeterWindowSamples) {
            float rms = static_cast<float>(std::sqrt(m_pcMicSumSq / m_pcMicSampleCount));
            float peakDb = (m_pcMicPeak > 1e-10f) ? 20.0f * std::log10(m_pcMicPeak) : -150.0f;
            float rmsDb  = (rms > 1e-10f)         ? 20.0f * std::log10(rms)          : -150.0f;
            emit pcMicLevelChanged(peakDb, rmsDb);
            m_pcMicPeak = 0.0f;
            m_pcMicSumSq = 0.0;
            m_pcMicSampleCount = 0;
        }
    }

    if (!m_daxTxUseRadioRoute) {
        // Low-latency route: keep radio on mic path (dax=0) and packetize
        // exactly like voice TX (PCC 0x03E3 float32 stereo).
        constexpr int FLOAT_BYTES_PER_PKT = TX_SAMPLES_PER_PACKET * 2 * sizeof(float);

        // Gate on raw radio TX state, not ownership. When an external app
        // (WSJT-X) triggers PTT, m_transmitting is false (we don't own TX)
        // but the radio IS transmitting and needs our DAX audio. (#752)
        if (!m_radioTransmitting) {
            m_daxPreTxBuffer.clear();
            m_txFloatAccumulator.clear();
            return;
        }

        m_txFloatAccumulator.append(float32pcm);
        while (m_txFloatAccumulator.size() >= FLOAT_BYTES_PER_PKT) {
            auto* samples = reinterpret_cast<const float*>(m_txFloatAccumulator.constData());
            QByteArray pkt = buildVitaTxPacket(samples, TX_SAMPLES_PER_PACKET);
            emit txPacketReady(pkt);
            m_txFloatAccumulator.remove(0, FLOAT_BYTES_PER_PKT);
        }
        return;
    }

    // Radio-native DAX route (dax=1): block DAX audio only when mic voice TX is active.
    if (m_transmitting && !m_daxTxMode) return;
    m_daxPreTxBuffer.clear();

    // Convert float32 stereo → int16 mono (reduced BW format, PCC 0x0123).
    const auto* src = reinterpret_cast<const float*>(float32pcm.constData());
    const int stereoSamples = float32pcm.size() / sizeof(float) / 2;

    // Convert: average L+R channels, scale to int16 big-endian
    QByteArray mono(stereoSamples * sizeof(qint16), Qt::Uninitialized);
    auto* dst = reinterpret_cast<qint16*>(mono.data());
    for (int i = 0; i < stereoSamples; ++i) {
        float avg = (src[i * 2] + src[i * 2 + 1]) * 0.5f;
        avg = std::clamp(avg, -1.0f, 1.0f);
        dst[i] = qToBigEndian(static_cast<qint16>(avg * 32767.0f));
    }

    m_txFloatAccumulator.append(mono);

    // Build and send VITA-49 packets: 128 mono int16 samples per packet
    constexpr int MONO_BYTES_PER_PKT = TX_SAMPLES_PER_PACKET * sizeof(qint16);  // 256 bytes
    while (m_txFloatAccumulator.size() >= MONO_BYTES_PER_PKT) {
        const int payloadBytes = MONO_BYTES_PER_PKT;
        const int packetWords = (payloadBytes / 4) + VITA_HEADER_WORDS;
        const int packetBytes = packetWords * 4;

        QByteArray pkt(packetBytes, '\0');
        quint32* words = reinterpret_cast<quint32*>(pkt.data());

        // Header: IFDataWithStream, C=1, TSI=3(Other), TSF=1(SampleCount)
        quint32 hdr = 0;
        hdr |= (0x1u << 28);          // pkt_type = IFDataWithStream
        hdr |= (1u << 27);            // C = 1 (class ID present)
        hdr |= (0x3u << 22);          // TSI = 3 (Other) — matches FlexLib/nDAX
        hdr |= (0x1u << 20);          // TSF = 1 (SampleCount)
        hdr |= ((m_txPacketCount & 0xF) << 16);
        hdr |= (packetWords & 0xFFFF);
        words[0] = qToBigEndian(hdr);
        words[1] = qToBigEndian(m_txStreamId);
        words[2] = qToBigEndian(FLEX_OUI);
        words[3] = qToBigEndian(
            (static_cast<quint32>(FLEX_INFO_CLASS) << 16) | PCC_DAX_REDUCED);
        words[4] = 0;  // integer timestamp (zero)
        words[5] = 0;  // fractional timestamp high (zero)
        words[6] = 0;  // fractional timestamp low (zero)

        // Copy pre-converted big-endian int16 mono payload
        std::memcpy(pkt.data() + VITA_HEADER_BYTES,
                    m_txFloatAccumulator.constData(), payloadBytes);

        m_txPacketCount = (m_txPacketCount + 1) & 0xF;
        emit txPacketReady(pkt);
        m_txFloatAccumulator.remove(0, MONO_BYTES_PER_PKT);
    }
}

void AudioEngine::feedDecodedSpeech(const QByteArray& pcm)
{
    if (!m_audioSink || !m_audioDevice || !m_audioDevice->isOpen()) return;

    // note: RX timer will handle actual audio device write
    if (m_resampleTo48k)
        m_rxBuffer.append(resampleStereo(pcm));
    else
        m_rxBuffer.append(pcm);
}

} // namespace AetherSDR
