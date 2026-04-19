#pragma once

#include <QWidget>

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

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    // Map Hz <-> x in the drawing rect (log scale, 20 Hz to 20 kHz).
    float freqToX(float hz) const;
    float xToFreq(float x) const;
    // Map dB <-> y in the drawing rect (±18 dB linear).
    float dbToY(float db) const;
    float yToDb(float y) const;

    ClientEq* m_eq{nullptr};
};

} // namespace AetherSDR
