#pragma once

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QVector>

namespace AetherSDR {

// GPU-accelerated waterfall + FFT renderer using Qt6 QRhi (#502 Phase 3).
// Replaces the QPainter software rendering in SpectrumWidget::paintEvent()
// for the heavy-duty waterfall blit and FFT polyline drawing.
//
// Platform backends (auto-selected by Qt):
//   Linux:   OpenGL
//   macOS:   Metal
//   Windows: Direct3D 11
//
// Text labels and complex overlays remain QPainter (hybrid approach).
class GpuSpectrumRenderer : public QRhiWidget {
    Q_OBJECT

public:
    explicit GpuSpectrumRenderer(QWidget* parent = nullptr);
    ~GpuSpectrumRenderer() override;

    // Waterfall: push a new row of intensity values (0.0 to ~120.0)
    void pushWaterfallRow(const QVector<float>& intensities);

    // FFT: update the spectrum bins (dBm values)
    void updateSpectrum(const QVector<float>& binsDbm);

    // Display parameters
    void setDbmRange(float minDbm, float maxDbm);
    void setSpectrumFraction(float frac);  // how much of the widget is FFT vs waterfall
    void setWaterfallBlackLevel(float level);
    void setWaterfallColorRange(float range);
    void setFftLineColor(const QColor& c);
    void setFftFillColor(const QColor& c);
    void setFftFillAlpha(float alpha);

    // Overlay: QPainter-rendered QImage blended on top of GPU content.
    // The image should be ARGB32_Premultiplied, same size as the widget.
    void setOverlayImage(const QImage& img);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

private:
    void initWaterfallPipeline(QRhi* rhi);
    void initSpectrumPipeline(QRhi* rhi);
    void createColorLut(QRhi* rhi, QRhiResourceUpdateBatch* batch);
    void uploadWaterfallRow(QRhiResourceUpdateBatch* batch);

    bool m_initialized{false};

    // ── Waterfall ──────────────────────────────────────────────────────
    QRhiGraphicsPipeline* m_wfPipeline{nullptr};
    QRhiShaderResourceBindings* m_wfSrb{nullptr};
    QRhiBuffer* m_wfVertexBuf{nullptr};     // fullscreen quad
    QRhiBuffer* m_wfUniformBuf{nullptr};    // rowOffset, blackLevel, colorRange
    QRhiTexture* m_wfTexture{nullptr};      // intensity data (R32F, width x history)
    QRhiTexture* m_colorLut{nullptr};       // 256x1 color gradient
    QRhiSampler* m_wfSampler{nullptr};
    QRhiSampler* m_lutSampler{nullptr};

    int m_wfWidth{0};
    int m_wfHeight{800};                    // history rows
    int m_wfWriteRow{0};                    // ring buffer position
    bool m_wfRowDirty{false};
    QVector<float> m_wfPendingRow;

    float m_wfBlackLevel{104.0f};
    float m_wfColorRange{16.0f};

    // ── FFT Spectrum ───────────────────────────────────────────────────
    QRhiGraphicsPipeline* m_fftLinePipeline{nullptr};
    QRhiGraphicsPipeline* m_fftFillPipeline{nullptr};
    QRhiShaderResourceBindings* m_fftSrb{nullptr};
    QRhiBuffer* m_fftVertexBuf{nullptr};    // bin positions
    QRhiBuffer* m_fftFillBuf{nullptr};      // triangle strip for fill
    QRhiBuffer* m_fftUniformBuf{nullptr};

    QVector<float> m_fftBins;
    bool m_fftDirty{false};

    float m_minDbm{-130.0f};
    float m_maxDbm{-40.0f};
    float m_spectrumFrac{0.4f};

    QColor m_fftLineColor{0x00, 0xb4, 0xd8};
    QColor m_fftFillColor{0x00, 0x60, 0x80};
    float m_fftFillAlpha{0.3f};

    // ── Overlay texture (QPainter → QImage → GPU) ─────────────────────
    QRhiGraphicsPipeline* m_overlayPipeline{nullptr};
    QRhiShaderResourceBindings* m_overlaySrb{nullptr};
    QRhiTexture* m_overlayTexture{nullptr};
    QRhiSampler* m_overlaySampler{nullptr};
    QImage m_overlayImage;
    bool m_overlayDirty{false};
    void initOverlayPipeline(QRhi* rhi);
};

} // namespace AetherSDR
