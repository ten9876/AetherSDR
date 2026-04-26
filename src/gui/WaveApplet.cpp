#include "WaveApplet.h"

#include "WaveformWidget.h"

#include <QSizePolicy>
#include <QVBoxLayout>

namespace AetherSDR {

WaveApplet::WaveApplet(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    m_waveform = new WaveformWidget(this);
    layout->addWidget(m_waveform);

    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void WaveApplet::appendScopeSamples(const QByteArray& monoFloat32Pcm,
                                    int sampleRate,
                                    bool tx)
{
    if (m_waveform)
        m_waveform->appendScopeSamples(monoFloat32Pcm, sampleRate, tx);
}

void WaveApplet::setTransmitting(bool tx)
{
    if (m_waveform)
        m_waveform->setTransmitting(tx);
}

} // namespace AetherSDR
