#include "WwffClient.h"
#include "LogManager.h"
#include "AppSettings.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace AetherSDR {

WwffClient::WwffClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &WwffClient::onPollTimer);
}

WwffClient::~WwffClient()
{
    stopPolling();
    m_logFile.close();
}

QString WwffClient::logFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/AetherSDR/wwff.log";
}

void WwffClient::startPolling(int intervalSec)
{
    if (m_polling) return;

    qCDebug(lcDxCluster) << "WwffClient: starting polling every" << intervalSec << "sec";

    // Open log file (truncate on each start)
    m_logFile.close();
    m_logFile.setFileName(logFilePath());
    QDir().mkpath(QFileInfo(m_logFile).absolutePath());
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_logFile.write(QString("--- WWFF polling started at %1 (every %2s) ---\n")
            .arg(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm:ss UTC"))
            .arg(intervalSec).toUtf8());
        m_logFile.flush();
    }

    m_seenSpotKeys.clear();
    m_polling = true;
    m_pollTimer->start(intervalSec * 1000);
    onPollTimer();  // immediate first poll
    emit started();
}

void WwffClient::stopPolling()
{
    if (!m_polling) return;
    m_pollTimer->stop();
    m_polling = false;
    emit stopped();
}

void WwffClient::onPollTimer()
{
    QNetworkRequest req{QUrl{ApiUrl}};
    req.setHeader(QNetworkRequest::UserAgentHeader, "AetherSDR");
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            qCWarning(lcDxCluster) << "WwffClient: poll failed:" << err;
            emit pollError(err);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) {
            emit pollError("Invalid JSON response");
            return;
        }

        QJsonArray arr = doc.array();
        int total = arr.size();
        int newCount = 0;

        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();

            // Build a unique key from callsign + frequency + time
            QString activator = obj.value("activator").toString();
            QString freqStr   = obj.value("frequency").toString();
            QString timeStr   = obj.value("time").toString();
            QString spotKey   = activator + "|" + freqStr + "|" + timeStr;

            if (m_seenSpotKeys.contains(spotKey))
                continue;
            m_seenSpotKeys.insert(spotKey);
            newCount++;

            DxSpot spot;
            spot.dxCall      = activator;
            spot.spotterCall = obj.value("spotter").toString();
            spot.source      = "WWFF";

            // Frequency in kHz → MHz
            spot.freqMhz = freqStr.toDouble() / 1000.0;

            // Default lifetime for WWFF spots
            spot.lifetimeSec = 900;

            // Apply WWFF spot color (#RRGGBB → #FFRRGGBB for radio)
            QString wwffColor = AppSettings::instance().value("WwffSpotColor", "#00FF7F").toString();
            if (wwffColor.length() == 7)
                wwffColor = "#FF" + wwffColor.mid(1);
            spot.color = wwffColor;

            // Build comment: WWFF reference + park name + mode
            QString ref  = obj.value("reference").toString();
            QString park = obj.value("name").toString();
            QString mode = obj.value("mode").toString();
            spot.comment = ref;
            if (!park.isEmpty())
                spot.comment += " " + park;
            if (!mode.isEmpty())
                spot.comment += " " + mode;

            // Parse spot time
            QDateTime dt = QDateTime::fromString(timeStr, Qt::ISODate);
            if (dt.isValid())
                spot.utcTime = dt.toUTC().time();
            else
                spot.utcTime = QDateTime::currentDateTimeUtc().time();

            if (spot.freqMhz <= 0.0 || spot.dxCall.isEmpty())
                continue;

            // Log
            QString logLine = QString("%1  %2  %3 kHz  %4  %5")
                .arg(spot.utcTime.toString("HH:mm"),
                     spot.dxCall,
                     QString::number(spot.freqMhz * 1000.0, 'f', 1),
                     ref, mode);
            if (m_logFile.isOpen()) {
                m_logFile.write((logLine + "\n").toUtf8());
                m_logFile.flush();
            }
            emit rawLineReceived(logLine);
            emit spotReceived(spot);
        }

        emit pollComplete(total, newCount);
    });
}

} // namespace AetherSDR
