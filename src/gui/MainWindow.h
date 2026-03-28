#pragma once

#include "models/RadioModel.h"
#include "models/BandSettings.h"
#include "models/AntennaGeniusModel.h"
#include "core/RadioDiscovery.h"
#include "core/AudioEngine.h"
#include "core/RigctlServer.h"
#include "core/RigctlPty.h"
#include "core/SmartLinkClient.h"
#include "core/WanConnection.h"
#include "core/CwDecoder.h"
#include "core/DxClusterClient.h"
#ifdef HAVE_SERIALPORT
#include "core/SerialPortController.h"
#endif

#include <QMainWindow>
#include <QSplitter>
#include <QPointer>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>

namespace AetherSDR {

class ConnectionPanel;
class TitleBar;
class SpectrumWidget;
class PanadapterApplet;
class PanadapterStack;
class AppletPanel;
class CwxPanel;
class DvkPanel;
#ifdef HAVE_RADE
class RADEEngine;
#endif
#if defined(Q_OS_MAC)
class VirtualAudioBridge;
using DaxBridge = VirtualAudioBridge;
#elif defined(HAVE_PIPEWIRE)
class PipeWireAudioBridge;
using DaxBridge = PipeWireAudioBridge;
#endif
class VfoWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

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
    void setActiveSlice(int sliceId);
    void updateFilterLimitsForMode(const QString& mode);
    void pushSliceOverlay(SliceModel* s);
    void updateSplitState();
    void disableSplit();
    void wirePanadapter(PanadapterApplet* applet);
    void setActivePanApplet(PanadapterApplet* applet);
    SpectrumWidget* spectrumForSlice(SliceModel* s) const;
    void wireVfoWidget(VfoWidget* w, SliceModel* s);
    void enableNr2WithWisdom();  // Wisdom-gated NR2 enable (shared by VFO + overlay)
    void setupKeyboardShortcuts();
    void updateKeyerAvailability(const QString& mode);
    void applyPanLayout(const QString& layoutId);
    void createPansSequentially(const QString& layoutId, int total,
                                std::shared_ptr<QStringList> panIds, int created);

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
    AntennaGeniusModel m_antennaGenius;
    CwDecoder         m_cwDecoder;
    DxClusterClient   m_dxCluster;
#ifdef HAVE_SERIALPORT
    SerialPortController m_serialPort;
#endif

    // GUI — left sidebar
    ConnectionPanel* m_connPanel{nullptr};

    // GUI — main area
    TitleBar*         m_titleBar{nullptr};
    QSplitter*        m_splitter{nullptr};
    PanadapterStack*  m_panStack{nullptr};
    QPointer<PanadapterApplet> m_panApplet;  // backward compat alias to active applet

    // GUI — right applet panel
    AppletPanel*     m_appletPanel{nullptr};

    // Menus
    QMenu*           m_profilesMenu{nullptr};

    // Audio stream re-creation flag (after profile load)
    bool             m_needAudioStream{false};

    // Pending WAN radio (between requestConnect and connectReady)
    WanRadioInfo     m_pendingWanRadio;

    // Status bar labels (SmartSDR-style)
    QLabel* m_connStatusLabel{nullptr};   // hidden, used for connection state logic
    QLabel* m_addPanLabel{nullptr};
    QLabel* m_tnfIndicator{nullptr};
    QLabel* m_cwxIndicator{nullptr};
    CwxPanel* m_cwxPanel{nullptr};
    DvkPanel* m_dvkPanel{nullptr};
    QLabel* m_dvkIndicator{nullptr};
    QLabel* m_fdxIndicator{nullptr};
    QLabel* m_radioInfoLabel{nullptr};
    QLabel* m_radioVersionLabel{nullptr};
    QLabel* m_stationLabel{nullptr};
    QLabel* m_stationNickLabel{nullptr};
    QLabel* m_gpsLabel{nullptr};
    QLabel* m_gpsStatusLabel{nullptr};
    QLabel* m_gridLabel{nullptr};
    QLabel* m_paTempLabel{nullptr};
    QLabel* m_supplyVoltLabel{nullptr};
    QLabel* m_networkLabel{nullptr};
    QLabel* m_tgxlIndicator{nullptr};
    QLabel* m_pgxlIndicator{nullptr};
    QLabel* m_txIndicator{nullptr};
    QLabel* m_gpsTimeLabel{nullptr};

    // Active slice tracking for multi-slice support
    int m_activeSliceId{-1};
    bool m_splitActive{false};
    int  m_splitRxSliceId{-1};
    int  m_splitTxSliceId{-1};

    // Guard: set true while updating controls from the model, so that
    // onFrequencyChanged doesn't echo the change back to the radio.
    bool m_updatingFromModel{false};
    void toggleConnectionDialog();
    bool m_useSystemClock{true};     // true when no GPS installed
    bool m_userDisconnected{false};  // true after explicit disconnect, blocks auto-connect
    QDialog* m_reconnectDlg{nullptr}; // shown on unexpected disconnect, dismissed on reconnect
    bool m_displaySettingsPushed{false};  // one-shot: push saved display settings after pan created
    bool m_applyingLayout{false};        // true during layout tear-down/recreate — suppresses panadapterAdded handler
    QTimer* m_layoutRestoreTimer{nullptr}; // debounced layout rearrange after pans added on connect
    QTimer* m_heartbeatMissTimer{nullptr}; // fires every 1.5s to detect missed discovery beats
    bool m_keyboardShortcutsEnabled{false}; // global enable for keyboard shortcuts (View menu)

#ifdef HAVE_RADE
    RADEEngine* m_radeEngine{nullptr};
    QThread*    m_radeThread{nullptr};
    int  m_radeSliceId{-1};
    bool m_radePrevMute{false};
    void activateRADE(int sliceId);
    void deactivateRADE();
#endif

#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
    DaxBridge* m_daxBridge{nullptr};
    QString m_savedMicSelection;  // restore on stopDax
    void startDax();
    void stopDax();
#endif
};

} // namespace AetherSDR
