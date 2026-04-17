#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QFile>
#include <QSet>
#include <atomic>
#include "DxClusterClient.h"  // for DxSpot

namespace AetherSDR {

// WWFF (World Wide Flora and Fauna) spot client — polls the WWFF spot feed
// for recent activations. Emits spotReceived() for each new spot.
class WwffClient : public QObject {
    Q_OBJECT

public:
    explicit WwffClient(QObject* parent = nullptr);
    ~WwffClient() override;

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
    QSet<QString> m_seenSpotKeys;   // track unique spots
    std::atomic<bool> m_polling{false};

    static constexpr const char* ApiUrl = "https://www.cqgma.org/spotsmart/spots.php?type=wwff&max=40";
};

} // namespace AetherSDR
