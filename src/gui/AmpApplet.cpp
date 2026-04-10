#include "AmpApplet.h"
#include "HGauge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QFrame>

namespace AetherSDR {

AmpApplet::AmpApplet(QWidget* parent)
    : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 2, 4, 2);
    vbox->setSpacing(2);

    // Fwd Power gauge: 0-2000W
    m_fwdGauge = new HGauge(0.0f, 2000.0f, 1500.0f, "Fwd Pwr", "",
        {{0, "0"}, {500, "500"}, {1000, "1000"}, {1500, "1.5k"}, {2000, "2k"}});
    vbox->addWidget(m_fwdGauge);

    // SWR gauge: 1-3
    m_swrGauge = new HGauge(1.0f, 3.0f, 2.5f, "SWR", "",
        {{1.0f, "1"}, {1.5f, "1.5"}, {2.0f, "2"}, {2.5f, "2.5"}, {3.0f, "3"}});
    vbox->addWidget(m_swrGauge);

    // Temp gauge: 30-100°C
    m_tempGauge = new HGauge(30.0f, 100.0f, 80.0f, "Temp", "°C",
        {{30, "30"}, {50, "50"}, {80, "80"}, {100, "100"}});
    vbox->addWidget(m_tempGauge);

    vbox->addSpacing(8);
    // PGXL direct telemetry — stacked labels + OPERATE button
    static const char* kLabelStyle =
        "QLabel { color: #c8d8e8; font-size: 10px; }";

    auto* telRow = new QHBoxLayout;
    telRow->setSpacing(4);

    auto* telStack = new QVBoxLayout;
    telStack->setSpacing(0);
    telStack->setContentsMargins(0, 0, 0, 0);

    m_powerLabel = new QLabel;
    m_powerLabel->setStyleSheet(kLabelStyle);
    m_powerLabel->hide();
    telStack->addWidget(m_powerLabel);

    m_meffLabel = new QLabel;
    m_meffLabel->setStyleSheet(kLabelStyle);
    m_meffLabel->hide();
    telStack->addWidget(m_meffLabel);

    telRow->addLayout(telStack);

    telRow->addSpacing(30);

    m_operateBtn = new QPushButton("OPERATE");
    m_operateBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_operateBtn->setStyleSheet(
        "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
        "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
        "QPushButton:hover { background: #204060; }");
    m_operateBtn->hide();
    connect(m_operateBtn, &QPushButton::clicked, this, [this]() {
        // Toggle: if currently operating → standby, else → operate
        bool isOp = (m_operateBtn->text() == "OPERATE");
        emit operateToggled(!isOp);
    });
    telRow->addWidget(m_operateBtn, 1);

    vbox->addLayout(telRow);

    // ── Plugin tab section (#1109) ────────────────────────────────────────────
    // Hidden until addPluginTab() is called. A horizontal rule separates
    // the PGXL gauges from the plugin tabs.
    m_pluginSection = new QWidget;
    auto* pluginLayout = new QVBoxLayout(m_pluginSection);
    pluginLayout->setContentsMargins(0, 6, 0, 0);
    pluginLayout->setSpacing(4);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("QFrame { color: #203040; }");
    pluginLayout->addWidget(sep);

    m_pluginTabs = new QTabWidget;
    m_pluginTabs->setTabPosition(QTabWidget::North);
    m_pluginTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #203040; background: #0a1020; }"
        "QTabBar::tab { background: #1a2a3a; color: #8aa8c0; "
        "border: 1px solid #203040; padding: 3px 8px; margin-right: 2px; "
        "font-size: 10px; }"
        "QTabBar::tab:selected { background: #0a1020; color: #c8d8e8; "
        "border-bottom-color: #0a1020; }");
    pluginLayout->addWidget(m_pluginTabs);

    m_pluginSection->hide();
    vbox->addWidget(m_pluginSection);
}

void AmpApplet::setFwdPower(float watts)
{
    m_fwdGauge->setValue(watts);
}

void AmpApplet::setSwr(float swr)
{
    m_swrGauge->setValue(swr);
}

void AmpApplet::setTemp(float degC)
{
    m_tempGauge->setValue(degC);
}

void AmpApplet::setDrainCurrent(float amps)
{
    m_drainAmps = amps;
    updatePowerLabel();
}

void AmpApplet::setMainsVoltage(int volts)
{
    m_mainsVolts = volts;
    updatePowerLabel();
}

void AmpApplet::updatePowerLabel()
{
    m_powerLabel->setText(
        QStringLiteral("Volts: %1V\u00A0\u00A0Amps: %2A")
            .arg(m_mainsVolts)
            .arg(m_drainAmps, 0, 'f', 1));
    m_powerLabel->show();
}

void AmpApplet::setState(const QString& state)
{
    // Match TGXL OPERATE button style: green when operating, default when standby
    // PGXL states: IDLE (ready), TRANSMIT_A/TRANSMIT_B (keyed), OPERATE, STANDBY, POWERUP, FAULT
    bool operating = (state == "IDLE" || state == "OPERATE"
                      || state.startsWith("TRANSMIT"));
    if (operating) {
        m_operateBtn->setText("OPERATE");
        m_operateBtn->setStyleSheet(
            "QPushButton { background: #006030; border: 1px solid #008040; "
            "border-radius: 3px; color: #ffffff; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover { background: #007040; }");
    } else {
        m_operateBtn->setText("STANDBY");
        m_operateBtn->setStyleSheet(
            "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
            "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover { background: #204060; }");
    }
    m_operateBtn->show();
}

void AmpApplet::setMeff(const QString& meff)
{
    m_meffLabel->setText(QStringLiteral("MEffA:\u00A0\u00A0\u00A0%1").arg(meff));
    m_meffLabel->show();
}

void AmpApplet::addPluginTab(const QString& tabLabel, QWidget* widget)
{
    m_pluginTabs->addTab(widget, tabLabel);
    m_pluginSection->show();
}

} // namespace AetherSDR
