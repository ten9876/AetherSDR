#include "RadioModel.h"
#include "core/CommandParser.h"
#include "core/AppSettings.h"
#include "core/LogManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDateTime>
#include <QSysInfo>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include "core/AppSettings.h"

namespace AetherSDR {

namespace {

QJsonArray toJsonArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QJsonArray toJsonArray(const QVector<int>& values)
{
    QJsonArray array;
    for (int value : values)
        array.append(value);
    return array;
}

QJsonArray toJsonArray(const QSet<int>& values)
{
    QList<int> sorted = values.values();
    std::sort(sorted.begin(), sorted.end());

    QJsonArray array;
    for (int value : sorted)
        array.append(value);
    return array;
}

QString atuStatusToString(ATUStatus status)
{
    switch (status) {
    case ATUStatus::None:         return "None";
    case ATUStatus::NotStarted:   return "NotStarted";
    case ATUStatus::InProgress:   return "InProgress";
    case ATUStatus::Bypass:       return "Bypass";
    case ATUStatus::Successful:   return "Successful";
    case ATUStatus::OK:           return "OK";
    case ATUStatus::FailBypass:   return "FailBypass";
    case ATUStatus::Fail:         return "Fail";
    case ATUStatus::Aborted:      return "Aborted";
    case ATUStatus::ManualBypass: return "ManualBypass";
    }
    return "Unknown";
}

QJsonObject panToJson(const PanadapterModel* pan, const QString& activePanId)
{
    QJsonObject obj;
    obj["pan_id"] = pan->panId();
    obj["active"] = pan->panId() == activePanId;
    obj["waterfall_id"] = pan->waterfallId();
    obj["center_mhz"] = pan->centerMhz();
    obj["bandwidth_mhz"] = pan->bandwidthMhz();
    obj["min_dbm"] = pan->minDbm();
    obj["max_dbm"] = pan->maxDbm();
    obj["antennas"] = toJsonArray(pan->antList());
    obj["rf_gain"] = pan->rfGain();
    obj["rf_gain_low"] = pan->rfGainLow();
    obj["rf_gain_high"] = pan->rfGainHigh();
    obj["rf_gain_step"] = pan->rfGainStep();
    obj["preamp"] = pan->preamp();
    obj["wnb_active"] = pan->wnbActive();
    obj["wnb_level"] = pan->wnbLevel();
    obj["resized"] = pan->isResized();
    obj["waterfall_configured"] = pan->isWaterfallConfigured();
    return obj;
}

QJsonObject clientInfoToJson(quint32 handle,
                             quint32 ourHandle,
                             quint32 txHandle,
                             const RadioModel::ClientInfo& info)
{
    QJsonObject obj;
    obj["role"] = (handle == ourHandle) ? "current_app" : "other_client";
    obj["owns_tx"] = (txHandle != 0 && handle == txHandle);
    obj["program"] = info.program;
    obj["local_ptt"] = info.localPtt;
    obj["tx_antenna"] = info.txAntenna;
    obj["tx_freq_mhz"] = info.txFreqMhz;
    return obj;
}

} // namespace

RadioModel::RadioModel(QObject* parent)
    : QObject(parent)
{
    // PanadapterStream runs on its own network thread (#502)
    m_networkThread = new QThread(this);
    m_networkThread->setObjectName("PanadapterStream");
    m_panStream = new PanadapterStream;  // no parent — will be moved to thread
    m_panStream->moveToThread(m_networkThread);
    connect(m_networkThread, &QThread::started, m_panStream, &PanadapterStream::init);
    m_networkThread->start();

    // RadioConnection runs on its own worker thread (#502) so TCP I/O
    // (including ping RTT measurement) is never blocked by paintEvent.
    m_connThread = new QThread(this);
    m_connThread->setObjectName("RadioConnection");
    m_connection = new RadioConnection;  // no parent — will be moved to thread
    m_connection->moveToThread(m_connThread);
    connect(m_connThread, &QThread::started, m_connection, &RadioConnection::init);
    m_connThread->start();

    // Signals from RadioConnection auto-queue to main thread (#502)
    connect(m_connection, &RadioConnection::statusReceived,
            this, &RadioModel::onStatusReceived);
    connect(m_connection, &RadioConnection::messageReceived,
            this, &RadioModel::onMessageReceived);
    connect(m_connection, &RadioConnection::connected,
            this, &RadioModel::onConnected);
    connect(m_connection, &RadioConnection::disconnected,
            this, &RadioModel::onDisconnected);
    connect(m_connection, &RadioConnection::errorOccurred,
            this, &RadioModel::onConnectionError);
    connect(m_connection, &RadioConnection::versionReceived,
            this, &RadioModel::onVersionReceived);

    // Response callbacks: RadioConnection emits commandResponse on worker thread,
    // we dispatch to the matching callback on the main thread. (#502)
    connect(m_connection, &RadioConnection::commandResponse,
            this, [this](quint32 seq, int code, const QString& body) {
        auto it = m_pendingCallbacks.find(seq);
        if (it != m_pendingCallbacks.end()) {
            it.value()(code, body);
            m_pendingCallbacks.erase(it);
        }
    });

    // Forward VITA-49 meter packets to MeterModel (cross-thread, auto-queued)
    connect(m_panStream, &PanadapterStream::meterDataReady,
            &m_meterModel, &MeterModel::updateValues);

    // Forward tuner commands to the radio — route through tune inhibit check
    connect(&m_tunerModel, &TunerModel::commandReady, this, [this](const QString& cmd){
        if (cmd.startsWith("tgxl autotune"))
            applyTuneInhibit();
        sendCmd(cmd);
    });

    // Forward DAX IQ commands to the radio
    connect(&m_daxIqModel, &DaxIqModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });

    // Forward transmit model commands to the radio
    connect(&m_transmitModel, &TransmitModel::commandReady, this, [this](const QString& cmd){
        static const QRegularExpression xmitRe(R"(^xmit\s+([01])\s*$)", QRegularExpression::CaseInsensitiveOption);
        const auto match = xmitRe.match(cmd.trimmed());
        if (match.hasMatch()) {
            const bool tx = (match.captured(1) == "1");
            m_txRequested = tx;
            if (!tx && m_txAudioGate) {
                m_txAudioGate = false;
                emit txAudioGateChanged(false);
            }
        }
        if (cmd == "transmit tune 1" || cmd == "atu start")
            applyTuneInhibit();
        sendCmd(cmd);
    });

    // Forward equalizer model commands to the radio
    connect(&m_equalizerModel, &EqualizerModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });

    // Forward TNF model commands to the radio
    connect(&m_tnfModel, &TnfModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });
    connect(&m_cwxModel, &CwxModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });
    connect(&m_dvkModel, &DvkModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });
    connect(&m_usbCableModel, &UsbCableModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });

    // Tune PA inhibit: restore TX outputs when tune completes
    connect(&m_transmitModel, &TransmitModel::tuneChanged, this, [this](bool tuning) {
        if (!tuning && m_tuneInhibitActive && m_tuneInhibitBandId >= 0)
            restoreTuneInhibit();
    });

    m_reconnectTimer.setInterval(5000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_intentionalDisconnect && !m_lastInfo.address.isNull()) {
            qCDebug(lcProtocol) << "RadioModel: auto-reconnecting to" << m_lastInfo.address.toString();
            QMetaObject::invokeMethod(m_connection, [this] {
                m_connection->connectToRadio(m_lastInfo);
            });
        } else {
            m_reconnectTimer.stop();
        }
    });

}

RadioModel::~RadioModel()
{
    // Disconnect all signals BEFORE member destruction to prevent
    // use-after-free (ASAN). (#502)
    QObject::disconnect(m_connection, nullptr, this, nullptr);
    QObject::disconnect(m_panStream, nullptr, this, nullptr);

    // Stop connection thread (#502)
    if (m_connection) {
        QMetaObject::invokeMethod(m_connection, &RadioConnection::disconnectFromRadio,
                                  Qt::BlockingQueuedConnection);
    }
    m_connThread->quit();
    m_connThread->wait(3000);
    delete m_connection;

    // Stop network thread (#502)
    if (m_panStream) {
        QMetaObject::invokeMethod(m_panStream, &PanadapterStream::stop,
                                  Qt::BlockingQueuedConnection);
    }
    m_networkThread->quit();
    m_networkThread->wait(3000);
    delete m_panStream;
}

bool RadioModel::isConnected() const
{
    return m_connection->isConnected() || (m_wanConn && m_wanConn->isConnected());
}

SliceModel* RadioModel::slice(int id) const
{
    for (auto* s : m_slices)
        if (s->sliceId() == id) return s;
    return nullptr;
}

// ─── Actions ──────────────────────────────────────────────────────────────────

void RadioModel::connectToRadio(const RadioInfo& info)
{
    m_wanConn = nullptr;  // LAN mode
    m_lastInfo = info;
    m_intentionalDisconnect = false;
    m_reconnectTimer.stop();
    m_name    = info.name;
    m_model   = info.model;
    m_version = info.version;
    // Populate client station map from discovery data
    m_clientStations.clear();
    m_clientInfoMap.clear();
    for (int i = 0; i < info.guiClientHandles.size(); ++i) {
        quint32 h = info.guiClientHandles[i].toUInt(nullptr, 16);
        QString station = (i < info.guiClientStations.size())
            ? info.guiClientStations[i]
            : (i < info.guiClientPrograms.size() ? info.guiClientPrograms[i] : "Unknown");
        m_clientStations[h] = station;
    }
    QMetaObject::invokeMethod(m_connection, [conn = m_connection, info] {
        conn->connectToRadio(info);
    });
}

void RadioModel::connectViaWan(WanConnection* wan, const QString& publicIp, quint16 udpPort)
{
    qCDebug(lcProtocol) << "RadioModel: connectViaWan publicIp=" << publicIp
             << "udpPort=" << udpPort
             << "wanHandle=0x" << QString::number(wan->clientHandle(), 16);

    // Disconnect any stale signal connections from a previous WAN session
    if (m_wanConn)
        m_wanConn->disconnect(this);

    m_wanConn = wan;
    m_wanPublicIp = publicIp;
    m_wanUdpPort = udpPort;
    m_intentionalDisconnect = false;
    m_reconnectTimer.stop();

    // Wire WAN connection signals (same as RadioConnection)
    connect(wan, &WanConnection::connected, this, &RadioModel::onConnected);
    connect(wan, &WanConnection::disconnected, this, &RadioModel::onDisconnected);
    connect(wan, &WanConnection::errorOccurred, this, &RadioModel::onConnectionError);
    connect(wan, &WanConnection::versionReceived, this, &RadioModel::onVersionReceived);
    connect(wan, &WanConnection::messageReceived, this, &RadioModel::onMessageReceived);
    connect(wan, &WanConnection::statusReceived, this, &RadioModel::onStatusReceived);
    connect(wan, &WanConnection::pingRttMeasured, this, [this](int ms) {
        m_pingMissCount = 0;
        m_lastPingRtt = ms;
        evaluateNetworkQuality();
        emit pingReceived();
    });

    // The WAN connection is already established (TLS + wan validate done)
    // and has already received V/H. Trigger onConnected manually.
    if (wan->isConnected()) {
        qCDebug(lcProtocol) << "RadioModel: WAN already connected, triggering onConnected";
        onConnected();
    } else {
        qCDebug(lcProtocol) << "RadioModel: WAN not yet connected, waiting for connected signal";
    }
}

void RadioModel::disconnectFromRadio()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_pingTimer.stop();
    if (m_wanConn) {
        m_wanConn->disconnect(this);  // remove signal connections to prevent duplicates on reconnect (#224)
        m_wanConn->disconnectFromRadio();
        m_wanConn = nullptr;
    } else {
        QMetaObject::invokeMethod(m_connection, &RadioConnection::disconnectFromRadio);
    }
}

void RadioModel::forceDisconnect()
{
    // Close TCP without setting m_intentionalDisconnect — allows auto-reconnect
    // when the radio reappears in discovery or via the repeating reconnect timer.
    QMetaObject::invokeMethod(m_connection, &RadioConnection::disconnectFromRadio);
}

void RadioModel::setTransmit(bool tx)
{
    // Track local intent so we can keep TX gating aligned with user/PTT edges
    // while radio interlock transitions through intermediate states.
    m_txRequested = tx;

    // Optimistic edge gating:
    // - TX on: start immediately to keep modem waveform aligned with PTT edge.
    // - TX off: stop immediately to avoid "stuck TX tail" during UNKEY_REQUESTED.
    m_transmitModel.setTransmitting(tx);
    if (!tx && m_txAudioGate) {
        m_txAudioGate = false;
        emit txAudioGateChanged(false);
    }

    sendCmd(QString("xmit %1").arg(tx ? 1 : 0));
}

QString RadioModel::audioCompressionParam() const
{
    QString setting = AppSettings::instance().value("AudioCompression", "None").toString();
    if (setting == "Opus") return "opus";
    if (setting == "None") return "none";
    // Auto: use Opus on WAN, uncompressed on LAN
    return isWan() ? "opus" : "none";
}

void RadioModel::sendCwKey(bool down)
{
    QString cmd = QString("cw key %1").arg(down ? 1 : 0);
    sendNetCwCommand(cmd);
}

void RadioModel::sendCwPaddle(bool dit, bool dah)
{
    QString cmd = QString("cw key %1 %2").arg(dit ? 1 : 0).arg(dah ? 1 : 0);
    sendNetCwCommand(cmd);
}

// ── NetCW stream — VITA-49 UDP delivery with redundant sends ────────────────

QByteArray RadioModel::buildNetCwPacket(const QByteArray& payload)
{
    // VITA-49 header: 28 bytes + ASCII command payload
    // Matches FlexLib NetCWStream.AddTXData() packet format
    const int payloadBytes = payload.size();
    const int packetWords = static_cast<int>(std::ceil(payloadBytes / 4.0) + 7); // 7 header words
    const int packetBytes = packetWords * 4;

    QByteArray pkt(packetBytes, '\0');
    auto* w = reinterpret_cast<quint32*>(pkt.data());

    // Word 0: ExtDataWithStream, C=1, T=0, TSI=3(Other), TSF=1(SampleCount)
    static int pktCount = 0;
    quint32 hdr = (0x3u << 28)     // pkt_type = ExtDataWithStream
                | (1u << 27)       // C = 1 (class ID present)
                | (0x3u << 22)     // TSI = 3 (Other)
                | (0x1u << 20)     // TSF = 1 (SampleCount)
                | ((pktCount & 0x0F) << 16)
                | (packetWords & 0xFFFF);
    pktCount = (pktCount + 1) & 0x0F;

    w[0] = qToBigEndian(hdr);
    w[1] = qToBigEndian(m_netCwStreamId);
    w[2] = qToBigEndian<quint32>(0x00001C2D);      // OUI (FlexRadio)
    w[3] = qToBigEndian<quint32>(0x534C03E3);       // ICC=0x534C, PCC=0x03E3
    w[4] = 0; w[5] = 0; w[6] = 0;                  // timestamps

    // Payload: ASCII command string
    memcpy(pkt.data() + 28, payload.constData(), payloadBytes);

    return pkt;
}

