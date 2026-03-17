#include "CatApplet.h"
#include "SliceColors.h"
#include "core/RigctlServer.h"
#include "core/RigctlPty.h"
#include "core/AudioEngine.h"
#include "ComboStyle.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QApplication>
#include "core/AppSettings.h"
#include <QFrame>

namespace AetherSDR {

static constexpr const char* kSectionStyle =
    "QWidget { background: transparent; }"
    "QLabel { color: #8090a0; font-size: 11px; }"
    "QPushButton { background: #1a2a3a; border: 1px solid #205070;"
    "  border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; color: #c8d8e8; }"
    "QPushButton:hover { background: #204060; }";

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

CatApplet::CatApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();  // hidden by default, shown via CAT toggle button
}

void CatApplet::buildUI()
{
    setStyleSheet(kSectionStyle);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    outer->addWidget(appletTitleBar("CAT Control"));

    auto* content = new QWidget;
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(4);
    outer->addWidget(content);

    auto& settings = AppSettings::instance();

    static const QString kGreenToggle =
        "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
        " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 8px; }"
        "QPushButton:hover { background: #204060; }"
        "QPushButton:checked { background: #006040; color: #00ff88; border: 1px solid #00a060; }";

    static constexpr const char* kDimLabel =
        "QLabel { color: #8090a0; font-size: 11px; }";

    static constexpr const char* kInsetStyle =
        "QLineEdit { font-size: 10px; background: #0a0a18; border: 1px solid #1e2e3e;"
        " border-radius: 3px; padding: 0px 2px; color: #c8d8e8; }";

    // ── Global enable row: [Enable TCP] [Enable TTY] Base port: [____] ───────
    auto* enableRow = new QHBoxLayout;
    enableRow->setSpacing(4);

    m_tcpEnable = new QPushButton("Enable TCP");
    m_tcpEnable->setCheckable(true);
    m_tcpEnable->setStyleSheet(kGreenToggle);
    m_tcpEnable->setFixedSize(76, 22);
    enableRow->addWidget(m_tcpEnable);

    m_ptyEnable = new QPushButton("Enable TTY");
    m_ptyEnable->setCheckable(true);
    m_ptyEnable->setStyleSheet(kGreenToggle);
    m_ptyEnable->setFixedSize(76, 22);
    enableRow->addWidget(m_ptyEnable);

    enableRow->addStretch();

    auto* portLabel = new QLabel("Base:");
    portLabel->setStyleSheet(kDimLabel);
    enableRow->addWidget(portLabel);

    m_basePort = new QLineEdit(settings.value("CatTcpPort", "4532").toString());
    m_basePort->setStyleSheet(kInsetStyle);
    m_basePort->setFixedWidth(40);
    m_basePort->setAlignment(Qt::AlignCenter);
    enableRow->addWidget(m_basePort);

    root->addLayout(enableRow);

    connect(m_basePort, &QLineEdit::editingFinished, this, [this]() {
        int port = m_basePort->text().toInt();
        if (port < 1024 || port > 65535) {
            port = 4532;
            m_basePort->setText("4532");
        }
        auto& ss = AppSettings::instance();
        ss.setValue("CatTcpPort", QString::number(port));
        ss.save();
        // If running, restart all servers with new base port
        if (m_tcpEnable->isChecked()) {
            for (int i = 0; i < kChannels; ++i) {
                if (m_servers[i]) {
                    m_servers[i]->stop();
                    m_servers[i]->start(static_cast<quint16>(port + i));
                }
            }
            updateAllChannelStatus();
        }
    });

    // ── TCP toggle: start/stop all 4 servers ─────────────────────────────────
    connect(m_tcpEnable, &QPushButton::toggled, this, [this](bool on) {
        int basePort = m_basePort->text().toInt();
        if (basePort < 1024 || basePort > 65535) basePort = 4532;
        auto& ss = AppSettings::instance();
        ss.setValue("CatTcpPort", QString::number(basePort));
        ss.save();
        for (int i = 0; i < kChannels; ++i) {
            if (!m_servers[i]) continue;
            if (on)
                m_servers[i]->start(static_cast<quint16>(basePort + i));
            else
                m_servers[i]->stop();
        }
        updateAllChannelStatus();
    });

    // ── PTY toggle: start/stop all 4 PTYs ───────────────────────────────────
    connect(m_ptyEnable, &QPushButton::toggled, this, [this](bool on) {
        for (int i = 0; i < kChannels; ++i) {
            if (!m_ptys[i]) continue;
            if (on)
                m_ptys[i]->start();
            else
                m_ptys[i]->stop();
        }
        updateAllChannelStatus();
    });

    // ── Per-channel status rows ──────────────────────────────────────────────
    static const char kLetters[] = "ABCD";

    for (int i = 0; i < kChannels; ++i) {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        // Coloured badge
        m_rows[i].badge = new QLabel(QString(kLetters[i]));
        m_rows[i].badge->setFixedWidth(16);
        m_rows[i].badge->setAlignment(Qt::AlignCenter);
        m_rows[i].badge->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 11px; font-weight: bold; }")
                .arg(kSliceColors[i].hexActive));
        row->addWidget(m_rows[i].badge);

        // TCP status
        m_rows[i].tcpStatus = new QLabel("(stopped)");
        m_rows[i].tcpStatus->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
        m_rows[i].tcpStatus->setFixedWidth(100);
        row->addWidget(m_rows[i].tcpStatus);

        // PTY path
        m_rows[i].ptyPath = new QLabel(
            QStringLiteral("/tmp/AetherSDR-CAT-%1").arg(kLetters[i]));
        m_rows[i].ptyPath->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
        row->addWidget(m_rows[i].ptyPath, 1);

        root->addLayout(row);
    }

    // ── DAX Section ─────────────────────────────────────────────────────────
    outer->addWidget(appletTitleBar("DAX Audio Channels"));

    // DAX enable row
    auto* daxEnRow = new QHBoxLayout;
    daxEnRow->setContentsMargins(4, 2, 4, 2);
    auto* daxLabel = new QLabel("DAX:");
    daxLabel->setStyleSheet(kDimLabel);
    daxEnRow->addWidget(daxLabel);
    daxEnRow->addStretch();
    m_daxEnable = new QPushButton("Enable");
    m_daxEnable->setCheckable(true);
    m_daxEnable->setStyleSheet(kGreenToggle);
    m_daxEnable->setFixedSize(60, 22);
    daxEnRow->addWidget(m_daxEnable);
    outer->addLayout(daxEnRow);

    // RX channel meters (DAX 1-4)
    const QString kMeterStyle =
        "QProgressBar { background: #0a0a18; border: 1px solid #1e2e3e;"
        "  border-radius: 2px; max-height: 10px; }"
        "QProgressBar::chunk { background: #00b4d8; border-radius: 1px; }";
    const QString kStatusLabel = "QLabel { color: #506070; font-size: 11px; }";

    for (int i = 0; i < 4; ++i) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 1, 4, 1);
        row->setSpacing(4);
        auto* chLabel = new QLabel(QString("DAX %1:").arg(i + 1));
        chLabel->setStyleSheet(kDimLabel);
        chLabel->setFixedWidth(40);
        row->addWidget(chLabel);

        m_daxRxStatus[i] = new QLabel(QStringLiteral("\u2014"));
        m_daxRxStatus[i]->setStyleSheet(kStatusLabel);
        m_daxRxStatus[i]->setFixedWidth(40);
        row->addWidget(m_daxRxStatus[i]);

        m_daxRxLevel[i] = new QProgressBar;
        m_daxRxLevel[i]->setRange(0, 100);
        m_daxRxLevel[i]->setValue(0);
        m_daxRxLevel[i]->setTextVisible(false);
        m_daxRxLevel[i]->setStyleSheet(kMeterStyle);
        row->addWidget(m_daxRxLevel[i], 1);

        outer->addLayout(row);
    }

    // TX meter
    auto* txRow = new QHBoxLayout;
    txRow->setContentsMargins(4, 1, 4, 1);
    txRow->setSpacing(4);
    auto* txLabel = new QLabel("TX:");
    txLabel->setStyleSheet(kDimLabel);
    txLabel->setFixedWidth(40);
    txRow->addWidget(txLabel);

    m_daxTxStatus = new QLabel(QStringLiteral("\u2014"));
    m_daxTxStatus->setStyleSheet(kStatusLabel);
    m_daxTxStatus->setFixedWidth(40);
    txRow->addWidget(m_daxTxStatus);

    m_daxTxLevel = new QProgressBar;
    m_daxTxLevel->setRange(0, 100);
    m_daxTxLevel->setValue(0);
    m_daxTxLevel->setTextVisible(false);
    m_daxTxLevel->setStyleSheet(kMeterStyle);
    txRow->addWidget(m_daxTxLevel, 1);

    outer->addLayout(txRow);
}

