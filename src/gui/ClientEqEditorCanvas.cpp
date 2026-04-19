#include "ClientEqEditorCanvas.h"
#include "core/AudioEngine.h"
#include "core/ClientEq.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QPointF>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {
constexpr float kHandleHitRadius = 8.0f;
}

ClientEqEditorCanvas::ClientEqEditorCanvas(QWidget* parent)
    : ClientEqCurveWidget(parent)
{
    setMinimumHeight(260);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

void ClientEqEditorCanvas::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
}

int ClientEqEditorCanvas::hitTestHandle(const QPointF& pos) const
{
    if (!m_eq) return -1;
    const int n = m_eq->activeBandCount();
    int best = -1;
    float bestDist = kHandleHitRadius * kHandleHitRadius;
    for (int i = 0; i < n; ++i) {
        const auto bp = m_eq->band(i);
        const bool isSlope = (bp.type == ClientEq::FilterType::LowPass
                           || bp.type == ClientEq::FilterType::HighPass);
        const float handleDb = isSlope ? 0.0f : bp.gainDb;
        const QPointF c(freqToX(bp.freqHz), dbToY(handleDb));
        const float dx = static_cast<float>(pos.x() - c.x());
        const float dy = static_cast<float>(pos.y() - c.y());
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDist) {
            bestDist = d2;
            best = i;
        }
    }
    return best;
}

void ClientEqEditorCanvas::persist()
{
    if (m_audio) m_audio->saveClientEqSettings();
    emit bandsChanged();
    update();
}

void ClientEqEditorCanvas::mousePressEvent(QMouseEvent* ev)
{
    if (!m_eq) { QWidget::mousePressEvent(ev); return; }
    if (ev->button() != Qt::LeftButton) { QWidget::mousePressEvent(ev); return; }

    const int idx = hitTestHandle(ev->position());
    if (idx < 0) {
        // Clicking empty canvas clears selection so the icon row + param
        // row lose their highlight. Keeps the UI honest about where focus is.
        setSelectedBand(-1);
        QWidget::mousePressEvent(ev);
        return;
    }

    setSelectedBand(idx);

    const auto bp = m_eq->band(idx);
    m_draggingBand = idx;
    m_dragShift    = (ev->modifiers() & Qt::ShiftModifier) != 0;
    m_dragStart    = ev->position();
    m_dragStartFreqHz = bp.freqHz;
    m_dragStartGainDb = bp.gainDb;
    m_dragStartQ      = bp.q;
    setCursor(Qt::ClosedHandCursor);
    ev->accept();
}

void ClientEqEditorCanvas::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_draggingBand < 0 || !m_eq) { QWidget::mouseMoveEvent(ev); return; }

    const auto bp = m_eq->band(m_draggingBand);
    ClientEq::BandParams next = bp;
    const bool isSlope = (bp.type == ClientEq::FilterType::LowPass
                       || bp.type == ClientEq::FilterType::HighPass);

    if (m_dragShift) {
        // Shift-drag = vertical Q control. Map vertical delta to
        // exponential Q change so a full-height drag spans ~2 decades.
        const float dy = static_cast<float>(m_dragStart.y() - ev->position().y());
        const float hOver2 = std::max(1.0f, static_cast<float>(height()) * 0.5f);
        const float factor = std::pow(10.0f, dy / hOver2);
        next.q = std::clamp(m_dragStartQ * factor, 0.1f, 18.0f);
    } else {
        // Freq is always horizontal. Gain is vertical for peak/shelf.
        // HP/LP have no gain, so their vertical drag controls Q instead
        // — each handle keeps two degrees of freedom, no shift needed.
        next.freqHz = std::clamp(xToFreq(static_cast<float>(ev->position().x())),
                                 20.0f, 20000.0f);
        if (isSlope) {
            const float dy = static_cast<float>(m_dragStart.y() - ev->position().y());
            const float hOver2 = std::max(1.0f, static_cast<float>(height()) * 0.5f);
            const float factor = std::pow(10.0f, dy / hOver2);
            next.q = std::clamp(m_dragStartQ * factor, 0.1f, 18.0f);
        } else {
            next.gainDb = std::clamp(yToDb(static_cast<float>(ev->position().y())),
                                     -18.0f, 18.0f);
        }
    }
    m_eq->setBand(m_draggingBand, next);
    emit bandsChanged();
    update();
    ev->accept();
}

void ClientEqEditorCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_draggingBand < 0) { QWidget::mouseReleaseEvent(ev); return; }
    m_draggingBand = -1;
    m_dragShift = false;
    setCursor(Qt::CrossCursor);
    persist();
    ev->accept();
}

