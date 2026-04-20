#include "CustomFilterDialog.h"
#include "FilterPassbandWidget.h"

#include <QSpinBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>

namespace AetherSDR {

CustomFilterDialog::CustomFilterDialog(int lo, int hi, const QString& mode,
                                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Custom Asymmetric Filter");
    setMinimumWidth(280);

    auto* layout = new QVBoxLayout(this);

    // Spin boxes for lo/hi cut
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_loSpin = new QSpinBox;
    m_loSpin->setRange(-20000, 0);
    m_loSpin->setSuffix(" Hz");
    m_loSpin->setSingleStep(10);
    m_loSpin->setValue(lo);

    m_hiSpin = new QSpinBox;
    m_hiSpin->setRange(0, 20000);
    m_hiSpin->setSuffix(" Hz");
    m_hiSpin->setSingleStep(10);
    m_hiSpin->setValue(hi);

    form->addRow("Low Cut:", m_loSpin);
    form->addRow("High Cut:", m_hiSpin);
    layout->addLayout(form);

    // Passband preview
    m_preview = new FilterPassbandWidget;
    m_preview->setFilter(lo, hi);
    m_preview->setMode(mode);
    m_preview->setFixedHeight(50);
    // Disable drag interaction — preview only
    m_preview->setEnabled(false);
    layout->addWidget(m_preview);

    // Result label showing total bandwidth
    auto* resultLabel = new QLabel;
    auto updateResult = [this, resultLabel]() {
        int total = m_hiSpin->value() - m_loSpin->value();
        QString loStr, hiStr;
        int loVal = m_loSpin->value();
        int hiVal = m_hiSpin->value();
        if (std::abs(loVal) >= 1000)
            loStr = QString::number(loVal / 1000.0, 'f', 1) + "K";
        else
            loStr = QString::number(loVal);
        if (hiVal >= 1000)
            hiStr = QString("+" ) + QString::number(hiVal / 1000.0, 'f', 1) + "K";
        else
            hiStr = QString("+") + QString::number(hiVal);

        QString totalStr;
        if (total >= 1000)
            totalStr = QString::number(total / 1000.0, 'f', 1) + "K";
        else
            totalStr = QString::number(total);
        resultLabel->setText(QString("Bandwidth: %1 (%2 / %3)").arg(totalStr, loStr, hiStr));
    };
    updateResult();
    layout->addWidget(resultLabel);

    connect(m_loSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, updateResult]() {
        updatePreview();
        updateResult();
    });
    connect(m_hiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, updateResult]() {
        updatePreview();
        updateResult();
    });

    // Dialog buttons
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
}

int CustomFilterDialog::filterLow() const { return m_loSpin->value(); }
int CustomFilterDialog::filterHigh() const { return m_hiSpin->value(); }

void CustomFilterDialog::updatePreview()
{
    m_preview->setFilter(m_loSpin->value(), m_hiSpin->value());
}

} // namespace AetherSDR
