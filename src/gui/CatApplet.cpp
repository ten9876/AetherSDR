#include "CatApplet.h"
#include "core/RigctlServer.h"
#include "core/RigctlPty.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QApplication>
#include "core/AppSettings.h"
#include <QFrame>
#include <QSlider>

namespace AetherSDR {

static constexpr const char* kSectionStyle =
    "QWidget { background: transparent; }"
    "QLabel { color: #8090a0; font-size: 10px; }"
    "QCheckBox { color: #c8d8e8; font-size: 11px; spacing: 4px; }"
    "QCheckBox::indicator { width: 14px; height: 14px; }"
    "QSpinBox { background: #182028; color: #c8d8e8; border: 1px solid #304050;"
    "  padding: 1px 4px; font-size: 11px; }"
    "QComboBox { background: #182028; color: #c8d8e8; border: 1px solid #304050;"
    "  padding: 1px 4px; font-size: 11px; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox QAbstractItemView { background: #182028; color: #c8d8e8;"
    "  selection-background-color: #00b4d8; }"
    "QPushButton { background: #1a2a3a; border: 1px solid #203040;"
    "  border-radius: 3px; padding: 2px 8px; font-size: 10px; color: #c8d8e8; }";

static QWidget* appletTitleBar(const QString& text)
{
    auto* bar = new QWidget;
    bar->setFixedHeight(16);
    bar->setStyleSheet(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #3a4a5a, stop:0.5 #2a3a4a, stop:1 #1a2a38); "
        "border-bottom: 1px solid #0a1a28; }");

    auto* lbl = new QLabel(text, bar);
    lbl->setStyleSheet("QLabel { background: transparent; color: #8aa8c0; "
                       "font-size: 10px; font-weight: bold; }");
    lbl->setGeometry(6, 1, 240, 14);
    return bar;
}

static QFrame* separator()
{
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #203040;");
    line->setFixedHeight(1);
    return line;
}

CatApplet::CatApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();  // hidden by default, shown via CAT toggle button
}

void CatApplet::buildUI()
{
    setStyleSheet(kSectionStyle);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(4);

    auto& settings = AppSettings::instance();

    // ── TCP Section ─────────────────────────────────────────────────────────
    root->addWidget(appletTitleBar("rigctld TCP Server"));

    static const QString kGreenToggle =
        "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
        " color: #c8d8e8; font-size: 10px; font-weight: bold; padding: 2px 8px; }"
        "QPushButton:hover { background: #204060; }"
        "QPushButton:checked { background: #006040; color: #00ff88; border: 1px solid #00a060; }";

    auto* tcpRow = new QHBoxLayout;
    m_tcpEnable = new QPushButton("Enable");
    m_tcpEnable->setCheckable(true);
    // Don't restore checked state — CAT auto-start is controlled by the
    // "Autostart CAT with AetherSDR" menu item, not per-button state.
    m_tcpEnable->setStyleSheet(kGreenToggle);
    m_tcpEnable->setFixedHeight(22);
    tcpRow->addWidget(m_tcpEnable);

    auto* portLabel = new QLabel("Port:");
    tcpRow->addWidget(portLabel);
    m_tcpPort = new QSpinBox;
    m_tcpPort->setRange(1024, 65535);
    m_tcpPort->setValue(settings.value("CatTcpPort", "4532").toInt());
    m_tcpPort->setFixedWidth(70);
    tcpRow->addWidget(m_tcpPort);
    tcpRow->addStretch();
    root->addLayout(tcpRow);

    m_tcpStatus = new QLabel("Status: stopped");
    root->addWidget(m_tcpStatus);

    connect(m_tcpEnable, &QPushButton::toggled, this, [this](bool on) {
        // State managed by Autostart CAT menu item
        if (!m_server) return;
        if (on) {
            int port = m_tcpPort->value();
            auto& ss = AppSettings::instance(); ss.setValue("CatTcpPort", QString::number(port)); ss.save();
            m_server->start(static_cast<quint16>(port));
        } else {
            m_server->stop();
        }
        updateTcpStatus();
    });

    connect(m_tcpPort, &QSpinBox::editingFinished, this, [this]() {
        { auto& ss = AppSettings::instance(); ss.setValue("CatTcpPort", QString::number(m_tcpPort->value())); ss.save(); }
        // Restart if running
        if (m_server && m_server->isRunning()) {
            m_server->stop();
            m_server->start(static_cast<quint16>(m_tcpPort->value()));
            updateTcpStatus();
        }
    });

    root->addWidget(separator());

    // ── PTY Section ─────────────────────────────────────────────────────────
    root->addWidget(appletTitleBar("Virtual Serial Port"));

    m_ptyEnable = new QPushButton("Enable");
    m_ptyEnable->setCheckable(true);
    // Don't restore checked state — controlled by Autostart CAT menu item.
    m_ptyEnable->setStyleSheet(kGreenToggle);
    m_ptyEnable->setFixedHeight(22);
    root->addWidget(m_ptyEnable);

    auto* ptyRow = new QHBoxLayout;
    m_ptyPath = new QLabel("Path: —");
    m_ptyPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ptyRow->addWidget(m_ptyPath, 1);

    m_ptyCopy = new QPushButton("Copy");
    m_ptyCopy->setFixedWidth(42);
    ptyRow->addWidget(m_ptyCopy);
    root->addLayout(ptyRow);

    connect(m_ptyCopy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_ptyPath->text().replace("Path: ", ""));
    });

    connect(m_ptyEnable, &QPushButton::toggled, this, [this](bool on) {
        // State managed by Autostart CAT menu item
        if (!m_pty) return;
        if (on)
            m_pty->start();
        else
            m_pty->stop();
        updatePtyStatus();
    });

    root->addWidget(separator());

    // ── Slice selector ──────────────────────────────────────────────────────
    auto* sliceRow = new QHBoxLayout;
    sliceRow->addWidget(new QLabel("Control Slice:"));
    m_sliceSelect = new QComboBox;
    m_sliceSelect->addItems({"A (Slice 0)", "B (Slice 1)"});
    m_sliceSelect->setCurrentIndex(0);
    sliceRow->addWidget(m_sliceSelect, 1);
    root->addLayout(sliceRow);

    root->addWidget(separator());

    // ── DAX Section (placeholder) ───────────────────────────────────────────
    root->addWidget(appletTitleBar("DAX Audio Channels"));

    m_daxPlaceholder = new QLabel("DAX virtual audio requires PipeWire\n"
                                   "integration — coming soon (issue #15)");
    m_daxPlaceholder->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
    m_daxPlaceholder->setAlignment(Qt::AlignCenter);
    root->addWidget(m_daxPlaceholder);

    root->addStretch();
}

