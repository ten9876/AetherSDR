#pragma once

#include <QWidget>

class QTimer;

namespace AetherSDR {

class AudioEngine;
class ClientCompKnob;

// Docked tile for the client-side TX reverb (Freeverb).  Compact
// 5-knob row — Size, Decay, Damping, PreDly, Mix — matches the PUDU
// applet footprint.  Enable/Edit live on the CHAIN widget gestures
// (single-click / double-click).  Interactive editor is
// ClientReverbEditor.
class ClientReverbApplet : public QWidget {
    Q_OBJECT

public:
    explicit ClientReverbApplet(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);
    void refreshEnableFromEngine();

signals:
    void editRequested();

private:
    void buildUI();
    void syncKnobsFromEngine();

    AudioEngine*    m_audio{nullptr};
    ClientCompKnob* m_size{nullptr};
    ClientCompKnob* m_decay{nullptr};
    ClientCompKnob* m_damping{nullptr};
    ClientCompKnob* m_preDly{nullptr};
    ClientCompKnob* m_mix{nullptr};
    QTimer*         m_syncTimer{nullptr};
};

} // namespace AetherSDR
