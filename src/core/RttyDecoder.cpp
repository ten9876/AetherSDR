#include "RttyDecoder.h"
#include "LogManager.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace AetherSDR {

// Baudot/ITA2 lookup tables
// Index 0-31, LTRS and FIGS shifts
static const char kBaudotLtrs[32] = {
    '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U',   // 0-7
    '\r', 'D',  'R', 'J', 'N', 'F', 'C', 'K',   // 8-15
     'T', 'Z',  'L', 'W', 'H', 'Y', 'P', 'Q',   // 16-23
     'O', 'B',  'G', '\0', 'M', 'X', 'V', '\0'   // 24-31
};

static const char kBaudotFigs[32] = {
    '\0', '3', '\n', '-', ' ', '\a', '8', '7',   // 0-7
    '\r', '$',  '4','\'', ',', '!', ':', '(',     // 8-15
     '5', '"',  ')', '2', '#', '6', '0', '1',    // 16-23
     '9', '?',  '&', '\0', '.', '/', ';', '\0'   // 24-31
};

RttyDecoder::RttyDecoder(QObject* parent)
    : QObject(parent)
{}

RttyDecoder::~RttyDecoder()
{
    stop();
}

void RttyDecoder::start()
{
    if (m_running) return;

    m_running = true;
    m_paramsChanged = true;

    // Reset decode state
    m_markEnv = 0;
    m_spaceEnv = 0;
    m_bitClock = 0;
    m_prevBit = -1;
    m_shiftReg = 0;
    m_bitCount = 0;
    m_inChar = false;
    m_figsMode = false;
    m_markState = {};
    m_spaceState = {};

    {
        QMutexLocker lock(&m_bufMutex);
        m_ringBuf.clear();
    }

    auto* worker = QThread::create([this]() { decodeLoop(); });
    worker->setObjectName("RttyDecoder");
    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    m_workerThread = worker;
    worker->start();

    qCDebug(lcDsp) << "RttyDecoder: started, baud:" << m_baudRate.load()
                    << "shift:" << m_shiftHz.load() << "mark:" << m_markFreqHz.load();
}

void RttyDecoder::stop()
{
    if (!m_running) return;
    m_running = false;

    if (m_workerThread) {
        m_workerThread->wait(2000);
        m_workerThread = nullptr;
    }

    qCDebug(lcDsp) << "RttyDecoder: stopped";
}

void RttyDecoder::setBaudRate(float baud)
{
    m_baudRate = baud;
    m_paramsChanged = true;
}

void RttyDecoder::setShiftHz(int hz)
{
    m_shiftHz = hz;
    m_paramsChanged = true;
}

void RttyDecoder::setMarkFreqHz(int hz)
{
    m_markFreqHz = hz;
    m_paramsChanged = true;
}

void RttyDecoder::setReversePolarity(bool rev)
{
    m_reverse = rev;
    m_paramsChanged = true;
}

void RttyDecoder::feedAudio(const QByteArray& pcm24kStereo)
{
    if (!m_running) return;

    // Downmix stereo float32 → mono float32
    const auto* src = reinterpret_cast<const float*>(pcm24kStereo.constData());
    const int stereoSamples = pcm24kStereo.size() / (2 * static_cast<int>(sizeof(float)));
    QByteArray mono(stereoSamples * static_cast<int>(sizeof(float)), Qt::Uninitialized);
    auto* dst = reinterpret_cast<float*>(mono.data());
    for (int i = 0; i < stereoSamples; ++i) {
        dst[i] = (src[2 * i] + src[2 * i + 1]) * 0.5f;
    }

    QMutexLocker lock(&m_bufMutex);
    m_ringBuf.append(mono);

    // Trim to capacity (drop oldest)
    if (m_ringBuf.size() > RING_CAPACITY) {
        m_ringBuf.remove(0, m_ringBuf.size() - RING_CAPACITY);
    }
}

RttyDecoder::BiquadCoeffs RttyDecoder::designBandpass(double centerHz, double bwHz, double sampleRate)
{
    // Standard biquad bandpass filter (constant 0dB peak gain)
    const double w0 = 2.0 * M_PI * centerHz / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * (centerHz / bwHz)); // Q = center/bw

    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = alpha / a0;
    c.b1 = 0.0;
    c.b2 = -alpha / a0;
    c.a1 = -2.0 * std::cos(w0) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

double RttyDecoder::processBiquad(BiquadCoeffs& c, BiquadState& s, double x)
{
    double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2
             - c.a1 * s.y1 - c.a2 * s.y2;
    s.x2 = s.x1; s.x1 = x;
    s.y2 = s.y1; s.y1 = y;
    return y;
}

void RttyDecoder::recalcFilterCoeffs()
{
    const double markHz = m_markFreqHz.load();
    const double shift = m_shiftHz.load();
    const double spaceHz = markHz - shift; // standard: space is below mark

    // Bandwidth = ~1.5x baud rate for good selectivity
    const double bw = m_baudRate.load() * 1.5;

    m_markCoeffs = designBandpass(markHz, bw, SAMPLE_RATE);
    m_spaceCoeffs = designBandpass(spaceHz, bw, SAMPLE_RATE);

    // Reset filter states on param change
    m_markState = {};
    m_spaceState = {};
    m_markEnv = 0;
    m_spaceEnv = 0;

    qCDebug(lcDsp) << "RttyDecoder: filters recalculated, mark:" << markHz
                    << "space:" << spaceHz << "bw:" << bw;
}

