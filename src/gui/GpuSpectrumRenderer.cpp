#include "GpuSpectrumRenderer.h"
#include <QFile>
#include <cstring>

namespace AetherSDR {

// 7-stop waterfall color gradient (matches QPainter version)
static const QColor kGradient[] = {
    QColor(0, 0, 0),        // black
    QColor(0, 0, 80),       // dark blue
    QColor(0, 0, 255),      // blue
    QColor(0, 255, 255),    // cyan
    QColor(0, 255, 0),      // green
    QColor(255, 255, 0),    // yellow
    QColor(255, 0, 0),      // red
};
static constexpr int kGradientStops = 7;

static QShader loadShader(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "GpuSpectrumRenderer: failed to load shader" << path;
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

GpuSpectrumRenderer::GpuSpectrumRenderer(QWidget* parent)
    : QRhiWidget(parent)
{
}

GpuSpectrumRenderer::~GpuSpectrumRenderer()
{
    releaseResources();
}

void GpuSpectrumRenderer::releaseResources()
{
    delete m_wfPipeline;     m_wfPipeline = nullptr;
    delete m_wfSrb;          m_wfSrb = nullptr;
    delete m_wfVertexBuf;    m_wfVertexBuf = nullptr;
    delete m_wfUniformBuf;   m_wfUniformBuf = nullptr;
    delete m_wfTexture;      m_wfTexture = nullptr;
    delete m_colorLut;       m_colorLut = nullptr;
    delete m_wfSampler;      m_wfSampler = nullptr;
    delete m_lutSampler;     m_lutSampler = nullptr;

    delete m_fftLinePipeline; m_fftLinePipeline = nullptr;
    delete m_fftFillPipeline; m_fftFillPipeline = nullptr;
    delete m_fftSrb;         m_fftSrb = nullptr;
    delete m_fftVertexBuf;   m_fftVertexBuf = nullptr;
    delete m_fftFillBuf;     m_fftFillBuf = nullptr;
    delete m_fftUniformBuf;  m_fftUniformBuf = nullptr;

    delete m_overlayPipeline; m_overlayPipeline = nullptr;
    delete m_overlaySrb;     m_overlaySrb = nullptr;
    delete m_overlayTexture; m_overlayTexture = nullptr;
    delete m_overlaySampler; m_overlaySampler = nullptr;

    m_initialized = false;
}

// ─── Initialization ──────────────────────────────────────────────────────────

void GpuSpectrumRenderer::initialize(QRhiCommandBuffer* cb)
{
    Q_UNUSED(cb);
    if (m_initialized) return;

    QRhi* r = rhi();
    if (!r) return;

    auto* batch = r->nextResourceUpdateBatch();

    initWaterfallPipeline(r);
    initSpectrumPipeline(r);
    initOverlayPipeline(r);
    createColorLut(r, batch);

    cb->resourceUpdate(batch);
    m_initialized = true;
}

void GpuSpectrumRenderer::initWaterfallPipeline(QRhi* r)
{
    // Fullscreen quad vertex data: position (x,y) + texcoord (u,v)
    static const float quadData[] = {
        // pos       uv
        -1, -1,    0, 1,  // bottom-left
         1, -1,    1, 1,  // bottom-right
        -1,  1,    0, 0,  // top-left
         1,  1,    1, 0,  // top-right
    };

    m_wfVertexBuf = r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(quadData));
    m_wfVertexBuf->create();

    m_wfUniformBuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
    m_wfUniformBuf->create();

    // Waterfall intensity texture (R32F, width x height)
    m_wfWidth = qMax(width(), 64);
    m_wfTexture = r->newTexture(QRhiTexture::R32F, QSize(m_wfWidth, m_wfHeight));
    m_wfTexture->create();

    // Samplers
    m_wfSampler = r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                 QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::Repeat);
    m_wfSampler->create();

