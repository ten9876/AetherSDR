#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QFile>
#include <QString>
#include <QTime>
#include <atomic>

namespace AetherSDR {

struct DxSpot {
    QString spotterCall;    // W3LPL
    double  freqMhz{0.0};  // 14.025 (converted from kHz)
    QString dxCall;         // JA1ABC
    QString comment;        // "CW big signal"
    QTime   utcTime;        // 18:24 UTC
    QString source;         // "Cluster", "RBN", "WSJT-X"
    QString color;          // #AARRGGBB for radio spot color (optional)
    int     snr{0};         // signal-to-noise ratio (dB), for WSJT-X decodes
    int     lifetimeSec{0}; // 0 = use source default from AppSettings
};

// Telnet client for DX cluster nodes (DX Spider, AR-Cluster, CC Cluster).
// Connects, logs in with callsign, parses "DX de" spot lines, and emits
// spotReceived() for each parsed spot.
class DxClusterClient : public QObject {
    Q_OBJECT

public:
    explicit DxClusterClient(QObject* parent = nullptr);
    ~DxClusterClient() override;

    void connectToCluster(const QString& host, quint16 port, const QString& callsign);
    void disconnect();
    bool isConnected() const { return m_connected; }

    void sendCommand(const QString& cmd);

    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QString logFilePath() const;
    void setLogFileName(const QString& name) { m_logFileName = name; }

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& error);
    void spotReceived(const DxSpot& spot);
    void rawLineReceived(const QString& line);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onReconnectTimer();

private:
    bool parseDxSpotLine(const QString& line, DxSpot& spot) const;
    bool isLoginPrompt(const QString& line) const;
    void handleLine(const QString& line);
    void stripTelnetIAC();
    // Arm the exponential-backoff reconnect timer. Guards against double-scheduling
    // (errorOccurred + timeout can both fire for one failed attempt). No-op when
    // m_intentionalDisconnect is set or the timer is already active (#2380).
    void scheduleReconnect();

    QTcpSocket* m_socket;
    QByteArray  m_readBuffer;
    QTimer*     m_reconnectTimer;
    QFile       m_logFile;

    QString m_logFileName{"dxcluster.log"};
    QString m_host;
    quint16 m_port{7300};
    QString m_callsign;
    std::atomic<bool> m_connected{false};
    bool    m_loggedIn{false};
    bool    m_intentionalDisconnect{false};
    int     m_reconnectAttempts{0};
    int     m_connectEpoch{0};  // incremented each connectToCluster(); guards stale timeouts

    static constexpr int MaxReconnectDelayMs    = 60000;
    static constexpr int InitialReconnectDelayMs = 5000;
    static constexpr int ConnectTimeoutMs        = 10000;
};

} // namespace AetherSDR
