#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QByteArray>
#include <QTimer>

#ifdef HAVE_WEBSOCKETS
class QWebSocketServer;
class QWebSocket;
#endif

namespace AetherSDR {

class RadioModel;
class SliceModel;

// Lightweight web API server for remote monitoring of AetherSDR radio state.
//
// Provides two interfaces:
//   1. HTTP REST endpoints (GET only, no auth) for polling radio state as JSON:
//        GET /api/radio       — radio info + connection state
//        GET /api/slices      — all slices (frequency, mode, filter, S-meter)
//        GET /api/transmit    — TX state (power, SWR, tuning, MOX)
//        GET /api/meters      — convenience meters (S-level, fwd power, SWR, PA temp)
//        GET /api/panadapters — panadapter list with center/bandwidth
//
//   2. WebSocket endpoint (ws://<host>:<port>/ws) for real-time push updates:
//        Broadcasts JSON messages on slice frequency/mode changes, TX state
//        changes, and periodic meter snapshots (200ms cadence).
//
// Binds on all interfaces. Intended for LAN use — no TLS, no authentication.
// This is a read-only monitoring API; it does not accept commands.
//
// Requested in #2089 as a lighter-weight alternative to a full web GUI.
class WebApiServer : public QObject {
    Q_OBJECT

public:
    explicit WebApiServer(RadioModel* model, QObject* parent = nullptr);
    ~WebApiServer() override;

    bool start(quint16 port = 8089);
    void stop();

    bool isRunning() const;
    quint16 port() const;
    int clientCount() const;

    // Wire a slice for real-time WebSocket broadcasts
    void wireSlice(SliceModel* slice);

signals:
    void clientCountChanged(int count);

private slots:
    void onHttpConnection();
    void onHttpReadyRead();
    void onHttpDisconnected();
    void broadcastMeters();

#ifdef HAVE_WEBSOCKETS
    void onWsNewConnection();
    void onWsDisconnected();
#endif

private:
    void handleHttpRequest(QTcpSocket* socket, const QByteArray& request);
    void sendHttpResponse(QTcpSocket* socket, int statusCode,
                          const QByteArray& contentType, const QByteArray& body);

    // JSON builders for each endpoint
    QByteArray buildRadioJson() const;
    QByteArray buildSlicesJson() const;
    QByteArray buildTransmitJson() const;
    QByteArray buildMetersJson() const;
    QByteArray buildPanadaptersJson() const;

    void wsBroadcast(const QByteArray& json);

    RadioModel*   m_model;
    QTcpServer*   m_httpServer{nullptr};

    struct HttpClient {
        QTcpSocket* socket{nullptr};
        QByteArray  buffer;
    };
    QList<HttpClient> m_httpClients;

#ifdef HAVE_WEBSOCKETS
    QWebSocketServer* m_wsServer{nullptr};
    QList<QWebSocket*> m_wsClients;
#endif

    QTimer* m_meterTimer{nullptr};  // periodic meter broadcast (200ms)
    bool    m_lastTxState{false};   // for detecting TX state transitions
};

} // namespace AetherSDR
