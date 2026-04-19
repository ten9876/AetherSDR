#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>

namespace AetherSDR {

// Vertical peak + peak-hold meter for the compressor editor.  Two
// visual modes are supported via setMode():
//   - Level: fills from the bottom upwards (input / output).  Colour
//     grades green→amber→red as the level approaches 0 dBFS.
//   - GainReduction: fills from the TOP downwards (amber).  A larger
//     negative gainReductionDb pulls the fill further down.
//
// Range is fixed to dBFS [-60, 0] for Level and [-20, 0] for GR.  Bar
// fill is smoothed with the same asymmetric attack/release ballistics
// as HGauge (30ms attack, 180ms release, polled at 120 Hz) so the
// motion reads smoothly instead of twitching on every audio block.
class ClientCompMeter : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Level, GainReduction };

    explicit ClientCompMeter(QWidget* parent = nullptr);

    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    // Feed the latest dB value.  Bar fill smoothly animates toward
    // the new value; peak-hold line still jumps instantly to any
    // higher reading, then decays after 700 ms.
    void setValueDb(float db);

    // Optional label shown above the bar (e.g. "GR", "Out").
    void setLabel(const QString& label);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    // Update m_targetFrac from the current mode + currentDb and start
    // the animation timer if the bar needs to move.
    void recomputeTarget();

    Mode m_mode{Mode::Level};
    QString m_label;

    float m_currentDb{-120.0f};
    float m_peakDb{-120.0f};
    QElapsedTimer m_peakHoldTimer;

    // Smoothed fill animation (same ballistics as HGauge).
    QTimer        m_animTimer;
    QElapsedTimer m_animElapsed;
    float         m_displayFrac{0.0f};
    float         m_targetFrac{0.0f};
};

} // namespace AetherSDR
