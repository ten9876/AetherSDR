#include "SupportBundle.h"
#include "AppSettings.h"
#include "LogManager.h"
#include "models/RadioModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>

namespace AetherSDR {

SupportBundle::SystemInfo SupportBundle::collectSystemInfo()
{
    return {
        QCoreApplication::applicationVersion(),
        QString::fromLatin1(qVersion()),
        QSysInfo::prettyProductName(),
        QSysInfo::kernelVersion(),
        QSysInfo::currentCpuArchitecture(),
        QString::fromLatin1(__DATE__)
    };
}

SupportBundle::RadioInfo SupportBundle::collectRadioInfo(const RadioModel* model)
{
    RadioInfo info;
    if (!model || !model->isConnected()) {
        info.connected = false;
        return info;
    }
    info.connected = true;
    info.model     = model->model();
    info.serial    = model->serial();
    info.firmware  = model->version();
    info.callsign  = model->callsign();
    info.ip        = model->ip();
    return info;
}

QString SupportBundle::createBundle(const RadioInfo& radio)
{
    auto& logMgr = LogManager::instance();

    // Create temp directory for bundle contents
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return {};
    tmpDir.setAutoRemove(false);
    const QString tmp = tmpDir.path();

    // 1. Copy log file
    QFile::copy(logMgr.logFilePath(), tmp + "/aethersdr.log");

    // 2. System info JSON
    {
        auto sys = collectSystemInfo();
        QJsonObject obj;
        obj["aetherVersion"] = sys.aetherVersion;
        obj["qtVersion"]     = sys.qtVersion;
        obj["os"]            = sys.osName;
        obj["kernel"]        = sys.kernelVersion;
        obj["cpu"]           = sys.cpuArch;
        obj["buildDate"]     = sys.buildDate;
        QFile f(tmp + "/system-info.json");
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }

    // 3. Radio info JSON
    {
        QJsonObject obj;
        obj["connected"] = radio.connected;
        if (radio.connected) {
            obj["model"]    = radio.model;
            obj["serial"]   = radio.serial;
            obj["firmware"] = radio.firmware;
            obj["callsign"] = radio.callsign;
            obj["ip"]       = radio.ip;
        }
        QFile f(tmp + "/radio-info.json");
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }

    // 4. Sanitized settings
    {
        auto& settings = AppSettings::instance();
        QFile src(settings.filePath());
        if (src.open(QIODevice::ReadOnly)) {
            QString xml = QString::fromUtf8(src.readAll());
            src.close();

            // Strip lines containing sensitive keys
            QStringList lines = xml.split('\n');
            QStringList sanitized;
            for (const auto& line : lines) {
                QString lower = line.toLower();
                if (lower.contains("token") || lower.contains("password") ||
                    lower.contains("secret") || lower.contains("auth0") ||
                    lower.contains("refresh")) {
                    sanitized << "  <!-- [REDACTED] -->";
                } else {
                    sanitized << line;
                }
            }
            QFile dst(tmp + "/settings.xml");
            if (dst.open(QIODevice::WriteOnly))
                dst.write(sanitized.join('\n').toUtf8());
        }
    }

    // 5. Enabled logging categories
    {
        QFile f(tmp + "/enabled-categories.txt");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            for (const auto& cat : logMgr.categories()) {
                f.write(QString("%1: %2\n")
                    .arg(cat.id, cat.enabled ? "ENABLED" : "disabled")
                    .toUtf8());
            }
        }
    }

    // 6. Create archive
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString configDir = QFileInfo(logMgr.logFilePath()).absolutePath();

#ifdef _WIN32
    const QString archivePath = configDir + "/support-bundle-" + timestamp + ".zip";
    QProcess proc;
    proc.start("powershell", {"-Command",
        QString("Compress-Archive -Path '%1/*' -DestinationPath '%2'")
            .arg(tmp, archivePath)});
#else
    const QString archivePath = configDir + "/support-bundle-" + timestamp + ".tar.gz";
    QProcess proc;
    proc.start("tar", {"czf", archivePath, "-C", tmp, "."});
#endif

    proc.waitForFinished(10000);
    tmpDir.setAutoRemove(true);  // clean up temp files

    if (proc.exitCode() != 0 || !QFile::exists(archivePath))
        return {};

    return archivePath;
}

void SupportBundle::openEmailClient(const QString& bundlePath,
                                    const SystemInfo& sys,
                                    const RadioInfo& radio)
{
    QString subject = QString("AetherSDR Support - %1 v%2")
        .arg(radio.connected ? radio.model : "No Radio", sys.aetherVersion);

    QString body;
    body += "AetherSDR Support Bundle\n\n";
    body += QString("App: AetherSDR v%1\n").arg(sys.aetherVersion);
    body += QString("Qt: %1\n").arg(sys.qtVersion);
    body += QString("OS: %1 (kernel %2)\n").arg(sys.osName, sys.kernelVersion);
    body += QString("CPU: %1\n").arg(sys.cpuArch);
    body += QString("Build: %1\n").arg(sys.buildDate);

    if (radio.connected) {
        body += QString("Radio: %1 (serial %2, fw %3)\n")
            .arg(radio.model, radio.serial, radio.firmware);
        body += QString("Callsign: %1\n").arg(radio.callsign);
    } else {
        body += "Radio: not connected\n";
    }

    body += QString("\nPlease attach the support bundle saved at:\n  %1\n").arg(bundlePath);
    body += "\nDescribe the issue below:\n---\n\n";

    QUrl url("mailto:support@aethersdr.com");
    QUrlQuery query;
    query.addQueryItem("subject", subject);
    query.addQueryItem("body", body);
    url.setQuery(query);

    QDesktopServices::openUrl(url);
}

} // namespace AetherSDR
