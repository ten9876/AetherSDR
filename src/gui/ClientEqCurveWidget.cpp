#include "ClientEqCurveWidget.h"
#include "core/ClientEq.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <QColor>
#include <array>
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

// Logic-Pro-style palette across the audio spectrum. Gray at the extremes
// reserves those slots visually for HP / LP slopes; the middle rainbow is
// for peaks and shelves. Interpolated beyond 8 slots (up to kMaxBands=16).
const std::array<QColor, 8> kPalette = {
    QColor("#9aa4ad"),  // gray
    QColor("#e8a35a"),  // amber
    QColor("#e8d65a"),  // yellow
    QColor("#66d19e"),  // green
    QColor("#66c8d1"),  // teal
    QColor("#6b92d6"),  // blue
    QColor("#a888d6"),  // purple
    QColor("#9aa4ad"),  // gray
};

} // namespace

QColor ClientEqCurveWidget::bandColor(int bandIdx)
{
    if (bandIdx < 0) bandIdx = 0;
    // Wrap by modulo so 8..15 reuse 0..7 rather than clamp to gray.
    return kPalette[static_cast<size_t>(bandIdx) % kPalette.size()];
}

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

void ClientEqCurveWidget::setSelectedBand(int idx)
{
    if (idx == m_selectedBand) return;
    m_selectedBand = idx;
    emit selectedBandChanged(idx);
    update();
}

void ClientEqCurveWidget::setShowFilledRegions(bool on)
{
    if (on == m_showFilled) return;
    m_showFilled = on;
    update();
}

