#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVector>
#include <QMap>
#include <QImage>
#include <QColor>
#include <QDateTime>

#ifdef AETHER_GPU_SPECTRUM
#include <QRhiWidget>
#include <rhi/qrhi.h>
#define SPECTRUM_BASE_CLASS QRhiWidget
#else
#define SPECTRUM_BASE_CLASS QWidget
#endif

namespace AetherSDR {

class SpectrumOverlayMenu;
class VfoWidget;

// Waterfall color scheme presets.
enum class WfColorScheme : int {
    Default = 0,   // black → dark blue → blue → cyan → green → yellow → red
    Grayscale,     // black → white
    BlueGreen,     // black → blue → teal → green → white
    Fire,          // black → red → orange → yellow → white
    Plasma,        // black → purple → magenta → orange → yellow
    Count          // sentinel — number of schemes
};

// Gradient stop used by waterfall color mapping.
struct WfGradientStop { float pos; int r, g, b; };

// Returns the gradient stops for a given color scheme.
const WfGradientStop* wfSchemeStops(WfColorScheme scheme, int& count);

// Returns the display name for a color scheme.
const char* wfSchemeName(WfColorScheme scheme);

// Panadapter / spectrum display widget.
//
// Layout (top to bottom):
//   ~40% — spectrum line plot (current FFT frame, smoothed)
//   ~60% — waterfall (scrolling heat-map history)
//   20px — absolute frequency scale bar
//
// Overlays (drawn on top of spectrum + waterfall):
//   - Filter passband: semi-transparent band from filterLow to filterHigh Hz
//   - VFO marker: vertical orange line at the tuned VFO frequency
//
// Click anywhere in the spectrum/waterfall area to emit frequencyClicked().
// When AETHER_GPU_SPECTRUM is enabled, inherits QRhiWidget for GPU-accelerated
// waterfall rendering. Otherwise falls back to QPainter (QWidget).
class SpectrumWidget : public SPECTRUM_BASE_CLASS {
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    // Per-pan settings persistence
    void setPanIndex(int idx) { m_panIndex = idx; }
    int panIndex() const { return m_panIndex; }
    QString settingsKey(const QString& base) const;
    void loadSettings();

    QSize sizeHint() const override { return {800, 300}; }

    // Set the frequency range covered by this panadapter.
    void setFrequencyRange(double centerMhz, double bandwidthMhz);
    void clearDisplay();  // blank spectrum and waterfall on disconnect

    // Feed a new FFT frame. bins are scaled dBm values.
    void updateSpectrum(const QVector<float>& binsDbm);

    // Feed a single waterfall row from a VITA-49 waterfall tile.
    // lowFreqMhz/highFreqMhz describe the tile's frequency span.
    // When waterfall tile data is available, this is used instead of
    // the FFT-derived waterfall rows from updateSpectrum().
    void updateWaterfallRow(const QVector<float>& binsDbm,
                            double lowFreqMhz, double highFreqMhz,
                            quint32 timecode = 0);

    // Update the dBm range used for the waterfall colour map and spectrum Y axis.
    void setDbmRange(float minDbm, float maxDbm);

    // Noise floor auto-adjust: position (0=top, 100=bottom), enable on/off.
    void setNoiseFloorPosition(int pos) { m_noiseFloorPosition = pos; }
    void setNoiseFloorEnable(bool on)   { m_noiseFloorEnable = on; }

    // (getters for display settings are below with their members)

    // Set the VFO frequency (draws the orange VFO marker).
    void setVfoFrequency(double freqMhz);

    // Set the filter edges (Hz offsets from VFO frequency).
    void setVfoFilter(int lowHz, int highHz);

    // Getters for band settings capture.
    float spectrumFrac()  const { return m_spectrumFrac; }
    float refLevel()      const { return m_refLevel; }
    float dynamicRange()  const { return m_dynamicRange; }

    // Set the FFT/waterfall split ratio programmatically.
    void setSpectrumFrac(float f);

