#include "ClientCompMeter.h"

#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr float kLevelMinDb = -60.0f;
constexpr float kLevelMaxDb =   0.0f;
constexpr float kGrMaxMag   =  20.0f;    // GR range 0..-20 dB
constexpr int   kPeakHoldMs =  700;
constexpr float kPeakDecayDbPer100Ms = 1.0f;

// Same ballistics as HGauge so every metering surface in the app
// has matching motion.  Attack is fast enough to feel responsive on
// speech peaks; release is slow enough that the fill doesn't flicker.
constexpr int   kAnimIntervalMs = 8;
constexpr float kAttackSeconds  = 0.030f;
constexpr float kReleaseSeconds = 0.180f;
constexpr float kSnapEpsilon    = 0.001f;

const QColor kBarBg    ("#0a1420");
const QColor kLevelLo  ("#56c48b");
const QColor kLevelMid ("#e8d65a");
const QColor kLevelHi  ("#e85a5a");
const QColor kGrColor  ("#e8a540");
const QColor kLabelColor("#b0c4d6");
const QColor kPeakLine ("#ffffff");

} // namespace

ClientCompMeter::ClientCompMeter(QWidget* parent) : QWidget(parent)
{
    setMinimumWidth(18);
    setMinimumHeight(80);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    m_peakHoldTimer.start();

    m_animTimer.setTimerType(Qt::PreciseTimer);
    m_animTimer.setInterval(kAnimIntervalMs);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        const qint64 ms = m_animElapsed.restart();
        if (ms <= 0) return;
        const float delta = m_targetFrac - m_displayFrac;
        if (std::fabs(delta) <= kSnapEpsilon) {
            m_displayFrac = m_targetFrac;
            m_animTimer.stop();
        } else {
            const float tau = (delta >= 0.0f) ? kAttackSeconds : kReleaseSeconds;
            const float alpha = 1.0f - std::exp(
                -static_cast<float>(ms) / 1000.0f / tau);
            m_displayFrac += delta * alpha;
        }
        update();
    });
}

void ClientCompMeter::setMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    m_currentDb = -120.0f;
    m_peakDb    = -120.0f;
    m_displayFrac = 0.0f;
    m_targetFrac  = 0.0f;
    update();
}

void ClientCompMeter::setLabel(const QString& label)
{
    m_label = label;
    update();
}

void ClientCompMeter::recomputeTarget()
{
    float target = 0.0f;
    if (m_mode == Mode::Level) {
        target = std::clamp(
            (m_currentDb - kLevelMinDb) / (kLevelMaxDb - kLevelMinDb),
            0.0f, 1.0f);
    } else {
        const float mag = std::clamp(-m_currentDb, 0.0f, kGrMaxMag);
        target = mag / kGrMaxMag;
    }
    m_targetFrac = target;
    if (std::fabs(m_targetFrac - m_displayFrac) <= kSnapEpsilon) {
        m_displayFrac = m_targetFrac;
        if (m_animTimer.isActive()) m_animTimer.stop();
    } else if (!m_animTimer.isActive()) {
        m_animElapsed.restart();
        m_animTimer.start();
    }
}

void ClientCompMeter::setValueDb(float db)
{
    // Peak-hold tracks the loudest recent reading (or largest GR) and
    // decays after a 700 ms hold.  Jumps instantly to any new extreme
    // so transients don't get hidden.  Bar fill is smoothed separately
    // through m_targetFrac / m_displayFrac.
    if (m_mode == Mode::Level) {
        m_currentDb = db;
        if (db > m_peakDb) {
            m_peakDb = db;
            m_peakHoldTimer.restart();
        } else if (m_peakHoldTimer.elapsed() > kPeakHoldMs) {
            m_peakDb -= kPeakDecayDbPer100Ms / 10.0f;
            m_peakDb = std::max(m_peakDb, m_currentDb);
        }
    } else {
        m_currentDb = db;
        if (db < m_peakDb) {
            m_peakDb = db;
            m_peakHoldTimer.restart();
        } else if (m_peakHoldTimer.elapsed() > kPeakHoldMs) {
            m_peakDb += kPeakDecayDbPer100Ms / 10.0f;
            m_peakDb = std::min(m_peakDb, m_currentDb);
        }
    }
    recomputeTarget();
}

void ClientCompMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int labelH = m_label.isEmpty() ? 0 : 12;
    const QRectF bar(2.0, labelH + 2.0, w - 4.0, h - labelH - 4.0);

    if (!m_label.isEmpty()) {
        QFont f = p.font();
        f.setPixelSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(kLabelColor);
        p.drawText(QRectF(0, 0, w, labelH), Qt::AlignCenter, m_label);
    }

    p.fillRect(bar, kBarBg);

    if (m_mode == Mode::Level) {
        const float fillH = m_displayFrac * bar.height();
        QRectF fill(bar.left(), bar.bottom() - fillH, bar.width(), fillH);

        QLinearGradient g(0, bar.bottom(), 0, bar.top());
        g.setColorAt(0.0, kLevelLo);
        g.setColorAt(0.7, kLevelMid);
        g.setColorAt(1.0, kLevelHi);
        p.fillRect(fill, g);

        if (m_peakDb > kLevelMinDb) {
            const float tp = std::clamp(
                (m_peakDb - kLevelMinDb) / (kLevelMaxDb - kLevelMinDb),
                0.0f, 1.0f);
            const float y = bar.bottom() - tp * bar.height();
            p.setPen(QPen(kPeakLine, 1.0));
            p.drawLine(QPointF(bar.left(), y), QPointF(bar.right(), y));
        }
    } else {
        const float fillH = m_displayFrac * bar.height();
        QRectF fill(bar.left(), bar.top(), bar.width(), fillH);
        p.fillRect(fill, kGrColor);

        const float peakMag = std::clamp(-m_peakDb, 0.0f, kGrMaxMag);
        if (peakMag > 0.0f) {
            const float y = bar.top() + (peakMag / kGrMaxMag) * bar.height();
            p.setPen(QPen(kPeakLine, 1.0));
            p.drawLine(QPointF(bar.left(), y), QPointF(bar.right(), y));
        }
    }
}

} // namespace AetherSDR
