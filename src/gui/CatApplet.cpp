#include "CatApplet.h"
#include "core/RigctlServer.h"
#include "core/RigctlPty.h"
#include "core/DaxStreamManager.h"
#include "core/VirtualAudioBridge.h"
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
#include <QSettings>
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

static QLabel* sectionHeader(const QString& text)
{
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet("color: #00b4d8; font-size: 11px; font-weight: bold;");
    return lbl;
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

    QSettings settings;

    // ── TCP Section ─────────────────────────────────────────────────────────
    root->addWidget(sectionHeader("rigctld TCP Server"));

    auto* tcpRow = new QHBoxLayout;
    m_tcpEnable = new QCheckBox("Enable");
    m_tcpEnable->setChecked(settings.value("cat/tcpEnable", false).toBool());
    tcpRow->addWidget(m_tcpEnable);

    auto* portLabel = new QLabel("Port:");
    tcpRow->addWidget(portLabel);
    m_tcpPort = new QSpinBox;
    m_tcpPort->setRange(1024, 65535);
    m_tcpPort->setValue(settings.value("cat/tcpPort", 4532).toInt());
    m_tcpPort->setFixedWidth(70);
    tcpRow->addWidget(m_tcpPort);
    tcpRow->addStretch();
    root->addLayout(tcpRow);

    m_tcpStatus = new QLabel("Status: stopped");
    root->addWidget(m_tcpStatus);

    connect(m_tcpEnable, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("cat/tcpEnable", on);
        if (!m_server) return;
        if (on) {
            int port = m_tcpPort->value();
            QSettings().setValue("cat/tcpPort", port);
            m_server->start(static_cast<quint16>(port));
        } else {
            m_server->stop();
        }
        updateTcpStatus();
    });

    connect(m_tcpPort, &QSpinBox::editingFinished, this, [this]() {
        QSettings().setValue("cat/tcpPort", m_tcpPort->value());
        // Restart if running
        if (m_server && m_server->isRunning()) {
            m_server->stop();
            m_server->start(static_cast<quint16>(m_tcpPort->value()));
            updateTcpStatus();
        }
    });

    root->addWidget(separator());

    // ── PTY Section ─────────────────────────────────────────────────────────
    root->addWidget(sectionHeader("Virtual Serial Port"));

    m_ptyEnable = new QCheckBox("Enable");
    m_ptyEnable->setChecked(settings.value("cat/ptyEnable", false).toBool());
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

    connect(m_ptyEnable, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("cat/ptyEnable", on);
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

    // ── DAX Section ─────────────────────────────────────────────────────────
    root->addWidget(sectionHeader("DAX Audio Channels"));

    m_daxEnable = new QCheckBox("Enable DAX 1-4");
    m_daxEnable->setChecked(settings.value("cat/daxEnable", false).toBool());
    root->addWidget(m_daxEnable);

    // DAX gain slider (0–100 → 0.0–1.0, default 25 = -12 dB)
    auto* gainRow = new QHBoxLayout;
    gainRow->addWidget(new QLabel("DAX Gain:"));
    m_daxGain = new QSlider(Qt::Horizontal);
    m_daxGain->setRange(0, 100);
    m_daxGain->setValue(settings.value("cat/daxGain", 50).toInt());
    m_daxGain->setFixedHeight(18);
    m_daxGain->setStyleSheet(
        "QSlider::groove:horizontal { background: #182028; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #00b4d8; width: 12px; margin: -4px 0;"
        "  border-radius: 6px; }");
    gainRow->addWidget(m_daxGain, 1);
    m_daxGainLabel = new QLabel(QStringLiteral("%1%").arg(m_daxGain->value()));
    m_daxGainLabel->setFixedWidth(32);
    gainRow->addWidget(m_daxGainLabel);
    root->addLayout(gainRow);

    connect(m_daxGain, &QSlider::valueChanged, this, [this](int val) {
        m_daxGainLabel->setText(QStringLiteral("%1%").arg(val));
        QSettings().setValue("cat/daxGain", val);
        if (m_bridge)
            m_bridge->setGain(val / 100.0f);
    });

    // DAX TX gain slider (0–100 → 0.0–1.0, default 50 = −6 dB)
    auto* txGainRow = new QHBoxLayout;
    txGainRow->addWidget(new QLabel("TX Gain:"));
    m_daxTxGain = new QSlider(Qt::Horizontal);
    m_daxTxGain->setRange(0, 100);
    m_daxTxGain->setValue(settings.value("cat/daxTxGain", 50).toInt());
    m_daxTxGain->setFixedHeight(18);
    m_daxTxGain->setStyleSheet(
        "QSlider::groove:horizontal { background: #182028; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #00b4d8; width: 12px; margin: -4px 0;"
        "  border-radius: 6px; }");
    txGainRow->addWidget(m_daxTxGain, 1);
    m_daxTxGainLabel = new QLabel(QStringLiteral("%1%").arg(m_daxTxGain->value()));
    m_daxTxGainLabel->setFixedWidth(32);
    txGainRow->addWidget(m_daxTxGainLabel);
    root->addLayout(txGainRow);

    connect(m_daxTxGain, &QSlider::valueChanged, this, [this](int val) {
        m_daxTxGainLabel->setText(QStringLiteral("%1%").arg(val));
        QSettings().setValue("cat/daxTxGain", val);
        if (m_audio)
            m_audio->setDaxTxGain(val / 100.0f);
    });

    for (int i = 0; i < 4; ++i) {
        m_daxLabels[i] = new QLabel(QStringLiteral("DAX %1: —").arg(i + 1));
        root->addWidget(m_daxLabels[i]);
    }

    connect(m_daxEnable, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("cat/daxEnable", on);
        if (on) {
            if (m_dax) m_dax->requestDaxStreams();
            if (m_bridge) m_bridge->open();
        } else {
            if (m_dax) m_dax->releaseDaxStreams();
            if (m_bridge) m_bridge->close();
        }
    });

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