    m_lutSampler = r->newSampler(QRhiSampler::Linear, QRhiSampler::Nearest,
                                  QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_lutSampler->create();

    // SRB created but not finalized — m_colorLut not yet allocated.
    // setBindings + create() called in createColorLut() after LUT is ready.
    m_wfSrb = r->newShaderResourceBindings();

    // Pipeline
    m_wfPipeline = r->newGraphicsPipeline();

    QShader vs = loadShader(":/shaders/waterfall.vert.qsb");
    QShader fs = loadShader(":/shaders/waterfall.frag.qsb");
    m_wfPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        {4 * sizeof(float)},  // stride: 2 pos + 2 uv
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},               // position
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}, // texcoord
    });
    m_wfPipeline->setVertexInputLayout(inputLayout);
    m_wfPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_wfPipeline->setShaderResourceBindings(m_wfSrb);
    m_wfPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    // Upload quad vertex data
    // (done in first render() call via resource update batch)
}

void GpuSpectrumRenderer::initSpectrumPipeline(QRhi* r)
{
    // FFT vertex buffer: dynamic, updated every frame
    const int maxBins = 4096;
    m_fftVertexBuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                   maxBins * 2 * sizeof(float));
    m_fftVertexBuf->create();

    // Fill buffer: triangle strip (2 vertices per bin: top + bottom)
    m_fftFillBuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                 maxBins * 4 * sizeof(float));
    m_fftFillBuf->create();

    m_fftUniformBuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80);
    m_fftUniformBuf->create();

    m_fftSrb = r->newShaderResourceBindings();
    m_fftSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_fftUniformBuf),
    });

    QShader vs = loadShader(":/shaders/spectrum.vert.qsb");
    QShader fs = loadShader(":/shaders/spectrum.frag.qsb");

    QRhiVertexInputLayout layout;
    layout.setBindings({{2 * sizeof(float)}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});

    // Line pipeline
    m_fftLinePipeline = r->newGraphicsPipeline();
    m_fftLinePipeline->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    m_fftLinePipeline->setVertexInputLayout(layout);
    m_fftLinePipeline->setTopology(QRhiGraphicsPipeline::LineStrip);
    m_fftLinePipeline->setShaderResourceBindings(m_fftSrb);
    m_fftLinePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    // Fill pipeline (triangle strip with alpha blending)
    m_fftFillPipeline = r->newGraphicsPipeline();
    m_fftFillPipeline->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    m_fftFillPipeline->setVertexInputLayout(layout);
    m_fftFillPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_fftFillPipeline->setShaderResourceBindings(m_fftSrb);
    m_fftFillPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_fftFillPipeline->setTargetBlends({blend});
}

void GpuSpectrumRenderer::createColorLut(QRhi* r, QRhiResourceUpdateBatch* batch)
{
    // 256x1 RGBA8 texture with the 7-stop gradient
    m_colorLut = r->newTexture(QRhiTexture::RGBA8, QSize(256, 1));
    m_colorLut->create();

    QByteArray lutData(256 * 4, 0);
    auto* px = reinterpret_cast<uchar*>(lutData.data());
    for (int i = 0; i < 256; ++i) {
        float t = i / 255.0f;
        float seg = t * (kGradientStops - 1);
        int idx = qBound(0, static_cast<int>(seg), kGradientStops - 2);
        float frac = seg - idx;

        int r0 = kGradient[idx].red(), g0 = kGradient[idx].green(), b0 = kGradient[idx].blue();
        int r1 = kGradient[idx+1].red(), g1 = kGradient[idx+1].green(), b1 = kGradient[idx+1].blue();

        px[i * 4 + 0] = static_cast<uchar>(r0 + frac * (r1 - r0));
        px[i * 4 + 1] = static_cast<uchar>(g0 + frac * (g1 - g0));
        px[i * 4 + 2] = static_cast<uchar>(b0 + frac * (b1 - b0));
        px[i * 4 + 3] = 255;
    }

    QRhiTextureSubresourceUploadDescription sub(lutData.constData(), lutData.size());
    batch->uploadTexture(m_colorLut, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, sub)}));

    // Now that m_colorLut is created, finalize the waterfall SRB
    m_wfSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_wfUniformBuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_wfTexture, m_wfSampler),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_colorLut, m_lutSampler),
    });
    m_wfSrb->create();
    m_wfPipeline->create();
    m_fftSrb->create();
    m_fftLinePipeline->create();
    m_fftFillPipeline->create();

    // Upload quad data
    static const float quadData[] = {
        -1, -1,  0, 1,
         1, -1,  1, 1,
        -1,  1,  0, 0,
         1,  1,  1, 0,
    };
    batch->uploadStaticBuffer(m_wfVertexBuf, quadData);
}

