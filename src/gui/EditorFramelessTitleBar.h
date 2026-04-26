#pragma once

#include <QWidget>

class QLabel;

namespace AetherSDR {

// Reusable 20 px-tall title bar for the PooDoo Audio editor windows
// (parametric EQ, compressor, gate, tube, PUDU, reverb, de-esser).
// Each editor sets Qt::FramelessWindowHint at construction and adds
// this widget at the very top of its layout.  Behaviour:
//
//  - Press-and-drag anywhere on the bar starts a compositor-managed
//    window move via QWindow::startSystemMove() — same pattern as
//    the main window's TitleBar.
//  - Double-click toggles maximize.
//  - The trio at the right (— □ ✕) wires to showMinimized /
//    showMaximized / close on the host window via an installed event
//    filter on each glyph QLabel.
//  - setTitleText() drives the heading on the left so the host editor
//    can flip the label when its Side / Path changes.
class EditorFramelessTitleBar : public QWidget {
public:
    explicit EditorFramelessTitleBar(QWidget* parent = nullptr);

    void setTitleText(const QString& text);

protected:
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QLabel* m_titleLbl{nullptr};
    QLabel* m_minLbl{nullptr};
    QLabel* m_maxLbl{nullptr};
    QLabel* m_closeLbl{nullptr};
};

} // namespace AetherSDR
