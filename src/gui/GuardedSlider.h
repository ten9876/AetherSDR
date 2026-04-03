#pragma once

#include <QSlider>
#include <QComboBox>
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
