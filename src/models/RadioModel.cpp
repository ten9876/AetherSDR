#include "RadioModel.h"
#include "core/CommandParser.h"
#include <QDebug>
#include <QRegularExpression>

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
        m_connection.sendCommand(cmd);
    });

    // Forward transmit model commands to the radio
    connect(&m_transmitModel, &TransmitModel::commandReady, this, [this](const QString& cmd){
        m_connection.sendCommand(cmd);
    });

    // Forward equalizer model commands to the radio
    connect(&m_equalizerModel, &EqualizerModel::commandReady, this, [this](const QString& cmd){
        m_connection.sendCommand(cmd);
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
    return m_connection.isConnected();
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
    m_lastInfo = info;
    m_intentionalDisconnect = false;
    m_reconnectTimer.stop();
    m_name  = info.name;
    m_model = info.model;
    m_connection.connectToRadio(info);
}

void RadioModel::disconnectFromRadio()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_connection.disconnectFromRadio();
}

void RadioModel::setTransmit(bool tx)
{
    m_connection.sendCommand(QString("xmit %1").arg(tx ? 1 : 0));
}

// ─── Connection slots ─────────────────────────────────────────────────────────

void RadioModel::onConnected()
{
    qDebug() << "RadioModel: connected";
    m_panResized = false;
    emit connectionStateChanged(true);

    // Full command sequence — each step waits for its R response before sending the next.
    // sub slice all → sub pan all → sub tx all → sub atu all → sub amplifier all
    //   → sub meter all → sub audio all → client gui → client program → ...
    m_connection.sendCommand("sub slice all", [this](int, const QString&) {
      m_connection.sendCommand("sub pan all", [this](int, const QString&) {
      m_connection.sendCommand("sub tx all", [this](int, const QString&) {
        m_connection.sendCommand("sub atu all", [this](int, const QString&) {
        m_connection.sendCommand("sub amplifier all", [this](int, const QString&) {
          m_connection.sendCommand("sub meter all", [this](int, const QString&) {
            m_connection.sendCommand("sub audio all", [this](int, const QString&) {
            // EQ status arrives automatically — no subscription needed on fw v1.4.0.0
            m_connection.sendCommand("client gui", [this](int code, const QString&) {
        if (code != 0)
            qWarning() << "RadioModel: client gui failed, code" << Qt::hex << code;

        // Identify this client to the radio; station name allows nCAT/nDAX to
        // attach to this instance rather than creating a separate one.
        m_connection.sendCommand("client program AetherSDR");
        m_connection.sendCommand("client station AetherSDR");

        // Request available mic inputs (comma-separated response: "MIC,BAL,LINE,ACC")
        m_connection.sendCommand("mic list", [this](int code, const QString& body) {
            if (code == 0) {
                QStringList inputs = body.trimmed().split(',', Qt::SkipEmptyParts);
                m_transmitModel.setMicInputList(inputs);
                qDebug() << "RadioModel: mic inputs:" << inputs;
            }
        });

        if (!m_panStream.isRunning())
            m_panStream.start(&m_connection);  // also sends one-byte UDP registration

        const quint16 udpPort = m_panStream.localPort();
        m_connection.sendCommand(
            QString("client udpport %1").arg(udpPort),
            [this, udpPort](int code2, const QString&) {
                if (code2 == 0)
                    qDebug() << "RadioModel: UDP port" << udpPort << "registered via client udpport";
                else
                    qDebug() << "RadioModel: client udpport returned error" << Qt::hex << code2;

                m_connection.sendCommand("slice list",
                    [this](int code3, const QString& body) {
                        if (code3 != 0) {
                            qWarning() << "RadioModel: slice list failed, code" << Qt::hex << code3;
                            return;
                        }
                        const QStringList ids = body.trimmed().split(' ', Qt::SkipEmptyParts);
                        qDebug() << "RadioModel: slice list ->" << (ids.isEmpty() ? "(empty)" : body);

                        if (ids.isEmpty()) {
                            createDefaultSlice();
                        } else {
                            qDebug() << "RadioModel: SmartConnect — keeping existing pan"
                                     << m_panId << "and" << m_slices.size() << "slice(s)";
                        }

                        for (auto* s : m_slices) {
                            for (const QString& cmd : s->drainPendingCommands())
                                m_connection.sendCommand(cmd);
                        }

                        // Request a remote audio RX stream (uncompressed).
                        // The radio creates an ExtDataWithStream VITA-49 stream
                        // (PCC 0x03E3, float32 stereo big-endian) and sends it
                        // to our registered UDP port.
                        m_connection.sendCommand(
                            "stream create type=remote_audio_rx compression=none",
                            [](int code, const QString& body) {
                                if (code == 0)
                                    qDebug() << "RadioModel: remote_audio_rx stream created, id:" << body;
                                else
                                    qWarning() << "RadioModel: stream create remote_audio_rx failed, code"
                                               << Qt::hex << code << "body:" << body;
                            });
                    });
            });
    }); // client gui
            }); // sub audio all
          }); // sub meter all
        }); // sub amplifier all
        }); // sub atu all
      }); // sub tx all
      }); // sub pan all
    }); // sub slice all
}

