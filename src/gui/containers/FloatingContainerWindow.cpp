#include "FloatingContainerWindow.h"
#include "ContainerWidget.h"
#include "core/AppSettings.h"
#include "gui/FramelessResizer.h"

#include <QByteArray>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

constexpr int kDefaultW = 300;
constexpr int kDefaultH = 240;

} // namespace

FloatingContainerWindow::FloatingContainerWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    const bool frameless =
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True";
    Qt::WindowFlags flags = Qt::Window;
    if (frameless) flags |= Qt::FramelessWindowHint;
    // Re-apply via setWindowFlags — some platform plugins ignore the
    // constructor bitmask and need an explicit call before show().
    setWindowFlags(flags);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);
    setStyleSheet("QWidget { background: #08121d; }");
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this,
            &FloatingContainerWindow::saveGeometryToKey);

    FramelessResizer::install(this);
}

FloatingContainerWindow::~FloatingContainerWindow() = default;

void FloatingContainerWindow::takeContainer(ContainerWidget* container)
{
    if (m_container == container) return;

    if (m_container) {
        m_layout->removeWidget(m_container);
        m_container->setParent(nullptr);
    }
    m_container = container;
    if (m_container) {
        m_container->setParent(this);
        m_layout->addWidget(m_container, 1);
        m_container->show();
        m_container->setDockMode(ContainerWidget::DockMode::Floating);
        setWindowTitle(m_container->title());
    }
}

ContainerWidget* FloatingContainerWindow::releaseContainer()
{
    ContainerWidget* c = m_container;
    if (c) {
        m_layout->removeWidget(c);
        c->setParent(nullptr);
        c->setDockMode(ContainerWidget::DockMode::PanelDocked);
        m_container = nullptr;
    }
    return c;
}

void FloatingContainerWindow::setGeometryKey(const QString& key)
{
    m_geometryKey = key;
}

void FloatingContainerWindow::restoreAndEnsureVisible(QWidget* anchor)
{
    m_restoring = true;
    bool restored = false;
    if (!m_geometryKey.isEmpty()) {
        const QString val = AppSettings::instance()
            .value(m_geometryKey, "").toString();
        if (!val.isEmpty()) {
            // New format: "x,y,w,h" — frame-agnostic, works across
            // frameless ↔ decorated mode changes.
            const QStringList parts = val.split(',');
            if (parts.size() == 4) {
                bool ok0, ok1, ok2, ok3;
                const int x = parts[0].toInt(&ok0);
                const int y = parts[1].toInt(&ok1);
                const int w = parts[2].toInt(&ok2);
                const int h = parts[3].toInt(&ok3);
                if (ok0 && ok1 && ok2 && ok3 && w > 0 && h > 0) {
                    setGeometry(x, y, w, h);
                    restored = true;
                }
            }
            if (!restored) {
                // Legacy: base64-encoded QWidget::saveGeometry() blob.
                // TODO(post-v0.10): drop this fallback once users have run a
                // build that saves in the new "x,y,w,h" format.
                const QByteArray blob = QByteArray::fromBase64(val.toUtf8());
                if (!blob.isEmpty() && restoreGeometry(blob))
                    restored = true;
            }
        }
    }
    if (!restored) {
        // Default: size to the container's natural sizeHint so we don't
        // open with dead space above and below the children.  Width
        // falls back to kDefaultW when the hint is too narrow to read.
        adjustSize();
        const QSize hint = sizeHint();
        const int w = std::max(kDefaultW, hint.width());
        const int h = std::max(hint.height(), 80);  // tiny floor
        resize(w, h);
        QScreen* screen = anchor && anchor->screen()
            ? anchor->screen()
            : QGuiApplication::primaryScreen();
        if (screen) {
            const QRect g = screen->availableGeometry();
            move(g.center().x() - w / 2,
                 g.center().y() - h / 2);
        }
    } else {
        // Clamp top-left to a visible screen — saved position may
        // reference a monitor that's no longer connected.
        bool onScreen = false;
        const QPoint tl = geometry().topLeft();
        for (QScreen* s : QGuiApplication::screens()) {
            if (s->availableGeometry().contains(tl)) { onScreen = true; break; }
        }
        if (!onScreen) {
            QScreen* screen = anchor && anchor->screen()
                ? anchor->screen() : QGuiApplication::primaryScreen();
            if (screen) {
                const QRect g = screen->availableGeometry();
                move(g.center().x() - width() / 2,
                     g.center().y() - height() / 2);
            }
        }
    }
    m_restoring = false;
}

void FloatingContainerWindow::saveGeometryToKey() const
{
    if (m_geometryKey.isEmpty()) return;
    // Store client-area rect as "x,y,w,h" — frame-agnostic so geometry
    // restores correctly whether the window is frameless or decorated.
    const QRect r = geometry();
    AppSettings::instance().setValue(
        m_geometryKey,
        QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}

void FloatingContainerWindow::prepareShutdown()
{
    m_saveTimer.stop();
    saveGeometryToKey();
    m_shuttingDown = true;
    close();
}

void FloatingContainerWindow::setFramelessMode(bool on)
{
    const bool wasVisible = isVisible();
    const QRect geom = geometry();
    Qt::WindowFlags flags = windowFlags();
    if (on) {
        flags |= Qt::FramelessWindowHint;
    } else {
        flags &= ~Qt::FramelessWindowHint;
    }
    setWindowFlags(flags);
    setGeometry(geom);
    if (wasVisible) show();
}

void FloatingContainerWindow::closeEvent(QCloseEvent* ev)
{
    if (m_shuttingDown) {
        ev->accept();
        return;
    }
    // Close = dock.  Manager handles reparenting via dockRequested.
    if (m_container) {
        emit dockRequested(m_container);
    }
    QWidget::closeEvent(ev);
}

void FloatingContainerWindow::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoring) m_saveTimer.start();
}

void FloatingContainerWindow::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoring) m_saveTimer.start();
}

} // namespace AetherSDR
