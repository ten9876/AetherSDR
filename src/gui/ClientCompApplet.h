#pragma once

#include <QWidget>

class QPushButton;
class QTimer;

namespace AetherSDR {

class AudioEngine;
class ClientCompCurveWidget;
class ClientCompGrBar;

// Docked dashboard tile for the client-side TX compressor.  View-only —
// shows the transfer curve with a live "ball" at the current envelope
// level, plus a compact horizontal gain-reduction strip, plus Bypass /
// Edit… buttons.  The full interactive editor lives in a separate
// floating window (ClientCompEditor).
//
// Single-path design: unlike the EQ applet there's no RX / TX tab —
// Phase 1 only exposes the TX-side compressor.  Phase 2+ can extend
// this if we ever add an RX-side compressor; today that would just be
// a CPU-waste feature.
class ClientCompApplet : public QWidget {
    Q_OBJECT

public:
    explicit ClientCompApplet(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);

    // Pull the Bypass toggle state from the bound ClientComp.  Called
    // by MainWindow after the floating editor toggles its bypass
    // button so both views stay in sync.
    void refreshEnableFromEngine();

signals:
    // Fired when the user clicks Edit…. MainWindow owns the single
    // editor window instance and shows / raises it in response.
    void editRequested();

private:
    void buildUI();
    void syncEnableFromEngine();
    void onEnableToggled(bool on);
    void tickMeter();

    AudioEngine*          m_audio{nullptr};
    QPushButton*          m_enable{nullptr};
    QPushButton*          m_edit{nullptr};
    ClientCompCurveWidget* m_curve{nullptr};
    ClientCompGrBar*      m_grBar{nullptr};   // narrow horizontal GR strip
    QTimer*               m_meterTimer{nullptr};

    // Latest GR dB (0 = no reduction, negative = active).  Paint is
    // driven by a QTimer polling ClientComp::gainReductionDb() at 30 Hz
    // so the strip feels live.
    float m_grDb{0.0f};
};

} // namespace AetherSDR
