#include "FilterPassbandWidget.h"

#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

FilterPassbandWidget::FilterPassbandWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::SizeAllCursor);
}

void FilterPassbandWidget::setFilter(int lo, int hi)
{
    if (lo == m_lo && hi == m_hi) return;
    m_lo = lo;
    m_hi = hi;
    update();
}

void FilterPassbandWidget::setMode(const QString& mode)
{
    if (mode == m_mode) return;
    m_mode = mode;
    update();
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void FilterPassbandWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();

    // Background
    p.fillRect(rect(), QColor(0x0a, 0x0a, 0x18));

    // Border
    p.setPen(QColor(0x20, 0x30, 0x40));
    p.drawRect(0, 0, w - 1, h - 1);

    // ── Static trapezoid shape ──────────────────────────────────────────
    // Fixed geometry — shape doesn't change with filter width
    const int margin = 16;
    const int topY = 14;
    const int botY = h - 16;  // room for labels
    const int loX = margin;
    const int hiX = w - margin;
    constexpr int SKIRT = 16;

    // Filter shape: left skirt, flat top, right skirt (no bottom, no fill)
    p.setPen(QPen(QColor(0x00, 0xb4, 0xd8), 1.5));
    p.drawLine(loX, botY, loX + SKIRT, topY);          // left skirt
    p.drawLine(loX + SKIRT, topY, hiX - SKIRT, topY);  // flat top
    p.drawLine(hiX - SKIRT, topY, hiX, botY);           // right skirt

    // Dashed vertical lines at filter edges (8px inside the skirt vertices)
    p.setPen(QPen(QColor(0x00, 0xb4, 0xd8, 120), 1, Qt::DashLine));
    p.drawLine(loX + SKIRT + 8, 2, loX + SKIRT + 8, h - 2);
    p.drawLine(hiX - SKIRT - 8, 2, hiX - SKIRT - 8, h - 2);

    // ── Labels ──────────────────────────────────────────────────────────
    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    const QFontMetrics fm(font);

    // Bandwidth label (centered at bottom)
    int bw = std::abs(m_hi - m_lo);
    QString bwText = bw >= 1000 ? QString("%1.%2K").arg(bw / 1000).arg((bw % 1000) / 100)
                                : QString::number(bw);
    p.setPen(QColor(0xc8, 0xd8, 0xe8));
    p.drawText((loX + hiX) / 2 - fm.horizontalAdvance(bwText) / 2, botY + 12, bwText);

    // Passband center offset (distance from carrier to filter center, below top line)
    int center = (m_lo + m_hi) / 2;
    QString centerText = std::abs(center) >= 1000
        ? QString("%1.%2K").arg(std::abs(center) / 1000).arg((std::abs(center) % 1000) / 100)
        : QString::number(std::abs(center));
    p.setPen(QColor(0x90, 0xa0, 0xb0));
    p.drawText((loX + hiX) / 2 - fm.horizontalAdvance(centerText) / 2, topY + 12, centerText);

    // Lo label (centered on left slant bottom point)
    QString loText = QString::number(std::abs(m_lo));
    p.setPen(QColor(0x80, 0x90, 0xa0));
    p.drawText(loX - fm.horizontalAdvance(loText) / 2, botY + 12, loText);

    // Hi label (centered on right slant bottom point)
    QString hiText = QString::number(std::abs(m_hi));
    p.drawText(hiX - fm.horizontalAdvance(hiText) / 2, botY + 12, hiText);
}

// ─── Mouse interaction ──────────────────────────────────────────────────────

void FilterPassbandWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;
    m_dragStartPos = ev->pos();
    m_dragStartLo = m_lo;
    m_dragStartHi = m_hi;

    // Detect edge grab: ±8px from the dashed vertical lines
    constexpr int margin = 16, SKIRT = 16, GRAB = 8;
    const int loLineX = margin + SKIRT + 8;
    const int hiLineX = width() - margin - SKIRT - 8;

    if (std::abs(ev->pos().x() - loLineX) <= GRAB)
        m_dragMode = DragLo;
    else if (std::abs(ev->pos().x() - hiLineX) <= GRAB)
        m_dragMode = DragHi;
    else
        m_dragMode = DragShift;
}

void FilterPassbandWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_dragMode == DragNone) return;

    const int dx = ev->pos().x() - m_dragStartPos.x();
    const int dy = ev->pos().y() - m_dragStartPos.y();

    const int usableW = width() - 32;
    const int usableH = height();
    const double hzPerPxH = 6000.0 / std::max(usableW, 1);
    const double hzPerPxV = 4000.0 / std::max(usableH, 1);

    int newLo = m_dragStartLo;
    int newHi = m_dragStartHi;

    if (m_dragMode == DragShift) {
        // Horizontal: shift passband, vertical: symmetric width
        int shiftHz = static_cast<int>(dx * hzPerPxH);
        int bwChange = static_cast<int>(-dy * hzPerPxV);
        shiftHz = (shiftHz / 50) * 50;
        bwChange = (bwChange / 50) * 50;
        newLo = m_dragStartLo + shiftHz - bwChange / 2;
        newHi = m_dragStartHi + shiftHz + bwChange / 2;
    } else if (m_dragMode == DragLo) {
        int deltaHz = static_cast<int>(dx * hzPerPxH);
        deltaHz = (deltaHz / 50) * 50;
        newLo = m_dragStartLo + deltaHz;
    } else if (m_dragMode == DragHi) {
        int deltaHz = static_cast<int>(dx * hzPerPxH);
        deltaHz = (deltaHz / 50) * 50;
        newHi = m_dragStartHi + deltaHz;
    }

    // Enforce minimum bandwidth
    if (newHi - newLo < MIN_BW) {
        if (m_dragMode == DragLo)
            newLo = newHi - MIN_BW;
        else if (m_dragMode == DragHi)
            newHi = newLo + MIN_BW;
        else {
            int center = (newLo + newHi) / 2;
            newLo = center - MIN_BW / 2;
            newHi = center + MIN_BW / 2;
        }
    }

    // Snap to 50 Hz grid
    newLo = (newLo / 50) * 50;
    newHi = (newHi / 50) * 50;

    if (newLo != m_lo || newHi != m_hi) {
        m_lo = newLo;
        m_hi = newHi;
        update();
        emit filterChanged(m_lo, m_hi);
    }
}

void FilterPassbandWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_dragMode = DragNone;
}

} // namespace AetherSDR