    // Get/set the click/scroll tuning step size in Hz (default 100).
    int stepSize() const { return m_stepHz; }
    void setStepSize(int hz) { m_stepHz = hz; }

    // Set the per-mode filter limits (Hz). Called when mode changes.
    void setFilterLimits(int minHz, int maxHz) { m_filterMinHz = minHz; m_filterMaxHz = maxHz; }

    // Set the current demod mode (for zoom centering behavior).
    void setMode(const QString& mode) { m_mode = mode; }


    // Access the floating overlay menu (for wiring signals).
    SpectrumOverlayMenu* overlayMenu() const { return m_overlayMenu; }

    // Access VFO info widgets (one per slice).
    VfoWidget* vfoWidget() const { return m_vfoWidget; }  // active slice (compat)
    VfoWidget* vfoWidget(int sliceId) const;
    VfoWidget* addVfoWidget(int sliceId);
    void       removeVfoWidget(int sliceId);
    void       setActiveVfoWidget(int sliceId);

    // WNB and RF gain state for on-screen indicators.
    bool wnbActive()   const { return m_wnbActive; }
    int  rfGainValue() const { return m_rfGainValue; }
    void setWnbActive(bool on) { m_wnbActive = on; markOverlayDirty(); }
    void setRfGain(int gain) { m_rfGainValue = gain; markOverlayDirty(); }

    // HF propagation forecast overlay (K-index and Solar Flux Index).
    // Values of -1 mean not yet fetched; visible only when enabled.
    void setPropForecastVisible(bool on) { m_propForecastVisible = on; markOverlayDirty(); }
    void setPropForecast(int kIndex, int sfi) { m_propKIndex = kIndex; m_propSfi = sfi; markOverlayDirty(); }
    bool propForecastVisible() const { return m_propForecastVisible; }

    // NB Waterfall Blanker (#277) — client-side impulse suppression
    void setWfBlankerEnabled(bool on);
    void setWfBlankerThreshold(float t);
    void setWfBlankerMode(int mode);  // 0=Fill, 1=Interpolate
    bool  wfBlankerEnabled()   const { return m_wfBlankerEnabled; }
    float wfBlankerThreshold() const { return m_wfBlankerThreshold; }
    int   wfBlankerMode()      const { return m_wfBlankerMode; }
    void setShowBandPlan(bool on) { m_bandPlanFontSize = on ? 6 : 0; update(); }
    void setBandPlanFontSize(int pt) { m_bandPlanFontSize = pt; update(); }
    void setBandPlanManager(class BandPlanManager* mgr);
    void setSingleClickTune(bool on) { m_singleClickTune = on; }
    void setShowCursorFreq(bool on) { m_showCursorFreq = on; update(); }
    bool showCursorFreq() const { return m_showCursorFreq; }
    void setBackgroundImage(const QString& path);
    QString backgroundImagePath() const { return m_bgImagePath; }
    void setBackgroundOpacity(int pct) { m_bgOpacity = qBound(0, pct, 100); markOverlayDirty(); }
    int backgroundOpacity() const { return m_bgOpacity; }
    bool showBandPlan() const { return m_bandPlanFontSize > 0; }
    int  bandPlanFontSize() const { return m_bandPlanFontSize; }

    // ── Display control setters ───────────────────────────────────────────
    // FFT controls (save to AppSettings on each change)
    void setFftAverage(int frames);
    void setFftWeightedAvg(bool on);
    void setFftFps(int fps);
    void setFftFillAlpha(float a);
    void setFftFillColor(const QColor& c);
    void setFftHeatMap(bool on);
    float fftFillAlpha() const         { return m_fftFillAlpha; }
    QColor fftFillColor() const        { return m_fftFillColor; }
    bool fftHeatMap() const            { return m_fftHeatMap; }
    int   fftAverage() const           { return m_fftAverage; }
    int   fftFps() const               { return m_fftFps; }
    bool  fftWeightedAvg() const       { return m_fftWeightedAvg; }

