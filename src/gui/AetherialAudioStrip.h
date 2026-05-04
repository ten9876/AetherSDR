#pragma once

#include "ClientEqApplet.h"   // ClientEqApplet::Path enum
#include "core/AudioEngine.h" // AudioEngine::TxChainStage in signal sig

#include <QWidget>

class QPushButton;
class QComboBox;

namespace AetherSDR {

class AudioEngine;
class ChannelStripPresets;
class StripChainWidget;
class EditorFramelessTitleBar;
class StripTubePanel;
class StripGatePanel;
class StripEqPanel;
class StripCompPanel;
class StripDeEssPanel;
class StripPuduPanel;
class StripReverbPanel;
class StripWaveformPanel;
class StripFinalOutputPanel;

// Aetherial Audio Channel Strip — unified TX DSP window.
//
// First-iteration plumbing for issue #2301.  Toplevel `Qt::Window`
// that embeds all 7 client-side TX DSP stage panels in a single
// view, with a horizontal `ClientChainWidget` at the top for chain
// ordering / bypass.  Per-stage editors and applets continue to
// work alongside this window during iteration; step 6 of the plan
// removes them.
//
// Geometry persists via AppSettings("AetherialStripGeometry").
// Visibility persists via AppSettings("AetherialStripVisible") so
// the strip reopens at last position on startup.
class AetherialAudioStrip : public QWidget {
    Q_OBJECT

public:
    explicit AetherialAudioStrip(AudioEngine* engine, QWidget* parent = nullptr);
    ~AetherialAudioStrip() override;

    // Forward radio TX filter cutoffs to the embedded EQ canvas so the
    // dashed yellow filter-edge guide lines render here too.  MainWindow
    // calls this from its txFilterCutoffChanged subscription, the same
    // way it does for the floating ClientEqEditor.
    void setTxFilterCutoffs(int lowHz, int highHz);

    // Mirrored from ClientChainApplet so the strip's own record / play
    // buttons share the PUDU monitor in MainWindow.
    void setMonitorRecording(bool on);
    void setMonitorPlaying(bool on);
    void setMonitorHasRecording(bool has);


    // MIC endpoint goes green when PC mic is selected and DAX is off
    // (i.e. PooDoo is actually in the TX signal path).  TX endpoint
    // pulses red while the user is transmitting on their own slice.
    // Both forward to the embedded StripChainWidget.
    void setMicInputReady(bool ready);
    void setTxActive(bool active);

    // Repaint the embedded StripChainWidget — used by MainWindow when
    // the docked Chain applet toggles a stage so the strip's tile
    // visuals stay in sync.  Engine state is the source of truth; this
    // just nudges the widget to repaint from it.
    void refreshChainPaint();

    // Accessor for the embedded Final Output panel — MainWindow wires
    // this to TransmitModel::quindarActiveChanged so the QUIN chip
    // flashes via signal hop instead of a poll.
    StripFinalOutputPanel* finalOutputPanel() const { return m_finalOutput; }

signals:
    // Re-emitted from the embedded StripEqPanel when the user drags one
    // of the dashed cutoff lines.  MainWindow connects this to the same
    // handler as ClientEqEditor::cutoffsDragRequested so dragging in the
    // strip writes the same TX filter command to the radio.
    void cutoffsDragRequested(ClientEqApplet::Path path,
                              int audioLowHz, int audioHighHz);

    // Record / playback button clicks — same semantics as
    // ClientChainApplet::monitorRecordClicked / monitorPlayClicked.
    void monitorRecordClicked();
    void monitorPlayClicked();


    // Re-emitted from the embedded StripChainWidget when the user
    // single-clicks a stage tile to toggle its bypass.  MainWindow
    // routes this to the same handler as ClientChainApplet's signal
    // so the docked Chain applet's chain widget repaints in lock-step.
    void stageEnabledChanged(AudioEngine::TxChainStage stage, bool enabled);

protected:
    void closeEvent(QCloseEvent* ev) override;
    void moveEvent(QMoveEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;
    // Frameless 8-axis resize: 4 edges + 4 corners.  Mouse hover on
    // the bare margin around the embedded grid updates the cursor;
    // press starts a compositor-managed resize via startSystemResize.
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void saveGeometryToSettings();
    void restoreGeometryFromSettings();
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateResizeCursor(const QPoint& pos);

    // Master bypass — snapshot all enabled TX stages, then disable
    // them.  Restores the snapshot on uncheck.  Mirrors the docked
    // ClientChainApplet's BYPASS button.
    void onBypassToggled(bool checked);

    // Preset combo helpers.
    void rebuildPresetCombo(const QString& selectName = QString());
    void onPresetComboActivated(int idx);
    void doImportPreset();
    void doExportPreset();
    void doExportLibrary();
    void doSavePreset();
    void doDeletePreset();
    void updatePresetButtonEnable();
    // After a preset has been applied to the engine, push fresh values
    // into every embedded panel's UI so labels / knobs / combos stop
    // showing the previous preset's data.
    void refreshAllPanelsFromEngine();

    AudioEngine*         m_audio{nullptr};
    ChannelStripPresets* m_presets{nullptr};
    QWidget*             m_titleBar{nullptr};   // custom inline ContainerTitleBar-styled bar
    StripChainWidget*    m_chain{nullptr};
    QPushButton*         m_txBtn{nullptr};
    QPushButton*         m_rxBtn{nullptr};
    QPushButton*         m_bypassBtn{nullptr};
    QPushButton*         m_monRecBtn{nullptr};
    QPushButton*         m_monPlayBtn{nullptr};
    QComboBox*           m_presetCombo{nullptr};
    QPushButton*         m_presetSaveBtn{nullptr};
    QPushButton*         m_presetDeleteBtn{nullptr};
    QString              m_currentPresetName;
    bool                 m_buildingCombo{false};
    bool               m_monRecording{false};
    bool               m_monPlaying{false};
    bool               m_monHasRecording{false};
    StripTubePanel*    m_tube{nullptr};
    StripGatePanel*    m_gate{nullptr};
    StripEqPanel*      m_eq{nullptr};
    StripCompPanel*    m_comp{nullptr};
    StripDeEssPanel*   m_dess{nullptr};
    StripPuduPanel*    m_pudu{nullptr};
    StripReverbPanel*  m_reverb{nullptr};
    StripWaveformPanel*    m_waveform{nullptr};
    StripFinalOutputPanel* m_finalOutput{nullptr};
    bool               m_restoring{false};
};

} // namespace AetherSDR
