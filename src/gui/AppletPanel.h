#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>

class QComboBox;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

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

    // Show/hide the TUNE button and applet based on tuner presence.
    void setTunerVisible(bool visible);

    // Show/hide the AMP button and applet based on amplifier presence.
    void setAmpVisible(bool visible);

    // Show/hide the AG button and applet based on Antenna Genius presence.
    void setAgVisible(bool visible);

    // Reset applet order to default
    void resetOrder();

    // Ordered applet entry for drag-reorder support
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

    QWidget*      m_sMeterSection{nullptr};
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
    CatApplet*     m_catApplet{nullptr};
    AntennaGeniusApplet* m_agApplet{nullptr};
    MeterApplet* m_meterApplet{nullptr};
    QPushButton* m_tuneBtn{nullptr};
    QPushButton* m_agBtn{nullptr};
    QVBoxLayout* m_stack{nullptr};
    QScrollArea* m_scrollArea{nullptr};
    QWidget*     m_dropIndicator{nullptr};

    // Ordered list of applets (drag-reorderable)
    QVector<AppletEntry> m_appletOrder;
    static const QStringList kDefaultOrder;
};

} // namespace AetherSDR