void RadioModel::sendNetCwCommand(const QString& baseCmd)
{
    if (m_netCwStreamId == 0) {
        // No netcw stream — fall back to TCP immediate
        sendCmd(baseCmd.contains("cw key") ?
            QString(baseCmd).replace("cw key", "cw key immediate") : baseCmd);
        return;
    }

    // Build the full command with timing metadata and dedup index
    // FlexLib format: "cw key 1 time=0x<hex_ms> index=<N> client_handle=0x<handle>"
    quint64 timeMs = static_cast<quint64>(
        QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    int index = m_netCwIndex++;

    QString fullCmd = QString("%1 time=0x%2 index=%3 client_handle=0x%4")
        .arg(baseCmd)
        .arg(timeMs, 8, 16, QChar('0'))
        .arg(index)
        .arg(clientHandle(), 0, 16);

    QByteArray payload = fullCmd.toLatin1();
    QByteArray packet = buildNetCwPacket(payload);

    // Redundant sends via UDP: 0ms, 5ms, 10ms, 15ms
    // Radio deduplicates by index — processes first arrival, ignores repeats
    m_panStream->sendToRadio(packet);

    QTimer::singleShot(5, this, [this, packet]() {
        m_panStream->sendToRadio(packet);
    });
    QTimer::singleShot(10, this, [this, packet]() {
        m_panStream->sendToRadio(packet);
    });
    QTimer::singleShot(15, this, [this, packet]() {
        m_panStream->sendToRadio(packet);
    });

    // TCP fallback — guarantees delivery if all UDP packets are lost
    sendCmd(fullCmd);
}

void RadioModel::cwAutoTune(int sliceId, bool intermittent)
{
    if (intermittent)
        sendCmd(QString("slice auto_tune %1 int=1").arg(sliceId));
    else
        sendCmd(QString("slice auto_tune %1").arg(sliceId));
}

void RadioModel::addSlice()
{
    if (m_activePanId.isEmpty()) {
        qCWarning(lcProtocol) << "RadioModel::addSlice: no panadapter, cannot create slice";
        return;
    }

    // Create a new slice offset from existing slices so VFO flags deconflict.
    // Use pan center, but if an existing slice is within 5 kHz, offset by
    // 20% of the visible bandwidth.
    auto* pan = activePanadapter();
    double newFreq = pan ? pan->centerMhz() : 14.1;
    const double offsetMhz = (pan ? pan->bandwidthMhz() : 0.2) * 0.2;  // 20% of visible BW
    for (auto* s : m_slices) {
        if (std::abs(s->frequency() - newFreq) < 0.005) {  // within 5 kHz
            newFreq += offsetMhz;
            break;
        }
    }
    const QString freq = QString::number(newFreq, 'f', 6);
    const QString cmd = QString("slice create pan=%1 freq=%2").arg(m_activePanId, freq);

    qCDebug(lcProtocol) << "RadioModel::addSlice:" << cmd;
    sendCmd(cmd, [this](int code, const QString& body) {
        if (code != 0) {
            qCWarning(lcProtocol) << "RadioModel: slice create failed, code"
                       << Qt::hex << code << "body:" << body;
            emit sliceCreateFailed(maxSlices(), m_model);
        } else {
            qCDebug(lcProtocol) << "RadioModel: new slice created, index =" << body;
        }
    });
}

void RadioModel::addSliceOnPan(const QString& panId)
{
    if (panId.isEmpty()) { addSlice(); return; }

    auto* pan = panadapter(panId);
    double newFreq = pan ? pan->centerMhz() : 14.1;
    const double offsetMhz = (pan ? pan->bandwidthMhz() : 0.2) * 0.2;
    for (auto* s : m_slices) {
        if (std::abs(s->frequency() - newFreq) < 0.005) {
            newFreq += offsetMhz;
            break;
        }
    }
    const QString freq = QString::number(newFreq, 'f', 6);
    const QString cmd = QString("slice create pan=%1 freq=%2").arg(panId, freq);

    qCDebug(lcProtocol) << "RadioModel::addSliceOnPan:" << cmd;
    sendCmd(cmd, [this](int code, const QString& body) {
        if (code != 0) {
            qCWarning(lcProtocol) << "RadioModel: slice create failed, code"
                       << Qt::hex << code << "body:" << body;
            emit sliceCreateFailed(maxSlices(), m_model);
        } else {
            qCDebug(lcProtocol) << "RadioModel: new slice created, index =" << body;
        }
    });
}

void RadioModel::createPanadapter()
{
    int limit = maxPanadapters();
    if (static_cast<int>(m_panadapters.size()) >= limit) {
        qCWarning(lcProtocol) << "RadioModel::createPanadapter: limit of" << limit
                              << "panadapters reached for model" << m_model;
        emit panadapterLimitReached(limit, m_model);
        return;
    }
    qCDebug(lcProtocol) << "RadioModel::createPanadapter: sending display panafall create";
    sendCmd("display panafall create x=100 y=100", [this](int code, const QString& body) {
        if (code != 0) {
            qCWarning(lcProtocol) << "RadioModel: panadapter create failed, code"
                       << Qt::hex << code << "body:" << body;
            return;
        }
        // Parse pan ID from response
        QString panId;
        const QMap<QString, QString> kvs = CommandParser::parseKVs(body);
        if (kvs.contains("pan"))       panId = kvs["pan"];
        else if (kvs.contains("id"))   panId = kvs["id"];
        else                           panId = body.trimmed();

        qCDebug(lcProtocol) << "RadioModel: new panadapter created, pan_id =" << panId;

        // The radio will push display pan status for this panId,
        // which triggers PanadapterModel creation in onStatusReceived
        // and emits panadapterAdded. Configure after a short delay.
        if (!panId.isEmpty()) {
            QTimer::singleShot(200, this, [this, panId]() {
                sendCmd(QString("display pan set %1 xpixels=1024 ypixels=700").arg(panId));
                sendCmd(QString("display pan set %1 fps=25 min_dbm=-130 max_dbm=-40").arg(panId));
            });
        }
    });
}

void RadioModel::removePanadapter(const QString& panId)
{
    qCDebug(lcProtocol) << "RadioModel::removePanadapter:" << panId;
    sendCmd(QString("display pan close %1").arg(panId));
    // Radio will send "display pan <id> removed" → handled in onStatusReceived
}

// ── Pan accessor implementations ──────────────────────────────────────────────

PanadapterModel* RadioModel::activePanadapter() const
{
    return m_panadapters.value(m_activePanId, nullptr);
}

PanadapterModel* RadioModel::panadapter(const QString& panId) const
{
    return m_panadapters.value(panId, nullptr);
}

double RadioModel::panCenterMhz() const
{
    auto* p = activePanadapter();
    return p ? p->centerMhz() : 14.1;
}

double RadioModel::panBandwidthMhz() const
{
    auto* p = activePanadapter();
    return p ? p->bandwidthMhz() : 0.2;
}

void RadioModel::setPanBandwidth(double bandwidthMhz)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 bandwidth=%2")
            .arg(m_activePanId).arg(bandwidthMhz, 0, 'f', 6));
}

void RadioModel::setPanCenter(double centerMhz)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 center=%2")
            .arg(m_activePanId).arg(centerMhz, 0, 'f', 6));
}

void RadioModel::setPanDbmRange(float minDbm, float maxDbm)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 min_dbm=%2 max_dbm=%3")
            .arg(m_activePanId)
            .arg(static_cast<double>(minDbm), 0, 'f', 2)
            .arg(static_cast<double>(maxDbm), 0, 'f', 2));
}

void RadioModel::setPanWnb(bool on)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 wnb=%2").arg(m_activePanId).arg(on ? 1 : 0));
}

void RadioModel::setPanWnbLevel(int level)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 wnb_level=%2").arg(m_activePanId).arg(level));
}

void RadioModel::setPanRfGain(int gain)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 rfgain=%2").arg(m_activePanId).arg(gain));
}

// ── Display controls — FFT ─────────────────────────────────────────────────

void RadioModel::setPanAverage(int frames)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 average=%2").arg(m_activePanId).arg(frames));
}

void RadioModel::setPanFps(int fps)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 fps=%2").arg(m_activePanId).arg(fps));
}

void RadioModel::setPanWeightedAverage(bool on)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 weighted_average=%2").arg(m_activePanId).arg(on ? 1 : 0));
}

// ── Display controls — Waterfall ──────────────────────────────────────────

