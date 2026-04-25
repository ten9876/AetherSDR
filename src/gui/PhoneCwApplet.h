#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QSlider;
class QComboBox;
class QStackedWidget;

namespace AetherSDR {

class HGauge;
class TransmitModel;

// P/CW applet — mode-aware panel that shows Phone controls (default) or CW
// controls when the active slice is in CW/CWL mode.  Both sub-panels live
// inside a QStackedWidget beneath a shared "P/CW" title bar.
class PhoneCwApplet : public QWidget {
    Q_OBJECT

public:
    explicit PhoneCwApplet(QWidget* parent = nullptr);

    void setTransmitModel(TransmitModel* model);

signals:
    void micLevelChanged(int level);  // slider value 0-100

public slots:
    // Phone meters (mic level / compression)
    void updateMeters(float micLevel, float compLevel,
                      float micPeak, float compPeak);
    void updateCompression(float compPeak);
    void setCompressionAvailable(bool available);

    // CW meter (ALC 0–100)
    void updateAlc(float alc);

    // Switch between Phone and CW sub-panels based on slice mode.
    void setMode(const QString& mode);

private:
    void buildPhonePanel();
    void buildCwPanel();
    void syncPhoneFromModel();
    void syncCwFromModel();

    TransmitModel* m_model{nullptr};
    QStackedWidget* m_stack{nullptr};
    QWidget* m_phonePanel{nullptr};
    QWidget* m_cwPanel{nullptr};

    // ── Phone sub-panel widgets ──────────────────────────────────────────

    HGauge* m_levelGauge{nullptr};
    HGauge* m_compGauge{nullptr};

    QComboBox* m_micProfileCombo{nullptr};

    QComboBox*   m_micSourceCombo{nullptr};
    QSlider*     m_micLevelSlider{nullptr};
    QLabel*      m_micLevelLabel{nullptr};
    QPushButton* m_accBtn{nullptr};

    QPushButton* m_procBtn{nullptr};
    QSlider*     m_procSlider{nullptr};   // 3-position: 0=NOR, 1=DX, 2=DX+
    QPushButton* m_daxBtn{nullptr};

    QPushButton* m_monBtn{nullptr};
    QSlider*     m_monSlider{nullptr};
    QLabel*      m_monLabel{nullptr};

    // ── CW sub-panel widgets ─────────────────────────────────────────────

    HGauge*      m_alcGauge{nullptr};

    QSlider*     m_delaySlider{nullptr};
    QLabel*      m_delayLabel{nullptr};

    QSlider*     m_speedSlider{nullptr};
    QLabel*      m_speedLabel{nullptr};

    QPushButton* m_sidetoneBtn{nullptr};
    QSlider*     m_sidetoneSlider{nullptr};
    QLabel*      m_sidetoneLabel{nullptr};

    QSlider*     m_cwPanSlider{nullptr};

    QPushButton* m_breakinBtn{nullptr};
    QPushButton* m_iambicBtn{nullptr};

    QLabel*      m_pitchLabel{nullptr};
    QPushButton* m_pitchDown{nullptr};
    QPushButton* m_pitchUp{nullptr};

    // ── Shared state ─────────────────────────────────────────────────────

    bool m_updatingFromModel{false};

    // Client-side peak hold with slow decay for compression gauge
    float m_compHeld{0.0f};
    static constexpr float kCompDecayRate = 0.5f;
};

} // namespace AetherSDR