void ClientEqCurveWidget::setFftBinsDb(const std::vector<float>& binsDb,
                                       double sampleRate)
{
    m_fftBinsDb = binsDb;
    m_fftSampleRate = sampleRate > 0.0 ? sampleRate : 24000.0;
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

    // Live FFT analyzer — filled gradient showing what's actually flowing
    // through the audio path post-EQ.  Drawn early so every EQ-visual
    // layer sits on top.  Scale: -70 dB → bottom, 0 dB → top.
    if (!m_fftBinsDb.empty()) {
        const int bins = static_cast<int>(m_fftBinsDb.size());
        const float minDb = -70.0f;
        const float maxDb =   0.0f;
        const float h = static_cast<float>(r.height());
        auto dbfsToY = [&](float db) {
            const float n = (db - minDb) / (maxDb - minDb);
            return (1.0f - std::clamp(n, 0.0f, 1.0f)) * h;
        };

        QPainterPath fftPath;
        fftPath.moveTo(0, h);
        bool  started = false;
        float lastX   = 0.0f;
        for (int i = 1; i < bins; ++i) {
            // bins.size() == fftSize/2 + 1; bin i maps to i * fs / fftSize.
            const float f = static_cast<float>(i) *
                            static_cast<float>(m_fftSampleRate) /
                            static_cast<float>((bins - 1) * 2);
            const float x = freqToX(f);
            if (x < 0 || x > r.width()) continue;
            const float y = dbfsToY(m_fftBinsDb[i]);
            if (!started) { fftPath.lineTo(x, h); started = true; }
            fftPath.lineTo(x, y);
            lastX = x;
        }
        // Close the filled region at the last valid bin's x, not at the
        // canvas right edge.  Above Nyquist the FFT has no bins; drawing
        // out to r.width() produced a misleading near-horizontal "shelf"
        // connecting the last bin's level to the bottom-right corner.
        if (started) {
            fftPath.lineTo(lastX, h);
        }
        fftPath.closeSubpath();

        QLinearGradient grad(0, 0, 0, h);
        grad.setColorAt(0.0, QColor(88, 200, 232, 140));   // cyan top
        grad.setColorAt(0.6, QColor(30, 110, 170,  70));
        grad.setColorAt(1.0, QColor(12,  40,  70,   0));   // fades to clear
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPath(fftPath);
    }

    if (!m_eq || m_eq->activeBandCount() == 0) {
        p.setPen(QColor("#405060"));
        QFont f = p.font();
        f.setPointSizeF(8.0);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter,
                   m_eq ? QString("(no bands — add one in the editor)")
                        : QString("(no EQ connected)"));
        return;
    }

    const int   bandCount = m_eq->activeBandCount();
    const double fs       = m_eq->sampleRate();
    const int   W         = r.width();
    const bool  eqOn      = m_eq->isEnabled();

    // bandMagnitudeDb evaluates analog-prototype transfer functions in
    // double precision, so the drawn response is ideal across the full
    // 20 Hz - 20 kHz canvas — no aliasing, no Nyquist artefacts, no low-
    // end precision loss. The audio path still uses the real-rate digital
    // biquads; this is the analog reference the biquad approximates.
    // HP/LP bands cascade internally based on slopeDbPerOct and the
    // globally-selected FilterFamily on the bound ClientEq.
    const ClientEq::FilterFamily family = m_eq->filterFamily();
    QVector<float> summed(W, 0.0f);
    QVector<QVector<float>> perBand(bandCount, QVector<float>(W, 0.0f));
    for (int x = 0; x < W; ++x) {
        const float probe = xToFreq(static_cast<float>(x));
        float acc = 0.0f;
        for (int i = 0; i < bandCount; ++i) {
            const auto bp = m_eq->band(i);
            const float dB = ClientEq::bandMagnitudeDb(bp, probe, fs, family);
            perBand[i][x] = dB;
            acc += dB;
        }
        summed[x] = acc;
    }

    // Selected-band highlight bar — vertical translucent stripe that ties
    // the icon row, canvas, and param-row column together.  Drawn before
    // the filled regions so the filled region colour still shows through.
    if (m_selectedBand >= 0 && m_selectedBand < bandCount) {
        const auto bp = m_eq->band(m_selectedBand);
        const float cx = freqToX(bp.freqHz);
        const float stripeWidth = 18.0f;
        QColor stripe = bandColor(m_selectedBand);
        stripe.setAlphaF(0.16f);
        p.fillRect(QRectF(cx - stripeWidth * 0.5f, 0.0f,
                          stripeWidth, static_cast<float>(r.height())),
                   stripe);
    }

    // Filled per-band regions — semi-transparent area between the 0 dB
    // line and each band's response.  Renders the Logic-Pro-style "see
    // what each band is doing" look.  Drawn first so the per-band strokes
    // and summed curve on top stay readable.
    if (m_showFilled) {
        const float yZero = dbToY(0.0f);
        for (int i = 0; i < bandCount; ++i) {
            const auto bp = m_eq->band(i);
            if (!bp.enabled) continue;
            QColor fill = bandColor(i);
            fill.setAlphaF(eqOn ? 0.22f : 0.07f);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            QPainterPath path;
            path.moveTo(0, yZero);
            for (int x = 0; x < W; ++x) {
                path.lineTo(x, dbToY(perBand[i][x]));
            }
            path.lineTo(W - 1, yZero);
            path.closeSubpath();
            p.drawPath(path);
        }
    }

    // Per-band curves — thin stroke in the band's palette colour.
    for (int i = 0; i < bandCount; ++i) {
        QColor c = bandColor(i);
        c.setAlphaF(eqOn ? 0.55f : 0.18f);
        QPen pen(c);
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        for (int x = 0; x < W; ++x) {
            const float y = dbToY(perBand[i][x]);
            if (x == 0) path.moveTo(x, y);
            else        path.lineTo(x, y);
        }
        p.drawPath(path);
    }

    // Summed curve — bolder stroke in slightly saturated cyan when enabled,
    // dimmed when bypassed.
    {
        QColor c = eqOn ? QColor("#00b4d8") : QColor("#506070");
        QPen pen(c);
        pen.setWidthF(1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        QPainterPath path;
        for (int x = 0; x < W; ++x) {
            const float y = dbToY(summed[x]);
            if (x == 0) path.moveTo(x, y);
            else        path.lineTo(x, y);
        }
        p.drawPath(path);
    }

    // Band handles — small filled circles at each band's (freq, gain).
    // For Peak / shelf types the handle sits on the (freq, gain) point.
    // For HP / LP (which have no user gain), we anchor at 0 dB.
    // Selected band renders a halo ring to match the icon-row highlight.
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < bandCount; ++i) {
        const auto bp = m_eq->band(i);
        const bool isSlope = (bp.type == ClientEq::FilterType::LowPass
                           || bp.type == ClientEq::FilterType::HighPass);
        const float handleDb = isSlope ? 0.0f : bp.gainDb;
        const QPointF center(freqToX(bp.freqHz), dbToY(handleDb));
        QColor c = bandColor(i);
        if (!bp.enabled || !eqOn) c.setAlphaF(0.35f);

        if (i == m_selectedBand) {
            QColor halo = c; halo.setAlphaF(0.35f);
            p.setBrush(halo);
            p.setPen(Qt::NoPen);
            p.drawEllipse(center, 8.0, 8.0);
        }
        p.setBrush(c);
        p.setPen(QPen(QColor("#08121d"), 1.5));
        p.drawEllipse(center, 4.0, 4.0);
    }
}

} // namespace AetherSDR
