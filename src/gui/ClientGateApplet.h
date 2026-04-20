#pragma once

#include <QWidget>

class QPushButton;
class QTimer;

namespace AetherSDR {

class AudioEngine;
class ClientCompKnob;
class ClientGateCurveWidget;
class ClientGateGrBar;

// Docked tile for the client-side TX gate / expander.  View-only —
// shows the static transfer curve with a live ball at the current
// input level, plus a compact horizontal gain-reduction strip, plus
// Enable / Edit… buttons.  The interactive editor lives in a separate
// floating window (ClientGateEditor).
//
// Single-path (TX only) to match ClientCompApplet — gates on a ham
// RX feed are a rare configuration and can be added later if asked for.
class ClientGateApplet : public QWidget {
    Q_OBJECT

public:
    explicit ClientGateApplet(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);

    // Pull the Enable toggle state from the bound ClientGate.  Called
    // by MainWindow after the floating editor toggles its bypass button
    // so both views stay in sync.
    void refreshEnableFromEngine();

signals:
    void editRequested();

private:
    void buildUI();
    void syncEnableFromEngine();
    void onEnableToggled(bool on);
    void tickMeter();

    AudioEngine*           m_audio{nullptr};
    QPushButton*           m_enable{nullptr};
    QPushButton*           m_edit{nullptr};
    ClientGateCurveWidget* m_curve{nullptr};
    ClientGateGrBar*       m_grBar{nullptr};
    // Five most-tuned knobs mirroring the editor: threshold, ratio,
    // attack, release, floor.  Saves a trip to the floating editor for
    // everyday tuning.
    ClientCompKnob*        m_thresh{nullptr};
    ClientCompKnob*        m_ratio{nullptr};
    ClientCompKnob*        m_attack{nullptr};
    ClientCompKnob*        m_release{nullptr};
    ClientCompKnob*        m_floor{nullptr};
    QTimer*                m_meterTimer{nullptr};

    float m_grDb{0.0f};
};

} // namespace AetherSDR