void RadioModel::setWaterfallColorGain(int gain)
{
    if (activeWfId().isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 color_gain=%2").arg(activeWfId()).arg(gain));
}

void RadioModel::setWaterfallBlackLevel(int level)
{
    if (activeWfId().isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 black_level=%2").arg(activeWfId()).arg(level));
}

void RadioModel::setWaterfallAutoBlack(bool on)
{
    Q_UNUSED(on);
    // Auto-black is handled client-side. Always keep radio's auto_black off
    // because its algorithm targets SmartSDR's rendering, not ours.
    if (activeWfId().isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 auto_black=0").arg(activeWfId()));
}

void RadioModel::setWaterfallLineDuration(int ms)
{
    if (activeWfId().isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 line_duration=%2").arg(activeWfId()).arg(ms));
}

void RadioModel::setPanNoiseFloorPosition(int pos)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 noise_floor_position=%2").arg(m_activePanId).arg(pos));
}

void RadioModel::setPanNoiseFloorEnable(bool on)
{
    if (m_activePanId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 noise_floor_position_enable=%2").arg(m_activePanId).arg(on ? 1 : 0));
}

// ─── Connection slots ─────────────────────────────────────────────────────────

void RadioModel::onConnected()
{
    qCDebug(lcProtocol) << "RadioModel: connected";
    setActivePanResized(false);
    emit connectionStateChanged(true);
    // Delay network monitor until after client gui registration
    // (pings sent before registration cause "Malformed command" on WAN)

    // Send low bandwidth flag before GUI registration (matches FlexLib order)
    if (AppSettings::instance().value("LowBandwidthConnect", "False").toString() == "True")
        sendCmd("client low_bw_connect");

    // Register as GUI client FIRST — required before subscriptions,
    // especially on WAN/SmartLink where the radio is stricter.
    const QString clientId = AppSettings::instance().value("GUIClientID").toString();

    if (m_wanConn) {
        // On WAN: wait for client ip response before sending client gui.
        // The radio needs time after wan validate to accept GUI registration.
        sendCmd("client ip", [this, clientId](int, const QString& body) {
            qCDebug(lcProtocol) << "RadioModel: client ip ->" << body.trimmed();
            registerAsGuiClient(clientId);
        });
    } else {
        registerAsGuiClient(clientId);
    }
}

void RadioModel::registerAsGuiClient(const QString& clientId)
{
    sendCmd(QString("client gui %1").arg(clientId), [this](int code, const QString& body) {
        if (code != 0) {
            qCWarning(lcProtocol) << "RadioModel: client gui failed, code" << Qt::hex << code;
        } else if (!body.trimmed().isEmpty()) {
            // Save our UUID for session persistence across restarts.
            // The radio restores slices/frequencies for a known UUID.
            auto& s = AppSettings::instance();
            s.setValue("GUIClientID", body.trimmed());
            s.save();
            qCDebug(lcProtocol) << "RadioModel: saved GUIClientID:" << body.trimmed();
        }

        sendCmd("client program AetherSDR");
        QString station = AppSettings::instance().value("StationName", "AetherSDR").toString();
        sendCmd(QString("client station %1").arg(station));
        sendCmd("client set send_reduced_bw_dax=1");
        // Set network MTU for VITA-49 packets (matches FlexLib behavior)
        int mtu = AppSettings::instance().value("NetworkMtu", "1450").toInt();
        sendCmd(QString("client set enforce_network_mtu=1 network_mtu=%1").arg(mtu));
        // Enable keepalive (matches FlexLib behavior) — ping timer starts in startNetworkMonitor()
        sendCmd("keepalive enable");
        startNetworkMonitor();

    // Full command sequence — each step waits for its R response before sending the next.
    // sub slice all → sub pan all → sub tx all → sub atu all → sub amplifier all
    //   → sub meter all → sub audio all → ...
    sendCmd("sub slice all", [this](int, const QString&) {
      sendCmd("sub pan all", [this](int, const QString&) {
      sendCmd("sub tx all", [this](int, const QString&) {
        sendCmd("sub atu all", [this](int, const QString&) {
        sendCmd("sub amplifier all", [this](int, const QString&) {
          sendCmd("sub meter all", [this](int, const QString&) {
            sendCmd("sub audio all", [this](int, const QString&) {
            sendCmd("sub gps all", [this](int, const QString&) {
            sendCmd("sub apd all", [this](int, const QString&) {
            sendCmd("sub client all", [this](int, const QString&) {
            sendCmd("sub xvtr all", [this](int, const QString&) {
            // Memory status arrives via normal status handler — no subscription needed.
            // "sub memory all" returns 500000A3 (invalid subscription object).
        // Request available mic inputs (comma-separated response: "MIC,BAL,LINE,ACC")
        sendCmd("mic list", [this](int code, const QString& body) {
            if (code == 0) {
                QStringList inputs = body.trimmed().split(',', Qt::SkipEmptyParts);
                m_transmitModel.setMicInputList(inputs);
                qCDebug(lcProtocol) << "RadioModel: mic inputs:" << inputs;
            }
        });

        // Always (re)start on connect — re-binds socket and re-registers
        // UDP port with the radio. start() calls stop() internally if needed. (#561)
        if (m_wanConn) {
            QMetaObject::invokeMethod(m_panStream, [this]() {
                m_panStream->startWan(QHostAddress(m_wanPublicIp), m_wanUdpPort);
            }, Qt::BlockingQueuedConnection);
        } else {
            QMetaObject::invokeMethod(m_panStream, [this]() {
                m_panStream->start(m_connection);
            }, Qt::BlockingQueuedConnection);
        }

        // On WAN: use "client udp_register" via UDP (not TCP "client udpport").
        // The radio only accepts udp_register on WAN connections.
        if (m_wanConn) {
            QMetaObject::invokeMethod(m_panStream, [this]() {
                m_panStream->startWanUdpRegister(clientHandle());
            });
            qCDebug(lcProtocol) << "RadioModel: WAN — started UDP registration via udp_register";
        }

        const quint16 udpPort = m_panStream->localPort();
        sendCmd(
            QString("client udpport %1").arg(udpPort),
            [this, udpPort](int code2, const QString&) {
                if (code2 == 0)
                    qCDebug(lcProtocol) << "RadioModel: UDP port" << udpPort << "registered via client udpport";
                else
                    qCDebug(lcProtocol) << "RadioModel: client udpport returned error" << Qt::hex << code2
                             << "(expected on WAN — using udp_register instead)";

                // Query radio info (region, callsign, options, etc.)
                // Response is comma-separated key=value pairs.
                sendCmd("info",
                    [this](int code, const QString& body) {
                        if (code != 0) return;
                        for (const QString& kv : body.split(',')) {
                            const int eq = kv.indexOf('=');
                            if (eq < 0) continue;
                            const QString key = kv.left(eq).trimmed();
                            const QString val = kv.mid(eq + 1).trimmed()
                                .remove('\\').remove('"');
                            if (key == "callsign")        m_callsign = val;
                            else if (key == "name")        m_nickname = val;
                            else if (key == "region")      m_region = val;
                            else if (key == "options")     m_radioOptions = val;
                            else if (key == "model")       m_model = val;
                            else if (key == "chassis_serial") m_chassisSerial = val;
                            else if (key == "software_ver")   m_version = val;
                            else if (key == "ip")             m_ip = val;
                            else if (key == "netmask")        m_netmask = val;
                            else if (key == "gateway")        m_gateway = val;
                            else if (key == "mac")            m_mac = val;
                        }
                        qCDebug(lcProtocol) << "RadioModel: info — callsign:" << m_callsign
                                 << "region:" << m_region << "options:" << m_radioOptions;
                        emit infoChanged();
                    });

                sendCmd("slice list",
                    [this](int code3, const QString& body) {
                        if (code3 != 0) {
                            qCWarning(lcProtocol) << "RadioModel: slice list failed, code" << Qt::hex << code3;
                            return;
                        }
                        const QStringList ids = body.trimmed().split(' ', Qt::SkipEmptyParts);
                        qCDebug(lcProtocol) << "RadioModel: slice list ->" << (ids.isEmpty() ? "(empty)" : body);

                        if (ids.isEmpty()) {
                            // Radio has no slices at all — create one
                            qCDebug(lcProtocol) << "RadioModel: no slices on radio, creating default";
                            auto& settings = AppSettings::instance();
                            double lastFreq = settings.value("LastFrequency", "0").toDouble();
                            QString lastMode = settings.value("LastMode", "").toString();
                            if (lastFreq > 0.0) {
                                createDefaultSlice(
                                    QString::number(lastFreq, 'f', 6),
                                    lastMode.isEmpty() ? "USB" : lastMode);
                            } else {
                                createDefaultSlice();
                            }
                        } else if (m_slices.isEmpty()) {
                            // Radio has slices but we haven't matched any to our
                            // client_handle yet (status messages still in flight).
                            // Defer the decision to give status messages time to
                            // arrive and populate m_slices via handleSliceStatus.
                            qCDebug(lcProtocol) << "RadioModel: radio has" << ids.size()
                                     << "slice(s) but none matched yet — deferring 500ms";
                            QTimer::singleShot(500, this, [this]() {
                                if (m_slices.isEmpty() && isConnected()) {
                                    qCDebug(lcProtocol) << "RadioModel: deferred check — still no owned slices, creating default";
                                    auto& settings = AppSettings::instance();
                                    double lastFreq = settings.value("LastFrequency", "0").toDouble();
                                    QString lastMode = settings.value("LastMode", "").toString();
                                    if (lastFreq > 0.0) {
                                        createDefaultSlice(
                                            QString::number(lastFreq, 'f', 6),
                                            lastMode.isEmpty() ? "USB" : lastMode);
                                    } else {
                                        createDefaultSlice();
                                    }
                                } else if (!m_slices.isEmpty()) {
                                    qCDebug(lcProtocol) << "RadioModel: deferred check — adopted"
                                             << m_slices.size() << "existing slice(s)";
                                }
                            });
                        } else {
                            qCDebug(lcProtocol) << "RadioModel: SmartConnect — using our pan"
                                     << m_activePanId << "and" << m_slices.size() << "slice(s)";
                        }

                        for (auto* s : m_slices) {
                            for (const QString& cmd : s->drainPendingCommands())
                                sendCmd(cmd);
                        }

                        // Request a remote audio RX stream (uncompressed).
                        // Only create remote_audio_rx if PC audio is enabled.
                        // When disabled, audio plays through the radio's physical
                        // outputs (line out, headphone, front speaker).
                        if (AppSettings::instance().value("PcAudioEnabled", "True").toString() == "True") {
                            sendCmd(
                                QString("stream create type=remote_audio_rx compression=%1").arg(audioCompressionParam()),
                                [this](int code, const QString& body) {
                                    if (code == 0) {
                                        m_rxAudioStreamId = body.trimmed();
                                        qCDebug(lcProtocol) << "RadioModel: remote_audio_rx stream created, id:" << m_rxAudioStreamId;
                                    } else
                                        qCWarning(lcProtocol) << "RadioModel: stream create remote_audio_rx failed, code"
                                                   << Qt::hex << code << "body:" << body;
                                });
                        } else {
                            qCDebug(lcProtocol) << "RadioModel: PC audio disabled, skipping remote_audio_rx (using radio line out)";
                        }

        // Request DAX TX audio stream (PC mic → radio, DAX mode)
                        sendCmd(
                            "stream create type=dax_tx",
                            [this](int code, const QString& body) {
                                if (code == 0) {
                                    quint32 id = body.trimmed().toUInt(nullptr, 16);
                                    qCDebug(lcProtocol) << "RadioModel: dax_tx stream created, id:"
                                             << Qt::hex << id;
                                    emit txAudioStreamReady(id);
                                } else {
                                    qCWarning(lcProtocol) << "RadioModel: dax_tx failed, code"
                                               << Qt::hex << code << "body:" << body;
                                }
                            });

                        // Request remote audio TX stream (voice mode, VOX monitoring)
                        // This stream carries mic audio to the radio for voice TX and
                        // VOX detection. met_in_rx=1 tells the radio to monitor it during RX.
                        // Create netcw stream for low-latency CW keying via UDP
                        sendCmd("stream create netcw",
                            [this](int code, const QString& body) {
                                if (code == 0) {
                                    m_netCwStreamId = body.trimmed().toUInt(nullptr, 16);
                                    m_netCwIndex = 1;
                                    qCDebug(lcProtocol) << "RadioModel: netcw stream created, id:"
                                             << Qt::hex << m_netCwStreamId;
                                } else {
                                    qCDebug(lcProtocol) << "RadioModel: netcw stream not supported, code"
                                             << Qt::hex << code << "— using cw key immediate fallback";
                                }
                            });

                        sendCmd(
                            QString("stream create type=remote_audio_tx compression=%1").arg(audioCompressionParam()),
                            [this](int code, const QString& body) {
                                if (code == 0) {
                                    quint32 id = body.trimmed().toUInt(nullptr, 16);
                                    qCDebug(lcProtocol) << "RadioModel: remote_audio_tx stream created, id:"
                                             << Qt::hex << id;
                                    sendCmd("transmit set met_in_rx=1");
                                    emit remoteTxStreamReady(id);
                                } else {
                                    qCWarning(lcProtocol) << "RadioModel: stream create remote_audio_tx failed, code"
                                               << Qt::hex << code << "body:" << body;
                                }
                            });
                    });
            });
            // Request global profile list
            sendCmd("profile global info");
            sendCmd("sub tnf all");
            sendCmd("sub memories all");
            // Additional subscriptions (matches SmartSDR connection sequence)
            sendCmd("sub cwx all");
            sendCmd("sub dax all");
            sendCmd("sub daxiq all");
            sendCmd("sub radio all");
            sendCmd("sub codec all");
            sendCmd("sub dvk all");
            sendCmd("sub usb_cable all");
            sendCmd("sub spot all");
            sendCmd("sub license all");
            }); // sub xvtr all
            }); // sub client all
            }); // sub apd all
            }); // sub gps all
            }); // sub audio all
          }); // sub meter all
        }); // sub amplifier all
        }); // sub atu all
      }); // sub tx all
      }); // sub pan all
    }); // sub slice all
    }); // client gui
}

int RadioModel::bandIdForFrequency(double freqMhz) const
{
    // Standard amateur HF band ranges → band names matching radio's band_name field
    struct BandRange { double lo; double hi; const char* name; };
    static constexpr BandRange bands[] = {
        {1.8,    2.0,    "160"},
        {3.5,    4.0,    "80"},
        {5.0,    5.5,    "60"},
        {7.0,    7.3,    "40"},
        {10.1,   10.15,  "30"},
        {14.0,   14.35,  "20"},
        {18.068, 18.168, "17"},
        {21.0,   21.45,  "15"},
        {24.89,  24.99,  "12"},
        {28.0,   29.7,   "10"},
        {50.0,   54.0,   "6"},
        {144.0,  148.0,  "2m"},
    };

    for (const auto& b : bands) {
        if (freqMhz >= b.lo && freqMhz <= b.hi) {
            // Find the band ID with this name in m_txBandSettings
            for (auto it = m_txBandSettings.cbegin(); it != m_txBandSettings.cend(); ++it) {
                if (it->bandName == b.name)
                    return it->bandId;
            }
        }
    }
    // Out-of-band or GEN — check for GEN band
    for (auto it = m_txBandSettings.cbegin(); it != m_txBandSettings.cend(); ++it) {
        if (it->bandName == "GEN")
            return it->bandId;
    }
    return -1;
}

void RadioModel::applyTuneInhibit()
{
    auto& s = AppSettings::instance();
    double txFreq = 0.0;
    for (auto* sl : m_slices) {
        if (sl->isTxSlice()) { txFreq = sl->frequency(); break; }
    }
    int bandId = bandIdForFrequency(txFreq);
    if (bandId < 0) return;
    auto it = m_txBandSettings.find(bandId);
    if (it == m_txBandSettings.end()) return;

    QStringList inhibited;
    if (s.value("TuneInhibitAccTx", "False").toString() == "True" && it->accTx) {
        sendCmd(QString("interlock bandset %1 acc_tx_enabled=0").arg(bandId));
        inhibited << "ACC TX";
    }
    if (s.value("TuneInhibitTx1", "False").toString() == "True" && it->tx1) {
        sendCmd(QString("interlock bandset %1 tx1_enabled=0").arg(bandId));
        inhibited << "TX1";
    }
    if (s.value("TuneInhibitTx2", "False").toString() == "True" && it->tx2) {
        sendCmd(QString("interlock bandset %1 tx2_enabled=0").arg(bandId));
        inhibited << "TX2";
    }
    if (s.value("TuneInhibitTx3", "False").toString() == "True" && it->tx3) {
        sendCmd(QString("interlock bandset %1 tx3_enabled=0").arg(bandId));
        inhibited << "TX3";
    }
    if (!inhibited.isEmpty()) {
        m_tuneInhibitBandId = bandId;
        m_tuneInhibitActive = true;
        qDebug() << "Tune PA inhibit: disabled" << inhibited.join(", ")
                 << "on band" << bandId << "before tune";
    }
}

void RadioModel::restoreTuneInhibit()
{
    auto& s = AppSettings::instance();
    int id = m_tuneInhibitBandId;
    QStringList restored;
    if (s.value("TuneInhibitAccTx", "False").toString() == "True") {
        sendCmd(QString("interlock bandset %1 acc_tx_enabled=1").arg(id));
        restored << "ACC TX";
    }
    if (s.value("TuneInhibitTx1", "False").toString() == "True") {
        sendCmd(QString("interlock bandset %1 tx1_enabled=1").arg(id));
        restored << "TX1";
    }
    if (s.value("TuneInhibitTx2", "False").toString() == "True") {
        sendCmd(QString("interlock bandset %1 tx2_enabled=1").arg(id));
        restored << "TX2";
    }
    if (s.value("TuneInhibitTx3", "False").toString() == "True") {
        sendCmd(QString("interlock bandset %1 tx3_enabled=1").arg(id));
        restored << "TX3";
    }
    qDebug() << "Tune PA inhibit: restored" << restored.join(", ") << "on band" << id;
    m_tuneInhibitActive = false;
    m_tuneInhibitBandId = -1;
}

void RadioModel::onDisconnected()
{
    qCDebug(lcProtocol) << "RadioModel: disconnected";

    // Safety: restore TX outputs if we were inhibiting during tune
    if (m_tuneInhibitActive && m_tuneInhibitBandId >= 0)
        restoreTuneInhibit();

    m_txRequested = false;
    if (m_txAudioGate) {
        m_txAudioGate = false;
        emit txAudioGateChanged(false);
    }
    m_transmitModel.setTransmitting(false);
    m_transmitModel.resetState();

    // Reset radio-model-specific state — different radios have different
    // capabilities (APD, max power, pan count, TGXL, amplifier, XVTR, etc.)
    // Must re-derive everything from the new radio's status on next connect. (#359)
    m_tunerModel.setHandle({});       // clear TGXL presence
    m_xvtrList.clear();
    m_hasAmplifier = false;
    m_ampHandle.clear();
    m_ampOperate = false;
    m_fullDuplex = false;
    m_model.clear();
    m_version.clear();
    m_chassisSerial.clear();
    m_callsign.clear();
    m_region.clear();
    m_rxAudioStreamId.clear();
    m_netCwStreamId = 0;
    m_netCwIndex = 1;
    m_lineoutGain = 50;
    m_headphoneGain = 50;

    stopNetworkMonitor();
    // stop() must run on the network thread (socket lives there). (#561)
    QMetaObject::invokeMethod(m_panStream, &PanadapterStream::stop,
                              Qt::BlockingQueuedConnection);
    m_panStream->clearRegisteredStreams();
    // Clean up panadapter models
    qDeleteAll(m_panadapters);
    m_panadapters.clear();
    m_activePanId.clear();
    m_ownedSliceIds.clear();
    if (!m_memories.isEmpty()) {
        m_memories.clear();
        emit memoriesCleared();
    }
    m_clientStations.clear();
    m_clientInfoMap.clear();
    emit otherClientsChanged(0, {});
    emit connectionStateChanged(false);

    if (m_wanConn) {
        qCDebug(lcProtocol) << "RadioModel: WAN disconnected (no auto-reconnect for SmartLink)";
        m_wanConn = nullptr;
    } else if (!m_intentionalDisconnect && !m_lastInfo.address.isNull()) {
        qCDebug(lcProtocol) << "RadioModel: unexpected disconnect — reconnecting in 3s";
        m_reconnectTimer.start();
    }
}

void RadioModel::onConnectionError(const QString& msg)
{
    qCWarning(lcProtocol) << "RadioModel: connection error:" << msg;
    emit connectionError(msg);
    // Don't emit connectionStateChanged here — onDisconnected already handles it.
    // Emitting from both causes duplicate disconnect UI triggers on failed reconnects.
}

void RadioModel::onVersionReceived(const QString& v)
{
    // The V line from the radio is the protocol version (e.g. "1.4.0.0").
    // We prefer the software version from discovery (e.g. "4.1.5").
    // Only use protocol version as fallback if discovery didn't provide one.
    if (m_version.isEmpty())
        m_version = v;
    m_protocolVersion = v;
    emit infoChanged();
}

// ─── Network quality monitor (matches FlexLib MonitorNetworkQuality) ─────────

void RadioModel::startNetworkMonitor()
{
    m_netState = NetState::Excellent;
    m_nextState = NetState::Excellent;
    m_stateCountdown = 0;
    m_lastErrorCount = 0;
    m_lastPingRtt = 0;
    m_pingMissCount = 0;

    // RTT is read from kernel TCP_INFO (smoothed RTT from TCP ACK timing),
    // completely independent of Qt event loop buffering. Falls back to
    // QElapsedTimer stopwatch if the platform kernel call is unavailable.
    connect(m_connection, &RadioConnection::pingRttMeasured, this, [this](int ms) {
        m_pingMissCount = 0;
        m_lastPingRtt = ms;
        evaluateNetworkQuality();
        emit pingReceived();
    });

    connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
        if (!isConnected()) {
            stopNetworkMonitor();
            return;
        }
        ++m_pingMissCount;
        if (m_pingMissCount >= PING_MISS_DISCONNECT) {
            qDebug() << "RadioModel:" << PING_MISS_DISCONNECT
                     << "consecutive pings unanswered — forcing disconnect";
            forceDisconnect();
            return;
        }
        sendCmd("ping");  // RTT measured by RadioConnection::pingRttMeasured
    });
    m_pingTimer.start(1000);
}

void RadioModel::stopNetworkMonitor()
{
    m_pingTimer.stop();
    m_pingTimer.disconnect();
    m_netState = NetState::Off;
    m_nextState = NetState::Off;
}

void RadioModel::evaluateNetworkQuality()
{
    // Check for new packet errors since last evaluation
    const int currentErrors = m_panStream->packetErrorCount();
    const bool packetLost = (currentErrors > m_lastErrorCount);
    m_lastErrorCount = currentErrors;

    const int ping = m_lastPingRtt;

    // State machine from FlexLib MonitorNetworkQualityTask
    switch (m_netState) {
    case NetState::Excellent:
        if (ping >= LAN_PING_POOR_MS)
            m_nextState = NetState::Poor;
        else if (ping >= LAN_PING_FAIR_MS)
            m_nextState = NetState::Good;
        else if (packetLost)
            m_nextState = NetState::VeryGood;
        break;

    case NetState::VeryGood:
        if (ping >= LAN_PING_POOR_MS) {
            m_nextState = NetState::Poor;
            m_stateCountdown = 5;
        } else if (ping >= LAN_PING_FAIR_MS || packetLost) {
            m_nextState = NetState::Good;
            m_stateCountdown = 5;
        } else {
            if (m_stateCountdown-- <= 0) {
                m_nextState = NetState::Excellent;
                m_stateCountdown = 5;
            }
        }
        break;

    case NetState::Good:
        if (ping >= LAN_PING_POOR_MS) {
            m_nextState = NetState::Poor;
            m_stateCountdown = 5;
        } else if (packetLost) {
            m_nextState = NetState::Fair;
            m_stateCountdown = 5;
        } else if (ping < LAN_PING_FAIR_MS) {
            if (m_stateCountdown-- <= 0) {
                m_nextState = NetState::VeryGood;
                m_stateCountdown = 5;
            }
        }
        break;

    case NetState::Fair:
        if (ping >= LAN_PING_POOR_MS || packetLost) {
            m_nextState = NetState::Poor;
            m_stateCountdown = 5;
        } else {
            if (m_stateCountdown-- <= 0) {
                m_nextState = NetState::Good;
                m_stateCountdown = 5;
            }
        }
        break;

    case NetState::Poor:
        if (ping < LAN_PING_POOR_MS) {
            if (m_stateCountdown-- <= 0) {
                m_nextState = NetState::Fair;
                m_stateCountdown = 5;
            }
        }
        break;

    case NetState::Off:
        m_nextState = NetState::Poor;
        m_stateCountdown = 5;
        break;
    }

    m_netState = m_nextState;
    if (ping > m_maxPingRtt) m_maxPingRtt = ping;

    static const char* names[] = {"Off", "Excellent", "Very Good", "Good", "Fair", "Poor"};
    emit networkQualityChanged(names[static_cast<int>(m_netState)], ping);
}

QString RadioModel::networkQuality() const
{
    static const char* names[] = {"Off", "Excellent", "Very Good", "Good", "Fair", "Poor"};
    return names[static_cast<int>(m_netState)];
}

int RadioModel::packetDropCount() const
{
    return m_panStream->packetErrorCount();
}

int RadioModel::packetTotalCount() const
{
    return m_panStream->packetTotalCount();
}

qint64 RadioModel::rxBytes() const
{
    return m_panStream->totalRxBytes();
}

qint64 RadioModel::txBytes() const
{
    return m_panStream->totalTxBytes();
}

PanadapterStream::CategoryStats RadioModel::categoryStats(PanadapterStream::StreamCategory cat) const
{
    return m_panStream->categoryStats(cat);
}

void RadioModel::handleMemoryStatus(int index, const QMap<QString, QString>& kvs)
{
    // Check for removal — radio sends either "in_use=0" or "removed" (no value)
    if (kvs.value("in_use") == "0" || kvs.contains("removed")) {
        m_memories.remove(index);
        emit memoryRemoved(index);
        return;
    }

    auto& m = m_memories[index];
    m.index = index;

    for (auto it = kvs.begin(); it != kvs.end(); ++it) {
        const QString& k = it.key();
        const QString& v = it.value();
        if      (k == "group")           m.group = QString(v).replace('\x7f', ' ');
        else if (k == "owner")           m.owner = QString(v).replace('\x7f', ' ');
        else if (k == "freq")            m.freq = v.toDouble();
        else if (k == "name")            m.name = QString(v).replace('\x7f', ' ');
        else if (k == "mode")            m.mode = v;
        else if (k == "step")            m.step = v.toInt();
        else if (k == "repeater")        m.offsetDir = v;
        else if (k == "repeater_offset") m.repeaterOffset = v.toDouble();
        else if (k == "tone_mode")       m.toneMode = v;
        else if (k == "tone_value")      m.toneValue = v.toDouble();
        else if (k == "squelch")         m.squelch = (v == "1");
        else if (k == "squelch_level")   m.squelchLevel = v.toInt();
        else if (k == "rx_filter_low")   m.rxFilterLow = v.toInt();
        else if (k == "rx_filter_high")  m.rxFilterHigh = v.toInt();
        else if (k == "rtty_mark")       m.rttyMark = v.toInt();
        else if (k == "rtty_shift")      m.rttyShift = v.toInt();
        else if (k == "digl_offset")     m.diglOffset = v.toInt();
        else if (k == "digu_offset")     m.diguOffset = v.toInt();
    }

    emit memoryChanged(index);
}

// ─── Raw message handler (for meter status with '#' separators) ──────────────

void RadioModel::onMessageReceived(const ParsedMessage& msg)
{
    // Meter status uses '#' as KV separator (not spaces), so the normal
    // parseKVs() in CommandParser doesn't handle it.  We intercept the raw
    // status line here and parse it ourselves.
    if (msg.type != MessageType::Status) return;

    // Raw line: "S<handle>|meter 7.src=SLC#7.num=0#7.nam=LEVEL#..."
    const QString& raw = msg.raw;
    const int pipe = raw.indexOf('|');
    if (pipe < 0) return;
    const QString body = raw.mid(pipe + 1);
    // Profile status: "profile tx list=Default^..." or "profile mic list=..."
    // Profile names contain spaces, so parseKVs() (which splits on spaces) breaks
    // the list value.  Handle raw here, same pattern as meter status.
    if (body.startsWith("profile tx ")) {
        handleProfileStatusRaw("tx", body.mid(11));  // skip "profile tx "
        return;
    }
    if (body.startsWith("profile mic ")) {
        handleProfileStatusRaw("mic", body.mid(12));  // skip "profile mic "
        return;
    }
    if (body.startsWith("profile global ")) {
        handleProfileStatusRaw("global", body.mid(15));  // skip "profile global "
        return;
    }

    // GPS status: "gps lat=...#lon=...#grid=...#tracked=...#visible=...#status=..."
    if (body.startsWith("gps ")) {
        handleGpsStatus(body.mid(4));  // skip "gps "
        return;
    }

    if (!body.startsWith("meter ")) return;

    handleMeterStatus(body.mid(6));  // skip "meter "
}

// ─── Status dispatch ──────────────────────────────────────────────────────────
//
// Object strings look like:
//   "radio"           → global radio properties
//   "slice 0"         → slice receiver
//   "panadapter 0"    → panadapter (spectrum)
//   "meter 1"         → meter reading (handled by onMessageReceived)
//   "removed=True"    → object was removed

void RadioModel::sendCommand(const QString& cmd)
{
    qCDebug(lcProtocol) << "RadioModel::sendCommand:" << cmd
             << "connected:" << isConnected() << "wan:" << (m_wanConn != nullptr);
    this->sendCmd(cmd);
}

void RadioModel::sendCmdPublic(const QString& cmd, ResponseCallback cb)
{
    sendCmd(cmd, cb);
}

void RadioModel::setAmpOperate(bool on)
{
    if (m_ampHandle.isEmpty()) return;
    // FlexLib API: "amplifier set <handle> operate=0|1"
    sendCmd(QString("amplifier set %1 operate=%2").arg(m_ampHandle).arg(on ? 1 : 0));
}

void RadioModel::createRxAudioStream()
{
    if (!m_rxAudioStreamId.isEmpty()) return;  // already exists
    sendCmd(QString("stream create type=remote_audio_rx compression=%1").arg(audioCompressionParam()),
        [this](int code, const QString& body) {
            if (code == 0) {
                m_rxAudioStreamId = body.trimmed();
                qCDebug(lcProtocol) << "RadioModel: remote_audio_rx stream created, id:" << m_rxAudioStreamId;
            } else {
                qCWarning(lcProtocol) << "RadioModel: stream create remote_audio_rx failed, code"
                           << Qt::hex << code << "body:" << body;
            }
        });
}

void RadioModel::removeRxAudioStream()
{
    if (m_rxAudioStreamId.isEmpty()) return;
    sendCmd(QString("stream remove 0x%1").arg(m_rxAudioStreamId));
    qCDebug(lcProtocol) << "RadioModel: removed remote_audio_rx stream" << m_rxAudioStreamId;
    m_rxAudioStreamId.clear();
}

quint32 RadioModel::sendCmd(const QString& command, ResponseCallback cb)
{
    if (m_wanConn)
        return m_wanConn->sendCommand(command, std::move(cb));

    // Allocate seq on main thread, store callback locally. (#502)
    const quint32 seq = m_seqCounter.fetch_add(1);
    if (cb)
        m_pendingCallbacks.insert(seq, std::move(cb));

    // Queue the socket write on the connection's worker thread.
    QMetaObject::invokeMethod(m_connection, [conn = m_connection, seq, command] {
        conn->writeCommand(seq, command);
    });
    return seq;
}

quint32 RadioModel::clientHandle() const
{
    if (m_wanConn)
        return m_wanConn->clientHandle();
    return m_connection->clientHandle();
}

quint32 RadioModel::ourClientHandle() const { return clientHandle(); }

void RadioModel::emitOtherClientsChanged()
{
    quint32 ours = clientHandle();
    QStringList names;
    for (auto it = m_clientStations.cbegin(); it != m_clientStations.cend(); ++it) {
        if (it.key() != ours)
            names << it.value();
    }
    emit otherClientsChanged(names.size(), names);
}

void RadioModel::onStatusReceived(const QString& object,
                                  const QMap<QString, QString>& kvs)
{
    // Relay to listeners (e.g., MemoryDialog)
    emit statusReceived(object, kvs);

    if (object == "radio") {
        handleRadioStatus(kvs);
        return;
    }

    // Client connected/disconnected:
    //   object="client 0x7594C952"       kvs={connected, program=SmartSDR, station=W1AW}
    //   object="client 0x7594C952 disconnected"  kvs={forced=0, ...}
    static const QRegularExpression clientRe(R"(^client\s+(0x[0-9A-Fa-f]+)(?:\s+(\w+))?$)");
    if (object.startsWith("client 0x")) {
        const auto cm = clientRe.match(object);
        if (cm.hasMatch()) {
            quint32 handle = cm.captured(1).toUInt(nullptr, 16);
            QString action = cm.captured(2);  // "disconnected" or empty

            if (action == "disconnected") {
                m_clientStations.remove(handle);
                m_clientInfoMap.remove(handle);
                emitOtherClientsChanged();
            } else if (action == "connected" || kvs.contains("connected")) {
                QString program = kvs.value("program", "Unknown");
                QString station = kvs.value("station", program);
                bool ptt = kvs.value("local_ptt", "0") == "1";
                m_clientStations[handle] = station;
                m_clientInfoMap[handle] = {station, program, ptt};
                emitOtherClientsChanged();
            }
        }
        return;
    }

    // XVTR status: "xvtr 0 name=2m rf_freq=144.000000 if_freq=28.000000 ..."
    static const QRegularExpression xvtrRe(R"(^xvtr\s+(\d+)$)");
    if (object.startsWith("xvtr")) {
        const auto m = xvtrRe.match(object);
        if (m.hasMatch()) {
            int idx = m.captured(1).toInt();
            // "in_use=0" means the xvtr was removed
            if (kvs.contains("in_use") && kvs["in_use"] == "0") {
                m_xvtrList.remove(idx);
                emit infoChanged();
                return;
            }
            auto& x = m_xvtrList[idx];
            x.index = idx;
            if (kvs.contains("name"))      x.name     = kvs["name"];
            if (kvs.contains("rf_freq"))   x.rfFreq   = kvs["rf_freq"].toDouble();
            if (kvs.contains("if_freq"))   x.ifFreq   = kvs["if_freq"].toDouble();
            if (kvs.contains("lo_error"))  x.loError  = kvs["lo_error"].toDouble();
            if (kvs.contains("rx_gain"))   x.rxGain   = kvs["rx_gain"].toDouble();
            if (kvs.contains("max_power")) x.maxPower = kvs["max_power"].toDouble();
            if (kvs.contains("rx_only"))   x.rxOnly   = kvs["rx_only"] == "1";
            if (kvs.contains("is_valid"))  x.isValid  = kvs["is_valid"] == "1";
            emit infoChanged();
        }
        return;
    }

    // Filter sharpness: "radio filter_sharpness VOICE level=3 auto_level=0"
    if (object.startsWith("radio filter_sharpness")) {
        int level = kvs.value("level", "-1").toInt();
        bool autoLvl = kvs.value("auto_level", "0") == "1";
        if (object.contains("VOICE"))       { m_filterVoice = level; m_filterVoiceAuto = autoLvl; }
        else if (object.contains("CW"))     { m_filterCw = level; m_filterCwAuto = autoLvl; }
        else if (object.contains("DIGITAL")){ m_filterDigital = level; m_filterDigitalAuto = autoLvl; }
        emit infoChanged();
        return;
    }

    if (object == "radio oscillator") {
        if (kvs.contains("state"))        m_oscState    = kvs["state"];
        if (kvs.contains("setting"))      m_oscSetting  = kvs["setting"];
        if (kvs.contains("locked"))       m_oscLocked   = kvs["locked"] == "1";
        if (kvs.contains("ext_present"))  m_extPresent  = kvs["ext_present"] == "1";
        if (kvs.contains("gpsdo_present"))m_gpsdoPresent= kvs["gpsdo_present"] == "1";
        if (kvs.contains("tcxo_present")) m_tcxoPresent = kvs["tcxo_present"] == "1";
        if (kvs.contains("gnss_present")) m_gpsdoPresent= m_gpsdoPresent || kvs["gnss_present"] == "1";
        emit infoChanged();
        return;
    }

    if (object == "radio static_net_params") {
        m_staticIp      = kvs.value("ip");
        m_staticNetmask = kvs.value("netmask");
        m_staticGateway = kvs.value("gateway");
        m_hasStaticIp   = !m_staticIp.isEmpty();
        emit infoChanged();
        return;
    }

    static const QRegularExpression sliceRe(R"(^slice\s+(\d+)$)");
    const auto sliceMatch = sliceRe.match(object);
    if (sliceMatch.hasMatch()) {
        // Extract per-client TX info for multiFLEX dashboard before
        // handleSliceStatus filters out other clients' slices
        if (kvs.contains("client_handle") && kvs.value("tx") == "1") {
            quint32 ch = kvs["client_handle"].toUInt(nullptr, 16);
            auto it = m_clientInfoMap.find(ch);
            if (it != m_clientInfoMap.end()) {
                it->txAntenna = kvs.value("txant");
                if (kvs.contains("RF_frequency"))
                    it->txFreqMhz = kvs["RF_frequency"].toDouble();
            }
        }
        const bool removed = kvs.value("in_use") == "0";
        handleSliceStatus(sliceMatch.captured(1).toInt(), kvs, removed);
        return;
    }

    // Memory channels: "memory <index> key=val ..." or "memory <index> removed"
    // When there are no KV pairs (e.g., "memory 7 removed"), the parser puts
    // everything into the object name. Extract the index from the first token.
    if (object.startsWith("memory ")) {
        const QString rest = object.mid(7);  // "7 removed" or "7"
        const int sp = rest.indexOf(' ');
        const QString idxStr = (sp >= 0) ? rest.left(sp) : rest;
        bool ok;
        int idx = idxStr.toInt(&ok);
        if (ok) {
            // Merge any trailing bare words into kvs
            QMap<QString, QString> merged = kvs;
            if (sp >= 0) {
                const QString extra = rest.mid(sp + 1);
                for (const auto& token : extra.split(' ', Qt::SkipEmptyParts)) {
                    const int eq = token.indexOf('=');
                    if (eq < 0)
                        merged.insert(token, QString{});
                    else
                        merged.insert(token.left(eq), token.mid(eq + 1));
                }
            }
            qCDebug(lcProtocol) << "RadioModel: memory status for index" << idx
                     << "keys:" << merged.keys();
            handleMemoryStatus(idx, merged);
            return;
        }
    }

    // Meter status uses '#'-separated tokens and is handled by onMessageReceived().

    // "display pan 0x40000000 center=14.1 bandwidth=0.2 ..."
    // Only process status for OUR panadapter (matching client_handle or first unclaimed).
    static const QRegularExpression panRe(R"(^display pan\s+(0x[0-9A-Fa-f]+))");
    if (object.startsWith("display pan")) {
        const auto m = panRe.match(object);
        if (m.hasMatch()) {
            const QString panId = m.captured(1);

            // Handle pan removal — "display pan 0x40000001 removed" arrives
            // with no '=' so the parser puts the whole string in 'object'
            if (kvs.contains("removed") || object.endsWith("removed")) {
                auto* pan = m_panadapters.take(panId);
                if (pan) {
                    m_panStream->unregisterPanStream(pan->panStreamId());
                    m_panStream->unregisterWfStream(pan->wfStreamId());
                    qCDebug(lcProtocol) << "RadioModel: panadapter removed" << panId;
                    emit panadapterRemoved(panId);
                    pan->deleteLater();
                }
                if (m_activePanId == panId) {
                    m_activePanId = m_panadapters.isEmpty() ? QString()
                                                            : m_panadapters.firstKey();
                }
                return;
            }

            // Preamp is shared antenna hardware — apply to ALL our pans
            // regardless of which client's pan status this came from.
            if (kvs.contains("pre")) {
                const QString pre = kvs["pre"];
                for (auto* pan : m_panadapters)
                    pan->setPreamp(pre);
            }

            if (!m_panadapters.contains(panId)) {
                // Only create PanadapterModel when client_handle is present
                // AND matches our handle. Early status messages may arrive
                // without client_handle — defer until ownership is confirmed.
                // This prevents briefly adopting another Multi-Flex client's pan.
                if (!kvs.contains("client_handle"))
                    return;  // defer — can't confirm ownership yet
                quint32 owner = kvs["client_handle"].toUInt(nullptr, 16);
                if (owner != clientHandle())
                    return;  // not our panadapter, ignore

                auto* pan = new PanadapterModel(panId, this);
                pan->setClientHandle(QString::number(clientHandle(), 16));
                m_panadapters[panId] = pan;
                if (m_activePanId.isEmpty())
                    m_activePanId = panId;
                // Re-register stream IDs when waterfall ID arrives (it's not
                // available at pan creation time — comes later in display pan status)
                connect(pan, &PanadapterModel::waterfallIdChanged,
                        this, &RadioModel::updateStreamFilters);
                updateStreamFilters();
                // Query RF gain range from radio (varies by model)
                sendCmd(QString("display pan rfgain_info %1").arg(panId),
                        [pan](int code, const QString& body) {
                    if (code != 0 || body.isEmpty()) return;
                    QStringList vals = body.split(',');
                    if (vals.size() < 3) return;
                    int low = vals[0].trimmed().toInt();
                    int high = vals[1].trimmed().toInt();
                    int step = vals[2].trimmed().toInt();
                    if (step > 0)
                        pan->setRfGainInfo(low, high, step);
                });
                qCDebug(lcProtocol) << "RadioModel: claimed panadapter" << panId;
                emit panadapterAdded(pan);
            }
            handlePanadapterStatus(panId, kvs);
        }
        return;
    }

    // "display waterfall 0x42000000 auto_black=1 ..."
    // Only process status for OUR waterfall (matching client_handle).
    static const QRegularExpression wfRe(R"(^display waterfall\s+(0x[0-9A-Fa-f]+)$)");
    if (object.startsWith("display waterfall")) {
        const auto m = wfRe.match(object);
        if (m.hasMatch()) {
            const QString wfId = m.captured(1);
            // Check if this waterfall belongs to one of our panadapters.
            // The waterfallId is set on PanadapterModel by the "display pan" status
            // message which contains "waterfall=0x42xxxxxx".
            bool ours = false;
            for (auto* pan : m_panadapters) {
                if (pan->waterfallId() == wfId) { ours = true; break; }
            }
            if (!ours) {
                // Not yet associated via display pan status — check client_handle
                if (!kvs.contains("client_handle"))
                    return;  // defer — can't confirm ownership yet
                quint32 owner = kvs["client_handle"].toUInt(nullptr, 16);
                if (owner != clientHandle())
                    return;  // not our waterfall
                // Own it but don't force-associate — the display pan status
                // will set the correct waterfallId on the right pan.
                ours = true;
            }

            if (activeWfId().isEmpty())
                setActiveWfId(wfId);
            updateStreamFilters();
            qCDebug(lcProtocol) << "RadioModel: claimed waterfall" << wfId;
        }
        if (!activeWfConfigured() && !activeWfId().isEmpty() && isConnected()) {
            setActiveWfConfigured(true);
            configureWaterfall();
        }
        return;
    }

    // ATU status: "atu <handle> status=TUNE_SUCCESSFUL atu_enabled=1 ..."
    // Routes to TransmitModel for the TX applet ATU controls.
    // Also forwards to TunerModel if an external TGXL is connected.
    static const QRegularExpression atuRe(R"(^atu\s+(\S+)$)");
    if (object.startsWith("atu")) {
        const auto m = atuRe.match(object);
        if (m.hasMatch() && m_tunerModel.handle().isEmpty())
            m_tunerModel.setHandle(m.captured(1));
        m_transmitModel.applyAtuStatus(kvs);
        if (m_tunerModel.isPresent())
            m_tunerModel.applyStatus(kvs);
        return;
    }

    // APD status: "apd enable=1 ..."
    if (object == "apd") {
        m_transmitModel.applyApdStatus(kvs);
        return;
    }

    // Amplifier status: both TGXL and PGXL report via the amplifier API.
    // FlexLib distinguishes them by the "model" key:
    //   model=TunerGeniusXL  → antenna tuner (TGXL)
    //   model=PowerGeniusXL  → power amplifier (PGXL)
    // "amplifier <handle> model=TunerGeniusXL operate=1 relayC1=20 ..."
    static const QRegularExpression ampRe(R"(^amplifier\s+(\S+)$)");
    if (object.startsWith("amplifier")) {
        const auto m = ampRe.match(object);
        if (m.hasMatch()) {
            const QString handle = m.captured(1);
            const QString model = kvs.value("model");

            // Route TunerGeniusXL to TunerModel
            if (model == "TunerGeniusXL" || handle == m_tunerModel.handle()) {
                // Always update handle — first status may arrive with 0x00000000
                // before the real handle is assigned
                if (handle != "0x00000000" && handle != m_tunerModel.handle()) {
                    m_tunerModel.setHandle(handle);
                    m_meterModel.setTgxlHandle(handle.toUInt(nullptr, 0));
                } else if (m_tunerModel.handle().isEmpty()) {
                    m_tunerModel.setHandle(handle);
                    m_meterModel.setTgxlHandle(handle.toUInt(nullptr, 0));
                }
                m_tunerModel.applyStatus(kvs);
            }

            // Track power amplifier state (PGXL or any non-TGXL amp).
            // PGXL uses "state=OPERATE|IDLE|BYPASS|..." (not operate=/bypass= like TGXL)
            if (!model.isEmpty() && model != "TunerGeniusXL") {
                m_ampHandle = handle;
                if (!m_hasAmplifier) {
                    m_hasAmplifier = true;
                    m_ampIp = kvs.value("ip");
                    m_ampModel = model;
                    qCDebug(lcProtocol) << "RadioModel: power amplifier detected, model=" << model
                                       << "ip=" << m_ampIp;
                    emit amplifierChanged(true);
                }
            }
            if (!m_ampHandle.isEmpty() && handle == m_ampHandle) {
                const QString state = kvs.value("state").toUpper();
                if (!state.isEmpty()) {
                    // PGXL states: IDLE = on/ready, STANDBY = off, POWERUP = transitioning
                    bool op = (state == "IDLE" || state == "OPERATE"
                               || state.startsWith("TRANSMIT"));
                    if (m_ampOperate != op) {
                        m_ampOperate = op;
                        emit ampStateChanged();
                    }
                }
            }
        }
        return;
    }

    // Transmit status: "transmit rfpower=93 tunepower=38 tune=0 ..."
    if (object == "transmit") {
        m_transmitModel.applyTransmitStatus(kvs);
        return;
    }

    // TX profile status: "profile tx list=DAX^Default^..." or "profile tx current=Default"
    if (object.startsWith("profile")) {
        handleProfileStatus(object, kvs);
        return;
    }

    // Per-band TX settings: "transmit band 9 band_name=20 rfpower=100 ..."
    static const QRegularExpression txBandRe(R"(^transmit band\s+(\d+)$)");
    if (object.startsWith("transmit band")) {
        const auto m = txBandRe.match(object);
        if (m.hasMatch()) {
            int id = m.captured(1).toInt();
            auto& b = m_txBandSettings[id];
            b.bandId = id;
            if (kvs.contains("band_name"))    b.bandName  = kvs["band_name"];
            if (kvs.contains("rfpower"))      b.rfPower   = kvs["rfpower"].toInt();
            if (kvs.contains("tunepower"))    b.tunePower = kvs["tunepower"].toInt();
            if (kvs.contains("inhibit"))      b.inhibit   = kvs["inhibit"] == "1";
            if (kvs.contains("hwalc_enabled"))b.hwAlc     = kvs["hwalc_enabled"] == "1";
        }
        return;
    }

    // Per-band interlock: "interlock band 9 band_name=20 acc_txreq_enable=0 ..."
    static const QRegularExpression ilBandRe(R"(^interlock band\s+(\d+)$)");
    if (object.startsWith("interlock band")) {
        const auto m = ilBandRe.match(object);
        if (m.hasMatch()) {
            int id = m.captured(1).toInt();
            auto& b = m_txBandSettings[id];
            b.bandId = id;
            if (kvs.contains("band_name"))       b.bandName = kvs["band_name"];
            if (kvs.contains("acc_txreq_enable"))b.accTxReq = kvs["acc_txreq_enable"] == "1";
            if (kvs.contains("rca_txreq_enable"))b.rcaTxReq = kvs["rca_txreq_enable"] == "1";
            if (kvs.contains("acc_tx_enabled"))  b.accTx    = kvs["acc_tx_enabled"] == "1";
            if (kvs.contains("tx1_enabled"))     b.tx1      = kvs["tx1_enabled"] == "1";
            if (kvs.contains("tx2_enabled"))     b.tx2      = kvs["tx2_enabled"] == "1";
            if (kvs.contains("tx3_enabled"))     b.tx3      = kvs["tx3_enabled"] == "1";
        }
        return;
    }

    // Spot status: "spot 42 callsign=W1AW rx_freq=14.074000 ..."
    //              "spot 42 removed"
    //              "spot 42 triggered pan=0x40000000"
    if (object.startsWith("spot ")) {
        static const QRegularExpression spotRe(R"(^spot\s+(\d+))");
        const auto sm = spotRe.match(object);
        if (sm.hasMatch()) {
            int idx = sm.captured(1).toInt();
            if (kvs.isEmpty() && object.contains("removed")) {
                m_spotModel.removeSpot(idx);
            } else if (kvs.isEmpty() && object.contains("triggered")) {
                // Parse pan= from the object string if present
                static const QRegularExpression panRe2(R"(pan=(0x[0-9A-Fa-f]+))");
                const auto pm = panRe2.match(object);
                emit m_spotModel.spotTriggered(idx, pm.hasMatch() ? pm.captured(1) : QString());
            } else {
                m_spotModel.applySpotStatus(idx, kvs);
            }
        }
        return;
    }

    // USB cable status: "usb_cable FTDI-1234 type=cat enable=1 ..."
    //                   "usb_cable FTDI-1234 bit 0 enable=1 source=active_slice ..."
    //                   "usb_cable FTDI-1234 removed"
    if (object.startsWith("usb_cable ")) {
        QString rest = object.mid(10);  // after "usb_cable "
        // Serial number is the first word
        int spaceIdx = rest.indexOf(' ');
        QString sn = (spaceIdx >= 0) ? rest.left(spaceIdx) : rest;

        if (rest.contains("removed")) {
            m_usbCableModel.handleRemoved(sn);
        } else {
            // Check for bit-level status: remaining object text is "bit <N>"
            // The CommandParser puts extra object words before the KV split.
            // "usb_cable FTDI-1234 bit 3" → object="usb_cable FTDI-1234 bit 3", kvs={enable=1,...}
            QMap<QString, QString> effectiveKvs = kvs;
            if (spaceIdx >= 0) {
                QString afterSn = rest.mid(spaceIdx + 1).trimmed();
                if (afterSn.startsWith("bit ")) {
                    int bitNum = afterSn.mid(4).trimmed().toInt();
                    effectiveKvs["_bit_number"] = QString::number(bitNum);
                }
            }
            m_usbCableModel.applyStatus(sn, effectiveKvs);
        }
        return;
    }

    // CWX status: "cwx sent=0", "cwx wpm=20", "cwx macro1=CQ\u007fCQ"
    if (object == "cwx") {
        m_cwxModel.applyStatus(kvs);
        return;
    }

    // DVK status: "dvk status=idle enabled=1" or "dvk added id=1 name="Recording 1" duration=0"
    if (object.startsWith("dvk")) {
        // Pass both the object string (may contain "added"/"deleted") and KVs
        m_dvkModel.applyStatus(object, kvs);
        return;
    }

    // Interlock status: "interlock tx_client_handle=0x... state=TRANSMITTING ..."
    if (object == "interlock") {
        // Track TX ownership — only show TX state if we own the transmitter
        if (kvs.contains("tx_client_handle")) {
            quint32 txOwner = kvs["tx_client_handle"].toUInt(nullptr, 16);
            m_txClientHandle = txOwner;
            m_txOwnedByUs = (txOwner == clientHandle() || txOwner == 0);
        }
        // Parse interlock timing fields into TransmitModel (#498)
        m_transmitModel.applyInterlockStatus(kvs);

        if (kvs.contains("state")) {
            const QString state = kvs["state"].toUpper();

            // Emit raw radio TX state regardless of ownership — used by DAX
            // passthrough when an external app triggers PTT (#752).
            const bool radioTx = (state == "TRANSMITTING");
            emit radioTransmittingChanged(radioTx);

            if (!m_txOwnedByUs || (!m_txRequested && !m_transmitModel.isTuning())) {
                // Another client owns TX, or local unkey requested:
                // force local TX/audio gate off through all interlock states.
                m_transmitModel.setTransmitting(false);
                if (m_txAudioGate) {
                    m_txAudioGate = false;
                    emit txAudioGateChanged(false);
                }
            } else if (state == "TRANSMITTING") {
                // Radio confirms RF is keyed.
                m_transmitModel.setTransmitting(true);
                if (!m_txAudioGate) {
                    m_txAudioGate = true;
                    emit txAudioGateChanged(true);
                }
            } else {
                // Local key requested but radio is still in pre-TX transition
                // (e.g. PTT/TX delay). Keep optimistic TX-on gating for
                // modem/PTT edge alignment.
                const bool transitioningToTx =
                    state.contains("REQUESTED") || state.contains("DELAY");
                if (!transitioningToTx)
                    m_transmitModel.setTransmitting(false);
                if (!transitioningToTx && m_txAudioGate) {
                    m_txAudioGate = false;
                    emit txAudioGateChanged(false);
                }
            }
        }
        // Emit TX ownership state for title bar indicator
        // txOwnerChanged(otherIsTx, stationName) — true when ANOTHER client has TX
        if (!m_txOwnedByUs) {
            QString station = m_clientStations.value(m_txClientHandle, "TX Not Ready");
            emit txOwnerChanged(true, station);  // another client has TX
        } else {
            emit txOwnerChanged(false, {});  // we own TX (or nobody does)
        }
        m_transmitModel.applyInterlockStatus(kvs);
        return;
    }

    // EQ status: "eq txsc mode=1 63Hz=0 125Hz=5 ..." or "eq rxsc ..."
    if (object == "eq txsc") {
        m_equalizerModel.applyTxEqStatus(kvs);
        return;
    }
    if (object == "eq rxsc") {
        m_equalizerModel.applyRxEqStatus(kvs);
        return;
    }

    // TNF status: "tnf <id> freq=14.100000 width=100 depth=1 permanent=0"
    static const QRegularExpression tnfRe(R"(^tnf\s+(\d+)$)");
    auto tnfMatch = tnfRe.match(object);
    if (tnfMatch.hasMatch()) {
        int tnfId = tnfMatch.captured(1).toInt();
        m_tnfModel.applyTnfStatus(tnfId, kvs);
        return;
    }

    // WAN, etc. — informational, ignore for now.
}

QString RadioModel::serial() const
{
    return m_lastInfo.serial;
}

void RadioModel::setRemoteOnEnabled(bool on)
{
    m_remoteOnEnabled = on;
    sendCmd(QString("radio set remote_on_enabled=%1").arg(on ? 1 : 0));
    emit infoChanged();
}

void RadioModel::setMultiFlexEnabled(bool on)
{
    m_multiFlexEnabled = on;
    sendCmd(QString("radio set mf_enable=%1").arg(on ? 1 : 0));
    emit infoChanged();
}

void RadioModel::handleRadioStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;
    if (kvs.contains("model"))    { m_model = kvs["model"]; changed = true; }
    if (kvs.contains("slices")) {
        int n = kvs["slices"].toInt();
        if (n > m_maxSlices) m_maxSlices = n;  // track highest seen
        changed = true;
    }
    if (kvs.contains("callsign")) { m_callsign = kvs["callsign"]; changed = true; }
    if (kvs.contains("nickname")) { m_nickname = kvs["nickname"]; changed = true; }
    if (kvs.contains("region"))   { m_region = kvs["region"]; changed = true; }
    if (kvs.contains("radio_options")) { m_radioOptions = kvs["radio_options"]; changed = true; }
    if (kvs.contains("remote_on_enabled")) {
        m_remoteOnEnabled = kvs["remote_on_enabled"] == "1";
        changed = true;
    }
    if (kvs.contains("mf_enable")) {
        m_multiFlexEnabled = kvs["mf_enable"] == "1";
        changed = true;
    }
    if (kvs.contains("enforce_private_ip_connections")) {
        m_enforcePrivateIp = kvs["enforce_private_ip_connections"] == "1";
        changed = true;
    }
    if (kvs.contains("binaural_rx")) {
        m_binauralRx = kvs["binaural_rx"] == "1";
        changed = true;
    }
    if (kvs.contains("full_duplex_enabled")) {
        m_fullDuplex = kvs["full_duplex_enabled"] == "1";
        changed = true;
    }
    if (kvs.contains("mute_local_audio_when_remote")) {
        m_muteLocalWhenRemote = kvs["mute_local_audio_when_remote"] == "1";
        changed = true;
    }
    if (kvs.contains("freq_error_ppb")) {
        m_freqErrorPpb = kvs["freq_error_ppb"].toInt();
        changed = true;
    }
    if (kvs.contains("cal_freq")) {
        m_calFreqMhz = kvs["cal_freq"].toDouble();
        changed = true;
    }
    if (kvs.contains("low_latency_digital_modes")) {
        m_lowLatencyDigital = kvs["low_latency_digital_modes"] == "1";
        changed = true;
    }
    if (kvs.contains("rtty_mark_default")) {
        m_rttyMarkDefault = kvs["rtty_mark_default"].toInt();
        changed = true;
    }
    if (kvs.contains("tnf_enabled")) {
        m_tnfModel.setGlobalEnabled(kvs["tnf_enabled"] == "1");
    }
    // Audio outputs
    bool audioChanged = false;
    if (kvs.contains("lineout_gain")) {
        m_lineoutGain = kvs["lineout_gain"].toInt();
        audioChanged = true;
    }
    if (kvs.contains("lineout_mute")) {
        m_lineoutMute = kvs["lineout_mute"] == "1";
        audioChanged = true;
    }
    if (kvs.contains("headphone_gain")) {
        m_headphoneGain = kvs["headphone_gain"].toInt();
        audioChanged = true;
    }
    if (kvs.contains("headphone_mute")) {
        m_headphoneMute = kvs["headphone_mute"] == "1";
        audioChanged = true;
    }
    if (kvs.contains("front_speaker_mute")) {
        m_frontSpeakerMute = kvs["front_speaker_mute"] == "1";
        audioChanged = true;
    }
    if (kvs.contains("daxiq_capacity"))
        m_daxIqModel.setCapacity(kvs["daxiq_capacity"].toInt());
    if (kvs.contains("daxiq_available"))
        m_daxIqModel.setAvailable(kvs["daxiq_available"].toInt());

    if (audioChanged) emit audioOutputChanged();
    if (changed) emit infoChanged();
}

void RadioModel::setLineoutGain(int v)
{
    v = std::clamp(v, 0, 100);
    qCDebug(lcAudio) << "setLineoutGain:" << v;
    sendCmd(QString("mixer lineout gain %1").arg(v));
}

void RadioModel::setLineoutMute(bool m)
{
    qCDebug(lcAudio) << "setLineoutMute:" << m;
    sendCmd(QString("mixer lineout mute %1").arg(m ? 1 : 0));
}

void RadioModel::setHeadphoneGain(int v)
{
    v = std::clamp(v, 0, 100);
    qCDebug(lcAudio) << "setHeadphoneGain:" << v;
    sendCmd(QString("mixer headphone gain %1").arg(v));
}

void RadioModel::setHeadphoneMute(bool m)
{
    qCDebug(lcAudio) << "setHeadphoneMute:" << m;
    sendCmd(QString("mixer headphone mute %1").arg(m ? 1 : 0));
}

void RadioModel::setFrontSpeakerMute(bool m)
{
    qCDebug(lcAudio) << "setFrontSpeakerMute:" << m;
    sendCmd(QString("mixer front_speaker mute %1").arg(m ? 1 : 0));
}

void RadioModel::handleSliceStatus(int id,
                                    const QMap<QString, QString>& kvs,
                                    bool removed)
{
    // Track slice ownership via client_handle (only present in some messages)
    if (kvs.contains("client_handle")) {
        quint32 owner = kvs["client_handle"].toUInt(nullptr, 16);
        if (owner == clientHandle()) {
            m_ownedSliceIds.insert(id);
            qCDebug(lcProtocol) << "RadioModel: slice" << id << "is ours (client_handle match)";
        } else if (owner != 0) {
            qCDebug(lcProtocol) << "RadioModel: slice" << id << "belongs to another client"
                     << Qt::hex << owner << ", removing";
            m_ownedSliceIds.remove(id);
            // If we already have a SliceModel for this ID, remove it
            if (SliceModel* existing = slice(id)) {
                m_slices.removeOne(existing);
                emit sliceRemoved(id);
                existing->deleteLater();
            }
            return;  // slice belongs to another client
        }
    }

    // If we've seen client_handle info and this slice isn't ours, skip it
    if (!m_ownedSliceIds.isEmpty() && !m_ownedSliceIds.contains(id)) {
        qCDebug(lcProtocol) << "RadioModel: ignoring slice" << id << "status (not in owned set)";
        return;
    }

    SliceModel* s = slice(id);

    if (removed) {
        if (s) {
            m_slices.removeOne(s);
            emit sliceRemoved(id);
            s->deleteLater();
        }
        return;
    }

    if (!s) {
        // Only create SliceModel from a full status (has in_use=1 and RF_frequency).
        // Partial statuses (e.g. "slice 3 rit_on=0") arrive early without enough
        // data to initialize the VFO widget correctly.
        if (!kvs.contains("in_use") || kvs["in_use"] != "1")
            return;

        s = new SliceModel(id, this);
        // Forward slice commands to the radio
        connect(s, &SliceModel::commandReady, this, [this](const QString& cmd){
            sendCmd(cmd);
        });
        m_slices.append(s);
        s->applyStatus(kvs);  // populate frequency/mode before notifying UI
        emit sliceAdded(s);
        return;                // applyStatus already called below; skip second call
    }

    s->applyStatus(kvs);

    // Aurora/AU-520: max_internal_pa_power in slice status reports the true
    // system power capability (e.g. 500W) while transmit status max_power_level
    // only reports the exciter limit (100W). Use the higher value. (#484)
    if (kvs.contains("max_internal_pa_power")) {
        int internalMax = kvs["max_internal_pa_power"].toInt();
        if (internalMax > m_transmitModel.maxPowerLevel()) {
            m_transmitModel.setMaxPowerLevel(internalMax);
        }
    }

    // Send any queued commands (e.g. if GUI changed freq before status arrived)
    if (isConnected()) {
        for (const QString& cmd : s->drainPendingCommands())
            sendCmd(cmd);
    }
}

void RadioModel::handleMeterStatus(const QString& rawBody)
{
    // Meter status body format (from FlexLib Radio.cs ParseMeterStatus):
    //   Tokens separated by '#', each token is "index.key=value".
    //   Example: "7.src=SLC#7.num=0#7.nam=LEVEL#7.unit=dBm#7.low=-150.0#7.hi=20.0"
    //
    // Removal format: "7 removed"

    if (rawBody.contains("removed")) {
        const QStringList words = rawBody.split(' ', Qt::SkipEmptyParts);
        if (words.size() >= 1) {
            bool ok = false;
            const int idx = words[0].toInt(&ok);
            if (ok) m_meterModel.removeMeter(idx);
        }
        return;
    }

    // Group tokens by meter index
    QMap<int, QMap<QString, QString>> grouped;
    const QStringList tokens = rawBody.split('#', Qt::SkipEmptyParts);

    for (const QString& token : tokens) {
        const int dot = token.indexOf('.');
        if (dot < 0) continue;
        const int eq = token.indexOf('=', dot);
        if (eq < 0) continue;

        bool ok = false;
        const int idx = token.left(dot).toInt(&ok);
        if (!ok) continue;

        const QString key   = token.mid(dot + 1, eq - dot - 1);
        const QString value = token.mid(eq + 1);
        grouped[idx][key] = value;
    }

    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        const auto& fields = it.value();

        MeterDef def;
        def.index = it.key();
        if (fields.contains("src"))  def.source      = fields["src"];
        if (fields.contains("num"))  def.sourceIndex  = fields["num"].toInt();
        if (fields.contains("nam"))  def.name         = fields["nam"];
        if (fields.contains("unit")) def.unit         = fields["unit"];
        if (fields.contains("low"))  def.low          = fields["low"].toDouble();
        if (fields.contains("hi"))   def.high         = fields["hi"].toDouble();
        if (fields.contains("desc")) def.description  = fields["desc"];

        m_meterModel.defineMeter(def);
    }
}

