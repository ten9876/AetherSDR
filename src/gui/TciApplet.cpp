#ifdef HAVE_WEBSOCKETS
#include "TciApplet.h"
#include "MeterSlider.h"
#include "core/TciServer.h"
#include "core/AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

static constexpr const char* kTciSectionStyle =
    "QWidget { background: transparent; }"
    "QLabel { color: #8090a0; font-size: 11px; }"
    "QPushButton { background: #1a2a3a; border: 1px solid #205070;"
    "  border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; color: #c8d8e8; }"
    "QPushButton:hover { background: #204060; }";

TciApplet::TciApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();
}

void TciApplet::buildUI()
{
    setStyleSheet(kTciSectionStyle);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

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

    // ── Per-channel RX meters with gain sliders ──────────────────────────
    for (int i = 0; i < kChannels; ++i) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 1, 4, 1);
        row->setSpacing(4);

        auto* chLabel = new QLabel(QString("RX%1:").arg(i + 1));
        chLabel->setStyleSheet(kDimLabel);
        chLabel->setFixedWidth(28);
        row->addWidget(chLabel);

        m_rxMeter[i] = new MeterSlider;
        {
            auto key = QStringLiteral("TciRxGain%1").arg(i + 1);
            float saved = settings.value(key, "1.0").toString().toFloat();
            m_rxMeter[i]->setGain(std::clamp(saved, 0.0f, 1.0f));
        }
        connect(m_rxMeter[i], &MeterSlider::gainChanged, this, [this, i](float g) {
            auto& ss = AppSettings::instance();
            ss.setValue(QStringLiteral("TciRxGain%1").arg(i + 1), QString::number(g, 'f', 2));
            ss.save();
            emit tciRxGainChanged(i + 1, g);
        });
        row->addWidget(m_rxMeter[i], 1);

        outer->addLayout(row);
    }

    // ── TX meter with gain slider ────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 1, 4, 1);
        row->setSpacing(4);

        auto* txLabel = new QLabel("TX:");
        txLabel->setStyleSheet(kDimLabel);
        txLabel->setFixedWidth(28);
        row->addWidget(txLabel);

        m_txMeter = new MeterSlider;
        {
            float saved = settings.value("TciTxGain", "1.0").toString().toFloat();
            m_txMeter->setGain(std::clamp(saved, 0.0f, 1.0f));
        }
        connect(m_txMeter, &MeterSlider::gainChanged, this, [this](float g) {
            auto& ss = AppSettings::instance();
            ss.setValue("TciTxGain", QString::number(g, 'f', 2));
            ss.save();
            emit tciTxGainChanged(g);
        });
        row->addWidget(m_txMeter, 1);

        outer->addLayout(row);
    }

    // ── Port + status + enable row ───────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 2, 4, 2);
        row->setSpacing(4);

        m_tciEnable = new QPushButton("Enable");
        m_tciEnable->setCheckable(true);
        m_tciEnable->setStyleSheet(kGreenToggle);
        m_tciEnable->setFixedSize(60, 22);
        row->addWidget(m_tciEnable);

        m_tciStatus = new QLabel("(stopped)");
        m_tciStatus->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
        row->addWidget(m_tciStatus, 1);

        auto* tciPortLabel = new QLabel("Port:");
        tciPortLabel->setStyleSheet(kDimLabel);
        row->addWidget(tciPortLabel);

        m_tciPort = new QLineEdit(settings.value("TciPort", "50001").toString());
        m_tciPort->setStyleSheet(kInsetStyle);
        m_tciPort->setFixedWidth(46);
        m_tciPort->setAlignment(Qt::AlignCenter);
        row->addWidget(m_tciPort);

        outer->addLayout(row);

        connect(m_tciPort, &QLineEdit::editingFinished, this, [this]() {
            int port = m_tciPort->text().toInt();
            if (port < 1024 || port > 65535) {
                port = 50001;
                m_tciPort->setText("50001");
            }
            auto& ss = AppSettings::instance();
            ss.setValue("TciPort", QString::number(port));
            ss.save();
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
}

void TciApplet::setTciServer(TciServer* tci)
{
    m_tciServer = tci;
    if (tci) {
        connect(tci, &TciServer::clientCountChanged,
                this, [this]() { updateTciStatus(); });
    }
}

void TciApplet::setTciEnabled(bool on)
{
    if (m_tciEnable) {
        QSignalBlocker b(m_tciEnable);
        m_tciEnable->setChecked(on);
    }
    updateTciStatus();
}

void TciApplet::updateTciStatus()
{
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
}

void TciApplet::setTciRxLevel(int channel, float rms)
{
    if (channel < 1 || channel > kChannels) return;
    float& s = m_smoothedRx[channel - 1];
    float alpha = (rms > s) ? 0.4f : 0.08f;
    s = alpha * rms + (1.0f - alpha) * s;
    m_rxMeter[channel - 1]->setLevel(std::clamp(s * 2.0f, 0.0f, 1.0f));
}

void TciApplet::setTciTxLevel(float rms)
{
    m_txMeter->setLevel(std::clamp(rms * 2.0f, 0.0f, 1.0f));
}

} // namespace AetherSDR

#endif // HAVE_WEBSOCKETS
