#pragma once

#include <QDialog>
#include <QTabWidget>

class QSlider;
class QLabel;
class QPushButton;
class QRadioButton;
class QCheckBox;
class QButtonGroup;

namespace AetherSDR {

class AudioEngine;

// AetherDSP Settings — modeless dialog for client-side DSP parameters.
// Accessible from Settings menu and "AetherDSP Settings..." in right-click popups.
// All values persist via AppSettings (PascalCase keys).
class AetherDspDialog : public QDialog {
    Q_OBJECT

public:
    explicit AetherDspDialog(AudioEngine* audio, QWidget* parent = nullptr);

    // Sync UI from current AudioEngine state
    void syncFromEngine();

signals:
    // NR2 parameter changes
    void nr2GainMaxChanged(float value);
    void nr2GainSmoothChanged(float value);
    void nr2QsppChanged(float value);
    void nr2GainMethodChanged(int method);
    void nr2NpeMethodChanged(int method);
    void nr2AeFilterChanged(bool on);
    // DFNR parameter changes
    void dfnrAttenLimitChanged(float dB);
    void dfnrPostFilterBetaChanged(float beta);
    // NR4 parameter changes
    void nr4ReductionChanged(float dB);
    void nr4SmoothingChanged(float pct);
    void nr4WhiteningChanged(float pct);
    void nr4AdaptiveNoiseChanged(bool on);
    void nr4NoiseMethodChanged(int method);
    void nr4MaskingDepthChanged(float value);
    void nr4SuppressionChanged(float value);

private:
    void buildNr2Tab(QTabWidget* tabs);
    void buildNr4Tab(QTabWidget* tabs);
    void buildRn2Tab(QTabWidget* tabs);
    void buildBnrTab(QTabWidget* tabs);
    void buildDfnrTab(QTabWidget* tabs);

    AudioEngine* m_audio;

    // NR2 controls
    QButtonGroup* m_nr2GainGroup{nullptr};
    QButtonGroup* m_nr2NpeGroup{nullptr};
    QCheckBox*    m_nr2AeCheck{nullptr};
    QSlider*      m_nr2GainMaxSlider{nullptr};
    QLabel*       m_nr2GainMaxLabel{nullptr};
    QSlider*      m_nr2SmoothSlider{nullptr};
    QLabel*       m_nr2SmoothLabel{nullptr};
    QSlider*      m_nr2QsppSlider{nullptr};
    QLabel*       m_nr2QsppLabel{nullptr};

    // NR4 controls
    QSlider*      m_nr4ReductionSlider{nullptr};
    QLabel*       m_nr4ReductionLabel{nullptr};
    QSlider*      m_nr4SmoothingSlider{nullptr};
    QLabel*       m_nr4SmoothingLabel{nullptr};
    QSlider*      m_nr4WhiteningSlider{nullptr};
    QLabel*       m_nr4WhiteningLabel{nullptr};
    QCheckBox*    m_nr4AdaptiveCheck{nullptr};
    QButtonGroup* m_nr4MethodGroup{nullptr};
    QSlider*      m_nr4MaskingSlider{nullptr};
    QLabel*       m_nr4MaskingLabel{nullptr};
    QSlider*      m_nr4SuppressionSlider{nullptr};
    QLabel*       m_nr4SuppressionLabel{nullptr};

    // DFNR controls
    QSlider*      m_dfnrAttenSlider{nullptr};
    QLabel*       m_dfnrAttenLabel{nullptr};
    QSlider*      m_dfnrBetaSlider{nullptr};
    QLabel*       m_dfnrBetaLabel{nullptr};
};

} // namespace AetherSDR
