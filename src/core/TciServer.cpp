#ifdef HAVE_WEBSOCKETS
#include "TciServer.h"
#include "TciProtocol.h"
#include "AudioEngine.h"
#include "AppSettings.h"
#include "Resampler.h"
#include "LogManager.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/DaxIqModel.h"
#include "models/MeterModel.h"
#include "models/TransmitModel.h"
#include "models/SpotModel.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace AetherSDR {

// ── TCI binary audio frame header (per ExpertSDR3 TCI spec v2.0) ────────
// 9 × uint32 = 36 bytes, followed by sample payload
// TCI audio header: 16 × uint32 = 64 bytes
// Per ExpertSDR3 TCI spec v2.0 Stream struct
struct TciAudioHeader {
    quint32 receiver;     // receiver/TRX number
    quint32 sampleRate;   // Hz
    quint32 format;       // 0=int16, 1=int24, 2=int32, 3=float32
    quint32 codec;        // 0 (uncompressed)
    quint32 crc;          // 0 (unused)
    quint32 length;       // number of real samples in data
    quint32 type;         // 0=IQ, 1=RX_AUDIO, 2=TX_AUDIO, 3=TX_CHRONO
    quint32 channels;     // 1 or 2
    quint32 reserved[8];  // zero-filled
};
static_assert(sizeof(TciAudioHeader) == 64, "TCI audio header must be 64 bytes");

namespace {

constexpr int kTxChronoSamples = 2048; // float payload length sent to WSJT-X
constexpr int kTxChronoStereoFrames = kTxChronoSamples / 2;
constexpr qint64 kTxChronoPeriodNs =
    (static_cast<qint64>(kTxChronoStereoFrames) * 1000000000LL) / 48000LL;
constexpr int kTxChronoPollMs = 5;
constexpr qint64 kTxSummaryEveryBlocks = 48;

} // namespace

TciServer::TciServer(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    // Load per-channel RX gains from persistence (decoupled from DaxRxGain<n>, #1627).
    // Migrate DaxRxGain<n> → TciRxGain<n> on first read so existing users keep
    // their current balance when the applets split.
    {
        auto& s = AppSettings::instance();
        for (int ch = 1; ch <= 4; ++ch) {
            const QString key = QStringLiteral("TciRxGain%1").arg(ch);
            if (!s.contains(key)) {
                const QString legacy = s.value(QStringLiteral("DaxRxGain%1").arg(ch), "0.5").toString();
                s.setValue(key, legacy);
            }
            m_rxChannelGain[ch - 1] = std::clamp(
                s.value(key, "0.5").toString().toFloat(), 0.0f, 1.0f);
        }
        s.save();
    }

    // Cache S-meter values for periodic broadcast (avoid flooding clients)
    if (m_model) {
        connect(&m_model->meterModel(), &MeterModel::sLevelChanged,
                this, [this](int sliceIndex, float dbm) {
            if (sliceIndex >= 0 && sliceIndex < 8)
                m_cachedSLevel[sliceIndex] = dbm;
        });
    }

    // Cache TX meter values
    if (m_model) {
        connect(&m_model->meterModel(), &MeterModel::txMetersChanged,
                this, [this](float fwd, float swr) {
            m_cachedFwdPower = fwd;
            m_cachedSwr = swr;
        });
        connect(&m_model->meterModel(), &MeterModel::micMetersChanged,
                this, [this](float micLevel, float, float, float) {
            m_cachedMicLevel = micLevel;
        });
    }

    // Capture DAX RX stream creation responses so we can register them
    // in PanadapterStream for VITA-49 routing (#1331).
    if (m_model) {
        connect(m_model, &RadioModel::statusReceived,
                this, [this](const QString& obj, const QMap<QString,QString>& kvs) {
            if (!obj.startsWith("stream ")) return;
            if (kvs.value("type") != "dax_rx") return;
            quint32 streamId = obj.mid(7).toUInt(nullptr, 16);
            int ch = kvs.value("dax_channel").toInt();
            if (!streamId || ch < 1 || ch > 4) return;
            // Only register if this channel is one we requested (placeholder = 0)
            if (!m_tciDaxStreamIds.contains(ch)) return;
            if (m_tciDaxStreamIds[ch] != 0) return; // already registered
            m_tciDaxStreamIds[ch] = streamId;
            if (m_model->panStream()) {
                m_model->panStream()->registerDaxStream(streamId, ch);
                qCInfo(lcCat) << "TCI: registered DAX RX stream" << Qt::hex << streamId
                              << "for channel" << ch << "(#1331)";
            }
        });
    }

    // Periodic status broadcast (200ms — S-meter, TX sensors, TX state)
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(200);
    connect(m_meterTimer, &QTimer::timeout, this, &TciServer::broadcastStatus);

    // TX_CHRONO timer — sends timing frames to TCI client during TX.
    // WSJT-X only sends TX audio in response to these frames.
    //
    // One TCI TX block is 2048 float samples = 1024 stereo frames at 48 kHz,
    // or 21.333 ms of audio. A fixed 21 ms timer runs ~1.6% fast and warps
    // digital-mode tones, so we poll more frequently and emit frames from a
    // monotonic elapsed-time accumulator.
    m_txChronoTimer = new QTimer(this);
    m_txChronoTimer->setTimerType(Qt::PreciseTimer);
    m_txChronoTimer->setInterval(kTxChronoPollMs);
    connect(m_txChronoTimer, &QTimer::timeout, this, [this]() {
        // Local copy guards against onClientDisconnected nulling the pointer
        // between the check and the send.
        QWebSocket* client = m_txChronoClient;
        if (!client) { m_txChronoTimer->stop(); return; }

        if (!m_txChronoClock.isValid()) {
            m_txChronoClock.start();
            return;
        }

        m_txChronoAccumNs += m_txChronoClock.nsecsElapsed();
        m_txChronoClock.restart();

        while (m_txChronoAccumNs >= kTxChronoPeriodNs) {
            sendTxChronoFrame(client);
            m_txChronoAccumNs -= kTxChronoPeriodNs;
        }
    });
}

TciServer::~TciServer()
{
    stop();
}

bool TciServer::start(quint16 port)
{
    if (m_server)
        return m_server->isListening();

    m_server = new QWebSocketServer(
        QStringLiteral("AetherSDR-TCI"),
        QWebSocketServer::NonSecureMode, this);

    if (!m_server->listen(QHostAddress::Any, port)) {
        qCWarning(lcCat) << "TciServer: failed to listen on port" << port
                         << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QWebSocketServer::newConnection,
            this, &TciServer::onNewConnection);

    m_meterTimer->start();
    qCInfo(lcCat) << "TciServer: listening on port" << m_server->serverPort();
    return true;
}

void TciServer::stop()
{
    m_meterTimer->stop();
    stopTxChrono();

    if (!m_server) return;

    for (auto& cs : m_clients) {
        cs.socket->disconnect(this);   // prevent onClientDisconnected re-entry
        cs.socket->close();
        cs.socket->deleteLater();
        delete cs.protocol;
        delete cs.resampler;
    }
    m_clients.clear();
    releaseDaxForTci();
    emit clientCountChanged(0);

    m_server->close();
    delete m_server;
    m_server = nullptr;

    qCInfo(lcCat) << "TciServer: stopped";
}

