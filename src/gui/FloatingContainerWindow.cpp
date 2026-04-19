#include "FloatingContainerWindow.h"

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

FloatingContainerWindow::FloatingContainerWindow(const QString& containerId,
                                                 const QString& title,
                                                 QWidget* content,
                                                 QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , m_containerId(containerId)
{
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose, false);  // ContainerManager owns lifecycle
    setMinimumWidth(260);

    setStyleSheet(
        "FloatingContainerWindow { background: #0f0f1a; "
        "border: 1px solid #203040; }"
        "QWidget { background: #0a0a18; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Custom title bar ──────────────────────────────────────────────────
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
    dockBtn->setToolTip("Return container to the panel");
    dockBtn->setStyleSheet(
        "QPushButton { background: #1a2a3a; color: #8aa8c0; "
        "border: 1px solid #304050; border-radius: 3px; "
        "font-size: 10px; padding: 0 5px; }"
        "QPushButton:hover { background: #243848; color: #c8d8e8; }");
    connect(dockBtn, &QPushButton::clicked, this, [this]() {
        captureGeometry();
        emit dockRequested(m_containerId);
    });
    tbLayout->addWidget(dockBtn);

    root->addWidget(titleBar);

    // ── Content area ──────────────────────────────────────────────────────
    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    if (content) {
        m_contentLayout->addWidget(content);
    }
    root->addLayout(m_contentLayout);

    adjustSize();

    // Debounce geometry saves — fire 400 ms after the last resize/move.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &FloatingContainerWindow::captureGeometry);

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        m_appShuttingDown = true;
    });
}

QRect FloatingContainerWindow::savedGeometry() const
{
    return m_savedGeo;
}

void FloatingContainerWindow::applySavedGeometry(const QRect& rect)
{
    m_savedGeo = rect;
}

void FloatingContainerWindow::captureGeometry()
{
    if (!isVisible()) { return; }
    const QPoint framePos = pos();
    const QSize  clientSz = geometry().size();
    m_savedGeo = QRect(framePos, clientSz);
}

void FloatingContainerWindow::restoreGeometryImpl(const QRect& geo)
{
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) { return; }

    resize(geo.size());

    // Wayland compositors own window placement — skip move().
    if (QGuiApplication::platformName() == QLatin1String("wayland")) { return; }

    // Clamp to the nearest screen's available geometry so the title bar
    // is always reachable even if the screen layout changed.
    QScreen* screen = nullptr;
    if (windowHandle()) { screen = windowHandle()->screen(); }
    if (!screen) { screen = QGuiApplication::primaryScreen(); }
    if (!screen) { return; }

    const QRect avail = screen->availableGeometry();
    const int x = qBound(avail.left(), geo.x(),
                          avail.right()  - qMin(geo.width(),  avail.width()));
    const int y = qBound(avail.top(),  geo.y(),
                          avail.bottom() - qMin(geo.height(), avail.height()));
    move(x, y);
}

void FloatingContainerWindow::showAndRestore(const QRect& geo)
{
    m_restoringGeometry = true;
    show();
    raise();
    QTimer::singleShot(200, this, [this, geo]() { restoreGeometryImpl(geo); });
    QTimer::singleShot(650, this, [this]() { m_restoringGeometry = false; });
}

void FloatingContainerWindow::hideAndCapture()
{
    m_saveTimer->stop();
    captureGeometry();
    hide();
}

void FloatingContainerWindow::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoringGeometry) { m_saveTimer->start(); }
}

void FloatingContainerWindow::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoringGeometry) { m_saveTimer->start(); }
}

void FloatingContainerWindow::hideEvent(QHideEvent* ev)
{
    QWidget::hideEvent(ev);
    if (!m_restoringGeometry) {
        m_saveTimer->stop();
        captureGeometry();
    }
}

void FloatingContainerWindow::closeEvent(QCloseEvent* ev)
{
    if (auto* p = qobject_cast<QWidget*>(parent()); m_appShuttingDown || !p || !p->isVisible()) {
        m_saveTimer->stop();
        captureGeometry();
        ev->accept();
        return;
    }
    captureGeometry();
    emit dockRequested(m_containerId);
    ev->ignore();
}

} // namespace AetherSDR
