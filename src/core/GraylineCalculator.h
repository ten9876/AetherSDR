#pragma once

#include <QImage>
#include <QDateTime>
#include <QVector>
#include <QPointF>

namespace AetherSDR {

// Computes the solar terminator (day/night boundary) from UTC time using
// standard solar declination math. Pure geometry, no network calls.
//
// The render methods produce equirectangular map backgrounds and grayline
// overlays suitable for compositing onto the SpectrumWidget background.
class GraylineCalculator {
public:
    // Map background style presets.
    enum class MapStyle : int {
        None  = 0,   // No map background
        Light = 1,   // Light-colored world map
        Dark  = 2,   // Dark-colored world map
    };

    // Render a world map background image at the given size.
    // Returns a null QImage if style == None.
    static QImage renderMapBackground(MapStyle style, const QSize& size);

    // Render a semi-transparent grayline (night-side) overlay at the given size.
    // The overlay darkens the night side of an equirectangular map projection.
    // utc: the time to compute the terminator for (default = now).
    static QImage renderGraylineOverlay(const QSize& size,
                                        const QDateTime& utc = QDateTime::currentDateTimeUtc());

    // Compute the solar declination in degrees for a given UTC datetime.
    static double solarDeclination(const QDateTime& utc);

    // Compute the sub-solar longitude in degrees for a given UTC datetime.
    static double subSolarLongitude(const QDateTime& utc);

    // Compute the terminator curve as a list of (longitude, latitude) points.
    // Returns 360 points, one per degree of longitude (-180 to +179).
    static QVector<QPointF> terminatorCurve(const QDateTime& utc);
};

} // namespace AetherSDR