void RadioModel::handleGpsStatus(const QString& rawBody)
{
    // GPS status uses '#' as separator, same as meter status.
    // Example: "lat=48.27#lon=-116.56#grid=DN18rg#altitude=644 m#tracked=16#
    //           visible=31#speed=0 kts#freq_error=0 ppb#status=Fine Lock#time=05:25:20Z"
    const QStringList tokens = rawBody.split('#', Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        const int eq = token.indexOf('=');
        if (eq < 1) continue;
        const QString key   = token.left(eq).toLower();
        const QString value = token.mid(eq + 1);

        if      (key == "status")   m_gpsStatus   = value;
        else if (key == "tracked")  m_gpsTracked  = value.toInt();
        else if (key == "visible")  m_gpsVisible  = value.toInt();
        else if (key == "grid")     m_gpsGrid     = value;
        else if (key == "altitude") m_gpsAltitude = value;
        else if (key == "lat")      m_gpsLat      = value;
        else if (key == "lon")      m_gpsLon      = value;
        else if (key == "time")       m_gpsTime     = value;
        else if (key == "speed")      m_gpsSpeed    = value;
        else if (key == "freq_error") m_gpsFreqError = value;
    }

    emit gpsStatusChanged(m_gpsStatus, m_gpsTracked, m_gpsVisible,
                           m_gpsGrid, m_gpsAltitude, m_gpsLat, m_gpsLon,
                           m_gpsTime);
}

