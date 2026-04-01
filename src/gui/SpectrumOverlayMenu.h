#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <QWheelEvent>
#include <QMouseEvent>

class QPushButton;
class QComboBox;
class QSlider;
class QLabel;

namespace AetherSDR {

class SliceModel;

// Floating overlay menu anchored to the top-left of the SpectrumWidget.
// Open by default; collapses to a single arrow button when closed.
// Buttons are placeholders — signals emitted for parent to wire.
class SpectrumOverlayMenu : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumOverlayMenu(QWidget* parent = nullptr);

    // Raise this widget and all floating panels above sibling widgets.
    void raiseAll();

    // Set the antenna list (from RadioModel::antListChanged).
    void setAntennaList(const QStringList& ants);

    // Sync Display sub-panel controls with saved settings.
    void syncDisplaySettings(int avg, int fps, int fillPct, bool weightedAvg,
                             const QColor& fillColor, int gain, int black,
                             bool autoBlack, int rate,
                             int floorPos = 75, bool floorEnable = false);

    // Set the panadapter ID this overlay belongs to (for +RX routing).
    void setPanId(const QString& id) { m_panId = id; }
    QString panId() const { return m_panId; }

    // Connect/disconnect the ANT panel to a slice model.
    void setSlice(SliceModel* slice);
    void setWnbState(bool on, int level);
    void setRfGain(int gain);

    // Populate XVTR band sub-panel
    struct XvtrBand { QString name; double rfFreqMhz; };
    void setXvtrBands(const QVector<XvtrBand>& bands);
    QPushButton* dspNr2Button() const;
    QPushButton* dspRn2Button() const;
    QPushButton* dspBnrButton() const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override { event->accept(); }
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }

signals:
    void addRxClicked(const QString& panId);
    void addTnfClicked();
    void daxIqChannelChanged(int channel);  // 0=Off, 1-4
    void addPanClicked();
    void daxClicked();
    void nr2Toggled(bool on);
    void rn2Toggled(bool on);
    void bnrToggled(bool on);
    void bnrIntensityChanged(float ratio);
    // Display sub-panel signals
    void fftAverageChanged(int frames);
    void fftFpsChanged(int fps);
    void fftWeightedAverageChanged(bool on);
    void fftFillAlphaChanged(float alpha);
    void fftFillColorChanged(const QColor& color);
    void wfColorGainChanged(int gain);
    void wfBlackLevelChanged(int level);
    void wfAutoBlackChanged(bool on);
    void wfLineDurationChanged(int ms);
    void noiseFloorPositionChanged(int pos);
    void noiseFloorEnableChanged(bool on);
    // Emitted when user selects a band from the sub-panel.
    void bandSelected(const QString& bandName, double freqMhz, const QString& mode);
    // Emitted when WNB toggle changes.
    void wnbToggled(bool on);
    // Emitted when WNB level slider changes (0–100).
    void wnbLevelChanged(int level);
    // Emitted when RF gain slider changes (panadapter-level).
    void rfGainChanged(int gain);
    // NB Waterfall Blanker (#277)
    void wfBlankerEnabledChanged(bool on);
    void wfBlankerThresholdChanged(float threshold);

private:
    QString m_panId;
    void toggle();
    void updateLayout();
    void toggleBandPanel();
    void buildBandPanel();
    void toggleAntPanel();
    void buildAntPanel();
    void toggleDspPanel();
    void buildDspPanel();
    void syncDspPanel();
    void toggleDaxPanel();
    void buildDaxPanel();
    void syncDaxPanel();
    void toggleDisplayPanel();
    void buildDisplayPanel();
    void hideAllSubPanels();
    void syncAntPanel();

    QPushButton* m_toggleBtn{nullptr};
    QVector<QPushButton*> m_menuBtns;
    bool m_expanded{true};

    // Band sub-panel (shown to the right of the menu)
    QWidget* m_bandPanel{nullptr};
    bool m_bandPanelVisible{false};
    QWidget* m_xvtrPanel{nullptr};
    bool m_xvtrPanelVisible{false};

    // ANT sub-panel
    QWidget*     m_antPanel{nullptr};
    bool         m_antPanelVisible{false};
    QComboBox*   m_rxAntCmb{nullptr};
    QSlider*     m_rfGainSlider{nullptr};
    QLabel*      m_rfGainLabel{nullptr};
    QPushButton* m_wnbBtn{nullptr};
    QSlider*     m_wnbSlider{nullptr};
    QLabel*      m_wnbLabel{nullptr};

    // DSP sub-panel
    QWidget* m_dspPanel{nullptr};
    bool     m_dspPanelVisible{false};
    struct DspRow {
        QPushButton* btn{nullptr};
        QSlider*     slider{nullptr};
        QLabel*      valueLbl{nullptr};
    };
    QVector<DspRow> m_dspRows;

    // DAX sub-panel
    QWidget*   m_daxPanel{nullptr};
    bool       m_daxPanelVisible{false};
    QComboBox* m_daxCmb{nullptr};
    QComboBox* m_daxIqCmb{nullptr};

    // Display sub-panel
    QWidget*     m_displayPanel{nullptr};
    bool         m_displayPanelVisible{false};
    QSlider*     m_avgSlider{nullptr};
    QLabel*      m_avgLabel{nullptr};
    QSlider*     m_fpsSlider{nullptr};
    QLabel*      m_fpsLabel{nullptr};
    QSlider*     m_fillSlider{nullptr};
    QLabel*      m_fillLabel{nullptr};
    QPushButton* m_fillColorBtn{nullptr};
    QColor       m_fillColor{0x00, 0xe5, 0xff};  // default cyan
    QPushButton* m_weightedAvgBtn{nullptr};
    QSlider*     m_gainSlider{nullptr};
    QLabel*      m_gainLabel{nullptr};
    QSlider*     m_blackSlider{nullptr};
    QLabel*      m_blackLabel{nullptr};
    QPushButton* m_autoBlackBtn{nullptr};
    QSlider*     m_rateSlider{nullptr};
    QLabel*      m_rateLabel{nullptr};
    // NB Waterfall Blanker (#277)
    QPushButton* m_wfBlankerBtn{nullptr};
    QSlider*     m_wfBlankerThreshSlider{nullptr};
    QLabel*      m_wfBlankerThreshLabel{nullptr};
    QSlider*     m_floorSlider{nullptr};
    QLabel*      m_floorLabel{nullptr};
    QPushButton* m_floorEnableBtn{nullptr};

    QStringList  m_antList;
    SliceModel*  m_slice{nullptr};
    bool         m_updatingFromModel{false};
};

} // namespace AetherSDR
