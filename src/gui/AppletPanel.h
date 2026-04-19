#pragma once

#include <QWidget>
#include <QMap>
#include <QStringList>
#include <QVector>

class QComboBox;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace AetherSDR { class FloatingAppletWindow; }

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

    // Global controls lock — disables wheel/mouse on sidebar sliders (#745)
    bool controlsLocked() const;
    void setControlsLocked(bool locked);

    // Ordered applet entry for drag-reorder and float support
    struct AppletEntry {
        QString id;
        QWidget* widget{nullptr};      // wrapper widget (titleBar + applet)
        QWidget* titleBar{nullptr};    // draggable AppletTitleBar
        QPushButton* btn{nullptr};     // toggle button in button row
        bool floating{false};          // true when applet is in a FloatingAppletWindow
    };

    // Detach an applet into a floating window / re-dock it
    void floatApplet(const QString& id);
    void dockApplet(const QString& id);

    // Returns true if the applet with the given id is currently floating
    bool isAppletFloating(const QString& id) const;

    friend class AppletDropArea;

private:
    void rebuildStackOrder();
    void saveOrder();
    int dropIndexFromY(int localY) const;

    // Float/dock the S-Meter (VU) section — not in m_appletOrder so handled separately.
    void floatSMeter();
    void dockSMeter();

    QWidget*      m_sMeterSection{nullptr};
    QWidget*      m_sMeterContent{nullptr};  // floatable content (smeter + controls)
    SMeterWidget* m_sMeter{nullptr};
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

    // Floating windows keyed by applet ID
    QMap<QString, FloatingAppletWindow*> m_floatingWindows;
};

} // namespace AetherSDR
