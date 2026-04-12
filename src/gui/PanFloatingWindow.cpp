#include "PanFloatingWindow.h"
#include "PanadapterApplet.h"
#include "core/AppSettings.h"

#include <QVBoxLayout>
#include <QCloseEvent>
#include <QPushButton>

namespace AetherSDR {

PanFloatingWindow::PanFloatingWindow(PanadapterApplet* applet, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_applet(applet)
{
    setWindowTitle(QString("AetherSDR — Pan %1").arg(applet->panId()));
    setMinimumSize(400, 300);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Reparent the applet into this window
    m_applet->setParent(this);
    m_layout->addWidget(m_applet, 1);
    m_applet->show();

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
    auto& s = AppSettings::instance();
    s.setValue(QString("FloatingPan_%1_Geometry").arg(panId()),
              QString::fromLatin1(saveGeometry().toBase64()));
}

void PanFloatingWindow::restoreWindowGeometry()
{
    auto& s = AppSettings::instance();
    QString geom = s.value(QString("FloatingPan_%1_Geometry").arg(panId())).toString();
    if (!geom.isEmpty()) {
        restoreGeometry(QByteArray::fromBase64(geom.toLatin1()));
    }
}

} // namespace AetherSDR