bool TciServer::isRunning() const
{
    return m_server && m_server->isListening();
}

quint16 TciServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

void TciServer::setTxGain(float gain)
{
    const float clamped = std::clamp(gain, 0.0f, 1.0f);
    if (m_txGain == clamped) return;
    m_txGain = clamped;
    auto& s = AppSettings::instance();
    s.setValue("TciTxGain", QString::number(clamped, 'f', 2));
    s.save();
}

void TciServer::setRxChannelGain(int channel, float gain)
{
    if (channel < 1 || channel > 4) return;
    const float clamped = std::clamp(gain, 0.0f, 1.0f);
    if (m_rxChannelGain[channel - 1] == clamped) return;
    m_rxChannelGain[channel - 1] = clamped;
    auto& s = AppSettings::instance();
    s.setValue(QStringLiteral("TciRxGain%1").arg(channel),
               QString::number(clamped, 'f', 2));
    s.save();
}

float TciServer::rxChannelGain(int channel) const
{
    if (channel < 1 || channel > 4) return 1.0f;
    return m_rxChannelGain[channel - 1];
}

void TciServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        auto* ws = m_server->nextPendingConnection();
        auto* protocol = new TciProtocol(m_model);

        ClientState cs;
        cs.socket = ws;
        cs.protocol = protocol;
        // Default 48kHz — create 24→48kHz resampler for RX audio
        cs.resampler = new Resampler(24000.0, 48000.0, 4096);
        m_clients.append(cs);

        connect(ws, &QWebSocket::textMessageReceived,
                this, &TciServer::onTextMessage);
        connect(ws, &QWebSocket::binaryMessageReceived,
                this, &TciServer::onBinaryMessage);
        connect(ws, &QWebSocket::disconnected,
                this, &TciServer::onClientDisconnected);

        qCInfo(lcCat) << "TciServer: client connected from"
                      << ws->peerAddress().toString();
        emit clientCountChanged(m_clients.size());

        sendInitBurst(ws);
    }
}

void TciServer::onClientDisconnected()
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) return;

    for (int i = 0; i < m_clients.size(); ++i) {
        if (m_clients[i].socket == ws) {
            // If this client was driving TX_CHRONO, stop and unkey
            if (ws == m_txChronoClient) {
                stopTxChrono();
                if (m_model) {
                    QMetaObject::invokeMethod(m_model, [this]() {
                        m_model->setTransmit(false);
                    }, Qt::QueuedConnection);
                }
            }
            // Clean up IQ stream if this client started one
            if (m_clients[i].iqEnabled && m_model) {
                int ch = m_clients[i].iqChannel + 1;  // TRX 0 → DAX channel 1
                // Only remove if no other client uses the same IQ channel
                bool otherUsing = false;
                for (int j = 0; j < m_clients.size(); ++j) {
                    if (j != i && m_clients[j].iqEnabled &&
                        m_clients[j].iqChannel == m_clients[i].iqChannel) {
                        otherUsing = true;
                        break;
                    }
                }
                if (!otherUsing) {
                    QMetaObject::invokeMethod(m_model, [this, ch]() {
                        m_model->daxIqModel().removeStream(ch);
                    }, Qt::QueuedConnection);
                }
            }
            delete m_clients[i].protocol;
            delete m_clients[i].resampler;
            m_clients.removeAt(i);

            // Release DAX if no remaining clients want audio (#1331)
            bool anyAudio = false;
            for (const auto& cs : m_clients) {
                if (cs.audioEnabled) { anyAudio = true; break; }
            }
            if (!anyAudio) releaseDaxForTci();
            break;
        }
    }

    ws->deleteLater();
    qCInfo(lcCat) << "TciServer: client disconnected,"
                  << m_clients.size() << "remaining";
    emit clientCountChanged(m_clients.size());
}