// ─── Data updates ────────────────────────────────────────────────────────────

void GpuSpectrumRenderer::pushWaterfallRow(const QVector<float>& intensities)
{
    m_wfPendingRow = intensities;
    m_wfRowDirty = true;
    update();  // trigger render
}

void GpuSpectrumRenderer::updateSpectrum(const QVector<float>& binsDbm)
{
    m_fftBins = binsDbm;
    m_fftDirty = true;
    update();
}

void GpuSpectrumRenderer::setDbmRange(float minDbm, float maxDbm)
{
    m_minDbm = minDbm;
    m_maxDbm = maxDbm;
    update();
}

void GpuSpectrumRenderer::setSpectrumFraction(float frac) { m_spectrumFrac = frac; update(); }
void GpuSpectrumRenderer::setWaterfallBlackLevel(float level) { m_wfBlackLevel = level; update(); }
void GpuSpectrumRenderer::setWaterfallColorRange(float range) { m_wfColorRange = range; update(); }
void GpuSpectrumRenderer::setFftLineColor(const QColor& c) { m_fftLineColor = c; update(); }
void GpuSpectrumRenderer::setFftFillColor(const QColor& c) { m_fftFillColor = c; update(); }
void GpuSpectrumRenderer::setFftFillAlpha(float alpha) { m_fftFillAlpha = alpha; update(); }

void GpuSpectrumRenderer::setOverlayImage(const QImage& img)
{
    m_overlayImage = img.convertToFormat(QImage::Format_RGBA8888);
    m_overlayDirty = true;
    update();
}

void GpuSpectrumRenderer::initOverlayPipeline(QRhi* r)
{
    // Start with a 1x1 placeholder texture — resized on first overlay upload
    m_overlayTexture = r->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    m_overlayTexture->create();

    m_overlaySampler = r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_overlaySampler->create();

    m_overlaySrb = r->newShaderResourceBindings();
    m_overlaySrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                   m_overlayTexture, m_overlaySampler),
    });
    m_overlaySrb->create();

    m_overlayPipeline = r->newGraphicsPipeline();

    QShader vs = loadShader(":/shaders/overlay.vert.qsb");
    QShader fs = loadShader(":/shaders/overlay.frag.qsb");
    m_overlayPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    QRhiVertexInputLayout layout;
    layout.setBindings({{4 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    m_overlayPipeline->setVertexInputLayout(layout);
    m_overlayPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_overlayPipeline->setShaderResourceBindings(m_overlaySrb);
    m_overlayPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_overlayPipeline->setTargetBlends({blend});
    m_overlayPipeline->create();
}

// ─── Upload helpers ──────────────────────────────────────────────────────────

void GpuSpectrumRenderer::uploadWaterfallRow(QRhiResourceUpdateBatch* batch)
{
    if (!m_wfRowDirty || m_wfPendingRow.isEmpty() || !m_wfTexture) return;

    // Check if texture needs resizing (widget may have been 0-width at init)
    const int curWidth = qMax(width(), 64);
    if (curWidth != m_wfWidth && rhi()) {
        m_wfWidth = curWidth;
        delete m_wfTexture;
        m_wfTexture = rhi()->newTexture(QRhiTexture::R32F, QSize(m_wfWidth, m_wfHeight));
        m_wfTexture->create();
        m_wfWriteRow = 0;
        // Rebuild SRB with new texture
        m_wfSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_wfUniformBuf),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_wfTexture, m_wfSampler),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_colorLut, m_lutSampler),
        });
        m_wfSrb->create();
    }

    // Resize row to texture width
    QVector<float> row(m_wfWidth, 0.0f);
    const int srcSize = m_wfPendingRow.size();
    for (int x = 0; x < m_wfWidth; ++x) {
        int srcIdx = x * srcSize / m_wfWidth;
        if (srcIdx >= 0 && srcIdx < srcSize)
            row[x] = m_wfPendingRow[srcIdx];
    }

    // Upload single row at ring buffer position
    QRhiTextureSubresourceUploadDescription sub(row.constData(), row.size() * sizeof(float));
    sub.setDestinationTopLeft(QPoint(0, m_wfWriteRow));
    sub.setSourceSize(QSize(m_wfWidth, 1));
    batch->uploadTexture(m_wfTexture, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, sub)}));

    m_wfWriteRow = (m_wfWriteRow + 1) % m_wfHeight;
    m_wfRowDirty = false;
}

