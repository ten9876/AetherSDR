#pragma once
#ifdef HAVE_WEBSOCKETS

#include <QWidget>

class QPushButton;
class QLabel;
class QLineEdit;

namespace AetherSDR {

class TciServer;
class MeterSlider;

// TCI Server Applet — per-channel RX/TX meters with gain sliders,
// port control, and enable toggle. Mirrors the DAX applet's visual
// pattern. Split from the former monolithic CatApplet (DIGI) (#1627).
class TciApplet : public QWidget {
    Q_OBJECT

public:
    static constexpr int kChannels = 4;

    explicit TciApplet(QWidget* parent = nullptr);

    void setTciServer(TciServer* tci);
    void setTciEnabled(bool on);
    void updateTciStatus();

    // Per-channel RX level for meter display
    void setTciRxLevel(int channel, float rms);
    // TX level for meter display
    void setTciTxLevel(float rms);

signals:
    void tciEnabledChanged(bool on);
    void tciRxGainChanged(int channel, float gain);
    void tciTxGainChanged(float gain);

private:
    void buildUI();

    TciServer* m_tciServer{nullptr};

    QPushButton* m_tciEnable{nullptr};
    QLineEdit*   m_tciPort{nullptr};
    QLabel*      m_tciStatus{nullptr};

    MeterSlider* m_rxMeter[kChannels]{};
    MeterSlider* m_txMeter{nullptr};

    float m_smoothedRx[kChannels]{};
};

} // namespace AetherSDR

#endif // HAVE_WEBSOCKETS
