#include "CatApplet.h"
#include "GuardedSlider.h"
#include "SliceColors.h"
#include <algorithm>
#include <cmath>
#include "core/RigctlServer.h"
#include "core/RigctlPty.h"
#include "core/AudioEngine.h"
#ifdef HAVE_WEBSOCKETS
#include "core/TciServer.h"
#endif
#include "ComboStyle.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/DaxIqModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include "MeterSlider.h"
#include <QApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QProgressBar>
#include "core/AppSettings.h"
#include <QFrame>

namespace AetherSDR {

static constexpr const char* kSectionStyle =
    "QWidget { background: transparent; }"
    "QLabel { color: #8090a0; font-size: 11px; }"
    "QPushButton { background: #1a2a3a; border: 1px solid #205070;"
    "  border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; color: #c8d8e8; }"
    "QPushButton:hover { background: #204060; }";



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

    // ── CAT Control section header ────────────────────────────────────────
    {
        auto* lbl = new QLabel("CAT Control");
        lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; font-weight: bold; "
            "border-top: 1px solid #304050; padding: 3px 6px 1px 6px; }");
        root->addWidget(lbl);
    }

    // ── Enable row (created here, added to layout after channel rows) ────────
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

    root->addLayout(enableRow);

    // ── TCI Section ─────────────────────────────────────────────────────────
#ifdef HAVE_WEBSOCKETS
    {
        auto* lbl = new QLabel("TCI Server");
        lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; font-weight: bold; "
            "border-top: 1px solid #304050; padding: 3px 6px 1px 6px; }");
        root->addWidget(lbl);
    }
    {
        auto* tciRow = new QHBoxLayout;
        tciRow->setSpacing(4);

        m_tciEnable = new QPushButton("Enable");
        m_tciEnable->setCheckable(true);
        m_tciEnable->setStyleSheet(kGreenToggle);
        m_tciEnable->setFixedSize(60, 22);
        tciRow->addWidget(m_tciEnable);

        m_tciStatus = new QLabel("(stopped)");
        m_tciStatus->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
        tciRow->addWidget(m_tciStatus, 1);

        auto* tciPortLabel = new QLabel("Port:");
        tciPortLabel->setStyleSheet(kDimLabel);
        tciRow->addWidget(tciPortLabel);

        m_tciPort = new QLineEdit(settings.value("TciPort", "50001").toString());
        m_tciPort->setStyleSheet(kInsetStyle);
        m_tciPort->setFixedWidth(46);
        m_tciPort->setAlignment(Qt::AlignCenter);
        tciRow->addWidget(m_tciPort);

        root->addLayout(tciRow);

        connect(m_tciPort, &QLineEdit::editingFinished, this, [this]() {
            int port = m_tciPort->text().toInt();
            if (port < 1024 || port > 65535) {
                port = 50001;
                m_tciPort->setText("50001");
            }
            auto& ss = AppSettings::instance();
            ss.setValue("TciPort", QString::number(port));
            ss.save();
            // If running, restart with new port
            if (m_tciEnable->isChecked() && m_tciServer) {
                m_tciServer->stop();
                m_tciServer->start(static_cast<quint16>(port));
                updateTciStatus();
            }
        });

        connect(m_tciEnable, &QPushButton::toggled, this, [this](bool on) {
            if (!m_tciServer) return;
            int port = m_tciPort->text().toInt();
            if (port < 1024 || port > 65535) port = 50001;
            auto& ss = AppSettings::instance();
            ss.setValue("TciPort", QString::number(port));
            ss.save();
            if (on)
                m_tciServer->start(static_cast<quint16>(port));
            else
                m_tciServer->stop();
            updateTciStatus();
        });
    }
