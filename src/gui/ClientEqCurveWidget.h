#pragma once

#include <QWidget>
#include <vector>

namespace AetherSDR {

class ClientEq;

// Custom QPainter-rendered view of a ClientEq instance — log-freq grid,
// dB grid, and (in later phases) the summed response curve, per-band
// filled regions, FFT analyzer overlay, and draggable band handles.
//
// This widget is used in two places:
//   - Compact mode inside the docked ClientEqApplet (analyzer + summed curve)
//   - Full-size inside the floating ClientEqEditor (all above + interactions)
//
// Phase B.1: grid only.  Phases B.2+B.3 add the curve, filled regions,
// analyzer, and drag interactions.
class ClientEqCurveWidget : public QWidget {
    Q_OBJECT

public:
    explicit ClientEqCurveWidget(QWidget* parent = nullptr);

    // Null is allowed — widget draws the grid with no response data.
    void setEq(ClientEq* eq);

    // -1 means "nothing selected" — the default in the docked applet view.
    // The editor canvas sets this as the user interacts with handles /
    // icons / param columns so all three UI layers stay in sync.
    void setSelectedBand(int idx);
    int  selectedBand() const { return m_selectedBand; }

    // Show semi-transparent filled regions behind each band's response
    // curve. On by default for the editor canvas; the docked applet view
    // can turn it off if the curve gets too busy at sidebar width.
    void setShowFilledRegions(bool on);

    // Feed the live post-EQ FFT bins to render as a filled analyzer
    // gradient behind the EQ curves. Pass an empty vector to clear.
    // `sampleRate` is the rate the FFT was computed at so the widget
    // can map bins to log-freq x positions.
    void setFftBinsDb(const std::vector<float>& binsDb,
                      double sampleRate);

    // When true, the per-bin peak-hold trace stops decaying — peaks
    // stick at whatever maximum has been observed so far.  Toggling
    // back to false resumes the normal decay.
    void setPeakHoldFrozen(bool frozen);

signals:
    void selectedBandChanged(int idx);
    // Fired whenever band params mutate on the audio side from user
    // interaction in the canvas (drag, double-click-to-create, type
    // cycle via right-click menu, delete). The editor subscribes so it
    // can refresh the icon row + param row text live.
    void bandsChanged();

public:
    // Band palette — 8-step colour wheel across the audio spectrum, with
    // ends grayed for HP/LP slopes. Editor and curve share this so a band
    // keeps the same color in handle, curve, and parameter-row contexts.
    // Index is 0..kMaxBands-1; wraps / interpolates beyond the 8 stops.
    static QColor bandColor(int bandIdx);

protected:
    void paintEvent(QPaintEvent* ev) override;

    // Map Hz <-> x in the drawing rect (log scale, 20 Hz to 20 kHz).
    float freqToX(float hz) const;
    float xToFreq(float x) const;
    // Map dB <-> y in the drawing rect (±18 dB linear).
    float dbToY(float db) const;
    float yToDb(float y) const;

    ClientEq*          m_eq{nullptr};
    int                m_selectedBand{-1};
    bool               m_showFilled{true};
    std::vector<float> m_fftBinsDb;      // empty = no analyzer drawn
    std::vector<float> m_peakHoldDb;     // per-bin peak-hold trail
    bool               m_peakHoldFrozen{false};
    double             m_fftSampleRate{24000.0};
};

} // namespace AetherSDR