void RadioModel::onDisconnected()
{
    qDebug() << "RadioModel: disconnected";
    m_panStream.stop();
    m_panId.clear();
    m_waterfallId.clear();
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
    m_version = v;
    emit infoChanged();
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

void RadioModel::onStatusReceived(const QString& object,
                                  const QMap<QString, QString>& kvs)
{
    if (object == "radio") {
        handleRadioStatus(kvs);
        return;
    }

    static const QRegularExpression sliceRe(R"(^slice\s+(\d+)$)");
    const auto sliceMatch = sliceRe.match(object);
    if (sliceMatch.hasMatch()) {
        const bool removed = kvs.value("in_use") == "0";
        handleSliceStatus(sliceMatch.captured(1).toInt(), kvs, removed);
        return;
    }

    // Meter status uses '#'-separated tokens and is handled by onMessageReceived().

    // "display pan 0x40000000 center=14.1 bandwidth=0.2 ..."
    static const QRegularExpression panRe(R"(^display pan\s+(0x[0-9A-Fa-f]+)$)");
    if (object.startsWith("display pan")) {
        const auto m = panRe.match(object);
        if (m.hasMatch() && m_panId.isEmpty())
            m_panId = m.captured(1);
        handlePanadapterStatus(kvs);
        return;
    }

    // "display waterfall 0x42000000 auto_black=1 ..."
    static const QRegularExpression wfRe(R"(^display waterfall\s+(0x[0-9A-Fa-f]+)$)");
    if (object.startsWith("display waterfall")) {
        const auto m = wfRe.match(object);
        if (m.hasMatch() && m_waterfallId.isEmpty())
            m_waterfallId = m.captured(1);
        if (!m_wfConfigured && !m_waterfallId.isEmpty() && m_connection.isConnected()) {
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

    // Interlock status: "interlock state=TRANSMITTING ..."
    // TODO: track interlock state for TX button feedback
    if (object == "interlock") {
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

    // WAN, etc. — informational, ignore for now.
}

void RadioModel::handleRadioStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;
    if (kvs.contains("model")) { m_model = kvs["model"]; changed = true; }
    if (changed) emit infoChanged();
}

void RadioModel::handleSliceStatus(int id,
                                    const QMap<QString, QString>& kvs,
                                    bool removed)
{
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
            m_connection.sendCommand(cmd);
        });
        m_slices.append(s);
        s->applyStatus(kvs);  // populate frequency/mode before notifying UI
        emit sliceAdded(s);
        return;                // applyStatus already called below; skip second call
    }

    s->applyStatus(kvs);

    // Send any queued commands (e.g. if GUI changed freq before status arrived)
    if (m_connection.isConnected()) {
        for (const QString& cmd : s->drainPendingCommands())
            m_connection.sendCommand(cmd);
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
    if (!m_panResized && !m_panId.isEmpty() && m_connection.isConnected()) {
        m_panResized = true;
        configurePan();
    }
}

void RadioModel::configurePan()
{
    if (m_panId.isEmpty()) return;
    m_connection.sendCommand(
        QString("display pan set %1 fps=25 average=0").arg(m_panId),
        [this](int code, const QString&) {
            if (code != 0)
                qWarning() << "RadioModel: display pan set fps/average failed, code" << Qt::hex << code;

            // Request higher-resolution FFT bins.  Firmware v1.4.0.0 may reject
            // x_pixels with 0x5000002D but the attempt is harmless.
            if (!m_panId.isEmpty())
                m_connection.sendCommand(
                    QString("display pan set %1 x_pixels=1024").arg(m_panId),
                    [](int code2, const QString&) {
                        if (code2 != 0)
                            qDebug() << "RadioModel: display pan set x_pixels=1024 rejected, code"
                                     << Qt::hex << code2 << "(expected on v1.4.0.0)";
                    });
        });
}

void RadioModel::configureWaterfall()
{
    if (m_waterfallId.isEmpty()) return;

    // Disable automatic black-level and set a fixed threshold.
    // FlexLib uses "display panafall set" addressed to the waterfall stream ID.
    const QString cmd = QString("display panafall set %1 auto_black=0 black_level=15 color_gain=50")
                            .arg(m_waterfallId);
    m_connection.sendCommand(cmd, [this](int code, const QString&) {
        if (code != 0) {
            qDebug() << "RadioModel: display panafall set waterfall failed, code"
                     << Qt::hex << code << "— trying display waterfall set";
            // Fallback for firmware that doesn't support panafall addressing
            m_connection.sendCommand(
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

    m_connection.sendCommand("panadapter create",
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

            m_connection.sendCommand(sliceCmd,
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
    }
}

} // namespace AetherSDR
