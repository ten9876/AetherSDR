#pragma once

#include <QDialog>
#include <QLabel>
#include <QSlider>
#include <QPushButton>

namespace AetherSDR {

class RadioModel;

class SpotSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpotSettingsDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    RadioModel* m_model;

    QPushButton* m_spotsToggle;
    QSlider*     m_levelsSlider;
    QLabel*      m_levelsValue;
    QSlider*     m_positionSlider;
    QLabel*      m_positionValue;
    QSlider*     m_fontSizeSlider;
    QLabel*      m_fontSizeValue;
    QPushButton* m_overrideColorsToggle;
    QPushButton* m_overrideBgEnabled;
    QPushButton* m_overrideBgAuto;
    QLabel*      m_totalSpotsLabel;

    bool m_spotsEnabled{true};
    bool m_overrideColors{false};
    bool m_overrideBg{true};
    bool m_overrideBgAutoMode{true};
};

} // namespace AetherSDR