void RadioModel::handlePanadapterStatus(const QString& panId, const QMap<QString, QString>& kvs)
{
    // Delegate to the specific PanadapterModel, not just the active one
    auto* pan = m_panadapters.value(panId, nullptr);
    if (!pan) pan = activePanadapter();  // fallback
    if (pan) {
        pan->applyPanStatus(kvs);
    }

    // Keep legacy signals for backward compat (MainWindow still uses these)
    if (kvs.contains("center") || kvs.contains("bandwidth")) {
        if (pan) emit panadapterInfoChanged(pan->centerMhz(), pan->bandwidthMhz());
    }
    if (kvs.contains("min_dbm") || kvs.contains("max_dbm")) {
        const float minDbm = kvs.value("min_dbm", "-130").toFloat();
        const float maxDbm = kvs.value("max_dbm", "-20").toFloat();
        if (pan) {
            m_panStream->setDbmRange(pan->panStreamId(), minDbm, maxDbm);
        }
        emit panadapterLevelChanged(minDbm, maxDbm);
    }
    // Track ypixels from radio status — the radio encodes FFT bins as pixel
    // Y positions (0..ypixels-1), so PanadapterStream needs this for dBm conversion.
    // Also detect when the radio resets to default dimensions (e.g. after profile
    // load) and re-request correct dimensions from MainWindow.
    if (kvs.contains("y_pixels") && pan) {
        int yPix = kvs["y_pixels"].toInt();
        if (yPix > 0)
            m_panStream->setYPixels(pan->panStreamId(), yPix);
    }
    if ((kvs.contains("x_pixels") || kvs.contains("y_pixels")) && pan) {
        int xPix = kvs.value("x_pixels", "0").toInt();
        int yPix = kvs.value("y_pixels", "0").toInt();
        // Radio reset to defaults (profile load, reconnect) — re-push real dimensions
        if ((xPix > 0 && xPix <= 100) || (yPix > 0 && yPix <= 100))
            emit panDimensionsNeeded(pan->panId());
    }
    if (kvs.contains("ant_list")) {
        const QStringList ants = kvs["ant_list"].split(',', Qt::SkipEmptyParts);
        if (ants != m_antList) {
            m_antList = ants;
            emit antListChanged(m_antList);
        }
    }

    // Configure the panadapter once we know its ID.
    if (pan && !pan->isResized() && isConnected()) {
        pan->setResized(true);
        configurePan();
    }
}

