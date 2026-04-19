#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QLineEdit;

namespace AetherSDR {

class RadioModel;
class RigctlServer;
class RigctlPty;

// CAT Control Applet — 4-channel rigctld TCP + PTY control panel.
// Split from the former monolithic CatApplet (DIGI) (#1627).
class CatControlApplet : public QWidget {
    Q_OBJECT

public:
    static constexpr int kChannels = 4;

    explicit CatControlApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);
    void setRigctlServers(RigctlServer** servers, int count);
    void setRigctlPtys(RigctlPty** ptys, int count);

    void setTcpEnabled(bool on);
    void setPtyEnabled(bool on);

private:
    void buildUI();
    void updateChannelStatus(int ch);
    void updateAllChannelStatus();

    RadioModel*    m_model{nullptr};
    RigctlServer*  m_servers[kChannels]{};
    RigctlPty*     m_ptys[kChannels]{};

    QPushButton* m_tcpEnable{nullptr};
    QPushButton* m_ptyEnable{nullptr};
    QLineEdit*   m_basePort{nullptr};

    struct ChannelRow {
        QLabel* badge{nullptr};
        QLabel* tcpStatus{nullptr};
        QLabel* ptyPath{nullptr};
    };
    ChannelRow m_rows[kChannels];
};

} // namespace AetherSDR
