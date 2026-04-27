#include "WebApiServer.h"
#include "LogManager.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/MeterModel.h"
#include "models/TransmitModel.h"
#include "models/PanadapterModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpSocket>
#include <QTimer>
#include <cmath>

#ifdef HAVE_WEBSOCKETS
#include <QWebSocketServer>
#include <QWebSocket>
#endif

namespace AetherSDR {

WebApiServer::WebApiServer(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(200);
    connect(m_meterTimer, &QTimer::timeout, this, &WebApiServer::broadcastMeters);
}

WebApiServer::~WebApiServer()
{
    stop();
}

bool WebApiServer::start(quint16 port)
{
    if (m_httpServer) {
        return m_httpServer->isListening();
    }

    // ── HTTP server for REST endpoints ──
    m_httpServer = new QTcpServer(this);
    if (!m_httpServer->listen(QHostAddress::Any, port)) {
        qCWarning(lcWebApi) << "WebApiServer: failed to listen on port" << port
                            << m_httpServer->errorString();
        delete m_httpServer;
        m_httpServer = nullptr;
        return false;
    }
    connect(m_httpServer, &QTcpServer::newConnection,
            this, &WebApiServer::onHttpConnection);

#ifdef HAVE_WEBSOCKETS
    // ── WebSocket server on port+1 ──
    m_wsServer = new QWebSocketServer(
        QStringLiteral("AetherSDR-WebAPI"),
        QWebSocketServer::NonSecureMode, this);

    quint16 wsPort = port + 1;
    if (!m_wsServer->listen(QHostAddress::Any, wsPort)) {
        qCWarning(lcWebApi) << "WebApiServer: WebSocket failed on port" << wsPort
                            << m_wsServer->errorString();
        // HTTP still works, just no WebSocket push
        delete m_wsServer;
        m_wsServer = nullptr;
    } else {
        connect(m_wsServer, &QWebSocketServer::newConnection,
                this, &WebApiServer::onWsNewConnection);
        qCInfo(lcWebApi) << "WebApiServer: WebSocket listening on port" << wsPort;
    }
#endif

    m_meterTimer->start();

    // Wire TX state changes for WebSocket broadcast.
    // TransmitModel::moxChanged fires when the transmitting flag transitions.
    if (m_model) {
        connect(&m_model->transmitModel(), &TransmitModel::moxChanged,
                this, [this](bool tx) {
            if (tx == m_lastTxState) {
                return;
            }
            m_lastTxState = tx;
            QJsonObject obj;
            obj["event"] = QStringLiteral("tx_state");
            obj["transmitting"] = tx;
            wsBroadcast(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        });
    }

    qCInfo(lcWebApi) << "WebApiServer: HTTP listening on port"
                     << m_httpServer->serverPort();
    return true;
}

void WebApiServer::stop()
{
    m_meterTimer->stop();

#ifdef HAVE_WEBSOCKETS
    if (m_wsServer) {
        for (auto* ws : m_wsClients) {
            ws->disconnect(this);
            ws->close();
            ws->deleteLater();
        }
        m_wsClients.clear();
        m_wsServer->close();
        delete m_wsServer;
        m_wsServer = nullptr;
    }
#endif

    if (m_httpServer) {
        for (auto& hc : m_httpClients) {
            hc.socket->disconnect(this);
            hc.socket->close();
            hc.socket->deleteLater();
        }
        m_httpClients.clear();
        m_httpServer->close();
        delete m_httpServer;
        m_httpServer = nullptr;
    }

    emit clientCountChanged(0);
    qCInfo(lcWebApi) << "WebApiServer: stopped";
}

bool WebApiServer::isRunning() const
{
    return m_httpServer && m_httpServer->isListening();
}

quint16 WebApiServer::port() const
{
    return m_httpServer ? m_httpServer->serverPort() : 0;
}

int WebApiServer::clientCount() const
{
    int count = 0;
#ifdef HAVE_WEBSOCKETS
    count += m_wsClients.size();
#endif
    return count;
}

// ── HTTP handling ───────────────────────────────────────────────────────────

void WebApiServer::onHttpConnection()
{
    while (m_httpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_httpServer->nextPendingConnection();
        HttpClient hc;
        hc.socket = socket;
        m_httpClients.append(hc);

        connect(socket, &QTcpSocket::readyRead,
                this, &WebApiServer::onHttpReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                this, &WebApiServer::onHttpDisconnected);
    }
}

void WebApiServer::onHttpReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    for (auto& hc : m_httpClients) {
        if (hc.socket == socket) {
            hc.buffer.append(socket->readAll());
            // Look for end of HTTP headers
            if (hc.buffer.contains("\r\n\r\n")) {
                handleHttpRequest(socket, hc.buffer);
                hc.buffer.clear();
            }
            // Guard against oversized requests
            if (hc.buffer.size() > 8192) {
                sendHttpResponse(socket, 413, "text/plain", "Request too large");
                socket->close();
            }
            return;
        }
    }
}

void WebApiServer::onHttpDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    for (int i = 0; i < m_httpClients.size(); ++i) {
        if (m_httpClients[i].socket == socket) {
            m_httpClients.removeAt(i);
            break;
        }
    }
    socket->deleteLater();
}

