#include "DaxStreamManager.h"
#include "PanadapterStream.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "RadioConnection.h"

#include <QDebug>

namespace AetherSDR {

DaxStreamManager::DaxStreamManager(RadioModel* model, PanadapterStream* panStream,
                                   QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_panStream(panStream)
{
    // Forward DAX audio from PanadapterStream to our signal
    connect(m_panStream, &PanadapterStream::daxAudioReady,
            this, &DaxStreamManager::daxAudioReady);
}

void DaxStreamManager::requestDaxStreams()
{
    if (!m_model || !m_model->isConnected()) {
        qWarning() << "DaxStreamManager: not connected, cannot request DAX streams";
        return;
    }

    auto* conn = m_model->connection();

    // First, assign DAX channels to slices.
    // Slice 0 → DAX 1, Slice 1 → DAX 2, etc.
    const auto slices = m_model->slices();
    for (int ch = 1; ch <= MAX_DAX_CHANNELS; ++ch) {
        int sliceIdx = ch - 1;
        if (sliceIdx < slices.size()) {
            auto* slice = slices[sliceIdx];
            qInfo() << "DaxStreamManager: assigning slice" << slice->sliceId()
                    << "→ DAX channel" << ch;
            slice->setDaxChannel(ch);
        }
    }

    // Then create DAX RX streams.
    // SmartSDR fw v1.4.0.0: "stream create type=dax_rx dax_channel=N"
    // returns a hex stream ID in the response body.
    for (int ch = 1; ch <= MAX_DAX_CHANNELS; ++ch) {
        QString cmd = QStringLiteral("stream create type=dax_rx dax_channel=%1").arg(ch);
        qInfo() << "DaxStreamManager: sending:" << cmd;

        conn->sendCommand(cmd, [this, ch](int code, const QString& body) {
            if (code != 0) {
                qWarning() << "DaxStreamManager: failed to create DAX stream" << ch
                           << "error:" << Qt::hex << code << body;
                return;
            }

            // Response body contains the stream ID as hex
            bool ok;
            quint32 sid = body.trimmed().toUInt(&ok, 16);
            if (!ok) {
                qWarning() << "DaxStreamManager: invalid stream ID in response:" << body;
                return;
            }

            m_streamIds[ch] = sid;
            m_panStream->registerDaxStream(sid, ch);
            qInfo() << "DaxStreamManager: DAX channel" << ch
                    << "stream ID:" << Qt::hex << sid;
            emit streamCreated(ch, sid);
        });
    }
}

void DaxStreamManager::releaseDaxStreams()
{
    if (!m_model) return;

    auto* conn = m_model->connection();

    // Unregister streams from PanadapterStream
    for (auto it = m_streamIds.begin(); it != m_streamIds.end(); ++it) {
        m_panStream->unregisterDaxStream(it.value());
        emit streamRemoved(it.key());

        if (m_model->isConnected()) {
            // SmartSDR fw v1.4.0.0: "stream remove 0x<id>"
            QString cmd = QStringLiteral("stream remove 0x%1")
                              .arg(it.value(), 8, 16, QChar('0'));
            conn->sendCommand(cmd);
        }
    }
    m_streamIds.clear();

    // Reset DAX channel assignments on slices
    const auto slices = m_model->slices();
    for (auto* slice : slices) {
        if (slice->daxChannel() > 0)
            slice->setDaxChannel(0);
    }

    qInfo() << "DaxStreamManager: all DAX streams released";
}

quint32 DaxStreamManager::streamId(int channel) const
{
    return m_streamIds.value(channel, 0);
}

} // namespace AetherSDR
