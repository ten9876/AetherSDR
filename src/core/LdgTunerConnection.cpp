#include "LdgTunerConnection.h"
#include "LogManager.h"

#include <QDebug>
#include <cmath>

namespace AetherSDR {

LdgTunerConnection::LdgTunerConnection(QObject* parent)
    : QObject(parent)
{
#ifdef HAVE_SERIALPORT
    m_tuneTimer.setSingleShot(true);
    m_tuneTimer.setInterval(TUNE_TIMEOUT_MS);
    connect(&m_tuneTimer, &QTimer::timeout, this, &LdgTunerConnection::onTuneTimeout);
    connect(&m_port, &QSerialPort::readyRead, this, &LdgTunerConnection::onReadyRead);
    connect(&m_port, &QSerialPort::errorOccurred, this, &LdgTunerConnection::onError);
#endif
}

LdgTunerConnection::~LdgTunerConnection()
{
    disconnect();
}

bool LdgTunerConnection::isConnected() const
{
#ifdef HAVE_SERIALPORT
    return m_connected;
#else
    return false;
#endif
}

QString LdgTunerConnection::portName() const
{
#ifdef HAVE_SERIALPORT
    return m_port.portName();
#else
    return {};
#endif
}

void LdgTunerConnection::connectToTuner(const QString& portName)
{
#ifdef HAVE_SERIALPORT
    if (m_connected)
        disconnect();

    m_port.setPortName(portName);
    m_port.setBaudRate(BAUD_RATE);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        qCWarning(lcTuner) << "LdgTunerConnection: failed to open" << portName
                           << m_port.errorString();
        emit connectionFailed(m_port.errorString());
        return;
    }

    m_connected = true;
    m_readBuf.clear();
    qCInfo(lcTuner) << "LdgTunerConnection: connected to" << portName;
    emit connected();

    // Enter streaming mode to start receiving meter data
    sendStreamingMode();
#else
    Q_UNUSED(portName)
    qCWarning(lcTuner) << "LdgTunerConnection: serial port support not compiled";
    emit connectionFailed("Serial port support not available");
#endif
}

void LdgTunerConnection::disconnect()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;

    m_tuneTimer.stop();
    m_tuning = false;
    m_port.close();
    m_connected = false;
    m_readBuf.clear();
    qCInfo(lcTuner) << "LdgTunerConnection: disconnected";
    emit disconnected();
#endif
}

void LdgTunerConnection::sendFullTune()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;
    qCDebug(lcTuner) << "LdgTunerConnection: sending Full Tune (F)";
    m_tuning = true;
    m_tuneTimer.start();
    sendCommand('F');
#endif
}

void LdgTunerConnection::sendMemoryTune()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;
    qCDebug(lcTuner) << "LdgTunerConnection: sending Memory Tune (T)";
    m_tuning = true;
    m_tuneTimer.start();
    sendCommand('T');
#endif
}

void LdgTunerConnection::sendBypass()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;
    qCDebug(lcTuner) << "LdgTunerConnection: sending Bypass (P)";
    sendCommand('P');
#endif
}

void LdgTunerConnection::sendAutoTuneMode()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;
    qCDebug(lcTuner) << "LdgTunerConnection: sending Auto Tune mode (C)";
    sendCommand('C');
#endif
}

void LdgTunerConnection::sendAntennaToggle()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;
    qCDebug(lcTuner) << "LdgTunerConnection: sending Antenna Toggle (A)";
    sendCommand('A');
#endif
}

void LdgTunerConnection::sendStreamingMode()
{
#ifdef HAVE_SERIALPORT
    if (!m_connected) return;
    qCDebug(lcTuner) << "LdgTunerConnection: entering Streaming mode (S)";
    sendCommand('S');
#endif
}

// ── Private implementation ─────────────────────────────────────────────────

#ifdef HAVE_SERIALPORT

void LdgTunerConnection::sendCommand(char cmdByte)
{
    // Protocol: wake byte (0x20), then enter control mode ('X'), then command
    QByteArray seq;
    seq.append(WAKE_BYTE);
    seq.append('X');
    seq.append(cmdByte);
    m_port.write(seq);
    m_port.flush();
}

