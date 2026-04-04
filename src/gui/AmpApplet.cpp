#include "AmpApplet.h"
#include "HGauge.h"

#include <QVBoxLayout>
#include <QLabel>

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


} // namespace AetherSDR
