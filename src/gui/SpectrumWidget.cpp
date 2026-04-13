#include "SpectrumWidget.h"
#include "SpectrumOverlayMenu.h"
#include "VfoWidget.h"
#include "SliceColors.h"

#ifdef AETHER_GPU_SPECTRUM
#include <rhi/qrhi.h>
#include <QFile>
#endif

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
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
#include <QGuiApplication>
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

// ─── Waterfall color scheme gradient presets ─────────────────────────────────

static constexpr WfGradientStop kDefaultStops[] = {
    {0.00f,   0,   0,   0},   // black
    {0.15f,   0,   0, 128},   // dark blue
    {0.30f,   0,  64, 255},   // blue
    {0.45f,   0, 200, 255},   // cyan
    {0.60f,   0, 220,   0},   // green
    {0.80f, 255, 255,   0},   // yellow
    {1.00f, 255,   0,   0},   // red
};
static constexpr WfGradientStop kGrayscaleStops[] = {
    {0.00f,   0,   0,   0},
    {1.00f, 255, 255, 255},
};
static constexpr WfGradientStop kBlueGreenStops[] = {
    {0.00f,   0,   0,   0},
    {0.25f,   0,  30, 120},
    {0.50f,   0, 100, 180},
    {0.75f,   0, 200, 130},
    {1.00f, 220, 255, 220},
};
static constexpr WfGradientStop kFireStops[] = {
    {0.00f,   0,   0,   0},
    {0.25f, 128,   0,   0},
    {0.50f, 220,  80,   0},
    {0.75f, 255, 200,   0},
    {1.00f, 255, 255, 220},
};
static constexpr WfGradientStop kPlasmaStops[] = {
    {0.00f,   0,   0,   0},
    {0.25f,  80,   0, 140},
    {0.50f, 200,   0, 120},
    {0.75f, 240, 120,   0},
    {1.00f, 255, 255,  80},
};

const WfGradientStop* wfSchemeStops(WfColorScheme scheme, int& count)
{
    switch (scheme) {
    case WfColorScheme::Grayscale: count = 2; return kGrayscaleStops;
    case WfColorScheme::BlueGreen: count = 5; return kBlueGreenStops;
    case WfColorScheme::Fire:      count = 5; return kFireStops;
    case WfColorScheme::Plasma:    count = 5; return kPlasmaStops;
    default:                       count = 7; return kDefaultStops;
    }
}

const char* wfSchemeName(WfColorScheme scheme)
{
    switch (scheme) {
    case WfColorScheme::Grayscale: return "Grayscale";
    case WfColorScheme::BlueGreen: return "Blue-Green";
    case WfColorScheme::Fire:      return "Fire";
    case WfColorScheme::Plasma:    return "Plasma";
    default:                       return "Default";
    }
}

// Interpolate a normalized value t (0–1) through the given gradient stops.
static QRgb interpolateGradient(float t, const WfGradientStop* stops, int n)
{
    int i = 0;
    while (i < n - 2 && stops[i + 1].pos < t) ++i;
    const float seg = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    const int r = static_cast<int>(stops[i].r + seg * (stops[i + 1].r - stops[i].r));
    const int g = static_cast<int>(stops[i].g + seg * (stops[i + 1].g - stops[i].g));
    const int b = static_cast<int>(stops[i].b + seg * (stops[i + 1].b - stops[i].b));
    return qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : SPECTRUM_BASE_CLASS(parent)
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
#ifdef AETHER_GPU_SPECTRUM
    // Explicitly request Metal on macOS.
#  ifdef Q_OS_MAC
    setApi(QRhiWidget::Api::Metal);
    // WA_NativeWindow forces Qt to create a dedicated native NSView for this widget.
    // Without it, QRhiWidget embedded in a QWidget hierarchy (especially one whose
    // backing store was created before this widget was added) fails to obtain a QRhi
    // context because the parent window's surface type is RasterSurface, not MetalSurface.
    // A native window gives QRhiWidget its own Metal-capable surface to render into.
    setAttribute(Qt::WA_NativeWindow);
#  else
    // Warn if running under XWayland — GLX context switching between the main
    // window and child dialogs (e.g. Radio Setup) can trigger BadAccess (#1233).
    // main.cpp normally forces native Wayland, but log it if we ended up here.
    if (QGuiApplication::platformName() == QLatin1String("xcb")
        && qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")) {
        qWarning() << "SpectrumWidget: running under XWayland with OpenGL — "
                      "GLX context issues may occur. Set QT_QPA_PLATFORM=wayland "
                      "or AETHER_NO_GPU=1 to work around (#1233)";
    }
#  endif
#else
    setAttribute(Qt::WA_OpaquePaintEvent);
#endif
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);

    // Floating overlay menu (child widget, stays on top)
    m_overlayMenu = new SpectrumOverlayMenu(this);
    m_overlayMenu->raise();

    // Tune guide auto-hide timer (2-second inactivity timeout)
    m_tuneGuideTimer = new QTimer(this);
    m_tuneGuideTimer->setSingleShot(true);
    m_tuneGuideTimer->setInterval(4000);
    connect(m_tuneGuideTimer, &QTimer::timeout, this, [this]() {
        m_tuneGuideVisible = false;
        markOverlayDirty();
    });

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
    m_zoomOutBtn  = makeBtn("\u2212");  // minus sign U+2212
    m_zoomInBtn   = makeBtn("+");

    // SmartSDR pcap: B sends "band_zoom=1", S sends "segment_zoom=1"
    connect(m_zoomBandBtn, &QPushButton::clicked, this, [this]() {
        emit bandZoomRequested();
    });
    connect(m_zoomSegBtn, &QPushButton::clicked, this, [this]() {
        emit segmentZoomRequested();
    });

    // Bandwidth zoom: − zooms out (wider BW), + zooms in (narrower BW)
    // Send both bandwidth AND current center to prevent the radio from
    // auto-centering the panadapter (which causes band jumps).
    auto emitZoom = [this](double factor) {
        const double newBw = m_bandwidthMhz * factor;
        if (newBw < m_minBwMhz || newBw > m_maxBwMhz) { return; }  // at limit
        reprojectWaterfall(m_centerMhz, m_bandwidthMhz, m_centerMhz, newBw);
        m_bandwidthMhz = newBw;
        markOverlayDirty();
        emit bandwidthChangeRequested(newBw);
        emit centerChangeRequested(m_centerMhz);  // anchor the current center
    };
    connect(m_zoomOutBtn, &QPushButton::clicked, this, [emitZoom]() { emitZoom(1.5); });
    connect(m_zoomInBtn,  &QPushButton::clicked, this, [emitZoom]() { emitZoom(1.0 / 1.5); });
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
    m_fftHeatMap     = s.value(settingsKey("DisplayFftHeatMap"), "True").toString() == "True";
    m_showGrid       = s.value(settingsKey("DisplayShowGrid"), "True").toString() == "True";
    m_fftLineWidth   = s.value(settingsKey("DisplayFftLineWidth"), "2.0").toFloat();
    m_wfColorScheme  = static_cast<WfColorScheme>(
        std::clamp(s.value(settingsKey("DisplayWfColorScheme"), "0").toInt(),
                   0, static_cast<int>(WfColorScheme::Count) - 1));
    m_singleClickTune = s.value("SingleClickTune", "False").toString() == "True";
    m_showTuneGuides  = s.value("ShowTuneGuides", "False").toString() == "True";

    // Background image — default to bundled logo, "none" = explicitly cleared
    QString bgPath = s.value(settingsKey("BackgroundImage"), ":/bg-default.jpg").toString();
    if (bgPath != "none" && !bgPath.isEmpty())
        setBackgroundImage(bgPath);
    m_bgOpacity = s.value(settingsKey("BackgroundOpacity"), "80").toInt();

    // Sync overlay menu sliders with restored settings
    if (m_overlayMenu)
        m_overlayMenu->syncDisplaySettings(m_fftAverage, m_fftFps,
            static_cast<int>(m_fftFillAlpha * 100), m_fftWeightedAvg, m_fftFillColor,
            m_wfColorGain, m_wfBlackLevel, m_wfAutoBlack, m_wfLineDuration,
            75, false, m_fftHeatMap, static_cast<int>(m_wfColorScheme), m_showGrid,
            m_fftLineWidth);
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
void SpectrumWidget::setFftHeatMap(bool on) {
    m_fftHeatMap = on;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftHeatMap"), on ? "True" : "False");
    s.save();
}
void SpectrumWidget::setShowGrid(bool on) {
    m_showGrid = on;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayShowGrid"), on ? "True" : "False");
    s.save();
    markOverlayDirty();
}
void SpectrumWidget::setShowTuneGuides(bool on) {
    m_showTuneGuides = on;
    if (!on) {
        m_tuneGuideVisible = false;
        m_tuneGuideTimer->stop();
    }
    auto& s = AppSettings::instance();
    s.setValue("ShowTuneGuides", on ? "True" : "False");
    s.save();
    markOverlayDirty();

    // Propagate to all sibling SpectrumWidgets so the toggle is global
    if (QWidget* top = window()) {
        const auto siblings = top->findChildren<SpectrumWidget*>();
        for (SpectrumWidget* sw : siblings) {
            if (sw != this && sw->m_showTuneGuides != on) {
                sw->m_showTuneGuides = on;
                if (!on) {
                    sw->m_tuneGuideVisible = false;
                    sw->m_tuneGuideTimer->stop();
                }
                sw->markOverlayDirty();
            }
        }
    }
}
void SpectrumWidget::setFftLineWidth(float w) {
    m_fftLineWidth = std::clamp(w, 0.0f, 5.0f);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftLineWidth"), QString::number(m_fftLineWidth, 'f', 1));
    s.save();
}
void SpectrumWidget::setFftFillAlpha(float a) {
    m_fftFillAlpha = std::clamp(a, 0.0f, 1.0f);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftFillAlpha"), QString::number(m_fftFillAlpha, 'f', 2));
    s.save();
    markOverlayDirty();
}
void SpectrumWidget::setFftFillColor(const QColor& c) {
    m_fftFillColor = c;
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("DisplayFftFillColor"), c.name());
    s.save();
    markOverlayDirty();
}
void SpectrumWidget::setWfColorGain(int gain) {
    int clamped = std::clamp(gain, 0, 100);
    if (clamped != m_wfColorGain) {
        m_wfColorGain = clamped;
        auto& s = AppSettings::instance();
        s.setValue(settingsKey("DisplayWfColorGain"), QString::number(m_wfColorGain));
        s.save();
    }
    update();
}
void SpectrumWidget::setWfBlackLevel(int level) {
    int clamped = std::clamp(level, 0, 100);
    if (clamped != m_wfBlackLevel) {
        m_wfBlackLevel = clamped;
        auto& s = AppSettings::instance();
        s.setValue(settingsKey("DisplayWfBlackLevel"), QString::number(m_wfBlackLevel));
        s.save();
    }
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

void SpectrumWidget::setWfColorScheme(int scheme) {
    auto clamped = static_cast<WfColorScheme>(
        std::clamp(scheme, 0, static_cast<int>(WfColorScheme::Count) - 1));
    if (clamped != m_wfColorScheme) {
        m_wfColorScheme = clamped;
        auto& s = AppSettings::instance();
        s.setValue(settingsKey("DisplayWfColorScheme"), QString::number(static_cast<int>(m_wfColorScheme)));
        s.save();
    }
    update();
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
    markOverlayDirty();
}

void SpectrumWidget::reprojectWaterfall(double oldCenterMhz, double oldBandwidthMhz,
                                        double newCenterMhz, double newBandwidthMhz)
{
    if (m_waterfall.isNull()) {
        return;
    }
    if (oldBandwidthMhz <= 0.0 || newBandwidthMhz <= 0.0) {
        return;
    }

    const int imageWidth = m_waterfall.width();
    const int imageHeight = m_waterfall.height();
    if (imageWidth <= 0 || imageHeight <= 0) {
        return;
    }

    const double oldStartMhz = oldCenterMhz - oldBandwidthMhz / 2.0;
    const double oldEndMhz = oldCenterMhz + oldBandwidthMhz / 2.0;
    const double newStartMhz = newCenterMhz - newBandwidthMhz / 2.0;
    const double newEndMhz = newCenterMhz + newBandwidthMhz / 2.0;
    const double overlapStartMhz = std::max(oldStartMhz, newStartMhz);
    const double overlapEndMhz = std::min(oldEndMhz, newEndMhz);

    QImage reprojected(imageWidth, imageHeight, QImage::Format_RGB32);
    reprojected.fill(Qt::black);

    if (overlapEndMhz > overlapStartMhz) {
        const double srcLeft = (overlapStartMhz - oldStartMhz) / oldBandwidthMhz * imageWidth;
        const double srcRight = (overlapEndMhz - oldStartMhz) / oldBandwidthMhz * imageWidth;
        const double dstLeft = (overlapStartMhz - newStartMhz) / newBandwidthMhz * imageWidth;
        const double dstRight = (overlapEndMhz - newStartMhz) / newBandwidthMhz * imageWidth;

        if (srcRight > srcLeft && dstRight > dstLeft) {
            QPainter painter(&reprojected);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QRectF(dstLeft, 0.0, dstRight - dstLeft, imageHeight),
                              m_waterfall,
                              QRectF(srcLeft, 0.0, srcRight - srcLeft, imageHeight));
        }
    }

    m_waterfall = std::move(reprojected);
    m_prevTileScanline.clear();
#ifdef AETHER_GPU_SPECTRUM
    m_wfTexFullUpload = true;
#endif
}

