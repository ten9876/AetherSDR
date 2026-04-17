#include "SotaClient.h"
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

SotaClient::SotaClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &SotaClient::onPollTimer);
}

SotaClient::~SotaClient()
{
    stopPolling();
    m_logFile.close();
}

QString SotaClient::logFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/AetherSDR/sota.log";
}

void SotaClient::startPolling(int intervalSec)
{
    if (m_polling) return;

    qCDebug(lcDxCluster) << "SotaClient: starting polling every" << intervalSec << "sec";

    // Open log file (truncate on each start)
    m_logFile.close();
    m_logFile.setFileName(logFilePath());
    QDir().mkpath(QFileInfo(m_logFile).absolutePath());
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_logFile.write(QString("--- SOTA polling started at %1 (every %2s) ---\n")
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

void SotaClient::stopPolling()
{
    if (!m_polling) return;
    m_pollTimer->stop();
    m_polling = false;
    emit stopped();
}

void SotaClient::onPollTimer()
{
    QNetworkRequest req{QUrl{ApiUrl}};
    req.setHeader(QNetworkRequest::UserAgentHeader, "AetherSDR");
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            qCWarning(lcDxCluster) << "SotaClient: poll failed:" << err;
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
            QString activator = obj.value("activatorCallsign").toString();
            QString freqStr   = obj.value("frequency").toString();
            QString timeStr   = obj.value("timeStamp").toString();
            QString spotKey   = activator + "|" + freqStr + "|" + timeStr;

            if (m_seenSpotKeys.contains(spotKey))
                continue;
            m_seenSpotKeys.insert(spotKey);
            newCount++;

            DxSpot spot;
            spot.dxCall      = activator;
            spot.spotterCall = obj.value("callsign").toString();  // spotter
            spot.source      = "SOTA";

            // Frequency in MHz (SOTA API provides MHz as string)
            spot.freqMhz = freqStr.toDouble();

            // Default lifetime for SOTA spots
            spot.lifetimeSec = 900;

            // Apply SOTA spot color (#RRGGBB → #FFRRGGBB for radio)
            QString sotaColor = AppSettings::instance().value("SotaSpotColor", "#FF8C00").toString();
            if (sotaColor.length() == 7)
                sotaColor = "#FF" + sotaColor.mid(1);
            spot.color = sotaColor;

            // Build comment: summit reference + summit name + mode
            QString ref      = obj.value("associationCode").toString()
                             + "/" + obj.value("summitCode").toString();
            QString summit   = obj.value("summitDetails").toString();
            QString mode     = obj.value("mode").toString();
            spot.comment = ref;
            if (!summit.isEmpty())
                spot.comment += " " + summit;
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
            QString logLine = QString("%1  %2  %3 MHz  %4  %5")
                .arg(spot.utcTime.toString("HH:mm"),
                     spot.dxCall,
                     QString::number(spot.freqMhz, 'f', 4),
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
