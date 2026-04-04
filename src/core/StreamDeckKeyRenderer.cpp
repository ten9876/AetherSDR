#ifdef HAVE_HIDAPI
#include "StreamDeckKeyRenderer.h"
#include "StreamDeckDevice.h"

#include <QPainter>
#include <QBuffer>
#include <QImageWriter>
#include <cmath>

namespace AetherSDR {

QByteArray StreamDeckKeyRenderer::render(const StreamDeckDeviceInfo& info,
                                          const SDKeyStyle& style)
{
    if (info.keyWidth <= 0 || info.keyHeight <= 0)
        return {};

    QImage img = createKeyImage(info.keyWidth, info.keyHeight, style);
    img = applyTransform(img, info.flipH, info.flipV, info.rotation);

    return info.useJpeg ? encodeJpeg(img) : encodeBmp(img);
}

QByteArray StreamDeckKeyRenderer::renderTouchscreen(int width, int height,
                                                      const QString& text,
                                                      float meterValue)
{
    QImage img(width, height, QImage::Format_RGB888);
    img.fill(QColor(0x0F, 0x0F, 0x1A));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // Meter bar background
    int barH = height - 20;
    int barY = 10;
    p.fillRect(4, barY, width - 8, barH, QColor(0x20, 0x30, 0x40));

    // Meter fill
    int fillW = static_cast<int>((width - 8) * std::clamp(meterValue, 0.0f, 1.0f));
    QColor fillColor = meterValue < 0.7f ? QColor(0x00, 0xB4, 0xD8)
                      : meterValue < 0.9f ? QColor(0xE0, 0xD0, 0x60)
                      : QColor(0xE0, 0x60, 0x40);
    p.fillRect(4, barY, fillW, barH, fillColor);

    // Text overlay
    if (!text.isEmpty()) {
        QFont f = p.font();
        f.setPixelSize(height - 24);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xC8, 0xD8, 0xE8));
        p.drawText(img.rect(), Qt::AlignCenter, text);
    }

    return encodeJpeg(img);
}

QImage StreamDeckKeyRenderer::createKeyImage(int w, int h, const SDKeyStyle& style)
{
    QImage img(w, h, QImage::Format_RGB888);
    img.fill(style.background);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    const int pad = 4;
    const QRect inner(pad, pad, w - 2 * pad, h - 2 * pad);

    switch (style.type) {
    case SDKeyType::Blank:
        break;

    case SDKeyType::TextLabel: {
        QFont f = p.font();
        f.setPixelSize(w / 5);
        f.setBold(true);
        p.setFont(f);
        p.setPen(style.foreground);
        p.drawText(inner, Qt::AlignCenter | Qt::TextWordWrap, style.text);
        if (!style.subtext.isEmpty()) {
            f.setPixelSize(w / 7);
            f.setBold(false);
            p.setFont(f);
            p.setPen(QColor(0x80, 0x90, 0xA0));
            p.drawText(inner.adjusted(0, inner.height() / 2, 0, 0),
                       Qt::AlignHCenter | Qt::AlignBottom, style.subtext);
        }
        break;
    }

    case SDKeyType::FrequencyDisplay: {
        // Frequency text centered
        QFont f = p.font();
        f.setPixelSize(w / 6);
        f.setBold(true);
        p.setFont(f);
        p.setPen(style.accent);
        p.drawText(inner, Qt::AlignCenter, style.text);
        // Subtext (mode) below
        if (!style.subtext.isEmpty()) {
            f.setPixelSize(w / 8);
            p.setFont(f);
            p.setPen(style.foreground);
            p.drawText(inner.adjusted(0, inner.height() * 2 / 3, 0, 0),
                       Qt::AlignHCenter | Qt::AlignBottom, style.subtext);
        }
        break;
    }

    case SDKeyType::ModeButton:
    case SDKeyType::BandButton: {
        // Rounded rect background with highlight if active
        QColor bg = style.active ? style.accent : QColor(0x1A, 0x2A, 0x3A);
        QColor fg = style.active ? QColor(0x0F, 0x0F, 0x1A) : style.foreground;
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(inner, 8, 8);

        QFont f = p.font();
        f.setPixelSize(w / 4);
        f.setBold(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(inner, Qt::AlignCenter, style.text);
        break;
    }

    case SDKeyType::MeterGauge: {
        // Label at top
        QFont f = p.font();
        f.setPixelSize(w / 7);
        f.setBold(true);
        p.setFont(f);
        p.setPen(style.foreground);
        p.drawText(inner.adjusted(0, 0, 0, -inner.height() / 2),
                   Qt::AlignHCenter | Qt::AlignTop, style.text);

        // Horizontal bar
        int barY = h / 2;
        int barH = h / 4;
        p.fillRect(pad, barY, inner.width(), barH, QColor(0x20, 0x30, 0x40));
        int fillW = static_cast<int>(inner.width() * std::clamp(style.meterValue, 0.0f, 1.0f));
        QColor fc = style.meterValue < 0.7f ? style.accent
                   : style.meterValue < 0.9f ? QColor(0xE0, 0xD0, 0x60)
                   : QColor(0xE0, 0x60, 0x40);
        p.fillRect(pad, barY, fillW, barH, fc);

        // Value text below bar
        if (!style.subtext.isEmpty()) {
            f.setPixelSize(w / 8);
            p.setFont(f);
            p.setPen(QColor(0x80, 0x90, 0xA0));
            p.drawText(inner.adjusted(0, barY + barH - pad, 0, 0),
                       Qt::AlignHCenter | Qt::AlignTop, style.subtext);
        }
        break;
    }

    case SDKeyType::ToggleButton: {
        QColor bg = style.active ? style.activeColor : QColor(0x1A, 0x2A, 0x3A);
        QColor fg = style.active ? QColor(0xFF, 0xFF, 0xFF) : style.foreground;
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(inner, 8, 8);

        QFont f = p.font();
        f.setPixelSize(w / 4);
        f.setBold(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(inner, Qt::AlignCenter, style.text);
        break;
    }

    case SDKeyType::StatusIndicator: {
        // Colored dot
        int dotR = w / 6;
        QColor dotColor = style.active ? style.activeColor : QColor(0x60, 0x60, 0x60);
        p.setBrush(dotColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(w / 2, h / 3), dotR, dotR);

        // Label below
        QFont f = p.font();
        f.setPixelSize(w / 6);
        f.setBold(true);
        p.setFont(f);
        p.setPen(style.foreground);
        p.drawText(inner.adjusted(0, h / 2 - pad, 0, 0),
                   Qt::AlignHCenter | Qt::AlignTop, style.text);
        break;
    }
    }

    return img;
}

QImage StreamDeckKeyRenderer::applyTransform(const QImage& img,
                                               bool flipH, bool flipV, int rotation)
{
    QImage result = img;
    if (flipH || flipV)
        result = result.mirrored(flipH, flipV);
    if (rotation != 0) {
        QTransform t;
        t.rotate(-rotation);  // PIL rotate() is CCW, Qt rotate() is CW
        result = result.transformed(t);
    }
    return result;
}

QByteArray StreamDeckKeyRenderer::encodeJpeg(const QImage& img, int quality)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", quality);
    return data;
}

QByteArray StreamDeckKeyRenderer::encodeBmp(const QImage& img)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "BMP");
    return data;
}

} // namespace AetherSDR
#endif
