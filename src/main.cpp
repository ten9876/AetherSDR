#include "gui/MainWindow.h"
#include "gui/SliceColorManager.h"
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
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QRegularExpression>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#ifdef __linux__
#include <dlfcn.h>

// Minimal forward declarations matching the Xlib error-handler ABI.
// Field order must match X11/Xlib.h's XErrorEvent exactly — otherwise
// ev->error_code etc. read the wrong bytes.
struct AetherX11Display;
struct AetherX11ErrorEvent {
    int               type;
    AetherX11Display* display;      // Display the event was read from
    unsigned long     resourceid;   // XID of failed resource
    unsigned long     serial;       // serial of failed request
    unsigned char     error_code;   // BadAccess == 10
    unsigned char     request_code; // major opcode
    unsigned char     minor_code;
};

// Tolerant X11 error handler — logs errors instead of aborting.
// On systems with non-free FFmpeg (e.g. openSUSE Packman), Qt Multimedia's
// FFmpeg backend may trigger X11 hardware-acceleration probing that causes a
// BadAccess error, even under native Wayland.  Xlib's default handler calls
// exit() on any error; ours logs and continues.  AetherSDR only uses Qt
// Multimedia for audio device enumeration and PCM I/O, so X11 errors from
// video hwaccel probing are harmless.  (#1839)
static int aetherTolerantX11ErrorHandler(AetherX11Display*, AetherX11ErrorEvent* ev)
{
    qWarning("Non-fatal X11 error suppressed (error_code=%d, request=%d) — "
             "see issue #1839",
             ev ? ev->error_code : -1,
             ev ? ev->request_code : -1);
    return 0;
}
#endif  // __linux__

static QFile* s_logFile = nullptr;

static QMutex* logMutex()
{
    // qInstallMessageHandler callbacks can arrive concurrently from any Qt
    // thread; leak this intentionally so shutdown logging cannot outlive it.
    static QMutex* mutex = new QMutex;
    return mutex;
}

// Redact PII from log messages before writing to file.
// Patterns: IP addresses, radio serial numbers, Auth0 tokens, MAC addresses.
//
// Regex objects are heap-allocated (intentional leak) so they survive static
// destruction order during abnormal teardown.  Without this, crash cleanup
// invokes the message handler from dying threads after the function-local
// statics are destroyed, producing "invalid QRegularExpression" errors (#1233).
static QString redactPii(const QString& msg)
{
    QString out = msg;

    // IPv4 addresses: 192.168.50.121 → *.*.*. 121 (keep last octet)
    // Negative lookbehind/lookahead skip 4-component version strings:
    //   "0.9.2.1"            — quoted (Qt qDebug output)
    //   v0.9.2.1             — v-prefixed
    //   software_ver=4.2.18.41174  — protocol field with trailing build number
    //   firmware_ver=…       — same shape
    // The trailing (?![\d"]) handles the build-number case where the 4th
    // captured "octet" is part of a longer number (e.g. 41174 → matches as
    // 411 with 74 dangling, which is the bug we're fixing).
    static const QRegularExpression* ipRe = new QRegularExpression(
        R"((?<!ver=)(?<![v"])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d"]))");
    out.replace(*ipRe, QStringLiteral("*.*.*. \\4"));

    // Radio serial: 4424-1213-8600-7836 → ****-****-****-7836
    static const QRegularExpression* serialRe = new QRegularExpression(
        R"(\d{4}-\d{4}-\d{4}-(\d{4}))");
    out.replace(*serialRe, QStringLiteral("****-****-****-\\1"));

    // Auth0 tokens (long base64 strings after id_token or token)
    static const QRegularExpression* tokenRe = new QRegularExpression(
        R"((id_token[= :]|token[= :])\s*([A-Za-z0-9_\-\.]{20})[A-Za-z0-9_\-\.]+)");
    out.replace(*tokenRe, QStringLiteral("\\1 \\2...REDACTED"));

    // MAC addresses: 00-1C-2D-05-37-2A → **-**-**-**-**-2A
    static const QRegularExpression* macRe = new QRegularExpression(
        R"(([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2}))");
    out.replace(*macRe, QStringLiteral("**-**-**-**-**-\\6"));

    return out;
}

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    static const char* labels[] = {"DBG", "WRN", "CRT", "FTL", "INF"};
    const char* label = (type <= QtInfoMsg) ? labels[type] : "???";
    const QString category = (ctx.category && *ctx.category)
        ? QString::fromUtf8(ctx.category)
        : QStringLiteral("default");

    const QString safeMsg = redactPii(msg);
    const QString line = QString("[%1] %2 %3: %4\n")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), label, category, safeMsg);
    const QByteArray lineBytes = line.toUtf8();

    // File write is the only thing that needs the mutex — QFile is not
    // thread-safe (PR #2284 added this serialization to fix concurrent-write
    // corruption).
    {
        QMutexLocker locker(logMutex());
        if (s_logFile && s_logFile->isOpen()) {
            s_logFile->write(lineBytes);
            s_logFile->flush();
        }
    }

    // Skip stderr when it's a pipe to a non-draining parent (Stream Deck
    // "Run Command", systemd user services, GUI launchers).  Once the
    // ~64 KB pipe buffer fills, a blocking write would lock up the app.
    // The fprintf is outside the log mutex so even an unexpected stall
    // here can't block other threads' log calls.
    static const bool stderrIsTty = isatty(fileno(stderr));
    if (stderrIsTty)
        fprintf(stderr, "%s", lineBytes.constData());
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
    // Without this, Qt may fall back to XWayland (xcb platform) where GLX
    // context switching between the main window and child dialogs triggers
    // a BadAccess crash (X_GLXMakeCurrent) on some compositors.
    // Only set when QT_QPA_PLATFORM isn't already configured by the user.
    // Skip for AppImage: the bundled Qt Wayland plugin may not match the
    // host compositor's protocol version, causing an abort on init (#1389).
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")
            && !qEnvironmentVariableIsSet("APPIMAGE")) {
        const QByteArray session = qgetenv("XDG_SESSION_TYPE");
        if (session == "wayland" && qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "wayland");
        }
    }