void CatApplet::setRadioModel(RadioModel* model)
{
    m_model = model;
    if (model) {
        connect(model, &RadioModel::connectionStateChanged,
                this, [this](bool) { updateAllChannelStatus(); });

        // Wire slice add/remove for DAX channel tracking
        connect(model, &RadioModel::sliceAdded, this, [this](SliceModel* s) {
            connect(s, &SliceModel::daxChannelChanged, this, [this]() {
                // Update DAX status labels
                for (int i = 0; i < 4; ++i)
                    m_daxRxStatus[i]->setText(QStringLiteral("\u2014"));
                if (!m_model) return;
                static const char letters[] = "ABCDEFGH";
                for (auto* sl : m_model->slices()) {
                    int ch = sl->daxChannel();
                    if (ch >= 1 && ch <= 4) {
                        m_daxRxStatus[ch - 1]->setText(
                            QString("Slice %1").arg(letters[sl->sliceId()]));
                    }
                }
            });
        });

        // Wire TX state for TX status label
        connect(model->transmitModel(), &TransmitModel::moxChanged, this, [this]() {
            if (!m_model) { m_daxTxStatus->setText(QStringLiteral("\u2014")); return; }
            bool isTx = m_model->transmitModel()->isMox();
            if (isTx) {
                static const char letters[] = "ABCDEFGH";
                for (auto* s : m_model->slices()) {
                    if (s->isTxSlice()) {
                        m_daxTxStatus->setText(QString("Slice %1").arg(letters[s->sliceId()]));
                        return;
                    }
                }
                m_daxTxStatus->setText("TX");
            } else {
                m_daxTxStatus->setText("Ready");
            }
        });
    }
}

