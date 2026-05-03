#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QSlider;

namespace AetherSDR {

class AudioEngine;
class StripWaveform;

// "Waveform CE-SSB" panel — small strip-row tile that embeds a copy
// of the WaveformWidget (forked into StripWaveform so we can iterate
// on it without disturbing the floating Waveform applet).  Defaults
// to Envelope view since CE-SSB is fundamentally about envelope
// behaviour; a single toggle button on the right cycles between
// Scope / Envelope / History modes.
class StripWaveformPanel : public QWidget {
    Q_OBJECT

public:
    explicit StripWaveformPanel(AudioEngine* engine,
                                QWidget* parent = nullptr);
    ~StripWaveformPanel() override;

    void showForTx();

    // Match the other strip panels' API surface — present even though
    // the waveform display has no engine controls of its own yet.
    void syncControlsFromEngine();

private:
    void cycleViewMode();
    void applyViewMode();
    void applyWindowSec(int sec);

    AudioEngine*    m_audio{nullptr};
    StripWaveform*  m_waveform{nullptr};
    QPushButton*    m_modeBtn{nullptr};
    QSlider*        m_windowSlider{nullptr};
    QLabel*         m_windowLbl{nullptr};
    int             m_modeIdx{1};   // 0=Scope, 1=Envelope, 2=History
};

} // namespace AetherSDR
