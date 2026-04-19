#pragma once

#include <QWidget>

class QPushButton;
class QLabel;

namespace AetherSDR {

class RadioModel;
class MeterSlider;

// DAX Audio Applet — per-channel RX meters/sliders, TX meter/slider, enable.
// Split from the former monolithic CatApplet (DIGI) (#1627).
class DaxApplet : public QWidget {
    Q_OBJECT

public:
    static constexpr int kChannels = 4;

    explicit DaxApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);
    void setDaxEnabled(bool on);
    void setDaxRxLevel(int channel, float rms);
    void setDaxTxLevel(float rms);

signals:
    void daxToggled(bool on);
    void daxRxGainChanged(int channel, float gain);  // 1-4, 0.0–1.0
    void daxTxGainChanged(float gain);

private:
    void buildUI();

    RadioModel* m_model{nullptr};

    QPushButton*  m_daxEnable{nullptr};
    MeterSlider*  m_daxRxMeter[kChannels]{};
    QLabel*       m_daxRxStatus[kChannels]{};
    MeterSlider*  m_daxTxMeter{nullptr};
    QLabel*       m_daxTxStatus{nullptr};

    // Per-channel exponential smoothing state (member, not static)
    float m_smoothedRx[kChannels]{};
};

} // namespace AetherSDR
