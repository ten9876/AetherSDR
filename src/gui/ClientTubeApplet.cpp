#include "ClientTubeApplet.h"
#include "ClientTubeCurveWidget.h"
#include "core/AudioEngine.h"
#include "core/ClientTube.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

namespace AetherSDR {

namespace {

const QString kEnableStyle =
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
    "  color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 10px;"
    "}"
    "QPushButton:hover { background: #204060; }"
    "QPushButton:checked {"
    "  background: #006040; color: #00ff88; border: 1px solid #00a060;"
    "}";

constexpr const char* kEditStyle =
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
    "  color: #c8d8e8; font-size: 11px; padding: 2px 10px;"
    "}"
    "QPushButton:hover { background: #204060; }";

} // namespace

ClientTubeApplet::ClientTubeApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();
}

void ClientTubeApplet::buildUI()
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    m_curve = new ClientTubeCurveWidget;
    m_curve->setCompactMode(true);
    m_curve->setMinimumHeight(90);
    outer->addWidget(m_curve, 1);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_enable = new QPushButton("Enable");
        m_enable->setCheckable(true);
        m_enable->setStyleSheet(kEnableStyle);
        m_enable->setFixedHeight(22);
        row->addWidget(m_enable);

        row->addStretch();

        m_edit = new QPushButton("Edit…");
        m_edit->setStyleSheet(kEditStyle);
        m_edit->setFixedHeight(22);
        m_edit->setToolTip("Open the dynamic tube editor");
        row->addWidget(m_edit);

        connect(m_enable, &QPushButton::toggled, this,
                &ClientTubeApplet::onEnableToggled);
        connect(m_edit, &QPushButton::clicked, this, [this]() {
            emit editRequested();
        });

        outer->addLayout(row);
    }
}

void ClientTubeApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (!m_audio) return;
    m_curve->setTube(m_audio->clientTubeTx());
    syncEnableFromEngine();
}

void ClientTubeApplet::syncEnableFromEngine()
{
    if (!m_audio || !m_enable) return;
    ClientTube* t = m_audio->clientTubeTx();
    QSignalBlocker b(m_enable);
    m_enable->setChecked(t && t->isEnabled());
    if (m_curve) m_curve->update();
}

void ClientTubeApplet::refreshEnableFromEngine()
{
    syncEnableFromEngine();
}

void ClientTubeApplet::onEnableToggled(bool on)
{
    if (!m_audio) return;
    ClientTube* t = m_audio->clientTubeTx();
    if (!t) return;
    t->setEnabled(on);
    m_audio->saveClientTubeSettings();
    if (m_curve) m_curve->update();
}

} // namespace AetherSDR
