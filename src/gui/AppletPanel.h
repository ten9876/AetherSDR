#pragma once

#include <QWidget>
#include <QStringList>

class QComboBox;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace AetherSDR {

class SliceModel;
class RxApplet;
class SMeterWidget;
class TunerApplet;
class TxApplet;
class PhoneCwApplet;
class PhoneApplet;
class EqApplet;
class CatApplet;
class AntennaGeniusApplet;

// AppletPanel — right-side panel with a row of toggle buttons at the top,
// an S-Meter gauge below them, and a scrollable stack of applets.
// Multiple applets can be visible simultaneously.
class AppletPanel : public QWidget {
    Q_OBJECT

public:
    explicit AppletPanel(QWidget* parent = nullptr);

    void setSlice(SliceModel* slice);
    void setAntennaList(const QStringList& ants);

    RxApplet*     rxApplet()      { return m_rxApplet; }
    SMeterWidget* sMeterWidget()  { return m_sMeter; }
    TunerApplet*  tunerApplet()   { return m_tunerApplet; }
    TxApplet*       txApplet()       { return m_txApplet; }
    PhoneCwApplet*  phoneCwApplet()  { return m_phoneCwApplet; }
    PhoneApplet*    phoneApplet()    { return m_phoneApplet; }
    EqApplet*       eqApplet()       { return m_eqApplet; }
    CatApplet*      catApplet()      { return m_catApplet; }
    AntennaGeniusApplet* agApplet()  { return m_agApplet; }

    // Show/hide the TUNE button and applet based on tuner presence.
    void setTunerVisible(bool visible);

    // Show/hide the AG button and applet based on Antenna Genius presence.
    void setAgVisible(bool visible);

private:
    QWidget*      m_sMeterSection{nullptr};
    SMeterWidget* m_sMeter{nullptr};
    QComboBox*    m_txSelect{nullptr};
    QComboBox*    m_rxSelect{nullptr};
    RxApplet*    m_rxApplet{nullptr};
    TunerApplet* m_tunerApplet{nullptr};
    TxApplet*      m_txApplet{nullptr};
    PhoneCwApplet* m_phoneCwApplet{nullptr};
    PhoneApplet*   m_phoneApplet{nullptr};
    EqApplet*      m_eqApplet{nullptr};
    CatApplet*     m_catApplet{nullptr};
    AntennaGeniusApplet* m_agApplet{nullptr};
    QPushButton* m_tuneBtn{nullptr}; // TUNE toggle button (hidden until TGXL detected)
    QPushButton* m_agBtn{nullptr};   // AG toggle button (hidden until AG discovered)
    QVBoxLayout* m_stack{nullptr};   // layout inside the scroll area
};

} // namespace AetherSDR
