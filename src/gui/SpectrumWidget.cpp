#include "SpectrumWidget.h"
#include "SpectrumOverlayMenu.h"
#include "VfoWidget.h"
#include "SliceColors.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QToolTip>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include "core/AppSettings.h"
#include "models/BandPlanManager.h"
#include "models/BandDefs.h"
#include <QDateTime>
#include <QTimeZone>
#include <QElapsedTimer>
#include "core/LogManager.h"
#include <cmath>
#include <cstring>

namespace AetherSDR {

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);

    // Floating overlay menu (child widget, stays on top)
    m_overlayMenu = new SpectrumOverlayMenu(this);
    m_overlayMenu->raise();

    // Load display settings (panIndex 0 by default — loadSettings() can be
    // called again after setPanIndex() for multi-pan)
    loadSettings();

    // VFO widgets are created per-slice via addVfoWidget().
    // m_vfoWidget is set by setActiveVfoWidget() as an alias to the active one.

    // Bottom-left waterfall zoom buttons
    static const QString kZoomBtnStyle =
        "QPushButton { background: rgba(15,15,26,180); border: 1px solid #304050;"
        " border-radius: 2px; color: #90a0b0; font-size: 11px; font-weight: bold;"
        " padding: 0; margin: 0; min-width: 0; }"
        "QPushButton:hover { background: rgba(30,50,70,200); color: #c8d8e8; }"
        "QPushButton:pressed { background: #00b4d8; color: #000; }";

    auto makeBtn = [&](const QString& text) {
        auto* btn = new QPushButton(text, this);
        btn->setFixedSize(22, 22);
        btn->setStyleSheet(kZoomBtnStyle);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };
    m_zoomSegBtn  = makeBtn("S");
    m_zoomBandBtn = makeBtn("B");

    // SmartSDR pcap: B sends "band_zoom=1", S sends "segment_zoom=1"
    connect(m_zoomBandBtn, &QPushButton::clicked, this, [this]() {
        emit bandZoomRequested();
    });
    connect(m_zoomSegBtn, &QPushButton::clicked, this, [this]() {
        emit segmentZoomRequested();
    });
}

// ── Multi-VfoWidget management ────────────────────────────────────────────────

VfoWidget* SpectrumWidget::vfoWidget(int sliceId) const
{
    return m_vfoWidgets.value(sliceId, nullptr);
}

QString SpectrumWidget::settingsKey(const QString& base) const
{
    if (m_panIndex == 0)
        return base;  // backward compat — no suffix for pan 0
    return QString("%1_%2").arg(base).arg(m_panIndex);
}

void SpectrumWidget::loadSettings()
{
    auto& s = AppSettings::instance();
    m_spectrumFrac   = std::clamp(s.value(settingsKey("SpectrumSplitRatio"), "0.40").toFloat(), 0.10f, 0.90f);
    m_fftAverage     = s.value(settingsKey("DisplayFftAverage"), "0").toInt();
    m_fftFps         = s.value(settingsKey("DisplayFftFps"), "25").toInt();
    m_fftFillAlpha   = s.value(settingsKey("DisplayFftFillAlpha"), "0.70").toFloat();
    m_fftWeightedAvg = s.value(settingsKey("DisplayFftWeightedAvg"), "False").toString() == "True";
    const QString fillColorStr = s.value(settingsKey("DisplayFftFillColor"), "#00e5ff").toString();
    QColor parsed(fillColorStr);
    if (parsed.isValid())
        m_fftFillColor = parsed;
    m_wfColorGain    = s.value(settingsKey("DisplayWfColorGain"), "50").toInt();
    m_wfBlackLevel   = s.value(settingsKey("DisplayWfBlackLevel"), "15").toInt();
    m_wfAutoBlack    = s.value(settingsKey("DisplayWfAutoBlack"), "True").toString() == "True";
    m_wfLineDuration = s.value(settingsKey("DisplayWfLineDuration"), "100").toInt();
    // NB Waterfall Blanker (#277)
    m_wfBlankerEnabled   = s.value(settingsKey("WaterfallBlankingEnabled"), "False").toString() == "True";
    m_wfBlankerMode      = s.value(settingsKey("WaterfallBlankingMode"), "0").toInt();
    m_wfBlankerThreshold = std::clamp(
        s.value(settingsKey("WaterfallBlankingThreshold"), "1.15").toFloat(), 1.05f, 2.0f);
    // Migrate old ShowBandPlan bool → BandPlanFontSize int
    if (s.value("BandPlanFontSize").toString().isEmpty()) {
        m_bandPlanFontSize = s.value("ShowBandPlan", "True").toString() == "True" ? 6 : 0;
    } else {
        m_bandPlanFontSize = s.value("BandPlanFontSize", "6").toInt();
    }
    m_singleClickTune = s.value("SingleClickTune", "False").toString() == "True";

    // Sync overlay menu sliders with restored settings
    if (m_overlayMenu)
        m_overlayMenu->syncDisplaySettings(m_fftAverage, m_fftFps,
            static_cast<int>(m_fftFillAlpha * 100), m_fftWeightedAvg, m_fftFillColor,
            m_wfColorGain, m_wfBlackLevel, m_wfAutoBlack, m_wfLineDuration);
}

VfoWidget* SpectrumWidget::addVfoWidget(int sliceId)
{
    if (m_vfoWidgets.contains(sliceId))
        return m_vfoWidgets[sliceId];

    auto* w = new VfoWidget(this);
    m_vfoWidgets[sliceId] = w;
    w->show();
    w->raise();
    m_overlayMenu->raiseAll();  // keep overlay + panels on top of all VFO widgets
    return w;
}

void SpectrumWidget::removeVfoWidget(int sliceId)
{
    if (auto* w = m_vfoWidgets.take(sliceId)) {
        if (m_vfoWidget == w)
            m_vfoWidget = nullptr;
        delete w;
    }
}

void SpectrumWidget::setActiveVfoWidget(int sliceId)
{
    m_vfoWidget = m_vfoWidgets.value(sliceId, nullptr);
    if (m_vfoWidget) {
        m_vfoWidget->raise();
        m_overlayMenu->raiseAll();  // keep overlay above VFO
    }
}

// ── Display control setters (save to AppSettings on each change) ──────────────

void SpectrumWidget::setBandPlanManager(BandPlanManager* mgr) {
    m_bandPlanMgr = mgr;
    if (mgr)
        connect(mgr, &BandPlanManager::planChanged, this, QOverload<>::of(&QWidget::update));
}

void SpectrumWidget::setFftAverage(int frames) {
    m_fftAverage = frames;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftAverage"), QString::number(frames));
    s.save();
}
void SpectrumWidget::setFftWeightedAvg(bool on) {
    m_fftWeightedAvg = on;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftWeightedAvg"), on ? "True" : "False");
    s.save();
}
void SpectrumWidget::setFftFps(int fps) {
    m_fftFps = fps;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftFps"), QString::number(fps));
    s.save();
}
void SpectrumWidget::setFftFillAlpha(float a) {
    m_fftFillAlpha = std::clamp(a, 0.0f, 1.0f);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftFillAlpha"), QString::number(m_fftFillAlpha, 'f', 2));
    s.save();
    update();
}
void SpectrumWidget::setFftFillColor(const QColor& c) {
    m_fftFillColor = c;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftFillColor"), c.name());
    s.save();
    update();
}
void SpectrumWidget::setWfColorGain(int gain) {
    m_wfColorGain = std::clamp(gain, 0, 100);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayWfColorGain"), QString::number(m_wfColorGain));
    s.save();
    update();
}
void SpectrumWidget::setWfBlackLevel(int level) {
    m_wfBlackLevel = std::clamp(level, 0, 100);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayWfBlackLevel"), QString::number(m_wfBlackLevel));
    s.save();
    update();
}
void SpectrumWidget::setWfAutoBlack(bool on) {
    m_wfAutoBlack = on;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayWfAutoBlack"), on ? "True" : "False");
    s.save();
}
void SpectrumWidget::setWfLineDuration(int ms) {
    m_wfLineDuration = std::clamp(ms, 50, 500);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayWfLineDuration"), QString::number(m_wfLineDuration));
    s.save();
    // Re-calibrate the time scale for the new rate
    resetWfTimeScale();
}

// ── NB Waterfall Blanker setters (#277) ──────────────────────────────────────

void SpectrumWidget::setWfBlankerEnabled(bool on)
{
    m_wfBlankerEnabled = on;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("WaterfallBlankingEnabled"), on ? "True" : "False");
    s.save();
    if (!on) {
        m_wfBlankerRingCount = 0;
        m_wfBlankerRingIdx = 0;
    }
}

void SpectrumWidget::setWfBlankerThreshold(float t)
{
    m_wfBlankerThreshold = std::clamp(t, 1.05f, 2.0f);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("WaterfallBlankingThreshold"),
              QString::number(m_wfBlankerThreshold, 'f', 2));
    s.save();
}

void SpectrumWidget::setWfBlankerMode(int mode)
{
    m_wfBlankerMode = qBound(0, mode, 1);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("WaterfallBlankingMode"), QString::number(m_wfBlankerMode));
    s.save();
}

void SpectrumWidget::resetWfTimeScale() {
    m_wfMsPerRow = static_cast<float>(m_wfLineDuration);  // start from radio's value
    m_wfPrevTimecode = 0;
    m_wfPrevTimecodeMs = 0;
    m_wfCalibrationCount = 0;
    m_wfTimeScaleLocked = false;
}

// ─────────────────────────────────────────────────────────────────────────────

void SpectrumWidget::clearDisplay()
{
    m_bins.clear();
    m_smoothed.clear();
    if (!m_waterfall.isNull())
        m_waterfall.fill(Qt::black);
    m_wfWriteRow = 0;
    update();
}

void SpectrumWidget::setFrequencyRange(double centerMhz, double bandwidthMhz)
{
    if (centerMhz != m_centerMhz || bandwidthMhz != m_bandwidthMhz)
        qDebug() << "SpectrumWidget::setFrequencyRange center="
                 << QString::number(centerMhz, 'f', 6)
                 << "bw=" << QString::number(bandwidthMhz, 'f', 6)
                 << "bins=" << m_smoothed.size();
    m_centerMhz    = centerMhz;
    m_bandwidthMhz = bandwidthMhz;
    update();
}

void SpectrumWidget::setSpectrumFrac(float f)
{
    m_spectrumFrac = std::clamp(f, 0.10f, 0.90f);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("SpectrumSplitRatio"), QString::number(m_spectrumFrac, 'f', 3));
    s.save();
    update();
}

void SpectrumWidget::setDbmRange(float minDbm, float maxDbm)
{
    m_refLevel     = maxDbm;
    m_dynamicRange = maxDbm - minDbm;
    update();
}

// ─── Slice color table (shared via SliceColors.h) ────────────────────────────

static QColor sliceColor(int sliceId, bool active) {
    const auto& c = kSliceColors[sliceId & 3];
    if (active) return QColor(c.r, c.g, c.b);
    return QColor(c.dr, c.dg, c.db);
}

// ─── Multi-slice overlay management ──────────────────────────────────────────

int SpectrumWidget::overlayIndex(int sliceId) const
{
    for (int i = 0; i < m_sliceOverlays.size(); ++i)
        if (m_sliceOverlays[i].sliceId == sliceId) return i;
    return -1;
}

const SpectrumWidget::SliceOverlay* SpectrumWidget::activeOverlay() const
{
    for (const auto& o : m_sliceOverlays)
        if (o.isActive) return &o;
    return m_sliceOverlays.isEmpty() ? nullptr : &m_sliceOverlays.first();
}

void SpectrumWidget::setSliceOverlay(int sliceId, double freq, int fLow, int fHigh,
                                     bool tx, bool active, const QString& mode,
                                     int rttyMark, int rttyShift,
                                     bool ritOn, int ritFreq,
                                     bool xitOn, int xitFreq)
{
    int idx = overlayIndex(sliceId);
    if (idx < 0) {
        SliceOverlay o;
        o.sliceId = sliceId; o.freqMhz = freq;
        o.filterLowHz = fLow; o.filterHighHz = fHigh;
        o.isTxSlice = tx; o.isActive = active;
        o.mode = mode; o.rttyMark = rttyMark; o.rttyShift = rttyShift;
        o.ritOn = ritOn; o.ritFreq = ritFreq;
        o.xitOn = xitOn; o.xitFreq = xitFreq;
        m_sliceOverlays.append(o);
    } else {
        auto& o = m_sliceOverlays[idx];
        o.freqMhz = freq; o.filterLowHz = fLow; o.filterHighHz = fHigh;
        o.isTxSlice = tx; o.isActive = active;
        o.mode = mode; o.rttyMark = rttyMark; o.rttyShift = rttyShift;
        o.ritOn = ritOn; o.ritFreq = ritFreq;
        o.xitOn = xitOn; o.xitFreq = xitFreq;
    }
    update();
}