void SpectrumWidget::setFrequencyRange(double centerMhz, double bandwidthMhz)
{
    if (centerMhz == m_centerMhz && bandwidthMhz == m_bandwidthMhz)
        return;

    const double oldCenterMhz = m_centerMhz;
    const double oldBandwidthMhz = m_bandwidthMhz;

    if (oldBandwidthMhz > 0.0 && bandwidthMhz > 0.0) {
        reprojectWaterfall(oldCenterMhz, oldBandwidthMhz, centerMhz, bandwidthMhz);
    }

    qDebug() << "SpectrumWidget::setFrequencyRange center="
             << QString::number(centerMhz, 'f', 6)
             << "bw=" << QString::number(bandwidthMhz, 'f', 6)
             << "bins=" << m_smoothed.size();
    m_centerMhz    = centerMhz;
    m_bandwidthMhz = bandwidthMhz;
    markOverlayDirty();
}

void SpectrumWidget::setSpectrumFrac(float f)
{
    m_spectrumFrac = std::clamp(f, 0.10f, 0.90f);
    auto& s = AppSettings::instance();
    s.setValue(settingsKey("SpectrumSplitRatio"), QString::number(m_spectrumFrac, 'f', 3));
    s.save();
    markOverlayDirty();
}

void SpectrumWidget::setDbmRange(float minDbm, float maxDbm)
{
    float ref = maxDbm;
    float dyn = maxDbm - minDbm;
    if (ref == m_refLevel && dyn == m_dynamicRange) return;
    m_refLevel     = ref;
    m_dynamicRange = dyn;
    markOverlayDirty();
}

// ─── Slice color table (shared via SliceColors.h) ────────────────────────────

static QColor sliceColor(int sliceId, bool active) {
    const auto& c = kSliceColors[sliceId % kSliceColorCount];
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
        markOverlayDirty();
    } else {
        auto& o = m_sliceOverlays[idx];
        if (o.freqMhz == freq && o.filterLowHz == fLow && o.filterHighHz == fHigh &&
            o.isTxSlice == tx && o.isActive == active && o.mode == mode &&
            o.rttyMark == rttyMark && o.rttyShift == rttyShift &&
            o.ritOn == ritOn && o.ritFreq == ritFreq &&
            o.xitOn == xitOn && o.xitFreq == xitFreq)
            return;
        o.freqMhz = freq; o.filterLowHz = fLow; o.filterHighHz = fHigh;
        o.isTxSlice = tx; o.isActive = active;
        o.mode = mode; o.rttyMark = rttyMark; o.rttyShift = rttyShift;
        o.ritOn = ritOn; o.ritFreq = ritFreq;
        o.xitOn = xitOn; o.xitFreq = xitFreq;
        markOverlayDirty();
    }
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
    markOverlayDirty();
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
    markOverlayDirty();
}

// ─── Legacy single-slice convenience wrappers ────────────────────────────────

void SpectrumWidget::setVfoFrequency(double freqMhz)
{
    auto* o = const_cast<SliceOverlay*>(activeOverlay());
    if (o) { o->freqMhz = freqMhz; markOverlayDirty(); }
}

void SpectrumWidget::setVfoFilter(int lowHz, int highHz)
{
    auto* o = const_cast<SliceOverlay*>(activeOverlay());
    if (o) { o->filterLowHz = lowHz; o->filterHighHz = highHz; markOverlayDirty(); }
}

