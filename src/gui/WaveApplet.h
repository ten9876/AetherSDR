#pragma once

#include <QByteArray>
#include <QWidget>

namespace AetherSDR {

class WaveformWidget;

class WaveApplet : public QWidget {
    Q_OBJECT

public:
    explicit WaveApplet(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {240, 165}; }
    QSize minimumSizeHint() const override { return {220, 120}; }

public slots:
    void appendScopeSamples(const QByteArray& monoFloat32Pcm, int sampleRate, bool tx);
    void setTransmitting(bool tx);

private:
    WaveformWidget* m_waveform{nullptr};
};

} // namespace AetherSDR
