#include "FloatingAppletWindow.h"
#include "core/AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

namespace AetherSDR {

FloatingAppletWindow::FloatingAppletWindow(const QString& appletId,
                                           const QString& title,
                                           QWidget* applet,
                                           QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , m_appletId(appletId)
{
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose, false);  // AppletPanel owns lifecycle
    setMinimumWidth(260);

    // QWidget selector cascades to children, providing the app's dark background
    // for applets that relied on AppletPanel's parent stylesheet.
    setStyleSheet(
        "FloatingAppletWindow { background: #0f0f1a; "
        "border: 1px solid #203040; }"
        "QWidget { background: #0a0a18; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Custom title bar ──────────────────────────────────────────────────────
    auto* titleBar = new QWidget;
    titleBar->setFixedHeight(22);
    titleBar->setStyleSheet(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #3a4a5a, stop:0.5 #2a3a4a, stop:1 #1a2a38); "
        "border-bottom: 1px solid #0a1a28; }");

    auto* tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(6, 0, 4, 0);
    tbLayout->setSpacing(4);

    auto* titleLabel = new QLabel(title, titleBar);
    titleLabel->setStyleSheet(
        "QLabel { background: transparent; color: #8aa8c0; "
        "font-size: 10px; font-weight: bold; }");
    tbLayout->addWidget(titleLabel);
    tbLayout->addStretch();

    auto* dockBtn = new QPushButton("\u21a9 Dock", titleBar);
    dockBtn->setFixedHeight(16);
    dockBtn->setToolTip("Return applet to the panel");
    dockBtn->setStyleSheet(
        "QPushButton { background: #1a2a3a; color: #8aa8c0; "
        "border: 1px solid #304050; border-radius: 3px; "
        "font-size: 10px; padding: 0 5px; }"
        "QPushButton:hover { background: #243848; color: #c8d8e8; }");
    connect(dockBtn, &QPushButton::clicked, this, [this]() {
        saveGeometry();
        emit dockRequested(m_appletId);
    });
    tbLayout->addWidget(dockBtn);

    root->addWidget(titleBar);

    // ── Applet content area ───────────────────────────────────────────────────
    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_contentLayout->addWidget(applet);
    root->addLayout(m_contentLayout);

    adjustSize();

    // Debounce geometry saves — fire 400 ms after the last resize/move event.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &FloatingAppletWindow::saveGeometry);

    // On app shutdown, prevent closeEvent from emitting dockRequested.
    // Geometry is saved in hideEvent (fires while the window still has valid
    // coordinates), not here — aboutToQuit fires after tool windows are already
    // hidden on Windows and pos() may no longer be reliable.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        m_appShuttingDown = true;
    });
}

// Applet IDs like "P/CW" contain characters that are invalid in XML element
// names. Sanitize by replacing '/' with '_' before forming settings keys.
static QString settingsPrefix(const QString& appletId)
{
    return QStringLiteral("FloatingApplet_%1").arg(QString(appletId).replace('/', '_'));
}

void FloatingAppletWindow::saveGeometry()
{
    const QString prefix = settingsPrefix(m_appletId);
    auto& s = AppSettings::instance();

    QScreen* screen = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!screen) { screen = QGuiApplication::primaryScreen(); }
    if (!screen) { return; }

    // pos() is the frame position (what move() sets). geometry() is the client
    // area — it's offset from pos() by the decoration height. Saving geometry()
    // and restoring with move() accumulates the decoration offset on every
    // hide/show cycle, causing visible drift. Use pos() for x/y, geometry()
    // for w/h (client size, consistent with resize()).
    const QPoint framePos  = pos();
    const QSize  clientSz  = geometry().size();
    const QRect  screenGeo = screen->geometry();

    s.setValue(prefix + "_Screen", screen->name());
    s.setValue(prefix + "_X",      QString::number(framePos.x() - screenGeo.x()));
    s.setValue(prefix + "_Y",      QString::number(framePos.y() - screenGeo.y()));
    s.setValue(prefix + "_W",      QString::number(clientSz.width()));
    s.setValue(prefix + "_H",      QString::number(clientSz.height()));
    s.save();
}