void SpectrumWidget::setSliceInfo(int sliceId, bool isTxSlice)
{
    int idx = overlayIndex(sliceId);
    if (idx >= 0) { m_sliceOverlays[idx].isTxSlice = isTxSlice; markOverlayDirty(); }
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
    if (m_noiseFloorEnable && !m_transmitting && !m_smoothed.isEmpty()) {
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
        // Estimate noise floor from incoming tiles using a two-pass trimmed mean.
        // Freeze during TX; threshold is restored to pre-TX value on TX→RX transition.
        // Pass 1: compute overall mean (sampled every 8th bin for speed).
        float sum = 0;
        int count = 0;
        for (int i = 0; i < binsIntensity.size(); i += 8) {
            sum += binsIntensity[i];
            count++;
        }
        if (count > 0) {
            const float mean = sum / count;
            // Pass 2: mean of bins at or below overall mean — filters out
            // strong signals that would pull the noise floor estimate upward.
            float noiseSum = 0;
            int noiseCount = 0;
            for (int i = 0; i < binsIntensity.size(); i += 8) {
                if (binsIntensity[i] <= mean) {
                    noiseSum += binsIntensity[i];
                    noiseCount++;
                }
            }
            const float noiseFloor = (noiseCount > 0) ? (noiseSum / noiseCount) : mean;
            // Use noise floor directly as threshold — noise maps to black,
            // signals above stand out with better contrast.
            const float target = noiseFloor;
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
        const double mouseXFrac = ev->position().x() / width() - 0.5;
        m_bwDragAnchorMhz = m_centerMhz + mouseXFrac * m_bandwidthMhz;
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

    // Left-click on off-screen slice indicator → absorb or switch slice
    if (ev->button() == Qt::LeftButton) {
        for (int oi = 0; oi < m_offScreenRects.size(); ++oi) {
            if (!m_offScreenRects[oi].isNull() &&
                m_offScreenRects[oi].contains(QPoint(static_cast<int>(ev->position().x()), y))) {
                const auto& so = m_sliceOverlays[oi];
                if (!so.isActive) emit sliceClicked(so.sliceId);
                ev->accept();
                return;
            }
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
                markOverlayDirty();
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

        // Right-click on off-screen slice indicator → slice context menu
        for (int oi = 0; oi < m_offScreenRects.size(); ++oi) {
            if (!m_offScreenRects[oi].isNull() &&
                m_offScreenRects[oi].contains(QPoint(mx, y))) {
                const auto& so = m_sliceOverlays[oi];
                const QChar letter = QChar('A' + (so.sliceId % kSliceColorCount));
                QMenu menu(this);
                menu.addAction(QString("Close Slice %1").arg(letter), this,
                    [this, id = so.sliceId]{ emit sliceCloseRequested(id); });
                menu.addAction(QString("Move Slice %1 Here").arg(letter), this,
                    [this, id = so.sliceId]{ emit sliceTuneRequested(id, m_centerMhz); });
                menu.addAction(QString("Center Slice %1").arg(letter), this,
                    [this, freq = so.freqMhz]{
                        m_centerMhz = freq;
                        markOverlayDirty();
                        emit centerChangeRequested(m_centerMhz);
                    });
                menu.exec(ev->globalPosition().toPoint());
                ev->accept();
                return;
            }
        }

        const double freqMhz = xToMhz(mx);
        const int hitTnf = tnfAtPixel(mx);

        // Check if right-click is on an existing spot label
        int hitSpotIdx = -1;
        QString hitSpotCall;
        double hitSpotFreq = 0;
        QString hitSpotSource;
        for (const auto& hr : m_spotClickRects) {
            if (hr.rect.contains(mx, static_cast<int>(ev->position().y()))) {
                if (hr.markerIndex >= 0 && hr.markerIndex < m_spotMarkers.size()) {
                    const auto& sm = m_spotMarkers[hr.markerIndex];
                    hitSpotIdx = sm.index;
                    hitSpotCall = sm.callsign;
                    hitSpotFreq = sm.freqMhz;
                    hitSpotSource = sm.source;
                }
                break;
            }
        }

        QMenu menu(this);

        // Spot-on-label context menu
        if (hitSpotIdx >= 0) {
            if (hitSpotSource == "Memory") {
                const QString title = hitSpotCall.isEmpty()
                    ? QStringLiteral("Apply Memory")
                    : QString("Apply %1").arg(hitSpotCall);
                menu.addAction(title, this, [this, hitSpotFreq, hitSpotIdx]{
                    emit frequencyClicked(hitSpotFreq);
                    emit spotTriggered(hitSpotIdx);
                });
            } else {
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

        menu.addSeparator();
        QAction* tuneGuideAction = menu.addAction("Show Tune Guides");
        tuneGuideAction->setCheckable(true);
        tuneGuideAction->setChecked(m_showTuneGuides);
        connect(tuneGuideAction, &QAction::toggled, this, &SpectrumWidget::setShowTuneGuides);

        menu.addSeparator();
        bool floating = m_isFloating;
        QAction* popOutAction = menu.addAction(floating ? "\u21a9 Dock" : "\u2197 Pop out");
        connect(popOutAction, &QAction::triggered, this, [this, floating]() {
            emit popOutRequested(!floating);
        });

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

        const bool loHit = std::abs(mx - loX) <= GRAB;
        const bool hiHit = std::abs(mx - hiX) <= GRAB;
        if (loHit || hiHit) {
            // When both edges are within grab range, pick the closer one (#764)
            if (loHit && hiHit)
                m_draggingFilter = (std::abs(mx - loX) <= std::abs(mx - hiX))
                    ? FilterEdge::Low : FilterEdge::High;
            else
                m_draggingFilter = loHit ? FilterEdge::Low : FilterEdge::High;

            // Store anchor offset so the edge doesn't snap to cursor (#764)
            const double mhz = xToMhz(mx);
            const int mouseHz = static_cast<int>(std::round((mhz - ao->freqMhz) * 1.0e6));
            const int edgeHz = (m_draggingFilter == FilterEdge::Low) ? ao->filterLowHz : ao->filterHighHz;
            m_filterDragStartX = mx;
            m_filterDragStartHz = edgeHz;

            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }

        // Click inside the filter passband → start VFO drag (#404)
        const int left = std::min(loX, hiX);
        const int right = std::max(loX, hiX);
        if (mx > left + GRAB && mx < right - GRAB) {
            m_draggingVfo = true;
            m_vfoDragOffsetHz = static_cast<int>(std::round((xToMhz(mx) - ao->freqMhz) * 1.0e6));
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
        markOverlayDirty();
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
            if (!m_waterfall.isNull()) {
                QImage scaled = m_waterfall.scaled(width(), wfHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
                if (!scaled.isNull())
                    newWf = std::move(scaled);
            }
            m_waterfall = std::move(newWf);
            m_wfWriteRow = 0;
        }
        markOverlayDirty();
        ev->accept();
        return;
    }

    if (m_draggingDbm) {
        const int dy = y - m_dbmDragStartY;
        const int specH = static_cast<int>(contentH * m_spectrumFrac);
        // Convert pixel drag to dB: full FFT height = full dynamic range
        const float deltaDb = (static_cast<float>(dy) / specH) * m_dynamicRange;
        m_refLevel = m_dbmDragStartRef + deltaDb;
        markOverlayDirty();
        emit dbmRangeChangeRequested(m_refLevel - m_dynamicRange, m_refLevel);
        ev->accept();
        return;
    }

    if (m_draggingBandwidth) {
        const int dx = static_cast<int>(ev->position().x()) - m_bwDragStartX;
        // 4x multiplier: dragging 1/4 of widget width doubles/halves bandwidth
        const double scale = std::pow(2.0, static_cast<double>(-dx) / (width() / 4.0));
        const double newBw = std::clamp(m_bwDragStartBw * scale, m_minBwMhz, m_maxBwMhz);
        const double mouseXFrac = static_cast<double>(m_bwDragStartX) / width() - 0.5;
        const double zoomCenter = m_bwDragAnchorMhz - mouseXFrac * newBw;
        reprojectWaterfall(m_centerMhz, m_bandwidthMhz, zoomCenter, newBw);
        m_bandwidthMhz = newBw;
        m_centerMhz = zoomCenter;
        markOverlayDirty();
        emit bandwidthChangeRequested(newBw);
        ev->accept();
        return;
    }

    if (m_draggingFilter != FilterEdge::None) {
        auto* ao = const_cast<SliceOverlay*>(activeOverlay());
        if (!ao) { m_draggingFilter = FilterEdge::None; return; }
        const int mx = static_cast<int>(ev->position().x());
        // Compute Hz delta from pixel delta — immune to freq/overlay changes (#764)
        const double hzPerPx = (m_bandwidthMhz * 1.0e6) / width();
        int hz = m_filterDragStartHz + static_cast<int>(std::round((mx - m_filterDragStartX) * hzPerPx));

        if (m_draggingFilter == FilterEdge::Low) {
            ao->filterLowHz = hz;
        } else {
            ao->filterHighHz = hz;
        }
        markOverlayDirty();
        emit filterChangeRequested(ao->filterLowHz, ao->filterHighHz);
        ev->accept();
        return;
    }

    if (m_draggingVfo) {
        const int mx = static_cast<int>(ev->position().x());
        const double mhz = snapToStep(xToMhz(mx) - m_vfoDragOffsetHz / 1.0e6, m_stepHz);
        emit frequencyClicked(mhz);
        ev->accept();
        return;
    }

    if (m_draggingPan) {
        const int dx = static_cast<int>(ev->position().x()) - m_panDragStartX;
        // Dragging right moves the view right → center shifts left
        const double deltaMhz = -(static_cast<double>(dx) / width()) * m_bandwidthMhz;
        const double newCenter = m_panDragStartCenter + deltaMhz;
        reprojectWaterfall(m_centerMhz, m_bandwidthMhz, newCenter, m_bandwidthMhz);
        m_centerMhz = newCenter;
        markOverlayDirty();
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
        if (m_hoveringOffScreenIdx != oldHover) markOverlayDirty();

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

    // Track cursor position for frequency label overlay and tune guides
    if (m_showTuneGuides) {
        m_tuneGuideVisible = true;
        m_tuneGuideTimer->start();
    }
    if (m_showCursorFreq || m_showTuneGuides) {
        m_cursorPos = ev->position().toPoint();
        markOverlayDirty();
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
        // Emit final center after drag completes — not during drag to avoid
        // flooding the radio with commands on every mouse move (#1313).
        emit centerChangeRequested(m_centerMhz);
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
            markOverlayDirty();
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

void SpectrumWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (m_showCursorFreq || m_showTuneGuides) {
        m_cursorPos = {-1, -1};
        markOverlayDirty();
    }
}

void SpectrumWidget::setBackgroundImage(const QString& path)
{
    m_bgImagePath = path;
    m_bgScaled = {};
    m_bgScaledSize = {};
    if (path.isEmpty()) {
        m_bgImage = {};
    } else {
        m_bgImage = QImage(path);
        qDebug() << "SpectrumWidget: background image" << path
                 << "loaded:" << !m_bgImage.isNull()
                 << "size:" << m_bgImage.size();
        if (m_bgImage.isNull())
            qWarning() << "SpectrumWidget: failed to load background image:" << path;
    }
    markOverlayDirty();
}

bool SpectrumWidget::event(QEvent* ev)
{
    // Re-assert mouse tracking after native window changes (reparenting into
    // QSplitter, window recreation). Without this, QRhiWidget's native Metal
    // surface loses mouse tracking and mouseMoveEvent stops firing.
    if (ev->type() == QEvent::WinIdChange || ev->type() == QEvent::ParentChange) {
        setMouseTracking(true);
    }

    if (ev->type() == QEvent::NativeGesture) {
        auto* ge = static_cast<QNativeGestureEvent*>(ev);
        if (ge->gestureType() == Qt::ZoomNativeGesture) {
            // value > 0 = pinch out (zoom in = narrower BW)
            // value < 0 = pinch in  (zoom out = wider BW)
            // Zoom anchored on the frequency under the cursor: the frequency
            // at the mouse position stays at that pixel after the zoom.
            const double delta = ge->value();
            if (qFuzzyIsNull(delta)) { return true; }
            const double factor = 1.0 / (1.0 + delta);  // invert: pinch-out narrows BW
            const double newBw = m_bandwidthMhz * factor;
            if (newBw < m_minBwMhz || newBw > m_maxBwMhz) { return true; }  // at limit
            // Anchor: keep the frequency under the cursor at the same pixel.
            const double mouseXFrac = ge->position().x() / width() - 0.5;
            const double anchorMhz = m_centerMhz + mouseXFrac * m_bandwidthMhz;
            const double newCenter = anchorMhz - mouseXFrac * newBw;
            reprojectWaterfall(m_centerMhz, m_bandwidthMhz, newCenter, newBw);
            m_bandwidthMhz = newBw;
            m_centerMhz = newCenter;
            markOverlayDirty();
            emit bandwidthChangeRequested(newBw);
            emit centerChangeRequested(newCenter);
            return true;
        }
    }
    return SPECTRUM_BASE_CLASS::event(ev);
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
        // Clamp to ±1: on Linux/libinput regular mice often report pixelDelta
        // (e.g. 120px per notch) which would produce 8 steps and an 8× jump.
        steps = qBound(-1, steps, 1);
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
    // Snap the base frequency to the step grid first, then add the delta.
    // This ensures every scroll moves by exactly m_stepHz rather than snapping
    // the destination, which would cause an alignment artifact on the first
    // scroll (e.g. step=500 Hz at .100 MHz → effective 400 Hz jump).
    const double baseMhz = snapToStep(vfoMhz, m_stepHz);
    const double newMhz  = baseMhz + steps * m_stepHz / 1e6;
    emit frequencyClicked(newMhz);
    ev->accept();
}

// ─── Resize ───────────────────────────────────────────────────────────────────

void SpectrumWidget::resizeEvent(QResizeEvent* ev)
{
    SPECTRUM_BASE_CLASS::resizeEvent(ev);

    // Re-assert mouse tracking — on macOS with WA_NativeWindow, reparenting
    // into a QSplitter can reset native window properties.
    setMouseTracking(true);

    const int chromeH  = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = height() - chromeH;
    const int wfHeight = static_cast<int>(contentH * (1.0f - m_spectrumFrac));
    if (wfHeight > 0 && width() > 0) {
        QImage newWf(width(), wfHeight, QImage::Format_RGB32);
        newWf.fill(Qt::black);
        if (!m_waterfall.isNull()) {
            QImage scaled = m_waterfall.scaled(width(), wfHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            if (!scaled.isNull())
                newWf = std::move(scaled);
        }
        m_waterfall = std::move(newWf);
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

    // Row 1 (bottom): − | + (bandwidth zoom)
    m_zoomOutBtn->move(pad, botY - sz);
    m_zoomInBtn->move(pad + sz + 2, botY - sz);
    // Row 0 (above): S | B (segment/band zoom)
    m_zoomSegBtn->move(pad, botY - sz - sz - 2);
    m_zoomBandBtn->move(pad + sz + 2, botY - sz - sz - 2);
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

    int n = 0;
    const auto* stops = wfSchemeStops(m_wfColorScheme, n);
    return interpolateGradient(t, stops, n);
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

    int n = 0;
    const auto* stops = wfSchemeStops(m_wfColorScheme, n);
    return interpolateGradient(t, stops, n);
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

#ifdef AETHER_GPU_SPECTRUM

// Fullscreen quad: position (x,y) + texcoord (u,v)
static const float kQuadData[] = {
    -1, -1,  0, 1,   // bottom-left
     1, -1,  1, 1,   // bottom-right
    -1,  1,  0, 0,   // top-left
     1,  1,  1, 0,   // top-right
};

static QShader loadShader(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "SpectrumWidget: failed to load shader" << path;
        return {};
    }
    QShader s = QShader::fromSerialized(f.readAll());
    if (!s.isValid())
        qWarning() << "SpectrumWidget: invalid shader" << path;
    return s;
}

void SpectrumWidget::initWaterfallPipeline()
{
    QRhi* r = rhi();

    m_wfVbo = r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadData));
    m_wfVbo->create();

    m_wfUbo = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
    m_wfUbo->create();

    m_wfGpuTexW = qMax(width(), 64);
    m_wfGpuTexH = qMax(m_waterfall.height(), 64);
    m_wfGpuTex = r->newTexture(QRhiTexture::RGBA8, QSize(m_wfGpuTexW, m_wfGpuTexH));
    m_wfGpuTex->create();

    m_wfSampler = r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                 QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::Repeat);
    m_wfSampler->create();

    m_wfSrb = r->newShaderResourceBindings();
    m_wfSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_wfUbo),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_wfGpuTex, m_wfSampler),
    });
    m_wfSrb->create();

    QShader vs = loadShader(":/shaders/resources/shaders/texturedquad.vert.qsb");
    QShader fs = loadShader(":/shaders/resources/shaders/texturedquad.frag.qsb");
    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "SpectrumWidget: waterfall shader load failed";
        return;
    }

    m_wfPipeline = r->newGraphicsPipeline();
    m_wfPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    QRhiVertexInputLayout layout;
    layout.setBindings({{4 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    m_wfPipeline->setVertexInputLayout(layout);
    m_wfPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_wfPipeline->setShaderResourceBindings(m_wfSrb);
    m_wfPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_wfPipeline->create();

    qDebug() << "SpectrumWidget: waterfall pipeline created"
             << m_wfGpuTexW << "x" << m_wfGpuTexH;
}

void SpectrumWidget::initOverlayPipeline()
{
    QRhi* r = rhi();

    m_ovVbo = r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadData));
    m_ovVbo->create();

    int w = qMax(width(), 64);
    int h = qMax(height(), 64);
    const qreal dpr = devicePixelRatioF();
    const int pw = static_cast<int>(w * dpr);
    const int ph = static_cast<int>(h * dpr);
    m_ovGpuTex = r->newTexture(QRhiTexture::RGBA8, QSize(pw, ph));
    m_ovGpuTex->create();

    m_ovSampler = r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                 QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_ovSampler->create();

    m_ovSrb = r->newShaderResourceBindings();
    m_ovSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_ovGpuTex, m_ovSampler),
    });
    m_ovSrb->create();

    QShader vs = loadShader(":/shaders/resources/shaders/overlay.vert.qsb");
    QShader fs = loadShader(":/shaders/resources/shaders/overlay.frag.qsb");
    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "SpectrumWidget: overlay shader load failed";
        return;
    }

    m_ovPipeline = r->newGraphicsPipeline();
    m_ovPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    QRhiVertexInputLayout layout;
    layout.setBindings({{4 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    m_ovPipeline->setVertexInputLayout(layout);
    m_ovPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_ovPipeline->setShaderResourceBindings(m_ovSrb);
    m_ovPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    // Enable alpha blending for overlay compositing
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_ovPipeline->setTargetBlends({blend});

    m_ovPipeline->create();

    m_overlayStatic = QImage(pw, ph, QImage::Format_RGBA8888_Premultiplied);
    m_overlayStatic.setDevicePixelRatio(dpr);
    m_overlayDynamic = QImage(pw, ph, QImage::Format_RGBA8888_Premultiplied);
    m_overlayDynamic.setDevicePixelRatio(dpr);
    m_overlayDynamic.fill(Qt::transparent);

    qDebug() << "SpectrumWidget: overlay pipeline created" << pw << "x" << ph << "dpr:" << dpr;
}

void SpectrumWidget::initSpectrumPipeline()
{
    QRhi* r = rhi();

    // Dynamic vertex buffers: 2N × 6 floats for triangle strip line expansion
    m_fftLineVbo = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                 kMaxFftBins * 2 * kFftVertStride * sizeof(float));
    m_fftLineVbo->create();

    m_fftFillVbo = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                 kMaxFftBins * 2 * kFftVertStride * sizeof(float));
    m_fftFillVbo->create();

    // No uniforms — color is per-vertex
    m_fftSrb = r->newShaderResourceBindings();
    m_fftSrb->setBindings({});
    m_fftSrb->create();

    QShader vs = loadShader(":/shaders/resources/shaders/spectrum.vert.qsb");
    QShader fs = loadShader(":/shaders/resources/shaders/spectrum.frag.qsb");
    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "SpectrumWidget: spectrum shader load failed";
        return;
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({{kFftVertStride * sizeof(float)}});  // stride: 6 floats
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},                     // position
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)},     // color
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    // Fill pipeline (triangle strip)
    m_fftFillPipeline = r->newGraphicsPipeline();
    m_fftFillPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });
    m_fftFillPipeline->setVertexInputLayout(layout);
    m_fftFillPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_fftFillPipeline->setShaderResourceBindings(m_fftSrb);
    m_fftFillPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_fftFillPipeline->setTargetBlends({blend});
    m_fftFillPipeline->create();

    // Line pipeline (line strip)
    m_fftLinePipeline = r->newGraphicsPipeline();
    m_fftLinePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });
    m_fftLinePipeline->setVertexInputLayout(layout);
    m_fftLinePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_fftLinePipeline->setShaderResourceBindings(m_fftSrb);
    m_fftLinePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_fftLinePipeline->setTargetBlends({blend});
    m_fftLinePipeline->create();

    qDebug() << "SpectrumWidget: spectrum pipeline created (vertex-colored)";
}