void SpectrumWidget::setSliceOverlayFreq(int sliceId, double freqMhz)
{
    for (auto& so : m_sliceOverlays) {
        if (so.sliceId == sliceId) {
            so.freqMhz = freqMhz;
            return;
        }
    }
}

void SpectrumWidget::removeSliceOverlay(int sliceId)
{
    int idx = overlayIndex(sliceId);
    if (idx >= 0) m_sliceOverlays.remove(idx);
    update();
}

void SpectrumWidget::setSplitPair(int rxSliceId, int txSliceId)
{
    // Clear old split markers
    for (auto& so : m_sliceOverlays)
        so.splitPartnerId = -1;

    if (rxSliceId >= 0 && txSliceId >= 0) {
        int rxIdx = overlayIndex(rxSliceId);
        int txIdx = overlayIndex(txSliceId);
        if (rxIdx >= 0) m_sliceOverlays[rxIdx].splitPartnerId = txSliceId;
        if (txIdx >= 0) m_sliceOverlays[txIdx].splitPartnerId = rxSliceId;
    }
    update();
}

// ─── Legacy single-slice convenience wrappers ────────────────────────────────

void SpectrumWidget::setVfoFrequency(double freqMhz)
{
    auto* o = const_cast<SliceOverlay*>(activeOverlay());
    if (o) { o->freqMhz = freqMhz; update(); }
}

void SpectrumWidget::setVfoFilter(int lowHz, int highHz)
{
    auto* o = const_cast<SliceOverlay*>(activeOverlay());
    if (o) { o->filterLowHz = lowHz; o->filterHighHz = highHz; update(); }
}

void SpectrumWidget::setSliceInfo(int sliceId, bool isTxSlice)
{
    int idx = overlayIndex(sliceId);
    if (idx >= 0) { m_sliceOverlays[idx].isTxSlice = isTxSlice; update(); }
}

void SpectrumWidget::updateSpectrum(const QVector<float>& binsDbm)
{
    // Forward to GPU renderer (#502)


    if (m_smoothed.size() != binsDbm.size())
        m_smoothed = binsDbm;
    else {
        for (int i = 0; i < binsDbm.size(); ++i)
            m_smoothed[i] = SMOOTH_ALPHA * binsDbm[i] + (1.0f - SMOOTH_ALPHA) * m_smoothed[i];
    }
    m_bins = binsDbm;

    // Noise floor auto-adjust: every 10 frames, measure noise floor and
    // adjust min_dbm so it sits at the user's chosen position.
    if (m_noiseFloorEnable && !m_smoothed.isEmpty()) {
        if (++m_noiseFloorFrameCount >= 10) {
            m_noiseFloorFrameCount = 0;

            // Compute noise floor as 20th percentile of smoothed bins
            QVector<float> sorted = m_smoothed;
            std::sort(sorted.begin(), sorted.end());
            int idx = sorted.size() / 5;  // 20th percentile
            float noiseFloor = sorted[idx];

            // Position: 0 = noise at top, 100 = noise at bottom
            // noiseFloor should appear at (position/100) of the way down
            float frac = m_noiseFloorPosition / 100.0f;
            // noiseFloor maps to frac in the display:
            //   frac = (refLevel - noiseFloor) / dynamicRange
            //   => dynamicRange = (refLevel - noiseFloor) / frac
            // Keep refLevel (max_dbm) fixed, adjust min_dbm
            if (frac > 0.05f && frac < 0.95f) {
                float newRange = (m_refLevel - noiseFloor) / frac;
                newRange = std::clamp(newRange, 20.0f, 150.0f);
                float newMin = m_refLevel - newRange;
                // Only adjust if change is significant (> 1 dB)
                float currentMin = m_refLevel - m_dynamicRange;
                if (std::abs(newMin - currentMin) > 1.0f) {
                    emit dbmRangeChangeRequested(newMin, m_refLevel);
                }
            }
        }
    }

    // Use FFT data for waterfall only when native tiles aren't available.
    // If native tiles stop arriving (e.g., disconnect), fall back after 2 seconds.
    // During TX: immediately use FFT-derived rows (radio stops sending tiles).
    // During RX: use native tiles, fall back to FFT after 2s timeout.
    if (m_transmitting) {
        // TX: push FFT-derived rows only if show-tx-in-waterfall is enabled,
        // or if this pan doesn't contain the TX slice (multi-pan: non-TX pan
        // keeps scrolling regardless).
        bool freeze = !m_showTxInWaterfall && m_hasTxSlice;
        if (!freeze && !m_waterfall.isNull())
            pushWaterfallRow(binsDbm, m_waterfall.width());
    } else {
        if (m_hasNativeWaterfall) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastNativeTileMs > 2000) {
                m_hasNativeWaterfall = false;
                qDebug() << "SpectrumWidget: native waterfall tiles timed out, falling back to FFT-derived";
            }
        }
        if (!m_hasNativeWaterfall && !m_waterfall.isNull())
            pushWaterfallRow(binsDbm, m_waterfall.width());
    }

    update();
}

void SpectrumWidget::updateWaterfallRow(const QVector<float>& binsIntensity,
                                        double lowFreqMhz, double highFreqMhz,
                                        quint32 timecode)
{
    // Native waterfall tiles carry intensity values (int16/128.0f, ~96-120 on HF).
    if (binsIntensity.isEmpty()) return;

    // Forward to GPU renderer (#502)


    if (m_waterfall.isNull()) return;

    // Freeze waterfall during TX if show-tx-in-waterfall is off and this pan
    // contains the TX slice. Non-TX pans keep scrolling in multi-pan.
    if (m_transmitting && !m_showTxInWaterfall && m_hasTxSlice) return;

    // Derive ms-per-row by measuring wall-clock / timecode delta.
    // Collect data for the first 50 tiles to converge, then lock the value.
    // Only re-measure when the user changes the waterfall rate slider
    // (which calls resetWfTimeScale()).
    if (!m_wfTimeScaleLocked) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (timecode > 0 && m_wfPrevTimecode > 0 && timecode > m_wfPrevTimecode && m_wfPrevTimecodeMs > 0) {
            const float wallDelta = static_cast<float>(now - m_wfPrevTimecodeMs);
            const float tcDelta = static_cast<float>(timecode - m_wfPrevTimecode);
            if (tcDelta > 0 && wallDelta > 0) {
                const float measured = wallDelta / tcDelta;
                m_wfMsPerRow = 0.9f * m_wfMsPerRow + 0.1f * measured;
                if (++m_wfCalibrationCount >= 50) {
                    m_wfTimeScaleLocked = true;  // lock — no more jitter
                }
            }
        }
        if (timecode > 0) {
            m_wfPrevTimecode = timecode;
            m_wfPrevTimecodeMs = now;
        }
    }

    // Client-side auto-black: track the noise floor from tile data and adjust
    // the black threshold to sit just above it. This replaces the radio's
    // auto_black which targets SmartSDR's different rendering engine.
    if (m_wfAutoBlack && !m_transmitting) {
        // Estimate noise floor from incoming tiles.
        // Freeze during TX; threshold is restored to pre-TX value on TX→RX transition.
        // Cheaper than full sort: sample every 8th bin.
        float sum = 0;
        float minVal = 1e9f, maxVal = -1e9f;
        int count = 0;
        for (int i = 0; i < binsIntensity.size(); i += 8) {
            float v = binsIntensity[i];
            sum += v;
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            count++;
        }
        if (count > 0) {
            float mean = sum / count;
            // Use mean as noise floor estimate (most bins are noise)
            // Set threshold below mean so noise shows as faint dark blue
            float target = mean - 3.0f;
            // Smooth to prevent jitter
            m_autoBlackThresh = 0.95f * m_autoBlackThresh + 0.05f * target;
        }
    }

    // One pixel row per tile — scroll speed determined by tile arrival rate.
    int rowsToPush = 1;

    m_hasNativeWaterfall = true;
    m_lastNativeTileMs = QDateTime::currentMSecsSinceEpoch();

    const int destWidth = m_waterfall.width();
    if (destWidth <= 0) return;

    const int h = m_waterfall.height();
    if (h <= 1) return;
    rowsToPush = std::min(rowsToPush, h - 1);

    // Ring buffer: write new row at m_wfWriteRow, no memmove (#391)
    uchar* bits = m_waterfall.bits();
    const qsizetype bpl = m_waterfall.bytesPerLine();

    // Render the tile row into a temporary scanline.
    // Per FlexRadio community guidance: tiles extend BEYOND the panadapter edges.
    // For each display pixel, calculate its frequency, then find the corresponding
    // tile bin via: binIdx = (freq - tileLowFreq) / binBandwidth.
    const int srcSize = binsIntensity.size();
    const double tileBw = (srcSize > 0) ? (highFreqMhz - lowFreqMhz) / srcSize : 0.0;
    const double panStartMhz = m_centerMhz - m_bandwidthMhz / 2.0;

    QVector<QRgb> scanline(destWidth, qRgb(0, 0, 0));
    if (tileBw > 0) {
        for (int x = 0; x < destWidth; ++x) {
            const double freq = panStartMhz + (static_cast<double>(x) / destWidth) * m_bandwidthMhz;
            const double binF = (freq - lowFreqMhz) / tileBw;
            const int binIdx = static_cast<int>(binF);
            if (binIdx >= 0 && binIdx < srcSize) {
                // Linear interpolation between adjacent bins
                const float frac = static_cast<float>(binF - binIdx);
                const float i0 = binsIntensity[binIdx];
                const float i1 = (binIdx + 1 < srcSize) ? binsIntensity[binIdx + 1] : i0;
                scanline[x] = intensityToRgb(i0 + frac * (i1 - i0));
            }
        }
    }

    // NB Waterfall Blanker (#277) — suppress impulse rows
    if (m_wfBlankerEnabled) {
        float rowSum = 0.0f;
        const int binCount = binsIntensity.size();
        for (int i = 0; i < binCount; ++i)
            rowSum += binsIntensity[i];
        const float rowMean = (binCount > 0) ? (rowSum / binCount) : 0.0f;

        // Compute rolling baseline
        float baseline = 0.0f;
        for (int i = 0; i < m_wfBlankerRingCount; ++i)
            baseline += m_wfBlankerRing[i];
        if (m_wfBlankerRingCount > 0)
            baseline /= m_wfBlankerRingCount;

        // Detect impulse (need ≥8 rows of history)
        if (m_wfBlankerRingCount >= 8 && baseline > 0.0f
                && rowMean > baseline * m_wfBlankerThreshold) {
            // Impulse detected — replace with last good row (interpolate)
            if (m_wfLastGoodRow.size() == destWidth) {
                scanline = m_wfLastGoodRow;
            } else {
                // No previous good row yet — fill with noise floor color
                const QRgb floorColor = intensityToRgb(baseline);
                std::fill(scanline.begin(), scanline.end(), floorColor);
            }
            m_wfBlankerRing[m_wfBlankerRingIdx] = std::min(rowMean, baseline * 1.05f);
        } else {
            m_wfLastGoodRow = scanline;
            m_wfBlankerRing[m_wfBlankerRingIdx] = rowMean;
        }
        m_wfBlankerRingIdx = (m_wfBlankerRingIdx + 1) % WF_BLANKER_N;
        if (m_wfBlankerRingCount < WF_BLANKER_N)
            ++m_wfBlankerRingCount;
    }

    // Write rows into ring buffer (no memmove)
    const bool canInterp = (m_prevTileScanline.size() == destWidth && rowsToPush > 1);
    for (int r = 0; r < rowsToPush; ++r) {
        m_wfWriteRow = (m_wfWriteRow - 1 + h) % h;
        auto* row = reinterpret_cast<QRgb*>(bits + m_wfWriteRow * bpl);
        if (canInterp) {
            // t=0 at row 0 (current), t=1 at last row (previous)
            const float t = static_cast<float>(r) / rowsToPush;
            for (int x = 0; x < destWidth; ++x) {
                const QRgb c = scanline[x];
                const QRgb p = m_prevTileScanline[x];
                const int cr = qRed(c)   + static_cast<int>(t * (qRed(p)   - qRed(c)));
                const int cg = qGreen(c) + static_cast<int>(t * (qGreen(p) - qGreen(c)));
                const int cb = qBlue(c)  + static_cast<int>(t * (qBlue(p)  - qBlue(c)));
                row[x] = qRgb(cr, cg, cb);
            }
        } else {
            std::memcpy(row, scanline.constData(), destWidth * sizeof(QRgb));
        }
    }
    m_prevTileScanline = scanline;

    update();
}