void FloatingAppletWindow::restoreGeometry()
{
    const QString prefix = settingsPrefix(m_appletId);
    const auto& s = AppSettings::instance();

    const int w = s.value(prefix + "_W", "0").toInt();
    const int h = s.value(prefix + "_H", "0").toInt();
    if (w <= 0 || h <= 0) { return; }  // no saved geometry yet

    // Find the saved screen by name; fall back to primary if disconnected.
    const QString screenName = s.value(prefix + "_Screen").toString();
    QScreen* screen = nullptr;
    for (QScreen* sc : QGuiApplication::screens()) {
        if (sc->name() == screenName) { screen = sc; break; }
    }
    if (!screen) { screen = QGuiApplication::primaryScreen(); }
    if (!screen) { return; }

    // m_restoringGeometry is owned by the caller (showAndRestore). Do not
    // touch the guard here — showAndRestore sets it before show() and clears
    // it 650 ms later, well after any WM ConfigureNotify can arrive.

    resize(w, h);

    // Wayland compositors own window placement — attempting move() is a no-op
    // on most compositors. Skip it so we don't fight the compositor.
    // On X11 (including XWayland / WSL2 with QT_QPA_PLATFORM=xcb) and on
    // Windows / macOS, move() works correctly.
    if (QGuiApplication::platformName() == QLatin1String("wayland")) { return; }

    const int     savedX = s.value(prefix + "_X", "0").toInt();
    const int     savedY = s.value(prefix + "_Y", "0").toInt();
    const QRect   avail  = screen->availableGeometry();
    const QRect   sGeo   = screen->geometry();

    // Clamp so the window title bar is always reachable even if the screen
    // resolution has changed or the window was partially off-screen.
    const int x = qBound(avail.left(),
                         sGeo.x() + savedX,
                         avail.right()  - qMin(w, avail.width()));
    const int y = qBound(avail.top(),
                         sGeo.y() + savedY,
                         avail.bottom() - qMin(h, avail.height()));

    move(x, y);
}

void FloatingAppletWindow::hideAndSave()
{
    m_saveTimer->stop();
    saveGeometry();
    hide();
}

void FloatingAppletWindow::showAndRestore()
{
    // Guard the entire show+restore+WM-settle sequence so no moveEvent or
    // resizeEvent can start the debounce timer and overwrite the saved geometry.
    //
    // Timeline:
    //   t=  0 ms  guard=true, show() — WM centers window, fires moveEvent (suppressed)
    //   t=200 ms  restoreGeometry() — resize()+move() fire events (suppressed)
    //   t=~250 ms WM sends ConfigureNotify for our move() (suppressed)
    //   t=650 ms  guard=false — any events after this are genuine user moves
    //
    // 650 ms = 200 ms (restore delay) + 300 ms (WM settle) + 150 ms safety margin.
    m_restoringGeometry = true;
    show();
    raise();
    QTimer::singleShot(200, this, [this]() { restoreGeometry(); });
    QTimer::singleShot(650, this, [this]() { m_restoringGeometry = false; });
}

void FloatingAppletWindow::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoringGeometry) { m_saveTimer->start(); }
}

void FloatingAppletWindow::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoringGeometry) { m_saveTimer->start(); }
}

void FloatingAppletWindow::hideEvent(QHideEvent* ev)
{
    QWidget::hideEvent(ev);
    // Belt-and-suspenders save for paths that call hide() directly (e.g. on
    // Linux when parent visibility cascades via Qt). Qt does NOT guarantee
    // hideEvent fires when a parent is hidden — so this cannot be the primary
    // save path. hideAndSave() and closeEvent() both call saveGeometry()
    // explicitly before reaching this point.
    if (!m_restoringGeometry) {
        m_saveTimer->stop();
        saveGeometry();
    }
}

void FloatingAppletWindow::closeEvent(QCloseEvent* ev)
{
    // During app shutdown (aboutToQuit already fired) or when the parent panel
    // is no longer visible, just accept. Geometry was already saved in hideEvent.
    // Never emit dockRequested here — that writes IsFloating=False and causes
    // the applet to reappear docked on next launch.
    if (auto* p = qobject_cast<QWidget*>(parent()); m_appShuttingDown || !p || !p->isVisible()) {
        m_saveTimer->stop();
        saveGeometry();
        ev->accept();
        return;
    }
    // User closed the floating window manually — re-dock instead of discarding.
    saveGeometry();
    emit dockRequested(m_appletId);
    ev->ignore();
}

} // namespace AetherSDR