void CatApplet::setRadioModel(RadioModel* model)
{
    m_model = model;
    if (model) {
        connect(model, &RadioModel::connectionStateChanged,
                this, &CatApplet::onConnectionStateChanged);
    }
}

void CatApplet::setRigctlServer(RigctlServer* server)
{
    m_server = server;
    if (server) {
        connect(server, &RigctlServer::clientCountChanged, this, &CatApplet::updateTcpStatus);

        // Auto-start if was enabled
        if (m_tcpEnable->isChecked()) {
            server->start(static_cast<quint16>(m_tcpPort->value()));
            updateTcpStatus();
        }
    }
}

void CatApplet::setRigctlPty(RigctlPty* pty)
{
    m_pty = pty;
    if (pty) {
        connect(pty, &RigctlPty::started, this, [this](const QString& path) {
            m_ptyPath->setText("Path: " + path);
        });
        connect(pty, &RigctlPty::stopped, this, [this]() {
            m_ptyPath->setText("Path: —");
        });

        // Auto-start if was enabled
        if (m_ptyEnable->isChecked()) {
            pty->start();
            updatePtyStatus();
        }
    }
}

void CatApplet::setAudioEngine(AudioEngine* audio)
{
    m_audio = audio;
}

void CatApplet::onConnectionStateChanged(bool /*connected*/)
{
    // DAX auto-start deferred — needs PipeWire virtual devices (issue #15)
}

void CatApplet::updateTcpStatus()
{
    if (!m_server || !m_server->isRunning()) {
        m_tcpStatus->setText("Status: stopped");
        return;
    }
    int n = m_server->clientCount();
    m_tcpStatus->setText(QStringLiteral("Status: listening on port %1 (%2 client%3)")
                             .arg(m_server->port())
                             .arg(n)
                             .arg(n == 1 ? "" : "s"));
}

void CatApplet::updatePtyStatus()
{
    if (!m_pty || !m_pty->isRunning()) {
        m_ptyPath->setText("Path: —");
    } else {
        m_ptyPath->setText("Path: " + m_pty->symlinkPath());
    }
}

} // namespace AetherSDR
