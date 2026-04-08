#include "PropForecastClient.h"
#include "AppSettings.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(lcPropForecast, "aether.propforecast")

namespace AetherSDR {

PropForecastClient::PropForecastClient(QObject* parent)
    : QObject(parent)
{
    m_timer.setSingleShot(false);
    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &PropForecastClient::onTimer);
}

// ── Enable / disable ─────────────────────────────────────────────────────────

void PropForecastClient::setEnabled(bool on)
{
    if (m_enabled == on) { return; }
    m_enabled = on;

    if (on) {
        // Restore cached values so the overlay appears instantly on startup.
        loadCache();
        if (m_last.kIndex >= 0 && m_last.sfi > 0) {
            emit forecastUpdated(m_last);
        }

        // Only hit the network if the cache is stale (>= 1 hour old).
        fetchIfStale();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

// ── Timer callback ───────────────────────────────────────────────────────────

void PropForecastClient::onTimer()
{
    fetchIfStale();
}

// ── Fetch logic ──────────────────────────────────────────────────────────────

void PropForecastClient::fetchIfStale()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 age = now - m_lastFetchMs;

    if (m_lastFetchMs > 0 && age < kIntervalMs) {
        qCDebug(lcPropForecast) << "cache is" << (age / 1000) << "s old — skipping fetch";
        return;
    }

    fetch();
}

void PropForecastClient::fetch()
{
    // Guard against overlapping requests (e.g. slow network + timer fires again).
    if (m_fetchInFlight) {
        qCDebug(lcPropForecast) << "fetch already in flight — skipping";
        return;
    }
    m_fetchInFlight = true;

    QNetworkRequest req{QUrl{QString(kUrl)}};
    req.setHeader(QNetworkRequest::UserAgentHeader, "AetherSDR");
    auto* reply = m_nam.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_fetchInFlight = false;

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcPropForecast) << "fetch failed:" << reply->errorString();
            emit fetchError(reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QXmlStreamReader xml(data);

        PropForecast fc;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) { continue; }
            QString tag = xml.name().toString();
            if (tag == QLatin1String("kindex")) {
                fc.kIndex = xml.readElementText().toInt();
            } else if (tag == QLatin1String("solarflux")) {
                fc.sfi = xml.readElementText().toInt();
            }
        }

        if (xml.hasError()) {
            qCWarning(lcPropForecast) << "XML parse error:" << xml.errorString();
            emit fetchError(xml.errorString());
            return;
        }

        if (fc.kIndex >= 0 && fc.sfi > 0) {
            m_last = fc;
            m_lastFetchMs = QDateTime::currentMSecsSinceEpoch();
            saveCache();
            emit forecastUpdated(m_last);
            qCDebug(lcPropForecast) << "updated — K" << fc.kIndex << "SFI" << fc.sfi;
        } else {
            qCWarning(lcPropForecast) << "unexpected XML — kIndex:" << fc.kIndex << "sfi:" << fc.sfi;
        }
    });
}

// ── AppSettings persistence ──────────────────────────────────────────────────

void PropForecastClient::loadCache()
{
    auto& s = AppSettings::instance();
    int k   = s.value("PropForecastKIndex", "-1").toInt();
    int sfi = s.value("PropForecastSfi",    "-1").toInt();
    qint64 ts = s.value("PropForecastTimestamp", "0").toLongLong();

    if (k >= 0 && sfi > 0 && ts > 0) {
        m_last.kIndex  = k;
        m_last.sfi     = sfi;
        m_lastFetchMs  = ts;
        qCDebug(lcPropForecast) << "restored cache — K" << k << "SFI" << sfi
                                << "age" << ((QDateTime::currentMSecsSinceEpoch() - ts) / 1000) << "s";
    }
}

void PropForecastClient::saveCache()
{
    auto& s = AppSettings::instance();
    s.setValue("PropForecastKIndex",    QString::number(m_last.kIndex));
    s.setValue("PropForecastSfi",       QString::number(m_last.sfi));
    s.setValue("PropForecastTimestamp",  QString::number(m_lastFetchMs));
    s.save();
}

} // namespace AetherSDR