// ─── Layout helpers ────────────────────────────────────────────────────────────

int SpectrumWidget::mhzToX(double mhz) const
{
    if (m_bandwidthMhz <= 0.0) return -1;
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double px = (mhz - startMhz) / m_bandwidthMhz * width();
    if (std::isnan(px) || std::isinf(px)) return -1;
    return static_cast<int>(std::clamp(px, -1.0e6, 1.0e6));
}

double SpectrumWidget::xToMhz(int x) const
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    return startMhz + (static_cast<double>(x) / width()) * m_bandwidthMhz;
}

// ─── Mouse ────────────────────────────────────────────────────────────────────

// Snap a frequency (MHz) to the nearest multiple of m_stepHz.
static double snapToStep(double mhz, int stepHz)
{
    if (stepHz <= 0) return mhz;
    const double stepMhz = stepHz / 1e6;
    return std::round(mhz / stepMhz) * stepMhz;
}

void SpectrumWidget::mousePressEvent(QMouseEvent* ev)
{
    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = height() - chromeH;
    const int specH = static_cast<int>(contentH * m_spectrumFrac);
    const int y = static_cast<int>(ev->position().y());

    // Save press position for single-click-to-tune drag threshold
    if (ev->button() == Qt::LeftButton)
        m_clickPressPos = ev->position().toPoint();

    // Click on a spot label → tune to that frequency
    if (m_showSpots && ev->button() == Qt::LeftButton) {
        const QPoint pos(static_cast<int>(ev->position().x()), y);
        for (const auto& hr : m_spotClickRects) {
            if (hr.rect.contains(pos)) {
                emit frequencyClicked(hr.freqMhz);
                // Notify the radio that a spot was clicked (#341)
                if (hr.markerIndex >= 0 && hr.markerIndex < m_spotMarkers.size())
                    emit spotTriggered(m_spotMarkers[hr.markerIndex].index);
                m_spotClickConsumed = true;  // suppress release-to-tune (#530)
                ev->accept();
                return;
            }
        }
        // Click on a cluster badge → show popup with collapsed spots
        for (const auto& cluster : m_spotClusters) {
            if (cluster.rect.contains(pos)) {
                showSpotClusterPopup(cluster, mapToGlobal(pos));
                ev->accept();
                return;
            }
        }
    }

    // Click on the divider bar → start split drag
    if (y >= specH && y < specH + DIVIDER_H) {
        m_draggingDivider = true;
        setCursor(Qt::SplitVCursor);
        ev->accept();
        return;
    }

    // Click on the freq scale bar → start bandwidth drag
    const int scaleY = specH + DIVIDER_H;
    if (y >= scaleY && y < scaleY + FREQ_SCALE_H) {
        m_draggingBandwidth = true;
        m_bwDragStartX = static_cast<int>(ev->position().x());
        m_bwDragStartBw = m_bandwidthMhz;
        setCursor(Qt::SizeHorCursor);
        ev->accept();
        return;
    }

    // Left-click in waterfall area → start pan drag (tune on double-click only)
    const int wfY = scaleY + FREQ_SCALE_H;
    if (y >= wfY && ev->button() == Qt::LeftButton) {
        m_draggingPan = true;
        m_panDragStartX = static_cast<int>(ev->position().x());
        m_panDragStartCenter = m_centerMhz;
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }

    // Click on off-screen slice indicator → absorb or switch slice
    for (int oi = 0; oi < m_offScreenRects.size(); ++oi) {
        if (!m_offScreenRects[oi].isNull() &&
            m_offScreenRects[oi].contains(QPoint(static_cast<int>(ev->position().x()), y))) {
            const auto& so = m_sliceOverlays[oi];
            if (!so.isActive) emit sliceClicked(so.sliceId);
            ev->accept();
            return;
        }
    }

    // Check for click on dBm scale strip (right edge of FFT area)
    if (y < specH) {
        const int mx = static_cast<int>(ev->position().x());
        const int stripX = width() - DBM_STRIP_W;

        if (mx >= stripX) {
            // Arrow row (side by side: left = up, right = down)
            if (y < DBM_ARROW_H) {
                const float bottom = m_refLevel - m_dynamicRange;
                if (mx < stripX + DBM_STRIP_W / 2) {
                    // Up arrow: raise ref level by 10 dB, keep bottom fixed
                    m_refLevel += 10.0f;
                } else {
                    // Down arrow: lower ref level by 10 dB, keep bottom fixed
                    m_refLevel -= 10.0f;
                }
                m_dynamicRange = m_refLevel - bottom;
                if (m_dynamicRange < 10.0f) {
                    m_dynamicRange = 10.0f;
                    m_refLevel = bottom + m_dynamicRange;
                }
                update();
                emit dbmRangeChangeRequested(bottom, m_refLevel);
                ev->accept();
                return;
            }
            // Below arrows: start dBm drag (pan reference)
            m_draggingDbm = true;
            m_dbmDragStartY = y;
            m_dbmDragStartRef = m_refLevel;
            setCursor(Qt::SizeVerCursor);
            ev->accept();
            return;
        }
    }

    // Right-click context menu on spectrum or waterfall (cancel any active TNF drag first)
    if (ev->button() == Qt::RightButton) {
        m_draggingTnfId = -1;
        const int mx = static_cast<int>(ev->position().x());
        const double freqMhz = xToMhz(mx);
        const int hitTnf = tnfAtPixel(mx);

        // Check if right-click is on an existing spot label
        int hitSpotIdx = -1;
        QString hitSpotCall;
        double hitSpotFreq = 0;
        for (const auto& hr : m_spotClickRects) {
            if (hr.rect.contains(mx, static_cast<int>(ev->position().y()))) {
                if (hr.markerIndex >= 0 && hr.markerIndex < m_spotMarkers.size()) {
                    const auto& sm = m_spotMarkers[hr.markerIndex];
                    hitSpotIdx = sm.index;
                    hitSpotCall = sm.callsign;
                    hitSpotFreq = sm.freqMhz;
                }
                break;
            }
        }

        QMenu menu(this);

        // Spot-on-label context menu
        if (hitSpotIdx >= 0) {
            menu.addAction(QString("Tune to %1").arg(hitSpotCall), this,
                [this, hitSpotFreq]{ emit frequencyClicked(hitSpotFreq); });
            menu.addAction("Copy Callsign", this, [hitSpotCall]{
                QApplication::clipboard()->setText(hitSpotCall);
            });
            menu.addAction("Lookup on QRZ", this, [hitSpotCall]{
                QDesktopServices::openUrl(QUrl("https://www.qrz.com/db/" + hitSpotCall));
            });
            menu.addSeparator();
            menu.addAction("Remove Spot", this,
                [this, hitSpotIdx]{ emit spotRemoveRequested(hitSpotIdx); });
        }
        // TNF context menu (when clicking on a TNF marker)
        else if (hitTnf >= 0) {
            menu.addAction("Remove TNF", this, [this, hitTnf]{ emit tnfRemoveRequested(hitTnf); });
            auto* widthMenu = menu.addMenu("Width");
            for (int w : {50, 100, 200, 500}) {
                widthMenu->addAction(QString("%1 Hz").arg(w), this,
                    [this, hitTnf, w]{ emit tnfWidthRequested(hitTnf, w); });
            }
            auto* depthMenu = menu.addMenu("Depth");
            depthMenu->addAction("Normal", this, [this, hitTnf]{ emit tnfDepthRequested(hitTnf, 1); });
            depthMenu->addAction("Deep", this, [this, hitTnf]{ emit tnfDepthRequested(hitTnf, 2); });
            depthMenu->addAction("Very Deep", this, [this, hitTnf]{ emit tnfDepthRequested(hitTnf, 3); });
            menu.addSeparator();
            bool isPerm = false;
            for (const auto& t : m_tnfMarkers)
                if (t.id == hitTnf) { isPerm = t.permanent; break; }
            if (isPerm)
                menu.addAction("Make Temporary", this, [this, hitTnf]{ emit tnfPermanentRequested(hitTnf, false); });
            else
                menu.addAction("Make Permanent", this, [this, hitTnf]{ emit tnfPermanentRequested(hitTnf, true); });
        }
        // General area context menu
        else {
            // Snap frequency to step size for spot placement
            double snappedMhz = freqMhz;
            if (m_stepHz > 0) {
                const double stepMhz = m_stepHz / 1e6;
                snappedMhz = std::round(freqMhz / stepMhz) * stepMhz;
            }
            const QString freqStr = QString::number(snappedMhz, 'f', 6);
            menu.addAction(QString("Add Spot at %1 MHz...").arg(freqStr), this,
                [this, snappedMhz]{ showAddSpotDialog(snappedMhz); });
            menu.addAction(QString("Add TNF at %1 MHz").arg(freqStr), this,
                [this, freqMhz]{ emit tnfCreateRequested(freqMhz); });
        }

        // Close Slice option (only when multiple slices exist)
        if (m_sliceOverlays.size() > 1) {
            menu.addSeparator();
            int closestSlice = -1;
            int closestDist = INT_MAX;
            for (const auto& so : m_sliceOverlays) {
                int dist = std::abs(mx - mhzToX(so.freqMhz));
                if (dist < closestDist) { closestDist = dist; closestSlice = so.sliceId; }
            }
            if (closestSlice >= 0) {
                const QChar letter = QChar('A' + (closestSlice & 3));
                menu.addAction(QString("Close Slice %1").arg(letter), this,
                    [this, closestSlice]{ emit sliceCloseRequested(closestSlice); });
            }
        }

        menu.exec(ev->globalPosition().toPoint());
        ev->accept();
        return;
    }

    // Check for click on TNF marker in FFT area → start drag
    if (ev->button() == Qt::LeftButton && y < specH) {
        const int mx = static_cast<int>(ev->position().x());
        const int hitTnf = tnfAtPixel(mx);
        if (hitTnf >= 0) {
            m_draggingTnfId = hitTnf;
            for (const auto& t : m_tnfMarkers) {
                if (t.id == hitTnf) { m_dragTnfOrigFreq = t.freqMhz; break; }
            }
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
    }

    // Check for click near an inactive slice marker or its badge — switch active
    if (y < specH) {
        const int mx = static_cast<int>(ev->position().x());
        for (const auto& so : m_sliceOverlays) {
            if (so.isActive) continue;
            int sliceX = mhzToX(so.freqMhz);
            // Slice badge area in top 25px
            if (mx >= sliceX - 8 && mx <= sliceX + 35 && y <= 25) {
                emit sliceClicked(so.sliceId);
                ev->accept();
                return;
            }
            // Center line anywhere vertically
            if (std::abs(mx - sliceX) <= 8) {
                emit sliceClicked(so.sliceId);
                ev->accept();
                return;
            }
        }
    }

    // Check for click on filter edges in FFT area (5px grab zone)
    if (y < specH) {
        const auto* ao = activeOverlay();
        if (!ao) { ev->accept(); return; }
        const int mx = static_cast<int>(ev->position().x());
        const int loX = mhzToX(ao->freqMhz + ao->filterLowHz / 1.0e6);
        const int hiX = mhzToX(ao->freqMhz + ao->filterHighHz / 1.0e6);
        constexpr int GRAB = 5;

        if (std::abs(mx - loX) <= GRAB) {
            m_draggingFilter = FilterEdge::Low;
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
        if (std::abs(mx - hiX) <= GRAB) {
            m_draggingFilter = FilterEdge::High;
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }

        // Click inside the filter passband → start VFO drag (#404)
        const int left = std::min(loX, hiX);
        const int right = std::max(loX, hiX);
        if (mx > left + GRAB && mx < right - GRAB) {
            m_draggingVfo = true;
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
    }

    // Click in FFT area → start pan drag (tune on double-click only)
    m_draggingPan = true;
    m_panDragStartX = static_cast<int>(ev->position().x());
    m_panDragStartCenter = m_centerMhz;
    setCursor(Qt::ClosedHandCursor);
    ev->accept();
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* ev)
{
    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = height() - chromeH;
    const int y = static_cast<int>(ev->position().y());

    // TNF drag
    if (m_draggingTnfId >= 0) {
        const int mx = static_cast<int>(ev->position().x());
        const double newFreq = xToMhz(mx);
        for (auto& t : m_tnfMarkers) {
            if (t.id == m_draggingTnfId) { t.freqMhz = newFreq; break; }
        }
        update();
        ev->accept();
        return;
    }

    if (m_draggingDivider) {
        // Clamp the divider position: 10%–90% of content area
        float frac = static_cast<float>(y) / contentH;
        m_spectrumFrac = std::clamp(frac, 0.10f, 0.90f);
        // Rebuild waterfall image for new size
        const int wfHeight = static_cast<int>(contentH * (1.0f - m_spectrumFrac));
        if (wfHeight > 0 && width() > 0) {
            QImage newWf(width(), wfHeight, QImage::Format_RGB32);
            newWf.fill(Qt::black);
            if (!m_waterfall.isNull())
                newWf = m_waterfall.scaled(width(), wfHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            m_waterfall = std::move(newWf);
            m_wfWriteRow = 0;
        }
        update();
        ev->accept();
        return;
    }

    if (m_draggingDbm) {
        const int dy = y - m_dbmDragStartY;
        const int specH = static_cast<int>(contentH * m_spectrumFrac);
        // Convert pixel drag to dB: full FFT height = full dynamic range
        const float deltaDb = (static_cast<float>(dy) / specH) * m_dynamicRange;
        m_refLevel = m_dbmDragStartRef + deltaDb;
        update();
        emit dbmRangeChangeRequested(m_refLevel - m_dynamicRange, m_refLevel);
        ev->accept();
        return;
    }

    if (m_draggingBandwidth) {
        const int dx = static_cast<int>(ev->position().x()) - m_bwDragStartX;
        // 4x multiplier: dragging 1/4 of widget width doubles/halves bandwidth
        const double scale = std::pow(2.0, static_cast<double>(-dx) / (width() / 4.0));
        const double newBw = std::clamp(m_bwDragStartBw * scale, 0.004, 14.0);
        // SSB modes: center on filter midpoint so the passband stays visible.
        // Other modes: center on VFO frequency.
        double zoomCenter = m_centerMhz;
        if (const auto* ao = activeOverlay()) {
            if (m_mode == "USB" || m_mode == "LSB" || m_mode == "DIGU" || m_mode == "DIGL" || m_mode == "RTTY") {
                zoomCenter = ao->freqMhz + (ao->filterLowHz + ao->filterHighHz) / 2.0 / 1.0e6;
            } else {
                zoomCenter = ao->freqMhz;
            }
        }
        m_bandwidthMhz = newBw;
        m_centerMhz = zoomCenter;
        update();
        emit bandwidthChangeRequested(newBw);
        emit centerChangeRequested(zoomCenter);
        ev->accept();
        return;
    }

    if (m_draggingFilter != FilterEdge::None) {
        auto* ao = const_cast<SliceOverlay*>(activeOverlay());
        if (!ao) { m_draggingFilter = FilterEdge::None; return; }
        const int mx = static_cast<int>(ev->position().x());
        const double mhz = xToMhz(mx);
        int hz = static_cast<int>(std::round((mhz - ao->freqMhz) * 1.0e6));

        if (m_draggingFilter == FilterEdge::Low) {
            hz = std::clamp(hz, m_filterMinHz, ao->filterHighHz - 10);
            ao->filterLowHz = hz;
        } else {
            hz = std::clamp(hz, ao->filterLowHz + 10, m_filterMaxHz);
            ao->filterHighHz = hz;
        }
        update();
        emit filterChangeRequested(ao->filterLowHz, ao->filterHighHz);
        ev->accept();
        return;
    }

    if (m_draggingVfo) {
        const int mx = static_cast<int>(ev->position().x());
        const double mhz = snapToStep(xToMhz(mx), m_stepHz);
        emit frequencyClicked(mhz);
        ev->accept();
        return;
    }

    if (m_draggingPan) {
        const int dx = static_cast<int>(ev->position().x()) - m_panDragStartX;
        // Dragging right moves the view right → center shifts left
        const double deltaMhz = -(static_cast<double>(dx) / width()) * m_bandwidthMhz;
        const double newCenter = m_panDragStartCenter + deltaMhz;
        m_centerMhz = newCenter;
        update();
        emit centerChangeRequested(newCenter);
        ev->accept();
        return;
    }

    // Update cursor based on hover position
    const int specH = static_cast<int>(contentH * m_spectrumFrac);
    const int wfY = specH + DIVIDER_H + FREQ_SCALE_H;

    if (y >= specH && y < specH + DIVIDER_H) {
        setCursor(Qt::SplitVCursor);
    } else if (y >= specH + DIVIDER_H && y < wfY) {
        setCursor(Qt::SizeHorCursor);
    } else if (y < specH) {
        const int mx = static_cast<int>(ev->position().x());
        const QPoint pos(mx, y);

        // Check off-screen slice indicator hover
        int oldHover = m_hoveringOffScreenIdx;
        m_hoveringOffScreenIdx = -1;
        for (int oi = 0; oi < m_offScreenRects.size(); ++oi) {
            if (!m_offScreenRects[oi].isNull() && m_offScreenRects[oi].contains(pos)) {
                m_hoveringOffScreenIdx = oi; break;
            }
        }
        if (m_hoveringOffScreenIdx != oldHover) update();

        if (m_hoveringOffScreenIdx >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else {
            const int stripX = width() - DBM_STRIP_W;

            // Hovering over dBm scale strip
            if (mx >= stripX) {
                if (y < DBM_ARROW_H)
                    setCursor(Qt::PointingHandCursor);
                else
                    setCursor(Qt::SizeVerCursor);
            } else {
                // Check if hovering over a filter edge or inactive slice marker
                bool foundCursor = false;
                if (const auto* ao = activeOverlay()) {
                    const int loX = mhzToX(ao->freqMhz + ao->filterLowHz / 1.0e6);
                    const int hiX = mhzToX(ao->freqMhz + ao->filterHighHz / 1.0e6);
                    constexpr int GRAB = 5;
                    if (std::abs(mx - loX) <= GRAB || std::abs(mx - hiX) <= GRAB) {
                        setCursor(Qt::SizeHorCursor);
                        foundCursor = true;
                    }
                }
                if (!foundCursor) {
                    // Check inactive slice markers + badges
                    for (const auto& so : m_sliceOverlays) {
                        if (so.isActive) continue;
                        int sliceX = mhzToX(so.freqMhz);
                        if ((mx >= sliceX - 8 && mx <= sliceX + 35 && y <= 25)
                            || std::abs(mx - sliceX) <= 8) {
                            setCursor(Qt::PointingHandCursor);
                            foundCursor = true;
                            break;
                        }
                    }
                }
                if (!foundCursor && m_showSpots) {
                    bool spotHover = false;
                    for (const auto& hr : m_spotClickRects) {
                        if (hr.rect.contains(pos)) {
                            setCursor(Qt::PointingHandCursor);
                            foundCursor = true;
                            // Show spot tooltip
                            if (hr.markerIndex >= 0 && hr.markerIndex < m_spotMarkers.size()) {
                                const auto& sm = m_spotMarkers[hr.markerIndex];
                                QString tip = QString("<b>%1</b>  %2 MHz")
                                    .arg(sm.callsign)
                                    .arg(sm.freqMhz, 0, 'f', 4);
                                if (!sm.source.isEmpty())
                                    tip += QString("<br>Source: %1").arg(sm.source);
                                if (!sm.spotterCallsign.isEmpty())
                                    tip += QString("<br>Spotter: %1").arg(sm.spotterCallsign);
                                if (!sm.comment.isEmpty())
                                    tip += QString("<br>%1").arg(sm.comment);
                                if (sm.timestampMs > 0)
                                    tip += QString("<br>Spotted: %1 UTC").arg(
                                        QDateTime::fromMSecsSinceEpoch(sm.timestampMs, QTimeZone::utc()).toString("yyyy-MM-dd HH:mm:ss"));
                                QToolTip::showText(ev->globalPosition().toPoint(), tip, this);
                            }
                            spotHover = true;
                            break;
                        }
                    }
                    if (!spotHover)
                        QToolTip::hideText();
                    if (!foundCursor) {
                        for (const auto& cluster : m_spotClusters) {
                            if (cluster.rect.contains(pos)) {
                                setCursor(Qt::PointingHandCursor);
                                foundCursor = true;
                                break;
                            }
                        }
                    }
                }
                if (!foundCursor) setCursor(Qt::CrossCursor);
            }
        }
    } else {
        setCursor(Qt::CrossCursor);
    }

    // Band plan spot tooltip on hover
    const int specH2 = static_cast<int>((height() - FREQ_SCALE_H - DIVIDER_H) * m_spectrumFrac);
    const int bandBarTop = specH2 - 8;
    if (y >= bandBarTop && y <= specH2) {
        const int mx2 = static_cast<int>(ev->position().x());
        const auto& spots = m_bandPlanMgr ? m_bandPlanMgr->spots() : QVector<BandPlanManager::Spot>{};
        for (const auto& spot : spots) {
            const int sx = mhzToX(spot.freqMhz);
            if (std::abs(mx2 - sx) <= 5) {
                QToolTip::showText(ev->globalPosition().toPoint(),
                    QString("%1 MHz — %2")
                        .arg(spot.freqMhz, 0, 'f', 3)
                        .arg(spot.label),
                    this);
                return;
            }
        }
        QToolTip::hideText();
    }
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_draggingTnfId >= 0) {
        const int id = m_draggingTnfId;
        m_draggingTnfId = -1;
        setCursor(Qt::CrossCursor);
        // Only emit move if the TNF still exists in our markers
        bool exists = false;
        for (const auto& t : m_tnfMarkers)
            if (t.id == id) { exists = true; break; }
        if (exists) {
            const int mx = static_cast<int>(ev->position().x());
            emit tnfMoveRequested(id, xToMhz(mx));
        }
        ev->accept();
        return;
    }
    if (m_draggingDivider) {
        m_draggingDivider = false;
        setCursor(Qt::CrossCursor);
        auto& s = AppSettings::instance();
        s.setValue(settingsKey("SpectrumSplitRatio"), QString::number(m_spectrumFrac, 'f', 3));
        s.save();
        ev->accept();
        return;
    }
    if (m_draggingDbm) {
        m_draggingDbm = false;
        setCursor(Qt::CrossCursor);
        ev->accept();
        return;
    }
    if (m_draggingBandwidth) {
        m_draggingBandwidth = false;
        setCursor(Qt::CrossCursor);
        ev->accept();
        return;
    }
    if (m_draggingVfo) {
        m_draggingVfo = false;
        setCursor(Qt::CrossCursor);
        ev->accept();
        return;
    }
    if (m_draggingFilter != FilterEdge::None) {
        m_draggingFilter = FilterEdge::None;
        setCursor(Qt::CrossCursor);
        ev->accept();
        return;
    }
    if (m_draggingPan) {
        m_draggingPan = false;
        setCursor(Qt::CrossCursor);

        // Single-click-to-tune: if the mouse didn't move during the
        // "pan drag", treat it as a click-to-tune instead
        if (m_singleClickTune && ev->button() == Qt::LeftButton && !m_spotClickConsumed) {
            QPoint delta = ev->position().toPoint() - m_clickPressPos;
            if (delta.manhattanLength() <= 4) {
                const int mx = static_cast<int>(ev->position().x());
                if (mx < width() - DBM_STRIP_W) {
                    double rawMhz = xToMhz(mx);
                    emit frequencyClicked(snapToStep(rawMhz, m_stepHz));
                }
            }
        }
        m_spotClickConsumed = false;
        ev->accept();
        return;
    }

    // Single-click-to-tune in FFT area (not consumed by pan drag)
    if (m_singleClickTune && ev->button() == Qt::LeftButton && !m_spotClickConsumed) {
        QPoint delta = ev->position().toPoint() - m_clickPressPos;
        if (delta.manhattanLength() <= 4) {
            const int mx = static_cast<int>(ev->position().x());
            if (mx < width() - DBM_STRIP_W) {
                double rawMhz = xToMhz(mx);
                emit frequencyClicked(snapToStep(rawMhz, m_stepHz));
                ev->accept();
                m_spotClickConsumed = false;
                return;
            }
        }
    }
    m_spotClickConsumed = false;
}

void SpectrumWidget::showAddSpotDialog(double freqMhz)
{
    // Snap to step size
    if (m_stepHz > 0) {
        const double stepMhz = m_stepHz / 1e6;
        freqMhz = std::round(freqMhz / stepMhz) * stepMhz;
    }
    auto& as = AppSettings::instance();
    QDialog dlg(this);
    dlg.setWindowTitle("Add Spot");
    dlg.setStyleSheet("QDialog { background: #1a1a2e; color: #c8d8e8; }"
                      "QLineEdit { background: #0f0f1a; color: #c8d8e8; border: 1px solid #304060; padding: 4px; }"
                      "QComboBox { background: #0f0f1a; color: #c8d8e8; border: 1px solid #304060; padding: 4px; }"
                      "QLabel { color: #c8d8e8; }"
                      "QCheckBox { color: #c8d8e8; }");

    auto* layout = new QFormLayout(&dlg);

    auto* freqLabel = new QLabel(QString("%1 MHz").arg(freqMhz, 0, 'f', 6));
    layout->addRow("Frequency:", freqLabel);

    auto* callEdit = new QLineEdit;
    callEdit->setPlaceholderText("Callsign (required)");
    layout->addRow("Callsign:", callEdit);

    auto* commentEdit = new QLineEdit;
    commentEdit->setPlaceholderText("Optional comment");
    layout->addRow("Comment:", commentEdit);

    auto* lifetimeCombo = new QComboBox;
    lifetimeCombo->addItem("5 minutes", 300);
    lifetimeCombo->addItem("15 minutes", 900);
    lifetimeCombo->addItem("30 minutes", 1800);
    lifetimeCombo->addItem("1 hour", 3600);
    lifetimeCombo->addItem("2 hours", 7200);
    int defaultLifetime = as.value("ManualSpotLifetime", 1800).toInt();
    for (int i = 0; i < lifetimeCombo->count(); ++i) {
        if (lifetimeCombo->itemData(i).toInt() == defaultLifetime) {
            lifetimeCombo->setCurrentIndex(i);
            break;
        }
    }
    layout->addRow("Lifetime:", lifetimeCombo);

    auto* forwardCheck = new QCheckBox("Forward to DX Cluster");
    forwardCheck->setChecked(as.value("SpotForwardToCluster", "False").toString() == "True");
    layout->addRow("", forwardCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString callsign = callEdit->text().trimmed().toUpper();
    if (callsign.isEmpty()) return;

    const QString comment = commentEdit->text().trimmed();
    const int lifetimeSec = lifetimeCombo->currentData().toInt();
    const bool forward = forwardCheck->isChecked();

    // Remember preferences
    as.setValue("ManualSpotLifetime", QString::number(lifetimeSec));
    as.setValue("SpotForwardToCluster", forward ? "True" : "False");

    emit spotAddRequested(freqMhz, callsign, comment, lifetimeSec, forward);
}

void SpectrumWidget::mouseDoubleClickEvent(QMouseEvent* ev)
{
    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = height() - chromeH;
    const int specH = static_cast<int>(contentH * m_spectrumFrac);
    const int wfY = specH + DIVIDER_H + FREQ_SCALE_H;
    const int y = static_cast<int>(ev->position().y());
    const int mx = static_cast<int>(ev->position().x());

    // Consume double-clicks on the dBm and time strips
    if (mx >= width() - DBM_STRIP_W) {
        ev->accept();
        return;
    }

    // Double-click on off-screen slice indicator → recenter on that slice
    for (int oi = 0; oi < m_offScreenRects.size(); ++oi) {
        if (!m_offScreenRects[oi].isNull() && m_offScreenRects[oi].contains(QPoint(mx, y))) {
            m_centerMhz = m_sliceOverlays[oi].freqMhz;
            update();
            emit centerChangeRequested(m_centerMhz);
            ev->accept();
            return;
        }
    }

    // Double-click in FFT or waterfall → tune to clicked frequency
    if (y < specH || y >= wfY) {
        const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
        double rawMhz = startMhz + (ev->position().x() / width()) * m_bandwidthMhz;

        emit frequencyClicked(snapToStep(rawMhz, m_stepHz));
        ev->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(ev);
}

void SpectrumWidget::wheelEvent(QWheelEvent* ev)
{
    // Skip scroll on the divider + freq scale bar.
    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH2 = height() - chromeH;
    const int specH2 = static_cast<int>(contentH2 * m_spectrumFrac);
    const int chromeTop = specH2;
    const int chromeBot = specH2 + chromeH;
    if (ev->position().y() >= chromeTop && ev->position().y() < chromeBot) {
        ev->ignore();
        return;
    }
    // Consume wheel events on the dBm / time scale strips
    const int mx = static_cast<int>(ev->position().x());
    if (mx >= width() - DBM_STRIP_W) {
        ev->accept();
        return;
    }

    // Handle both trackpad (pixelDelta) and mouse wheel (angleDelta)
    int steps = 0;
    if (!ev->pixelDelta().isNull()) {
        // Trackpad: accumulate pixel delta; 1 step per ~15px
        // Ignore momentum (inertial) scrolling
        if (ev->phase() == Qt::ScrollMomentum) { ev->accept(); return; }
        // Ignore horizontal-dominant swipes
        if (qAbs(ev->pixelDelta().x()) > qAbs(ev->pixelDelta().y())) {
            ev->ignore(); return;
        }
        m_scrollAccum += ev->pixelDelta().y();
        steps = m_scrollAccum / 15;
        m_scrollAccum -= steps * 15;
    } else {
        // Standard mouse wheel: angleDelta is in 1/8° units, one notch = 120.
        // Some desktops (KDE Plasma, Cinnamon) send inflated deltas
        // (e.g. 960 per notch) or multiple rapid events per physical notch.
        // Clamp to ±1 per event AND debounce within 80ms window. (#504, #556)
        m_angleAccum += ev->angleDelta().y();
        steps = m_angleAccum / 120;
        m_angleAccum -= steps * 120;
        steps = qBound(-1, steps, 1);
        if (steps != 0) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastWheelMs < 50) {
                steps = 0;  // debounce: too soon after last step
            } else {
                m_lastWheelMs = now;
            }
        }
    }
    if (steps == 0) { ev->ignore(); return; }

    const auto* ao = activeOverlay();
    const double vfoMhz = ao ? ao->freqMhz : m_centerMhz;
    const double newMhz = snapToStep(vfoMhz + steps * m_stepHz / 1e6, m_stepHz);
    emit frequencyClicked(newMhz);
    ev->accept();
}

// ─── Resize ───────────────────────────────────────────────────────────────────

void SpectrumWidget::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);

    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = height() - chromeH;
    const int wfHeight = static_cast<int>(contentH * (1.0f - m_spectrumFrac));
    if (wfHeight > 0 && width() > 0) {
        QImage newWf(width(), wfHeight, QImage::Format_RGB32);
        newWf.fill(Qt::black);
        if (!m_waterfall.isNull())
            newWf = m_waterfall.scaled(width(), wfHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        m_waterfall = newWf;
        m_wfWriteRow = 0;
    }

    // Position GPU renderer to cover FFT + waterfall area


    positionZoomButtons();
}

void SpectrumWidget::positionZoomButtons()
{
    constexpr int pad = 4;
    constexpr int sz = 22;
    const int botY = height() - pad;

    // S | B at bottom-left
    m_zoomSegBtn->move(pad, botY - sz);
    m_zoomBandBtn->move(pad + sz + 2, botY - sz);
}

// ─── Colour map ───────────────────────────────────────────────────────────────

QRgb SpectrumWidget::dbmToRgb(float dbm) const
{
    // Black level shifts the floor: higher black_level = more of the noise is black.
    // Color gain controls the visible range: higher gain = narrower range = more contrast.
    // black_level 0-125: higher = floor moves closer to signals (more black)
    // color_gain 0-100: higher = narrower visible range = more contrast
    const float floorShift = (125 - m_wfBlackLevel) * 0.4f;  // inverted: 0=max shift, 125=no shift
    const float visRange = 80.0f - m_wfColorGain * 0.7f;  // 80 dB down to 10 dB
    const float effectiveMin = m_wfMinDbm + floorShift;
    const float effectiveMax = effectiveMin + visRange;

    const float t = qBound(0.0f, (dbm - effectiveMin) / (effectiveMax - effectiveMin), 1.0f);

    // Multi-stop gradient: black → blue → cyan → green → yellow → red
    struct Stop { float pos; int r, g, b; };
    static constexpr Stop stops[] = {
        {0.00f,   0,   0,   0},   // black  (noise floor)
        {0.15f,   0,   0, 128},   // dark blue
        {0.30f,   0,  64, 255},   // blue
        {0.45f,   0, 200, 255},   // cyan
        {0.60f,   0, 220,   0},   // green
        {0.80f, 255, 255,   0},   // yellow
        {1.00f, 255,   0,   0},   // red     (strong signal)
    };
    static constexpr int N = sizeof(stops) / sizeof(stops[0]);

    // Find the two stops bracketing t and interpolate.
    int i = 0;
    while (i < N - 2 && stops[i + 1].pos < t) ++i;
    const float seg = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    const int r = static_cast<int>(stops[i].r + seg * (stops[i + 1].r - stops[i].r));
    const int g = static_cast<int>(stops[i].g + seg * (stops[i + 1].g - stops[i].g));
    const int b = static_cast<int>(stops[i].b + seg * (stops[i + 1].b - stops[i].b));
    return qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

// Map native waterfall tile intensity to RGB.
// Intensity is int16(raw)/128.0f — observed range ~96-120 on HF.
// m_wfBlackLevel and m_wfColorGain control the mapping independently from FFT.
QRgb SpectrumWidget::intensityToRgb(float intensity) const
{
    // Map black_level (0-100) to an intensity threshold.
    // When auto-black is on, use the measured noise floor instead.
    float blackThresh;
    if (m_wfAutoBlack) {
        blackThresh = m_autoBlackThresh;
    } else {
        // Manual: slider 0 → thresh 160 (well above noise), slider 100 → thresh 60.
        blackThresh = 160.0f - m_wfBlackLevel * 1.0f;
    }

    // Map color_gain (0-100) to the visible range width.
    // Higher gain = narrower range = more color contrast.
    // gain=0 → 120 dB range (very dim), gain=100 → 29 dB range (max contrast)
    const float rangeWidth = std::max(1.0f, 120.0f - m_wfColorGain * 0.91f);

    const float t = qBound(0.0f, (intensity - blackThresh) / rangeWidth, 1.0f);

    // Same gradient as dbmToRgb
    struct Stop { float pos; int r, g, b; };
    static constexpr Stop stops[] = {
        {0.00f,   0,   0,   0},
        {0.15f,   0,   0, 128},
        {0.30f,   0,  64, 255},
        {0.45f,   0, 200, 255},
        {0.60f,   0, 220,   0},
        {0.80f, 255, 255,   0},
        {1.00f, 255,   0,   0},
    };
    static constexpr int N = sizeof(stops) / sizeof(stops[0]);

    int i = 0;
    while (i < N - 2 && stops[i + 1].pos < t) ++i;
    const float seg = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    const int r = static_cast<int>(stops[i].r + seg * (stops[i + 1].r - stops[i].r));
    const int g = static_cast<int>(stops[i].g + seg * (stops[i + 1].g - stops[i].g));
    const int b = static_cast<int>(stops[i].b + seg * (stops[i + 1].b - stops[i].b));
    return qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

// ─── Waterfall update ─────────────────────────────────────────────────────────

void SpectrumWidget::pushWaterfallRow(const QVector<float>& bins, int destWidth,
                                      double tileLowMhz, double tileHighMhz)
{
    // (time scale uses m_wfLineDuration from radio status — no measurement needed)

    if (m_waterfall.isNull() || destWidth <= 0) return;

    const int h = m_waterfall.height();
    if (h <= 1) return;

    // Ring buffer: write new row at m_wfWriteRow, no memmove (#391)
    uchar* bits = m_waterfall.bits();
    const qsizetype bpl = m_waterfall.bytesPerLine();

    m_wfWriteRow = (m_wfWriteRow - 1 + h) % h;
    auto* row = reinterpret_cast<QRgb*>(bits + m_wfWriteRow * bpl);

    Q_UNUSED(tileLowMhz);
    Q_UNUSED(tileHighMhz);

    for (int x = 0; x < destWidth; ++x) {
        const int binIdx = x * bins.size() / destWidth;
        const float dbm = (binIdx >= 0 && binIdx < bins.size()) ? bins[binIdx] : m_wfMinDbm;
        row[x] = dbmToRgb(dbm);
    }
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    if (width() <= 0 || height() <= FREQ_SCALE_H + DIVIDER_H + 2) return;

    QElapsedTimer frameTimer;
    frameTimer.start();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = height() - chromeH;
    const int specH    = static_cast<int>(contentH * m_spectrumFrac);
    const int wfH      = contentH - specH;

    const int divY     = specH;
    const int scaleY   = specH + DIVIDER_H;
    const int wfY      = scaleY + FREQ_SCALE_H;

    const QRect specRect (0, 0,       width(), specH);
    const QRect divRect  (0, divY,    width(), DIVIDER_H);
    const QRect scaleRect(0, scaleY,  width(), FREQ_SCALE_H);
    const QRect wfRect   (0, wfY,     width(), wfH);

    {
        // Software fallback: full QPainter rendering
        p.fillRect(specRect, QColor(0x0a, 0x0a, 0x14));
        drawGrid(p, specRect);
        drawSpectrum(p, specRect);
        if (m_bandPlanFontSize > 0) drawBandPlan(p, specRect);
        drawDbmScale(p, specRect);

        p.fillRect(divRect, QColor(0x18, 0x28, 0x38));
        p.setPen(QColor(m_draggingDivider ? 0x00b4d8 : 0x304050));
        p.drawLine(divRect.left(), divRect.center().y(), divRect.right(), divRect.center().y());

        drawFreqScale(p, scaleRect);
        drawWaterfall(p, wfRect);
        drawTimeScale(p, wfRect);
        drawTnfMarkers(p, specRect, wfRect);
        if (m_showSpots) drawSpotMarkers(p, specRect);
        drawSliceMarkers(p, specRect, wfRect);
        drawOffScreenSlices(p, specRect);
    }

    // Reposition all VFO widgets — deconflict flags so they fly away from each other
    // Split pairs always face each other: RX←  →TX
    {
        // Collect visible VFOs sorted by screen X position
        struct VfoPos { int sliceId; int x; VfoWidget* w; int splitPartner; };
        QVector<VfoPos> vfos;
        for (const auto& so : m_sliceOverlays) {
            if (auto* w = m_vfoWidgets.value(so.sliceId, nullptr))
                vfos.append({so.sliceId, mhzToX(so.freqMhz), w, so.splitPartnerId});
        }
        std::sort(vfos.begin(), vfos.end(), [](const VfoPos& a, const VfoPos& b) {
            return a.x < b.x;
        });

        const int panelW = vfos.isEmpty() ? 0 : vfos[0].w->width();
        const int specW = specRect.width();

        // First pass: assign directions for split pairs
        QMap<int, VfoWidget::FlagDir> dirMap;  // sliceId → direction
        for (int i = 0; i < vfos.size(); ++i) {
            if (vfos[i].splitPartner < 0) continue;
            if (dirMap.contains(vfos[i].sliceId)) continue;  // already assigned

            // Find partner index
            int pi = -1;
            for (int j = 0; j < vfos.size(); ++j) {
                if (vfos[j].sliceId == vfos[i].splitPartner) { pi = j; break; }
            }
            if (pi < 0) continue;

            // Left partner flies left, right partner flies right
            int leftIdx  = (vfos[i].x <= vfos[pi].x) ? i : pi;
            int rightIdx = (leftIdx == i) ? pi : i;
            dirMap[vfos[leftIdx].sliceId]  = VfoWidget::ForceLeft;
            dirMap[vfos[rightIdx].sliceId] = VfoWidget::ForceRight;

            // Edge clamp
            if (vfos[leftIdx].x < panelW)
                dirMap[vfos[leftIdx].sliceId] = VfoWidget::ForceRight;
            if (vfos[rightIdx].x + panelW > specW)
                dirMap[vfos[rightIdx].sliceId] = VfoWidget::ForceLeft;
        }

        // Second pass: assign remaining (non-split) VFOs
        if (vfos.size() == 1) {
            vfos[0].w->updatePosition(vfos[0].x, specRect.top());
        } else {
            for (int i = 0; i < vfos.size(); ++i) {
                VfoWidget::FlagDir dir = VfoWidget::Auto;

                if (dirMap.contains(vfos[i].sliceId)) {
                    // Split pair: use pre-assigned direction
                    dir = dirMap[vfos[i].sliceId];
                } else if (vfos.size() == 2) {
                    dir = (i == 0) ? VfoWidget::ForceLeft : VfoWidget::ForceRight;
                    if (i == 0 && vfos[i].x < panelW) dir = VfoWidget::ForceRight;
                    if (i == 1 && vfos[i].x + panelW > specW) dir = VfoWidget::ForceLeft;
                } else {
                    if (i == 0) {
                        dir = VfoWidget::ForceLeft;
                        if (vfos[i].x < panelW) dir = VfoWidget::ForceRight;
                    } else if (i == vfos.size() - 1) {
                        dir = VfoWidget::ForceRight;
                        if (vfos[i].x + panelW > specW) dir = VfoWidget::ForceLeft;
                    } else {
                        int gapLeft = vfos[i].x - vfos[i-1].x;
                        int gapRight = vfos[i+1].x - vfos[i].x;
                        dir = (gapLeft >= gapRight) ? VfoWidget::ForceLeft : VfoWidget::ForceRight;
                    }
                }

                vfos[i].w->updatePosition(vfos[i].x, specRect.top(), dir);
            }
        }
    }
    // Active widget on top, but overlay stays above all
    if (m_vfoWidget) {
        m_vfoWidget->raise();
        m_overlayMenu->raiseAll();
    }

    // ── WNB / RF Gain indicators (top-right of FFT area) ──────────────────
    if (m_wnbActive || m_rfGainValue != 0) {
        QFont indFont = p.font();
        indFont.setPointSize(18);
        indFont.setBold(true);
        p.setFont(indFont);
        p.setPen(QColor(255, 255, 255, 84));

        const QFontMetrics fm(indFont);
        const int rightEdge = specRect.right() - DBM_STRIP_W - 6;
        const int topY = specRect.top() + fm.ascent() + 2;

        int x = rightEdge;

        // RF Gain (rightmost)
        if (m_rfGainValue != 0) {
            QString gainStr = (m_rfGainValue > 0)
                ? QString("+%1dB").arg(m_rfGainValue)
                : QString("%1dB").arg(m_rfGainValue);
            int gw = fm.horizontalAdvance(gainStr);
            x -= gw;
            p.drawText(x, topY, gainStr);
            x -= 10;  // gap between labels
        }

        // WNB (to the left of RF Gain)
        if (m_wnbActive) {
            int ww = fm.horizontalAdvance("WNB");
            x -= ww;
            p.drawText(x, topY, "WNB");
        }
    }

    qCDebug(lcPerf) << "paintEvent:" << static_cast<int>(frameTimer.elapsed()) << "ms";
}

// ─── Grid ─────────────────────────────────────────────────────────────────────

void SpectrumWidget::drawGrid(QPainter& p, const QRect& r)
{
    const int w = r.width();
    const int h = r.height();

    // Horizontal dB grid lines — adaptive step matching the dBm scale strip
    float rawDbStep = m_dynamicRange / 5.0f;
    float dbStep;
    if      (rawDbStep >= 20.0f) dbStep = 20.0f;
    else if (rawDbStep >= 10.0f) dbStep = 10.0f;
    else if (rawDbStep >= 5.0f)  dbStep = 5.0f;
    else                          dbStep = 2.0f;

    const float bottomDbm = m_refLevel - m_dynamicRange;
    const float firstDb = std::ceil(bottomDbm / dbStep) * dbStep;
    p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
    for (float dbm = firstDb; dbm <= m_refLevel; dbm += dbStep) {
        const float frac = (m_refLevel - dbm) / m_dynamicRange;
        const int y = r.top() + static_cast<int>(frac * h);
        p.drawLine(0, y, w, y);
    }

    // Vertical frequency grid lines — adaptive step matching the scale bar
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;
    const double rawStep  = m_bandwidthMhz / 5.0;
    const double gridMag  = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double gridNorm = rawStep / gridMag;
    double gridStep;
    if      (gridNorm >= 5.0) gridStep = 5.0 * gridMag;
    else if (gridNorm >= 2.0) gridStep = 2.0 * gridMag;
    else                      gridStep = 1.0 * gridMag;
    const double firstLine = std::ceil(startMhz / gridStep) * gridStep;

    p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
    for (double f = firstLine; f <= endMhz; f += gridStep)
        p.drawLine(mhzToX(f), r.top(), mhzToX(f), r.bottom());
}

// ─── Spectrum line ────────────────────────────────────────────────────────────

void SpectrumWidget::drawSpectrum(QPainter& p, const QRect& r)
{
    if (m_smoothed.isEmpty()) {
        p.setPen(QColor(0x00, 0x60, 0x80));
        p.drawText(r, Qt::AlignCenter, "No panadapter data — waiting for radio stream");
        return;
    }

    const int w = r.width();
    const int h = r.height();
    const int n = m_smoothed.size();

    // Build the spectrum line path (data points only)
    QPainterPath linePath;
    bool first = true;

    for (int i = 0; i < n; ++i) {
        const float dbm  = m_smoothed[i];
        const float norm = qBound(0.0f, (m_refLevel - dbm) / m_dynamicRange, 1.0f);
        const int   x    = r.left() + static_cast<int>(static_cast<float>(i) / n * w);
        const int   y    = r.top()  + qMin(static_cast<int>(norm * h), h - 1);

        if (first) { linePath.moveTo(x, y); first = false; }
        else        linePath.lineTo(x, y);
    }

    // Closed fill path (line + bottom edges) for gradient fill only
    QPainterPath fillPath(linePath);
    fillPath.lineTo(r.right(), r.bottom());
    fillPath.lineTo(r.left(),  r.bottom());
    fillPath.closeSubpath();

    const int alphaTop = static_cast<int>(200 * m_fftFillAlpha);
    const int alphaBot = static_cast<int>(60 * m_fftFillAlpha);
    // Derive darker bottom color from fill color
    QColor topColor(m_fftFillColor);
    topColor.setAlpha(alphaTop);
    QColor botColor = m_fftFillColor.darker(300);
    botColor.setAlpha(alphaBot);
    QLinearGradient grad(0, r.top(), 0, r.bottom());
    grad.setColorAt(0.0, topColor);
    grad.setColorAt(1.0, botColor);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillPath(fillPath, grad);
    // Stroke only the spectrum line, not the fill closure
    p.setPen(QPen(m_fftFillColor, 1.5));
    p.drawPath(linePath);
    p.setRenderHint(QPainter::Antialiasing, false);
}

// ─── Waterfall ────────────────────────────────────────────────────────────────

void SpectrumWidget::drawWaterfall(QPainter& p, const QRect& r)
{
    if (m_waterfall.isNull()) {
        p.fillRect(r, Qt::black);
        return;
    }

    // Ring buffer rendering: m_wfWriteRow is the newest row.
    // Draw in two halves: [writeRow..end] then [0..writeRow)
    const int h = m_waterfall.height();
    const int topRows = h - m_wfWriteRow;  // rows from writeRow to bottom of image
    const int botRows = m_wfWriteRow;       // rows from top of image to writeRow

    if (topRows >= h) {
        // writeRow == 0, no split needed
        p.drawImage(r, m_waterfall);
    } else {
        const double scale = static_cast<double>(r.height()) / h;
        const int topH = static_cast<int>(topRows * scale);
        const int botH = r.height() - topH;

        // Top part: newest rows (from writeRow to end of image)
        p.drawImage(QRect(r.x(), r.y(), r.width(), topH),
                    m_waterfall,
                    QRect(0, m_wfWriteRow, m_waterfall.width(), topRows));

        // Bottom part: older rows (from start of image to writeRow)
        if (botRows > 0 && botH > 0) {
            p.drawImage(QRect(r.x(), r.y() + topH, r.width(), botH),
                        m_waterfall,
                        QRect(0, 0, m_waterfall.width(), botRows));
        }
    }
}

// ─── Band plan overlay (bottom 8px of FFT area) ─────────────────────────────

void SpectrumWidget::drawBandPlan(QPainter& p, const QRect& specRect)
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;
    const int bandH = m_bandPlanFontSize + 4;  // scale strip height with font
    const int bandY = specRect.bottom() - bandH + 1;

    const auto& segments = m_bandPlanMgr ? m_bandPlanMgr->segments()
                                         : QVector<BandPlanManager::Segment>{};
    for (const auto& seg : segments) {
        if (seg.highMhz <= startMhz || seg.lowMhz >= endMhz) continue;

        const int x1 = mhzToX(std::max(seg.lowMhz, startMhz));
        const int x2 = mhzToX(std::min(seg.highMhz, endMhz));
        if (x2 <= x1) continue;

        // License class contrast: Extra-only = dim, wider access = brighter
        const QString& lic = seg.license;
        int alpha = 150;
        if (lic == "E")          alpha = 50;
        else if (lic == "E,G")   alpha = 100;
        else if (lic.contains("T")) alpha = 150;
        else if (lic.isEmpty())  alpha = 130; // beacons / no class info

        QColor fill = seg.color;
        fill.setAlpha(alpha);
        p.fillRect(x1, bandY, x2 - x1, bandH, fill);

        // Draw separator lines between adjacent segments
        p.setPen(QColor(0x0f, 0x0f, 0x1a, 200));
        p.drawLine(x1, bandY, x1, bandY + bandH);

        // Label: mode + lowest license class allowed
        if (x2 - x1 > 20) {
            QFont f = p.font();
            f.setPointSize(m_bandPlanFontSize);
            f.setBold(true);
            p.setFont(f);

            // Determine lowest (least restrictive) license class
            QString lowestClass;
            if (lic.contains("T"))       lowestClass = "Tech";
            else if (lic.contains("G"))  lowestClass = "General";
            else if (lic == "E")         lowestClass = "Extra";

            QString label = seg.label;
            if (!lowestClass.isEmpty() && x2 - x1 > 60)
                label = QString("%1 %2").arg(seg.label, lowestClass);

            p.setPen(Qt::white);
            p.drawText(QRect(x1, bandY, x2 - x1, bandH),
                       Qt::AlignCenter, label);
        }
    }

    // Draw single-frequency spot markers (white circles)
    const auto& spots = m_bandPlanMgr ? m_bandPlanMgr->spots()
                                       : QVector<BandPlanManager::Spot>{};
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    for (const auto& spot : spots) {
        if (spot.freqMhz < startMhz || spot.freqMhz > endMhz) continue;
        const int sx = mhzToX(spot.freqMhz);
        p.drawEllipse(QPoint(sx, bandY + bandH / 2), 4, 4);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

// ─── TNF markers ────────────────────────────────────────────────────────────

void SpectrumWidget::setTnfMarkers(const QVector<TnfMarker>& markers)
{
    m_tnfMarkers = markers;
    update();
}

void SpectrumWidget::setSpotMarkers(const QVector<SpotMarker>& markers)
{
    m_spotMarkers = markers;
    update();
}

void SpectrumWidget::setTnfGlobalEnabled(bool on)
{
    m_tnfGlobalEnabled = on;
    update();
}

void SpectrumWidget::drawTnfMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect)
{
    if (m_tnfMarkers.isEmpty()) return;

    const int alpha = m_tnfGlobalEnabled ? 40 : 15;
    const int lineAlpha = m_tnfGlobalEnabled ? 140 : 50;

    for (const auto& tnf : m_tnfMarkers) {
        const int cx = mhzToX(tnf.freqMhz);
        const int halfW = std::max(2, mhzToX(tnf.freqMhz + tnf.widthHz / 2.0e6) - cx);
        const int left = cx - halfW;
        const int right = cx + halfW;

        // Skip if fully off-screen
        if (right < 0 || left > width()) continue;

        // Permanent = green, temporary = yellow
        const int r = tnf.permanent ? 0x30 : 0xff;
        const int g = tnf.permanent ? 0xc0 : 0xc0;
        const int b = tnf.permanent ? 0x30 : 0x00;

        // Shaded fill across spectrum + waterfall
        const QColor fillColor(r, g, b, alpha);
        p.fillRect(left, specRect.top(), right - left, specRect.height(), fillColor);
        p.fillRect(left, wfRect.top(), right - left, wfRect.height(), fillColor);

        // Edge lines
        const QPen edgePen(QColor(r, g, b, lineAlpha), 1, Qt::SolidLine);
        p.setPen(edgePen);
        p.drawLine(left, specRect.top(), left, wfRect.bottom());
        p.drawLine(right, specRect.top(), right, wfRect.bottom());

        // Center triangle (grab handle) at top of spectrum
        const int triH = 8 + tnf.depthDb * 2;  // bigger triangle for deeper notch
        QPolygon tri;
        tri << QPoint(cx - 5, specRect.top())
            << QPoint(cx + 5, specRect.top())
            << QPoint(cx, specRect.top() + triH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(r, g, b, m_tnfGlobalEnabled ? 200 : 80));
        p.drawPolygon(tri);
    }
}

int SpectrumWidget::tnfAtPixel(int x) const
{
    for (const auto& tnf : m_tnfMarkers) {
        int cx = mhzToX(tnf.freqMhz);
        if (std::abs(x - cx) <= 8)
            return tnf.id;
    }
    return -1;
}

// ─── VFO marker (filter passband + tuned frequency line) ──────────────────────

void SpectrumWidget::drawSpotMarkers(QPainter& p, const QRect& specRect)
{
    if (m_spotMarkers.isEmpty()) return;

    QFont spotFont = p.font();
    spotFont.setPixelSize(m_spotFontSize);
    spotFont.setBold(true);
    p.setFont(spotFont);
    const QFontMetrics fm(spotFont);

    // Starting Y position based on percentage setting
    const int startY = specRect.top() + specRect.height() * m_spotStartPct / 100;
    const int th = fm.height() + 2;
    const int maxBottom = startY + th * m_spotMaxLevels;

    // Track label positions to avoid overlap and for click detection
    QVector<QRect> placed;
    m_spotClickRects.clear();
    m_spotClusters.clear();

    // Track which spots overflow (can't be placed within max levels)
    // Key: x pixel position (quantized to label width), Value: list of overflowed spots
    QMap<int, QVector<SpotMarker>> overflowGroups;
    constexpr int ClusterBinWidth = 40;  // pixels — spots within this range cluster together

    for (const auto& spot : m_spotMarkers) {
        const int x = mhzToX(spot.freqMhz);
        if (x < 0 || x > width()) continue;

        // Color priority: override → DXCC → spot-provided → default cyan
        QColor col(0x00, 0xb4, 0xd8);  // default cyan
        if (m_spotOverrideColors) {
            col = m_spotColor;
        } else if (spot.dxccColor.isValid()) {
            col = spot.dxccColor;
        } else if (!spot.color.isEmpty() && spot.color.startsWith('#')) {
            QColor parsed(spot.color);
            if (parsed.isValid()) col = parsed;
        }

        // Draw callsign label
        const QString label = spot.callsign;
        const int tw = fm.horizontalAdvance(label) + 6;

        // Start at configured position, nudge down to avoid overlap.
        // Re-scan from the start after each nudge to handle cases where
        // nudging past label A lands on top of label B.
        QRect labelRect(x - tw / 2, startY, tw, th);
        bool collision = true;
        while (collision) {
            collision = false;
            for (const auto& r : placed) {
                if (labelRect.intersects(r)) {
                    labelRect.moveTop(r.bottom() + 1);
                    collision = true;
                    break;
                }
            }
        }
        // Overflow — collect for cluster badge
        if (labelRect.bottom() > maxBottom) {
            int bin = x / ClusterBinWidth;
            overflowGroups[bin].append(spot);
            continue;
        }

        // Draw vertical tick line from bottom of spectrum up to the label
        p.setPen(QPen(QColor(col.red(), col.green(), col.blue(), 120), 1, Qt::DotLine));
        p.drawLine(x, specRect.bottom(), x, labelRect.bottom());

        placed.append(labelRect);
        int mIdx = static_cast<int>(&spot - &m_spotMarkers[0]);
        m_spotClickRects.append({labelRect, spot.freqMhz, mIdx});

        // Background pill
        int bgAlpha = m_spotBgOpacity * 255 / 100;
        QColor bgCol = m_spotBgColor;
        bgCol.setAlpha(bgAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush(bgCol);
        p.drawRoundedRect(labelRect, 3, 3);

        // Text
        p.setPen(col);
        p.drawText(labelRect, Qt::AlignCenter, label);
    }

    // Draw cluster badges for overflow groups
    if (!overflowGroups.isEmpty()) {
        QFont badgeFont = spotFont;
        badgeFont.setPixelSize(m_spotFontSize - 2);
        p.setFont(badgeFont);
        const QFontMetrics bfm(badgeFont);

        for (auto it = overflowGroups.constBegin(); it != overflowGroups.constEnd(); ++it) {
            const auto& spots = it.value();
            if (spots.isEmpty()) continue;

            // Position badge at average x of the group, at maxBottom
            int avgX = 0;
            for (const auto& s : spots)
                avgX += mhzToX(s.freqMhz);
            avgX /= spots.size();

            const QString badgeText = QString("+%1").arg(spots.size());
            const int bw = bfm.horizontalAdvance(badgeText) + 10;
            QRect badgeRect(avgX - bw / 2, maxBottom + 2, bw, th);

            // Nudge horizontally to avoid overlapping other badges/labels
            for (const auto& r : placed) {
                if (badgeRect.intersects(r))
                    badgeRect.moveLeft(r.right() + 3);
            }
            placed.append(badgeRect);

            // Draw badge with distinct style
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x30, 0x50, 0x70, 200));
            p.drawRoundedRect(badgeRect, 3, 3);

            p.setPen(QColor(0xff, 0xc0, 0x40));  // amber text
            p.drawText(badgeRect, Qt::AlignCenter, badgeText);

            // Store for click detection
            SpotCluster cluster;
            cluster.rect = badgeRect;
            cluster.spots = spots;
            m_spotClusters.append(cluster);
        }

        p.setFont(spotFont);  // restore spot font
    }

    p.setFont(QFont());  // restore default
}

void SpectrumWidget::showSpotClusterPopup(const SpotCluster& cluster, const QPoint& globalPos)
{
    auto* menu = new QMenu(this);
    menu->setStyleSheet(
        "QMenu {"
        "  background: #0f0f1a;"
        "  border: 1px solid #305070;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  color: #c8d8e8;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "}"
        "QMenu::item:selected {"
        "  background: #1a3a5a;"
        "  color: #00b4d8;"
        "}");

    for (const auto& spot : cluster.spots) {
        QString text = QString("%1  %2 kHz")
            .arg(spot.callsign, -10)
            .arg(spot.freqMhz * 1000.0, 0, 'f', 1);
        if (!spot.mode.isEmpty())
            text += "  " + spot.mode;
        auto* action = menu->addAction(text);
        connect(action, &QAction::triggered, this, [this, freq = spot.freqMhz] {
            emit frequencyClicked(freq);
        });
    }

    menu->popup(globalPos);
    // QMenu self-deletes on close with WA_DeleteOnClose
    menu->setAttribute(Qt::WA_DeleteOnClose);
}

void SpectrumWidget::drawSliceMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect)
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;

    // Draw inactive slices first, then active slice on top
    auto drawOne = [&](const SliceOverlay& so) {
        if (so.freqMhz < startMhz || so.freqMhz > endMhz) return;

        const QColor col = sliceColor(so.sliceId, so.isActive);
        const double fLoMhz = so.freqMhz + so.filterLowHz / 1.0e6;
        const double fHiMhz = so.freqMhz + so.filterHighHz / 1.0e6;

        const int vfoX = mhzToX(so.freqMhz);
        const int fX1  = mhzToX(fLoMhz);
        const int fX2  = mhzToX(fHiMhz);
        const int fW   = fX2 - fX1;

        // ── Filter passband shading ──────────────────────────────────────
        // All slices use the same style — color distinguishes them.
        p.fillRect(QRect(fX1, specRect.top(), fW, specRect.height()),
                   QColor(col.red(), col.green(), col.blue(), 35));
        p.fillRect(QRect(fX1, wfRect.top(), fW, wfRect.height()),
                   QColor(col.red(), col.green(), col.blue(), 25));

        // Filter edge lines
        p.setPen(QPen(QColor(col.red(), col.green(), col.blue(), 130), 1));
        p.drawLine(fX1, specRect.top(), fX1, specRect.bottom());
        p.drawLine(fX2, specRect.top(), fX2, specRect.bottom());

        // ── Center line ──────────────────────────────────────────────────
        int markerX = vfoX;

        p.setPen(QPen(QColor(col.red(), col.green(), col.blue(), 220), 2.0));
        p.drawLine(markerX, specRect.top(), markerX, wfRect.bottom());

        // ── Triangle marker at top ───────────────────────────────────────
        const int triHalf = 6;
        const int triH = 10;
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        QPolygon tri;
        tri << QPoint(markerX - triHalf, specRect.top())
            << QPoint(markerX + triHalf, specRect.top())
            << QPoint(markerX, specRect.top() + triH);
        p.drawPolygon(tri);

        // ── RTTY mark/space lines ──────────────────────────────────────
        if (so.mode == "RTTY") {
            const double markMhz  = so.freqMhz + so.rttyMark / 1.0e6;
            const double spaceMhz = markMhz - so.rttyShift / 1.0e6;
            const int markX  = mhzToX(markMhz);
            const int spaceX = mhzToX(spaceMhz);

            QPen rttyPen(QColor(200, 200, 255, 180), 1, Qt::DashLine);
            p.setPen(rttyPen);
            p.drawLine(markX,  specRect.top(), markX,  wfRect.bottom());
            p.drawLine(spaceX, specRect.top(), spaceX, wfRect.bottom());

            // Labels at top
            QFont f = p.font();
            f.setPixelSize(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(200, 200, 255, 220));
            p.drawText(markX + 2,  specRect.top() + 12, "M");
            p.drawText(spaceX + 2, specRect.top() + 12, "S");
        }

        // ── RIT/XIT offset lines ──────────────────────────────────────
        if (so.ritOn && so.ritFreq != 0) {
            const int ritX = mhzToX(so.freqMhz + so.ritFreq / 1.0e6);
            QPen ritPen(QColor(col.red(), col.green(), col.blue(), 160), 1, Qt::DashLine);
            p.setPen(ritPen);
            p.drawLine(ritX, specRect.top(), ritX, wfRect.bottom());
            QFont f = p.font();
            f.setPixelSize(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(col.red(), col.green(), col.blue(), 200));
            p.drawText(ritX + 2, specRect.top() + 12, "R");
        }
        if (so.xitOn && so.xitFreq != 0) {
            const int xitX = mhzToX(so.freqMhz + so.xitFreq / 1.0e6);
            QPen xitPen(QColor(220, 80, 80, 180), 1, Qt::DashLine);
            p.setPen(xitPen);
            p.drawLine(xitX, specRect.top(), xitX, wfRect.bottom());
            QFont f = p.font();
            f.setPixelSize(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(220, 80, 80, 220));
            p.drawText(xitX + 2, specRect.top() + 12, "X");
        }

        // Slice letter badge and TX badge are now rendered by each
        // slice's VfoWidget — no need to draw them on the spectrum.
    };

    // Draw all slices (active last so its marker is on top)
    for (const auto& so : m_sliceOverlays)
        if (!so.isActive) drawOne(so);
    for (const auto& so : m_sliceOverlays)
        if (so.isActive) drawOne(so);
}

// ─── Frequency scale bar ──────────────────────────────────────────────────────

void SpectrumWidget::drawFreqScale(QPainter& p, const QRect& r)
{
    p.fillRect(r, QColor(0x06, 0x06, 0x10));

    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;

    // Pick a step using 1-2-5 sequence to give ~5-10 labels at any zoom level.
    double rawStep = m_bandwidthMhz / 5.0;
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double norm = rawStep / mag;
    double stepMhz;
    if      (norm >= 5.0) stepMhz = 5.0 * mag;
    else if (norm >= 2.0) stepMhz = 2.0 * mag;
    else                  stepMhz = 1.0 * mag;

    const double firstLine = std::ceil(startMhz / stepMhz) * stepMhz;

    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    const QFontMetrics fm(f);

    // Decimal places: enough to distinguish labels at this step size
    int decimals;
    if      (stepMhz < 0.0001) decimals = 6;
    else if (stepMhz < 0.001)  decimals = 5;
    else if (stepMhz < 0.01)   decimals = 4;
    else if (stepMhz < 1.0)    decimals = 3;
    else                        decimals = 2;

    for (double freq = firstLine; freq <= endMhz; freq += stepMhz) {
        const int x = mhzToX(freq);

        // Tick mark
        p.setPen(QColor(0x40, 0x60, 0x80));
        p.drawLine(x, r.top(), x, r.top() + 4);

        const QString label = QString::number(freq, 'f', decimals);
        const int tw = fm.horizontalAdvance(label);
        const int lx = qBound(0, x - tw / 2, width() - tw);

        p.setPen(QColor(0x70, 0x90, 0xb0));
        p.drawText(lx, r.bottom() - 2, label);
    }
}

// ─── dBm scale strip (right edge of FFT area) ────────────────────────────────

void SpectrumWidget::drawDbmScale(QPainter& p, const QRect& specRect)
{
    const int stripX = specRect.right() - DBM_STRIP_W + 1;
    const QRect strip(stripX, specRect.top(), DBM_STRIP_W, specRect.height());

    // Semi-opaque background
    p.fillRect(strip, QColor(0x0a, 0x0a, 0x18, 220));

    // Left border line
    p.setPen(QColor(0x30, 0x40, 0x50));
    p.drawLine(stripX, specRect.top(), stripX, specRect.bottom());

    // ── Up/Down arrows side by side at top ─────────────────────────────
    const int halfW = DBM_STRIP_W / 2;
    const int upCx  = stripX + halfW / 2;       // left half center
    const int dnCx  = stripX + halfW + halfW / 2; // right half center
    const int arrowTop = specRect.top() + 2;
    const int arrowBot = specRect.top() + DBM_ARROW_H - 2;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x60, 0x80, 0xa0));

    // Up arrow (▲) — left side
    QPolygon upTri;
    upTri << QPoint(upCx - 5, arrowBot)
          << QPoint(upCx + 5, arrowBot)
          << QPoint(upCx,     arrowTop);
    p.drawPolygon(upTri);

    // Down arrow (▼) — right side
    QPolygon dnTri;
    dnTri << QPoint(dnCx - 5, arrowTop)
          << QPoint(dnCx + 5, arrowTop)
          << QPoint(dnCx,     arrowBot);
    p.drawPolygon(dnTri);

    // ── dBm labels ───────────────────────────────────────────────────────
    QFont f = p.font();
    f.setPointSize(7);
    p.setFont(f);
    const QFontMetrics fm(f);

    const int labelTop = specRect.top() + DBM_ARROW_H + 4;

    // Use adaptive step: aim for ~4-6 labels
    float rawStep = m_dynamicRange / 5.0f;
    float stepDb;
    if      (rawStep >= 20.0f) stepDb = 20.0f;
    else if (rawStep >= 10.0f) stepDb = 10.0f;
    else if (rawStep >= 5.0f)  stepDb = 5.0f;
    else                        stepDb = 2.0f;

    const float bottomDbm = m_refLevel - m_dynamicRange;
    const float firstLabel = std::ceil(bottomDbm / stepDb) * stepDb;

    for (float dbm = firstLabel; dbm <= m_refLevel; dbm += stepDb) {
        const float frac = (m_refLevel - dbm) / m_dynamicRange;
        const int y = specRect.top() + static_cast<int>(frac * specRect.height());
        if (y < labelTop || y > specRect.bottom() - 5) continue;

        // Tick mark
        p.setPen(QColor(0x50, 0x70, 0x80));
        p.drawLine(stripX, y, stripX + 4, y);

        // Label
        const QString label = QString::number(static_cast<int>(dbm));
        p.setPen(QColor(0x80, 0xa0, 0xb0));
        p.drawText(stripX + 6, y + fm.ascent() / 2, label);
    }
}

