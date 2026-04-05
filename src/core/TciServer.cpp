#ifdef HAVE_WEBSOCKETS
#include "TciServer.h"
#include "TciProtocol.h"
#include "AudioEngine.h"
#include "Resampler.h"
#include "LogManager.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/MeterModel.h"
#include "models/TransmitModel.h"
#include "models/SpotModel.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>
#include <QtEndian>
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

TciServer::TciServer(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
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

    // Periodic status broadcast (200ms — S-meter, TX sensors, TX state)
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(200);
    connect(m_meterTimer, &QTimer::timeout, this, &TciServer::broadcastStatus);
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

    if (!m_server) return;

    for (auto& cs : m_clients) {
        cs.socket->close();
        delete cs.protocol;
        delete cs.resampler;
    }
    m_clients.clear();
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

void TciServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        auto* ws = m_server->nextPendingConnection();
        auto* protocol = new TciProtocol(m_model);

        ClientState cs;
        cs.socket = ws;
        cs.protocol = protocol;
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
            delete m_clients[i].protocol;
            delete m_clients[i].resampler;
            m_clients.removeAt(i);
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

    // TCI messages are semicolon-terminated; may contain multiple commands
    const QStringList cmds = msg.split(';', Qt::SkipEmptyParts);
    for (const auto& cmd : cmds) {
        QString trimmed = cmd.trimmed().toLower();

        // Handle audio start/stop at server level (affects per-client state)
        if (trimmed.startsWith("audio_start")) {
            client.audioEnabled = true;
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
            int fmt = trimmed.mid(colonIdx2 + 1).toInt();
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

    // TCI sends float32 samples (format=3). Convert to float32 stereo for AudioEngine.
    if (hdr.format == 3) {
        int floatCount = payloadBytes / static_cast<int>(sizeof(float));

        if (hdr.channels == 2) {
            // Already float32 stereo — pass directly
            QByteArray pcm(payload, payloadBytes);
            QMetaObject::invokeMethod(m_audio, "feedDaxTxAudio",
                                      Qt::QueuedConnection,
                                      Q_ARG(QByteArray, pcm));
        } else if (hdr.channels == 1) {
            // Mono → duplicate to stereo
            int monoSamples = floatCount;
            QByteArray stereo(monoSamples * 2 * sizeof(float), Qt::Uninitialized);
            auto* dst = reinterpret_cast<float*>(stereo.data());
            auto* src = reinterpret_cast<const float*>(payload);
            for (int i = 0; i < monoSamples; ++i) {
                dst[i * 2]     = src[i];
                dst[i * 2 + 1] = src[i];
            }
            QMetaObject::invokeMethod(m_audio, "feedDaxTxAudio",
                                      Qt::QueuedConnection,
                                      Q_ARG(QByteArray, stereo));
        }
    } else if (hdr.format == 0) {
        // int16 — convert to float32 stereo
        int sampleCount = payloadBytes / static_cast<int>(sizeof(qint16));
        auto* src = reinterpret_cast<const qint16*>(payload);

        if (hdr.channels == 2) {
            QByteArray stereo(sampleCount * sizeof(float), Qt::Uninitialized);
            auto* dst = reinterpret_cast<float*>(stereo.data());
            for (int i = 0; i < sampleCount; ++i)
                dst[i] = src[i] / 32768.0f;
            QMetaObject::invokeMethod(m_audio, "feedDaxTxAudio",
                                      Qt::QueuedConnection,
                                      Q_ARG(QByteArray, stereo));
        } else {
            int monoSamples = sampleCount;
            QByteArray stereo(monoSamples * 2 * sizeof(float), Qt::Uninitialized);
            auto* dst = reinterpret_cast<float*>(stereo.data());
            for (int i = 0; i < monoSamples; ++i) {
                float v = src[i] / 32768.0f;
                dst[i * 2]     = v;
                dst[i * 2 + 1] = v;
            }
            QMetaObject::invokeMethod(m_audio, "feedDaxTxAudio",
                                      Qt::QueuedConnection,
                                      Q_ARG(QByteArray, stereo));
        }
    }
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
    const auto* src = reinterpret_cast<const qint16*>(pcm.constData());
    int totalInt16 = pcm.size() / static_cast<int>(sizeof(qint16));
    int stereoFrames = totalInt16 / 2;

    // Periodic debug log
    static int rxCount = 0;
    if (++rxCount % 1000 == 1)
        qCInfo(lcCat) << "TCI: RX audio" << pcm.size() << "bytes,"
                      << m_clients.size() << "clients";

    for (auto& cs : m_clients) {
        if (!cs.audioEnabled) continue;

        const qint16* audioSrc = src;
        int audioFrames = stereoFrames;
        QByteArray resampledBuf;

        // Resample if client wants a different rate
        if (cs.resampler) {
            resampledBuf = cs.resampler->processStereoToStereo(src, stereoFrames);
            audioSrc = reinterpret_cast<const qint16*>(resampledBuf.constData());
            audioFrames = resampledBuf.size() / (2 * sizeof(qint16));
        }

        int srcSamples = audioFrames * 2;  // stereo

        if (cs.audioFormat == 3) {
            // float32 output
            if (cs.audioChannels == 2) {
                QVector<float> floatBuf(srcSamples);
                for (int i = 0; i < srcSamples; ++i)
                    floatBuf[i] = audioSrc[i] / 32768.0f;
                cs.socket->sendBinaryMessage(
                    buildAudioFrame(0, 1, cs.audioSampleRate, 2,
                                    floatBuf.constData(), audioFrames));
            } else {
                // Mono: average L+R
                QVector<float> monoBuf(audioFrames);
                for (int i = 0; i < audioFrames; ++i)
                    monoBuf[i] = (audioSrc[i*2] + audioSrc[i*2+1]) / 65536.0f;
                cs.socket->sendBinaryMessage(
                    buildAudioFrame(0, 1, cs.audioSampleRate, 1,
                                    monoBuf.constData(), audioFrames));
            }
        } else {
            // int16 output — pack into binary frame with format=0
            if (cs.audioChannels == 2) {
                int payloadBytes = srcSamples * sizeof(qint16);
                QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);
                TciAudioHeader hdr{};
                hdr.receiver = 0;
                hdr.sampleRate = static_cast<quint32>(cs.audioSampleRate);
                hdr.format = 0;  // int16
                hdr.length = static_cast<quint32>(srcSamples);
                hdr.type = 1;    // RX_AUDIO
                hdr.channels = 2;
                std::memcpy(frame.data(), &hdr, sizeof(hdr));
                std::memcpy(frame.data() + sizeof(hdr), audioSrc, payloadBytes);
                cs.socket->sendBinaryMessage(frame);
            } else {
                // Mono int16
                QVector<qint16> monoBuf(audioFrames);
                for (int i = 0; i < audioFrames; ++i)
                    monoBuf[i] = static_cast<qint16>(
                        (audioSrc[i*2] + audioSrc[i*2+1]) / 2);
                int payloadBytes = audioFrames * sizeof(qint16);
                QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);
                TciAudioHeader hdr{};
                hdr.receiver = 0;
                hdr.sampleRate = static_cast<quint32>(cs.audioSampleRate);
                hdr.format = 0;
                hdr.length = static_cast<quint32>(audioFrames);
                hdr.type = 1;
                hdr.channels = 1;
                std::memcpy(frame.data(), &hdr, sizeof(hdr));
                std::memcpy(frame.data() + sizeof(hdr), monoBuf.constData(), payloadBytes);
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

    // Input: int16 stereo, 24 kHz, little-endian
    const auto* src = reinterpret_cast<const qint16*>(pcm.constData());
    int totalInt16 = pcm.size() / static_cast<int>(sizeof(qint16));
    int stereoFrames = totalInt16 / 2;  // L+R pairs

    // Convert int16 stereo → float32 stereo for TCI
    QVector<float> floatBuf(totalInt16);
    for (int i = 0; i < totalInt16; ++i)
        floatBuf[i] = src[i] / 32768.0f;

    // Map DAX channel to TRX: channel 1 → TRX 0, channel 2 → TRX 1, etc.
    int trx = channel - 1;
    if (trx < 0) trx = 0;

    QByteArray frame = buildAudioFrame(trx, 1 /*RX_AUDIO_STREAM*/,
                                       24000, 2, floatBuf.constData(),
                                       stereoFrames);

    // Send to all audio-enabled clients
    for (auto& cs : m_clients) {
        if (cs.audioEnabled)
            cs.socket->sendBinaryMessage(frame);
    }
}

// ── Build TCI binary audio frame ────────────────────────────────────────

QByteArray TciServer::buildAudioFrame(int receiver, int type,
                                      int sampleRate, int channels,
                                      const float* samples, int sampleCount)
{
    // sampleCount = number of frames (mono) or frame pairs (stereo counted as frames)
    int totalFloats = sampleCount * channels;
    int payloadBytes = totalFloats * static_cast<int>(sizeof(float));

    QByteArray frame(sizeof(TciAudioHeader) + payloadBytes, Qt::Uninitialized);

    // Fill header
    TciAudioHeader hdr{};
    hdr.receiver   = static_cast<quint32>(receiver);
    hdr.sampleRate = static_cast<quint32>(sampleRate);
    hdr.format     = 3;  // float32
    hdr.codec      = 0;
    hdr.crc        = 0;
    hdr.length     = static_cast<quint32>(totalFloats);
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
        broadcast(QStringLiteral("trx:%1,%2;")
                      .arg(txTrx)
                      .arg(tx ? "true" : "false"));
        if (tx) {
            long long hz = static_cast<long long>(std::round(txFreqMhz * 1e6));
            broadcast(QStringLiteral("tx_frequency:%1;").arg(hz));
        }
    }
}

} // namespace AetherSDR

#endif // HAVE_WEBSOCKETS
