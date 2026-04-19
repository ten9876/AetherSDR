#include "ClientCompEditorCanvas.h"
#include "core/ClientComp.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr float kHandleHitRadius = 10.0f;

const QColor kThresholdHandle("#e8a540");
const QColor kRatioHandle    ("#4db8d4");
const QColor kHandleOutline  ("#ffffff");

} // namespace

ClientCompEditorCanvas::ClientCompEditorCanvas(QWidget* parent)
    : ClientCompCurveWidget(parent)
{
    setMinimumHeight(220);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

bool ClientCompEditorCanvas::thresholdHandleHit(const QPointF& pos) const
{
    if (!m_comp) return false;
    const float T = m_comp->thresholdDb();
    // Threshold handle lives on the bottom edge at x=dbToX(T).
    const QPointF h(dbToX(T), rect().bottom() - 6.0);
    const float dx = static_cast<float>(pos.x() - h.x());
    const float dy = static_cast<float>(pos.y() - h.y());
    return (dx * dx + dy * dy) < kHandleHitRadius * kHandleHitRadius;
}

bool ClientCompEditorCanvas::ratioHandleHit(const QPointF& pos) const
{
    if (!m_comp) return false;
    const float T = m_comp->thresholdDb();
    const float outDb = curveOutputDb(T);
    const QPointF h(dbToX(T), dbToY(outDb));
    const float dx = static_cast<float>(pos.x() - h.x());
    const float dy = static_cast<float>(pos.y() - h.y());
    return (dx * dx + dy * dy) < kHandleHitRadius * kHandleHitRadius;
}

void ClientCompEditorCanvas::paintEvent(QPaintEvent* ev)
{
    ClientCompCurveWidget::paintEvent(ev);
    if (!m_comp) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Threshold handle — small filled triangle hanging off the bottom
    // of the grid at the current threshold x.  Easy target for the
    // mouse and doesn't compete visually with the curve dot.
    const float T    = m_comp->thresholdDb();
    const float tx   = dbToX(T);
    const float by   = rect().bottom() - 4.0f;
    QPainterPath tri;
    tri.moveTo(tx,         by);
    tri.lineTo(tx - 5.5f, by + 8.0f);
    tri.lineTo(tx + 5.5f, by + 8.0f);
    tri.closeSubpath();
    p.setBrush(kThresholdHandle);
    p.setPen(QPen(kHandleOutline, 1.0));
    p.drawPath(tri);

    // Ratio handle — larger filled dot at the knee centre so it reads
    // as "drag me to change the slope."  White outline so it stays
    // visible against the cyan curve even when GR is 0.
    const float outDb = curveOutputDb(T);
    const QPointF rh(tx, dbToY(outDb));
    p.setBrush(kRatioHandle);
    p.setPen(QPen(kHandleOutline, 1.2));
    p.drawEllipse(rh, 6.5, 6.5);
}

void ClientCompEditorCanvas::mousePressEvent(QMouseEvent* ev)
{
    if (!m_comp || ev->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(ev);
        return;
    }
    if (ratioHandleHit(ev->position())) {
        m_drag = Drag::Ratio;
        m_dragStart = ev->position();
        m_dragStartValue = m_comp->ratio();
        setCursor(Qt::SizeVerCursor);
        ev->accept();
        return;
    }
    if (thresholdHandleHit(ev->position())) {
        m_drag = Drag::Threshold;
        m_dragStart = ev->position();
        m_dragStartValue = m_comp->thresholdDb();
        setCursor(Qt::SizeHorCursor);
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void ClientCompEditorCanvas::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_drag == Drag::None || !m_comp) {
        QWidget::mouseMoveEvent(ev);
        return;
    }

    if (m_drag == Drag::Threshold) {
        const float t = std::clamp(xToDb(static_cast<float>(ev->position().x())),
                                   kMinDb, kMaxDb);
        emit thresholdChanged(t);
        update();
    } else if (m_drag == Drag::Ratio) {
        // Vertical pixels → ratio.  Dragging up increases ratio
        // (steeper compression / limiter behaviour), dragging down
        // decreases it toward 1:1.  Exponential feels best — a small
        // motion near 1:1 stays subtle and a big motion near the top
        // pushes into limiter territory.
        const float dy = static_cast<float>(m_dragStart.y() - ev->position().y());
        const float scale = (ev->modifiers() & Qt::ShiftModifier) ? 0.25f : 1.0f;
        const float factor = std::pow(4.0f, scale * dy / 200.0f);
        const float next = std::clamp(m_dragStartValue * factor, 1.0f, 20.0f);
        emit ratioChanged(next);
        update();
    }
    ev->accept();
}

void ClientCompEditorCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_drag == Drag::None) {
        QWidget::mouseReleaseEvent(ev);
        return;
    }
    m_drag = Drag::None;
    setCursor(Qt::CrossCursor);
    ev->accept();
}

} // namespace AetherSDR
