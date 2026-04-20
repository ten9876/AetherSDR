#pragma once

#include <QDialog>

class QSpinBox;
class QDialogButtonBox;

namespace AetherSDR {

class FilterPassbandWidget;

// Dialog for entering asymmetric (independent lo/hi) filter values.
// Shows two spin boxes for Low Cut and High Cut in Hz, plus a live
// passband preview widget.
class CustomFilterDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomFilterDialog(int lo, int hi, const QString& mode,
                                QWidget* parent = nullptr);

    int filterLow() const;
    int filterHigh() const;

private:
    void updatePreview();

    QSpinBox* m_loSpin{nullptr};
    QSpinBox* m_hiSpin{nullptr};
    FilterPassbandWidget* m_preview{nullptr};
    QDialogButtonBox* m_buttons{nullptr};
};

} // namespace AetherSDR
