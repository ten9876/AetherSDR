#pragma once

#include <QDialog>

class QEvent;
class QMouseEvent;

namespace AetherSDR {

class AudioEngine;
class AetherDspWidget;

// AetherDSP Settings — modeless dialog wrapping AetherDspWidget.
// Accessible from Settings menu and "AetherDSP Settings..." in right-click
// popups.  All values persist via AppSettings (PascalCase keys).  The same
// widget body is also embedded in ClientRxDspApplet for in-chain access.
class AetherDspDialog : public QDialog {
    Q_OBJECT

public:
    explicit AetherDspDialog(AudioEngine* audio, QWidget* parent = nullptr);

    // Sync UI from current AudioEngine state.
    void syncFromEngine();

    // Jump to a named tab (e.g. "MNR", "NR2", "DFNR").
    void selectTab(const QString& name);

    // The underlying tabbed widget — connect to its signals for parameter
    // change notifications.  Re-emitted on this dialog as well so existing
    // callers can keep connecting to the dialog directly.
    AetherDspWidget* widget() const { return m_widget; }

signals:
    // NR2 parameter changes (forwarded from m_widget)
    void nr2GainMaxChanged(float value);
    void nr2GainSmoothChanged(float value);
    void nr2QsppChanged(float value);
    void nr2GainMethodChanged(int method);
    void nr2NpeMethodChanged(int method);
    void nr2AeFilterChanged(bool on);
    // MNR parameter changes
    void mnrEnabledChanged(bool on);
    void mnrStrengthChanged(float value);
    // DFNR parameter changes
    void dfnrAttenLimitChanged(float dB);
    void dfnrPostFilterBetaChanged(float beta);
    // NR4 parameter changes
    void nr4ReductionChanged(float dB);
    void nr4SmoothingChanged(float pct);
    void nr4WhiteningChanged(float pct);
    void nr4AdaptiveNoiseChanged(bool on);
    void nr4NoiseMethodChanged(int method);
    void nr4MaskingDepthChanged(float value);
    void nr4SuppressionChanged(float value);

protected:
    // Frameless 8-axis resize + drag-to-move (same pattern as
    // NetworkDiagnosticsDialog and the AetherialAudioStrip).
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateResizeCursor(const QPoint& pos);

    AetherDspWidget* m_widget{nullptr};
    QWidget*         m_titleBar{nullptr};
};

} // namespace AetherSDR
