#include "LogManager.h"
#include "AppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace AetherSDR {

// Define all logging categories (disabled by default)
Q_LOGGING_CATEGORY(lcDiscovery,  "aether.discovery",  QtDebugMsg)
Q_LOGGING_CATEGORY(lcConnection, "aether.connection",  QtDebugMsg)
Q_LOGGING_CATEGORY(lcProtocol,   "aether.protocol",    QtDebugMsg)
Q_LOGGING_CATEGORY(lcAudio,      "aether.audio",       QtWarningMsg)
Q_LOGGING_CATEGORY(lcVita49,     "aether.vita49",      QtWarningMsg)
Q_LOGGING_CATEGORY(lcDsp,        "aether.dsp",         QtWarningMsg)
Q_LOGGING_CATEGORY(lcRade,       "aether.rade",        QtWarningMsg)
Q_LOGGING_CATEGORY(lcSmartLink,  "aether.smartlink",   QtWarningMsg)
Q_LOGGING_CATEGORY(lcCat,        "aether.cat",         QtWarningMsg)
Q_LOGGING_CATEGORY(lcDax,        "aether.dax",         QtWarningMsg)
Q_LOGGING_CATEGORY(lcMeters,     "aether.meters",      QtWarningMsg)
Q_LOGGING_CATEGORY(lcTransmit,   "aether.transmit",    QtWarningMsg)
Q_LOGGING_CATEGORY(lcFirmware,   "aether.firmware",    QtWarningMsg)
Q_LOGGING_CATEGORY(lcTuner,      "aether.tuner",       QtWarningMsg)
Q_LOGGING_CATEGORY(lcGui,        "aether.gui",         QtWarningMsg)
Q_LOGGING_CATEGORY(lcDxCluster,  "aether.dxcluster",   QtWarningMsg)
Q_LOGGING_CATEGORY(lcRbn,        "aether.rbn",         QtWarningMsg)
Q_LOGGING_CATEGORY(lcPerf,       "aether.perf",        QtWarningMsg)

LogManager::LogManager()
{
    // Register categories with human-readable labels and descriptions
    m_categories = {
        {"aether.discovery",  "Discovery",    "UDP radio discovery broadcasts"},
        {"aether.connection", "Commands",     "TCP commands sent (TX) and responses received (RX)"},
        {"aether.protocol",   "Status",       "Parsed status messages (slice, pan, transmit, meter)"},
        {"aether.audio",      "Audio",        "RX/TX audio, device negotiation, volume"},
        {"aether.vita49",     "VITA-49",      "UDP packet routing: FFT, waterfall, meters, DAX"},
        {"aether.dsp",        "DSP",          "NR2, RN2, CW decoder processing"},
        {"aether.rade",       "RADE",         "FreeDV Radio Autoencoder digital voice"},
        {"aether.smartlink",  "SmartLink",    "Auth0 login, TLS tunnel, WAN streaming"},
        {"aether.cat",        "CAT/rigctld",  "rigctld TCP servers, PTY virtual serial ports"},
        {"aether.dax",        "DAX",          "Virtual audio bridge (PipeWire/CoreAudio)"},
        {"aether.meters",     "Meters",       "Meter definitions and value conversion"},
        {"aether.transmit",   "Transmit",     "TX state, ATU, profiles, power control"},
        {"aether.firmware",   "Firmware",     "Firmware download, staging, upload"},
        {"aether.tuner",      "Tuner/AGM",    "TGXL tuner, Antenna Genius state"},
        {"aether.gui",        "GUI",          "Window, applets, dialogs"},
        {"aether.dxcluster",  "DX Cluster",   "DX cluster telnet connection and spot parsing"},
        {"aether.rbn",        "RBN",          "Reverse Beacon Network connection and spots"},
        {"aether.perf",       "Performance",  "Render timing and CPU profiling data"},
    };

    // QLoggingCategory objects are defined above via Q_LOGGING_CATEGORY macros.
    // setFilterRules() controls them by name string — no need to hold pointers.
}

LogManager& LogManager::instance()
{
    static LogManager mgr;
    return mgr;
}

bool LogManager::isEnabled(const QString& id) const
{
    for (const auto& c : m_categories)
        if (c.id == id) return c.enabled;
    return false;
}

void LogManager::setEnabled(const QString& id, bool on)
{
    for (auto& c : m_categories) {
        if (c.id == id) {
            if (c.enabled == on) return;
            c.enabled = on;
            applyFilterRules();
            saveSettings();
            emit categoryChanged(id, on);
            return;
        }
    }
}

void LogManager::setAllEnabled(bool on)
{
    for (auto& c : m_categories)
        c.enabled = on;
    applyFilterRules();
    saveSettings();
}

void LogManager::applyFilterRules()
{
    // Build a filter rule string for QLoggingCategory
    // Default: all aether.* debug messages off, then enable selected ones
    QStringList rules;
    rules << "aether.*.debug=false";
    for (const auto& c : m_categories) {
        if (c.enabled)
            rules << QString("%1.debug=true").arg(c.id);
    }
    QLoggingCategory::setFilterRules(rules.join('\n'));
}

QString LogManager::logFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/AetherSDR/aethersdr.log";
}

qint64 LogManager::logFileSize() const
{
    QFileInfo fi(logFilePath());
    return fi.exists() ? fi.size() : 0;
}

void LogManager::clearLog()
{
    QFile f(logFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.close();
}

// AppSettings XML keys cannot contain dots — replace '.' with '_'
static QString settingsKey(const QString& id)
{
    QString key = "LogCategory_" + id;
    key.replace('.', '_');
    return key;
}

void LogManager::saveSettings()
{
    auto& s = AppSettings::instance();
    for (const auto& c : m_categories)
        s.setValue(settingsKey(c.id), c.enabled ? "True" : "False");
    s.save();
}

void LogManager::loadSettings()
{
    auto& s = AppSettings::instance();
    // Default Discovery, Commands, and Status to on
    static const QStringList defaultOn = {
        "aether.discovery", "aether.connection", "aether.protocol"
    };
    for (auto& c : m_categories) {
        QString def = defaultOn.contains(c.id) ? "True" : "False";
        c.enabled = s.value(settingsKey(c.id), def).toString() == "True";
    }
    applyFilterRules();
}

} // namespace AetherSDR
