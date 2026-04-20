#pragma once

#include "core/AudioEngine.h"

#include <QWidget>
#include <QMap>
#include <QStringList>
#include <QVector>

class QComboBox;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace AetherSDR {
class ContainerManager;
class ContainerWidget;
} // namespace AetherSDR

namespace AetherSDR {

class SliceModel;
class RxApplet;
class SMeterWidget;
class TunerApplet;
class AmpApplet;
class TxApplet;
class PhoneCwApplet;
class PhoneApplet;
class EqApplet;
class ClientEqApplet;
class ClientCompApplet;
class ClientGateApplet;
class ClientDeEssApplet;
class ClientTubeApplet;
class ClientPuduApplet;
class ClientReverbApplet;
class ClientChainApplet;
class CatControlApplet;
class DaxApplet;
class TciApplet;
class DaxIqApplet;
class AntennaGeniusApplet;
class MeterApplet;
class MqttApplet;

// AppletPanel — right-side panel with a row of toggle buttons at the top,
// an S-Meter gauge below them, and a scrollable stack of applets.
// Multiple applets can be visible simultaneously. Applets can be reordered
// by dragging their title bars (QDrag with custom MIME type).
class AppletPanel : public QWidget {
    Q_OBJECT

public:
    explicit AppletPanel(QWidget* parent = nullptr);

    void setSlice(SliceModel* slice);
    void setAntennaList(const QStringList& ants);
    void setMaxSlices(int maxSlices);
    void updateSliceButtons(const QList<SliceModel*>& slices, int activeSliceId);

    RxApplet*     rxApplet()      { return m_rxApplet; }
    SMeterWidget* sMeterWidget()  { return m_sMeter; }
    TunerApplet*  tunerApplet()   { return m_tunerApplet; }
    AmpApplet*    ampApplet()     { return m_ampApplet; }
    TxApplet*       txApplet()       { return m_txApplet; }
    PhoneCwApplet*  phoneCwApplet()  { return m_phoneCwApplet; }
    PhoneApplet*    phoneApplet()    { return m_phoneApplet; }
    EqApplet*       eqApplet()       { return m_eqApplet; }
    ClientEqApplet* clientEqApplet() { return m_clientEqApplet; }
    ClientCompApplet* clientCompApplet() { return m_clientCompApplet; }
    ClientGateApplet* clientGateApplet() { return m_clientGateApplet; }
    ClientDeEssApplet* clientDeEssApplet() { return m_clientDeEssApplet; }
    ClientTubeApplet* clientTubeApplet() { return m_clientTubeApplet; }
    ClientPuduApplet* clientPuduApplet() { return m_clientPuduApplet; }
    ClientReverbApplet* clientReverbApplet() { return m_clientReverbApplet; }
    ClientChainApplet* clientChainApplet() { return m_clientChainApplet; }
    CatControlApplet* catControlApplet() { return m_catControlApplet; }
    DaxApplet*      daxApplet()      { return m_daxApplet; }
    TciApplet*      tciApplet()      { return m_tciApplet; }
    DaxIqApplet*    daxIqApplet()    { return m_daxIqApplet; }
    AntennaGeniusApplet* agApplet()  { return m_agApplet; }
    MeterApplet*  meterApplet()  { return m_meterApplet; }
#ifdef HAVE_MQTT
    MqttApplet*   mqttApplet()   { return m_mqttApplet; }
#endif

    // Show/hide the TUNE button and applet based on tuner presence.
    void setTunerVisible(bool visible);

    // Show/hide the AMP button and applet based on amplifier presence.
    void setAmpVisible(bool visible);

    // Show/hide the AG button and applet based on Antenna Genius presence.
    void setAgVisible(bool visible);

    // Reset applet order to default
    void resetOrder();

    // Show / hide an applet by ID — used to drive visibility from
    // external state (e.g. the CHAIN widget mirrors DSP bypass onto
    // CEQ and CMP tile visibility).  No-op for unknown IDs.
    void setAppletVisible(const QString& id, bool visible);

