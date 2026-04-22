#include "FloatingContainerWindow.h"
#include "ContainerWidget.h"
#include "core/AppSettings.h"

#include <QByteArray>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

constexpr int kDefaultW = 300;
constexpr int kDefaultH = 240;

} // namespace

FloatingContainerWindow::FloatingContainerWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet("QWidget { background: #08121d; }");
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this,
            &FloatingContainerWindow::saveGeometryToKey);
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
        const QByteArray geom = QByteArray::fromBase64(
            AppSettings::instance()
                .value(m_geometryKey, "").toByteArray());
        if (!geom.isEmpty() && restoreGeometry(geom)) {
            restored = true;
        }
    }
    if (!restored) {
        // Default: centre on the anchor's screen at a reasonable size.
        QScreen* screen = anchor && anchor->screen()
            ? anchor->screen()
            : QGuiApplication::primaryScreen();
        if (screen) {
            const QRect g = screen->availableGeometry();
            resize(kDefaultW, kDefaultH);
            move(g.center().x() - kDefaultW / 2,
                 g.center().y() - kDefaultH / 2);
        } else {
            resize(kDefaultW, kDefaultH);
        }
    } else {
        // Clamp to any visible screen — saved geometry may reference
        // a monitor that's no longer connected.
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
    AppSettings::instance().setValue(
        m_geometryKey, saveGeometry().toBase64());
}

void FloatingContainerWindow::closeEvent(QCloseEvent* ev)
{
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