    // Waterfall controls (save to AppSettings on each change)
    void setWfColorGain(int gain);
    void setWfBlackLevel(int level);
    void setWfAutoBlack(bool on);
    void setWfLineDuration(int ms);
    void setWfColorScheme(int scheme);
    void resetWfTimeScale();
    int   wfColorGain() const          { return m_wfColorGain; }
    int   wfBlackLevel() const         { return m_wfBlackLevel; }
    bool  wfAutoBlack() const          { return m_wfAutoBlack; }
    int   wfLineDuration() const       { return m_wfLineDuration; }
    int   wfColorScheme() const        { return static_cast<int>(m_wfColorScheme); }

    // Set slice info for the off-screen VFO indicator (legacy single-slice).
    void setSliceInfo(int sliceId, bool isTxSlice);

    // ── Multi-slice overlay API ───────────────────────────────────────────
    struct SliceOverlay {
        int    sliceId{0};
        double freqMhz{0};
        int    filterLowHz{0};
        int    filterHighHz{0};
        bool   isTxSlice{false};
        bool   isActive{false};
        int    splitPartnerId{-1};  // slice ID of split partner, -1 if not in split
        QString mode;               // "RTTY", "USB", etc.
        int    rttyMark{2125};      // RTTY mark audio offset (Hz)
        int    rttyShift{170};      // RTTY shift (Hz)
        bool   ritOn{false};
        int    ritFreq{0};          // Hz offset
        bool   xitOn{false};
        int    xitFreq{0};          // Hz offset
    };

    // Add or update a slice overlay (called per-slice on any state change).
    bool isDraggingFilter() const { return m_draggingFilter != FilterEdge::None; }
    void setSliceOverlay(int sliceId, double freq, int fLow, int fHigh,
                         bool tx, bool active, const QString& mode = {},
                         int rttyMark = 2125, int rttyShift = 170,
                         bool ritOn = false, int ritFreq = 0,
                         bool xitOn = false, int xitFreq = 0);
    // Update just the frequency on an existing overlay (for optimistic scroll-to-tune)
    void setSliceOverlayFreq(int sliceId, double freqMhz);
    // Remove a slice overlay.
    void removeSliceOverlay(int sliceId);

    // Mark two slices as a split pair (RX + TX). Pass -1 to clear.
    void setSplitPair(int rxSliceId, int txSliceId);

    // ── TNF overlay ─────────────────────────────────────────────────────
    struct TnfMarker {
        int    id;
        double freqMhz;
        int    widthHz;
        int    depthDb;
        bool   permanent;
    };
    void setTnfMarkers(const QVector<TnfMarker>& markers);
    void setTnfGlobalEnabled(bool on);

    struct SpotMarker {
        int    index;
        QString callsign;
        double freqMhz;
        QString color;       // #AARRGGBB or empty for default
        QString mode;
        QColor  dxccColor;   // DXCC-aware color from DxccColorProvider (#330)
        QString source;
        QString spotterCallsign;
        QString comment;
        qint64  timestampMs{0};
    };
    void setSpotMarkers(const QVector<SpotMarker>& markers);

    struct SpotCluster {
        QRect rect;
        QVector<SpotMarker> spots;
    };

