#include "ClientCompKnob.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr float kDragPxPerFullRange = 200.0f;  // 200 px vertical = 0..1
constexpr float kWheelNormStep      = 0.01f;   // 1% per wheel notch
constexpr float kFineMultiplier     = 0.25f;   // Shift-drag scales ×0.25
constexpr float kArcStartDeg        = 225.0f;  // 7:30 clockwise to
constexpr float kArcSpanDeg         = -270.0f; // 4:30  = 270° sweep

const QColor kRingBg     ("#1a2a3a");
const QColor kRingArc    ("#4db8d4");
const QColor kPointer    ("#e8e8e8");
const QColor kLabelColor ("#b0c4d6");
const QColor kValueColor ("#e8e8e8");

} // namespace

ClientCompKnob::ClientCompKnob(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(58, 64);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ClientCompKnob::setLabel(const QString& text)
{
    m_label = text;
    update();
}

void ClientCompKnob::setRange(float minPhysical, float maxPhysical)
{
    m_minPhys = minPhysical;
    m_maxPhys = maxPhysical;
    setValue(m_physical);
}

void ClientCompKnob::setDefault(float physical)
{
    m_defaultPhys = physical;
}

void ClientCompKnob::setValueFromNorm(ValueMap fromNorm)
{
    m_fromNorm = std::move(fromNorm);
    setValue(m_physical);  // re-evaluate with new mapping
}

void ClientCompKnob::setNormFromValue(ValueMap toNorm)
{
    m_toNorm = std::move(toNorm);
    setValue(m_physical);
}

void ClientCompKnob::setLabelFormat(LabelFormat fmt)
{
    m_fmt = std::move(fmt);
    update();
}

void ClientCompKnob::setValue(float physical)
{
    m_physical = std::clamp(physical, m_minPhys, m_maxPhys);
    if (m_toNorm) {
        m_norm = std::clamp(m_toNorm(m_physical), 0.0f, 1.0f);
    } else {
        const float span = std::max(1e-9f, m_maxPhys - m_minPhys);
        m_norm = std::clamp((m_physical - m_minPhys) / span, 0.0f, 1.0f);
    }
    update();
}

void ClientCompKnob::applyNorm(float norm)
{
    m_norm = std::clamp(norm, 0.0f, 1.0f);
    if (m_fromNorm) {
        m_physical = std::clamp(m_fromNorm(m_norm), m_minPhys, m_maxPhys);
    } else {
        m_physical = m_minPhys + m_norm * (m_maxPhys - m_minPhys);
    }
    emit valueChanged(m_physical);
    update();
}

QString ClientCompKnob::formatValue() const
{
    if (m_fmt) return m_fmt(m_physical);
    return QString::number(m_physical, 'f', 2);
}

void ClientCompKnob::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int diameter = std::min(w - 4, h - 24);  // leave room for text rows
    const QRectF ring((w - diameter) * 0.5f, 12.0f, diameter, diameter);

    // Label (above the ring).
    QFont labelFont = p.font();
    labelFont.setPixelSize(9);
    labelFont.setBold(true);
    p.setFont(labelFont);
    p.setPen(kLabelColor);
    p.drawText(QRectF(0, 0, w, 12), Qt::AlignCenter, m_label);

    // Background ring.
    const qreal thick = std::max(2.0, diameter * 0.10);
    QPen bgPen(kRingBg, thick);
    bgPen.setCapStyle(Qt::FlatCap);
    p.setPen(bgPen);
    p.drawArc(ring.adjusted(thick * 0.5, thick * 0.5,
                            -thick * 0.5, -thick * 0.5),
              static_cast<int>(kArcStartDeg * 16.0f),
              static_cast<int>(kArcSpanDeg * 16.0f));

    // Value arc.
    QPen arcPen(kRingArc, thick);
    arcPen.setCapStyle(Qt::FlatCap);
    p.setPen(arcPen);
    p.drawArc(ring.adjusted(thick * 0.5, thick * 0.5,
                            -thick * 0.5, -thick * 0.5),
              static_cast<int>(kArcStartDeg * 16.0f),
              static_cast<int>((kArcSpanDeg * m_norm) * 16.0f));

    // Pointer tick line from the inner edge of the arc to the inner
    // tick radius — visible indicator of exact knob position.
    const float angle = (kArcStartDeg + kArcSpanDeg * m_norm) * (M_PI / 180.0);
    const QPointF c = ring.center();
    const float rOut = diameter * 0.5f - thick * 0.5f;
    const float rIn  = diameter * 0.5f - thick * 1.6f;
    const QPointF pOut(c.x() + rOut * std::cos(angle),
                       c.y() - rOut * std::sin(angle));
    const QPointF pIn (c.x() + rIn  * std::cos(angle),
                       c.y() - rIn  * std::sin(angle));
    QPen pointerPen(kPointer, thick * 0.6);
    pointerPen.setCapStyle(Qt::RoundCap);
    p.setPen(pointerPen);
    p.drawLine(pIn, pOut);

    // Value text beneath the ring.
    QFont valFont = p.font();
    valFont.setPixelSize(9);
    valFont.setBold(false);
    p.setFont(valFont);
    p.setPen(kValueColor);
    p.drawText(QRectF(0, h - 12, w, 12), Qt::AlignCenter, formatValue());
}

void ClientCompKnob::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;
    m_dragging = true;
    m_dragStartY = ev->position().y();
    m_dragStartNorm = m_norm;
    setCursor(Qt::SizeVerCursor);
    ev->accept();
}

void ClientCompKnob::mouseMoveEvent(QMouseEvent* ev)
{
    if (!m_dragging) return;
    const float dy = static_cast<float>(m_dragStartY - ev->position().y());
    const float scale = (ev->modifiers() & Qt::ShiftModifier)
                          ? kFineMultiplier : 1.0f;
    applyNorm(m_dragStartNorm + scale * dy / kDragPxPerFullRange);
    ev->accept();
}

void ClientCompKnob::mouseReleaseEvent(QMouseEvent* ev)
{
    if (!m_dragging) return;
    m_dragging = false;
    setCursor(Qt::ArrowCursor);
    ev->accept();
}

void ClientCompKnob::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;
    setValue(m_defaultPhys);
    emit valueChanged(m_physical);
    ev->accept();
}

void ClientCompKnob::wheelEvent(QWheelEvent* ev)
{
    const int ticks = ev->angleDelta().y() / 120;
    const float scale = (ev->modifiers() & Qt::ShiftModifier)
                          ? kFineMultiplier : 1.0f;
    applyNorm(m_norm + ticks * kWheelNormStep * scale);
    ev->accept();
}

} // namespace AetherSDR
