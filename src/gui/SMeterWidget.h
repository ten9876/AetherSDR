#pragma once

#include <QWidget>
#include <QTimer>

namespace AetherSDR {

// Analog S-Meter gauge widget matching the SmartSDR look.
//
// S-unit scale:
//   S0 = -127 dBm, S1 = -121 dBm, ... S9 = -73 dBm  (6 dB per S-unit)
//   S9+10 = -63 dBm, S9+20 = -53 dBm, S9+40 = -33 dBm, S9+60 = -13 dBm
//
// The needle sweeps from S0 (left) to S9+60 (right) across a 180° arc.
// Below S9 the scale markings are white; above S9 they are red.
class SMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit SMeterWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {280, 140}; }
    QSize minimumSizeHint() const override { return {200, 100}; }

    // Current reading in dBm.
    float levelDbm() const { return m_levelDbm; }

    // Reading as S-units string (e.g. "S7", "S9+20").
    QString sUnitsText() const;

public slots:
    // Update the displayed level.  Applies smoothing internally.
    void setLevel(float dbm);

    // Select meter source: "S-Meter Peak" (default), "Power", "SWR"
    void setMeterSource(const QString& source) { m_source = source; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Map dBm to angle (0.0 = left, 1.0 = right)
    float dbmToFraction(float dbm) const;

    float   m_levelDbm{-127.0f};    // current smoothed reading
    float   m_peakDbm{-127.0f};     // peak hold
    QString m_source{"S-Meter Peak"};

    QTimer  m_peakDecay;
    QTimer  m_peakReset;    // hard reset peak hold every 10 seconds

    // S-unit reference: S0 = -127 dBm, each S-unit = 6 dB
    static constexpr float S0_DBM  = -127.0f;
    static constexpr float S9_DBM  = -73.0f;
    static constexpr float MAX_DBM = -13.0f;  // S9+60
    static constexpr float DB_PER_S = 6.0f;

    static constexpr float SMOOTH_ALPHA = 0.3f;

    // Arc geometry: shallow arc spanning ~70° (like SmartSDR)
    static constexpr float ARC_START_DEG = 55.0f;   // right end (degrees from +X axis)
    static constexpr float ARC_END_DEG   = 125.0f;  // left end
};

} // namespace AetherSDR
