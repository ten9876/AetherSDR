#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QTextEdit;

namespace AetherSDR {

class SpectrumWidget;

// Container for a single panadapter display (FFT spectrum + waterfall).
// Adds a title bar with placeholder min/max/close buttons above the
// SpectrumWidget.  Prepares for future multi-slice stacking where each
// slice gets its own PanadapterApplet in a vertical splitter.
class PanadapterApplet : public QWidget {
    Q_OBJECT

public:
    explicit PanadapterApplet(QWidget* parent = nullptr);

    SpectrumWidget* spectrumWidget() const { return m_spectrum; }

    // Panadapter identity (e.g. "0x40000000")
    QString panId() const { return m_panId; }
    void setPanId(const QString& id) { m_panId = id; }

    // Set the slice ID (0=A, 1=B, 2=C, 3=D) shown in the title bar.
    void setSliceId(int id);
    void clearSliceTitle();

    // CW decode panel
    void setMultiPanMode(bool multi);  // show/hide title bar decorations
    void setFloatingState(bool floating);  // switch pop-out ↔ dock icon
    void setCwPanelVisible(bool visible);
    void appendCwText(const QString& text, float cost = 0.0f);
    void setCwStats(float pitchHz, float speedWpm);
    void clearCwText();
    QPushButton* lockPitchButton() const { return m_lockPitchBtn; }
    QPushButton* lockSpeedButton() const { return m_lockSpeedBtn; }

    QSize sizeHint() const override { return {800, 316}; }

signals:
    void activated(const QString& panId);
    void closeRequested(const QString& panId);
    void popOutClicked();
    void dockClicked();
    void pitchRangeChanged(int minHz, int maxHz);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QString m_panId;
    SpectrumWidget* m_spectrum{nullptr};
    QLabel*         m_titleLabel{nullptr};
    QPushButton*    m_popOutBtn{nullptr};
    QPushButton*    m_maxBtn{nullptr};
    QPushButton*    m_closeBtn{nullptr};
    bool            m_isFloating{false};

    // CW decode
    QWidget*   m_cwPanel{nullptr};
    QTextEdit* m_cwText{nullptr};
    QLabel*    m_cwStatsLabel{nullptr};
    QSlider*      m_cwSensSlider{nullptr};
    QPushButton*  m_lockPitchBtn{nullptr};
    QPushButton*  m_lockSpeedBtn{nullptr};
    QSlider*      m_pitchMinSlider{nullptr};
    QSlider*      m_pitchMaxSlider{nullptr};
    QLabel*       m_pitchMinValLabel{nullptr};
    QLabel*       m_pitchMaxValLabel{nullptr};
    float         m_cwCostThreshold{0.70f};
};

} // namespace AetherSDR
