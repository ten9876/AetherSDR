#pragma once

#include "models/RadioModel.h"
#include "models/BandSettings.h"
#include "core/RadioDiscovery.h"
#include "core/AudioEngine.h"
#include "core/RigctlServer.h"
#include "core/RigctlPty.h"
#include "core/SmartLinkClient.h"
#include "core/WanConnection.h"

#include <QMainWindow>
#include <QSplitter>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>

namespace AetherSDR {

class ConnectionPanel;
class SpectrumWidget;
class PanadapterApplet;
class AppletPanel;
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Radio/connection events
    void onConnectionStateChanged(bool connected);
    void onConnectionError(const QString& msg);
    void onSliceAdded(SliceModel* slice);
    void onSliceRemoved(int id);

    // Spectrum click-to-tune
    void onFrequencyChanged(double mhz);

private:
    void buildUI();
    void buildMenuBar();
    void applyDarkTheme();
    SliceModel* activeSlice() const;
    SpectrumWidget* spectrum() const;

    BandSnapshot captureCurrentBandState() const;
    void restoreBandState(const BandSnapshot& snap);

    // Core objects
    RadioDiscovery    m_discovery;
    RadioModel        m_radioModel;
    AudioEngine       m_audio;
    BandSettings      m_bandSettings;
    // 4-channel CAT: each channel (A-D) binds to a slice index (0-3)
    static constexpr int kCatChannels = 4;
    RigctlServer*     m_rigctlServers[kCatChannels]{};
    RigctlPty*        m_rigctlPtys[kCatChannels]{};
    SmartLinkClient   m_smartLink;
    WanConnection     m_wanConnection;

    // GUI — left sidebar
    ConnectionPanel* m_connPanel{nullptr};

    // GUI — main area
    QSplitter*        m_splitter{nullptr};
    PanadapterApplet* m_panApplet{nullptr};

    // GUI — right applet panel
    AppletPanel*     m_appletPanel{nullptr};

    // Menus
    QMenu*           m_profilesMenu{nullptr};

    // Audio stream re-creation flag (after profile load)
    bool             m_needAudioStream{false};

    // Pending WAN radio (between requestConnect and connectReady)
    WanRadioInfo     m_pendingWanRadio;

    // Status bar labels
    QLabel* m_connStatusLabel{nullptr};
    QLabel* m_radioInfoLabel{nullptr};
    QLabel* m_gpsTimeLabel{nullptr};
    QLabel* m_networkLabel{nullptr};
    QLabel* m_paTempLabel{nullptr};
    QLabel* m_gpsLabel{nullptr};
    QLabel* m_gridLabel{nullptr};

    // Guard: set true while updating controls from the model, so that
    // onFrequencyChanged doesn't echo the change back to the radio.
    bool m_updatingFromModel{false};
    bool m_userExpandedPanel{false};
    bool m_useSystemClock{true};     // true when no GPS installed
    bool m_userDisconnected{false};  // true after explicit disconnect, blocks auto-connect
    bool m_displaySettingsPushed{false};  // one-shot: push saved display settings after pan created
};

} // namespace AetherSDR