// ─── Time scale (right edge of waterfall) ─────────────────────────────────────

void SpectrumWidget::drawTimeScale(QPainter& p, const QRect& wfRect)
{
    const int stripX = wfRect.right() - DBM_STRIP_W + 1;
    const QRect strip(stripX, wfRect.top(), DBM_STRIP_W, wfRect.height());

    // Semi-opaque background
    p.fillRect(strip, QColor(0x0a, 0x0a, 0x18, 220));

    // Left border line
    p.setPen(QColor(0x30, 0x40, 0x50));
    p.drawLine(stripX, wfRect.top(), stripX, wfRect.bottom());

    // Total time depth: use ms-per-row derived from radio tile timecodes.
    // This is the radio's own clock — stable and accurate to actual scroll rate.
    const float msPerRow = m_wfMsPerRow > 0 ? m_wfMsPerRow : 100.0f;
    const float totalSec = wfRect.height() * msPerRow / 1000.0f;
    if (totalSec <= 0) return;

    QFont f = p.font();
    f.setPointSize(7);
    p.setFont(f);
    const QFontMetrics fm(f);

    const float stepSec = 5.0f;

    for (float sec = 0; sec <= totalSec; sec += stepSec) {
        const float frac = sec / totalSec;
        const int y = wfRect.top() + static_cast<int>(frac * wfRect.height());
        if (y > wfRect.bottom() - 5) continue;

        // Tick mark
        p.setPen(QColor(0x50, 0x70, 0x80));
        p.drawLine(stripX, y, stripX + 4, y);

        const QString label = QString("%1s").arg(static_cast<int>(sec));

        p.setPen(QColor(0x80, 0xa0, 0xb0));
        p.drawText(stripX + 6, y + fm.ascent() / 2, label);
    }
}

