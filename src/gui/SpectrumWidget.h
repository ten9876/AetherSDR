#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QColor>

namespace AetherSDR {

class SpectrumOverlayMenu;
class VfoWidget;

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
class SpectrumWidget : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {800, 300}; }

    // Set the frequency range covered by this panadapter.
    void setFrequencyRange(double centerMhz, double bandwidthMhz);

    // Feed a new FFT frame. bins are scaled dBm values.
    void updateSpectrum(const QVector<float>& binsDbm);

    // Feed a single waterfall row from a VITA-49 waterfall tile.
    // lowFreqMhz/highFreqMhz describe the tile's frequency span.
    // When waterfall tile data is available, this is used instead of
    // the FFT-derived waterfall rows from updateSpectrum().
    void updateWaterfallRow(const QVector<float>& binsDbm,
                            double lowFreqMhz, double highFreqMhz);

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

    // Set the click/scroll tuning step size in Hz (default 100).
    void setStepSize(int hz) { m_stepHz = hz; }

    // Set the per-mode filter limits (Hz). Called when mode changes.
    void setFilterLimits(int minHz, int maxHz) { m_filterMinHz = minHz; m_filterMaxHz = maxHz; }

    // Set the current demod mode (for zoom centering behavior).
    void setMode(const QString& mode) { m_mode = mode; }

    // Access the floating overlay menu (for wiring signals).
    SpectrumOverlayMenu* overlayMenu() const { return m_overlayMenu; }

    // Access the VFO info widget.
    VfoWidget* vfoWidget() const { return m_vfoWidget; }

    // WNB and RF gain state for on-screen indicators.
    bool wnbActive()   const { return m_wnbActive; }
    int  rfGainValue() const { return m_rfGainValue; }
    void setWnbActive(bool on) { m_wnbActive = on; update(); }
    void setRfGain(int gain) { m_rfGainValue = gain; update(); }

    // ── Display control setters ───────────────────────────────────────────
    // FFT controls (save to AppSettings on each change)
    void setFftAverage(int frames);
    void setFftWeightedAvg(bool on);
    void setFftFps(int fps);
    void setFftFillAlpha(float a);
    void setFftFillColor(const QColor& c);
    float fftFillAlpha() const         { return m_fftFillAlpha; }
    QColor fftFillColor() const        { return m_fftFillColor; }
    int   fftAverage() const           { return m_fftAverage; }
    int   fftFps() const               { return m_fftFps; }
    bool  fftWeightedAvg() const       { return m_fftWeightedAvg; }

    // Waterfall controls (save to AppSettings on each change)
    void setWfColorGain(int gain);
    void setWfBlackLevel(int level);
    void setWfAutoBlack(bool on);
    void setWfLineDuration(int ms);
    int   wfColorGain() const          { return m_wfColorGain; }
    int   wfBlackLevel() const         { return m_wfBlackLevel; }
    bool  wfAutoBlack() const          { return m_wfAutoBlack; }
    int   wfLineDuration() const       { return m_wfLineDuration; }

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
    };

    // Add or update a slice overlay (called per-slice on any state change).
    void setSliceOverlay(int sliceId, double freq, int fLow, int fHigh,
                         bool tx, bool active);
    // Remove a slice overlay.
    void removeSliceOverlay(int sliceId);

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

signals:
    // Emitted when user clicks on an inactive slice marker.
    void sliceClicked(int sliceId);
    // Emitted when the user clicks or scrolls in the panadapter area.
    void frequencyClicked(double mhz);
    // Emitted when the user drags the frequency scale bar to change bandwidth.
    void bandwidthChangeRequested(double newBandwidthMhz);
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

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void drawGrid(QPainter& p, const QRect& r);
    void drawSpectrum(QPainter& p, const QRect& r);
    void drawSliceMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect);
    void drawOffScreenSlices(QPainter& p, const QRect& specRect);
    void drawTnfMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect);
    int  tnfAtPixel(int x) const;
    void drawWaterfall(QPainter& p, const QRect& r);
    void drawFreqScale(QPainter& p, const QRect& r);
    void drawDbmScale(QPainter& p, const QRect& specRect);

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

    // ── FFT display controls (radio-side via "display pan set") ──────────
    int   m_fftAverage{0};           // 0=off, 1-10 frames
    bool  m_fftWeightedAvg{false};
    int   m_fftFps{25};
    float m_fftFillAlpha{0.70f};     // client-side fill opacity (0-1)
    QColor m_fftFillColor{0x00, 0xe5, 0xff};  // client-side fill color (default cyan)

    // ── Waterfall display controls (radio-side via "display panafall set") ─
    int   m_wfColorGain{50};         // 0-100, maps intensity to color range
    int   m_wfBlackLevel{15};        // 0-125, intensity floor (below = black)
    bool  m_wfAutoBlack{true};
    int   m_wfLineDuration{100};     // ms per waterfall row

    // Waterfall colour range for FFT-derived fallback (dBm).
    float m_wfMinDbm{-130.0f};
    float m_wfMaxDbm{-50.0f};

    // Scrolling waterfall image (Format_RGB32)
    QImage m_waterfall;

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

    // ── TNF markers ────────────────────────────────────────────────────
    QVector<TnfMarker> m_tnfMarkers;
    bool m_tnfGlobalEnabled{true};
    int  m_draggingTnfId{-1};
    double m_dragTnfOrigFreq{0.0};

    // Floating overlay menu (child widget, anchored top-left)
    SpectrumOverlayMenu* m_overlayMenu{nullptr};
    // VFO info widget (child widget, attached to VFO marker)
    VfoWidget* m_vfoWidget{nullptr};
};

} // namespace AetherSDR
