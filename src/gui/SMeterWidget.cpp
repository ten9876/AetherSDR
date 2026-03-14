#include "SMeterWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QtMath>
#include <QFontMetrics>

namespace AetherSDR {

// ─── Construction ────────────────────────────────────────────────────────────

SMeterWidget::SMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Peak hold decay: drops 0.5 dB every 50 ms after a new peak
    m_peakDecay.setInterval(50);
    connect(&m_peakDecay, &QTimer::timeout, this, [this]() {
        m_peakDbm -= 0.5f;
        if (m_peakDbm < m_levelDbm) {
            m_peakDbm = m_levelDbm;
            m_peakDecay.stop();
        }
        update();
    });

    // Hard reset peak hold every 10 seconds
    m_peakReset.setInterval(10000);
    m_peakReset.start();
    connect(&m_peakReset, &QTimer::timeout, this, [this]() {
        m_peakDbm = m_levelDbm;
        update();
    });
}

// ─── Public interface ────────────────────────────────────────────────────────

void SMeterWidget::setLevel(float dbm)
{
    // Exponential smoothing for needle movement
    m_levelDbm = m_levelDbm + SMOOTH_ALPHA * (dbm - m_levelDbm);

    // Peak hold
    if (dbm > m_peakDbm) {
        m_peakDbm = dbm;
        m_peakDecay.start();
    }

    update();
}

QString SMeterWidget::sUnitsText() const
{
    if (m_levelDbm <= S0_DBM) return "S0";
    if (m_levelDbm <= S9_DBM) {
        const int s = qRound((m_levelDbm - S0_DBM) / DB_PER_S);
        return QString("S%1").arg(qBound(0, s, 9));
    }
    const int over = qRound(m_levelDbm - S9_DBM);
    return QString("S9+%1").arg(over);
}

// ─── Mapping ─────────────────────────────────────────────────────────────────