void WebApiServer::handleHttpRequest(QTcpSocket* socket, const QByteArray& request)
{
    // Parse the request line: "GET /api/radio HTTP/1.1\r\n..."
    int firstLine = request.indexOf("\r\n");
    if (firstLine < 0) {
        sendHttpResponse(socket, 400, "text/plain", "Bad request");
        return;
    }

    QByteArray requestLine = request.left(firstLine);
    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendHttpResponse(socket, 400, "text/plain", "Bad request");
        return;
    }

    QByteArray method = parts[0];
    QByteArray path = parts[1];

    // Only GET is supported (read-only monitoring API)
    if (method != "GET") {
        sendHttpResponse(socket, 405, "text/plain", "Method not allowed");
        return;
    }

    QByteArray body;
    if (path == "/api/radio") {
        body = buildRadioJson();
    } else if (path == "/api/slices") {
        body = buildSlicesJson();
    } else if (path == "/api/transmit") {
        body = buildTransmitJson();
    } else if (path == "/api/meters") {
        body = buildMetersJson();
    } else if (path == "/api/panadapters") {
        body = buildPanadaptersJson();
    } else {
        // Unknown path — return a simple API index
        QJsonObject index;
        index["service"] = QStringLiteral("AetherSDR Web API");
        index["version"] = QStringLiteral(AETHERSDR_VERSION);
        QJsonArray endpoints;
        endpoints.append(QStringLiteral("/api/radio"));
        endpoints.append(QStringLiteral("/api/slices"));
        endpoints.append(QStringLiteral("/api/transmit"));
        endpoints.append(QStringLiteral("/api/meters"));
        endpoints.append(QStringLiteral("/api/panadapters"));
        index["endpoints"] = endpoints;
#ifdef HAVE_WEBSOCKETS
        index["websocket"] = QStringLiteral("ws://<host>:%1/ws")
            .arg(m_httpServer->serverPort() + 1);
#endif
        body = QJsonDocument(index).toJson(QJsonDocument::Compact);
    }

    sendHttpResponse(socket, 200, "application/json", body);
}

void WebApiServer::sendHttpResponse(QTcpSocket* socket, int statusCode,
                                    const QByteArray& contentType,
                                    const QByteArray& body)
{
    QByteArray statusText;
    switch (statusCode) {
    case 200: statusText = "OK"; break;
    case 400: statusText = "Bad Request"; break;
    case 405: statusText = "Method Not Allowed"; break;
    case 413: statusText = "Payload Too Large"; break;
    default:  statusText = "Error"; break;
    }

    QByteArray response;
    response.append("HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n");
    response.append("Content-Type: " + contentType + "\r\n");
    response.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    // Close after response (HTTP/1.0 style — simple and correct for an API)
    connect(socket, &QTcpSocket::bytesWritten, socket, [socket]() {
        if (socket->bytesToWrite() == 0) {
            socket->close();
        }
    });
}