// ─── Off-screen VFO indicator ─────────────────────────────────────────────────

void SpectrumWidget::drawOffScreenSlices(QPainter& p, const QRect& specRect)
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;

    m_offScreenRects.resize(m_sliceOverlays.size());
    int leftStack = 0, rightStack = 0;  // vertical stacking counters

    for (int oi = 0; oi < m_sliceOverlays.size(); ++oi) {
        const auto& so = m_sliceOverlays[oi];
        m_offScreenRects[oi] = QRect();

        if (so.freqMhz >= startMhz && so.freqMhz <= endMhz) continue;

        const bool isRight = (so.freqMhz > endMhz);
        const QColor col = sliceColor(so.sliceId, so.isActive);
        const QChar letter = QChar('A' + (so.sliceId & 3));

        long long hz = static_cast<long long>(std::round(so.freqMhz * 1e6));
        int mhzPart = static_cast<int>(hz / 1000000);
        int khzPart = static_cast<int>((hz / 1000) % 1000);
        const QString freqStr = QString("%1.%2").arg(mhzPart).arg(khzPart, 3, 10, QChar('0'));

        const int hovAlpha = (oi == m_hoveringOffScreenIdx) ? 230 : (so.isActive ? 180 : 100);
        const int padH = 4;

        QFont bigFont = p.font(); bigFont.setPointSize(16); bigFont.setBold(true);
        QFont smallFont = p.font(); smallFont.setPointSize(10); smallFont.setBold(true);
        const QFontMetrics bigFm(bigFont);
        const QFontMetrics smallFm(smallFont);

        const QString chevron = isRight ? " >" : "< ";
        const QString sliceAndChevron = isRight ? QString(letter) + chevron : chevron + letter;

        int topLineW = bigFm.horizontalAdvance(sliceAndChevron);
        int txW = 0;
        if (so.isTxSlice) { txW = smallFm.horizontalAdvance("TX "); topLineW += txW; }
        const int freqW = smallFm.horizontalAdvance(freqStr);
        const int boxW = std::max(topLineW, freqW) + 2 * padH;
        const int boxH = bigFm.height() + smallFm.height() + 4;

        int& stackCount = isRight ? rightStack : leftStack;
        const int boxY = specRect.top() + 20 + stackCount * (boxH + 4);
        ++stackCount;

        int boxX;
        if (isRight) {
            boxX = specRect.right() - DBM_STRIP_W - boxW - 4;
        } else {
            int leftMargin = 4;
            if (m_overlayMenu && m_overlayMenu->isVisible())
                leftMargin = m_overlayMenu->width() + 2;
            boxX = specRect.left() + leftMargin;
        }

        m_offScreenRects[oi] = QRect(boxX, boxY, boxW, boxH);

        // Draw with slice color
        p.setPen(QColor(col.red(), col.green(), col.blue(), hovAlpha));
        int tx = boxX + padH;
        const int topBaseline = boxY + bigFm.ascent();

        if (isRight) {
            if (so.isTxSlice) { p.setFont(smallFont); p.drawText(tx, boxY + smallFm.ascent(), "TX "); tx += txW; }
            p.setFont(bigFont); p.drawText(tx, topBaseline, sliceAndChevron);
        } else {
            p.setFont(bigFont); p.drawText(tx, topBaseline, sliceAndChevron);
            tx += bigFm.horizontalAdvance(sliceAndChevron);
            if (so.isTxSlice) { p.setFont(smallFont); p.drawText(tx, boxY + smallFm.ascent(), " TX"); }
        }

        p.setFont(smallFont);
        p.setPen(QColor(col.red(), col.green(), col.blue(), hovAlpha));
        const int freqY = topBaseline + smallFm.height() + 2;
        if (isRight) p.drawText(boxX + boxW - padH - freqW, freqY, freqStr);
        else         p.drawText(boxX + padH, freqY, freqStr);
    }
}

} // namespace AetherSDR
