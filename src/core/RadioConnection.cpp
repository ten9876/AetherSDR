#include "RadioConnection.h"
#include "LogManager.h"

namespace AetherSDR {

// The SmartSDR TCP session is maintained by the socket itself on a LAN.
// No application-level keepalive command exists in API v1.x.
// The timer is kept here for future use (e.g. client_set commands, meters).
static constexpr int HEARTBEAT_INTERVAL_MS = 30000;

RadioConnection::RadioConnection(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected,
            this, &RadioConnection::onSocketConnected);
    connect(&m_socket, &QTcpSocket::disconnected,
            this, &RadioConnection::onSocketDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead,
            this, &RadioConnection::onReadyRead);
    connect(&m_socket, &QAbstractSocket::errorOccurred,
            this, &RadioConnection::onSocketError);

    m_heartbeat.setInterval(HEARTBEAT_INTERVAL_MS);
    connect(&m_heartbeat, &QTimer::timeout, this, &RadioConnection::onHeartbeat);
}

RadioConnection::~RadioConnection()
{
    disconnectFromRadio();
}

// ─── Connection management ────────────────────────────────────────────────────

void RadioConnection::connectToRadio(const RadioInfo& info)
{
    connectToHost(info.address, info.port);
}

void RadioConnection::connectToHost(const QHostAddress& address, quint16 port)
{
    if (m_state != ConnectionState::Disconnected) {
        qCWarning(lcConnection) << "RadioConnection: already connected or connecting";
        return;
    }
    qCDebug(lcConnection) << "RadioConnection: connecting to" << address.toString() << ":" << port;
    setState(ConnectionState::Connecting);
    m_socket.connectToHost(address, port);
}

void RadioConnection::disconnectFromRadio()
{
    m_heartbeat.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.disconnectFromHost();
    m_seqCounter = 1;
    m_handle = 0;
}

// ─── Command dispatch ─────────────────────────────────────────────────────────

void RadioConnection::writeCommand(quint32 seq, const QString& command)
{
    if (!isConnected()) {
        qCWarning(lcConnection) << "RadioConnection::writeCommand: not connected, dropping seq" << seq;
        return;
    }
    const QByteArray data = CommandParser::buildCommand(seq, command);
    if (command.startsWith("ping")) {
        m_lastPingSeq = seq;
        m_pingStopwatch.restart();   // start RTT clock here in the worker thread
    } else {
        qCDebug(lcConnection) << "TX:" << data.trimmed();
    }
    m_socket.write(data);
}

// ─── Socket slots ─────────────────────────────────────────────────────────────

void RadioConnection::onSocketConnected()
{
    qCDebug(lcConnection) << "RadioConnection: TCP connected";
    // Disable Nagle — send commands immediately without buffering
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // Do NOT set Connected yet — wait for V and H messages from the radio.
    // The radio sends V<version>\n then H<handle>\n immediately after TCP accept.
}

void RadioConnection::onSocketDisconnected()
{
    qCDebug(lcConnection) << "RadioConnection: TCP disconnected";
    m_heartbeat.stop();
    setState(ConnectionState::Disconnected);
    emit disconnected();
}

void RadioConnection::onSocketError(QAbstractSocket::SocketError /*error*/)
{
    const QString msg = m_socket.errorString();
    qCWarning(lcConnection) << "RadioConnection: socket error:" << msg;
    setState(ConnectionState::Error);
    emit errorOccurred(msg);
}

void RadioConnection::onReadyRead()
{
    m_readBuffer.append(m_socket.readAll());

    // Process all complete lines (\n-terminated)
    int newlinePos;
    while ((newlinePos = m_readBuffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_readBuffer.left(newlinePos)).trimmed();
        m_readBuffer.remove(0, newlinePos + 1);
        if (!line.isEmpty())
            processLine(line);
    }
}

void RadioConnection::onHeartbeat()
{
    // No-op: keepalive is not a valid command on API v1.x.
    // If the radio requires periodic traffic (e.g. v2+ APIs), send
    // a benign command such as "client program AetherSDR" here.
}

// ─── Line processing ──────────────────────────────────────────────────────────

void RadioConnection::processLine(const QString& line)
{
    // Suppress noisy high-frequency messages (ping replies, GPS status)
    const bool isGps = line.contains("|gps ");
    bool isPingReply = false;
    if (m_lastPingSeq && line.startsWith("R")) {
        // Check if this response matches the last ping sequence number
        isPingReply = line.startsWith(QString("R%1|").arg(m_lastPingSeq));
    }
    if (!isGps && !isPingReply)
        qCDebug(lcConnection) << "RX:" << line;

    ParsedMessage msg = CommandParser::parseLine(line);
    emit messageReceived(msg);

    switch (msg.type) {
    case MessageType::Version:
        emit versionReceived(msg.object);
        break;

    case MessageType::Handle:
        m_handle.store(msg.handle, std::memory_order_relaxed);
        qCDebug(lcConnection) << "RadioConnection: assigned handle" << QString::number(m_handle, 16);
        setState(ConnectionState::Connected);
        m_heartbeat.start();
        // Full command sequence (sub → client gui → udpport → slice list) is
        // orchestrated by RadioModel::onConnected() so every command waits for
        // its R response before the next is sent.
        emit connected();
        break;

    case MessageType::Response: {
        // Measure ping RTT entirely in the worker thread for true network latency.
        if (m_lastPingSeq && msg.sequence == m_lastPingSeq && m_pingStopwatch.isValid())
            emit pingRttMeasured(static_cast<int>(m_pingStopwatch.elapsed()));
        // Dispatch response to RadioModel via queued signal (safe cross-thread).
        emit responseArrived(msg.sequence, msg.resultCode, msg.object);
        break;
    }

    case MessageType::Status:
        emit statusReceived(msg.object, msg.kvs);
        break;

    default:
        break;
    }
}

void RadioConnection::setState(ConnectionState s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace AetherSDR