void TciServer::onTextMessage(const QString& msg)
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) return;

    // Find the client state
    int clientIdx = -1;
    for (int i = 0; i < m_clients.size(); ++i) {
        if (m_clients[i].socket == ws) { clientIdx = i; break; }
    }
    if (clientIdx < 0) return;

    auto& client = m_clients[clientIdx];

    // Raw inbound log — helps diagnose TCI-variant dialects where WSJT-X
    // forks (Improved, Improved Plus, KN4CRD fork…) send commands our
    // parser doesn't match.  Truncate long ones to keep logs readable.
    qCDebug(lcCat) << "TCI rx:" << msg.left(256);

    // TCI messages are semicolon-terminated; may contain multiple commands
    const QStringList cmds = msg.split(';', Qt::SkipEmptyParts);
    for (const auto& cmd : cmds) {
        QString trimmed = cmd.trimmed().toLower();

        // Handle audio start/stop at server level (affects per-client state)
        if (trimmed.startsWith("audio_start")) {
            client.audioEnabled = true;
            ensureDaxForTci();
            ws->sendTextMessage(cmd.trimmed() + ";");
            qCInfo(lcCat) << "TCI: audio started for client"
                          << ws->peerAddress().toString()
                          << "rate=" << client.audioSampleRate
                          << "ch=" << client.audioChannels
                          << "fmt=" << client.audioFormat;
            continue;
        }
        if (trimmed.startsWith("audio_stop")) {
            client.audioEnabled = false;
            // Release DAX if no other clients still want audio
            bool anyAudio = false;
            for (const auto& cs : m_clients) {
                if (cs.audioEnabled) { anyAudio = true; break; }
            }
            if (!anyAudio) releaseDaxForTci();
            ws->sendTextMessage(cmd.trimmed() + ";");
            qCInfo(lcCat) << "TCI: audio stopped for client"
                          << ws->peerAddress().toString();
            continue;
        }

        // Audio format negotiation
        if (trimmed.startsWith("audio_samplerate:")) {
            int colonIdx2 = trimmed.indexOf(':');
            int rate = trimmed.mid(colonIdx2 + 1).toInt();
            if (rate == 8000 || rate == 12000 || rate == 24000 || rate == 48000) {
                client.audioSampleRate = rate;
                // Create or destroy resampler as needed
                delete client.resampler;
                if (rate != 24000)
                    client.resampler = new Resampler(24000.0, rate, 4096);
                else
                    client.resampler = nullptr;
                qCInfo(lcCat) << "TCI: audio sample rate set to" << rate
                              << "for" << ws->peerAddress().toString();
            }
            ws->sendTextMessage(QStringLiteral("audio_samplerate:%1;")
                                    .arg(client.audioSampleRate));
            continue;
        }
        if (trimmed.startsWith("audio_stream_sample_type:")) {
            int colonIdx2 = trimmed.indexOf(':');
            QString fmtStr = trimmed.mid(colonIdx2 + 1).trimmed();
            int fmt;
            if (fmtStr == "float32")
                fmt = 3;
            else if (fmtStr == "int16")
                fmt = 0;
            else
                fmt = fmtStr.toInt();  // numeric value
            if (fmt == 0 || fmt == 3)  // int16 or float32
                client.audioFormat = fmt;
            ws->sendTextMessage(QStringLiteral("audio_stream_sample_type:%1;")
                                    .arg(client.audioFormat));
            continue;
        }
        // Sensor enable/disable
        if (trimmed.startsWith("rx_sensors_enable:")) {
            int colonIdx2 = trimmed.indexOf(':');
            QString val = trimmed.mid(colonIdx2 + 1).split(',').first();
            client.rxSensorsEnabled = (val == "true");
            ws->sendTextMessage(QStringLiteral("rx_sensors_enable:%1;")
                                    .arg(client.rxSensorsEnabled ? "true" : "false"));
            qCInfo(lcCat) << "TCI: rx_sensors" << (client.rxSensorsEnabled ? "enabled" : "disabled");
            continue;
        }
        if (trimmed.startsWith("tx_sensors_enable:")) {
            int colonIdx2 = trimmed.indexOf(':');
            QString val = trimmed.mid(colonIdx2 + 1).split(',').first();
            client.txSensorsEnabled = (val == "true");
            ws->sendTextMessage(QStringLiteral("tx_sensors_enable:%1;")
                                    .arg(client.txSensorsEnabled ? "true" : "false"));
            qCInfo(lcCat) << "TCI: tx_sensors" << (client.txSensorsEnabled ? "enabled" : "disabled");
            continue;
        }

        // IQ start/stop — track per-client IQ state, then forward to protocol
        if (trimmed.startsWith("iq_start:")) {
            int colonIdx2 = trimmed.indexOf(':');
            int trx = trimmed.mid(colonIdx2 + 1).trimmed().toInt();
            client.iqEnabled = true;
            client.iqChannel = trx;
            qCInfo(lcCat) << "TCI: IQ started for client"
                          << ws->peerAddress().toString()
                          << "trx=" << trx;
            // Forward to protocol to create DAX IQ stream on the radio
            QString response = client.protocol->handleCommand(cmd.trimmed());
            if (!response.isEmpty())
                ws->sendTextMessage(response);
            continue;
        }
        if (trimmed.startsWith("iq_stop:")) {
            int colonIdx2 = trimmed.indexOf(':');
            int trx = trimmed.mid(colonIdx2 + 1).trimmed().toInt();
            if (client.iqChannel == trx)
                client.iqEnabled = false;
            qCInfo(lcCat) << "TCI: IQ stopped for client"
                          << ws->peerAddress().toString()
                          << "trx=" << trx;
            QString response = client.protocol->handleCommand(cmd.trimmed());
            if (!response.isEmpty())
                ws->sendTextMessage(response);
            continue;
        }

        if (trimmed.startsWith("audio_stream_samples:")) {
            // Samples per audio packet — acknowledge but we use fixed packet sizes
            ws->sendTextMessage(cmd.trimmed() + ";");
            continue;
        }
        if (trimmed.startsWith("tx_stream_audio_buffering:")) {
            // TX audio buffering in ms — acknowledge
            ws->sendTextMessage(cmd.trimmed() + ";");
            continue;
        }
        if (trimmed.startsWith("line_out_start") ||
            trimmed.startsWith("line_out_stop") ||
            trimmed.startsWith("line_out_recorder")) {
            // Line-out recording — not applicable to FlexRadio, acknowledge
            ws->sendTextMessage(cmd.trimmed() + ";");
            continue;
        }
        if (trimmed.startsWith("audio_stream_channels:")) {
            int colonIdx2 = trimmed.indexOf(':');
            int ch = trimmed.mid(colonIdx2 + 1).toInt();
            if (ch == 1 || ch == 2)
                client.audioChannels = ch;
            ws->sendTextMessage(QStringLiteral("audio_stream_channels:%1;")
                                    .arg(client.audioChannels));
            continue;
        }

        QString response = client.protocol->handleCommand(cmd.trimmed());
        if (!response.isEmpty()) {
            ws->sendTextMessage(response);
            qCDebug(lcCat) << "TCI cmd:" << cmd.trimmed()
                           << "-> resp:" << response.left(80).trimmed();
        }

        // If the command changed radio state, broadcast to all other clients
        QString notification = client.protocol->pendingNotification();
        if (!notification.isEmpty()) {
            for (auto& cs : m_clients) {
                if (cs.socket != ws)
                    cs.socket->sendTextMessage(notification);
            }
        }

        // Start/stop TX_CHRONO when a TCI client sets trx state.
        // WSJT-X only sends TX audio in response to TX_CHRONO (type=3) frames.
        if (trimmed.startsWith("trx:")) {
            int colonIdx2 = trimmed.indexOf(':');
            QStringList parts = trimmed.mid(colonIdx2 + 1).split(',');
            if (parts.size() >= 2) {
                int trx = parts[0].trimmed().toInt();
                bool txOn = (parts[1].trimmed() == "true");
                // Parse optional third argument: audio source / keying origin.
                // WSJT-X/JTDX running in ExpertSDR3 compatibility mode send
                // `trx:<rx>,true,tci`, but they still expect TX_CHRONO timing
                // frames and deliver TX audio over the TCI binary stream.
                // Treat `tci` the same as the legacy empty / `dax` cases.
                //
                // Only explicit hardware-style sources should bypass TX_CHRONO
                // and key the radio directly.
                // Unkey path runs stopTxChrono() + setTransmit(false)
                // unconditionally so either flavor of TX cleans up, even if
                // the client omits arg3 on the release message (#1534).
                QString source;
                if (parts.size() >= 3)
                    source = parts[2].trimmed().toLower().remove(';');
                const bool wantDax = source.isEmpty()
                                  || source == QStringLiteral("dax")
                                  || source == QStringLiteral("tci");
                qCInfo(lcCat) << "TCI: trx request"
                              << "trx=" << trx
                              << "txOn=" << txOn
                              << "source=" << (source.isEmpty() ? QStringLiteral("[default]") : source)
                              << "route=" << (wantDax ? QStringLiteral("tci-audio") : QStringLiteral("radio-direct"));
                if (txOn) {
                    if (wantDax) {
                        startTxChrono(ws, trx);
                    } else {
                        if (m_model) {
                            QMetaObject::invokeMethod(m_model, [this]() {
                                m_model->setTransmit(true);
                            }, Qt::QueuedConnection);
                        }
                    }
                } else {
                    stopTxChrono();
                    if (m_model) {
                        QMetaObject::invokeMethod(m_model, [this]() {
                            m_model->setTransmit(false);
                        }, Qt::QueuedConnection);
                    }
                }
            }
        }
    }
}

// ── Binary message handler (TX audio from TCI client) ───────────────────

