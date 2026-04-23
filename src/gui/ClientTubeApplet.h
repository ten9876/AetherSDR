#pragma once

#include <QWidget>

class QPushButton;
class QTimer;

namespace AetherSDR {

class AudioEngine;
class ClientCompKnob;
class ClientTubeCurveWidget;

// Docked tile for the client-side TX dynamic tube saturator.  Shows
// the transfer curve (which bends with Drive / Bias / Model) and a
// live input ball, plus Enable / Edit buttons.  Interactive editor
// is in ClientTubeEditor.
class ClientTubeApplet : public QWidget {
    Q_OBJECT

public:
    explicit ClientTubeApplet(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);
    void refreshEnableFromEngine();

signals:
    void editRequested();

private:
    void buildUI();
    void syncEnableFromEngine();
    void onEnableToggled(bool on);

    AudioEngine*           m_audio{nullptr};
    QPushButton*           m_enable{nullptr};
    QPushButton*           m_edit{nullptr};
    ClientTubeCurveWidget* m_curve{nullptr};
    // Five-knob tuning row — Drive, Tone, Bias, Output, Mix (Dry/Wet).
    ClientCompKnob*        m_drive{nullptr};
    ClientCompKnob*        m_tone{nullptr};
    ClientCompKnob*        m_bias{nullptr};
    ClientCompKnob*        m_output{nullptr};
    ClientCompKnob*        m_mix{nullptr};
    // Light polling timer — keeps the knobs in sync with any changes
    // made in the floating editor (and vice versa).
    QTimer*                m_syncTimer{nullptr};
};

} // namespace AetherSDR