void RadioModel::updateStreamFilters()
{
    // Register all known pan/wf stream IDs with PanadapterStream
    for (auto* pan : m_panadapters) {
        if (pan->panStreamId())
            m_panStream->registerPanStream(pan->panStreamId());
        if (pan->wfStreamId())
            m_panStream->registerWfStream(pan->wfStreamId());
    }
}

void RadioModel::configurePan()
{
    if (m_activePanId.isEmpty()) return;

    // Request MainWindow to push actual widget dimensions for this pan.
    // Do NOT hardcode xpixels/ypixels here — MainWindow knows the real sizes.
    emit panDimensionsNeeded(m_activePanId);

    sendCmd(
        QString("display pan set %1 fps=25 min_dbm=-130 max_dbm=-40").arg(m_activePanId),
        [](int code, const QString&) {
            if (code != 0)
                qCWarning(lcProtocol) << "RadioModel: display pan set fps/average/dbm failed, code" << Qt::hex << code;
        });
}

void RadioModel::configureWaterfall()
{
    if (activeWfId().isEmpty()) return;

    // Disable automatic black-level and set a fixed threshold.
    // FlexLib uses "display panafall set" addressed to the waterfall stream ID.
    const QString cmd = QString("display panafall set %1 auto_black=0 black_level=15 color_gain=50")
                            .arg(activeWfId());
    sendCmd(cmd, [this](int code, const QString&) {
        if (code != 0) {
            qCDebug(lcProtocol) << "RadioModel: display panafall set waterfall failed, code"
                     << Qt::hex << code << "— trying display waterfall set";
            // Fallback for firmware that doesn't support panafall addressing
            sendCmd(
                QString("display waterfall set %1 auto_black=0 black_level=15 color_gain=50")
                    .arg(activeWfId()),
                [](int code2, const QString&) {
                    if (code2 != 0)
                        qCWarning(lcProtocol) << "RadioModel: display waterfall set also failed, code"
                                   << Qt::hex << code2;
                    else
                        qCDebug(lcProtocol) << "RadioModel: waterfall configured via display waterfall set";
                });
        } else {
            qCDebug(lcProtocol) << "RadioModel: waterfall configured (auto_black=0 black_level=15 color_gain=50)";
        }
    });
}

