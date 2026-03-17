#include "RadioModel.h"
#include "core/CommandParser.h"
#include "core/AppSettings.h"
#include <QDebug>
#include <QRegularExpression>
#include <QSettings>

namespace AetherSDR {

RadioModel::RadioModel(QObject* parent)
    : QObject(parent)
{
    connect(&m_connection, &RadioConnection::statusReceived,
            this, &RadioModel::onStatusReceived);
    connect(&m_connection, &RadioConnection::messageReceived,
            this, &RadioModel::onMessageReceived);
    connect(&m_connection, &RadioConnection::connected,
            this, &RadioModel::onConnected);
    connect(&m_connection, &RadioConnection::disconnected,
            this, &RadioModel::onDisconnected);
    connect(&m_connection, &RadioConnection::errorOccurred,
            this, &RadioModel::onConnectionError);
    connect(&m_connection, &RadioConnection::versionReceived,
            this, &RadioModel::onVersionReceived);

    // Forward VITA-49 meter packets to MeterModel
    connect(&m_panStream, &PanadapterStream::meterDataReady,
            &m_meterModel, &MeterModel::updateValues);

    // Forward tuner commands to the radio
    connect(&m_tunerModel, &TunerModel::commandReady, this, [this](const QString& cmd){
        sendCmd(cmd);
    });

    // Forward transmit model commands to the radio
    connect(&m_transmitModel, &TransmitModel::commandReady, this, [this](const QString& cmd){
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

    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(3000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_intentionalDisconnect && !m_lastInfo.address.isNull()) {
            qDebug() << "RadioModel: auto-reconnecting to" << m_lastInfo.address.toString();
            m_connection.connectToRadio(m_lastInfo);
        }
    });
}

bool RadioModel::isConnected() const
{
    return m_connection.isConnected() || (m_wanConn && m_wanConn->isConnected());
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
    m_connection.connectToRadio(info);
}

void RadioModel::connectViaWan(WanConnection* wan, const QString& publicIp, quint16 udpPort)
{
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

    // The WAN connection is already established (TLS + wan validate done)
    // and has already received V/H. Trigger onConnected manually.
    if (wan->isConnected())
        onConnected();
}

void RadioModel::disconnectFromRadio()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    if (m_wanConn) {
        m_wanConn->disconnectFromRadio();
        m_wanConn = nullptr;
    } else {
        m_connection.disconnectFromRadio();
    }
}

void RadioModel::setTransmit(bool tx)
{
    // Immediately stop TX audio when unkeying — don't wait for radio's
    // interlock state to transition through UNKEY_REQUESTED → READY.
    if (!tx)
        m_transmitModel.setTransmitting(false);

    sendCmd(QString("xmit %1").arg(tx ? 1 : 0));
}

void RadioModel::addSlice()
{
    if (m_panId.isEmpty()) {
        qWarning() << "RadioModel::addSlice: no panadapter, cannot create slice";
        return;
    }

    // Create a new slice on the current panadapter at the center frequency.
    // The radio will pick a default mode (USB) and antenna.
    const QString freq = QString::number(m_panCenterMhz, 'f', 6);
    const QString cmd = QString("slice create pan=%1 freq=%2").arg(m_panId, freq);

    qDebug() << "RadioModel::addSlice:" << cmd;
    m_connection.sendCommand(cmd, [](int code, const QString& body) {
        if (code != 0)
            qWarning() << "RadioModel: slice create failed, code"
                       << Qt::hex << code << "body:" << body;
        else
            qDebug() << "RadioModel: new slice created, index =" << body;
    });
}

void RadioModel::setPanBandwidth(double bandwidthMhz)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 bandwidth=%2")
            .arg(m_panId).arg(bandwidthMhz, 0, 'f', 6));
}

void RadioModel::setPanCenter(double centerMhz)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 center=%2")
            .arg(m_panId).arg(centerMhz, 0, 'f', 6));
}

void RadioModel::setPanDbmRange(float minDbm, float maxDbm)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 min_dbm=%2 max_dbm=%3")
            .arg(m_panId)
            .arg(static_cast<double>(minDbm), 0, 'f', 2)
            .arg(static_cast<double>(maxDbm), 0, 'f', 2));
}

void RadioModel::setPanWnb(bool on)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 wnb=%2").arg(m_panId).arg(on ? 1 : 0));
}

void RadioModel::setPanWnbLevel(int level)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 wnb_level=%2").arg(m_panId).arg(level));
}

