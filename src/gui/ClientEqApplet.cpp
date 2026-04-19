#include "ClientEqApplet.h"
#include "ClientEqCurveWidget.h"
#include "core/AudioEngine.h"
#include "core/ClientEq.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

namespace AetherSDR {

namespace {

// Tab button: small rectangle, checked state highlighted. Two of these
// live at the top of the applet (RX / TX). Shared style to keep them
// visually symmetric.
constexpr const char* kTabStyle =
    "QPushButton {"
    "  background: #0e1b28;"
    "  color: #8aa8c0;"
    "  border: 1px solid #203040;"
    "  border-radius: 3px;"
    "  font-size: 10px;"
    "  font-weight: bold;"
    "  padding: 2px 10px;"
    "}"
    "QPushButton:checked {"
    "  background: #0070c0;"
    "  color: #ffffff;"
    "  border-color: #0090e0;"
    "}"
    "QPushButton:hover { background: #1a2a3a; }"
    "QPushButton:checked:hover { background: #0080d0; }";

// Enable is the prominent green toggle. Matches other applet Enable toggles.
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

ClientEqApplet::ClientEqApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();  // hidden until toggled on from the button tray
}

void ClientEqApplet::buildUI()
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // Tab row — RX / TX selector.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);

        m_rxTab = new QPushButton("RX");
        m_rxTab->setCheckable(true);
        m_rxTab->setChecked(true);
        m_rxTab->setStyleSheet(kTabStyle);
        m_rxTab->setFixedHeight(20);
        row->addWidget(m_rxTab);

        m_txTab = new QPushButton("TX");
        m_txTab->setCheckable(true);
        m_txTab->setStyleSheet(kTabStyle);
        m_txTab->setFixedHeight(20);
        row->addWidget(m_txTab);
        row->addStretch();

        connect(m_rxTab, &QPushButton::clicked, this, [this]() { setPath(Path::Rx); });
        connect(m_txTab, &QPushButton::clicked, this, [this]() { setPath(Path::Tx); });

        outer->addLayout(row);
    }

    // Curve / analyzer area. Phase B.1 draws just the grid; B.2 wires in
    // the summed response from the current path's ClientEq; B.3 adds the
    // live FFT analyzer overlay.
    m_curve = new ClientEqCurveWidget;
    m_curve->setMinimumHeight(90);
    outer->addWidget(m_curve, 1);

    // Bottom row — Enable toggle on the left, Edit… on the right.
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
        m_edit->setToolTip("Open the client EQ editor for the selected path");
        row->addWidget(m_edit);

        connect(m_enable, &QPushButton::toggled, this,
                &ClientEqApplet::onEnableToggled);
        connect(m_edit, &QPushButton::clicked, this, [this]() {
            emit editRequested(m_currentPath);
        });

        outer->addLayout(row);
    }
}

void ClientEqApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (!m_audio) return;
    // Bind the curve widget to the currently-selected path so it can
    // render band state (meaningful once B.2 draws the summed curve).
    m_curve->setEq(m_audio->clientEqRx());
    syncEnableFromEngine();
}

void ClientEqApplet::setPath(Path p)
{
    m_currentPath = p;
    if (m_rxTab) {
        QSignalBlocker b1(m_rxTab);
        QSignalBlocker b2(m_txTab);
        m_rxTab->setChecked(p == Path::Rx);
        m_txTab->setChecked(p == Path::Tx);
    }
    if (m_audio && m_curve) {
        m_curve->setEq(p == Path::Rx ? m_audio->clientEqRx()
                                     : m_audio->clientEqTx());
    }
    syncEnableFromEngine();
}

void ClientEqApplet::syncEnableFromEngine()
{
    if (!m_audio || !m_enable) return;
    ClientEq* eq = (m_currentPath == Path::Rx)
        ? m_audio->clientEqRx() : m_audio->clientEqTx();
    QSignalBlocker b(m_enable);
    m_enable->setChecked(eq && eq->isEnabled());
    if (m_curve) m_curve->update();
}

void ClientEqApplet::refreshEnableFromEngine()
{
    syncEnableFromEngine();
}

void ClientEqApplet::onEnableToggled(bool on)
{
    if (!m_audio) return;
    ClientEq* eq = (m_currentPath == Path::Rx)
        ? m_audio->clientEqRx() : m_audio->clientEqTx();
    if (!eq) return;
    eq->setEnabled(on);
    m_audio->saveClientEqSettings();
    if (m_curve) m_curve->update();
}

} // namespace AetherSDR