void LdgTunerConnection::onReadyRead()
{
    m_readBuf.append(m_port.readAll());
    processBuffer();
}

void LdgTunerConnection::processBuffer()
{
    // Look for meter frames: 6 payload bytes followed by ";;"
    // Also look for tune response bytes in the buffer.
    while (m_readBuf.size() >= 2) {
        // Check for tune result bytes — single byte responses
        // The LDG tuner sends status bytes indicating tune outcome:
        //   Bit 7 set = tune complete
        //   Bit 0 = success (1) / fail (0)
        if (m_tuning && !m_readBuf.isEmpty()) {
            char b = m_readBuf.at(0);
            // Tune result byte has bit 7 set (0x80+)
            if (static_cast<unsigned char>(b) & 0x80) {
                bool success = static_cast<unsigned char>(b) & 0x01;
                m_tuning = false;
                m_tuneTimer.stop();
                qCInfo(lcTuner) << "LdgTunerConnection: tune result"
                                << (success ? "SUCCESS" : "FAIL");
                emit tuneFinished(success);
                m_readBuf.remove(0, 1);
                continue;
            }
        }

        // Look for meter frame delimiter ";;"
        int delimPos = m_readBuf.indexOf(";;");
        if (delimPos < 0) {
            // No complete frame yet — keep only the last potential partial
            // frame (up to 8 bytes max: 6 payload + 2 delim).
            if (m_readBuf.size() > 64)
                m_readBuf = m_readBuf.right(16);
            break;
        }

        // Extract 6-byte payload before ";;"
        if (delimPos >= 6) {
            QByteArray frame = m_readBuf.mid(delimPos - 6, 6);
            parseMeterFrame(frame);
        }
        m_readBuf.remove(0, delimPos + 2);
    }
}

void LdgTunerConnection::parseMeterFrame(const QByteArray& frame)
{
    if (frame.size() < 6) return;

    // NeoLDG meter frame layout:
    // Byte 0: forward power MSB
    // Byte 1: forward power LSB
    // Byte 2: reflected power MSB
    // Byte 3: reflected power LSB
    // Byte 4-5: status/flags
    unsigned int fwdRaw = (static_cast<unsigned char>(frame[0]) << 8)
                        | static_cast<unsigned char>(frame[1]);
    unsigned int refRaw = (static_cast<unsigned char>(frame[2]) << 8)
                        | static_cast<unsigned char>(frame[3]);

    // Raw ADC values — model-specific calibration is handled by LdgTunerModel
    float fwd = static_cast<float>(fwdRaw);
    float ref = static_cast<float>(refRaw);

    // Basic SWR calculation from raw forward/reflected
    float swr = 1.0f;
    if (fwd > 0.0f) {
        float rho = (ref > 0.0f) ? std::sqrt(ref / fwd) : 0.0f;
        swr = (rho < 0.999f) ? (1.0f + rho) / (1.0f - rho) : 99.9f;
    }

    emit meterDataReceived(fwd, ref, swr);
}

void LdgTunerConnection::onError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;

    // ResourceError typically means the USB adapter was unplugged
    if (error == QSerialPort::ResourceError && m_connected) {
        qCWarning(lcTuner) << "LdgTunerConnection: device disconnected unexpectedly";
        m_tuning = false;
        m_tuneTimer.stop();
        m_connected = false;
        m_port.close();
        emit errorOccurred("LDG tuner disconnected unexpectedly");
        emit disconnected();
        return;
    }

    QString msg = m_port.errorString();
    qCWarning(lcTuner) << "LdgTunerConnection: serial error:" << msg;
    emit errorOccurred(msg);
}

void LdgTunerConnection::onTuneTimeout()
{
    if (!m_tuning) return;
    m_tuning = false;
    qCWarning(lcTuner) << "LdgTunerConnection: tune timed out after"
                       << TUNE_TIMEOUT_MS << "ms";
    emit tuneFinished(false);
}

#endif // HAVE_SERIALPORT

} // namespace AetherSDR
