#include "ClientEqCurveWidget.h"
#include "core/ClientEq.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QFont>
#include <QColor>
#include <cmath>

namespace AetherSDR {

namespace {

// Gridlines at standard audio decades + halves. 20k is the right-hand bound.
constexpr float kMinHz   = 20.0f;
constexpr float kMaxHz   = 20000.0f;
constexpr float kDbRange = 18.0f;   // ±18 dB vertical extent

const float kGridFreqs[] = {
    20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
    1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
};

QString freqLabel(float hz)
{
    if (hz >= 1000.0f) {
        const float k = hz / 1000.0f;
        if (std::fabs(k - std::round(k)) < 0.01f) {
            return QString::number(static_cast<int>(std::round(k))) + "k";
        }
        return QString::number(k, 'f', 1) + "k";
    }
    return QString::number(static_cast<int>(std::round(hz)));
}

} // namespace

ClientEqCurveWidget::ClientEqCurveWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(80);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ClientEqCurveWidget::setEq(ClientEq* eq)
{
    m_eq = eq;
    update();
}

float ClientEqCurveWidget::freqToX(float hz) const
{
    const float logMin = std::log10(kMinHz);
    const float logMax = std::log10(kMaxHz);
    const float norm   = (std::log10(std::max(hz, 0.1f)) - logMin) / (logMax - logMin);
    return norm * static_cast<float>(width());
}

float ClientEqCurveWidget::xToFreq(float x) const
{
    const float logMin = std::log10(kMinHz);
    const float logMax = std::log10(kMaxHz);
    const float norm   = std::clamp(x / static_cast<float>(width()), 0.0f, 1.0f);
    return std::pow(10.0f, logMin + norm * (logMax - logMin));
}

float ClientEqCurveWidget::dbToY(float db) const
{
    const float h = static_cast<float>(height());
    const float norm = (kDbRange - db) / (2.0f * kDbRange);  // +db = top
    return std::clamp(norm * h, 0.0f, h);
}

float ClientEqCurveWidget::yToDb(float y) const
{
    const float h = static_cast<float>(height());
    const float norm = std::clamp(y / h, 0.0f, 1.0f);
    return kDbRange - norm * (2.0f * kDbRange);
}

void ClientEqCurveWidget::paintEvent(QPaintEvent* /*ev*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect r = rect();

    // Background — deep navy matching our dark theme.
    p.fillRect(r, QColor("#0a0a18"));

    // Minor grid — dB lines at ±6, ±12 dB.
    {
        QPen pen(QColor("#1a2a38"));
        pen.setWidth(1);
        p.setPen(pen);
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f }) {
            const float y = dbToY(db);
            p.drawLine(0, static_cast<int>(y), r.width(), static_cast<int>(y));
        }
    }

    // Main freq gridlines.
    {
        QPen pen(QColor("#203040"));
        pen.setWidth(1);
        p.setPen(pen);
        for (float hz : kGridFreqs) {
            const float x = freqToX(hz);
            p.drawLine(static_cast<int>(x), 0, static_cast<int>(x), r.height());
        }
    }

    // 0 dB reference line — slightly brighter.
    {
        QPen pen(QColor("#304050"));
        pen.setWidth(1);
        p.setPen(pen);
        const float y = dbToY(0.0f);
        p.drawLine(0, static_cast<int>(y), r.width(), static_cast<int>(y));
    }

    // Freq labels along the bottom, tiny.
    {
        QFont f = p.font();
        f.setPointSizeF(7.0);
        p.setFont(f);
        p.setPen(QColor("#506070"));
        const int fh = p.fontMetrics().height();
        for (float hz : kGridFreqs) {
            const QString lbl = freqLabel(hz);
            const int w = p.fontMetrics().horizontalAdvance(lbl);
            int x = static_cast<int>(freqToX(hz)) - w / 2;
            x = std::clamp(x, 2, r.width() - w - 2);
            p.drawText(x, r.height() - 2, lbl);
            (void)fh;
        }
    }

    // Placeholder "EQ curve" message — removed in Phase B.2 when we
    // start drawing the actual summed response.
    if (!m_eq) {
        p.setPen(QColor("#405060"));
        QFont f = p.font();
        f.setPointSizeF(8.0);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, "EQ curve — editor coming soon");
    } else {
        p.setPen(QColor("#405060"));
        QFont f = p.font();
        f.setPointSizeF(8.0);
        p.setFont(f);
        const QString msg = m_eq->isEnabled()
            ? QString("%1 band%2 active").arg(m_eq->activeBandCount())
                                          .arg(m_eq->activeBandCount() == 1 ? "" : "s")
            : QString("(EQ disabled)");
        p.drawText(r, Qt::AlignCenter, msg);
    }
}

} // namespace AetherSDR
