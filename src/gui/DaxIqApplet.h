#pragma once

#include <QWidget>

class QPushButton;
class QComboBox;
class QProgressBar;

namespace AetherSDR {

class RadioModel;

// DAX IQ Applet — per-channel IQ rate selection, level meters, enable buttons.
// Split from the former monolithic CatApplet (DIGI) (#1627).
class DaxIqApplet : public QWidget {
    Q_OBJECT

public:
    static constexpr int kChannels = 4;

    explicit DaxIqApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);
    void setDaxIqLevel(int channel, float rms);

signals:
    void iqEnableRequested(int channel);
    void iqDisableRequested(int channel);
    void iqRateChanged(int channel, int rate);

private:
    void buildUI();

    RadioModel* m_model{nullptr};

    QPushButton*  m_iqEnable[kChannels]{};
    QComboBox*    m_iqRateCombo[kChannels]{};
    QProgressBar* m_iqMeter[kChannels]{};
};

} // namespace AetherSDR