void RadioModel::setPanRfGain(int gain)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 rfgain=%2").arg(m_panId).arg(gain));
}

// ── Display controls — FFT ─────────────────────────────────────────────────

void RadioModel::setPanAverage(int frames)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 average=%2").arg(m_panId).arg(frames));
}

void RadioModel::setPanFps(int fps)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 fps=%2").arg(m_panId).arg(fps));
}

void RadioModel::setPanWeightedAverage(bool on)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 weighted_average=%2").arg(m_panId).arg(on ? 1 : 0));
}

// ── Display controls — Waterfall ──────────────────────────────────────────

void RadioModel::setWaterfallColorGain(int gain)
{
    if (m_waterfallId.isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 color_gain=%2").arg(m_waterfallId).arg(gain));
}

void RadioModel::setWaterfallBlackLevel(int level)
{
    if (m_waterfallId.isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 black_level=%2").arg(m_waterfallId).arg(level));
}

void RadioModel::setWaterfallAutoBlack(bool on)
{
    if (m_waterfallId.isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 auto_black=%2").arg(m_waterfallId).arg(on ? 1 : 0));
}

void RadioModel::setWaterfallLineDuration(int ms)
{
    if (m_waterfallId.isEmpty()) return;
    sendCmd(
        QString("display panafall set %1 line_duration=%2").arg(m_waterfallId).arg(ms));
}

void RadioModel::setPanNoiseFloorPosition(int pos)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 noise_floor_position=%2").arg(m_panId).arg(pos));
}

void RadioModel::setPanNoiseFloorEnable(bool on)
{
    if (m_panId.isEmpty()) return;
    sendCmd(
        QString("display pan set %1 noise_floor_position_enable=%2").arg(m_panId).arg(on ? 1 : 0));
}

// ─── Connection slots ─────────────────────────────────────────────────────────

void RadioModel::onConnected()
{
    qDebug() << "RadioModel: connected";
    m_panResized = false;
    emit connectionStateChanged(true);
    // Delay network monitor until after client gui registration
    // (pings sent before registration cause "Malformed command" on WAN)

    // Register as GUI client FIRST — required before subscriptions,
    // especially on WAN/SmartLink where the radio is stricter.
    const QString clientId = AppSettings::instance().value("GUIClientID").toString();

    if (m_wanConn) {
        // On WAN: wait for client ip response before sending client gui.
        // The radio needs time after wan validate to accept GUI registration.
        sendCmd("client ip", [this, clientId](int, const QString& body) {
            qDebug() << "RadioModel: client ip ->" << body.trimmed();
            registerAsGuiClient(clientId);
        });
    } else {
        registerAsGuiClient(clientId);
    }
}

void RadioModel::registerAsGuiClient(const QString& clientId)
{
    sendCmd(QString("client gui %1").arg(clientId), [this](int code, const QString&) {
        if (code != 0)
            qWarning() << "RadioModel: client gui failed, code" << Qt::hex << code;

        sendCmd("client program AetherSDR");
        sendCmd("client station AetherSDR");
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
            sendCmd("sub xvtr all", [this](int, const QString&) {
            // Memory status arrives via normal status handler — no subscription needed.
            // "sub memory all" returns 500000A3 (invalid subscription object).
        // Request available mic inputs (comma-separated response: "MIC,BAL,LINE,ACC")
        sendCmd("mic list", [this](int code, const QString& body) {
            if (code == 0) {
                QStringList inputs = body.trimmed().split(',', Qt::SkipEmptyParts);
                m_transmitModel.setMicInputList(inputs);
                qDebug() << "RadioModel: mic inputs:" << inputs;
            }
        });

        if (!m_panStream.isRunning()) {
            if (m_wanConn)
                m_panStream.startWan(QHostAddress(m_wanPublicIp), m_wanUdpPort);
            else
                m_panStream.start(&m_connection);  // also sends one-byte UDP registration
        }

        const quint16 udpPort = m_panStream.localPort();
        sendCmd(
            QString("client udpport %1").arg(udpPort),
            [this, udpPort](int code2, const QString&) {
                if (code2 == 0)
                    qDebug() << "RadioModel: UDP port" << udpPort << "registered via client udpport";
                else
                    qDebug() << "RadioModel: client udpport returned error" << Qt::hex << code2;

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
                        qDebug() << "RadioModel: info — callsign:" << m_callsign
                                 << "region:" << m_region << "options:" << m_radioOptions;
                        emit infoChanged();
                    });

                sendCmd("slice list",
                    [this](int code3, const QString& body) {
                        if (code3 != 0) {
                            qWarning() << "RadioModel: slice list failed, code" << Qt::hex << code3;
                            return;
                        }
                        const QStringList ids = body.trimmed().split(' ', Qt::SkipEmptyParts);
                        qDebug() << "RadioModel: slice list ->" << (ids.isEmpty() ? "(empty)" : body);

                        if (ids.isEmpty() || m_slices.isEmpty()) {
                            // No slices at all, or no slices belonging to us
                            qDebug() << "RadioModel: creating own slice (total on radio:"
                                     << ids.size() << ", ours:" << m_slices.size() << ")";
                            // Use saved frequency/mode from last session if available
                            QSettings settings("AetherSDR", "AetherSDR");
                            double lastFreq = settings.value("lastFrequency", 0.0).toDouble();
                            QString lastMode = settings.value("lastMode").toString();
                            if (lastFreq > 0.0) {
                                createDefaultSlice(
                                    QString::number(lastFreq, 'f', 6),
                                    lastMode.isEmpty() ? "USB" : lastMode);
                            } else {
                                createDefaultSlice();
                            }
                        } else {
                            qDebug() << "RadioModel: SmartConnect — using our pan"
                                     << m_panId << "and" << m_slices.size() << "slice(s)";
                        }

                        for (auto* s : m_slices) {
                            for (const QString& cmd : s->drainPendingCommands())
                                sendCmd(cmd);
                        }

                        // Request a remote audio RX stream (uncompressed).
                        // The radio creates an ExtDataWithStream VITA-49 stream
                        // (PCC 0x03E3, float32 stereo big-endian) and sends it
                        // to our registered UDP port.
                        sendCmd(
                            "stream create type=remote_audio_rx compression=none",
                            [this](int code, const QString& body) {
                                if (code == 0) {
                                    m_rxAudioStreamId = body.trimmed();
                                    qDebug() << "RadioModel: remote_audio_rx stream created, id:" << m_rxAudioStreamId;
                                } else
                                    qWarning() << "RadioModel: stream create remote_audio_rx failed, code"
                                               << Qt::hex << code << "body:" << body;
                            });

                        // Request DAX TX audio stream (PC mic → radio)
                        sendCmd(
                            "stream create type=dax_tx",
                            [this](int code, const QString& body) {
                                if (code == 0) {
                                    quint32 id = body.trimmed().toUInt(nullptr, 16);
                                    qDebug() << "RadioModel: dax_tx stream created, id:"
                                             << Qt::hex << id;
                                    sendCmd("transmit set mic_selection=PC");
                                    emit txAudioStreamReady(id);
                                } else {
                                    qWarning() << "RadioModel: stream create dax_tx failed, code"
                                               << Qt::hex << code << "body:" << body;
                                }
                            });
                    });
            });
            // Request global profile list
            sendCmd("profile global info");
            sendCmd("sub tnf all");
            }); // sub xvtr all
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

void RadioModel::onDisconnected()
{
    qDebug() << "RadioModel: disconnected";
    stopNetworkMonitor();
    m_panStream.stop();
    m_panId.clear();
    m_waterfallId.clear();
    m_ownedSliceIds.clear();
    m_panResized = false;
    m_wfConfigured = false;
    emit connectionStateChanged(false);

    if (!m_intentionalDisconnect && !m_lastInfo.address.isNull()) {
        qDebug() << "RadioModel: unexpected disconnect — reconnecting in 3s";
        m_reconnectTimer.start();
    }
}

void RadioModel::onConnectionError(const QString& msg)
{
    qWarning() << "RadioModel: connection error:" << msg;
    emit connectionError(msg);
    emit connectionStateChanged(false);
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

    connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
        if (!isConnected()) {
            stopNetworkMonitor();
            return;
        }
        // Send ping and measure RTT
        m_pingStopwatch.restart();
        sendCmd("ping", [this](int code, const QString&) {
            if (code != 0) return;
            m_lastPingRtt = static_cast<int>(m_pingStopwatch.elapsed());
            evaluateNetworkQuality();
        });
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
    const int currentErrors = m_panStream.packetErrorCount();
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
    return m_panStream.packetErrorCount();
}

int RadioModel::packetTotalCount() const
{
    return m_panStream.packetTotalCount();
}

qint64 RadioModel::rxBytes() const
{
    return m_panStream.totalRxBytes();
}

void RadioModel::handleMemoryStatus(int index, const QMap<QString, QString>& kvs)
{
    // Check for removal — radio sends either "in_use=0" or "removed" (no value)
    if (kvs.value("in_use") == "0" || kvs.contains("removed")) {
        m_memories.remove(index);
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
    qDebug() << "RadioModel::sendCommand:" << cmd
             << "connected:" << isConnected() << "wan:" << (m_wanConn != nullptr);
    this->sendCmd(cmd);
}

quint32 RadioModel::sendCmd(const QString& command, ResponseCallback cb)
{
    if (m_wanConn)
        return m_wanConn->sendCommand(command, std::move(cb));
    return m_connection.sendCommand(command, std::move(cb));
}

quint32 RadioModel::clientHandle() const
{
    if (m_wanConn)
        return m_wanConn->clientHandle();
    return m_connection.clientHandle();
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
            qDebug() << "RadioModel: memory status for index" << idx
                     << "keys:" << merged.keys();
            handleMemoryStatus(idx, merged);
            return;
        }
    }

    // Meter status uses '#'-separated tokens and is handled by onMessageReceived().

    // "display pan 0x40000000 center=14.1 bandwidth=0.2 ..."
    // Only process status for OUR panadapter (matching client_handle or first unclaimed).
    static const QRegularExpression panRe(R"(^display pan\s+(0x[0-9A-Fa-f]+)$)");
    if (object.startsWith("display pan")) {
        const auto m = panRe.match(object);
        if (m.hasMatch()) {
            const QString panId = m.captured(1);
            if (m_panId.isEmpty()) {
                // Claim this pan only if it belongs to us
                if (kvs.contains("client_handle")) {
                    quint32 owner = kvs["client_handle"].toUInt(nullptr, 16);
                    if (owner == clientHandle()) {
                        m_panId = panId;
                        updateStreamFilters();
                        qDebug() << "RadioModel: claimed panadapter" << m_panId;
                    } else {
                        return;  // not our panadapter, ignore
                    }
                } else {
                    m_panId = panId;  // no client_handle field, assume ours
                }
            } else if (panId != m_panId) {
                return;  // not our panadapter, ignore
            }
        }
        handlePanadapterStatus(kvs);
        return;
    }

    // "display waterfall 0x42000000 auto_black=1 ..."
    // Only process status for OUR waterfall (matching client_handle).
    static const QRegularExpression wfRe(R"(^display waterfall\s+(0x[0-9A-Fa-f]+)$)");
    if (object.startsWith("display waterfall")) {
        const auto m = wfRe.match(object);
        if (m.hasMatch()) {
            const QString wfId = m.captured(1);
            if (m_waterfallId.isEmpty()) {
                if (kvs.contains("client_handle")) {
                    quint32 owner = kvs["client_handle"].toUInt(nullptr, 16);
                    if (owner == clientHandle()) {
                        m_waterfallId = wfId;
                        updateStreamFilters();
                        qDebug() << "RadioModel: claimed waterfall" << m_waterfallId;
                    } else {
                        return;  // not our waterfall
                    }
                } else {
                    m_waterfallId = wfId;
                }
            } else if (wfId != m_waterfallId) {
                return;  // not our waterfall
            }
        }
        if (!m_wfConfigured && !m_waterfallId.isEmpty() && isConnected()) {
            m_wfConfigured = true;
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
                if (m_tunerModel.handle().isEmpty())
                    m_tunerModel.setHandle(handle);
                m_tunerModel.applyStatus(kvs);
            }

            // Detect power amplifier (PGXL or any non-TGXL amp)
            if (!model.isEmpty() && model != "TunerGeniusXL" && !m_hasAmplifier) {
                m_hasAmplifier = true;
                qDebug() << "RadioModel: power amplifier detected, model=" << model;
                emit amplifierChanged(true);
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

    // Interlock status: "interlock state=TRANSMITTING ..."
    if (object == "interlock") {
        if (kvs.contains("state")) {
            bool tx = (kvs["state"] == "TRANSMITTING");
            m_transmitModel.setTransmitting(tx);
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
    if (kvs.contains("mute_local_audio_when_remote")) {
        m_muteLocalWhenRemote = kvs["mute_local_audio_when_remote"] == "1";
        changed = true;
    }
    if (kvs.contains("freq_error_ppb")) {
        m_freqErrorPpb = kvs["freq_error_ppb"].toInt();
        changed = true;
    }
    if (kvs.contains("low_latency_digital_modes")) {
        m_lowLatencyDigital = kvs["low_latency_digital_modes"] == "1";
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
    if (audioChanged) emit audioOutputChanged();
    if (changed) emit infoChanged();
}

void RadioModel::setLineoutGain(int v)
{
    v = std::clamp(v, 0, 100);
    sendCmd(QString("mixer lineout gain %1").arg(v));
}

void RadioModel::setLineoutMute(bool m)
{
    sendCmd(QString("mixer lineout mute %1").arg(m ? 1 : 0));
}

void RadioModel::setHeadphoneGain(int v)
{
    v = std::clamp(v, 0, 100);
    sendCmd(QString("mixer headphone gain %1").arg(v));
}

void RadioModel::setHeadphoneMute(bool m)
{
    sendCmd(QString("mixer headphone mute %1").arg(m ? 1 : 0));
}

void RadioModel::setFrontSpeakerMute(bool m)
{
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
            qDebug() << "RadioModel: slice" << id << "is ours (client_handle match)";
        } else if (owner != 0) {
            qDebug() << "RadioModel: slice" << id << "belongs to another client"
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
        qDebug() << "RadioModel: ignoring slice" << id << "status (not in owned set)";
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

void RadioModel::handlePanadapterStatus(const QMap<QString, QString>& kvs)
{
    bool freqChanged  = false;
    bool levelChanged = false;

    if (kvs.contains("center")) {
        m_panCenterMhz = kvs["center"].toDouble();
        freqChanged = true;
    }
    if (kvs.contains("bandwidth")) {
        m_panBandwidthMhz = kvs["bandwidth"].toDouble();
        freqChanged = true;
    }
    if (freqChanged)
        emit panadapterInfoChanged(m_panCenterMhz, m_panBandwidthMhz);

    if (kvs.contains("min_dbm") || kvs.contains("max_dbm")) {
        const float minDbm = kvs.value("min_dbm", "-130").toFloat();
        const float maxDbm = kvs.value("max_dbm", "-20").toFloat();
        m_panStream.setDbmRange(minDbm, maxDbm);
        emit panadapterLevelChanged(minDbm, maxDbm);
        levelChanged = true;
    }
    Q_UNUSED(levelChanged)

    if (kvs.contains("ant_list")) {
        const QStringList ants = kvs["ant_list"].split(',', Qt::SkipEmptyParts);
        if (ants != m_antList) {
            m_antList = ants;
            emit antListChanged(m_antList);
        }
    }

    // Configure the panadapter once we know its ID.
    // x_pixels is not settable on firmware v1.4.0.0 (always returns 5000002D),
    // so we only set fps and disable averaging.
    if (!m_panResized && !m_panId.isEmpty() && isConnected()) {
        m_panResized = true;
        configurePan();
    }
}

void RadioModel::updateStreamFilters()
{
    quint32 panId = m_panId.isEmpty() ? 0 : m_panId.toUInt(nullptr, 16);
    quint32 wfId  = m_waterfallId.isEmpty() ? 0 : m_waterfallId.toUInt(nullptr, 16);
    m_panStream.setOwnedStreamIds(panId, wfId);
}

void RadioModel::configurePan()
{
    if (m_panId.isEmpty()) return;

    // Set xpixels and ypixels — the radio requires explicit dimensions before
    // it will produce valid FFT data.  Note: the command uses "xpixels" (no
    // underscore) but status messages report "x_pixels" (with underscore).
    sendCmd(
        QString("display pan set %1 xpixels=1024 ypixels=700").arg(m_panId),
        [this](int code, const QString&) {
            if (code != 0)
                qWarning() << "RadioModel: display pan set xpixels/ypixels failed, code"
                           << Qt::hex << code;
            else
                qDebug() << "RadioModel: panadapter xpixels=1024 ypixels=700 set OK";
        });

    sendCmd(
        QString("display pan set %1 fps=25 average=0 min_dbm=-130 max_dbm=-40").arg(m_panId),
        [](int code, const QString&) {
            if (code != 0)
                qWarning() << "RadioModel: display pan set fps/average/dbm failed, code" << Qt::hex << code;
        });
}

void RadioModel::configureWaterfall()
{
    if (m_waterfallId.isEmpty()) return;

    // Disable automatic black-level and set a fixed threshold.
    // FlexLib uses "display panafall set" addressed to the waterfall stream ID.
    const QString cmd = QString("display panafall set %1 auto_black=0 black_level=15 color_gain=50")
                            .arg(m_waterfallId);
    sendCmd(cmd, [this](int code, const QString&) {
        if (code != 0) {
            qDebug() << "RadioModel: display panafall set waterfall failed, code"
                     << Qt::hex << code << "— trying display waterfall set";
            // Fallback for firmware that doesn't support panafall addressing
            sendCmd(
                QString("display waterfall set %1 auto_black=0 black_level=15 color_gain=50")
                    .arg(m_waterfallId),
                [](int code2, const QString&) {
                    if (code2 != 0)
                        qWarning() << "RadioModel: display waterfall set also failed, code"
                                   << Qt::hex << code2;
                    else
                        qDebug() << "RadioModel: waterfall configured via display waterfall set";
                });
        } else {
            qDebug() << "RadioModel: waterfall configured (auto_black=0 black_level=15 color_gain=50)";
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
    qDebug() << "RadioModel: standalone mode — creating panadapter + slice"
             << freqMhz << mode << antenna;

    sendCmd("panadapter create",
        [this, freqMhz, mode, antenna](int code, const QString& body) {
            if (code != 0) {
                qWarning() << "RadioModel: panadapter create failed, code" << Qt::hex << code
                           << "body:" << body;
                return;
            }

            qDebug() << "RadioModel: panadapter create response body:" << body;

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

            qDebug() << "RadioModel: panadapter created, pan_id =" << panId;

            if (panId.isEmpty()) {
                qWarning() << "RadioModel: panadapter create returned empty pan_id";
                return;
            }

            const QString sliceCmd =
                QString("slice create pan=%1 freq=%2 antenna=%3 mode=%4")
                    .arg(panId, freqMhz, antenna, mode);

            sendCmd(sliceCmd,
                [panId](int code2, const QString& body2) {
                    if (code2 != 0) {
                        qWarning() << "RadioModel: slice create failed, code"
                                   << Qt::hex << code2 << "body:" << body2;
                    } else {
                        qDebug() << "RadioModel: slice created, index =" << body2;
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
            qDebug() << "RadioModel: TX profiles:" << profiles;
        } else if (key == "current") {
            m_transmitModel.setActiveProfile(val);
            qDebug() << "RadioModel: active TX profile:" << val;
        }
    } else if (profileType == "mic") {
        if (key == "list") {
            QStringList profiles = val.split('^', Qt::SkipEmptyParts);
            m_transmitModel.setMicProfileList(profiles);
            qDebug() << "RadioModel: mic profiles:" << profiles;
        } else if (key == "current") {
            m_transmitModel.setActiveMicProfile(val);
            qDebug() << "RadioModel: active mic profile:" << val;
        }
    } else if (profileType == "global") {
        if (key == "list") {
            m_globalProfiles = val.split('^', Qt::SkipEmptyParts);
            qDebug() << "RadioModel: global profiles:" << m_globalProfiles;
            emit globalProfilesChanged();
        } else if (key == "current") {
            m_activeGlobalProfile = val;
            qDebug() << "RadioModel: active global profile:" << val;
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
    m_panResized = false;
    m_wfConfigured = false;
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
                    "stream create type=remote_audio_rx compression=none",
                    [this](int code, const QString& body) {
                        if (code == 0) {
                            m_rxAudioStreamId = body.trimmed();
                            qDebug() << "RadioModel: remote_audio_rx stream re-created, id:" << m_rxAudioStreamId;
                        } else
                            qWarning() << "RadioModel: stream create remote_audio_rx failed, code"
                                       << Qt::hex << code << "body:" << body;
                    });
            });
    } else {
        sendCmd(
            "stream create type=remote_audio_rx compression=none",
            [this](int code, const QString& body) {
                if (code == 0) {
                    m_rxAudioStreamId = body.trimmed();
                    qDebug() << "RadioModel: remote_audio_rx stream re-created, id:" << m_rxAudioStreamId;
                } else
                    qWarning() << "RadioModel: stream create remote_audio_rx failed, code"
                               << Qt::hex << code << "body:" << body;
            });
    }

    sendCmd(
        "stream create type=dax_tx",
        [this](int code, const QString& body) {
            if (code == 0) {
                quint32 id = body.trimmed().toUInt(nullptr, 16);
                qDebug() << "RadioModel: dax_tx stream re-created, id:" << Qt::hex << id;
                sendCmd("transmit set mic_selection=PC");
                emit txAudioStreamReady(id);
            }
        });
}

} // namespace AetherSDR