void SpectrumWidget::initialize(QRhiCommandBuffer* cb)
{
    if (m_rhiInitialized) return;

    QRhi* r = rhi();
    if (!r) {
        qWarning() << "SpectrumWidget: QRhi initialization failed — no GPU backend";
        return;
    }

    qDebug() << "SpectrumWidget: QRhi initialized, backend:" << r->backendName();

    // Upload quad vertex data for both pipelines
    auto* batch = r->nextResourceUpdateBatch();

    initWaterfallPipeline();
    initOverlayPipeline();
    initSpectrumPipeline();

    // Upload VBO data
    batch->uploadStaticBuffer(m_wfVbo, kQuadData);
    batch->uploadStaticBuffer(m_ovVbo, kQuadData);

    // Initial full waterfall texture upload (convert RGB32→RGBA8)
    if (!m_waterfall.isNull()) {
        QImage rgba = m_waterfall.convertToFormat(QImage::Format_RGBA8888);
        QRhiTextureSubresourceUploadDescription desc(rgba);
        batch->uploadTexture(m_wfGpuTex, QRhiTextureUploadEntry(0, 0, desc));
    }

    cb->resourceUpdate(batch);
    m_wfTexFullUpload = false;
    m_wfLastUploadedRow = m_wfWriteRow;
    m_rhiInitialized = true;

    // Force full overlay repaint + upload — the new GPU texture is empty.
    // Without this, m_overlayStaticDirty and m_overlayNeedsUpload may be
    // false from the previous render cycle, leaving the overlay invisible.
    m_overlayStaticDirty = true;
    m_overlayNeedsUpload = true;

    // Re-apply cursor and mouse tracking now that the native surface exists.
    setCursor(cursor());
    setMouseTracking(true);
}

