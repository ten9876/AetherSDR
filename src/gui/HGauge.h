#pragma once

#include <QPainter>
#include <QWidget>
#include <QWheelEvent>
#include <QVector>
#include <cmath>
#include <limits>

namespace AetherSDR {

// ── HGauge: reusable horizontal bar gauge ─────────────────────────────────────
//
// Draws a horizontal bar with:
//  - Dark background
//  - Filled portion (cyan below redStart, red above)
//  - Tick labels along the top
//  - Label text centred in the bar

class HGauge : public QWidget {
public:
    struct Tick { float value; QString label; };

    HGauge(float min, float max, float redStart,
           const QString& label, const QString& unit,
           const QVector<Tick>& ticks, QWidget* parent = nullptr,
           float yellowStart = std::numeric_limits<float>::quiet_NaN())
        : QWidget(parent)
        , m_min(min), m_max(max), m_redStart(redStart)
        , m_yellowStart(std::isnan(yellowStart) ? redStart : yellowStart)
        , m_label(label), m_unit(unit), m_ticks(ticks)
    {
        setFixedHeight(24);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setValue(float v) {
        if (qFuzzyCompare(m_value, v)) return;
        m_value = v;
        update();
    }

    void setPeakValue(float v) {
        if (qFuzzyCompare(m_peakValue, v)) return;
        m_peakValue = v;
        m_peakEnabled = true;
        update();
    }

    void setReversed(bool rev) { m_reversed = rev; update(); }

    void setRange(float min, float max, float redStart,
                  const QVector<Tick>& ticks, float yellowStart = std::numeric_limits<float>::quiet_NaN()) {
        m_min = min; m_max = max; m_redStart = redStart;
        m_yellowStart = std::isnan(yellowStart) ? redStart : yellowStart;
        m_ticks = ticks;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int w = width();
        const int h = height();
        const int barY = 12;
        const int barH = h - barY - 2;
        const int barX = 0;
        const int barW = w;

        // Background
        p.fillRect(barX, barY, barW, barH, QColor(0x0a, 0x0a, 0x18));
        p.setPen(QColor(0x20, 0x30, 0x40));
        p.drawRect(barX, barY, barW - 1, barH - 1);

        // Filled portion
        float frac = qBound(0.0f, (m_value - m_min) / (m_max - m_min), 1.0f);
        int fillW = static_cast<int>(frac * barW);

        if (m_reversed) {
            // Reversed: bar fills from right to left, single color.
            // frac=1 (max) means empty, frac=0 (min) means full bar.
            int revFillW = barW - fillW;
            if (revFillW > 0)
                p.fillRect(barX + fillW + 1, barY + 1, revFillW - 2, barH - 2, QColor(0xff, 0x44, 0x44));
        } else {
            // Normal: three zones cyan → yellow → red
            int yellowX = static_cast<int>(((m_yellowStart - m_min) / (m_max - m_min)) * barW);
            int redX = static_cast<int>(((m_redStart - m_min) / (m_max - m_min)) * barW);

            if (fillW > 0) {
                // Cyan portion (below yellow zone)
                int cyanW = qMin(fillW, yellowX);
                if (cyanW > 0)
                    p.fillRect(barX + 1, barY + 1, cyanW, barH - 2, QColor(0x00, 0xb4, 0xd8));

                // Yellow portion (between yellow and red zones)
                if (fillW > yellowX && yellowX < redX) {
                    int yw = qMin(fillW, redX) - yellowX;
                    if (yw > 0)
                        p.fillRect(barX + yellowX + 1, barY + 1, yw, barH - 2, QColor(0xdd, 0xbb, 0x00));
                }

                // Red portion (above red zone)
                if (fillW > redX) {
                    int rw = fillW - redX;
                    p.fillRect(barX + redX + 1, barY + 1, rw, barH - 2, QColor(0xff, 0x44, 0x44));
                }
            }
        }

        // Peak-hold marker (thin white vertical line)
        if (m_peakEnabled) {
            float peakFrac = qBound(0.0f, (m_peakValue - m_min) / (m_max - m_min), 1.0f);
            int peakX = barX + static_cast<int>(peakFrac * barW);
            if (m_reversed) {
                // In reversed mode, peak is the lowest value (most compression)
                if (peakX > barX && peakX < barX + barW - 1) {
                    p.setPen(QColor(0xff, 0xff, 0xff));
                    p.drawLine(peakX, barY + 1, peakX, barY + barH - 2);
                }
            } else {
                if (peakX > barX && peakX < barX + barW - 1) {
                    p.setPen(QColor(0xff, 0xff, 0xff));
                    p.drawLine(peakX, barY + 1, peakX, barY + barH - 2);
                }
            }
        }

        // Tick labels along the top
        QFont tickFont = font();
        tickFont.setPixelSize(9);
        p.setFont(tickFont);

        for (const auto& tick : m_ticks) {
            float tf = (tick.value - m_min) / (m_max - m_min);
            int tx = barX + static_cast<int>(tf * barW);
            QColor tickColor = (tick.value >= m_redStart) ? QColor(0xff, 0x44, 0x44)
                             : (tick.value >= m_yellowStart) ? QColor(0xdd, 0xbb, 0x00)
                             : QColor(0xc8, 0xd8, 0xe8);
            p.setPen(tickColor);
            const QFontMetrics fm(tickFont);
            int tw = fm.horizontalAdvance(tick.label);
            // Center label on tick position, clamp to widget bounds
            int lx = qBound(0, tx - tw / 2, w - tw);
            p.drawText(lx, 10, tick.label);
        }

        // Label in center of bar
        QFont lblFont = font();
        lblFont.setPixelSize(10);
        lblFont.setBold(true);
        p.setFont(lblFont);
        p.setPen(QColor(0x80, 0x90, 0xa0));
        const QFontMetrics lfm(lblFont);
        int labelW = lfm.horizontalAdvance(m_label);
        p.drawText((w - labelW) / 2, barY + barH / 2 + lfm.ascent() / 2 - 1, m_label);
    }

private:
    float m_min, m_max, m_redStart, m_yellowStart;
    float m_value{0.0f};
    float m_peakValue{0.0f};
    bool  m_peakEnabled{false};
    bool  m_reversed{false};
    QString m_label, m_unit;
    QVector<Tick> m_ticks;
};

// ── RelayBar: horizontal bar for relay position (0–255) ───────────────────────
// Supports mousewheel scrolling for manual relay adjustment (#469).

class RelayBar : public QWidget {
    Q_OBJECT

public:
    RelayBar(const QString& label, QWidget* parent = nullptr)
        : QWidget(parent), m_label(label)
    {
        setFixedHeight(18);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip("Scroll to adjust relay position");
    }