void TciServer::onBinaryMessage(const QByteArray& data)
{
    if (!m_audio) return;
    if (data.size() < static_cast<int>(sizeof(TciAudioHeader))) return;

    // Parse header
    TciAudioHeader hdr;
    std::memcpy(&hdr, data.constData(), sizeof(hdr));

    // Only accept TX_AUDIO_STREAM (type 2)
    if (hdr.type != 2) return;

    const int payloadBytes = data.size() - static_cast<int>(sizeof(TciAudioHeader));
    if (payloadBytes <= 0) return;

    const char* payload = data.constData() + sizeof(TciAudioHeader);

    // ── Convert TX audio to float32 stereo ─────────────────────────────────
    // WSJT-X channels field is garbage (FIFO reuse). readAudioData() writes
    // hdr.length floats to data[0..length-1]. Take the first hdr.length floats.
    QByteArray pcm;

    if (hdr.format == 3) {
        int validFloats = static_cast<int>(hdr.length);
        int availFloats = payloadBytes / static_cast<int>(sizeof(float));
        if (validFloats > availFloats) validFloats = availFloats;
        if (validFloats <= 0) return;

        pcm = QByteArray(payload,
                         validFloats * static_cast<int>(sizeof(float)));
    } else if (hdr.format == 0) {
        int validSamples = static_cast<int>(hdr.length);
        int availSamples = payloadBytes / static_cast<int>(sizeof(qint16));
        if (validSamples > availSamples) validSamples = availSamples;
        if (validSamples <= 0) return;

        auto* src = reinterpret_cast<const qint16*>(payload);
        pcm.resize(validSamples * static_cast<int>(sizeof(float)));
        auto* dst = reinterpret_cast<float*>(pcm.data());
        for (int i = 0; i < validSamples; ++i)
            dst[i] = src[i] / 32768.0f;
    }

    if (pcm.isEmpty()) return;

    int inputFrames48k = 0;
    bool duplicatedStereo = false;

    // ─── TX resampling: 48kHz (TCI) → 24kHz (radio native DAX) ───────────
    // Detect mono vs stereo from payload layout.
    //
    // WSJT-X's TCI modulator writes the first `hdr.length` floats as duplicated
    // stereo pairs (L=R), even though the payload buffer it allocates is larger.
    // Treating those `hdr.length` floats as true mono doubles the apparent
    // duration of every block and destroys digital-mode tones.
    if (m_txResampler) {
        int totalFloats = pcm.size() / static_cast<int>(sizeof(float));
        int declaredSamples = static_cast<int>(hdr.length);
        const auto* fSrc = reinterpret_cast<const float*>(pcm.constData());

        if (hdr.format == 3 && totalFloats >= 2 && (totalFloats % 2) == 0) {
            const int pairsToCheck = std::min(totalFloats / 2, 128);
            int duplicatedPairs = 0;
            for (int i = 0; i < pairsToCheck; ++i) {
                if (std::fabs(fSrc[i * 2] - fSrc[i * 2 + 1]) < 1.0e-6f)
                    ++duplicatedPairs;
            }
            duplicatedStereo = duplicatedPairs >= (pairsToCheck * 9) / 10;
        }

        if (duplicatedStereo) {
            // WSJT-X fills `length` floats as stereo pairs in-place.
            int stereoFrames = totalFloats / 2;
            inputFrames48k = stereoFrames;
            pcm = m_txResampler->processStereoToStereo(fSrc, stereoFrames);
        } else if (totalFloats <= declaredSamples) {
            // True mono: upmix to stereo then resample.
            int monoFrames = totalFloats;
            inputFrames48k = monoFrames;
            pcm = m_txResampler->processMonoToStereo(fSrc, monoFrames);
        } else {
            // Explicit stereo: resample directly.
            int stereoFrames = totalFloats / 2;
            inputFrames48k = stereoFrames;
            pcm = m_txResampler->processStereoToStereo(fSrc, stereoFrames);
        }
        if (pcm.isEmpty()) return;
    }

    auto* dst = reinterpret_cast<float*>(pcm.data());
    const int outputStereoFrames = pcm.size() / (2 * static_cast<int>(sizeof(float)));
    const int outputSamples = pcm.size() / static_cast<int>(sizeof(float));
    double sumSq = 0.0;
    float peak = 0.0f;
    qint64 clipSamples = 0;
    for (int i = 0; i < outputSamples; ++i) {
        float v = dst[i] * m_txGain;
        if (v > 1.0f) {
            v = 1.0f;
            ++clipSamples;
        } else if (v < -1.0f) {
            v = -1.0f;
            ++clipSamples;
        }
        dst[i] = v;
        peak = std::max(peak, std::abs(v));
        sumSq += static_cast<double>(v) * static_cast<double>(v);
    }

    ++m_txAudioBlocks;
    m_txInputFrames += inputFrames48k;
    m_txOutputFrames += outputStereoFrames;
    m_txClipSamples += clipSamples;
    m_txAudioSampleCount += outputSamples;
    m_txAudioSumSq += sumSq;
    m_txAudioPeak = std::max(m_txAudioPeak, peak);
    m_txSawDuplicatedStereo = m_txSawDuplicatedStereo || duplicatedStereo;

    if (outputSamples > 0) {
        emit txLevel(std::sqrt(static_cast<float>(sumSq / outputSamples)));
    }

    if ((m_txAudioBlocks % kTxSummaryEveryBlocks) == 0)
        logTxAudioSummary("running");

    QMetaObject::invokeMethod(m_audio, "feedDaxTxAudio",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, pcm));
}

// ── RX audio from main audio pipeline → TCI binary frames ───────────────

