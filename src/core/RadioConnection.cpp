#include "RadioConnection.h"
#include "LogManager.h"

namespace AetherSDR {

static constexpr int HEARTBEAT_INTERVAL_MS = 30000;

RadioConnection::RadioConnection(QObject* parent)
    : QObject(parent)
{
}

RadioConnection::~RadioConnection()
{
    disconnectFromRadio();
}

void RadioConnection::init()
{
    m_socket = new QTcpSocket(this);
    m_heartbeat = new QTimer(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &RadioConnection::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &RadioConnection::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &RadioConnection::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &RadioConnection::onSocketError);

    m_heartbeat->setInterval(HEARTBEAT_INTERVAL_MS);
    connect(m_heartbeat, &QTimer::timeout, this, &RadioConnection::onHeartbeat);
}

void RadioConnection::connectToRadio(const RadioInfo& info)
{
    connectToHost(info.address, info.port);
}

void RadioConnection::connectToHost(const QHostAddress& address, quint16 port)
{
    if (m_state.load() != ConnectionState::Disconnected) return;
    if (!m_socket) { qCWarning(lcConnection) << "RadioConnection: init() not called"; return; }
    qCDebug(lcConnection) << "RadioConnection: connecting to" << address.toString() << ":" << port;
    setState(ConnectionState::Connecting);
    m_socket->connectToHost(address, port);
}

void RadioConnection::disconnectFromRadio()
{
    if (m_heartbeat) m_heartbeat->stop();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
    m_handle = 0;
}

void RadioConnection::writeCommand(quint32 seq, const QString& command)
{
    if (!isConnected() || !m_socket) return;

    const QByteArray data = CommandParser::buildCommand(seq, command);
    if (command.startsWith("ping")) {
        m_lastPingSeq = seq;
        m_pingStopwatch.restart();
    } else {
        qCDebug(lcConnection) << "TX:" << data.trimmed();
    }
    m_socket->write(data);
}

void RadioConnection::onSocketConnected()
{
    qCDebug(lcConnection) << "RadioConnection: TCP connected";
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_radioAddr = m_socket->peerAddress();
    m_localPort = m_socket->localPort();
}

void RadioConnection::onSocketDisconnected()
{
    qCDebug(lcConnection) << "RadioConnection: TCP disconnected";
    if (m_heartbeat) m_heartbeat->stop();
    setState(ConnectionState::Disconnected);
    emit disconnected();
}

void RadioConnection::onSocketError(QAbstractSocket::SocketError)
{
    if (!m_socket) return;
    const QString msg = m_socket->errorString();
    qCWarning(lcConnection) << "RadioConnection: socket error:" << msg;
    setState(ConnectionState::Error);
    emit errorOccurred(msg);
}

void RadioConnection::onReadyRead()
{
    if (!m_socket) return;
    m_readBuffer.append(m_socket->readAll());
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
}

void RadioConnection::processLine(const QString& line)
{
    const bool isGps = line.contains("|gps ");
    bool isPingReply = false;
    if (m_lastPingSeq && line.startsWith("R")) {
        isPingReply = line.startsWith(QString("R%1|").arg(m_lastPingSeq));
        if (isPingReply) {
            emit pingRttMeasured(static_cast<int>(m_pingStopwatch.elapsed()));
            m_lastPingSeq = 0;
        }
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
        m_handle = msg.handle;
        qCDebug(lcConnection) << "RadioConnection: assigned handle" << QString::number(m_handle, 16);
        setState(ConnectionState::Connected);
        if (m_heartbeat) m_heartbeat->start();
        emit connected();
        break;
    case MessageType::Response:
        emit commandResponse(msg.sequence, msg.resultCode, msg.object);
        break;
    case MessageType::Status:
        emit statusReceived(msg.object, msg.kvs);
        break;
    default:
        break;
    }
}

void RadioConnection::setState(ConnectionState s)
{
    m_state.store(s);
    emit stateChanged(s);
}

} // namespace AetherSDR