// ─── Render ──────────────────────────────────────────────────────────────────

void GpuSpectrumRenderer::render(QRhiCommandBuffer* cb)
{
    if (!m_initialized) {
        initialize(cb);
        if (!m_initialized) return;
    }
    if (!m_wfPipeline || !m_fftLinePipeline) return;

    QRhi* r = rhi();
    auto* batch = r->nextResourceUpdateBatch();

    // Upload pending waterfall row
    uploadWaterfallRow(batch);

    // Update waterfall uniforms
    {
        float uniforms[4] = {
            static_cast<float>(m_wfWriteRow) / m_wfHeight,  // rowOffset
            m_wfBlackLevel,
            m_wfColorRange,
            0.0f,
        };
        batch->updateDynamicBuffer(m_wfUniformBuf, 0, sizeof(uniforms), uniforms);
    }

    // Update FFT vertex data
    if (m_fftDirty && !m_fftBins.isEmpty()) {
        const int count = qMin(m_fftBins.size(), 4096);
        // Line vertices: x (0-1), y (dBm)
        QVector<float> verts(count * 2);
        for (int i = 0; i < count; ++i) {
            verts[i * 2]     = static_cast<float>(i) / (count - 1);
            verts[i * 2 + 1] = m_fftBins[i];
        }
        batch->updateDynamicBuffer(m_fftVertexBuf, 0, count * 2 * sizeof(float), verts.constData());

        // Fill vertices: triangle strip (top = dBm, bottom = minDbm)
        QVector<float> fill(count * 4);
        for (int i = 0; i < count; ++i) {
            float x = static_cast<float>(i) / (count - 1);
            fill[i * 4]     = x;
            fill[i * 4 + 1] = m_fftBins[i];  // top
            fill[i * 4 + 2] = x;
            fill[i * 4 + 3] = m_minDbm;      // bottom
        }
        batch->updateDynamicBuffer(m_fftFillBuf, 0, count * 4 * sizeof(float), fill.constData());
        m_fftDirty = false;
    }

    // Update FFT uniforms
    {
        // Viewport maps to full clip space (-1 to 1) — hardware viewport handles positioning
        float uniforms[20] = {
            -1.0f, -1.0f, 2.0f, 2.0f,  // full clip space
            m_minDbm, m_maxDbm, m_fftFillAlpha, 0.0f,  // minDbm, maxDbm, fillAlpha, isFill
            // lineColor
            static_cast<float>(m_fftLineColor.redF()),
            static_cast<float>(m_fftLineColor.greenF()),
            static_cast<float>(m_fftLineColor.blueF()),
            1.0f,
            // fillColor
            static_cast<float>(m_fftFillColor.redF()),
            static_cast<float>(m_fftFillColor.greenF()),
            static_cast<float>(m_fftFillColor.blueF()),
            static_cast<float>(m_fftFillAlpha),
            0, 0, 0, 0,  // padding
        };
        batch->updateDynamicBuffer(m_fftUniformBuf, 0, sizeof(uniforms), uniforms);
    }

    // Begin render pass
    const QColor clearColor(0x0a, 0x0a, 0x14);
    cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, batch);

    // QRhi viewport: Y=0 is bottom-left (OpenGL convention).
    // FFT is at the TOP of the widget = high Y in viewport.
    // Waterfall is at the BOTTOM = low Y in viewport.
    const QSize sz = renderTarget()->pixelSize();
    const int specH = static_cast<int>(sz.height() * m_spectrumFrac);
    const int wfH = sz.height() - specH;

    // Draw waterfall (bottom portion of widget = low Y in viewport)
    cb->setGraphicsPipeline(m_wfPipeline);
    cb->setShaderResources(m_wfSrb);
    cb->setViewport({0, 0, static_cast<float>(sz.width()), static_cast<float>(wfH)});
    const QRhiCommandBuffer::VertexInput wfVbuf(m_wfVertexBuf, 0);
    cb->setVertexInput(0, 1, &wfVbuf);
    cb->draw(4);

    // Draw FFT (top portion of widget = high Y in viewport)
    if (!m_fftBins.isEmpty()) {
        cb->setViewport({0, static_cast<float>(wfH),
                         static_cast<float>(sz.width()), static_cast<float>(specH)});

        // Fill pass — set isFill=1.0 in uniform
        {
            float isFill = 1.0f;
            auto* fillBatch = rhi()->nextResourceUpdateBatch();
            fillBatch->updateDynamicBuffer(m_fftUniformBuf, 7 * sizeof(float), sizeof(float), &isFill);
            cb->resourceUpdate(fillBatch);
        }
        cb->setGraphicsPipeline(m_fftFillPipeline);
        cb->setShaderResources(m_fftSrb);
        const QRhiCommandBuffer::VertexInput fillVbuf(m_fftFillBuf, 0);
        cb->setVertexInput(0, 1, &fillVbuf);
        cb->draw(m_fftBins.size() * 2);

        // Line pass — set isFill=0.0
        {
            float isFill = 0.0f;
            auto* lineBatch = rhi()->nextResourceUpdateBatch();
            lineBatch->updateDynamicBuffer(m_fftUniformBuf, 7 * sizeof(float), sizeof(float), &isFill);
            cb->resourceUpdate(lineBatch);
        }
        cb->setGraphicsPipeline(m_fftLinePipeline);
        cb->setShaderResources(m_fftSrb);
        const QRhiCommandBuffer::VertexInput lineVbuf(m_fftVertexBuf, 0);
        cb->setVertexInput(0, 1, &lineVbuf);
        cb->draw(m_fftBins.size());
    }

    // Draw overlay (QPainter-rendered QImage, alpha-blended)
    if (m_overlayPipeline && !m_overlayImage.isNull()) {
        // Upload overlay texture if dirty or resized
        if (m_overlayDirty) {
            const QSize imgSize = m_overlayImage.size();
            if (!m_overlayTexture || m_overlayTexture->pixelSize() != imgSize) {
                delete m_overlayTexture;
                m_overlayTexture = rhi()->newTexture(QRhiTexture::RGBA8, imgSize);
                m_overlayTexture->create();
                // Rebuild SRB with new texture
                m_overlaySrb->setBindings({
                    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                               m_overlayTexture, m_overlaySampler),
                });
                m_overlaySrb->create();
            }
            QRhiTextureSubresourceUploadDescription sub(m_overlayImage);
            auto* overlayBatch = rhi()->nextResourceUpdateBatch();
            overlayBatch->uploadTexture(m_overlayTexture,
                QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, sub)}));
            cb->resourceUpdate(overlayBatch);
            m_overlayDirty = false;
        }

        // Draw fullscreen overlay quad
        cb->setGraphicsPipeline(m_overlayPipeline);
        cb->setShaderResources(m_overlaySrb);
        cb->setViewport({0, 0, static_cast<float>(sz.width()), static_cast<float>(sz.height())});
        const QRhiCommandBuffer::VertexInput ovVbuf(m_wfVertexBuf, 0);  // reuse quad
        cb->setVertexInput(0, 1, &ovVbuf);
        cb->draw(4);
    }

    cb->endPass();
}

} // namespace AetherSDR