void TciServer::onRxAudioReady(const QByteArray& pcm)
{
    // Check if any client has audio enabled
    bool anyAudio = false;
    for (const auto& cs : m_clients) {
        if (cs.audioEnabled) { anyAudio = true; break; }
    }
    if (!anyAudio) return;

    // Input: int16 stereo, 24 kHz, little-endian
    const auto* src = reinterpret_cast<const float*>(pcm.constData());
    int stereoFrames = pcm.size() / (2 * static_cast<int>(sizeof(float)));

    // Periodic debug log
    static int rxCount = 0;
    if (++rxCount % 1000 == 1)
        qCInfo(lcCat) << "TCI: RX audio" << pcm.size() << "bytes,"
                      << m_clients.size() << "clients";

    for (auto& cs : m_clients) {
        if (!cs.audioEnabled) continue;

        const float* audioSrc = src;
        int audioFrames = stereoFrames;
        QByteArray resampledBuf;

        // Resample if client wants a different rate (float32 I/O)
        if (cs.resampler) {
            resampledBuf = cs.resampler->processStereoToStereo(src, stereoFrames);
            audioSrc = reinterpret_cast<const float*>(resampledBuf.constData());
            audioFrames = resampledBuf.size() / (2 * static_cast<int>(sizeof(float)));
        }

        int srcSamples = audioFrames * 2;  // stereo

        if (cs.audioFormat == 3) {
            // float32 output — pass through directly
            if (cs.audioChannels == 2) {
                cs.socket->sendBinaryMessage(
                    buildAudioFrame(0, 1, cs.audioSampleRate, 2,
                                    audioSrc, audioFrames));
            } else {
                // Mono: average L+R
                QVector<float> monoBuf(audioFrames);
                for (int i = 0; i < audioFrames; ++i)
                    monoBuf[i] = (audioSrc[i*2] + audioSrc[i*2+1]) * 0.5f;
                cs.socket->sendBinaryMessage(
                    buildAudioFrame(0, 1, cs.audioSampleRate, 1,
                                    monoBuf.constData(), audioFrames));
            }
        } else {
            // int16 output — convert float32 → int16
            if (cs.audioChannels == 2) {
                int payloadBytes = srcSamples * static_cast<int>(sizeof(qint16));
                QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);
                TciAudioHeader hdr{};
                hdr.receiver = 0;
                hdr.sampleRate = static_cast<quint32>(cs.audioSampleRate);
                hdr.format = 0;  // int16
                hdr.length = static_cast<quint32>(audioFrames * 2);  // total samples (stereo)
                hdr.type = 1;    // RX_AUDIO
                hdr.channels = 2;
                std::memcpy(frame.data(), &hdr, sizeof(hdr));
                auto* i16dst = reinterpret_cast<qint16*>(frame.data() + sizeof(hdr));
                for (int i = 0; i < srcSamples; ++i) {
                    i16dst[i] = static_cast<qint16>(std::clamp(audioSrc[i] * 32768.0f, -32768.0f, 32767.0f));
                }
                cs.socket->sendBinaryMessage(frame);
            } else {
                // Mono int16
                int payloadBytes = audioFrames * static_cast<int>(sizeof(qint16));
                QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);
                TciAudioHeader hdr{};
                hdr.receiver = 0;
                hdr.sampleRate = static_cast<quint32>(cs.audioSampleRate);
                hdr.format = 0;
                hdr.length = static_cast<quint32>(audioFrames);  // total samples (mono = frames)
                hdr.type = 1;
                hdr.channels = 1;
                std::memcpy(frame.data(), &hdr, sizeof(hdr));
                auto* i16dst = reinterpret_cast<qint16*>(frame.data() + sizeof(hdr));
                for (int i = 0; i < audioFrames; ++i)
                    i16dst[i] = static_cast<qint16>(std::clamp(
                        (audioSrc[i*2] + audioSrc[i*2+1]) * 0.5f * 32768.0f, -32768.0f, 32767.0f));
                cs.socket->sendBinaryMessage(frame);
            }
        }
    }
}

// ── RX audio from DAX pipeline → TCI binary frames ─────────────────────

void TciServer::onDaxAudioReady(int channel, const QByteArray& pcm)
{
    // Check if any client has audio enabled
    bool anyAudio = false;
    for (const auto& cs : m_clients) {
        if (cs.audioEnabled) { anyAudio = true; break; }
    }
    if (!anyAudio) return;

    // Map DAX channel → TRX by finding which slice owns this channel.
    int trx = 0;
    if (m_model) {
        for (auto* s : m_model->slices()) {
            if (s->daxChannel() == channel) {
                trx = s->sliceId();
                break;
            }
        }
    }

    // RMS level meter: compute on the raw incoming packet BEFORE per-client
    // gain, so the level reflects radio output, not local slider position.
    // One emission per DAX packet is cheap at ~187 Hz (128-frame packets /24kHz).
    if (channel >= 1 && channel <= 4) {
        const auto* src = reinterpret_cast<const float*>(pcm.constData());
        const int n = pcm.size() / static_cast<int>(sizeof(float));
        if (n > 0) {
            double sumSq = 0.0;
            for (int i = 0; i < n; ++i) sumSq += static_cast<double>(src[i]) * src[i];
            emit rxLevel(channel, std::sqrt(static_cast<float>(sumSq / n)));
        }
    }

    const float channelGain = (channel >= 1 && channel <= 4)
        ? m_rxChannelGain[channel - 1] : 1.0f;

    // Per-client: accumulate then resample
    for (auto& cs : m_clients) {
        if (!cs.audioEnabled) continue;

        // Accumulate DAX packets into a buffer before resampling.
        // DAX delivers ~128-frame packets; r8brain needs larger blocks
        // for clean output without startup transients.
        QByteArray& accumBuf = cs.rxAccumBuf[channel];
        accumBuf.append(pcm);

        int accumFrames = accumBuf.size() / (2 * static_cast<int>(sizeof(float)));

        // If no resampler, flush immediately (native 24kHz pass-through)
        // If resampling, wait for enough data to feed r8brain cleanly
        if (cs.resampler && accumFrames < kAccumMinFrames) {
            continue;
        }

        const float* audioSrc = reinterpret_cast<const float*>(accumBuf.constData());
        int audioFrames = accumFrames;
        QByteArray resampledBuf;

        if (cs.resampler) {
            resampledBuf = cs.resampler->processStereoToStereo(audioSrc, audioFrames);
            audioSrc = reinterpret_cast<const float*>(resampledBuf.constData());
            audioFrames = resampledBuf.size() / (2 * static_cast<int>(sizeof(float)));
        }

        // squeeze() after clear() releases the buffer's heap allocation so
        // idle channels that were reassigned away don't hold kAccumMinFrames
        // worth of memory indefinitely. Cost is a re-alloc on next packet —
        // trivial next to the resampling work that just finished.
        accumBuf.clear();
        accumBuf.squeeze();

        int srcSamples = audioFrames * 2;  // stereo

        // Apply per-channel TCI gain.  Copy into a gained buffer only when the
        // gain is not unity — unity skips the memcpy and keeps audioSrc pointing
        // at the resampler output (or the raw accumulator in the 24kHz path).
        QByteArray gainedBuf;
        if (channelGain != 1.0f) {
            gainedBuf.resize(srcSamples * static_cast<int>(sizeof(float)));
            auto* dst = reinterpret_cast<float*>(gainedBuf.data());
            for (int i = 0; i < srcSamples; ++i) dst[i] = audioSrc[i] * channelGain;
            audioSrc = dst;
        }

        if (cs.audioFormat == 3) {
            // float32 output — pass through directly
            if (cs.audioChannels == 2) {
                cs.socket->sendBinaryMessage(
                    buildAudioFrame(trx, 1, cs.audioSampleRate, 2,
                                    audioSrc, audioFrames));
            } else {
                // Mono: average L+R
                QVector<float> monoBuf(audioFrames);
                for (int i = 0; i < audioFrames; ++i)
                    monoBuf[i] = (audioSrc[i*2] + audioSrc[i*2+1]) * 0.5f;
                cs.socket->sendBinaryMessage(
                    buildAudioFrame(trx, 1, cs.audioSampleRate, 1,
                                    monoBuf.constData(), audioFrames));
            }
        } else {
            // int16 output — convert float32 → int16
            if (cs.audioChannels == 2) {
                int payloadBytes = srcSamples * static_cast<int>(sizeof(qint16));
                QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);
                TciAudioHeader hdr{};
                hdr.receiver = static_cast<quint32>(trx);
                hdr.sampleRate = static_cast<quint32>(cs.audioSampleRate);
                hdr.format = 0;  // int16
                hdr.length = static_cast<quint32>(audioFrames * 2);  // total samples (stereo)
                hdr.type = 1;    // RX_AUDIO
                hdr.channels = 2;
                std::memcpy(frame.data(), &hdr, sizeof(hdr));
                auto* i16dst = reinterpret_cast<qint16*>(frame.data() + sizeof(hdr));
                for (int i = 0; i < srcSamples; ++i) {
                    i16dst[i] = static_cast<qint16>(std::clamp(audioSrc[i] * 32768.0f, -32768.0f, 32767.0f));
                }
                cs.socket->sendBinaryMessage(frame);
            } else {
                // Mono int16
                int payloadBytes = audioFrames * static_cast<int>(sizeof(qint16));
                QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);
                TciAudioHeader hdr{};
                hdr.receiver = static_cast<quint32>(trx);
                hdr.sampleRate = static_cast<quint32>(cs.audioSampleRate);
                hdr.format = 0;
                hdr.length = static_cast<quint32>(audioFrames);  // total samples (mono = frames)
                hdr.type = 1;
                hdr.channels = 1;
                std::memcpy(frame.data(), &hdr, sizeof(hdr));
                auto* i16dst = reinterpret_cast<qint16*>(frame.data() + sizeof(hdr));
                for (int i = 0; i < audioFrames; ++i)
                    i16dst[i] = static_cast<qint16>(std::clamp(
                        (audioSrc[i*2] + audioSrc[i*2+1]) * 0.5f * 32768.0f, -32768.0f, 32767.0f));
                cs.socket->sendBinaryMessage(frame);
            }
        }
    }
}