void CatApplet::setDaxStreamManager(DaxStreamManager* dax)
{
    m_dax = dax;
    if (dax) {
        connect(dax, &DaxStreamManager::streamCreated, this,
                [this](int ch, quint32 sid) {
                    if (ch >= 1 && ch <= 4)
                        m_daxLabels[ch - 1]->setText(
                            QStringLiteral("DAX %1: 0x%2 ✓")
                                .arg(ch)
                                .arg(sid, 8, 16, QChar('0')));
                });
        connect(dax, &DaxStreamManager::streamRemoved, this,
                [this](int ch) {
                    if (ch >= 1 && ch <= 4)
                        m_daxLabels[ch - 1]->setText(
                            QStringLiteral("DAX %1: —").arg(ch));
                });
    }
}

void CatApplet::setVirtualAudioBridge(VirtualAudioBridge* bridge)
{
    m_bridge = bridge;
    if (bridge) {
        // Apply saved gain
        int val = QSettings().value("cat/daxGain", 50).toInt();
        bridge->setGain(val / 100.0f);
    }
}

void CatApplet::setAudioEngine(AudioEngine* audio)
{
    m_audio = audio;
    if (audio) {
        // Apply saved TX gain
        int val = QSettings().value("cat/daxTxGain", 50).toInt();
        audio->setDaxTxGain(val / 100.0f);
    }
}

void CatApplet::onConnectionStateChanged(bool connected)
{
    if (connected) {
        // Auto-start DAX if it was enabled before
        if (m_daxEnable->isChecked()) {
            if (m_dax) m_dax->requestDaxStreams();
            if (m_bridge) m_bridge->open();
        }
    } else {
        // Release DAX streams on disconnect
        if (m_dax) m_dax->releaseDaxStreams();
        if (m_bridge) m_bridge->close();
    }
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
