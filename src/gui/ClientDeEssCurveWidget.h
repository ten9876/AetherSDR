#pragma once

#include <QWidget>

class QTimer;

namespace AetherSDR {

class ClientDeEss;

// Compact sidechain response display for the docked de-esser tile.
// Draws the biquad bandpass magnitude curve across 100 Hz .. 12 kHz
// (log-frequency), with a vertical marker at the threshold level on
// a dB scale, and a live dot tracking the sidechain envelope so the
// user can see when the de-esser is triggering.
//
// Thread model: ClientDeEss parameter + meter reads are already
// atomic internally, so paintEvent + polling QTimer are UI-thread
// safe without extra locking.
class ClientDeEssCurveWidget : public QWidget {
    Q_OBJECT

public:
    explicit ClientDeEssCurveWidget(QWidget* parent = nullptr);

    void setDeEss(ClientDeEss* d);
    ClientDeEss* deEss() const { return m_deEss; }

    void setCompactMode(bool on);

    // Frequency range drawn on the X axis (log scale).
    static constexpr float kMinHz = 100.0f;
    static constexpr float kMaxHz = 12000.0f;
    // dB range on the Y axis for the magnitude plot.
    static constexpr float kMinDb = -40.0f;
    static constexpr float kMaxDb =  12.0f;

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    float hzToX(float hz) const;
    float dbToY(float db) const;
    // RBJ bandpass magnitude at `hz` using the current filter params.
    float bandpassMagDb(float hz) const;

    ClientDeEss* m_deEss{nullptr};
    QTimer*      m_pollTimer{nullptr};
    bool         m_compact{false};
    float        m_lastScDb{-120.0f};   // smoothed sidechain ball dB
};

} // namespace AetherSDR