#endif

    // ── DAX Section ─────────────────────────────────────────────────────────
    {
        auto* lbl = new QLabel("DAX Audio Channels");
        lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; font-weight: bold; "
            "border-top: 1px solid #304050; padding: 3px 6px 1px 6px; }");
        outer->addWidget(lbl);
    }

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

    // DAX enable button → save setting + notify MainWindow
    {
        const QSignalBlocker b(m_daxEnable);
        m_daxEnable->setChecked(
            settings.value("AutoStartDAX", "False").toString() == "True");
    }
    connect(m_daxEnable, &QPushButton::toggled, this, [this](bool on) {
        auto& ss = AppSettings::instance();
        ss.setValue("AutoStartDAX", on ? "True" : "False");
        ss.save();
        emit daxToggled(on);
    });

    // RX channel meter/sliders (DAX 1-4)
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

        m_daxRxMeter[i] = new MeterSlider;
        {
            auto key = QStringLiteral("DaxRxGain%1").arg(i + 1);
            float saved = settings.value(key, "0.5").toString().toFloat();
            m_daxRxMeter[i]->setGain(std::clamp(saved, 0.0f, 1.0f));
        }
        connect(m_daxRxMeter[i], &MeterSlider::gainChanged, this, [this, i](float g) {
            auto& ss = AppSettings::instance();
            ss.setValue(QStringLiteral("DaxRxGain%1").arg(i + 1), QString::number(g, 'f', 2));
            ss.save();
            emit daxRxGainChanged(i + 1, g);
        });
        row->addWidget(m_daxRxMeter[i], 1);

        outer->addLayout(row);
    }

    // TX meter/slider
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

    m_daxTxMeter = new MeterSlider;
    {
        float saved = settings.value("DaxTxGain", "0.5").toString().toFloat();
        m_daxTxMeter->setGain(std::clamp(saved, 0.0f, 1.0f));
    }
    connect(m_daxTxMeter, &MeterSlider::gainChanged, this, [this](float g) {
        auto& ss = AppSettings::instance();
        ss.setValue("DaxTxGain", QString::number(g, 'f', 2));
        ss.save();
        emit daxTxGainChanged(g);
    });
    txRow->addWidget(m_daxTxMeter, 1);

    outer->addLayout(txRow);
    outer->addLayout(daxEnRow);

    // ── DAX IQ section ──────────────────────────────────────────────────
    {
        auto* lbl = new QLabel("DAX IQ");
        lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; font-weight: bold; "
            "border-top: 1px solid #304050; padding: 3px 6px 1px 6px; }");
        outer->addWidget(lbl);
    }
    outer->addSpacing(2);

    static const QString kIqBtnOn =
        "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
        "border: 1px solid #008ba8; padding: 2px 8px; border-radius: 3px; font-size: 10px; }";
    static const QString kIqBtnOff =
        "QPushButton { background: #1a2a3a; color: #8090a0; "
        "border: 1px solid #205070; padding: 2px 8px; border-radius: 3px; font-size: 10px; }";

    for (int i = 0; i < kChannels; ++i) {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        row->setContentsMargins(4, 1, 4, 1);

        auto* label = new QLabel(QString("IQ %1:").arg(i + 1));
        label->setStyleSheet(kDimLabel);
        label->setFixedWidth(28);
        row->addWidget(label);

        m_iqRateCombo[i] = new GuardedComboBox;
        applyComboStyle(m_iqRateCombo[i]);
        m_iqRateCombo[i]->addItem("24k",  24000);
        m_iqRateCombo[i]->addItem("48k",  48000);
        m_iqRateCombo[i]->addItem("96k",  96000);
        m_iqRateCombo[i]->addItem("192k", 192000);
        m_iqRateCombo[i]->setCurrentIndex(1);  // default 48k
        m_iqRateCombo[i]->setFixedWidth(60);
        connect(m_iqRateCombo[i], &QComboBox::currentIndexChanged, this, [this, i]() {
            int rate = m_iqRateCombo[i]->currentData().toInt();
            emit iqRateChanged(i + 1, rate);
        });
        row->addWidget(m_iqRateCombo[i]);

        m_iqMeter[i] = new QProgressBar;
        m_iqMeter[i]->setRange(0, 100);
        m_iqMeter[i]->setValue(0);
        m_iqMeter[i]->setTextVisible(false);
        m_iqMeter[i]->setFixedHeight(14);
        m_iqMeter[i]->setStyleSheet(
            "QProgressBar { background: #0a0a14; border: 1px solid #203040; border-radius: 2px; }"
            "QProgressBar::chunk { background: #00b4d8; }");
        row->addWidget(m_iqMeter[i], 1);

        m_iqEnable[i] = new QPushButton("Off");
        m_iqEnable[i]->setFixedWidth(36);
        m_iqEnable[i]->setStyleSheet(kIqBtnOff);
        connect(m_iqEnable[i], &QPushButton::clicked, this, [this, i]() {
            bool wasOn = m_iqEnable[i]->text() == "On";
            if (wasOn) {
                emit iqDisableRequested(i + 1);
                m_iqEnable[i]->setText("Off");
                m_iqEnable[i]->setStyleSheet(kIqBtnOff);
                m_iqMeter[i]->setValue(0);
            } else {
                emit iqEnableRequested(i + 1);
                m_iqEnable[i]->setText("On");
                m_iqEnable[i]->setStyleSheet(kIqBtnOn);
            }
        });
        row->addWidget(m_iqEnable[i]);

        outer->addLayout(row);
    }
}