// ─── Standalone mode: create panadapter + slice ───────────────────────────────
//
// SmartSDR API 1.4.0.0 standalone flow:
//   1. "panadapter create"
//      → R|0|pan=0x40000000         (KV response; key is "pan")
//   2. "slice create pan=0x40000000 freq=14.225000 antenna=ANT1 mode=USB"
//      → R|0|<slice_index>          (decimal, e.g. "0")
//   3. Radio emits S messages for the new panadapter and slice.
//
// Note: "display panafall create" (v2+ syntax) returns 0x50000016 on this firmware.

void RadioModel::createDefaultSlice(const QString& freqMhz,
                                     const QString& mode,
                                     const QString& antenna)
{
    qCDebug(lcProtocol) << "RadioModel: standalone mode — creating panadapter + slice"
             << freqMhz << mode << antenna;

    sendCmd("panadapter create",
        [this, freqMhz, mode, antenna](int code, const QString& body) {
            if (code != 0) {
                qCWarning(lcProtocol) << "RadioModel: panadapter create failed, code" << Qt::hex << code
                           << "body:" << body;
                return;
            }

            qCDebug(lcProtocol) << "RadioModel: panadapter create response body:" << body;

            // Response body may be a bare hex ID ("0x40000000") or KV ("pan=0x40000000").
            // Parse KVs first; fall back to treating the whole body as the ID.
            QString panId;
            const QMap<QString, QString> kvs = CommandParser::parseKVs(body);
            if (kvs.contains("pan")) {
                panId = kvs["pan"];
            } else if (kvs.contains("id")) {
                panId = kvs["id"];
            } else {
                panId = body.trimmed();
            }

            qCDebug(lcProtocol) << "RadioModel: panadapter created, pan_id =" << panId;

            if (panId.isEmpty()) {
                qCWarning(lcProtocol) << "RadioModel: panadapter create returned empty pan_id";
                return;
            }

            const QString sliceCmd =
                QString("slice create pan=%1 freq=%2 antenna=%3 mode=%4")
                    .arg(panId, freqMhz, antenna, mode);

            sendCmd(sliceCmd,
                [panId](int code2, const QString& body2) {
                    if (code2 != 0) {
                        qCWarning(lcProtocol) << "RadioModel: slice create failed, code"
                                   << Qt::hex << code2 << "body:" << body2;
                    } else {
                        qCDebug(lcProtocol) << "RadioModel: slice created, index =" << body2;
                        // Radio now emits S|slice N ... status messages;
                        // handleSliceStatus() picks them up automatically.
                    }
                });
        });
}

void RadioModel::handleProfileStatus(const QString& object,
                                      const QMap<QString, QString>& kvs)
{
    // Profile list/current with space-containing names are handled by
    // handleProfileStatusRaw() via onMessageReceived().  This fallback
    // handles any remaining profile status keys that don't have spaces
    // (e.g. "profile all unsaved_changes_tx=0").
    Q_UNUSED(object);
    Q_UNUSED(kvs);
}

void RadioModel::handleProfileStatusRaw(const QString& profileType,
                                         const QString& rawBody)
{
    // rawBody is everything after "profile tx " or "profile mic ", e.g.:
    //   "list=Default^Default FHM-1^Default FHM-1 DX^..."
    //   "current=Default FHM-1"
    // We parse key=value ourselves to avoid splitting on spaces in values.
    const int eq = rawBody.indexOf('=');
    if (eq < 0) return;

    const QString key = rawBody.left(eq).trimmed();
    const QString val = rawBody.mid(eq + 1).trimmed();

    if (profileType == "tx") {
        if (key == "list") {
            QStringList profiles = val.split('^', Qt::SkipEmptyParts);
            m_transmitModel.setProfileList(profiles);
            qCDebug(lcProtocol) << "RadioModel: TX profiles:" << profiles;
        } else if (key == "current") {
            m_transmitModel.setActiveProfile(val);
            qCDebug(lcProtocol) << "RadioModel: active TX profile:" << val;
        }
    } else if (profileType == "mic") {
        if (key == "list") {
            QStringList profiles = val.split('^', Qt::SkipEmptyParts);
            m_transmitModel.setMicProfileList(profiles);
            qCDebug(lcProtocol) << "RadioModel: mic profiles:" << profiles;
        } else if (key == "current") {
            m_transmitModel.setActiveMicProfile(val);
            qCDebug(lcProtocol) << "RadioModel: active mic profile:" << val;
        }
    } else if (profileType == "global") {
        if (key == "list") {
            m_globalProfiles = val.split('^', Qt::SkipEmptyParts);
            qCDebug(lcProtocol) << "RadioModel: global profiles:" << m_globalProfiles;
            emit globalProfilesChanged();
        } else if (key == "current") {
            m_activeGlobalProfile = val;
            qCDebug(lcProtocol) << "RadioModel: active global profile:" << val;
            emit globalProfilesChanged();
        }
    }
}

void RadioModel::loadGlobalProfile(const QString& name)
{
    sendCmd(QString("profile global load \"%1\"").arg(name));
}

void RadioModel::resetPanState()
{
    setActivePanResized(false);
    setActiveWfConfigured(false);
}

void RadioModel::createAudioStream()
{
    // Remove old audio stream first, then create new one in the callback
    if (!m_rxAudioStreamId.isEmpty()) {
        const QString oldId = m_rxAudioStreamId;
        m_rxAudioStreamId.clear();
        sendCmd(
            QString("stream remove 0x%1").arg(oldId),
            [this](int, const QString&) {
                // Old stream removed — now create the new one
                sendCmd(
                    QString("stream create type=remote_audio_rx compression=%1").arg(audioCompressionParam()),
                    [this](int code, const QString& body) {
                        if (code == 0) {
                            m_rxAudioStreamId = body.trimmed();
                            qCDebug(lcProtocol) << "RadioModel: remote_audio_rx stream re-created, id:" << m_rxAudioStreamId;
                        } else
                            qCWarning(lcProtocol) << "RadioModel: stream create remote_audio_rx failed, code"
                                       << Qt::hex << code << "body:" << body;
                    });
            });
    } else {
        sendCmd(
            QString("stream create type=remote_audio_rx compression=%1").arg(audioCompressionParam()),
            [this](int code, const QString& body) {
                if (code == 0) {
                    m_rxAudioStreamId = body.trimmed();
                    qCDebug(lcProtocol) << "RadioModel: remote_audio_rx stream re-created, id:" << m_rxAudioStreamId;
                } else
                    qCWarning(lcProtocol) << "RadioModel: stream create remote_audio_rx failed, code"
                               << Qt::hex << code << "body:" << body;
            });
    }

    sendCmd(
        "stream create type=dax_tx",
        [this](int code, const QString& body) {
            if (code == 0) {
                quint32 id = body.trimmed().toUInt(nullptr, 16);
                qCDebug(lcProtocol) << "RadioModel: dax_tx stream re-created, id:" << Qt::hex << id;
                emit txAudioStreamReady(id);
            }
        });
}

