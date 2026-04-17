#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QFile>
#include <QSet>
#include <atomic>
#include "DxClusterClient.h"  // for DxSpot

namespace AetherSDR {

// SOTA (Summits On The Air) spot client — polls the SOTAwatch API
// for recent activations. Emits spotReceived() for each new spot.
class SotaClient : public QObject {
    Q_OBJECT

public:
    explicit SotaClient(QObject* parent = nullptr);
    ~SotaClient() override;

    void startPolling(int intervalSec = 30);
    void stopPolling();
    bool isPolling() const { return m_polling; }

    QString logFilePath() const;

signals:
    void started();
    void stopped();
    void pollError(const QString& error);
    void spotReceived(const DxSpot& spot);
    void rawLineReceived(const QString& line);
    void pollComplete(int spotCount, int newCount);

private slots:
    void onPollTimer();

private:
    QNetworkAccessManager* m_nam;
    QTimer*     m_pollTimer;
    QFile       m_logFile;
    QSet<QString> m_seenSpotKeys;   // track unique spots (no numeric spotId in SOTA API)
    std::atomic<bool> m_polling{false};

    static constexpr const char* ApiUrl = "https://api2.sota.org.uk/api/spots/40/all";
};

} // namespace AetherSDR