// ── Build TCI binary audio frame ────────────────────────────────────────

QByteArray TciServer::buildAudioFrame(int receiver, int type,
                                      int sampleRate, int channels,
                                      const float* samples, int sampleCount)
{
    // sampleCount = number of frames (samples per channel)
    int totalFloats = sampleCount * channels;
    int payloadBytes = totalFloats * static_cast<int>(sizeof(float));

    QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);

    // Fill header — length = samples per channel (frames), per TCI v2.0 spec
    TciAudioHeader hdr{};
    hdr.receiver   = static_cast<quint32>(receiver);
    hdr.sampleRate = static_cast<quint32>(sampleRate);
    hdr.format     = 3;  // float32
    hdr.codec      = 0;
    hdr.crc        = 0;
    // length = total number of floats in the data field (not frames).
    // WSJT-X divides this by bytesPerFrame(2) to get stereo frame count.
    hdr.length     = static_cast<quint32>(sampleCount * channels);
    hdr.type       = static_cast<quint32>(type);
    hdr.channels   = static_cast<quint32>(channels);
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    std::memcpy(frame.data(), &hdr, sizeof(hdr));
    std::memcpy(frame.data() + sizeof(hdr), samples, payloadBytes);

    return frame;
}

// ── Wire slice signals for state change broadcasts ──────────────────────

void TciServer::wireSlice(int trx, SliceModel* slice)
{
    if (!slice) return;

    connect(slice, &SliceModel::frequencyChanged, this, [this, trx](double mhz) {
        if (m_clients.isEmpty()) return;
        long long hz = static_cast<long long>(std::round(mhz * 1e6));
        broadcast(QStringLiteral("vfo:%1,0,%2;").arg(trx).arg(hz));
    });

    connect(slice, &SliceModel::modeChanged, this, [this, trx](const QString& mode) {
        if (m_clients.isEmpty()) return;
        broadcast(QStringLiteral("modulation:%1,%2;")
                      .arg(trx).arg(TciProtocol::smartsdrToTci(mode)));
    });

    connect(slice, &SliceModel::filterChanged, this, [this, trx](int lo, int hi) {
        if (m_clients.isEmpty()) return;
        broadcast(QStringLiteral("rx_filter_band:%1,%2,%3;")
                      .arg(trx).arg(lo).arg(hi));
    });

    connect(slice, &SliceModel::txSliceChanged, this, [this, trx](bool tx) {
        if (m_clients.isEmpty()) return;
        broadcast(QStringLiteral("tx_enable:%1,%2;")
                      .arg(trx).arg(tx ? "true" : "false"));
    });

    connect(slice, &SliceModel::lockedChanged, this, [this, trx](bool locked) {
        if (m_clients.isEmpty()) return;
        broadcast(QStringLiteral("lock:%1,%2;")
                      .arg(trx).arg(locked ? "true" : "false"));
    });
}

// ── Wire spot click notifications ───────────────────────────────────────

void TciServer::wireSpotModel()
{
    if (!m_model) return;
    connect(&m_model->spotModel(), &SpotModel::spotTriggered,
            this, [this](int index, const QString&) {
        if (m_clients.isEmpty()) return;
        auto& spots = m_model->spotModel().spots();
        if (!spots.contains(index)) return;
        const auto& spot = spots[index];
        long long hz = static_cast<long long>(spot.rxFreqMhz * 1e6);
        broadcast(QStringLiteral("clicked_on_spot:%1,%2;")
                      .arg(spot.callsign).arg(hz));
    });
}

void TciServer::sendInitBurst(QWebSocket* client)
{
    if (!client || !m_model) return;

    // Find protocol for this client to generate init burst
    TciProtocol* protocol = nullptr;
    for (auto& cs : m_clients) {
        if (cs.socket == client) { protocol = cs.protocol; break; }
    }
    if (!protocol) return;

    // TCI protocol requires one command per WebSocket message.
    // Split the concatenated burst into individual messages.
    QString burst = protocol->generateInitBurst();
    const auto commands = burst.split(';', Qt::SkipEmptyParts);
    for (const auto& cmd : commands)
        client->sendTextMessage(cmd + ';');
    qCDebug(lcCat) << "TCI: sent init burst," << commands.size() << "commands";
}

void TciServer::broadcast(const QString& msg)
{
    for (auto& cs : m_clients)
        cs.socket->sendTextMessage(msg);
}

void TciServer::broadcastBinary(const QByteArray& data)
{
    for (auto& cs : m_clients) {
        if (cs.audioEnabled)
            cs.socket->sendBinaryMessage(data);
    }
}

