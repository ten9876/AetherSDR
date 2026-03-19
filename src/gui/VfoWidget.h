#pragma once

#include <QWidget>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QVector>
#include <QStringList>

class QPushButton;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QSlider;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;

namespace AetherSDR {

class SliceModel;
class TransmitModel;

// Floating VFO info panel attached to the VFO marker on the spectrum display.
// Shows slice info (antennas, frequency, signal level, filter width, TX/SPLIT)
// and tabbed sub-menus (Audio, DSP, Mode, X/RIT, DAX).
// Anchored to the left of the VFO marker; flips right when clipped.
class VfoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VfoWidget(QWidget* parent = nullptr);

    void setSlice(SliceModel* slice);
    void setAntennaList(const QStringList& ants);
    void setTransmitModel(TransmitModel* txModel);
    void setSignalLevel(float dbm);

    // Reposition relative to VFO marker x coordinate.
    void updatePosition(int vfoX, int specTop);

    QPushButton* nr2Button() const { return m_nr2Btn; }
    QPushButton* rn2Button() const { return m_rn2Btn; }

#ifdef HAVE_RADE
    void setRadeActive(bool on);
    void setRadeSynced(bool synced);
    void setRadeSnr(float snrDb);
    void setRadeFreqOffset(float hz);
#endif

Q_SIGNALS:
    void afGainChanged(int value);
    void closeSliceRequested();
    void lockToggled(bool locked);
    void nr2Toggled(bool on);
    void rn2Toggled(bool on);
    void pcAudioToggled(bool on);
#ifdef HAVE_RADE
    void radeActivated(bool on);
#endif

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void wheelEvent(QWheelEvent*) override { /* consume — don't tune VFO */ }
    void mousePressEvent(QMouseEvent* ev) override { ev->accept(); }

private:
    void buildUI();
    void buildTabContent();
    void syncFromSlice();
    void updateTxBadgeStyle(bool isTx);
    void showTab(int index);
    void updateFreqLabel();
    void updateFilterLabel();
    void updateModeTab();
    void rebuildFilterButtons();
    void updateFilterHighlight();
    void applyFilterPreset(int widthHz);
    static QString formatFilterLabel(int hz);

    SliceModel*    m_slice{nullptr};
    TransmitModel* m_txModel{nullptr};
    QStringList    m_antList;
    bool           m_updatingFromModel{false};
    float          m_signalDbm{-130.0f};

    // Header row
    QPushButton* m_rxAntBtn{nullptr};
    QPushButton* m_txAntBtn{nullptr};
    QLabel*      m_filterWidthLbl{nullptr};
    QLabel*      m_splitBadge{nullptr};
    QPushButton* m_txBadge{nullptr};
    QLabel*      m_sliceBadge{nullptr};
    QPushButton* m_lockVfoBtn{nullptr};
    QPushButton* m_closeSliceBtn{nullptr};

    // Frequency / meter
    QLabel* m_freqLabel{nullptr};
    QLineEdit* m_freqEdit{nullptr};
    QStackedWidget* m_freqStack{nullptr};
    QLabel* m_dbmLabel{nullptr};

    // Sub-menu tabs (QLabels with click via event filter)
    QVector<QLabel*> m_tabBtns;
    QStackedWidget* m_tabStack{nullptr};
    QWidget*        m_tabBar{nullptr};
    int m_activeTab{-1};

    // Tab content widgets
    // Audio tab
    QSlider* m_afGainSlider{nullptr};
    QSlider* m_panSlider{nullptr};
    QPushButton* m_muteBtn{nullptr};
    QPushButton* m_pcAudioBtn{nullptr};
    QPushButton* m_sqlBtn{nullptr};
    QSlider* m_sqlSlider{nullptr};
    QComboBox* m_agcCmb{nullptr};
    QSlider* m_agcTSlider{nullptr};
    // DSP tab
    QPushButton* m_nbBtn{nullptr};
    QPushButton* m_nrBtn{nullptr};
    QPushButton* m_nr2Btn{nullptr};
    QPushButton* m_anfBtn{nullptr};
    QPushButton* m_nrlBtn{nullptr};
    QPushButton* m_nrsBtn{nullptr};
    QPushButton* m_rnnBtn{nullptr};
    QPushButton* m_rn2Btn{nullptr};
    QPushButton* m_nrfBtn{nullptr};
    QPushButton* m_anflBtn{nullptr};
    QPushButton* m_anftBtn{nullptr};
    QPushButton* m_apfBtn{nullptr};
    QWidget* m_apfContainer{nullptr};
    QSlider* m_apfSlider{nullptr};
    QLabel*  m_apfValueLbl{nullptr};
    // DSP grid re-layout
    QGridLayout* m_dspGrid{nullptr};
    void relayoutDspGrid();
    // RTTY Mark/Shift (shown only in RTTY mode)
    QWidget* m_rttyContainer{nullptr};
    // DIG offset (shown only in DIGL/DIGU mode)
    QWidget* m_digContainer{nullptr};
    QLabel*  m_digOffsetLabel{nullptr};
    // FM OPT controls (shown only in FM/NFM mode)
    QWidget*       m_fmContainer{nullptr};
    QComboBox*     m_fmToneModeCmb{nullptr};
    QComboBox*     m_fmToneValueCmb{nullptr};
    QDoubleSpinBox* m_fmOffsetSpin{nullptr};
    QPushButton*   m_fmOffsetDown{nullptr};
    QPushButton*   m_fmSimplexBtn{nullptr};
    QPushButton*   m_fmOffsetUp{nullptr};
    QPushButton*   m_fmRevBtn{nullptr};
    QLabel*  m_markLabel{nullptr};
    QLabel*  m_shiftLabel{nullptr};
    // Mode tab
    QComboBox* m_modeCombo{nullptr};
    QGridLayout* m_filterGrid{nullptr};
    QVector<QPushButton*> m_filterBtns;
    QVector<int> m_filterWidths;
    // RIT/XIT tab
    QPushButton* m_ritBtn{nullptr};
    QPushButton* m_xitBtn{nullptr};
    QLabel* m_ritLabel{nullptr};
    QLabel* m_xitLabel{nullptr};
    // DAX tab
    QComboBox* m_daxCmb{nullptr};

#ifdef HAVE_RADE
    QLabel* m_radeStatusLabel{nullptr};
    bool    m_radeActive{false};
#endif

    static constexpr int WIDGET_W = 252;
};

} // namespace AetherSDR
