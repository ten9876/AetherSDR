#pragma once
#ifdef HAVE_HIDAPI

#include <QImage>
#include <QByteArray>
#include <QString>
#include <QColor>

namespace AetherSDR {

struct StreamDeckDeviceInfo;

enum class SDKeyType {
    Blank,
    TextLabel,
    FrequencyDisplay,
    ModeButton,
    BandButton,
    MeterGauge,
    ToggleButton,
    StatusIndicator,
};

struct SDKeyStyle {
    SDKeyType type{SDKeyType::Blank};
    QString  text;
    QString  subtext;
    QColor   background{0x0F, 0x0F, 0x1A};
    QColor   foreground{0xC8, 0xD8, 0xE8};
    QColor   accent{0x00, 0xB4, 0xD8};
    QColor   activeColor{0x20, 0xC0, 0x60};
    bool     active{false};
    float    meterValue{0.0f};  // 0.0 - 1.0
};

class StreamDeckKeyRenderer {
public:
    // Render a key image for the given device, returns JPEG or BMP bytes
    static QByteArray render(const StreamDeckDeviceInfo& info, const SDKeyStyle& style);

    // Render a touchscreen strip image
    static QByteArray renderTouchscreen(int width, int height,
                                         const QString& text, float meterValue = 0.0f);

private:
    static QImage createKeyImage(int w, int h, const SDKeyStyle& style);
    static QImage applyTransform(const QImage& img, bool flipH, bool flipV, int rotation);
    static QByteArray encodeJpeg(const QImage& img, int quality = 80);
    static QByteArray encodeBmp(const QImage& img);
};

} // namespace AetherSDR
#endif
