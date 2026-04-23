#pragma once

#include "ClientEqApplet.h"  // for Path enum
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace AetherSDR {

class AudioEngine;
class ClientEqEditorCanvas;
class ClientEqFftAnalyzer;
class ClientEqIconRow;
class ClientEqOutputFader;
class ClientEqParamRow;

// Floating editor window for the client-side parametric EQ. One single
// instance lives on MainWindow; calling showForPath() swaps its bound
// ClientEq between RX and TX and raises the window. Geometry persists
// across shows via AppSettings (`ClientEqEditorGeometry` key).
//
// Phase B.2 layout: title bar, path-indicator strip, large interactive
// curve canvas. Phases B.3+ add the filter-type icon row at the top,
// bottom parameter-text row with per-band columns, master gain knob on
// the right, and the FFT analyzer overlay inside the canvas.
class ClientEqEditor : public QWidget {
    Q_OBJECT

public:
    explicit ClientEqEditor(AudioEngine* engine, QWidget* parent = nullptr);
    ~ClientEqEditor() override;

    // Switch the editor to the given path and show / raise the window.
    // Safe to call whether or not the window is currently visible.
    void showForPath(ClientEqApplet::Path path);

signals:
    // Fired when the bypass button is toggled in the editor. The docked
    // applet subscribes so its Enable toggle stays in sync — both widgets
    // read/write the same ClientEq::enabled flag underneath.
    void bypassToggled(ClientEqApplet::Path path, bool bypassed);

protected:
    void closeEvent(QCloseEvent* ev) override;
    void moveEvent(QMoveEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

private:
    void saveGeometryToSettings();
    void restoreGeometryFromSettings();

    // Fans a band-selection change out to icon row, canvas, and param row
    // so all three views share the same highlighted column / handle.
    void syncSelection(int idx);

    // Refresh bypass-button state from the bound ClientEq's enabled flag.
    void syncBypassFromEq();

    // Pull latest post-EQ samples from AudioEngine, run the FFT, push
    // the smoothed bins into the curve widget. Called by m_fftTimer.
    void tickFftAnalyzer();

    AudioEngine*               m_audio{nullptr};
    ClientEqApplet::Path       m_path{ClientEqApplet::Path::Rx};
    QLabel*                    m_pathLabel{nullptr};
    QComboBox*                 m_familyCombo{nullptr};
    QPushButton*               m_bypass{nullptr};
    ClientEqIconRow*           m_iconRow{nullptr};
    ClientEqEditorCanvas*      m_canvas{nullptr};
    ClientEqParamRow*          m_paramRow{nullptr};
    ClientEqOutputFader*       m_outFader{nullptr};
    QTimer*                    m_fftTimer{nullptr};
    std::unique_ptr<ClientEqFftAnalyzer> m_fftAnalyzer;
    bool                       m_restoring{false};
};

} // namespace AetherSDR