QJsonObject RadioModel::troubleshootingSnapshot() const
{
    QJsonObject snapshot;
    snapshot["schema_version"] = 1;
    snapshot["captured_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    snapshot["captured_from"] = "AetherSDR in-memory application state";
    snapshot["note"] =
        "This snapshot is built from the app's cached radio, panadapter, slice, "
        "and meter models. It does not query the radio directly.";
    snapshot["privacy"] =
        "Sensitive identifiers are omitted by design, including radio name, "
        "nickname, callsign, serial numbers, MAC/IP addresses, GPS data, and "
        "client station names.";

    QJsonObject app;
    app["name"] = QCoreApplication::applicationName();
    app["version"] = QCoreApplication::applicationVersion();
    app["qt_version"] = qVersion();
    app["os"] = QSysInfo::prettyProductName();
    app["cpu_arch"] = QSysInfo::currentCpuArchitecture();
    snapshot["app"] = app;

    QJsonObject radio;
    radio["connected"] = isConnected();
    radio["transport"] = isWan() ? "WAN" : "LAN";
    radio["model"] = m_model;
    radio["software_version"] = m_version;
    radio["protocol_version"] = m_protocolVersion;
    radio["region"] = m_region;
    radio["radio_options"] = m_radioOptions;
    radio["max_slices"] = m_maxSlices;
    radio["full_duplex_enabled"] = m_fullDuplex;
    radio["binaural_rx"] = m_binauralRx;
    radio["mute_local_audio_when_remote"] = m_muteLocalWhenRemote;
    radio["low_latency_digital_modes"] = m_lowLatencyDigital;
    radio["enforce_private_ip_connections"] = m_enforcePrivateIp;
    radio["remote_on_enabled"] = m_remoteOnEnabled;
    radio["mf_enable"] = m_multiFlexEnabled;
    radio["rtty_mark_default"] = m_rttyMarkDefault;
    radio["antenna_list"] = toJsonArray(m_antList);
    radio["owned_slice_ids"] = toJsonArray(m_ownedSliceIds);
    radio["global_profile_count"] = m_globalProfiles.size();
    radio["active_global_profile_set"] = !m_activeGlobalProfile.trimmed().isEmpty();

    QJsonObject oscillator;
    oscillator["setting"] = m_oscSetting;
    oscillator["locked"] = m_oscLocked;
    oscillator["ext_present"] = m_extPresent;
    oscillator["tcxo_present"] = m_tcxoPresent;
    radio["oscillator"] = oscillator;

    QJsonObject audioOutputs;
    audioOutputs["lineout_gain"] = m_lineoutGain;
    audioOutputs["lineout_mute"] = m_lineoutMute;
    audioOutputs["headphone_gain"] = m_headphoneGain;
    audioOutputs["headphone_mute"] = m_headphoneMute;
    audioOutputs["front_speaker_mute"] = m_frontSpeakerMute;
    radio["audio_outputs"] = audioOutputs;

    QJsonObject filterSharpness;
    filterSharpness["voice_level"] = m_filterVoice;
    filterSharpness["voice_auto"] = m_filterVoiceAuto;
    filterSharpness["cw_level"] = m_filterCw;
    filterSharpness["cw_auto"] = m_filterCwAuto;
    filterSharpness["digital_level"] = m_filterDigital;
    filterSharpness["digital_auto"] = m_filterDigitalAuto;
    radio["filter_sharpness"] = filterSharpness;

    QJsonObject amplifier;
    amplifier["present"] = m_hasAmplifier;
    amplifier["handle"] = m_ampHandle;
    amplifier["model"] = m_ampModel;
    amplifier["operate"] = m_ampOperate;
    radio["amplifier"] = amplifier;

    QJsonObject ownership;
    ownership["tx_owned_by_us"] = m_txOwnedByUs;
    QJsonArray clients;
    QSet<quint32> seenHandles;
    const quint32 ourHandle = ourClientHandle();
    for (auto it = m_clientStations.cbegin(); it != m_clientStations.cend(); ++it) {
        clients.append(clientInfoToJson(it.key(), ourHandle, m_txClientHandle,
                                        m_clientInfoMap.value(it.key())));
        seenHandles.insert(it.key());
    }
    for (auto it = m_clientInfoMap.cbegin(); it != m_clientInfoMap.cend(); ++it) {
        if (seenHandles.contains(it.key()))
            continue;
        clients.append(clientInfoToJson(it.key(), ourHandle, m_txClientHandle,
                                        it.value()));
    }
    ownership["clients"] = clients;
    ownership["client_count"] = clients.size();
    ownership["multiple_clients_present"] = clients.size() > 1;
    radio["ownership"] = ownership;

    auto categoryStatsToJson = [this](PanadapterStream::StreamCategory cat) {
        const auto stats = categoryStats(cat);
        QJsonObject obj;
        obj["bytes"] = static_cast<qint64>(stats.bytes);
        obj["packets"] = stats.packets;
        obj["errors"] = stats.errors;
        return obj;
    };

    QJsonObject network;
    network["quality"] = networkQuality();
    network["last_ping_rtt_ms"] = m_lastPingRtt;
    network["max_ping_rtt_ms"] = m_maxPingRtt;
    network["packet_drop_count"] = packetDropCount();
    network["packet_total_count"] = packetTotalCount();
    network["rx_bytes"] = static_cast<qint64>(rxBytes());
    network["tx_bytes"] = static_cast<qint64>(txBytes());
    QJsonObject streamCategories;
    streamCategories["audio"] = categoryStatsToJson(PanadapterStream::CatAudio);
    streamCategories["fft"] = categoryStatsToJson(PanadapterStream::CatFFT);
    streamCategories["waterfall"] = categoryStatsToJson(PanadapterStream::CatWaterfall);
    streamCategories["meter"] = categoryStatsToJson(PanadapterStream::CatMeter);
    streamCategories["dax"] = categoryStatsToJson(PanadapterStream::CatDAX);
    network["stream_categories"] = streamCategories;
    radio["network"] = network;

    QJsonObject telemetry;
    telemetry["pa_temp_c"] = m_meterModel.paTemp();
    telemetry["supply_volts"] = m_meterModel.supplyVolts();
    telemetry["tx_forward_power_w"] = m_meterModel.fwdPower();
    telemetry["tx_swr"] = m_meterModel.swr();
    telemetry["alc"] = m_meterModel.alc();
    telemetry["mic_level_dbfs"] = m_meterModel.micLevel();
    telemetry["mic_peak_dbfs"] = m_meterModel.micPeak();
    telemetry["comp_level_db"] = m_meterModel.compLevel();
    telemetry["comp_peak_db"] = m_meterModel.compPeak();
    radio["telemetry"] = telemetry;

    snapshot["radio"] = radio;

    QJsonObject transmit;

    QJsonObject txPower;
    txPower["rf_power"] = m_transmitModel.rfPower();
    txPower["tune_power"] = m_transmitModel.tunePower();
    txPower["max_power_level"] = m_transmitModel.maxPowerLevel();
    txPower["tune_mode"] = m_transmitModel.tuneMode();
    txPower["show_tx_in_waterfall"] = m_transmitModel.showTxInWaterfall();
    txPower["tuning"] = m_transmitModel.isTuning();
    txPower["mox"] = m_transmitModel.isMox();
    txPower["transmitting"] = m_transmitModel.isTransmitting();
    transmit["power"] = txPower;

    QJsonObject mic;
    mic["selection"] = m_transmitModel.micSelection();
    mic["level"] = m_transmitModel.micLevel();
    mic["mic_acc"] = m_transmitModel.micAcc();
    mic["speech_processor_enable"] = m_transmitModel.speechProcessorEnable();
    mic["speech_processor_level"] = m_transmitModel.speechProcessorLevel();
    mic["compander_on"] = m_transmitModel.companderOn();
    mic["compander_level"] = m_transmitModel.companderLevel();
    mic["dax_on"] = m_transmitModel.daxOn();
    mic["sb_monitor"] = m_transmitModel.sbMonitor();
    mic["mon_gain_sb"] = m_transmitModel.monGainSb();
    mic["mic_boost"] = m_transmitModel.micBoost();
    mic["mic_bias"] = m_transmitModel.micBias();
    mic["met_in_rx"] = m_transmitModel.metInRx();
    mic["sync_cwx"] = m_transmitModel.syncCwx();
    mic["am_carrier_level"] = m_transmitModel.amCarrierLevel();
    mic["dexp_on"] = m_transmitModel.dexpOn();
    mic["dexp_level"] = m_transmitModel.dexpLevel();
    mic["tx_filter_low"] = m_transmitModel.txFilterLow();
    mic["tx_filter_high"] = m_transmitModel.txFilterHigh();
    transmit["mic"] = mic;

    QJsonObject vox;
    vox["enabled"] = m_transmitModel.voxEnable();
    vox["level"] = m_transmitModel.voxLevel();
    vox["delay"] = m_transmitModel.voxDelay();
    transmit["vox"] = vox;

    QJsonObject cw;
    cw["speed_wpm"] = m_transmitModel.cwSpeed();
    cw["pitch_hz"] = m_transmitModel.cwPitch();
    cw["break_in"] = m_transmitModel.cwBreakIn();
    cw["delay_ms"] = m_transmitModel.cwDelay();
    cw["sidetone"] = m_transmitModel.cwSidetone();
    cw["iambic"] = m_transmitModel.cwIambic();
    cw["iambic_mode"] = m_transmitModel.cwIambicMode();
    cw["swap_paddles"] = m_transmitModel.cwSwapPaddles();
    cw["cwl_enabled"] = m_transmitModel.cwlEnabled();
    cw["monitor_gain"] = m_transmitModel.monGainCw();
    transmit["cw"] = cw;

    QJsonObject interlock;
    interlock["acc_tx_delay"] = m_transmitModel.accTxDelay();
    interlock["tx1_delay"] = m_transmitModel.tx1Delay();
    interlock["tx2_delay"] = m_transmitModel.tx2Delay();
    interlock["tx3_delay"] = m_transmitModel.tx3Delay();
    interlock["tx_delay"] = m_transmitModel.txDelay();
    interlock["timeout"] = m_transmitModel.interlockTimeout();
    interlock["acc_tx_req_polarity"] = m_transmitModel.accTxReqPolarity();
    interlock["rca_tx_req_polarity"] = m_transmitModel.rcaTxReqPolarity();
    transmit["interlock"] = interlock;

    QJsonObject atu;
    atu["enabled"] = m_transmitModel.atuEnabled();
    atu["status"] = atuStatusToString(m_transmitModel.atuStatus());
    atu["memories_enabled"] = m_transmitModel.memoriesEnabled();
    atu["using_memory"] = m_transmitModel.usingMemory();
    transmit["atu"] = atu;

    QJsonObject apd;
    apd["enabled"] = m_transmitModel.apdEnabled();
    apd["configurable"] = m_transmitModel.apdConfigurable();
    apd["equalizer_active"] = m_transmitModel.apdEqualizerActive();
    transmit["apd"] = apd;

    QJsonObject profiles;
    profiles["tx_profile_count"] = m_transmitModel.profileList().size();
    profiles["active_tx_profile_set"] = !m_transmitModel.activeProfile().trimmed().isEmpty();
    profiles["mic_profile_count"] = m_transmitModel.micProfileList().size();
    profiles["active_mic_profile_set"] = !m_transmitModel.activeMicProfile().trimmed().isEmpty();
    profiles["mic_inputs"] = toJsonArray(m_transmitModel.micInputList());
    transmit["profiles"] = profiles;

    snapshot["transmit"] = transmit;

    QJsonArray panadapters;
    for (auto it = m_panadapters.cbegin(); it != m_panadapters.cend(); ++it)
        panadapters.append(panToJson(it.value(), m_activePanId));
    snapshot["panadapters"] = panadapters;

    auto txBandInfoToJson = [](const TxBandInfo& band) {
        QJsonObject obj;
        obj["band_id"] = band.bandId;
        obj["band_name"] = band.bandName;
        obj["rf_power"] = band.rfPower;
        obj["tune_power"] = band.tunePower;
        obj["inhibit"] = band.inhibit;
        obj["hw_alc"] = band.hwAlc;
        obj["acc_tx_req"] = band.accTxReq;
        obj["rca_tx_req"] = band.rcaTxReq;
        obj["acc_tx"] = band.accTx;
        obj["tx1"] = band.tx1;
        obj["tx2"] = band.tx2;
        obj["tx3"] = band.tx3;
        return obj;
    };

    QJsonArray txBands;
    for (auto it = m_txBandSettings.cbegin(); it != m_txBandSettings.cend(); ++it)
        txBands.append(txBandInfoToJson(it.value()));
    snapshot["tx_band_settings"] = txBands;

    QJsonArray allMeters = m_meterModel.allMeters();
    QJsonArray globalMeters;
    int sliceMeterCount = 0;
    for (const QJsonValue& value : allMeters) {
        const QJsonObject meter = value.toObject();
        if (meter["source"].toString() == "SLC")
            ++sliceMeterCount;
        else
            globalMeters.append(meter);
    }
    snapshot["global_meters"] = globalMeters;

    QList<SliceModel*> sortedSlices = m_slices;
    std::sort(sortedSlices.begin(), sortedSlices.end(), [](SliceModel* lhs, SliceModel* rhs) {
        return lhs->sliceId() < rhs->sliceId();
    });

    QJsonArray slices;
    for (SliceModel* sliceModel : sortedSlices) {
        QJsonObject slice;
        slice["slice_id"] = sliceModel->sliceId();
        slice["pan_id"] = sliceModel->panId();
        slice["frequency_mhz"] = sliceModel->frequency();
        slice["mode"] = sliceModel->mode();
        slice["mode_list"] = toJsonArray(sliceModel->modeList());
        slice["active"] = sliceModel->isActive();
        slice["tx_slice"] = sliceModel->isTxSlice();

        QJsonObject filter;
        filter["low_hz"] = sliceModel->filterLow();
        filter["high_hz"] = sliceModel->filterHigh();
        slice["filter"] = filter;

        QJsonObject audio;
        audio["gain"] = sliceModel->audioGain();
        audio["pan"] = sliceModel->audioPan();
        audio["mute"] = sliceModel->audioMute();
        slice["audio"] = audio;

        slice["rf_gain"] = sliceModel->rfGain();

        QJsonObject antennas;
        antennas["rx"] = sliceModel->rxAntenna();
        antennas["tx"] = sliceModel->txAntenna();
        slice["antennas"] = antennas;

        QJsonObject control;
        control["locked"] = sliceModel->isLocked();
        control["qsk"] = sliceModel->qskOn();
        control["record_on"] = sliceModel->recordOn();
        control["play_on"] = sliceModel->playOn();
        control["play_enabled"] = sliceModel->playEnabled();
        slice["control"] = control;

        QJsonObject dsp;
        dsp["agc_mode"] = sliceModel->agcMode();
        dsp["agc_threshold"] = sliceModel->agcThreshold();
        dsp["nb"] = QJsonObject{{"enabled", sliceModel->nbOn()}, {"level", sliceModel->nbLevel()}};
        dsp["nr"] = QJsonObject{{"enabled", sliceModel->nrOn()}, {"level", sliceModel->nrLevel()}};
        dsp["anf"] = QJsonObject{{"enabled", sliceModel->anfOn()}, {"level", sliceModel->anfLevel()}};
        dsp["lms_nr"] = QJsonObject{{"enabled", sliceModel->nrlOn()}, {"level", sliceModel->nrlLevel()}};
        dsp["speex_nr"] = QJsonObject{{"enabled", sliceModel->nrsOn()}, {"level", sliceModel->nrsLevel()}};
        dsp["rnnoise"] = sliceModel->rnnOn();
        dsp["nrf"] = QJsonObject{{"enabled", sliceModel->nrfOn()}, {"level", sliceModel->nrfLevel()}};
        dsp["lms_anf"] = QJsonObject{{"enabled", sliceModel->anflOn()}, {"level", sliceModel->anflLevel()}};
        dsp["anft"] = sliceModel->anftOn();
        dsp["apf"] = QJsonObject{{"enabled", sliceModel->apfOn()}, {"level", sliceModel->apfLevel()}};
        slice["dsp"] = dsp;

        QJsonObject diversity;
        diversity["enabled"] = sliceModel->diversity();
        diversity["is_parent"] = sliceModel->isDiversityParent();
        diversity["is_child"] = sliceModel->isDiversityChild();
        diversity["index"] = sliceModel->diversityIndex();
        diversity["esc_enabled"] = sliceModel->escEnabled();
        diversity["esc_gain"] = sliceModel->escGain();
        diversity["esc_phase_shift_deg"] = sliceModel->escPhaseShift();
        slice["diversity"] = diversity;

        QJsonObject tuning;
        tuning["squelch_on"] = sliceModel->squelchOn();
        tuning["squelch_level"] = sliceModel->squelchLevel();
        tuning["rit_on"] = sliceModel->ritOn();
        tuning["rit_hz"] = sliceModel->ritFreq();
        tuning["xit_on"] = sliceModel->xitOn();
        tuning["xit_hz"] = sliceModel->xitFreq();
        tuning["step_hz"] = sliceModel->stepHz();
        tuning["step_list"] = toJsonArray(sliceModel->stepList());
        slice["tuning"] = tuning;

        QJsonObject digital;
        digital["dax_channel"] = sliceModel->daxChannel();
        digital["rtty_mark_hz"] = sliceModel->rttyMark();
        digital["rtty_shift_hz"] = sliceModel->rttyShift();
        digital["digl_offset_hz"] = sliceModel->diglOffset();
        digital["digu_offset_hz"] = sliceModel->diguOffset();
        slice["digital"] = digital;

        QJsonObject fm;
        fm["tone_mode"] = sliceModel->fmToneMode();
        fm["tone_value"] = sliceModel->fmToneValue();
        fm["repeater_offset_dir"] = sliceModel->repeaterOffsetDir();
        fm["repeater_offset_mhz"] = sliceModel->fmRepeaterOffsetFreq();
        fm["tx_offset_mhz"] = sliceModel->txOffsetFreq();
        fm["deviation_hz"] = sliceModel->fmDeviation();
        slice["fm"] = fm;

        if (PanadapterModel* pan = panadapter(sliceModel->panId()))
            slice["panadapter_state"] = panToJson(pan, m_activePanId);

        slice["meters"] = m_meterModel.metersForSource("SLC", sliceModel->sliceId());
        slices.append(slice);
    }
    snapshot["slices"] = slices;

    QJsonObject counts;
    counts["panadapters"] = panadapters.size();
    counts["slices"] = slices.size();
    counts["meters_total"] = allMeters.size();
    counts["global_meters"] = globalMeters.size();
    counts["slice_meters"] = sliceMeterCount;
    snapshot["counts"] = counts;

    return snapshot;
}

} // namespace AetherSDR
