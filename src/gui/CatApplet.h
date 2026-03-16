#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QSlider;

namespace AetherSDR {

class RadioModel;
class RigctlServer;
class RigctlPty;
class DaxStreamManager;
class VirtualAudioBridge;
class AudioEngine;

// CAT Applet — settings panel for rigctld TCP server, virtual serial port,
// and DAX audio channel management.
class CatApplet : public QWidget {
    Q_OBJECT

public:
    explicit CatApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);
    void setRigctlServer(RigctlServer* server);
    void setRigctlPty(RigctlPty* pty);
    void setDaxStreamManager(DaxStreamManager* dax);
    void setVirtualAudioBridge(VirtualAudioBridge* bridge);
    void setAudioEngine(AudioEngine* audio);

private:
    void buildUI();
    void updateTcpStatus();
    void updatePtyStatus();
    void onConnectionStateChanged(bool connected);

    RadioModel*         m_model{nullptr};
    RigctlServer*       m_server{nullptr};
    RigctlPty*          m_pty{nullptr};
    DaxStreamManager*   m_dax{nullptr};
    VirtualAudioBridge* m_bridge{nullptr};
    AudioEngine*        m_audio{nullptr};

    // TCP section
    QPushButton* m_tcpEnable{nullptr};
    QSpinBox*    m_tcpPort{nullptr};
    QLabel*      m_tcpStatus{nullptr};

    // PTY section
    QPushButton* m_ptyEnable{nullptr};
    QLabel*      m_ptyPath{nullptr};
    QPushButton* m_ptyCopy{nullptr};

    // Slice selector
    QComboBox*   m_sliceSelect{nullptr};

    // DAX section
    QPushButton* m_daxEnable{nullptr};
    QSlider*     m_daxGain{nullptr};
    QLabel*      m_daxGainLabel{nullptr};
    QSlider*     m_daxTxGain{nullptr};
    QLabel*      m_daxTxGainLabel{nullptr};
    QLabel*      m_daxLabels[4]{};
};

} // namespace AetherSDR
