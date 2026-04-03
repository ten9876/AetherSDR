#pragma once

#include <QWidget>

class QPushButton;
class QSlider;
class QLabel;
class QMenuBar;
class QHBoxLayout;
class QTimer;

namespace AetherSDR {

class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    // Embed the menu bar into the left side of the title bar
    void setMenuBar(QMenuBar* mb);

    void setPcAudioEnabled(bool on);
    void setMasterVolume(int pct);
    void setHeadphoneVolume(int pct);
    void setOtherClientTx(bool transmitting, const QString& station);
    void setMultiFlexStatus(int clientCount, const QStringList& names);
    void onHeartbeat();     // Call when a discovery packet arrives
    void onHeartbeatLost(); // Call when radio lost from discovery
    void setMinimalMode(bool on);

signals:
    void pcAudioToggled(bool on);
    void masterVolumeChanged(int pct);
    void headphoneVolumeChanged(int pct);
    void lineoutMuteChanged(bool muted);
    void headphoneMuteChanged(bool muted);
    void minimalModeRequested();
    void multiFlexClicked();

private:
    void showFeatureRequestDialog();
    void showFeatureRequestDialogImpl();
    QHBoxLayout* m_hbox{nullptr};
    QMenuBar*    m_menuBar{nullptr};
    QLabel*      m_otherTxLabel{nullptr};
    QPushButton* m_mfBtn{nullptr};
    QPushButton* m_pcBtn{nullptr};
    QPushButton* m_speakerBtn{nullptr};
    QPushButton* m_headphoneBtn{nullptr};
    QSlider*     m_masterSlider{nullptr};
    QSlider*     m_hpSlider{nullptr};
    QLabel*      m_masterLabel{nullptr};
    QLabel*      m_hpLabel{nullptr};

    QPushButton* m_minimalBtn{nullptr};
    QPushButton* m_featureBtn{nullptr};

    // Heartbeat indicator
    QLabel*      m_heartbeat{nullptr};
    QTimer*      m_heartbeatOffTimer{nullptr};  // 100ms green→grey
    QTimer*      m_heartbeatAlarmTimer{nullptr}; // 500ms red/grey blink
    int          m_missedBeats{0};
    bool         m_alarmRed{false};
};

} // namespace AetherSDR