void CatApplet::setRigctlServers(RigctlServer** servers, int count)
{
    for (int i = 0; i < kChannels && i < count; ++i) {
        m_servers[i] = servers[i];
        if (servers[i]) {
            connect(servers[i], &RigctlServer::clientCountChanged,
                    this, [this, i]() { updateChannelStatus(i); });
        }
    }
}

void CatApplet::setRigctlPtys(RigctlPty** ptys, int count)
{
    for (int i = 0; i < kChannels && i < count; ++i) {
        m_ptys[i] = ptys[i];
        if (ptys[i]) {
            connect(ptys[i], &RigctlPty::started, this,
                    [this, i](const QString& path) {
                        m_rows[i].ptyPath->setText(path);
                    });
            connect(ptys[i], &RigctlPty::stopped, this,
                    [this, i]() {
                        static const char kLetters[] = "ABCD";
                        m_rows[i].ptyPath->setText(
                            QStringLiteral("/tmp/AetherSDR-CAT-%1").arg(kLetters[i]));
                    });
        }
    }
}

void CatApplet::setAudioEngine(AudioEngine* audio)
{
    m_audio = audio;
}

void CatApplet::updateChannelStatus(int ch)
{
    if (ch < 0 || ch >= kChannels) return;

    auto* srv = m_servers[ch];
    if (!srv || !srv->isRunning()) {
        m_rows[ch].tcpStatus->setText("(stopped)");
        m_rows[ch].tcpStatus->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
    } else {
        int n = srv->clientCount();
        m_rows[ch].tcpStatus->setText(
            QStringLiteral(":%1 (%2 client%3)")
                .arg(srv->port())
                .arg(n)
                .arg(n == 1 ? "" : "s"));
        m_rows[ch].tcpStatus->setStyleSheet(
            n > 0 ? QStringLiteral("QLabel { color: %1; font-size: 10px; }")
                         .arg(kSliceColors[ch].hexActive)
                   : "QLabel { color: #8090a0; font-size: 10px; }");
    }
}

void CatApplet::updateAllChannelStatus()
{
    for (int i = 0; i < kChannels; ++i)
        updateChannelStatus(i);
}

void CatApplet::setTcpEnabled(bool on)
{
    QSignalBlocker b(m_tcpEnable);
    m_tcpEnable->setChecked(on);
    updateAllChannelStatus();
}

void CatApplet::setPtyEnabled(bool on)
{
    QSignalBlocker b(m_ptyEnable);
    m_ptyEnable->setChecked(on);
    updateAllChannelStatus();
}

} // namespace AetherSDR
