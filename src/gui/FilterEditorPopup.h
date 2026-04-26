#pragma once

#include <QWidget>

class QSpinBox;

namespace AetherSDR {

// Popup with Lo/Hi Hz spin boxes for direct filter edge entry.
// Shown when the user clicks the filter width label (e.g. "2.7K").
// Auto-dismisses on click outside (Qt::Popup).
class FilterEditorPopup : public QWidget {
    Q_OBJECT

public:
    explicit FilterEditorPopup(QWidget* parent = nullptr);

    // Set initial values before showing.
    void setFilter(int lo, int hi);

    // Show anchored below the given global position.
    void showAt(const QPoint& globalPos);

signals:
    void filterChanged(int lo, int hi);

private:
    QSpinBox* m_loSpin{nullptr};
    QSpinBox* m_hiSpin{nullptr};
};

} // namespace AetherSDR