    void setValue(int v) {
        if (m_value == v) return;
        m_value = v;
        update();
    }

    void setScrollEnabled(bool on) {
        m_scrollEnabled = on;
        setCursor(on ? Qt::SizeVerCursor : Qt::ArrowCursor);
    }

signals:
    void relayAdjusted(int direction);  // +1 scroll up, -1 scroll down

protected:
    void wheelEvent(QWheelEvent* e) override {
        if (!m_scrollEnabled) {
            QWidget::wheelEvent(e);
            return;
        }
        m_angleAccum += e->angleDelta().y();
        constexpr int step = 120;
        while (m_angleAccum >= step)  { m_angleAccum -= step; emit relayAdjusted(+1); }
        while (m_angleAccum <= -step) { m_angleAccum += step; emit relayAdjusted(-1); }
        e->accept();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int w = width();
        const int h = height();
        const int labelW = 24;
        const int valueW = 28;
        const int barX = labelW;
        const int barW = w - labelW - valueW - 4;
        const int barY = 3;
        const int barH = h - 6;

        // Label
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xc8, 0xd8, 0xe8));
        p.drawText(0, barY + barH / 2 + QFontMetrics(f).ascent() / 2, m_label);

        // Bar background
        p.fillRect(barX, barY, barW, barH, QColor(0x0a, 0x0a, 0x18));
        p.setPen(QColor(0x20, 0x30, 0x40));
        p.drawRect(barX, barY, barW - 1, barH - 1);

        // Filled portion
        float frac = qBound(0.0f, m_value / 255.0f, 1.0f);
        int fillW = static_cast<int>(frac * (barW - 2));
        if (fillW > 0)
            p.fillRect(barX + 1, barY + 1, fillW, barH - 2, QColor(0x00, 0xb4, 0xd8));

        // Value text
        p.setPen(QColor(0xc8, 0xd8, 0xe8));
        const QString valText = QString::number(m_value);
        const QFontMetrics fm(f);
        p.drawText(w - valueW, barY + barH / 2 + fm.ascent() / 2, valText);
    }

private:
    QString m_label;
    int m_value{0};
    bool m_scrollEnabled{false};
    int m_angleAccum{0};
};

} // namespace AetherSDR
