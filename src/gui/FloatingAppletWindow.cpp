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
}

void FloatingAppletWindow::saveGeometry()
{
    const QString prefix = QStringLiteral("FloatingApplet_%1").arg(m_appletId);
    auto& s = AppSettings::instance();

    QScreen* screen = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!screen) { screen = QGuiApplication::primaryScreen(); }
    if (!screen) { return; }

    // Store screen name (stable across reboots) and screen-relative position
    // so that restoreGeometry() can find the right monitor by name rather than
    // by index, and position the window correctly even if screen order changes.
    const QRect geo       = geometry();
    const QRect screenGeo = screen->geometry();

    s.setValue(prefix + "_Screen", screen->name());
    s.setValue(prefix + "_X",      QString::number(geo.x() - screenGeo.x()));
    s.setValue(prefix + "_Y",      QString::number(geo.y() - screenGeo.y()));
    s.setValue(prefix + "_W",      QString::number(geo.width()));
    s.setValue(prefix + "_H",      QString::number(geo.height()));
    s.save();
}

void FloatingAppletWindow::restoreGeometry()
{
    const QString prefix = QStringLiteral("FloatingApplet_%1").arg(m_appletId);
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

void FloatingAppletWindow::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    m_saveTimer->start();  // restart debounce on every resize step
}

void FloatingAppletWindow::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    m_saveTimer->start();  // restart debounce on every move step
}

void FloatingAppletWindow::closeEvent(QCloseEvent* ev)
{
    // If the parent AppletPanel is hidden the main window is closing — accept
    // so the window is properly removed from Qt's visible-window count.
    if (auto* p = qobject_cast<QWidget*>(parent()); !p || !p->isVisible()) {
        m_saveTimer->stop();
        saveGeometry();  // capture final size/position before exit
        ev->accept();
        return;
    }
    // User closed the floating window manually — re-dock instead of discarding.
    saveGeometry();
    emit dockRequested(m_appletId);
    ev->ignore();  // AppletPanel::dockApplet() will hide/destroy this window
}

} // namespace AetherSDR
