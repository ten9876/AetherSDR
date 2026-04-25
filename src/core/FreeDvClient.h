#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <atomic>
#include "DxClusterClient.h"  // for DxSpot

namespace AetherSDR {

// FreeDV Reporter spot client — connects to qso.freedv.org via WebSocket
// with manual Engine.IO v4 / Socket.IO v4 framing. Tracks station state
// (callsign, frequency, grid, mode) and emits spotReceived() for each
// rx_report event (station heard another station).
//
// Uses view-only auth — no callsign needed, read-only access.
class FreeDvClient : public QObject {
    Q_OBJECT

public:
    explicit FreeDvClient(QObject* parent = nullptr);
    ~FreeDvClient() override;

    void initialize();  // must be called from the target thread after moveToThread()
    void startConnection();
    void stopConnection();
    bool isConnected() const { return m_connected.load(); }

    QString logFilePath() const;

signals:
    void started();
    void stopped();
    void connectionError(const QString& error);
    void spotReceived(const DxSpot& spot);
    void rawLineReceived(const QString& line);

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString& message);
    void onWsError(QAbstractSocket::SocketError err);
    void onReconnectTimer();

private:
    // Per-station state tracked by Socket.IO session ID
    struct StationInfo {
        QString callsign;
        QString gridSquare;
        double  freqMhz{0.0};
        QString mode;
        bool    rxOnly{false};
    };

    void handleEngineIO(const QString& raw);
    void handleSocketIO(const QString& payload);
    void handleEvent(const QString& eventName, const QJsonObject& data);
    void onNewConnection(const QJsonObject& data);
    void onFreqChange(const QJsonObject& data);
    void onRxReport(const QJsonObject& data);
    void onTxReport(const QJsonObject& data);
    void onRemoveConnection(const QJsonObject& data);
    void onBulkUpdate(const QJsonArray& pairs);

    QWebSocket*  m_ws{nullptr};
    QTimer*      m_pingTimer{nullptr};
    QTimer*      m_reconnectTimer{nullptr};
    QFile        m_logFile;

    QHash<QString, StationInfo> m_stations;  // sid → station info

    std::atomic<bool> m_connected{false};
    bool    m_intentionalDisconnect{false};
    int     m_reconnectAttempts{0};
    int     m_pingIntervalMs{25000};

    static constexpr const char* WsUrl =
        "wss://qso.freedv.org/socket.io/?EIO=4&transport=websocket";
    static constexpr int MaxReconnectDelayMs     = 60000;
    static constexpr int InitialReconnectDelayMs  = 5000;
};

} // namespace AetherSDR
