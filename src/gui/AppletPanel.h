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
class CatApplet;
class AntennaGeniusApplet;
class MeterApplet;
class MqttApplet;

// AppletPanel — right-side panel with an S-Meter gauge at the top and a
// scrollable stack of applets below. Each applet has a clickable title bar
// that collapses/expands its content (accordion style). Multiple applets
// can be visible simultaneously. Applets can be reordered by dragging
// their title bars (QDrag with custom MIME type).
class AppletPanel : public QWidget {
    Q_OBJECT

public:
    explicit AppletPanel(QWidget* parent = nullptr);

    void setSlice(SliceModel* slice);
    void setAntennaList(const QStringList& ants);

    RxApplet*     rxApplet()      { return m_rxApplet; }
    SMeterWidget* sMeterWidget()  { return m_sMeter; }
    TunerApplet*  tunerApplet()   { return m_tunerApplet; }
    AmpApplet*    ampApplet()     { return m_ampApplet; }
    TxApplet*       txApplet()       { return m_txApplet; }
    PhoneCwApplet*  phoneCwApplet()  { return m_phoneCwApplet; }
    PhoneApplet*    phoneApplet()    { return m_phoneApplet; }
    EqApplet*       eqApplet()       { return m_eqApplet; }
    CatApplet*      catApplet()      { return m_catApplet; }
    AntennaGeniusApplet* agApplet()  { return m_agApplet; }
    MeterApplet*  meterApplet()  { return m_meterApplet; }
#ifdef HAVE_MQTT
    MqttApplet*   mqttApplet()   { return m_mqttApplet; }
#endif

    // Show/hide the tuner applet based on tuner presence.
    void setTunerVisible(bool visible);

    // Show/hide the amplifier applet based on amplifier presence.
    void setAmpVisible(bool visible);

    // Show/hide the Antenna Genius applet based on AG presence.
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
        QPushButton* btn{nullptr};     // unused (kept for ABI compat)
        bool floating{false};          // true when applet is in a FloatingAppletWindow
    };

    // Detach an applet into a floating window / re-dock it
    void floatApplet(const QString& id);
    void dockApplet(const QString& id);

    // Returns true if the applet with the given id is currently floating
    bool isAppletFloating(const QString& id) const;

    // Toggle visibility of a floating applet window (used by title bar click)
    void toggleFloatingVisibility(const QString& id);

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
    TxApplet*      m_txApplet{nullptr};
    PhoneCwApplet* m_phoneCwApplet{nullptr};
    PhoneApplet*   m_phoneApplet{nullptr};
    EqApplet*      m_eqApplet{nullptr};
    CatApplet*     m_catApplet{nullptr};
    AntennaGeniusApplet* m_agApplet{nullptr};
    MeterApplet* m_meterApplet{nullptr};
#ifdef HAVE_MQTT
    MqttApplet*  m_mqttApplet{nullptr};
#endif
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