void SpectrumWidget::renderGpuFrame(QRhiCommandBuffer* cb)
{
    // Guard: QRhiWidget surface recreation (add/remove panadapter, reparent)
    // can silently clear mouse tracking on macOS. Re-assert cheaply per frame.
    if (!hasMouseTracking()) {
        setMouseTracking(true);
    }

    // Tune guide recovery: after reparenting (add/remove panadapter), Qt may
    // not deliver mouseMoveEvents even with mouse tracking enabled (missing
    // enterEvent, stale widget state). Poll the actual cursor position to
    // detect when the mouse is inside the widget but events aren't flowing.
    if (m_showTuneGuides) {
        QPoint localPos = mapFromGlobal(QCursor::pos());
        if (rect().contains(localPos)) {
            if (localPos != m_cursorPos) {
                m_cursorPos = localPos;
                m_tuneGuideVisible = true;
                m_tuneGuideTimer->start();
                m_overlayStaticDirty = true;
            }
        } else if (m_cursorPos.x() >= 0) {
            // Mouse left the widget without a leaveEvent
            m_cursorPos = {-1, -1};
            m_overlayStaticDirty = true;
        }
    }

    QRhi* r = rhi();
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= FREQ_SCALE_H + DIVIDER_H + 2) return;

    const int chromeH = FREQ_SCALE_H + DIVIDER_H;
    const int contentH = h - chromeH;
    const int specH = static_cast<int>(contentH * m_spectrumFrac);
    const int wfY = specH + DIVIDER_H + FREQ_SCALE_H;
    const int wfH = h - wfY;
    const QRect specRect(0, 0, w, specH);
    const QRect wfRect(0, wfY, w, wfH);

    // Detect display state changes that may bypass markOverlayDirty()
    {
        if (m_centerMhz != m_lastDetectCenter || m_bandwidthMhz != m_lastDetectBw ||
            m_refLevel != m_lastDetectRef || m_dynamicRange != m_lastDetectDyn ||
            m_spectrumFrac != m_lastDetectFrac ||
            m_wnbActive != m_lastDetectWnb || m_rfGainValue != m_lastDetectRfGain ||
            m_wideActive != m_lastDetectWide) {
            markOverlayDirty();
            m_lastDetectCenter = m_centerMhz; m_lastDetectBw = m_bandwidthMhz;
            m_lastDetectRef = m_refLevel; m_lastDetectDyn = m_dynamicRange;
            m_lastDetectFrac = m_spectrumFrac;
            m_lastDetectWnb = m_wnbActive; m_lastDetectRfGain = m_rfGainValue;
            m_lastDetectWide = m_wideActive;
        }
    }

    auto* batch = r->nextResourceUpdateBatch();

    // Upload waterfall texture — full or incremental
    if (!m_waterfall.isNull()) {
        // Resize texture if needed — full re-upload
        if (m_waterfall.width() != m_wfGpuTexW || m_waterfall.height() != m_wfGpuTexH) {
            m_wfGpuTexW = m_waterfall.width();
            m_wfGpuTexH = m_waterfall.height();
            m_wfGpuTex->setPixelSize(QSize(m_wfGpuTexW, m_wfGpuTexH));
            m_wfGpuTex->create();
            m_wfSrb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_wfUbo),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_wfGpuTex, m_wfSampler),
            });
            m_wfSrb->create();
            m_wfTexFullUpload = true;
        }

        if (m_wfTexFullUpload) {
            // Full upload (init or resize)
            QImage rgba = m_waterfall.convertToFormat(QImage::Format_RGBA8888);
            QRhiTextureSubresourceUploadDescription desc(rgba);
            batch->uploadTexture(m_wfGpuTex, QRhiTextureUploadEntry(0, 0, desc));
            m_wfLastUploadedRow = m_wfWriteRow;
            m_wfTexFullUpload = false;
        } else if (m_wfWriteRow != m_wfLastUploadedRow) {
            // Incremental upload — only the rows that changed since last frame
            const int texH = m_wfGpuTexH;
            int from = m_wfLastUploadedRow;
            int to = m_wfWriteRow;

            // Walk backwards from 'from' to 'to' (ring buffer decrements)
            // Upload each dirty row individually
            QRhiTextureUploadDescription uploadDesc;
            QVector<QRhiTextureUploadEntry> entries;

            int row = from;
            int maxRows = texH;  // safety cap
            while (row != to && maxRows-- > 0) {
                row = (row - 1 + texH) % texH;
                // Extract one scanline from the waterfall QImage, convert to RGBA8
                const uchar* srcLine = m_waterfall.constScanLine(row);
                QImage rowImg(reinterpret_cast<const uchar*>(srcLine),
                              m_wfGpuTexW, 1, m_waterfall.bytesPerLine(),
                              QImage::Format_RGB32);
                QImage rowRgba = rowImg.convertToFormat(QImage::Format_RGBA8888);

                QRhiTextureSubresourceUploadDescription desc(rowRgba);
                desc.setDestinationTopLeft(QPoint(0, row));
                entries.append(QRhiTextureUploadEntry(0, 0, desc));
            }

            if (!entries.isEmpty()) {
                uploadDesc.setEntries(entries.begin(), entries.end());
                batch->uploadTexture(m_wfGpuTex, uploadDesc);
            }
            m_wfLastUploadedRow = m_wfWriteRow;
        }
    }

    // Update waterfall uniforms — just the ring buffer row offset
    float rowOffset = (m_wfGpuTexH > 0)
        ? static_cast<float>(m_wfWriteRow) / m_wfGpuTexH
        : 0.0f;
    float uniforms[] = {rowOffset, 0.0f, 0.0f, 0.0f};
    batch->updateDynamicBuffer(m_wfUbo, 0, sizeof(uniforms), uniforms);

    // Render overlays — split into static (on state change) and dynamic (every frame)
    {
        // Resize overlay images if needed
        const qreal dpr = devicePixelRatioF();
        const int pw = static_cast<int>(w * dpr);
        const int ph = static_cast<int>(h * dpr);
        if (m_overlayStatic.size() != QSize(pw, ph)) {
            m_overlayStatic = QImage(pw, ph, QImage::Format_RGBA8888_Premultiplied);
            m_overlayStatic.setDevicePixelRatio(dpr);
            m_overlayDynamic = QImage(pw, ph, QImage::Format_RGBA8888_Premultiplied);
            m_overlayDynamic.setDevicePixelRatio(dpr);
            m_overlayDynamic.fill(Qt::transparent);
            m_ovGpuTex->setPixelSize(QSize(pw, ph));
            m_ovGpuTex->create();
            m_ovSrb->setBindings({
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_ovGpuTex, m_ovSampler),
            });
            m_ovSrb->create();
            m_overlayStaticDirty = true;
        }

        // Static overlay: only repaint when state changes (markOverlayDirty).
        if (m_overlayStaticDirty) {
            m_overlayStatic.fill(Qt::transparent);
            QPainter p(&m_overlayStatic);
            p.setRenderHint(QPainter::Antialiasing, false);

            // Background image
            if (!m_bgImage.isNull()) {
                if (m_bgScaledSize != specRect.size()) {
                    // Scale to cover (keep aspect ratio, expand to fill) then center crop
                    QImage expanded = m_bgImage.scaled(specRect.size(),
                        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    int cx = (expanded.width()  - specRect.width())  / 2;
                    int cy = (expanded.height() - specRect.height()) / 2;
                    m_bgScaled = expanded.copy(cx, cy, specRect.width(), specRect.height());
                    m_bgScaledSize = specRect.size();
                }
                p.setOpacity(1.0 - m_bgOpacity / 100.0);
                p.drawImage(specRect.topLeft(), m_bgScaled);
                p.setOpacity(1.0);
            }

            drawGrid(p, specRect);
            if (m_bandPlanFontSize > 0)
                drawBandPlan(p, specRect);
            drawDbmScale(p, specRect);

            // Divider bar
            p.fillRect(0, specH, w, DIVIDER_H, QColor(0x30, 0x40, 0x50));

            drawFreqScale(p, QRect(0, specH + DIVIDER_H, w, FREQ_SCALE_H));
            drawTimeScale(p, wfRect);
            drawTnfMarkers(p, specRect, wfRect);
            if (m_showSpots)
                drawSpotMarkers(p, specRect);
            drawSliceMarkers(p, specRect, wfRect);
            drawOffScreenSlices(p, specRect);

            // WNB / RF gain / Prop forecast indicators (top-right of spectrum)
            {
                const bool showProp = m_propForecastVisible
                    && m_propKIndex >= 0
                    && m_propAIndex >= 0
                    && m_propSfi > 0;
                if (m_wnbActive || m_rfGainValue != 0 || showProp || m_wideActive) {
                    QFont indFont(p.font().family(), 14, QFont::Bold);
                    p.setFont(indFont);
                    p.setPen(QColor(0xc8, 0xd8, 0xe8, 180));
                    const QFontMetrics fm(indFont);
                    int y = specRect.top() + fm.ascent() + 4;
                    // Build combined label (left to right: prop, WNB, RF gain, WIDE), right-align
                    QString label;
                    if (showProp) {
                        label += QString("K%1  A%2  SFI %3")
                            .arg(m_propKIndex)
                            .arg(m_propAIndex)
                            .arg(m_propSfi);
                    }
                    if (m_wnbActive) {
                        if (!label.isEmpty()) { label += QStringLiteral("   "); }
                        label += QStringLiteral("WNB");
                    }
                    if (m_rfGainValue != 0) {
                        if (!label.isEmpty()) { label += QStringLiteral("   "); }
                        label += QStringLiteral("%1%2 dB")
                            .arg(m_rfGainValue > 0 ? "+" : "").arg(m_rfGainValue);
                    }
                    if (m_wideActive) {
                        if (!label.isEmpty()) { label += QStringLiteral("   "); }
                        label += QStringLiteral("WIDE");
                    }
                    int x = specRect.right() - DBM_STRIP_W - 8 - fm.horizontalAdvance(label);
                    p.drawText(x, y, label);
                }

                // MQTT device status overlay (#699)
                if (!m_mqttDisplayValues.isEmpty()) {
                    QFont mqttFont(p.font().family(), 12, QFont::Bold);
                    p.setFont(mqttFont);
                    p.setPen(QColor(0x80, 0xd0, 0xff, 200));
                    const QFontMetrics fm2(mqttFont);
                    QString mqttLabel;
                    for (auto it = m_mqttDisplayValues.constBegin();
                         it != m_mqttDisplayValues.constEnd(); ++it) {
                        if (!mqttLabel.isEmpty()) { mqttLabel += QStringLiteral("   "); }
                        mqttLabel += it.key() + QStringLiteral(": ") + it.value();
                    }
                    int mx = specRect.right() - DBM_STRIP_W - 8 - fm2.horizontalAdvance(mqttLabel);
                    int my = specRect.top() + fm2.ascent() + 22;
                    p.drawText(mx, my, mqttLabel);
                }
            }

            // Cursor frequency label (#726)
            if (m_showCursorFreq && m_cursorPos.x() >= 0
                && m_cursorPos.y() >= 0) {
                const double freqMhz = xToMhz(m_cursorPos.x());
                const QString label = QString::number(freqMhz, 'f', 6);
                QFont f = p.font();
                f.setPointSize(9);
                p.setFont(f);
                const QFontMetrics fm(f);
                const int tw = fm.horizontalAdvance(label) + 8;
                const int th = fm.height() + 4;
                int lx = m_cursorPos.x() + 12;
                if (lx + tw > w) lx = m_cursorPos.x() - tw - 4;
                int ly = m_cursorPos.y() - th - 4;
                if (ly < 0) ly = m_cursorPos.y() + 16;
                p.fillRect(lx, ly, tw, th, QColor(0x0f, 0x0f, 0x1a, 200));
                p.setPen(QColor(0xc8, 0xd8, 0xe8));
                p.drawText(lx + 4, ly + fm.ascent() + 2, label);
            }

            // Tune guide overlay (vertical line + frequency label)
            if (m_showTuneGuides && m_tuneGuideVisible
                && m_cursorPos.x() >= 0 && m_cursorPos.y() >= 0) {
                const int cx = m_cursorPos.x();
                p.setPen(QPen(QColor(0x60, 0x70, 0x80), 1));
                p.drawLine(cx, 0, cx, h);

                const double freqMhz = xToMhz(cx);
                long long hz = static_cast<long long>(std::round(freqMhz * 1e6));
                int mhzPart = static_cast<int>(hz / 1000000);
                int khzPart = static_cast<int>((hz / 1000) % 1000);
                int hzPart  = static_cast<int>(hz % 1000);
                const QString label = QString("%1.%2.%3")
                    .arg(mhzPart)
                    .arg(khzPart, 3, 10, QChar('0'))
                    .arg(hzPart, 3, 10, QChar('0'));
                QFont f = p.font();
                f.setPointSize(12);
                p.setFont(f);
                const QFontMetrics fm(f);
                const int tw = fm.horizontalAdvance(label) + 8;
                const int th = fm.height() + 4;
                int lx = cx + 12;
                if (lx + tw > w) { lx = cx - tw - 4; }
                int ly = m_cursorPos.y() - th - 4;
                if (ly < 0) { ly = m_cursorPos.y() + 16; }
                p.fillRect(lx, ly, tw, th, QColor(0x0f, 0x0f, 0x1a, 200));
                p.setPen(QColor(0xc8, 0xd8, 0xe8));
                p.drawText(lx + 4, ly + fm.ascent() + 2, label);
            }

            m_overlayStaticDirty = false;
            m_overlayNeedsUpload = true;
        }

        // Upload overlay texture only when content changed
        if (m_overlayNeedsUpload) {
            QRhiTextureSubresourceUploadDescription ovDesc(m_overlayStatic);
            batch->uploadTexture(m_ovGpuTex, QRhiTextureUploadEntry(0, 0, ovDesc));
            m_overlayNeedsUpload = false;
        }

        // Generate FFT spectrum vertices with baked colors
        if (!m_smoothed.isEmpty() && m_fftLineVbo && m_fftFillVbo) {
            const int n = qMin(m_smoothed.size(), kMaxFftBins);
            const float minDbm = m_refLevel - m_dynamicRange;
            const float maxDbm = m_refLevel;
            const float range = maxDbm - minDbm;
            const float yBot = -1.0f;
            const float yTop = 1.0f;

            // Colors from settings
            const float fr = m_fftFillColor.redF();
            const float fg = m_fftFillColor.greenF();
            const float fb = m_fftFillColor.blueF();
            const float fa = m_fftFillAlpha;

            // Solid fill: slider sweeps from translucent gradient to solid.
            // At low slider: soft glow under curve (bright top, dark faint base)
            // At high slider: converges to uniform solid fill color
            const QColor dk = m_fftFillColor.darker(300);
            const float topAlpha = fa;
            const float botAlpha = fa * fa;
            // Blend bottom color from darker(300) toward fill color as slider increases
            const float colorBlend = fa;  // 0=full dark, 1=same as top
            const float dr = fr + (1.0f - colorBlend) * (dk.redF() - fr);
            const float dg = fg + (1.0f - colorBlend) * (dk.greenF() - fg);
            const float db = fb + (1.0f - colorBlend) * (dk.blueF() - fb);
            const float gradRange = yTop - yBot;

            auto yColor = [&](float vy, float* out) {
                const float gt = (gradRange > 0)
                    ? qBound(0.0f, (yTop - vy) / gradRange, 1.0f) : 0.0f;
                out[0] = fr + gt * (dr - fr);
                out[1] = fg + gt * (dg - fg);
                out[2] = fb + gt * (db - fb);
                out[3] = topAlpha + gt * (botAlpha - topAlpha);
            };

            // Line vertices: 2N × (x, y, r, g, b, a) — triangle strip expansion
            // for variable-width lines on GPU (LineStrip is fixed at 1px)
            QVector<float> lineVerts(n * 2 * kFftVertStride);
            // Fill vertices: 2N × (x, y, r, g, b, a)
            QVector<float> fillVerts(n * 2 * kFftVertStride);

            // Pre-compute positions for normal calculation
            struct Pt { float x, y; };
            QVector<Pt> pts(n);
            for (int i = 0; i < n; ++i) {
                pts[i].x = 2.0f * i / (n - 1) - 1.0f;
                float t = qBound(0.0f, (m_smoothed[i] - minDbm) / range, 1.0f);
                pts[i].y = yBot + t * (yTop - yBot);
            }

            // Half-width in NDC — convert pixel width to NDC using viewport
            const float halfW = m_fftLineWidth / static_cast<float>(qMax(1, width()));

            for (int i = 0; i < n; ++i) {
                float t = qBound(0.0f, (m_smoothed[i] - minDbm) / range, 1.0f);

                // Compute perpendicular normal from adjacent points
                float dx, dy;
                if (i == 0) {
                    dx = pts[1].x - pts[0].x;
                    dy = pts[1].y - pts[0].y;
                } else if (i == n - 1) {
                    dx = pts[n-1].x - pts[n-2].x;
                    dy = pts[n-1].y - pts[n-2].y;
                } else {
                    dx = pts[i+1].x - pts[i-1].x;
                    dy = pts[i+1].y - pts[i-1].y;
                }
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-8f) len = 1e-8f;
                float nx = -dy / len * halfW;
                float ny =  dx / len * halfW;

                // Per-vertex color
                float cr, cg, cb2;
                if (m_fftHeatMap) {
                    if (t < 0.25f) {
                        float s = t / 0.25f;
                        cr = 0.0f; cg = s; cb2 = 1.0f;
                    } else if (t < 0.5f) {
                        float s = (t - 0.25f) / 0.25f;
                        cr = 0.0f; cg = 1.0f; cb2 = 1.0f - s;
                    } else if (t < 0.75f) {
                        float s = (t - 0.5f) / 0.25f;
                        cr = s; cg = 1.0f; cb2 = 0.0f;
                    } else {
                        float s = (t - 0.75f) / 0.25f;
                        cr = 1.0f; cg = 1.0f - s; cb2 = 0.0f;
                    }
                } else {
                    cr = fr; cg = fg; cb2 = fb;
                }

                // Two vertices per point: offset ± normal
                int li = i * 2 * kFftVertStride;
                lineVerts[li]     = pts[i].x + nx;
                lineVerts[li + 1] = pts[i].y + ny;
                lineVerts[li + 2] = cr;
                lineVerts[li + 3] = cg;
                lineVerts[li + 4] = cb2;
                lineVerts[li + 5] = 0.9f;
                lineVerts[li + 6]  = pts[i].x - nx;
                lineVerts[li + 7]  = pts[i].y - ny;
                lineVerts[li + 8]  = cr;
                lineVerts[li + 9]  = cg;
                lineVerts[li + 10] = cb2;
                lineVerts[li + 11] = 0.9f;

                // Fill vertices
                int fi = i * 2 * kFftVertStride;
                fillVerts[fi]     = pts[i].x;
                fillVerts[fi + 1] = pts[i].y;
                fillVerts[fi + 6] = pts[i].x;
                fillVerts[fi + 7] = yBot;

                if (m_fftHeatMap) {
                    // Heatmap: line color at top, fade to dark blue at base
                    fillVerts[fi + 2] = cr;
                    fillVerts[fi + 3] = cg;
                    fillVerts[fi + 4] = cb2;
                    fillVerts[fi + 5] = fa * 0.3f;
                    fillVerts[fi + 8]  = 0.0f;
                    fillVerts[fi + 9]  = 0.0f;
                    fillVerts[fi + 10] = 0.3f;
                    fillVerts[fi + 11] = fa;
                } else {
                    // Solid: Y-based gradient (bright at line, dark+faint at base)
                    yColor(pts[i].y, &fillVerts[fi + 2]);
                    yColor(yBot, &fillVerts[fi + 8]);
                }
            }

            batch->updateDynamicBuffer(m_fftLineVbo, 0,
                n * 2 * kFftVertStride * sizeof(float), lineVerts.constData());
            batch->updateDynamicBuffer(m_fftFillVbo, 0,
                n * 2 * kFftVertStride * sizeof(float), fillVerts.constData());
        }
    }

    cb->resourceUpdate(batch);

    // Begin render pass
    const QColor clearColor(0x0a, 0x0a, 0x14);
    cb->beginPass(renderTarget(), clearColor, {1.0f, 0});

    const QSize outputSize = renderTarget()->pixelSize();
    const float dpr = outputSize.width() / static_cast<float>(qMax(1, w));

    // Draw waterfall quad — viewport restricted to waterfall rect
    if (m_wfPipeline) {
        cb->setGraphicsPipeline(m_wfPipeline);
        cb->setShaderResources(m_wfSrb);
        // QRhiViewport: (x, y, width, height) — y is bottom-up in GL convention
        float vpX = static_cast<float>(wfRect.x()) * dpr;
        float vpY = static_cast<float>(h - wfRect.bottom() - 1) * dpr;
        float vpW = static_cast<float>(wfRect.width()) * dpr;
        float vpH = static_cast<float>(wfRect.height()) * dpr;

        cb->setViewport({vpX, vpY, vpW, vpH});
        const QRhiCommandBuffer::VertexInput vbuf(m_wfVbo, 0);
        cb->setVertexInput(0, 1, &vbuf);
        cb->draw(4);
    }

    // Draw FFT spectrum — viewport restricted to spectrum rect
    if (m_fftFillPipeline && m_fftLinePipeline && !m_smoothed.isEmpty()) {
        const int n = qMin(m_smoothed.size(), kMaxFftBins);
        float specVpX = static_cast<float>(specRect.x()) * dpr;
        float specVpY = static_cast<float>(h - specRect.bottom() - 1) * dpr;
        float specVpW = static_cast<float>(specRect.width()) * dpr;
        float specVpH = static_cast<float>(specRect.height()) * dpr;
        QRhiViewport specVp(specVpX, specVpY, specVpW, specVpH);

        // Fill pass
        cb->setGraphicsPipeline(m_fftFillPipeline);
        cb->setShaderResources(m_fftSrb);
        cb->setViewport(specVp);
        const QRhiCommandBuffer::VertexInput fillVbuf(m_fftFillVbo, 0);
        cb->setVertexInput(0, 1, &fillVbuf);
        cb->draw(n * 2);

        // Line pass (skip when line width is 0 = "Off")
        if (m_fftLineWidth > 0.0f) {
            cb->setGraphicsPipeline(m_fftLinePipeline);
            cb->setShaderResources(m_fftSrb);
            cb->setViewport(specVp);
            const QRhiCommandBuffer::VertexInput lineVbuf(m_fftLineVbo, 0);
            cb->setVertexInput(0, 1, &lineVbuf);
            cb->draw(n * 2);
        }
    }

    // Draw overlay quad — on top of FFT fill/line
    if (m_ovPipeline) {
        cb->setGraphicsPipeline(m_ovPipeline);
        cb->setShaderResources(m_ovSrb);
        cb->setViewport({0, 0,
            static_cast<float>(outputSize.width()),
            static_cast<float>(outputSize.height())});
        const QRhiCommandBuffer::VertexInput vbuf(m_ovVbo, 0);
        cb->setVertexInput(0, 1, &vbuf);
        cb->draw(4);
    }

    cb->endPass();

    // Reposition VFO widgets (same as paintEvent)
    {
        struct VfoPos { int sliceId; int x; VfoWidget* w; int splitPartner; };
        QVector<VfoPos> vfos;
        for (const auto& so : m_sliceOverlays) {
            if (auto* vw = m_vfoWidgets.value(so.sliceId, nullptr)) {
                int x = mhzToX(so.freqMhz);
                if (so.mode == "RTTY" || so.mode == "DIGL") {
                    double hiMhz = so.freqMhz + so.filterHighHz / 1.0e6;
                    x = mhzToX(hiMhz) + 4;
                }
                vfos.append({so.sliceId, x, vw, so.splitPartnerId});
            }
        }
        std::sort(vfos.begin(), vfos.end(), [](const VfoPos& a, const VfoPos& b) {
            return a.x < b.x;
        });

        const int panelW = vfos.isEmpty() ? 0 : vfos[0].w->width();
        const int specW = specRect.width();

        // First pass: assign directions for split pairs
        QMap<int, VfoWidget::FlagDir> dirMap;
        for (int i = 0; i < vfos.size(); ++i) {
            if (vfos[i].splitPartner < 0) continue;
            if (dirMap.contains(vfos[i].sliceId)) continue;
            int pi = -1;
            for (int j = 0; j < vfos.size(); ++j) {
                if (vfos[j].sliceId == vfos[i].splitPartner) { pi = j; break; }
            }
            if (pi < 0) continue;
            int leftIdx  = (vfos[i].x <= vfos[pi].x) ? i : pi;
            int rightIdx = (leftIdx == i) ? pi : i;
            dirMap[vfos[leftIdx].sliceId]  = VfoWidget::ForceLeft;
            dirMap[vfos[rightIdx].sliceId] = VfoWidget::ForceRight;
            if (vfos[leftIdx].x < panelW)
                dirMap[vfos[leftIdx].sliceId] = VfoWidget::ForceRight;
            if (vfos[rightIdx].x + panelW > specW)
                dirMap[vfos[rightIdx].sliceId] = VfoWidget::ForceLeft;
        }

        // Second pass: RTTY/DIGL force right
        for (const auto& so : m_sliceOverlays) {
            if (so.mode == "RTTY" || so.mode == "DIGL")
                dirMap[so.sliceId] = VfoWidget::ForceRight;
        }

        if (vfos.size() == 1) {
            VfoWidget::FlagDir dir = dirMap.value(vfos[0].sliceId, VfoWidget::Auto);
            vfos[0].w->updatePosition(vfos[0].x, specRect.top(), dir);
        } else {
            for (int i = 0; i < vfos.size(); ++i) {
                VfoWidget::FlagDir dir = VfoWidget::Auto;
                if (dirMap.contains(vfos[i].sliceId)) {
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
}

void SpectrumWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_rhiInitialized) return;
    renderGpuFrame(cb);
}

void SpectrumWidget::releaseResources()
{
    delete m_wfPipeline;     m_wfPipeline = nullptr;
    delete m_wfSrb;          m_wfSrb = nullptr;
    delete m_wfVbo;          m_wfVbo = nullptr;
    delete m_wfUbo;          m_wfUbo = nullptr;
    delete m_wfGpuTex;       m_wfGpuTex = nullptr;
    delete m_wfSampler;      m_wfSampler = nullptr;

    delete m_ovPipeline;     m_ovPipeline = nullptr;
    delete m_ovSrb;          m_ovSrb = nullptr;
    delete m_ovVbo;          m_ovVbo = nullptr;
    delete m_ovGpuTex;       m_ovGpuTex = nullptr;
    delete m_ovSampler;      m_ovSampler = nullptr;

    delete m_fftLinePipeline; m_fftLinePipeline = nullptr;
    delete m_fftFillPipeline; m_fftFillPipeline = nullptr;
    delete m_fftSrb;          m_fftSrb = nullptr;
    delete m_fftLineVbo;      m_fftLineVbo = nullptr;
    delete m_fftFillVbo;      m_fftFillVbo = nullptr;

    m_rhiInitialized = false;
    qDebug() << "SpectrumWidget: QRhi resources released";
}

#else // !AETHER_GPU_SPECTRUM

void SpectrumWidget::paintEvent(QPaintEvent* ev)
{
    if (width() <= 0 || height() <= FREQ_SCALE_H + DIVIDER_H + 2) return;

#ifdef AETHER_GPU_SPECTRUM
    // GPU mode: render() handles everything via QRhi. Skip the full
    // QPainter path to avoid redundant rendering + compositing overhead.
    // This is the single biggest CPU optimization on macOS (100% → 20%).
    if (m_rhiInitialized) {
        SPECTRUM_BASE_CLASS::paintEvent(ev);
        return;
    }
#endif
    Q_UNUSED(ev);

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
        if (m_bgImage.isNull()) {
            p.fillRect(specRect, QColor(0x0a, 0x0a, 0x14));
        } else {
            // Cache scaled image to avoid per-frame scaling
            if (m_bgScaledSize != specRect.size()) {
                // Scale to cover (keep aspect ratio, expand to fill) then crop to fit
                QImage expanded = m_bgImage.scaled(specRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                int cx = (expanded.width()  - specRect.width())  / 2;
                int cy = (expanded.height() - specRect.height()) / 2;
                m_bgScaled = expanded.copy(cx, cy, specRect.width(), specRect.height());
                m_bgScaledSize = specRect.size();
            }
            p.drawImage(specRect.topLeft(), m_bgScaled);
            // Dark overlay to mute the image (0%=full image, 100%=solid dark)
            int alpha = m_bgOpacity * 255 / 100;
            p.fillRect(specRect, QColor(0x0a, 0x0a, 0x14, alpha));
        }
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
            if (auto* w = m_vfoWidgets.value(so.sliceId, nullptr)) {
                int x = mhzToX(so.freqMhz);
                // In RTTY/DIGL, anchor the flag past the filter high edge
                // so it doesn't cover the mark/space passband.
                if (so.mode == "RTTY" || so.mode == "DIGL") {
                    double hiMhz = so.freqMhz + so.filterHighHz / 1.0e6;
                    x = mhzToX(hiMhz) + 4;  // 4px padding past filter edge
                }
                vfos.append({so.sliceId, x, w, so.splitPartnerId});
            }
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
        // In RTTY/DIGL, force flag to fly right so it doesn't cover M/S passband
        for (const auto& so : m_sliceOverlays) {
            if (so.mode == "RTTY" || so.mode == "DIGL")
                dirMap[so.sliceId] = VfoWidget::ForceRight;
        }

        if (vfos.size() == 1) {
            VfoWidget::FlagDir dir = dirMap.value(vfos[0].sliceId, VfoWidget::Auto);
            vfos[0].w->updatePosition(vfos[0].x, specRect.top(), dir);
        } else {
            for (int i = 0; i < vfos.size(); ++i) {
                VfoWidget::FlagDir dir = VfoWidget::Auto;

                if (dirMap.contains(vfos[i].sliceId)) {
                    // Split pair or RTTY: use pre-assigned direction
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

    // ── WNB / RF Gain / Prop Forecast indicators (top-right of FFT area) ────
    {
        const bool showProp = m_propForecastVisible
            && m_propKIndex >= 0
            && m_propAIndex >= 0
            && m_propSfi > 0;
        if (m_wnbActive || m_rfGainValue != 0 || showProp || m_wideActive) {
            QFont indFont = p.font();
            indFont.setPointSize(18);
            indFont.setBold(true);
            p.setFont(indFont);
            p.setPen(QColor(255, 255, 255, 84));

            const QFontMetrics fm(indFont);
            const int rightEdge = specRect.right() - DBM_STRIP_W - 6;
            const int topY = specRect.top() + fm.ascent() + 2;

            int x = rightEdge;

            // WIDE (rightmost)
            if (m_wideActive) {
                int ww = fm.horizontalAdvance("WIDE");
                x -= ww;
                p.drawText(x, topY, "WIDE");
                x -= 10;
            }

            // RF Gain (to the left of WIDE)
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
                x -= 10;
            }

            // Prop forecast (leftmost: "K3  A12  SFI 110")
            if (showProp) {
                QString propStr = QString("K%1  A%2  SFI %3")
                    .arg(m_propKIndex)
                    .arg(m_propAIndex)
                    .arg(m_propSfi);
                int pw = fm.horizontalAdvance(propStr);
                x -= pw;
                p.drawText(x, topY, propStr);
            }
        }
    }

    // ── Cursor frequency label (#456) ──────────────────────────────────────
    if (m_showCursorFreq && m_cursorPos.x() >= 0
        && m_cursorPos.y() >= 0) {
        const double freqMhz = xToMhz(m_cursorPos.x());
        const QString label = QString::number(freqMhz, 'f', 6);
        QFont f = p.font();
        f.setPointSize(9);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(label) + 8;
        const int th = fm.height() + 4;
        // Position label to the right of cursor, flip left if near right edge
        int lx = m_cursorPos.x() + 12;
        if (lx + tw > width()) lx = m_cursorPos.x() - tw - 4;
        int ly = m_cursorPos.y() - th - 4;
        if (ly < 0) ly = m_cursorPos.y() + 16;
        p.fillRect(lx, ly, tw, th, QColor(0x0f, 0x0f, 0x1a, 200));
        p.setPen(QColor(0xc8, 0xd8, 0xe8));
        p.drawText(lx + 4, ly + fm.ascent() + 2, label);
    }

    // ── Tune guide overlay (vertical line + frequency label) ──────────────
    if (m_showTuneGuides && m_tuneGuideVisible
        && m_cursorPos.x() >= 0 && m_cursorPos.y() >= 0) {
        const int cx = m_cursorPos.x();
        p.setPen(QPen(QColor(0x60, 0x70, 0x80), 1));
        p.drawLine(cx, 0, cx, height());

        const double freqMhz = xToMhz(cx);
        long long hz = static_cast<long long>(std::round(freqMhz * 1e6));
        int mhzPart = static_cast<int>(hz / 1000000);
        int khzPart = static_cast<int>((hz / 1000) % 1000);
        int hzPart  = static_cast<int>(hz % 1000);
        const QString label = QString("%1.%2.%3")
            .arg(mhzPart)
            .arg(khzPart, 3, 10, QChar('0'))
            .arg(hzPart, 3, 10, QChar('0'));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(label) + 8;
        const int th = fm.height() + 4;
        int lx = cx + 12;
        if (lx + tw > width()) { lx = cx - tw - 4; }
        int ly = m_cursorPos.y() - th - 4;
        if (ly < 0) { ly = m_cursorPos.y() + 16; }
        p.fillRect(lx, ly, tw, th, QColor(0x0f, 0x0f, 0x1a, 200));
        p.setPen(QColor(0xc8, 0xd8, 0xe8));
        p.drawText(lx + 4, ly + fm.ascent() + 2, label);
    }

    qCDebug(lcPerf) << "paintEvent:" << static_cast<int>(frameTimer.elapsed()) << "ms";
}

#endif // AETHER_GPU_SPECTRUM

// ─── Grid ─────────────────────────────────────────────────────────────────────

void SpectrumWidget::drawGrid(QPainter& p, const QRect& r)
{
    if (!m_showGrid) return;
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

    // Heat map: blue(0) → cyan(0.25) → green(0.5) → yellow(0.75) → red(1.0)
    auto heatColor = [](float t) -> QColor {
        float cr, cg, cb;
        if (t < 0.25f) {
            float s = t / 0.25f;
            cr = 0.0f; cg = s; cb = 1.0f;
        } else if (t < 0.5f) {
            float s = (t - 0.25f) / 0.25f;
            cr = 0.0f; cg = 1.0f; cb = 1.0f - s;
        } else if (t < 0.75f) {
            float s = (t - 0.5f) / 0.25f;
            cr = s; cg = 1.0f; cb = 0.0f;
        } else {
            float s = (t - 0.75f) / 0.25f;
            cr = 1.0f; cg = 1.0f - s; cb = 0.0f;
        }
        return QColor::fromRgbF(cr, cg, cb);
    };

    // Pre-compute positions and normalized levels
    struct Pt { int x, y; float t; };
    QVector<Pt> pts(n);
    for (int i = 0; i < n; ++i) {
        const float dbm  = m_smoothed[i];
        const float norm = qBound(0.0f, (m_refLevel - dbm) / m_dynamicRange, 1.0f);
        pts[i].x = r.left() + static_cast<int>(static_cast<float>(i) / n * w);
        pts[i].y = r.top()  + qMin(static_cast<int>(norm * h), h - 1);
        pts[i].t = 1.0f - norm;  // 0=noise floor, 1=strong signal
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_fftHeatMap) {
        // Heat map fill: per-column vertical gradient from heat color at top to dark blue at base
        const int bottom = r.bottom();
        for (int i = 0; i < n - 1; ++i) {
            QPolygonF trapezoid;
            trapezoid << QPointF(pts[i].x, pts[i].y)
                      << QPointF(pts[i+1].x, pts[i+1].y)
                      << QPointF(pts[i+1].x, bottom)
                      << QPointF(pts[i].x, bottom);

            float avgT = (pts[i].t + pts[i+1].t) * 0.5f;
            QColor top = heatColor(avgT);
            top.setAlphaF(m_fftFillAlpha * 0.3f);
            QColor bot(0, 0, 77, static_cast<int>(255 * m_fftFillAlpha));
            QLinearGradient grad(0, qMin(pts[i].y, pts[i+1].y), 0, bottom);
            grad.setColorAt(0.0, top);
            grad.setColorAt(1.0, bot);
            p.setPen(Qt::NoPen);
            p.setBrush(grad);
            p.drawPolygon(trapezoid);
        }

        // Heat map line: per-segment coloring
        for (int i = 0; i < n - 1; ++i) {
            float avgT = (pts[i].t + pts[i+1].t) * 0.5f;
            p.setPen(QPen(heatColor(avgT), 1.5));
            p.drawLine(pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y);
        }
    } else {
        // Solid color fill + line
        QPainterPath linePath;
        linePath.moveTo(pts[0].x, pts[0].y);
        for (int i = 1; i < n; ++i) {
            linePath.lineTo(pts[i].x, pts[i].y);
        }

        QPainterPath fillPath(linePath);
        fillPath.lineTo(r.right(), r.bottom());
        fillPath.lineTo(r.left(),  r.bottom());
        fillPath.closeSubpath();

        const int alphaTop = static_cast<int>(200 * m_fftFillAlpha);
        const int alphaBot = static_cast<int>(60 * m_fftFillAlpha);
        QColor topColor(m_fftFillColor);
        topColor.setAlpha(alphaTop);
        QColor botColor = m_fftFillColor.darker(300);
        botColor.setAlpha(alphaBot);
        QLinearGradient grad(0, r.top(), 0, r.bottom());
        grad.setColorAt(0.0, topColor);
        grad.setColorAt(1.0, botColor);

        p.fillPath(fillPath, grad);
        p.setPen(QPen(m_fftFillColor, 1.5));
        p.drawPath(linePath);
    }

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

        // License class contrast: Extra-only = dim, wider access = brighter.
        // Fully opaque — mix segment color with dark background to simulate
        // the old alpha look without letting FFT fill bleed through.
        const QString& lic = seg.license;
        float blend = 0.6f;  // how much of the segment color to show
        if (lic == "E")          blend = 0.20f;
        else if (lic == "E,G")   blend = 0.40f;
        else if (lic.contains("T")) blend = 0.60f;
        else if (lic.isEmpty())  blend = 0.50f;

        const QColor bg(0x0a, 0x0a, 0x14);  // dark background
        QColor fill(
            static_cast<int>(seg.color.red()   * blend + bg.red()   * (1.0f - blend)),
            static_cast<int>(seg.color.green() * blend + bg.green() * (1.0f - blend)),
            static_cast<int>(seg.color.blue()  * blend + bg.blue()  * (1.0f - blend)),
            255);
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
    markOverlayDirty();
}

void SpectrumWidget::setSpotMarkers(const QVector<SpotMarker>& markers)
{
    m_spotMarkers = markers;
    markOverlayDirty();
}

void SpectrumWidget::setTnfGlobalEnabled(bool on)
{
    m_tnfGlobalEnabled = on;
    markOverlayDirty();
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

        // Background pill — only draw when override background is enabled (#768)
        if (m_spotOverrideBg) {
            int bgAlpha = m_spotBgOpacity * 255 / 100;
            QColor bgCol = m_spotBgColor;
            bgCol.setAlpha(bgAlpha);
            p.setPen(Qt::NoPen);
            p.setBrush(bgCol);
            p.drawRoundedRect(labelRect, 3, 3);
        }

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
        connect(action, &QAction::triggered, this, [this, spot] {
            const double freq = spot.freqMhz;
            emit frequencyClicked(freq);
            if (spot.source == "Memory")
                emit spotTriggered(spot.index);
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

        // ── RTTY/DIGL: mark/space lines replace the VFO center line ────
        const bool isRttyMode = (so.mode == "RTTY" || so.mode == "DIGL");

        if (isRttyMode) {
            double markMhz, spaceMhz;
            if (so.mode == "RTTY") {
                // In RTTY mode, RF_frequency IS the mark (radio applies IF shift).
                markMhz  = so.freqMhz;
                spaceMhz = so.freqMhz - so.rttyShift / 1.0e6;
            } else {
                // In DIGL mode, RF_frequency is the carrier (no IF shift).
                markMhz  = so.freqMhz - so.rttyMark / 1.0e6;
                spaceMhz = markMhz - so.rttyShift / 1.0e6;
            }
            const int markX  = mhzToX(markMhz);
            const int spaceX = mhzToX(spaceMhz);

            // Mark line — green, dashed
            p.setPen(QPen(QColor(0, 200, 80, 200), 1, Qt::DashLine));
            p.drawLine(markX, specRect.top(), markX, wfRect.bottom());

            // Space line — red, dashed
            p.setPen(QPen(QColor(220, 60, 60, 200), 1, Qt::DashLine));
            p.drawLine(spaceX, specRect.top(), spaceX, wfRect.bottom());

            // Labels at top
            QFont f = p.font();
            f.setPixelSize(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(0, 200, 80, 240));
            p.drawText(markX + 2, specRect.top() + 12, "M");
            p.setPen(QColor(220, 60, 60, 240));
            p.drawText(spaceX + 2, specRect.top() + 12, "S");
        } else {
            // ── Standard VFO center line ─────────────────────────────────
            int markerX = vfoX;

            // Reduce line width when a filter edge is very close (e.g. CW mode) (#764)
            const qreal vfoLineW = (std::abs(vfoX - fX1) <= 4 || std::abs(vfoX - fX2) <= 4) ? 1.0 : 2.0;
            p.setPen(QPen(QColor(col.red(), col.green(), col.blue(), 220), vfoLineW));
            p.drawLine(markerX, specRect.top(), markerX, wfRect.bottom());

            // ── Triangle marker at top ───────────────────────────────────
            const int triHalf = 6;
            const int triH = 10;
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            QPolygon tri;
            tri << QPoint(markerX - triHalf, specRect.top())
                << QPoint(markerX + triHalf, specRect.top())
                << QPoint(markerX, specRect.top() + triH);
            p.drawPolygon(tri);
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
        const QChar letter = QChar('A' + (so.sliceId % kSliceColorCount));

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
