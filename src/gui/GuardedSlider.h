#pragma once

#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QWheelEvent>

// QSlider subclass that always consumes wheel events, even at min/max
// boundaries. Prevents scroll from propagating to parent widgets (e.g.
// SpectrumWidget tuning the VFO when a slider bottoms out). (#570)
class GuardedSlider : public QSlider {
public:
    using QSlider::QSlider;
    void wheelEvent(QWheelEvent* ev) override {
        QSlider::wheelEvent(ev);
        ev->accept();
    }
};

// QComboBox subclass that always consumes wheel events. Prevents
// accidental mode/antenna/profile changes and event propagation. (#570)
class GuardedComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
    void wheelEvent(QWheelEvent* ev) override {
        QComboBox::wheelEvent(ev);
        ev->accept();
    }
};

// QLabel subclass that emits scrolled(int steps) on wheel events and
// always consumes them. Used for RIT/XIT/pitch numeric displays. (#619)
class ScrollableLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;
    void wheelEvent(QWheelEvent* ev) override {
        int delta = ev->angleDelta().y();
        if (delta > 0) emit scrolled(1);
        else if (delta < 0) emit scrolled(-1);
        ev->accept();
    }
signals:
    void scrolled(int direction);
};