    void setShowSpots(bool on) { m_showSpots = on; update(); }
    bool showSpots() const { return m_showSpots; }
    void setSpotFontSize(int px) { m_spotFontSize = px; update(); }
    void setSpotMaxLevels(int n) { m_spotMaxLevels = n; update(); }
    void setSpotStartPct(int pct) { m_spotStartPct = pct; update(); }
    void setSpotOverrideColors(bool on) { m_spotOverrideColors = on; update(); }
    void setSpotOverrideBg(bool on) { m_spotOverrideBg = on; update(); }
    void setSpotColor(const QColor& c) { m_spotColor = c; update(); }
    void setSpotBgColor(const QColor& c) { m_spotBgColor = c; update(); }
    void setSpotBgOpacity(int pct) { m_spotBgOpacity = pct; update(); }
    void setTransmitting(bool tx) {
        if (tx && !m_transmitting)
            m_preTxAutoBlack = m_autoBlackThresh;  // save before TX
        if (!tx && m_transmitting)
            m_autoBlackThresh = m_preTxAutoBlack;  // restore after TX
        m_transmitting = tx;
    }
    void setShowTxInWaterfall(bool on) { m_showTxInWaterfall = on; }
    void setHasTxSlice(bool has) { m_hasTxSlice = has; }

signals:
    // Emitted when user clicks on an inactive slice marker.
    void sliceClicked(int sliceId);
    // Emitted when the user clicks or scrolls in the panadapter area.
    void frequencyClicked(double mhz);
    void spotTriggered(int spotIndex);
    // Emitted when the user drags the frequency scale bar to change bandwidth.
    void bandwidthChangeRequested(double newBandwidthMhz);
    // Band/segment zoom: radio handles center/bandwidth (SmartSDR pcap: "band_zoom=1" / "segment_zoom=1")
    void bandZoomRequested();
    void segmentZoomRequested();
    // Emitted when the user drags the waterfall to pan the center frequency.
    void centerChangeRequested(double newCenterMhz);
    // Emitted when the user drags a filter edge to resize the passband.
    void filterChangeRequested(int lowHz, int highHz);
    // Emitted when the user adjusts the dBm scale (drag or arrows).
    void dbmRangeChangeRequested(float minDbm, float maxDbm);
    // TNF signals
    void tnfCreateRequested(double freqMhz);
    void tnfMoveRequested(int id, double newFreqMhz);
    void tnfRemoveRequested(int id);
    void tnfWidthRequested(int id, int widthHz);
    void tnfDepthRequested(int id, int depthDb);
    void tnfPermanentRequested(int id, bool permanent);
    void sliceCloseRequested(int sliceId);
    void sliceTxRequested(int sliceId);
    // Spot signals
    void spotAddRequested(double freqMhz, const QString& callsign,
                          const QString& comment, int lifetimeSec,
                          bool forwardToCluster);
    void spotRemoveRequested(int spotIndex);

protected:
#ifdef AETHER_GPU_SPECTRUM
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
#else
    void paintEvent(QPaintEvent* event) override;
#endif
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

public:
    void showAddSpotDialog(double freqMhz);

private:
    void drawGrid(QPainter& p, const QRect& r);
    void drawSpectrum(QPainter& p, const QRect& r);
    void drawSliceMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect);
    void drawOffScreenSlices(QPainter& p, const QRect& specRect);
    void drawBandPlan(QPainter& p, const QRect& specRect);
    void drawTnfMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect);
    void drawSpotMarkers(QPainter& p, const QRect& specRect);
    void showSpotClusterPopup(const SpotCluster& cluster, const QPoint& globalPos);
    int  tnfAtPixel(int x) const;
    void drawWaterfall(QPainter& p, const QRect& r);
    void positionZoomButtons();
    void drawFreqScale(QPainter& p, const QRect& r);
    void drawDbmScale(QPainter& p, const QRect& specRect);
    void drawTimeScale(QPainter& p, const QRect& wfRect);

    // Helper: find overlay index for a sliceId, or -1.
    int overlayIndex(int sliceId) const;
    // Helper: find active overlay (or nullptr).
    const SliceOverlay* activeOverlay() const;

    void pushWaterfallRow(const QVector<float>& bins, int destWidth,
                          double tileLowMhz = -1, double tileHighMhz = -1);
    QRgb dbmToRgb(float dbm) const;
    QRgb intensityToRgb(float intensity) const;  // for native waterfall tiles

    // Pixel x coordinate for a given frequency in MHz (0 = left edge).
    int mhzToX(double mhz) const;
    // Convert pixel x back to MHz.
    double xToMhz(int x) const;

    QVector<float> m_bins;       // raw FFT frame (dBm)
    QVector<float> m_smoothed;   // exponential-smoothed for visual stability

    double m_centerMhz{14.225};
    double m_bandwidthMhz{0.200};

    // Multi-slice overlays (replaces single m_vfoFreqMhz / m_filterLowHz / etc.)
    QVector<SliceOverlay> m_sliceOverlays;

    int    m_filterMinHz{-12000};  // per-mode lower bound (active slice)
    int    m_filterMaxHz{12000};   // per-mode upper bound (active slice)
    QString m_mode{"USB"};         // current demod mode (active slice)

    float m_refLevel{-50.0f};       // top of display (dBm)
    float m_dynamicRange{100.0f};   // dB range shown in spectrum (-50 to -150)

    // Noise floor auto-adjust
    bool  m_noiseFloorEnable{false};
    int   m_noiseFloorPosition{75};  // 0=top, 100=bottom
    int   m_noiseFloorFrameCount{0};

    // Tuning step size for click-snap and wheel scroll (Hz)
    int m_stepHz{100};
    int m_scrollAccum{0};   // trackpad pixel scroll accumulator (macOS)
    int m_angleAccum{0};    // mouse wheel angle accumulator (#390)
    qint64 m_lastWheelMs{0}; // debounce: timestamp of last accepted wheel step

    // ── FFT display controls (radio-side via "display pan set") ──────────
    int   m_panIndex{0};             // per-pan settings index (0, 1, 2, 3)
    int   m_fftAverage{0};           // 0=off, 1-10 frames
    bool  m_fftWeightedAvg{false};
    int   m_fftFps{25};
    float m_fftFillAlpha{0.70f};     // client-side fill opacity (0-1)
    QColor m_fftFillColor{0x00, 0xe5, 0xff};  // client-side fill color (default cyan)
    bool m_fftHeatMap{true};        // true = intensity heat map, false = solid color

    // ── Waterfall display controls (radio-side via "display panafall set") ─
    int   m_wfColorGain{50};         // 0-100, maps intensity to color range
    int   m_wfBlackLevel{15};        // 0-125, intensity floor (below = black)
    bool  m_wfAutoBlack{true};
    WfColorScheme m_wfColorScheme{WfColorScheme::Default};
    float m_autoBlackThresh{145.0f}; // client-side auto-black: tracked noise floor
    int   m_wfLineDuration{100};     // ms per waterfall row

    // Waterfall colour range for FFT-derived fallback (dBm).
    float m_wfMinDbm{-130.0f};
    float m_wfMaxDbm{-50.0f};

    // Scrolling waterfall image (Format_RGB32)
    QImage m_waterfall;
    int    m_wfWriteRow{0};  // ring buffer: next row to write (newest at top)

    // True once we receive native waterfall tile data (PCC 0x8004).
    // When set, updateSpectrum() skips pushing FFT rows to the waterfall
    // because the radio provides dedicated waterfall tiles.
    bool m_hasNativeWaterfall{false};
    qint64 m_lastNativeTileMs{0};    // timestamp of last native tile (for fallback)
    QVector<QRgb> m_prevTileScanline;  // previous tile row for interpolation

    static constexpr float SMOOTH_ALPHA    = 0.35f;
    // Fraction of the panadapter area (above freq scale) used for spectrum
    float m_spectrumFrac{0.40f};
    // Height of the frequency scale bar
    static constexpr int   FREQ_SCALE_H    = 20;
    // Height of the draggable divider between FFT and freq scale
    static constexpr int   DIVIDER_H       = 4;
    // Divider drag state
    bool m_draggingDivider{false};
    // Bandwidth drag state (freq scale bar)
    bool m_draggingBandwidth{false};
    int  m_bwDragStartX{0};
    double m_bwDragStartBw{0.0};
    // Waterfall pan drag state
    bool m_draggingPan{false};
    int  m_panDragStartX{0};
    double m_panDragStartCenter{0.0};
    // Filter edge drag state
    enum class FilterEdge { None, Low, High };
    FilterEdge m_draggingFilter{FilterEdge::None};
    int m_filterDragStartX{0};      // pixel X at grab time (#764)
    int m_filterDragStartHz{0};     // filter edge Hz at grab time (#764)
    // VFO passband drag state (#404)
    bool m_draggingVfo{false};
    // dBm scale strip drag state
    static constexpr int DBM_STRIP_W = 36;  // width of the dBm scale strip
    static constexpr int DBM_ARROW_H = 14;  // height of each arrow button
    bool  m_draggingDbm{false};
    int   m_dbmDragStartY{0};
    float m_dbmDragStartRef{0.0f};
    // Off-screen slice indicator hit rects (parallel to m_sliceOverlays)
    QVector<QRect> m_offScreenRects;
    int  m_hoveringOffScreenIdx{-1};

    // On-screen indicators (WNB, RF Gain)
    bool m_wnbActive{false};
    int  m_rfGainValue{0};

    // HF propagation forecast overlay
    bool m_propForecastVisible{false};
    int  m_propKIndex{-1};
    int  m_propSfi{-1};

    // Background image
    QImage  m_bgImage;
    QImage  m_bgScaled;     // cached at current specRect size
    QString m_bgImagePath;
    QSize   m_bgScaledSize;
    int     m_bgOpacity{80};  // 0=full image, 100=solid dark (default 80%)

    // Cursor frequency label
    bool   m_showCursorFreq{false};
    QPoint m_cursorPos{-1, -1};

    // NB Waterfall Blanker (#277)
    bool  m_wfBlankerEnabled{false};
    int   m_wfBlankerMode{0};            // 0=Fill, 1=Interpolate
    float m_wfBlankerThreshold{1.15f};   // impulse multiplier vs rolling baseline
    static constexpr int WF_BLANKER_N = 32;
    float m_wfBlankerRing[WF_BLANKER_N]{};
    int   m_wfBlankerRingIdx{0};
    int   m_wfBlankerRingCount{0};
    QVector<QRgb> m_wfLastGoodRow;
    int  m_bandPlanFontSize{6};  // 0 = off
    BandPlanManager* m_bandPlanMgr{nullptr};
    bool m_singleClickTune{false};
    QPoint m_clickPressPos;        // for single-click-to-tune drag threshold
    bool   m_spotClickConsumed{false}; // suppress release-to-tune after spot click (#530)
    bool m_showTxInWaterfall{false};  // default matches radio default (off)
    bool m_hasTxSlice{false};  // true if this pan contains the TX slice

    bool     m_transmitting{false};
    float    m_preTxAutoBlack{145.0f}; // auto-black threshold saved before TX

    // Waterfall time scale: ms-per-row derived from tile timecodes + wall-clock.
    // Calibrates over the first 50 tiles, then locks to prevent jitter.
    // resetWfTimeScale() re-triggers calibration (called when rate slider changes).
    float    m_wfMsPerRow{100.0f};     // calibrated ms per waterfall row
    quint32  m_wfPrevTimecode{0};      // previous tile timecode (frame counter)
    qint64   m_wfPrevTimecodeMs{0};    // wall-clock time of previous timecode
    int      m_wfCalibrationCount{0};  // tiles measured so far
    bool     m_wfTimeScaleLocked{false};


    // Client-side row averaging (Rate slider)
    int      m_wfAvgTarget{1};   // how many tiles to average into one row
    int      m_wfAvgCount{0};    // tiles accumulated so far
    QVector<float> m_wfAvgBins;  // accumulated intensity values

    // ── TNF markers ────────────────────────────────────────────────────
    QVector<TnfMarker> m_tnfMarkers;
    bool m_tnfGlobalEnabled{true};
    QVector<SpotMarker> m_spotMarkers;
    struct SpotHitRect {
        QRect rect;
        double freqMhz;
        int markerIndex;  // index into m_spotMarkers for tooltip data
    };
    QVector<SpotHitRect> m_spotClickRects;

    QVector<SpotCluster> m_spotClusters;
    bool m_showSpots{true};
    int  m_spotFontSize{16};
    int  m_spotMaxLevels{3};
    int  m_spotStartPct{50};      // % down from top of spectrum
    bool   m_spotOverrideColors{false};
    bool   m_spotOverrideBg{true};
    QColor m_spotColor{Qt::yellow};
    QColor m_spotBgColor{Qt::black};
    int    m_spotBgOpacity{48};
    int  m_draggingTnfId{-1};
    double m_dragTnfOrigFreq{0.0};

    // Floating overlay menu (child widget, anchored top-left)
    SpectrumOverlayMenu* m_overlayMenu{nullptr};
    // VFO info widgets (one per slice, attached to VFO markers)
    QMap<int, VfoWidget*> m_vfoWidgets;
    VfoWidget* m_vfoWidget{nullptr};  // alias to active slice widget (compat)

    // Bottom-left waterfall zoom buttons: S(egment), B(and)
    QPushButton* m_zoomSegBtn{nullptr};
    QPushButton* m_zoomBandBtn{nullptr};

