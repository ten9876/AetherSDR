#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>

namespace AetherSDR {

class SliceModel;

// Sends N1MM-format RadioInfo UDP broadcast packets to an RF2K+/RF2K-S
// power amplifier whenever the TX slice frequency or mode changes.
// The amplifier parses the frequency to select the correct bandpass
// filter and ATU preset automatically.
//
// Packet format: standard N1MM Logger+ <RadioInfo> XML over UDP.
// Frequency unit: tens of Hz (14.225 MHz → 1422500).
// Default port: 12060 (configurable on the amplifier).
class Rf2kController : public QObject {
    Q_OBJECT

public:
    explicit Rf2kController(QObject* parent = nullptr);

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    void setTarget(const QString& ip, quint16 port);
    QString targetIp() const { return m_targetIp; }
    quint16 targetPort() const { return m_targetPort; }

    void setRadioNr(int nr);
    int radioNr() const { return m_radioNr; }

    // Connect to a TX slice to track its frequency/mode changes.
    void setTxSlice(SliceModel* slice);

public slots:
    void sendFrequencyUpdate(double mhz);

private:
    void sendPacket(double freqMhz, const QString& mode, bool transmitting);

    QUdpSocket m_socket;
    QString    m_targetIp;
    quint16    m_targetPort{12060};
    int        m_radioNr{1};
    bool       m_enabled{false};

    SliceModel* m_txSlice{nullptr};
    QMetaObject::Connection m_freqConn;
    QMetaObject::Connection m_modeConn;
    QMetaObject::Connection m_txConn;
};

} // namespace AetherSDR
