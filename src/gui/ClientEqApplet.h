#pragma once

#include <QWidget>

class QPushButton;

namespace AetherSDR {

class AudioEngine;
class ClientEqCurveWidget;

// Docked applet for client-side parametric EQ. Shows a compact live
// analyzer with the summed EQ response overlaid; the separate floating
// editor window (Edit… button) is where users add, remove, and configure
// bands interactively.
//
// RX and TX share the applet — the tab at top selects which path is
// being viewed. The Enable toggle controls the selected path's EQ.
class ClientEqApplet : public QWidget {
    Q_OBJECT

public:
    enum class Path { Rx = 0, Tx = 1 };

    explicit ClientEqApplet(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);

    Path currentPath() const { return m_currentPath; }

    // Pull the Enable toggle state from the bound ClientEq — called by
    // MainWindow after the floating editor toggles its bypass button so
    // both views stay in sync.
    void refreshEnableFromEngine();

signals:
    // Fired when the user clicks Edit…. MainWindow owns the single
    // editor window instance and shows / raises it in response.
    void editRequested(Path path);

private:
    void buildUI();
    void setPath(Path p);
    void syncEnableFromEngine();
    void onEnableToggled(bool on);

    AudioEngine* m_audio{nullptr};
    Path         m_currentPath{Path::Rx};

    QPushButton*         m_rxTab{nullptr};
    QPushButton*         m_txTab{nullptr};
    QPushButton*         m_enable{nullptr};
    QPushButton*         m_edit{nullptr};
    ClientEqCurveWidget* m_curve{nullptr};
};

} // namespace AetherSDR
