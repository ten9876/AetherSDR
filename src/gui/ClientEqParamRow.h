#pragma once

#include "core/ClientEq.h"
#include <QWidget>

class QHBoxLayout;

namespace AetherSDR {

// Bottom-of-editor strip: one column per active band, stacking frequency
// (Hz), gain (dB), and Q as text labels in the band's palette colour.
// Selected band gets a boxed outline around the gain value — the
// Logic-Pro-style "this is what you're tweaking" affordance.
//
// Clicking a column selects that band. No inline editing yet — a future
// PR can promote the labels to click-to-edit.
class ClientEqParamRow : public QWidget {
    Q_OBJECT

public:
    explicit ClientEqParamRow(QWidget* parent = nullptr);

    void setEq(ClientEq* eq);

signals:
    void bandSelected(int idx);

public slots:
    void refresh();           // rebuild columns to match current band count
    void refreshValues();     // update text without re-laying-out (drag path)
    void setSelectedBand(int idx);

private:
    class Column;  // implemented in the .cpp

    void rebuild();

    ClientEq*    m_eq{nullptr};
    QHBoxLayout* m_layout{nullptr};
    int          m_selectedBand{-1};
};

} // namespace AetherSDR
