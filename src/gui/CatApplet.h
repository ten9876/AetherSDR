#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QLineEdit;
class QComboBox;
class QSlider;
class QProgressBar;

namespace AetherSDR {

class RadioModel;
class SliceModel;
class RigctlServer;
class RigctlPty;
class AudioEngine;
class TciServer;

// CAT Applet — 4-channel rigctld TCP + PTY control panel.
// Each channel (A-D) is bound to a slice index (0-3) and provides
// an independent rigctld TCP port + PTY symlink.
class CatApplet : public QWidget {
    Q_OBJECT

public:
    static constexpr int kChannels = 4;

    explicit CatApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);
    void setRigctlServers(RigctlServer** servers, int count);
    void setRigctlPtys(RigctlPty** ptys, int count);
    void setAudioEngine(AudioEngine* audio);
    void setTciServer(TciServer* tci);

    // Sync Enable button state (called by MainWindow on autostart)
    void setTcpEnabled(bool on);
    void setPtyEnabled(bool on);
    void setTciEnabled(bool on);
    void setDaxEnabled(bool on);
    void setDaxRxLevel(int channel, float rms);
    void setDaxTxLevel(float rms);
    void setDaxIqLevel(int channel, float rms);

signals:
    void tciToggled(bool on);
    void daxToggled(bool on);
    void daxRxGainChanged(int channel, float gain);  // 1-4, 0.0–1.0
    void daxTxGainChanged(float gain);
    void iqEnableRequested(int channel);
    void iqDisableRequested(int channel);
    void iqRateChanged(int channel, int rate);

private:
    void buildUI();
    void updateChannelStatus(int ch);
    void updateAllChannelStatus();
    void updateTciStatus();

    RadioModel*    m_model{nullptr};
    RigctlServer*  m_servers[kChannels]{};
    RigctlPty*     m_ptys[kChannels]{};
    AudioEngine*   m_audio{nullptr};
    TciServer*     m_tciServer{nullptr};

    // Global controls
    QPushButton* m_tcpEnable{nullptr};
    QPushButton* m_ptyEnable{nullptr};
    QLineEdit*   m_basePort{nullptr};

    // TCI controls
    QPushButton* m_tciEnable{nullptr};
    QLineEdit*   m_tciPort{nullptr};
    QLabel*      m_tciStatus{nullptr};

    // Per-channel status labels
    struct ChannelRow {
        QLabel* badge{nullptr};      // coloured "A"/"B"/"C"/"D"
        QLabel* tcpStatus{nullptr};  // ":4532 (1 client)" or "(stopped)"
        QLabel* ptyPath{nullptr};    // "/tmp/AetherSDR-CAT-A"
    };
    ChannelRow m_rows[kChannels];

    // DAX section
    QPushButton*  m_daxEnable{nullptr};
    class MeterSlider* m_daxRxMeter[kChannels]{};
    QLabel*       m_daxRxStatus[kChannels]{};
    class MeterSlider* m_daxTxMeter{nullptr};
    QLabel*       m_daxTxStatus{nullptr};

    // DAX IQ section
    QPushButton*  m_iqEnable[kChannels]{};
    QComboBox*    m_iqRateCombo[kChannels]{};
    QProgressBar* m_iqMeter[kChannels]{};
};

} // namespace AetherSDR