void CatApplet::setRadioModel(RadioModel* model)
{
    m_model = model;
    if (model) {
        connect(model, &RadioModel::connectionStateChanged,
                this, [this](bool connected) {
            updateAllChannelStatus();
            // Reset IQ buttons — streams are per-session, not persisted by radio
            if (connected) {
                static const QString kOff =
                    "QPushButton { background: #1a2a3a; color: #8090a0; "
                    "border: 1px solid #205070; padding: 2px 8px; border-radius: 3px; font-size: 10px; }";
                for (int i = 0; i < kChannels; ++i) {
                    if (m_iqEnable[i]) {
                        m_iqEnable[i]->setText("Off");
                        m_iqEnable[i]->setStyleSheet(kOff);
                        if (m_iqMeter[i]) m_iqMeter[i]->setValue(0);
                    }
                }
            }
        });

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

        // Wire TX slice label — always show which slice has TX privileges
        auto updateTxLabel = [this]() {
            if (!m_model) { m_daxTxStatus->setText(QStringLiteral("\u2014")); return; }
            static const char letters[] = "ABCDEFGH";
            for (auto* s : m_model->slices()) {
                if (s->isTxSlice()) {
                    m_daxTxStatus->setText(QString("Slice %1").arg(letters[s->sliceId()]));
                    return;
                }
            }
            m_daxTxStatus->setText(QStringLiteral("\u2014"));
        };
        connect(model, &RadioModel::sliceAdded, this, [this, updateTxLabel](SliceModel* s) {
            connect(s, &SliceModel::txSliceChanged, this, updateTxLabel);
            updateTxLabel();
        });
        updateTxLabel();

        // Wire DAX IQ stream state changes → sync On/Off buttons
        connect(&model->daxIqModel(), &DaxIqModel::streamChanged, this, [this](int ch) {
            if (ch < 1 || ch > kChannels) return;
            int idx = ch - 1;
            bool exists = m_model->daxIqModel().stream(ch).exists;
            m_iqEnable[idx]->setText(exists ? "On" : "Off");
            static const QString kOn =
                "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
                "border: 1px solid #008ba8; padding: 2px 8px; border-radius: 3px; font-size: 10px; }";
            static const QString kOff =
                "QPushButton { background: #1a2a3a; color: #8090a0; "
                "border: 1px solid #205070; padding: 2px 8px; border-radius: 3px; font-size: 10px; }";
            m_iqEnable[idx]->setStyleSheet(exists ? kOn : kOff);
            if (!exists) m_iqMeter[idx]->setValue(0);

            // Sync rate combo from radio state
            int rate = m_model->daxIqModel().stream(ch).sampleRate;
            QSignalBlocker sb(m_iqRateCombo[idx]);
            for (int i = 0; i < m_iqRateCombo[idx]->count(); ++i) {
                if (m_iqRateCombo[idx]->itemData(i).toInt() == rate) {
                    m_iqRateCombo[idx]->setCurrentIndex(i);
                    break;
                }
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

void CatApplet::setTciServer(TciServer* tci)
{
#ifdef HAVE_WEBSOCKETS
    m_tciServer = tci;
    if (tci) {
        connect(tci, &TciServer::clientCountChanged,
                this, [this]() { updateTciStatus(); });
    }
#else
    (void)tci;
#endif
}

void CatApplet::setTciEnabled(bool on)
{
#ifdef HAVE_WEBSOCKETS
    if (m_tciEnable) {
        QSignalBlocker b(m_tciEnable);
        m_tciEnable->setChecked(on);
    }
    updateTciStatus();
#else
    (void)on;
#endif
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

void CatApplet::setDaxEnabled(bool on)
{
    QSignalBlocker b(m_daxEnable);
    m_daxEnable->setChecked(on);
}

void CatApplet::setDaxRxLevel(int channel, float rms)
{
    if (channel < 1 || channel > kChannels) return;
    // Exponential smoothing: fast attack (α=0.4), slow decay (α=0.08)
    static float smoothed[kChannels]{};
    float& s = smoothed[channel - 1];
    float alpha = (rms > s) ? 0.4f : 0.08f;
    s = alpha * rms + (1.0f - alpha) * s;
    m_daxRxMeter[channel - 1]->setLevel(std::clamp(s * 2.0f, 0.0f, 1.0f));
}

void CatApplet::setDaxTxLevel(float rms)
{
    m_daxTxMeter->setLevel(std::clamp(rms * 2.0f, 0.0f, 1.0f));
}

void CatApplet::setDaxIqLevel(int channel, float rms)
{
    if (channel < 1 || channel > kChannels) return;
    // Scale RMS to 0-100 for QProgressBar (IQ values are typically 0.0-0.5 range)
    int level = static_cast<int>(std::clamp(rms * 200.0f, 0.0f, 100.0f));
    m_iqMeter[channel - 1]->setValue(level);
}

void CatApplet::updateTciStatus()
{
#ifdef HAVE_WEBSOCKETS
    if (!m_tciStatus) return;
    if (!m_tciServer || !m_tciServer->isRunning()) {
        m_tciStatus->setText("(stopped)");
        m_tciStatus->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
    } else {
        int n = m_tciServer->clientCount();
        m_tciStatus->setText(
            QStringLiteral(":%1 (%2 client%3)")
                .arg(m_tciServer->port())
                .arg(n)
                .arg(n == 1 ? "" : "s"));
        m_tciStatus->setStyleSheet(
            n > 0 ? "QLabel { color: #00b4d8; font-size: 10px; }"
                  : "QLabel { color: #8090a0; font-size: 10px; }");
    }
#endif
}

} // namespace AetherSDR
