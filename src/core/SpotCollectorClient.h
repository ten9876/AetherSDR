#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QFile>
#include <QString>
#include <atomic>
#include "DxClusterClient.h"  // for DxSpot

namespace AetherSDR {

// DXLab SpotCollector UDP listener — receives spot push packets
// in standard "DX de" cluster format on a configurable UDP port (default 9999).
class SpotCollectorClient : public QObject {
    Q_OBJECT

public:
    explicit SpotCollectorClient(QObject* parent = nullptr);
    ~SpotCollectorClient() override;

    void initialize();  // must be called from the target thread after moveToThread()
    void startListening(quint16 port);
    void stopListening();
    bool isListening() const { return m_listening; }

    QString logFilePath() const;

signals:
    void listening();
    void stopped();
    void spotReceived(const DxSpot& spot);
    void rawLineReceived(const QString& line);

private slots:
    void onReadyRead();

private:
    bool parseDxSpotLine(const QString& line, DxSpot& spot) const;

    QUdpSocket* m_socket{nullptr};
    QFile       m_logFile;
    quint16     m_port{9999};
    std::atomic<bool> m_listening{false};
};

} // namespace AetherSDR
