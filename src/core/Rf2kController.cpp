#include "Rf2kController.h"
#include "models/SliceModel.h"

#include <QDebug>

namespace AetherSDR {

Rf2kController::Rf2kController(QObject* parent)
    : QObject(parent)
{
}

void Rf2kController::setEnabled(bool on)
{
    m_enabled = on;
    if (on && m_txSlice)
        sendFrequencyUpdate(m_txSlice->frequency());
}

void Rf2kController::setTarget(const QString& ip, quint16 port)
{
    m_targetIp = ip;
    m_targetPort = port;
}

void Rf2kController::setRadioNr(int nr)
{
    m_radioNr = qBound(1, nr, 2);
}

void Rf2kController::setTxSlice(SliceModel* slice)
{
    // Disconnect previous slice
    if (m_freqConn) QObject::disconnect(m_freqConn);
    if (m_modeConn) QObject::disconnect(m_modeConn);
    if (m_txConn)   QObject::disconnect(m_txConn);

    m_txSlice = slice;
    if (!slice) return;

    m_freqConn = connect(slice, &SliceModel::frequencyChanged,
                         this, &Rf2kController::sendFrequencyUpdate);
    m_modeConn = connect(slice, &SliceModel::modeChanged,
                         this, [this](const QString&) {
        if (m_txSlice)
            sendFrequencyUpdate(m_txSlice->frequency());
    });

    // Send initial state
    if (m_enabled)
        sendFrequencyUpdate(slice->frequency());
}

void Rf2kController::sendFrequencyUpdate(double mhz)
{
    if (!m_enabled || m_targetIp.isEmpty()) return;
    if (!m_txSlice) return;

    sendPacket(mhz, m_txSlice->mode(), false);
}

void Rf2kController::sendPacket(double freqMhz, const QString& mode, bool transmitting)
{
    // N1MM frequency unit: tens of Hz (14.225 MHz → 1422500)
    qint64 freqTensHz = static_cast<qint64>(freqMhz * 1e5 + 0.5);

    QByteArray xml;
    xml.append("<RadioInfo>");
    xml.append("<RadioNr>");
    xml.append(QByteArray::number(m_radioNr));
    xml.append("</RadioNr>");
    xml.append("<Freq>");
    xml.append(QByteArray::number(freqTensHz));
    xml.append("</Freq>");
    xml.append("<TXFreq>");
    xml.append(QByteArray::number(freqTensHz));
    xml.append("</TXFreq>");
    xml.append("<ActiveRadioNr>");
    xml.append(QByteArray::number(m_radioNr));
    xml.append("</ActiveRadioNr>");
    xml.append("<IsTransmitting>");
    xml.append(transmitting ? "True" : "False");
    xml.append("</IsTransmitting>");
    xml.append("<Mode>");
    xml.append(mode.toUtf8());
    xml.append("</Mode>");
    xml.append("</RadioInfo>");

    m_socket.writeDatagram(xml, QHostAddress(m_targetIp), m_targetPort);
}

} // namespace AetherSDR