// ── JSON builders ───────────────────────────────────────────────────────────

QByteArray WebApiServer::buildRadioJson() const
{
    QJsonObject obj;
    if (!m_model) {
        obj["connected"] = false;
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    obj["connected"] = m_model->isConnected();
    obj["name"] = m_model->name();
    obj["model"] = m_model->model();
    obj["version"] = m_model->version();
    obj["serial"] = m_model->serial();
    obj["callsign"] = m_model->callsign();
    obj["nickname"] = m_model->nickname();

    if (m_model->isConnected()) {
        obj["sliceCount"] = m_model->slices().size();
        obj["panadapterCount"] = m_model->panadapters().size();
        obj["gpsStatus"] = m_model->gpsStatus();
        obj["gpsGrid"] = m_model->gpsGrid();
    }

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray WebApiServer::buildSlicesJson() const
{
    QJsonArray arr;
    if (!m_model) {
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    }

    for (const auto* slice : m_model->slices()) {
        QJsonObject s;
        s["id"] = slice->sliceId();
        s["frequency"] = slice->frequency();
        s["mode"] = slice->mode();
        s["filterLow"] = slice->filterLow();
        s["filterHigh"] = slice->filterHigh();
        s["active"] = slice->isActive();
        s["txSlice"] = slice->isTxSlice();
        s["locked"] = slice->isLocked();
        s["rxAntenna"] = slice->rxAntenna();
        s["txAntenna"] = slice->txAntenna();
        s["audioGain"] = static_cast<double>(slice->audioGain());
        s["audioMute"] = slice->audioMute();
        s["agcMode"] = slice->agcMode();
        s["nbOn"] = slice->nbOn();
        s["nrOn"] = slice->nrOn();
        s["anfOn"] = slice->anfOn();

        // S-meter for this slice
        int meterIdx = m_model->meterModel().findMeter(
            QStringLiteral("SLC"), QStringLiteral("LEVEL"), slice->sliceId());
        if (meterIdx >= 0) {
            s["sLevel"] = static_cast<double>(m_model->meterModel().value(meterIdx));
        }

        arr.append(s);
    }

    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QByteArray WebApiServer::buildTransmitJson() const
{
    QJsonObject obj;
    if (!m_model) {
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    const auto& tx = m_model->transmitModel();
    obj["transmitting"] = tx.isTransmitting();
    obj["tuning"] = tx.isTuning();
    obj["mox"] = tx.isMox();
    obj["rfPower"] = tx.rfPower();
    obj["tunePower"] = tx.tunePower();

    // Active TX slice info
    for (const auto* slice : m_model->slices()) {
        if (slice->isTxSlice()) {
            obj["txSliceId"] = slice->sliceId();
            obj["txFrequency"] = slice->frequency();
            obj["txMode"] = slice->mode();
            break;
        }
    }

    const auto& meters = m_model->meterModel();
    obj["fwdPower"] = static_cast<double>(meters.fwdPower());
    obj["swr"] = static_cast<double>(meters.swr());
    obj["paTemp"] = static_cast<double>(meters.paTemp());

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray WebApiServer::buildMetersJson() const
{
    QJsonObject obj;
    if (!m_model) {
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    const auto& meters = m_model->meterModel();
    obj["sLevel"] = static_cast<double>(meters.sLevel());
    obj["fwdPower"] = static_cast<double>(meters.fwdPower());
    obj["swr"] = static_cast<double>(meters.swr());
    obj["paTemp"] = static_cast<double>(meters.paTemp());
    obj["supplyVolts"] = static_cast<double>(meters.supplyVolts());
    obj["micLevel"] = static_cast<double>(meters.micLevel());
    obj["alc"] = static_cast<double>(meters.alc());

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray WebApiServer::buildPanadaptersJson() const
{
    QJsonArray arr;
    if (!m_model) {
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    }

    for (const auto* pan : m_model->panadapters()) {
        QJsonObject p;
        p["id"] = pan->panId();
        p["centerMhz"] = pan->centerMhz();
        p["bandwidthMhz"] = pan->bandwidthMhz();
        p["minDbm"] = static_cast<double>(pan->minDbm());
        p["maxDbm"] = static_cast<double>(pan->maxDbm());
        arr.append(p);
    }

    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ── WebSocket handling ──────────────────────────────────────────────────────

#ifdef HAVE_WEBSOCKETS

void WebApiServer::onWsNewConnection()
{
    while (m_wsServer->hasPendingConnections()) {
        QWebSocket* ws = m_wsServer->nextPendingConnection();
        m_wsClients.append(ws);

        connect(ws, &QWebSocket::disconnected,
                this, &WebApiServer::onWsDisconnected);

        qCInfo(lcWebApi) << "WebApiServer: WS client connected from"
                         << ws->peerAddress().toString();
        emit clientCountChanged(clientCount());

        // Send initial state snapshot
        QJsonObject init;
        init["event"] = QStringLiteral("init");
        init["radio"] = QJsonDocument::fromJson(buildRadioJson()).object();
        init["slices"] = QJsonDocument::fromJson(buildSlicesJson()).array();
        init["transmit"] = QJsonDocument::fromJson(buildTransmitJson()).object();
        init["meters"] = QJsonDocument::fromJson(buildMetersJson()).object();
        ws->sendTextMessage(QString::fromUtf8(
            QJsonDocument(init).toJson(QJsonDocument::Compact)));
    }
}

void WebApiServer::onWsDisconnected()
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) {
        return;
    }

    m_wsClients.removeAll(ws);
    ws->deleteLater();

    qCInfo(lcWebApi) << "WebApiServer: WS client disconnected,"
                     << m_wsClients.size() << "remaining";
    emit clientCountChanged(clientCount());
}

#endif // HAVE_WEBSOCKETS

void WebApiServer::wsBroadcast(const QByteArray& json)
{
#ifdef HAVE_WEBSOCKETS
    if (m_wsClients.isEmpty()) {
        return;
    }
    QString msg = QString::fromUtf8(json);
    for (auto* ws : m_wsClients) {
        ws->sendTextMessage(msg);
    }
#else
    Q_UNUSED(json)
#endif
}

// ── Wire slice signals for real-time push ───────────────────────────────────

void WebApiServer::wireSlice(SliceModel* slice)
{
    if (!slice) {
        return;
    }

    connect(slice, &SliceModel::frequencyChanged, this, [this, slice](double mhz) {
        QJsonObject obj;
        obj["event"] = QStringLiteral("slice_frequency");
        obj["sliceId"] = slice->sliceId();
        obj["frequency"] = mhz;
        wsBroadcast(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    });

    connect(slice, &SliceModel::modeChanged, this, [this, slice](const QString& mode) {
        QJsonObject obj;
        obj["event"] = QStringLiteral("slice_mode");
        obj["sliceId"] = slice->sliceId();
        obj["mode"] = mode;
        wsBroadcast(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    });

    connect(slice, &SliceModel::txSliceChanged, this, [this, slice](bool tx) {
        QJsonObject obj;
        obj["event"] = QStringLiteral("slice_tx");
        obj["sliceId"] = slice->sliceId();
        obj["txSlice"] = tx;
        wsBroadcast(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    });
}

// ── Periodic meter broadcast ────────────────────────────────────────────────

void WebApiServer::broadcastMeters()
{
#ifdef HAVE_WEBSOCKETS
    if (m_wsClients.isEmpty() || !m_model || !m_model->isConnected()) {
        return;
    }
#else
    return;
#endif

    QJsonObject obj;
    obj["event"] = QStringLiteral("meters");

    const auto& meters = m_model->meterModel();
    obj["sLevel"] = static_cast<double>(meters.sLevel());
    obj["fwdPower"] = static_cast<double>(meters.fwdPower());
    obj["swr"] = static_cast<double>(meters.swr());
    obj["paTemp"] = static_cast<double>(meters.paTemp());

    wsBroadcast(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace AetherSDR
