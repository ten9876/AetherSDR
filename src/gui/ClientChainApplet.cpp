#include "ClientChainApplet.h"
#include "ClientChainWidget.h"

#include <QLabel>
#include <QVBoxLayout>

namespace AetherSDR {

ClientChainApplet::ClientChainApplet(QWidget* parent) : QWidget(parent)
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(2);

    auto* hint = new QLabel(
        "Click to edit · drag to reorder · right-click to bypass");
    hint->setStyleSheet(
        "QLabel { color: #607888; font-size: 9px;"
        " background: transparent; border: none; }");
    hint->setWordWrap(false);
    outer->addWidget(hint);

    m_chain = new ClientChainWidget;
    outer->addWidget(m_chain);

    connect(m_chain, &ClientChainWidget::editRequested,
            this, &ClientChainApplet::editRequested);
    connect(m_chain, &ClientChainWidget::stageEnabledChanged,
            this, &ClientChainApplet::stageEnabledChanged);
    // chainReordered is informational; the widget already pushed the
    // new order to the engine.  No-op here for now.

    hide();  // hidden until toggled on from the applet tray
}

void ClientChainApplet::setAudioEngine(AudioEngine* engine)
{
    if (m_chain) m_chain->setAudioEngine(engine);
}

void ClientChainApplet::refreshFromEngine()
{
    if (m_chain) m_chain->update();
}

} // namespace AetherSDR
