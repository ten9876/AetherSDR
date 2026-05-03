#include "AetherDspDialog.h"
#include "AetherDspWidget.h"

#include <QLabel>
#include <QVBoxLayout>

namespace AetherSDR {

AetherDspDialog::AetherDspDialog(AudioEngine* audio, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("AetherDSP Settings");
    setMinimumSize(420, 380);
    setStyleSheet("QDialog { background: #0f0f1a; color: #c8d8e8; }");

    auto* root = new QVBoxLayout(this);
    auto* title = new QLabel("AetherDSP Settings");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(
        "QLabel { font-size: 16px; font-weight: bold; color: #c8d8e8; }");
    root->addWidget(title);
    root->addSpacing(4);

    m_widget = new AetherDspWidget(audio, this);
    root->addWidget(m_widget);

    // Forward every parameter-change signal so existing connections to
    // AetherDspDialog::* keep working unchanged.
    connect(m_widget, &AetherDspWidget::nr2GainMaxChanged,
            this,    &AetherDspDialog::nr2GainMaxChanged);
    connect(m_widget, &AetherDspWidget::nr2GainSmoothChanged,
            this,    &AetherDspDialog::nr2GainSmoothChanged);
    connect(m_widget, &AetherDspWidget::nr2QsppChanged,
            this,    &AetherDspDialog::nr2QsppChanged);
    connect(m_widget, &AetherDspWidget::nr2GainMethodChanged,
            this,    &AetherDspDialog::nr2GainMethodChanged);
    connect(m_widget, &AetherDspWidget::nr2NpeMethodChanged,
            this,    &AetherDspDialog::nr2NpeMethodChanged);
    connect(m_widget, &AetherDspWidget::nr2AeFilterChanged,
            this,    &AetherDspDialog::nr2AeFilterChanged);
    connect(m_widget, &AetherDspWidget::mnrEnabledChanged,
            this,    &AetherDspDialog::mnrEnabledChanged);
    connect(m_widget, &AetherDspWidget::mnrStrengthChanged,
            this,    &AetherDspDialog::mnrStrengthChanged);
    connect(m_widget, &AetherDspWidget::dfnrAttenLimitChanged,
            this,    &AetherDspDialog::dfnrAttenLimitChanged);
    connect(m_widget, &AetherDspWidget::dfnrPostFilterBetaChanged,
            this,    &AetherDspDialog::dfnrPostFilterBetaChanged);
    connect(m_widget, &AetherDspWidget::nr4ReductionChanged,
            this,    &AetherDspDialog::nr4ReductionChanged);
    connect(m_widget, &AetherDspWidget::nr4SmoothingChanged,
            this,    &AetherDspDialog::nr4SmoothingChanged);
    connect(m_widget, &AetherDspWidget::nr4WhiteningChanged,
            this,    &AetherDspDialog::nr4WhiteningChanged);
    connect(m_widget, &AetherDspWidget::nr4AdaptiveNoiseChanged,
            this,    &AetherDspDialog::nr4AdaptiveNoiseChanged);
    connect(m_widget, &AetherDspWidget::nr4NoiseMethodChanged,
            this,    &AetherDspDialog::nr4NoiseMethodChanged);
    connect(m_widget, &AetherDspWidget::nr4MaskingDepthChanged,
            this,    &AetherDspDialog::nr4MaskingDepthChanged);
    connect(m_widget, &AetherDspWidget::nr4SuppressionChanged,
            this,    &AetherDspDialog::nr4SuppressionChanged);
}

void AetherDspDialog::syncFromEngine()
{
    if (m_widget) m_widget->syncFromEngine();
}

void AetherDspDialog::selectTab(const QString& name)
{
    if (m_widget) m_widget->selectTab(name);
}

} // namespace AetherSDR