#ifdef __linux__
    // Install a tolerant X11 error handler before QApplication and before any
    // library (FFmpeg, VA-API, VDPAU) can open an X11 connection.  Xlib's
    // default handler calls exit() on protocol errors like BadAccess, which
    // makes stray X11 probing from non-free FFmpeg builds fatal.  On the
    // Wayland platform Qt does not install its own X11 handler (unlike xcb),
    // so without this the default handler remains active.  (#1839)
    //
    // dlopen avoids a build-time dependency on libX11-dev.
    {
        using XErrHandler = int (*)(AetherX11Display*, AetherX11ErrorEvent*);
        using XSetErrHandlerFn = XErrHandler (*)(XErrHandler);
        void* x11 = dlopen("libX11.so.6", RTLD_LAZY);
        if (x11) {
            auto fn = reinterpret_cast<XSetErrHandlerFn>(
                dlsym(x11, "XSetErrorHandler"));
            if (fn)
                fn(aetherTolerantX11ErrorHandler);
            // Do not dlclose — libX11 must stay loaded for the handler to
            // remain registered for connections opened later by FFmpeg.
        }
    }
#endif

    // Apply saved UI scale factor BEFORE QApplication is created.
    // QT_SCALE_FACTOR must be set before Qt initializes the display.
    // We read the settings file directly (can't use AppSettings or
    // QStandardPaths before QApplication exists).
    {
#ifdef Q_OS_MAC
        QString settingsPath = QDir::homePath() + "/Library/Preferences/AetherSDR/AetherSDR.settings";
#elif defined(Q_OS_WIN)
        // On Windows, QStandardPaths::ConfigLocation maps to %APPDATA% — match that here
        // since QStandardPaths isn't available before QApplication.
        QString settingsPath = QDir::fromNativeSeparators(
                                   QString::fromLocal8Bit(qgetenv("APPDATA")))
                               + "/AetherSDR/AetherSDR.settings";
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
                    // Always set — even at 100% — so a restarted child process
                    // overrides any QT_SCALE_FACTOR it inherited from its parent.
                    if (pct > 0)
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
        AetherSDR::LogManager::instance().setActiveLogFilePath(logPath);
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

    // Load slice color overrides (must be after AppSettings::load)
    AetherSDR::SliceColorManager::instance().load();

    // Load per-module logging toggles (must be after AppSettings::load)
    AetherSDR::LogManager::instance().loadSettings();

    qDebug() << "Starting AetherSDR" << app.applicationVersion();

    AetherSDR::MainWindow window;
    window.show();

    return app.exec();
}
