#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

namespace AetherSDR {

class SliceModel;

// Modal dialog with a VFO knob and numeric keypad for touch-friendly
// frequency entry.  Opened by clicking/tapping the frequency display
// in RxApplet or VfoWidget.
class FrequencyEntryDialog : public QDialog {
    Q_OBJECT

public:
    // stepHz: tuning step for the knob (e.g. 100 = 100 Hz)
    explicit FrequencyEntryDialog(SliceModel* slice, int stepHz,
                                  QWidget* parent = nullptr);

private:
    void buildUI();
    void updateDisplay();
    void appendDigit(const QString& d);
    void backspace();
    void applyFrequency();

    SliceModel* m_slice;
    int    m_stepHz;
    double m_liveFreqMhz;   // frequency as tuned by knob (before keypad override)
    QString m_keypadText;    // digits entered via keypad (empty = knob mode)
    bool    m_keypadActive{false};

    QLabel*      m_freqDisplay{nullptr};
    QWidget*     m_knob{nullptr};
    QPoint       m_knobLastPos;
    bool         m_knobDragging{false};
    QPushButton* m_okBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
};

} // namespace AetherSDR
