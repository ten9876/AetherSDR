#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QSpinBox;
class QComboBox;
class QSlider;

namespace AetherSDR {

class RadioModel;
class RigctlServer;
class RigctlPty;
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
    void setAudioEngine(AudioEngine* audio);

    // Sync Enable button state (called by MainWindow on autostart)
    void setTcpEnabled(bool on);
    void setPtyEnabled(bool on);

private:
    void buildUI();
    void updateTcpStatus();
    void updatePtyStatus();
    void onConnectionStateChanged(bool connected);

    RadioModel*    m_model{nullptr};
    RigctlServer*  m_server{nullptr};
    RigctlPty*     m_pty{nullptr};
    AudioEngine*   m_audio{nullptr};

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

    // DAX section (placeholder — needs PipeWire virtual devices, issue #15)
    QLabel*      m_daxPlaceholder{nullptr};
};

} // namespace AetherSDR
