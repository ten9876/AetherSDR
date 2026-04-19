#pragma once

#include "core/AudioEngine.h"

#include <QWidget>

namespace AetherSDR {

class ClientChainWidget;

// Docked "CHAIN" tile — wraps ClientChainWidget with the applet chrome
// so the TX DSP chain appears in the applet tray alongside CEQ and
// CMP.  Clicking a stage opens its editor (signal forwarded to
// MainWindow); dragging reorders the chain in place.
class ClientChainApplet : public QWidget {
    Q_OBJECT

public:
    explicit ClientChainApplet(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);

    // Call after any external change to TX DSP state (e.g. the user
    // toggled bypass from a floating editor) so the chain strip
    // repaints with the new bypass state.
    void refreshFromEngine();

signals:
    // Forwarded from the internal widget — MainWindow maps each stage
    // to the corresponding editor.
    void editRequested(AudioEngine::TxChainStage stage);

    // Forwarded — MainWindow uses this to show / hide the stage's
    // applet tile in sync with its DSP bypass state.
    void stageEnabledChanged(AudioEngine::TxChainStage stage, bool enabled);

private:
    ClientChainWidget* m_chain{nullptr};
};

} // namespace AetherSDR