#ifdef AETHER_GPU_SPECTRUM
    bool m_rhiInitialized{false};

    // Waterfall GPU resources
    QRhiGraphicsPipeline* m_wfPipeline{nullptr};
    QRhiShaderResourceBindings* m_wfSrb{nullptr};
    QRhiBuffer* m_wfVbo{nullptr};
    QRhiBuffer* m_wfUbo{nullptr};
    QRhiTexture* m_wfGpuTex{nullptr};
    QRhiSampler* m_wfSampler{nullptr};
    int m_wfGpuTexW{0};
    int m_wfGpuTexH{0};
    bool m_wfTexFullUpload{true};  // full re-upload needed (resize/init)
    int m_wfLastUploadedRow{-1};   // last row uploaded to GPU (-1 = none)

    // Overlay GPU resources (QPainter → QImage → texture)
    // Static: grid, band plan, scales, slice markers, TNF, spots (repainted on state change)
    // Dynamic: FFT spectrum line (repainted every frame)
    QRhiGraphicsPipeline* m_ovPipeline{nullptr};
    QRhiShaderResourceBindings* m_ovSrb{nullptr};
    QRhiBuffer* m_ovVbo{nullptr};
    QRhiTexture* m_ovGpuTex{nullptr};
    QRhiSampler* m_ovSampler{nullptr};
    QImage m_overlayStatic;     // grid, markers, scales — repainted on change
    QImage m_overlayDynamic;    // FFT spectrum — repainted every frame
    bool m_overlayStaticDirty{true};
    bool m_overlayNeedsUpload{true};

    void initWaterfallPipeline();
    void initOverlayPipeline();
    void initSpectrumPipeline();
    void renderGpuFrame(QRhiCommandBuffer* cb);

    // FFT spectrum GPU resources — vertex color, no uniforms
    QRhiGraphicsPipeline* m_fftLinePipeline{nullptr};
    QRhiGraphicsPipeline* m_fftFillPipeline{nullptr};
    QRhiShaderResourceBindings* m_fftSrb{nullptr};
    QRhiBuffer* m_fftLineVbo{nullptr};    // dynamic, N × (vec2 pos + vec4 color)
    QRhiBuffer* m_fftFillVbo{nullptr};    // dynamic, 2N × (vec2 pos + vec4 color)
    static constexpr int kMaxFftBins = 8192;
    static constexpr int kFftVertStride = 6; // x, y, r, g, b, a
#endif

    // Mark the static overlay for repaint and schedule a frame update.
    // In non-GPU mode this is just update().
    void markOverlayDirty() {
#ifdef AETHER_GPU_SPECTRUM
        m_overlayStaticDirty = true;
#endif
        update();
    }
};

} // namespace AetherSDR
