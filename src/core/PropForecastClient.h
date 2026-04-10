#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>

namespace AetherSDR {

struct PropForecast {
    int kIndex{-1};  // Planetary K-index 0-9; -1 = not yet fetched
    int aIndex{-1};  // Planetary A-index; -1 = not yet fetched
    int sfi{-1};     // Solar Flux Index (SFU); -1 = not yet fetched
};

// Fetches HF propagation conditions from https://www.hamqsl.com/solarxml.php
// once per hour. The timer is only armed while enabled. Uses QNetworkAccessManager
// (async) so the download never blocks radio operations.
//
// Hardening:
//   - Persists last K/A/SFI + timestamp to AppSettings so the overlay is
//     available immediately on restart without waiting for a network fetch.
//   - Skips the network fetch if cached data is less than one hour old.
//   - Guards against overlapping in-flight requests.
class PropForecastClient : public QObject {
    Q_OBJECT

public:
    explicit PropForecastClient(QObject* parent = nullptr);

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    PropForecast lastForecast() const { return m_last; }

signals:
    void forecastUpdated(const PropForecast& forecast);
    void fetchError(const QString& error);

private slots:
    void onTimer();

private:
    void fetchIfStale();          // only fetches when cache is older than kIntervalMs
    void fetch();                 // unconditional network request

    void loadCache();             // restore from AppSettings
    void saveCache();             // persist to AppSettings

    QNetworkAccessManager m_nam;
    QTimer               m_timer;
    PropForecast         m_last;
    bool                 m_enabled{false};
    bool                 m_fetchInFlight{false};
    qint64               m_lastFetchMs{0};  // QDateTime::currentMSecsSinceEpoch of last success

    static constexpr const char* kUrl = "https://www.hamqsl.com/solarxml.php";
    static constexpr int kIntervalMs  = 60 * 60 * 1000;  // 1 hour
};

} // namespace AetherSDR
