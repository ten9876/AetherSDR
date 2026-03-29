#pragma once

#include "CommandParser.h"
#include "RadioDiscovery.h"

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QString>
#include <QHostAddress>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <atomic>

namespace AetherSDR {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

// Manages the TCP connection to a FlexRadio and provides the
// command/response layer of the SmartSDR API.
//
// Usage:
//   RadioConnection conn;
//   conn.moveToThread(&workerThread);
//   conn.connectToRadio(radioInfo);    // invoke via QMetaObject from main thread
//   connect(&conn, &RadioConnection::statusReceived, this, &MyClass::onStatus);
class RadioConnection : public QObject {
    Q_OBJECT
    Q_PROPERTY(ConnectionState state READ state NOTIFY stateChanged)

public:
    explicit RadioConnection(QObject* parent = nullptr);
    ~RadioConnection() override;

    ConnectionState state() const       { return m_state; }
    quint32 clientHandle() const        { return m_handle; }
    bool isConnected() const            { return m_state == ConnectionState::Connected; }
    QHostAddress radioAddress() const   { return m_socket.peerAddress(); }
    quint16      localTcpPort() const   { return m_socket.localPort(); }

    // Connect to a discovered radio
    void connectToRadio(const RadioInfo& info);
    // Connect directly by address/port
    void connectToHost(const QHostAddress& address, quint16 port = 4992);
    void disconnectFromRadio();

    // Write a pre-sequenced command to the socket.
    // Must be called from the thread that owns this object.
    // Use nextSeq() to allocate a sequence number from any thread.
    void writeCommand(quint32 seq, const QString& command);

    // Allocate the next sequence number. Thread-safe (atomic).
    quint32 nextSeq() { return m_seqCounter.fetch_add(1); }

signals:
    void stateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    // Emitted for every parsed incoming line
    void messageReceived(const ParsedMessage& msg);

    // Convenience signals for common message types
    void statusReceived(const QString& object, const QMap<QString, QString>& kvs);
    void versionReceived(const QString& version);

    // Emitted when a response arrives (replaces callback mechanism).
    // When RadioConnection runs in a worker thread this signal is queued,
    // so the slot fires on the receiver's thread automatically.
    void responseArrived(quint32 seq, int resultCode, QString body);

    // True network RTT measured entirely inside the worker thread.
    void pingRttMeasured(int ms);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onReadyRead();
    void onHeartbeat();

private:
    void processLine(const QString& line);
    void setState(ConnectionState s);

    QTcpSocket    m_socket;
    QByteArray    m_readBuffer;
    QTimer        m_heartbeat;
    QElapsedTimer m_pingStopwatch;   // started at write, stopped at read — worker thread only

    ConnectionState          m_state{ConnectionState::Disconnected};
    std::atomic<quint32>     m_handle{0};
    std::atomic<quint32>     m_seqCounter{1};
    quint32                  m_lastPingSeq{0};
};

} // namespace AetherSDR