char RttyDecoder::baudotToAscii(int code, bool figs)
{
    if (code < 0 || code > 31) return '\0';
    return figs ? kBaudotFigs[code] : kBaudotLtrs[code];
}

void RttyDecoder::decodeLoop()
{
    // Process audio in chunks of ~10ms (240 samples at 24kHz)
    const int chunkSamples = 240;
    const int chunkBytes = chunkSamples * static_cast<int>(sizeof(float));

    // Envelope follower time constant (~2x bit period for smoothing)
    const double envTau = 2.0 / m_baudRate.load();
    double envAlpha = 1.0 - std::exp(-1.0 / (SAMPLE_RATE * envTau));

    int statsCounter = 0;

    qCDebug(lcDsp) << "RttyDecoder: decode loop running";

    while (m_running) {
        // Check for parameter changes
        if (m_paramsChanged.exchange(false)) {
            recalcFilterCoeffs();
            envAlpha = 1.0 - std::exp(-1.0 / (SAMPLE_RATE * (2.0 / m_baudRate.load())));
        }

        // Grab a chunk of audio
        QByteArray chunk;
        {
            QMutexLocker lock(&m_bufMutex);
            if (m_ringBuf.size() < chunkBytes) {
                lock.unlock();
                QThread::msleep(10);
                continue;
            }
            chunk = m_ringBuf.left(chunkBytes);
            m_ringBuf.remove(0, chunkBytes);
        }

        const auto* samples = reinterpret_cast<const float*>(chunk.constData());
        const double samplesPerBit = SAMPLE_RATE / m_baudRate.load();
        const bool reverse = m_reverse.load();

        for (int i = 0; i < chunkSamples; ++i) {
            double x = samples[i];

            // Bandpass filter for mark and space tones
            double markOut = processBiquad(m_markCoeffs, m_markState, x);
            double spaceOut = processBiquad(m_spaceCoeffs, m_spaceState, x);

            // Envelope detection (rectify + lowpass)
            m_markEnv += envAlpha * (std::abs(markOut) - m_markEnv);
            m_spaceEnv += envAlpha * (std::abs(spaceOut) - m_spaceEnv);

            // Determine current bit: mark=1, space=0
            int bit;
            if (reverse) {
                bit = (m_spaceEnv > m_markEnv) ? 1 : 0;
            } else {
                bit = (m_markEnv > m_spaceEnv) ? 1 : 0;
            }

            // Bit clock recovery using zero-crossing (edge) detection
            if (bit != m_prevBit && m_prevBit >= 0) {
                // Edge detected — resync clock to midpoint of next bit
                // Snap clock to half a bit period from now
                double drift = m_bitClock - std::floor(m_bitClock);
                if (drift > 0.5) drift -= 1.0;
                // Gentle correction: adjust clock by 25% of drift
                m_bitClock -= drift * 0.25;
            }
            m_prevBit = bit;

            // Advance bit clock
            m_bitClock += 1.0 / samplesPerBit;

            // Sample at bit center (when clock crosses integer boundary)
            if (m_bitClock >= 1.0) {
                m_bitClock -= 1.0;

                if (!m_inChar) {
                    // Waiting for start bit (space = 0)
                    if (bit == 0) {
                        m_inChar = true;
                        m_bitCount = 0;
                        m_shiftReg = 0;
                    }
                } else {
                    if (m_bitCount < 5) {
                        // Data bits (LSB first)
                        m_shiftReg |= (bit << m_bitCount);
                        m_bitCount++;
                    } else {
                        // Stop bit (should be mark = 1)
                        // Accept character regardless of stop bit polarity
                        m_inChar = false;

                        // Process the 5-bit character
                        if (m_shiftReg == BAUDOT_LTRS) {
                            m_figsMode = false;
                        } else if (m_shiftReg == BAUDOT_FIGS) {
                            m_figsMode = true;
                        } else if (m_shiftReg != BAUDOT_NULL) {
                            char ch = baudotToAscii(m_shiftReg, m_figsMode);
                            if (ch != '\0' && ch != '\a') {
                                // Confidence based on mark/space SNR
                                double total = m_markEnv + m_spaceEnv;
                                float confidence = 0.5f;
                                if (total > 1e-8) {
                                    double ratio = std::max(m_markEnv, m_spaceEnv) / total;
                                    confidence = static_cast<float>(ratio); // 0.5 = no signal, 1.0 = perfect
                                }
                                emit textDecoded(QString(QChar::fromLatin1(ch)), confidence);
                            }
                        }
                    }
                }
            }
        }

        // Emit stats periodically (~2x per second)
        statsCounter += chunkSamples;
        if (statsCounter >= 12000) {
            statsCounter = 0;
            double total = m_markEnv + m_spaceEnv;
            float snr = 0.0f;
            bool locked = false;
            if (total > 1e-8) {
                double ratio = std::max(m_markEnv, m_spaceEnv) / total;
                snr = static_cast<float>(10.0 * std::log10(ratio / (1.0 - ratio + 1e-12)));
                locked = (snr > 3.0f); // >3dB SNR = locked
            }
            emit statsUpdated(snr, locked);
        }
    }

    qCDebug(lcDsp) << "RttyDecoder: decode loop exiting";
}

} // namespace AetherSDR
