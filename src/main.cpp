#include "gui/MainWindow.h"
#include "core/AppSettings.h"
#include "core/LogManager.h"
#include "core/MacMicPermission.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QGlobalStatic>

static QFile* s_logFile = nullptr;

// PII redaction patterns. Stored as a Q_GLOBAL_STATIC so they are lazily
// initialised once and never destroyed — outliving function-local statics and
// surviving abnormal teardown.  The isDestroyed() guard in redactPii() is the
// safety net if the message handler fires after program-exit teardown (#1233).
struct PiiPatterns {
    const QRegularExpression ip     { R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))" };
    const QRegularExpression serial { R"(\d{4}-\d{4}-\d{4}-(\d{4}))" };
    const QRegularExpression token  { R"((id_token[= :]|token[= :])\s*([A-Za-z0-9_\-\.]{20})[A-Za-z0-9_\-\.]+)" };
    const QRegularExpression mac    { R"(([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2}))" };
};
Q_GLOBAL_STATIC(PiiPatterns, s_piiPatterns)

// Redact PII from log messages before writing to file.
// Patterns: IP addresses, radio serial numbers, Auth0 tokens, MAC addresses.
static QString redactPii(const QString& msg)
{
    if (s_piiPatterns.isDestroyed())
        return msg;
    QString out = msg;
    out.replace(s_piiPatterns->ip,     QStringLiteral("*.*.*. \\4"));
    out.replace(s_piiPatterns->serial, QStringLiteral("****-****-****-\\1"));
    out.replace(s_piiPatterns->token,  QStringLiteral("\\1 \\2...REDACTED"));
    out.replace(s_piiPatterns->mac,    QStringLiteral("**-**-**-**-**-\\6"));
    return out;
}

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx);
    static const char* labels[] = {"DBG", "WRN", "CRT", "FTL", "INF"};
    const char* label = (type <= QtInfoMsg) ? labels[type] : "???";

    const QString safeMsg = redactPii(msg);
    const QString line = QString("[%1] %2: %3\n")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), label, safeMsg);

    // Write to log file (PII-redacted)
    if (s_logFile && s_logFile->isOpen()) {
        QTextStream ts(s_logFile);
        ts << line;
        ts.flush();
    }
    // Also print to stderr (PII-redacted)
    fprintf(stderr, "%s", line.toLocal8Bit().constData());
}

int main(int argc, char* argv[])
{
    // ── Pre-QApplication environment setup ────────────────────────────────

    // AETHER_NO_GPU: runtime toggle to force software OpenGL rendering.
    // Unlike the compile-time AETHER_GPU_SPECTRUM CMake flag, this works on
    // already-built binaries. Avoids hardware GLX/EGL entirely.
    if (qEnvironmentVariableIsSet("AETHER_NO_GPU")) {
        qputenv("QT_OPENGL", "software");
    }

    // Prefer native Wayland when running under a Wayland session (#1233).
    // Without this, Qt falls back to XWayland (xcb platform) where GLX
    // context switching between the main window and child dialogs triggers
    // a BadAccess crash (X_GLXMakeCurrent) on some compositors.
    // Only set when QT_QPA_PLATFORM isn't already configured by the user.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        const QByteArray session = qgetenv("XDG_SESSION_TYPE");
        if (session == "wayland" && qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "wayland");
        }
    }

    // Apply saved UI scale factor BEFORE QApplication is created.
    // QT_SCALE_FACTOR must be set before Qt initializes the display.
    // We read the settings file directly (can't use AppSettings or
    // QStandardPaths before QApplication exists).
    {
#ifdef Q_OS_MAC
        QString settingsPath = QDir::homePath() + "/Library/Preferences/AetherSDR/AetherSDR.settings";
#else
        QString settingsPath = QDir::homePath() + "/.config/AetherSDR/AetherSDR.settings";
#endif
        QFile f(settingsPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = f.readAll();
            // AppSettings XML format: <UiScalePercent>125</UiScalePercent>
            QByteArray tag = "<UiScalePercent>";
            int idx = data.indexOf(tag);
            if (idx >= 0) {
                idx += tag.size();
                int end = data.indexOf('<', idx);
                if (end > idx) {
                    int pct = data.mid(idx, end - idx).trimmed().toInt();
                    if (pct > 0 && pct != 100)
                        qputenv("QT_SCALE_FACTOR", QByteArray::number(pct / 100.0, 'f', 2));
                }
            }
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("AetherSDR");
    app.setApplicationVersion(AETHERSDR_VERSION);
    app.setOrganizationName("AetherSDR");
    app.setDesktopFileName("AetherSDR");  // matches .desktop file for taskbar icon

    // Request microphone permission early (macOS only).
    // Shows the system prompt on first launch so it's ready before PTT.
    requestMicrophonePermission();

    // Set up file logging in ~/.config/AetherSDR/ (works inside AppImage where
    // applicationDirPath() is read-only).
    // Use GenericConfigLocation + app name to avoid the double-nested
    // ~/.config/AetherSDR/AetherSDR/ path that AppConfigLocation produces.
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                           + "/AetherSDR";
    QDir().mkpath(logDir);

    // Timestamped log file — keep last 5 sessions
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString logPath = logDir + "/aethersdr-" + timestamp + ".log";

    // Prune old log files (keep newest 4 + the one we're about to create = 5)
    {
        QDir dir(logDir);
        QStringList logs = dir.entryList({"aethersdr-*.log"}, QDir::Files, QDir::Name);
        while (logs.size() >= 5) {
            dir.remove(logs.takeFirst());
        }
    }

    s_logFile = new QFile(logPath);
    if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        // Restrict log file to owner-only (may contain session identifiers)
        s_logFile->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        qInstallMessageHandler(messageHandler);

        // Symlink aethersdr.log → latest timestamped file (for Support dialog)
        const QString symlink = logDir + "/aethersdr.log";
        QFile::remove(symlink);
        QFile::link(logPath, symlink);
    } else {
        fprintf(stderr, "Warning: could not open log file %s\n", logPath.toLocal8Bit().constData());
        delete s_logFile;
        s_logFile = nullptr;
    }

    // Use Fusion style as a clean cross-platform base
    // (our dark theme overrides colors via stylesheet)
    app.setStyle(QStyleFactory::create("Fusion"));

    // Load XML settings (auto-migrates from QSettings on first run)
    AetherSDR::AppSettings::instance().load();

    // Load per-module logging toggles (must be after AppSettings::load)
    AetherSDR::LogManager::instance().loadSettings();

    qDebug() << "Starting AetherSDR" << app.applicationVersion();

    AetherSDR::MainWindow window;
    window.show();

    return app.exec();
}