void TciServer::startTxChrono(QWebSocket* client, int trx)
{
    if (m_txChronoClient) {
        stopTxChrono(); // clean up any previous session
    }
    m_txChronoClient = client;
    m_txChronoTrx = trx;

    // TCI always uses the radio-native DAX TX route (dax=1, int16 mono).
    // The legacy DaxTxLowLatency AppSettings key is retired — its only
    // real consumer was RADE mode, which now controls the route itself
    // via AudioEngine::setRadeMode().  Leaving TCI's route here unconditional
    // guarantees every WSJT-X / digital-mode client keeps the path that
    // works on firmware v4.1.5.
    m_txUseRadioRoute = true;
    // TCI has its own TX gain (decoupled from DaxTxGain) so users who split
    // DAX and TCI routing get independent slider control.  On first read,
    // copy DaxTxGain into TciTxGain so upgrading users see no behavior
    // change — later the DAX/TCI applet split supplies separate UI.
    auto& txGainSettings = AppSettings::instance();
    if (!txGainSettings.contains("TciTxGain")) {
        const QString legacy = txGainSettings.value("DaxTxGain", "0.5").toString();
        txGainSettings.setValue("TciTxGain", legacy);
        txGainSettings.save();
    }
    m_txGain = std::clamp(
        txGainSettings.value("TciTxGain", "0.5").toString().toFloat(),
        0.0f, 1.0f);
    m_txChronoAccumNs = 0;
    m_txChronoRequestedFrames = 0;
    m_txAudioBlocks = 0;
    m_txInputFrames = 0;
    m_txOutputFrames = 0;
    m_txClipSamples = 0;
    m_txAudioSampleCount = 0;
    m_txAudioSumSq = 0.0;
    m_txAudioPeak = 0.0f;
    m_txSawDuplicatedStereo = false;

    // TCI always routes through the radio-native DAX stream (int16 mono,
    // PCC 0x0123) — matches the dax=1 command sent below.
    if (m_audio) {
        m_audio->setDaxTxUseRadioRoute(m_txUseRadioRoute);
        m_audio->setDaxTxMode(true);
    }

    // Create TX resampler: 48kHz (TCI client) → 24kHz (radio DAX/native rate)
    m_txResampler = std::make_unique<Resampler>(48000.0, 24000.0, 4096);
    if (m_model) {
        // Always dax=1 for TCI TX. The DaxTxLowLatency flag only controls
        // VITA-49 packet format (PCC 0x03E3 vs 0x0123 in feedDaxTxAudio);
        // both formats require dax=1 so the radio routes the dax_tx stream
        // to the modulator. Sending dax=0 keeps the radio on the physical
        // mic and silently discards every dax_tx packet. — fw v1.4.0.0
        m_model->sendCmdPublic("transmit set dax=1", nullptr);
    }

    m_txChronoClock.start();
    m_txChronoSessionClock.start();
    m_txChronoTimer->start();
    sendTxChronoFrame(client);
    qCInfo(lcCat) << "TCI: TX_CHRONO started for TRX" << trx
                  << "route=" << (m_txUseRadioRoute ? "radio-dax" : "dax-tx-f32")
                  << "gain=" << m_txGain
                  << "poll_ms=" << kTxChronoPollMs
                  << "target_ms=" << (static_cast<double>(kTxChronoPeriodNs) / 1.0e6);
}

void TciServer::stopTxChrono()
{
    if (!m_txChronoTimer->isActive() && !m_txChronoClient) {
        return;
    }

    logTxAudioSummary("stop");
    m_txChronoTimer->stop();
    m_txChronoClient = nullptr;
    m_txChronoClock.invalidate();
    m_txChronoSessionClock.invalidate();
    m_txChronoAccumNs = 0;

    // Do NOT send `transmit set dax=0` here. The radio's status echo
    // flips m_daxTxMode to false via updateDaxTxMode, which blocks the
    // feedDaxTxAudio gate on the next TX cycle. Leave dax=1 active;
    // voice TX will override when needed. — fw v1.4.0.0
    if (m_audio) {
        m_audio->setDaxTxMode(false);
    }

    m_txResampler.reset();

    qCInfo(lcCat) << "TCI: TX_CHRONO stopped";
}

void TciServer::sendTxChronoFrame(QWebSocket* client)
{
    if (!client) return;

    // TX_CHRONO: header-only, no payload (matches Thetis).
    QByteArray frame(sizeof(TciAudioHeader), '\0');
    TciAudioHeader hdr{};
    hdr.receiver   = static_cast<quint32>(m_txChronoTrx);
    hdr.sampleRate = 48000;
    hdr.format     = 3;                // float32
    hdr.length     = kTxChronoSamples; // matches audio_stream_samples
    hdr.type       = 3;                // TX_CHRONO
    hdr.channels   = 2;
    std::memcpy(frame.data(), &hdr, sizeof(hdr));
    client->sendBinaryMessage(frame);
    m_txChronoRequestedFrames += kTxChronoStereoFrames;
}

void TciServer::logTxAudioSummary(const char* reason)
{
    if (m_txChronoRequestedFrames <= 0 && m_txAudioBlocks <= 0)
        return;

    const double elapsedSec = m_txChronoSessionClock.isValid()
        ? static_cast<double>(m_txChronoSessionClock.nsecsElapsed()) / 1.0e9
        : 0.0;
    const double effectiveRate48k = elapsedSec > 0.0
        ? static_cast<double>(m_txChronoRequestedFrames) / elapsedSec
        : 0.0;
    const double rms = m_txAudioSampleCount > 0
        ? std::sqrt(m_txAudioSumSq / static_cast<double>(m_txAudioSampleCount))
        : 0.0;

    qCInfo(lcCat).nospace()
        << "TCI TX summary reason=" << reason
        << " trx=" << m_txChronoTrx
        << " route=" << (m_txUseRadioRoute ? "radio-dax" : "dax-tx-f32")
        << " gain=" << m_txGain
        << " blocks=" << m_txAudioBlocks
        << " requested48k=" << m_txChronoRequestedFrames
        << " input48k=" << m_txInputFrames
        << " output24k=" << m_txOutputFrames
        << " effective48k=" << effectiveRate48k
        << " peak=" << m_txAudioPeak
        << " rms=" << rms
        << " clips=" << m_txClipSamples
        << " layout=" << (m_txSawDuplicatedStereo ? "duplicated-stereo" : "mono-or-stereo");
}

void TciServer::broadcastStatus()
{
    if (m_clients.isEmpty() || !m_model || !m_model->isConnected())
        return;

    // Broadcast S-meter for each owned slice (throttled to 200ms)
    // TCI spec: rx_smeter:receiver,value; (2 args)
    for (auto* s : m_model->slices()) {
        int trx = s->sliceId();
        if (trx >= 0 && trx < 8) {
            float dbm = m_cachedSLevel[trx];
            if (dbm > -200.0f)
                broadcast(QStringLiteral("rx_smeter:%1,%2;")
                              .arg(trx).arg(static_cast<int>(dbm)));
        }
    }

    // Broadcast RX/TX sensor telemetry to clients that enabled them
    for (auto& cs : m_clients) {
        if (cs.rxSensorsEnabled) {
            for (auto* s : m_model->slices()) {
                int trx = s->sliceId();
                if (trx >= 0 && trx < 8) {
                    float dbm = m_cachedSLevel[trx];
                    if (dbm > -200.0f)
                        cs.socket->sendTextMessage(
                            QStringLiteral("rx_channel_sensors:%1,0,%2;")
                                .arg(trx).arg(dbm, 0, 'f', 1));
                }
            }
        }
        if (cs.txSensorsEnabled && m_model->transmitModel().isTransmitting()) {
            // tx_sensors:trx,mic_dbm,fwd_watts,peak_watts,swr
            cs.socket->sendTextMessage(
                QStringLiteral("tx_sensors:0,%1,%2,%3,%4;")
                    .arg(m_cachedMicLevel, 0, 'f', 1)
                    .arg(m_cachedFwdPower, 0, 'f', 1)
                    .arg(m_cachedFwdPower, 0, 'f', 1)  // peak ≈ avg for now
                    .arg(m_cachedSwr, 0, 'f', 1));
        }
    }

    // Broadcast TX state changes + TX frequency
    bool tx = m_model->transmitModel().isTransmitting();
    if (tx != m_lastTx) {
        m_lastTx = tx;
        int txTrx = 0;
        double txFreqMhz = 0;
        for (auto* s : m_model->slices()) {
            if (s->isTxSlice()) {
                txTrx = s->sliceId();
                txFreqMhz = s->frequency();
                break;
            }
        }
        // Broadcast trx state to all clients EXCEPT the TX_CHRONO initiator.
        // Echoing trx back to the transmitting client (WSJT-X/JTDX) causes
        // it to interpret the echo as an external state change → PTT cycling.
        QString trxMsg = QStringLiteral("trx:%1,%2;")
                             .arg(txTrx).arg(tx ? "true" : "false");
        for (auto& cs : m_clients) {
            if (cs.socket != m_txChronoClient)
                cs.socket->sendTextMessage(trxMsg);
        }
        if (tx) {
            long long hz = static_cast<long long>(std::round(txFreqMhz * 1e6));
            QString freqMsg = QStringLiteral("tx_frequency:%1;").arg(hz);
            for (auto& cs : m_clients) {
                if (cs.socket != m_txChronoClient)
                    cs.socket->sendTextMessage(freqMsg);
            }
        }
    }
}

