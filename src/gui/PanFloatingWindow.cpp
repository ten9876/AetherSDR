#include "PanFloatingWindow.h"
#include "FramelessResizer.h"
#include "PanadapterApplet.h"
#include "core/AppSettings.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>

namespace AetherSDR {

PanFloatingWindow::PanFloatingWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    const bool frameless =
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True";
    Qt::WindowFlags flags = Qt::Window;
    if (frameless) flags |= Qt::FramelessWindowHint;
    // Re-apply via setWindowFlags — some platform plugins ignore the
    // constructor bitmask and need an explicit call before show().
    setWindowFlags(flags);
    setMinimumSize(400, 300);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    FramelessResizer::install(this);
}

void PanFloatingWindow::adoptApplet(PanadapterApplet* applet)
{
    if (!applet) return;
    m_applet = applet;

    // Use the user-facing slice title (e.g. "Slice A") instead of raw hex pan ID
    QString title = applet->sliceTitle();
    if (title.isEmpty())
        title = QString("Pan %1").arg(applet->panId());
    setWindowTitle(QString("AetherSDR — %1").arg(title));

    // Reparent directly into this window — addWidget() calls setParent()
    // internally, so the widget goes straight from the splitter to the
    // floating window without an intermediate nullptr/top-level state.
    // This avoids corrupting the main window's NSResponder chain on macOS.
    m_layout->addWidget(m_applet, 1);

    // Show dock icon in the applet's title bar
    m_applet->setFloatingState(true);
    connect(m_applet, &PanadapterApplet::dockClicked, this, [this]() {
        emit dockRequested(panId());
    });
}

QString PanFloatingWindow::panId() const
{
    return m_applet ? m_applet->panId() : QString();
}

PanadapterApplet* PanFloatingWindow::takeApplet()
{
    if (!m_applet) return nullptr;
    auto* a = m_applet;
    m_layout->removeWidget(a);
    a->setParent(nullptr);
    m_applet = nullptr;
    return a;
}

void PanFloatingWindow::setFramelessMode(bool on)
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

void PanFloatingWindow::closeEvent(QCloseEvent* ev)
{
    if (m_shuttingDown) {
        ev->accept();
        return;
    }
    // Don't close — dock instead
    emit dockRequested(panId());
    ev->ignore();
}

void PanFloatingWindow::saveWindowGeometry()
{
    // Store client-area rect as "x,y,w,h" — frame-agnostic, so geometry
    // restores correctly whether the window is frameless or decorated.
    const QRect r = geometry();
    AppSettings::instance().setValue(
        QString("FloatingPan_%1_Geometry").arg(panId()),
        QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}

void PanFloatingWindow::restoreWindowGeometry()
{
    const QString val = AppSettings::instance()
        .value(QString("FloatingPan_%1_Geometry").arg(panId())).toString();
    if (val.isEmpty()) return;

    // New format: "x,y,w,h" — frame-agnostic.
    const QStringList parts = val.split(',');
    if (parts.size() == 4) {
        bool ok0, ok1, ok2, ok3;
        const int x = parts[0].toInt(&ok0);
        const int y = parts[1].toInt(&ok1);
        const int w = parts[2].toInt(&ok2);
        const int h = parts[3].toInt(&ok3);
        if (ok0 && ok1 && ok2 && ok3 && w > 0 && h > 0) {
            // Clamp top-left to a visible screen so windows on
            // disconnected monitors don't land off-screen.
            QRect r(x, y, w, h);
            bool onScreen = false;
            for (QScreen* s : QGuiApplication::screens()) {
                if (s->availableGeometry().contains(r.topLeft())) {
                    onScreen = true;
                    break;
                }
            }
            if (!onScreen) {
                QScreen* s = QGuiApplication::primaryScreen();
                if (s) {
                    const QRect avail = s->availableGeometry();
                    r.moveCenter(avail.center());
                }
            }
            setGeometry(r);
            return;
        }
    }
    // Legacy: base64-encoded QWidget::saveGeometry() blob from older builds.
    // TODO(post-v0.10): drop this fallback once users have run a build that
    // saves in the new "x,y,w,h" format.
    restoreGeometry(QByteArray::fromBase64(val.toLatin1()));
}

} // namespace AetherSDR