float SMeterWidget::dbmToFraction(float dbm) const
{
    // S0 to S9 occupies the left 60% of the arc
    // S9 to S9+60 occupies the right 40%
    const float clamped = qBound(S0_DBM, dbm, MAX_DBM);

    if (clamped <= S9_DBM) {
        // Linear within S0..S9 → 0.0..0.6
        return 0.6f * (clamped - S0_DBM) / (S9_DBM - S0_DBM);
    }
    // Linear within S9..S9+60 → 0.6..1.0
    return 0.6f + 0.4f * (clamped - S9_DBM) / (MAX_DBM - S9_DBM);
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void SMeterWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();

    // Background
    p.fillRect(rect(), QColor(0x0f, 0x0f, 0x1a));

    // ── Arc geometry ─────────────────────────────────────────────────────
    // Large radius with center far below widget → shallow ~70° arc segment
    const float cx = w * 0.5f;
    const float radius = w * 0.85f;
    const float cy = h + radius - h * 0.65f;  // arc center well below widget
    const float needleCy = h + 6.0f;          // needle origin just below widget bottom

    // Convert arc degrees to radians
    const float arcStartRad = qDegreesToRadians(ARC_START_DEG);
    const float arcEndRad   = qDegreesToRadians(ARC_END_DEG);
    const float arcSpanRad  = arcEndRad - arcStartRad;

    // fraction 0.0 → left end (ARC_END_DEG), fraction 1.0 → right end (ARC_START_DEG)
    auto fractionToAngle = [&](float frac) -> float {
        return arcEndRad - frac * arcSpanRad;  // radians
    };

    // ── Draw colored outer arc (RX scale) ───────────────────────────────
    // White from S0 to S9, red from S9+
    {
        const QRectF outerArc(cx - radius, cy - radius, radius * 2, radius * 2);
        const float s9Deg = qRadiansToDegrees(fractionToAngle(0.6f));

        QPen whitePen(QColor(0xc8, 0xd8, 0xe8), 3);
        p.setPen(whitePen);
        p.drawArc(outerArc,
                  static_cast<int>(s9Deg * 16),
                  static_cast<int>((ARC_END_DEG - s9Deg) * 16));

        QPen redPen(QColor(0xff, 0x44, 0x44), 3);
        p.setPen(redPen);
        p.drawArc(outerArc,
                  static_cast<int>(ARC_START_DEG * 16),
                  static_cast<int>((s9Deg - ARC_START_DEG) * 16));
    }

    // ── Draw colored inner arc (TX scale) — 6px gap ──────────────────────
    // Blue from 0 to ~80 W, red from ~80 to 120 W
    const float arcGap = 6.0f;
    {
        const float innerR = radius - arcGap;
        const QRectF innerArc(cx - innerR, cy - innerR, innerR * 2, innerR * 2);

        const float splitDeg = qRadiansToDegrees(fractionToAngle(100.0f / 120.0f));

        QPen bluePen(QColor(0x00, 0x80, 0xd0), 3);
        p.setPen(bluePen);
        p.drawArc(innerArc,
                  static_cast<int>(splitDeg * 16),
                  static_cast<int>((ARC_END_DEG - splitDeg) * 16));

        QPen redPen(QColor(0xff, 0x44, 0x44), 3);
        p.setPen(redPen);
        p.drawArc(innerArc,
                  static_cast<int>(ARC_START_DEG * 16),
                  static_cast<int>((splitDeg - ARC_START_DEG) * 16));
    }

    // ── Tick drawing helpers ──────────────────────────────────────────────
    QFont tickFont = font();
    tickFont.setPixelSize(qMax(10, h / 10));
    tickFont.setBold(true);
    p.setFont(tickFont);
    const QFontMetrics tfm(tickFont);

    // Direction from needle origin through arc point, normalized
    auto needleDir = [&](float angle) -> std::pair<float, float> {
        const float arcX = cx + radius * std::cos(angle);
        const float arcY = cy - radius * std::sin(angle);
        const float dx = arcX - cx;
        const float dy = arcY - needleCy;
        const float len = std::sqrt(dx * dx + dy * dy);
        return {dx / len, dy / len};
    };

    // Outside tick (RX): extends outward from the arc, label above
    auto drawOutsideTick = [&](float frac, const QString& label, const QColor& color,
                               bool showLabel) {
        const float angle = fractionToAngle(frac);
        const float arcX = cx + radius * std::cos(angle);
        const float arcY = cy - radius * std::sin(angle);
        auto [ux, uy] = needleDir(angle);

        const QPointF inner(arcX + 2 * ux, arcY + 2 * uy);
        const QPointF outer(arcX + 14 * ux, arcY + 14 * uy);

        p.setPen(QPen(color, 1.5));
        p.drawLine(inner, outer);

        if (showLabel) {
            const QPointF labelPt(arcX + 26 * ux, arcY + 26 * uy);
            const int tw = tfm.horizontalAdvance(label);
            p.setPen(color);
            p.drawText(QPointF(labelPt.x() - tw / 2.0,
                               labelPt.y() + tfm.ascent() / 2.0), label);
        }
    };

    // Inside tick (TX): extends inward from the inner colored arc
    const float innerArcR = radius - arcGap;
    auto drawInsideTick = [&](float frac, const QString& label,
                              const QColor& tickColor, const QColor& labelColor,
                              bool showLabel) {
        const float angle = fractionToAngle(frac);
        // Start from the inner colored arc, not the outer arc
        const float iArcX = cx + innerArcR * std::cos(angle);
        const float iArcY = cy - innerArcR * std::sin(angle);
        auto [ux, uy] = needleDir(angle);

        const QPointF outer(iArcX - 2 * ux, iArcY - 2 * uy);
        const QPointF inner(iArcX - 14 * ux, iArcY - 14 * uy);

        p.setPen(QPen(tickColor, 1.5));
        p.drawLine(inner, outer);

        if (showLabel) {
            const QPointF labelPt(iArcX - 26 * ux, iArcY - 26 * uy);
            const int tw = tfm.horizontalAdvance(label);
            p.setPen(labelColor);
            p.drawText(QPointF(labelPt.x() - tw / 2.0,
                               labelPt.y() + tfm.ascent() / 2.0), label);
        }
    };

    const QColor whiteColor(0xc8, 0xd8, 0xe8);
    const QColor redColor(0xff, 0x44, 0x44);

    // ── Outside ticks (RX): S-meter scale — odd S-units only ──────────
    for (int s = 1; s <= 9; s += 2) {
        const float dbm = S0_DBM + s * DB_PER_S;
        drawOutsideTick(dbmToFraction(dbm), QString::number(s), whiteColor, true);
    }
    for (int over : {20, 40}) {
        const float dbm = S9_DBM + over;
        drawOutsideTick(dbmToFraction(dbm), QString("+%1").arg(over), redColor, true);
    }

    // ── Inside ticks (TX): Power scale 0–120 W ──────────────────────────
    // Linear mapping: 0 W → fraction 0.0, 120 W → fraction 1.0
    const float txMaxW = 120.0f;
    const QSet<int> txLabeled = {0, 40, 80, 100, 120};
    const QColor blueColor(0x00, 0x80, 0xd0);
    for (int watts = 0; watts <= 120; watts += 10) {
        const float frac = watts / txMaxW;
        const QString label = QString::number(watts);
        const QColor& tickColor = (watts >= 100) ? redColor : blueColor;
        const QColor& lblColor = (watts >= 100) ? redColor : whiteColor;
        drawInsideTick(frac, label, tickColor, lblColor, txLabeled.contains(watts));
    }

    // ── Draw needle ──────────────────────────────────────────────────────
    // Needle originates from needleCy (just below widget) rather than the
    // arc center, so the pivot is barely out of frame.
    {
        const float frac = dbmToFraction(m_levelDbm);
        const float angle = fractionToAngle(frac);

        // Compute where the needle should hit the scale (on the arc),
        // then extend 20px past the arc along the same direction.
        const float tipR = radius - 30;
        const float tipX = cx + tipR * std::cos(angle);
        const float tipY = cy - tipR * std::sin(angle);

        // Direction from needle origin to tip, normalized
        const float dx = tipX - cx;
        const float dy = tipY - needleCy;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float extX = tipX + 20.0f * dx / len;
        const float extY = tipY + 20.0f * dy / len;

        // Needle shadow
        p.setPen(QPen(QColor(0, 0, 0, 80), 3));
        p.drawLine(QPointF(cx + 1, needleCy + 1), QPointF(extX + 1, extY + 1));

        // Needle
        p.setPen(QPen(QColor(0xff, 0xff, 0xff), 2));
        p.drawLine(QPointF(cx, needleCy), QPointF(extX, extY));
    }

    // ── Draw peak marker (small triangle) ────────────────────────────────
    if (m_peakDbm > m_levelDbm + 1.0f) {
        const float frac = dbmToFraction(m_peakDbm);
        const float angle = fractionToAngle(frac);
        const float markerR = radius - 2;

        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        const QPointF tip(cx + markerR * cosA, cy - markerR * sinA);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0xaa, 0x00));
        const float perpCos = -sinA;
        const float perpSin = cosA;
        const float sz = 3.0f;
        QPainterPath tri;
        tri.moveTo(tip);
        tri.lineTo(tip.x() - 6 * cosA + sz * perpCos,
                   tip.y() + 6 * sinA + sz * perpSin);
        tri.lineTo(tip.x() - 6 * cosA - sz * perpCos,
                   tip.y() + 6 * sinA - sz * perpSin);
        tri.closeSubpath();
        p.drawPath(tri);
    }

    // ── Text readout — all top-aligned on the same baseline ─────────────
    QFont srcFont = font();
    srcFont.setPixelSize(qMax(9, h / 14));
    const QFontMetrics sfm(srcFont);
    const int topY = sfm.height() + 2;

    p.setFont(srcFont);
    p.setPen(QColor(0x80, 0x90, 0xa0));
    p.drawText((w - sfm.horizontalAdvance(m_source)) / 2, topY, m_source);

    // S-units — top left, larger font
    QFont sFont = font();
    sFont.setPixelSize(qMax(13, h / 8));
    sFont.setBold(true);
    p.setFont(sFont);
    p.setPen(QColor(0x00, 0xb4, 0xd8));
    p.drawText(6, topY, sUnitsText());

    // dBm value — top right, same size as S-units
    QFont dbmFont = font();
    dbmFont.setPixelSize(qMax(13, h / 8));
    dbmFont.setBold(true);
    p.setFont(dbmFont);
    const QFontMetrics dfm(dbmFont);
    const QString dbmText = QString("%1 dBm").arg(m_levelDbm, 0, 'f', 0);
    p.setPen(QColor(0xc8, 0xd8, 0xe8));
    p.drawText(w - dfm.horizontalAdvance(dbmText) - 6, topY, dbmText);
}

} // namespace AetherSDR