void ClientEqEditorCanvas::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (!m_eq) { QWidget::mouseDoubleClickEvent(ev); return; }
    if (ev->button() != Qt::LeftButton) { QWidget::mouseDoubleClickEvent(ev); return; }

    // Adding a new band on empty space. If a handle is near, let the hit
    // test (which also drives regular drag) handle it on the single-press
    // pass — here we only care about empty-area creation.
    if (hitTestHandle(ev->position()) >= 0) {
        QWidget::mouseDoubleClickEvent(ev);
        return;
    }

    const int bandCount = m_eq->activeBandCount();
    if (bandCount >= ClientEq::kMaxBands) return;

    // Auto-pick the filter type from click position: HP near left edge,
    // LP near right edge, shelves at top/bottom extremes, peak otherwise.
    const float xNorm = static_cast<float>(ev->position().x()) / width();
    const float yNorm = static_cast<float>(ev->position().y()) / height();
    ClientEq::FilterType type = ClientEq::FilterType::Peak;
    if (xNorm < 0.08f) {
        type = ClientEq::FilterType::HighPass;
    } else if (xNorm > 0.92f) {
        type = ClientEq::FilterType::LowPass;
    } else if (yNorm < 0.12f && xNorm < 0.4f) {
        type = ClientEq::FilterType::LowShelf;
    } else if (yNorm < 0.12f && xNorm > 0.6f) {
        type = ClientEq::FilterType::HighShelf;
    }

    ClientEq::BandParams bp;
    bp.type    = type;
    bp.freqHz  = xToFreq(static_cast<float>(ev->position().x()));
    bp.gainDb  = (type == ClientEq::FilterType::LowPass
               || type == ClientEq::FilterType::HighPass)
                 ? 0.0f
                 : std::clamp(yToDb(static_cast<float>(ev->position().y())),
                              -18.0f, 18.0f);
    bp.q       = (type == ClientEq::FilterType::LowPass
               || type == ClientEq::FilterType::HighPass)
                 ? 0.707f
                 : 1.0f;
    bp.enabled = true;
    m_eq->setBand(bandCount, bp);
    m_eq->setActiveBandCount(bandCount + 1);
    setSelectedBand(bandCount);
    persist();
    ev->accept();
}

void ClientEqEditorCanvas::contextMenuEvent(QContextMenuEvent* ev)
{
    if (!m_eq) { QWidget::contextMenuEvent(ev); return; }

    const int idx = hitTestHandle(ev->pos());
    if (idx < 0) { QWidget::contextMenuEvent(ev); return; }

    const auto bp = m_eq->band(idx);
    QMenu menu(this);

    auto* typeLabel = menu.addAction(
        QString("Band %1 — %2").arg(idx + 1).arg(
            bp.type == ClientEq::FilterType::Peak      ? "Peak"
          : bp.type == ClientEq::FilterType::LowShelf  ? "Low Shelf"
          : bp.type == ClientEq::FilterType::HighShelf ? "High Shelf"
          : bp.type == ClientEq::FilterType::LowPass   ? "Low Pass"
          : "High Pass"));
    typeLabel->setEnabled(false);
    menu.addSeparator();

    auto* peakAct = menu.addAction("Set type: Peak");
    auto* lsAct   = menu.addAction("Set type: Low Shelf");
    auto* hsAct   = menu.addAction("Set type: High Shelf");
    auto* lpAct   = menu.addAction("Set type: Low Pass");
    auto* hpAct   = menu.addAction("Set type: High Pass");
    menu.addSeparator();
    auto* enAct   = menu.addAction(bp.enabled ? "Bypass band" : "Enable band");
    auto* delAct  = menu.addAction("Delete band");

    QAction* chosen = menu.exec(ev->globalPos());
    if (!chosen) return;

    auto change = [&](ClientEq::FilterType t) {
        ClientEq::BandParams p = bp; p.type = t; m_eq->setBand(idx, p);
    };
    if      (chosen == peakAct) change(ClientEq::FilterType::Peak);
    else if (chosen == lsAct)   change(ClientEq::FilterType::LowShelf);
    else if (chosen == hsAct)   change(ClientEq::FilterType::HighShelf);
    else if (chosen == lpAct)   change(ClientEq::FilterType::LowPass);
    else if (chosen == hpAct)   change(ClientEq::FilterType::HighPass);
    else if (chosen == enAct) {
        ClientEq::BandParams p = bp; p.enabled = !bp.enabled; m_eq->setBand(idx, p);
    }
    else if (chosen == delAct) {
        // Collapse: shift all later bands down one slot, shrink active count.
        const int n = m_eq->activeBandCount();
        for (int i = idx; i < n - 1; ++i) {
            m_eq->setBand(i, m_eq->band(i + 1));
        }
        // Blank out the now-unused top slot so stale data doesn't leak
        // back on a future resize.
        if (n - 1 < ClientEq::kMaxBands) {
            ClientEq::BandParams blank;
            m_eq->setBand(n - 1, blank);
        }
        m_eq->setActiveBandCount(n - 1);
        // Adjust selection: if we deleted the selected band, clear;
        // if the selected band sat above the deletion, shift down.
        if (selectedBand() == idx) {
            setSelectedBand(-1);
        } else if (selectedBand() > idx) {
            setSelectedBand(selectedBand() - 1);
        }
    }
    persist();
    ev->accept();
}

} // namespace AetherSDR
