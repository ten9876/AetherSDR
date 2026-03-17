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

    // Sync Enable button state (called by MainWindow on autostart)
    void setTcpEnabled(bool on);
    void setPtyEnabled(bool on);

private:
    void buildUI();
    void updateChannelStatus(int ch);
    void updateAllChannelStatus();

    RadioModel*    m_model{nullptr};
    RigctlServer*  m_servers[kChannels]{};
    RigctlPty*     m_ptys[kChannels]{};
    AudioEngine*   m_audio{nullptr};

    // Global controls
    QPushButton* m_tcpEnable{nullptr};
    QPushButton* m_ptyEnable{nullptr};
    QLineEdit*   m_basePort{nullptr};

    // Per-channel status labels
    struct ChannelRow {
        QLabel* badge{nullptr};      // coloured "A"/"B"/"C"/"D"
        QLabel* tcpStatus{nullptr};  // ":4532 (1 client)" or "(stopped)"
        QLabel* ptyPath{nullptr};    // "/tmp/AetherSDR-CAT-A"
    };
    ChannelRow m_rows[kChannels];

    // DAX section (unchanged from upstream)
    QPushButton*  m_daxEnable{nullptr};
    QProgressBar* m_daxRxLevel[kChannels]{};
    QLabel*       m_daxRxStatus[kChannels]{};
    QProgressBar* m_daxTxLevel{nullptr};
    QLabel*       m_daxTxStatus{nullptr};
};

} // namespace AetherSDR