// ── IQ data from DAX IQ stream → TCI binary frames (type=0) ───────────

void TciServer::onIqDataReady(int channel, const QByteArray& rawPayload, int sampleRate)
{
    // Check if any client wants IQ for this channel
    bool anyIq = false;
    int trx = channel - 1;  // DAX IQ channel 1 → TRX 0
    for (const auto& cs : m_clients) {
        if (cs.iqEnabled && cs.iqChannel == trx) { anyIq = true; break; }
    }
    if (!anyIq) return;

    // Byte-swap big-endian float32 → native little-endian
    const int numFloats = rawPayload.size() / 4;
    QByteArray swapped(rawPayload.size(), Qt::Uninitialized);
    const quint32* src = reinterpret_cast<const quint32*>(rawPayload.constData());
    quint32* dst = reinterpret_cast<quint32*>(swapped.data());
    for (int i = 0; i < numFloats; ++i)
        dst[i] = qFromBigEndian(src[i]);

    // Build TCI IQ binary frame (type=0, channels=2 for I/Q pair)
    const int iqFrames = numFloats / 2;  // I/Q pairs
    QByteArray frame = buildAudioFrame(trx, 0 /*IQ*/, sampleRate, 2,
                                       reinterpret_cast<const float*>(swapped.constData()),
                                       iqFrames);

    for (auto& cs : m_clients) {
        if (cs.iqEnabled && cs.iqChannel == trx)
            cs.socket->sendBinaryMessage(frame);
    }
}

// ── DAX channel management for TCI audio (#1331) ─────────────────────────────
//
// TCI audio feeds from daxAudioReady (not audioDataReady) so that audio_mute
// doesn't kill TCI audio. We auto-assign a DAX channel to each slice that
// doesn't already have one, and release it when the last TCI audio client
// disconnects.

void TciServer::ensureDaxForTci()
{
    if (!m_model || !m_model->isConnected()) return;

    QSet<int> channelsNeeded;

    for (auto* s : m_model->slices()) {
        if (s->daxChannel() == 0) {
            // Slice has no DAX channel — auto-assign one
            QSet<int> used;
            for (auto* sl : m_model->slices()) {
                if (sl->daxChannel() > 0) {
                    used.insert(sl->daxChannel());
                }
            }
            for (int ch = 1; ch <= 4; ++ch) {
                if (!used.contains(ch)) {
                    qCInfo(lcCat) << "TCI: auto-assigning DAX channel" << ch
                                  << "to slice" << s->sliceId() << "for TCI audio (#1331)";
                    s->setDaxChannel(ch);
                    m_tciDaxSlices.insert(s->sliceId());
                    channelsNeeded.insert(ch);
                    break;
                }
            }
        } else {
            // Slice already has a DAX channel (from radio profile) —
            // still need to ensure a stream exists for it.
            channelsNeeded.insert(s->daxChannel());
        }
    }

    // Create DAX RX streams for channels that need them.  If an existing stream
    // is already registered in PanadapterStream (e.g. created by DaxBridge or
    // left over from a previous session), reuse it rather than stacking a
    // second subscription — duplicate streams cause daxAudioReady to fire
    // twice per period, doubling the apparent audio speed at the TCI client.
    for (int ch : channelsNeeded) {
        if (m_tciDaxStreamIds.contains(ch)) continue; // already have/pending
        quint32 existingId = m_model->panStream()
                           ? m_model->panStream()->daxStreamIdForChannel(ch)
                           : 0;
        if (existingId != 0) {
            m_tciDaxStreamIds[ch] = existingId;
            m_tciDaxBorrowedChannels.insert(ch);
            qCInfo(lcCat) << "TCI: reusing existing DAX RX stream" << Qt::hex << existingId
                          << "for channel" << ch << "(#1331)";
        } else {
            m_tciDaxStreamIds[ch] = 0;
            m_model->sendCommand(QString("stream create type=dax_rx dax_channel=%1").arg(ch));
            qCInfo(lcCat) << "TCI: creating DAX RX stream for channel" << ch << "(#1331)";
        }
    }

    // Re-assert slice → DAX channel mapping so the radio registers our
    // stream as a client.  Without this, dax_clients stays 0 and the
    // radio sends silence instead of demodulated audio. (#1439)
    for (auto* s : m_model->slices()) {
        int ch = s->daxChannel();
        if (ch > 0 && channelsNeeded.contains(ch)) {
            m_model->sendCommand(QString("slice set %1 dax=%2")
                .arg(s->sliceId()).arg(ch));
            qCInfo(lcCat) << "TCI: re-asserting dax=" << ch
                          << "on slice" << s->sliceId();
        }
    }
}

void TciServer::releaseDaxForTci()
{
    if (!m_model) return;

    // Remove DAX RX streams we created (skip borrowed streams — owned by DaxBridge
    // or pre-existing; removing them would break other audio consumers).
    for (auto it = m_tciDaxStreamIds.begin(); it != m_tciDaxStreamIds.end(); ++it) {
        int ch = it.key();
        quint32 streamId = it.value();
        if (m_tciDaxBorrowedChannels.contains(ch)) continue;
        if (streamId != 0) {
            if (m_model->panStream()) {
                m_model->panStream()->unregisterDaxStream(streamId);
            }
            if (m_model->isConnected()) {
                m_model->sendCommand(QString("stream remove 0x%1")
                    .arg(streamId, 8, 16, QChar('0')));
            }
            qCInfo(lcCat) << "TCI: removed DAX RX stream" << Qt::hex << streamId
                          << "channel" << ch << "(#1331)";
        }
    }
    m_tciDaxStreamIds.clear();
    m_tciDaxBorrowedChannels.clear();

    // Release DAX channel assignments we made
    for (int sliceId : m_tciDaxSlices) {
        if (auto* s = m_model->slice(sliceId)) {
            qCInfo(lcCat) << "TCI: releasing DAX channel from slice" << sliceId << "(#1331)";
            s->setDaxChannel(0);
        }
    }
    m_tciDaxSlices.clear();
}

} // namespace AetherSDR

#endif // HAVE_WEBSOCKETS