    // Reorder the TX DSP sub-containers inside the "tx_dsp" parent to
    // mirror the CHAIN's current stage order.  Call whenever the user
    // drags to reorder the chain; the applet tiles follow.
    void setTxDspChainOrder(const QVector<AudioEngine::TxChainStage>& stages);

    // ── Container system (Phase 4a groundwork, #1713) ───────────
    //
    // The panel owns a ContainerManager and a root sidebar container
    // so Phase 5+ code can nest new applets under "sidebar" without
    // waiting for the full AppletEntry → container migration to
    // finish.  The existing m_appletOrder plumbing remains primary
    // for all legacy applets — these accessors exist so new features
    // can opt in to the container system early.
    ContainerManager* containerManager() { return m_containerMgr; }
    ContainerWidget*  rootSidebarContainer() { return m_rootSidebar; }

    // Global controls lock — disables wheel/mouse on sidebar sliders (#745)
    bool controlsLocked() const;
    void setControlsLocked(bool locked);

    // One entry per tile in the reorderable applet stack.  `widget`
    // is always a ContainerWidget; `titleBar` is its ContainerTitleBar
    // (kept as a raw pointer so AppletDropArea can compute drop-indicator
    // positions without touching the container internals).
    struct AppletEntry {
        QString id;
        QWidget* widget{nullptr};
        QWidget* titleBar{nullptr};
        QPushButton* btn{nullptr};
    };

    friend class AppletDropArea;

private:
    void rebuildStackOrder();
    void saveOrder();
    int dropIndexFromY(int localY) const;

    ContainerManager* m_containerMgr{nullptr};
    ContainerWidget*  m_rootSidebar{nullptr};

    // S-Meter sits above the reorderable applet stack (not in
    // m_appletOrder).  Its ContainerWidget lives directly in the
    // sidebar root layout, toggled by m_vuBtn.
    ContainerWidget* m_sMeterContainer{nullptr};
    QPushButton*     m_vuBtn{nullptr};
    SMeterWidget*    m_sMeter{nullptr};
    QComboBox*    m_txSelect{nullptr};
    QComboBox*    m_rxSelect{nullptr};
    RxApplet*    m_rxApplet{nullptr};
    TunerApplet* m_tunerApplet{nullptr};
    AmpApplet*   m_ampApplet{nullptr};
    QPushButton* m_ampBtn{nullptr};
    TxApplet*      m_txApplet{nullptr};
    PhoneCwApplet* m_phoneCwApplet{nullptr};
    PhoneApplet*   m_phoneApplet{nullptr};
    EqApplet*      m_eqApplet{nullptr};
    ClientEqApplet* m_clientEqApplet{nullptr};
    ClientCompApplet* m_clientCompApplet{nullptr};
    ClientGateApplet* m_clientGateApplet{nullptr};
    ClientDeEssApplet* m_clientDeEssApplet{nullptr};
    ClientTubeApplet* m_clientTubeApplet{nullptr};
    ClientPuduApplet* m_clientPuduApplet{nullptr};
    ClientReverbApplet* m_clientReverbApplet{nullptr};
    ClientChainApplet* m_clientChainApplet{nullptr};
    CatControlApplet* m_catControlApplet{nullptr};
    DaxApplet*     m_daxApplet{nullptr};
    TciApplet*     m_tciApplet{nullptr};
    DaxIqApplet*   m_daxIqApplet{nullptr};
    AntennaGeniusApplet* m_agApplet{nullptr};
    MeterApplet* m_meterApplet{nullptr};
#ifdef HAVE_MQTT
    MqttApplet*  m_mqttApplet{nullptr};
#endif
    QPushButton* m_tuneBtn{nullptr};
    QPushButton* m_agBtn{nullptr};
    QVBoxLayout* m_stack{nullptr};
    QScrollArea* m_scrollArea{nullptr};
    QWidget*     m_dropIndicator{nullptr};
    QPushButton* m_lockBtn{nullptr};   // controls-lock toggle (#745)

    // Ordered list of applets (drag-reorderable)
    QVector<AppletEntry> m_appletOrder;
    static const QStringList kDefaultOrder;
};

} // namespace AetherSDR
