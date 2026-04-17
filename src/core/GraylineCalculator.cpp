#include "GraylineCalculator.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace AetherSDR {

// ── Solar geometry helpers ──────────────────────────────────────────────────

static constexpr double kDeg2Rad = M_PI / 180.0;
static constexpr double kRad2Deg = 180.0 / M_PI;

// Day of year (1-based).
static int dayOfYear(const QDateTime& utc)
{
    return utc.date().dayOfYear();
}

// Fractional hours in UTC.
static double utcHours(const QDateTime& utc)
{
    QTime t = utc.time();
    return t.hour() + t.minute() / 60.0 + t.second() / 3600.0;
}

double GraylineCalculator::solarDeclination(const QDateTime& utc)
{
    // Approximate solar declination using the Earth's axial tilt.
    // Formula: decl = -23.44° × cos(360/365 × (dayOfYear + 10))
    int doy = dayOfYear(utc);
    double angle = 360.0 / 365.0 * (doy + 10);
    return -23.44 * qCos(angle * kDeg2Rad);
}

double GraylineCalculator::subSolarLongitude(const QDateTime& utc)
{
    // The sub-solar point longitude is based on UTC hour angle.
    // At 12:00 UTC, the sun is over 0° longitude.
    double hours = utcHours(utc);
    double lng = 180.0 - (hours / 24.0) * 360.0;
    // Normalize to [-180, 180]
    if (lng > 180.0) {
        lng -= 360.0;
    }
    if (lng < -180.0) {
        lng += 360.0;
    }
    return lng;
}

QVector<QPointF> GraylineCalculator::terminatorCurve(const QDateTime& utc)
{
    double decl = solarDeclination(utc) * kDeg2Rad;
    double subSolarLng = subSolarLongitude(utc);

    QVector<QPointF> points;
    points.reserve(360);

    for (int i = 0; i < 360; ++i) {
        double lng = -180.0 + i;
        // Hour angle from sub-solar point
        double ha = (lng - subSolarLng) * kDeg2Rad;
        // Terminator latitude: where solar elevation = 0
        // tan(lat) = -cos(ha) / tan(decl)  — but this breaks at decl≈0
        // Better: lat = atan(-cos(ha) * cos(decl) / sin(decl))
        // Simplified: the terminator satisfies sin(decl)*sin(lat) + cos(decl)*cos(lat)*cos(ha) = 0
        // → lat = atan2(-cos(ha) * cos(decl), sin(decl))
        double lat = qAtan2(-qCos(ha) * qCos(decl), qSin(decl)) * kRad2Deg;
        points.append(QPointF(lng, lat));
    }

    return points;
}

// ── Map rendering ───────────────────────────────────────────────────────────

// Simple coastline data as (lon, lat) polygon segments.
// This is a highly simplified world outline for visual reference.
// Stored as {lon, lat, lon, lat, ...} terminated by 999.
// Uses a minimal set of recognizable landmasses.

// Draw simplified continent outlines on a painter using equirectangular projection.
static void drawContinentOutlines(QPainter& p, const QSize& size, const QColor& lineColor,
                                   const QColor& landColor)
{
    // Map pixel coords: x = (lon + 180) / 360 * width
    //                   y = (90 - lat) / 180 * height
    auto lonToX = [&](double lon) -> double { return (lon + 180.0) / 360.0 * size.width(); };
    auto latToY = [&](double lat) -> double { return (90.0 - lat) / 180.0 * size.height(); };

    // Simplified continent polygons (major landmasses only)
    // Each sub-array is a closed polygon of {lon, lat} pairs.
    struct Poly { const double* data; int count; };

    // North America (simplified)
    static constexpr double northAmerica[] = {
        -130,55, -125,60, -130,70, -140,70, -165,65, -168,55, -155,20, -130,25,
        -115,30, -105,25, -100,20, -85,10, -80,8, -85,25, -80,30, -75,35,
        -65,45, -60,47, -65,50, -55,50, -60,55, -70,55, -75,60, -80,63,
        -85,70, -95,72, -100,70, -110,68, -120,60, -130,55
    };
    // South America (simplified)
    static constexpr double southAmerica[] = {
        -80,8, -77,0, -80,-5, -75,-15, -70,-20, -65,-25, -70,-40, -75,-50,
        -70,-55, -65,-55, -55,-35, -50,-25, -45,-20, -35,-5, -50,0,
        -60,5, -70,10, -75,10, -80,8
    };
    // Europe (simplified)
    static constexpr double europe[] = {
        -10,36, 0,36, 5,38, 10,38, 15,40, 20,38, 25,35, 30,37,
        35,38, 40,42, 45,42, 40,48, 30,46, 25,50, 20,55, 25,58,
        28,60, 30,62, 30,70, 25,70, 20,68, 15,60, 10,55, 5,52,
        0,50, -5,48, -10,44, -10,36
    };
    // Africa (simplified)
    static constexpr double africa[] = {
        -15,10, -17,15, -10,20, -10,30, -5,36, 0,36, 10,37, 15,32,
        25,32, 30,30, 35,30, 40,12, 50,12, 50,0, 45,-10, 40,-15,
        35,-25, 30,-35, 20,-35, 15,-25, 10,-5, 5,5, 0,5, -5,5, -15,10
    };
    // Asia (simplified)
    static constexpr double asia[] = {
        30,37, 35,38, 40,42, 45,42, 50,40, 55,38, 60,40, 65,38,
        70,38, 75,30, 80,28, 85,28, 90,25, 95,18, 100,15, 105,10,
        110,20, 115,25, 120,30, 125,35, 130,35, 135,35, 140,40,
        140,45, 145,50, 140,55, 135,55, 130,50, 125,55, 130,60,
        140,60, 150,60, 160,60, 170,65, 180,65,
        180,70, 130,70, 100,70, 70,70, 50,65, 40,65, 30,62, 30,37
    };
    // Australia (simplified)
    static constexpr double australia[] = {
        115,-35, 120,-35, 130,-32, 135,-35, 140,-38, 150,-38, 152,-30,
        150,-25, 145,-15, 140,-12, 135,-12, 130,-15, 125,-15, 115,-22,
        115,-35
    };

    const Poly polys[] = {
        {northAmerica, sizeof(northAmerica) / sizeof(double) / 2},
        {southAmerica, sizeof(southAmerica) / sizeof(double) / 2},
        {europe,       sizeof(europe)       / sizeof(double) / 2},
        {africa,       sizeof(africa)       / sizeof(double) / 2},
        {asia,         sizeof(asia)         / sizeof(double) / 2},
        {australia,    sizeof(australia)     / sizeof(double) / 2},
    };

    p.setPen(QPen(lineColor, 1.0));
    p.setBrush(landColor);
    p.setRenderHint(QPainter::Antialiasing, true);

    for (const auto& poly : polys) {
        QPainterPath path;
        path.moveTo(lonToX(poly.data[0]), latToY(poly.data[1]));
        for (int i = 1; i < poly.count; ++i) {
            path.lineTo(lonToX(poly.data[i * 2]), latToY(poly.data[i * 2 + 1]));
        }
        path.closeSubpath();
        p.drawPath(path);
    }
}

// Draw latitude/longitude grid lines.
static void drawMapGrid(QPainter& p, const QSize& size, const QColor& gridColor)
{
    p.setPen(QPen(gridColor, 0.5, Qt::DotLine));
    p.setRenderHint(QPainter::Antialiasing, false);

    double w = size.width();
    double h = size.height();

    // Latitude lines every 30°
    for (int lat = -60; lat <= 60; lat += 30) {
        double y = (90.0 - lat) / 180.0 * h;
        p.drawLine(QPointF(0, y), QPointF(w, y));
    }
    // Longitude lines every 30°
    for (int lon = -150; lon <= 180; lon += 30) {
        double x = (lon + 180.0) / 360.0 * w;
        p.drawLine(QPointF(x, 0), QPointF(x, h));
    }
}

QImage GraylineCalculator::renderMapBackground(MapStyle style, const QSize& size)
{
    if (style == MapStyle::None || size.isEmpty()) {
        return {};
    }

    QImage img(size, QImage::Format_ARGB32_Premultiplied);

    if (style == MapStyle::Light) {
        img.fill(QColor(200, 215, 230));  // light blue ocean
        QPainter p(&img);
        drawMapGrid(p, size, QColor(170, 190, 210));
        drawContinentOutlines(p, size, QColor(100, 120, 100), QColor(180, 200, 160));
    } else {
        // Dark
        img.fill(QColor(15, 25, 45));  // dark navy ocean
        QPainter p(&img);
        drawMapGrid(p, size, QColor(30, 45, 65));
        drawContinentOutlines(p, size, QColor(50, 70, 80), QColor(30, 50, 40));
    }

    return img;
}

QImage GraylineCalculator::renderGraylineOverlay(const QSize& size, const QDateTime& utc)
{
    if (size.isEmpty()) {
        return {};
    }

    QVector<QPointF> terminator = terminatorCurve(utc);
    double decl = solarDeclination(utc);

    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    double w = size.width();
    double h = size.height();

    auto lonToX = [&](double lon) -> double { return (lon + 180.0) / 360.0 * w; };
    auto latToY = [&](double lat) -> double { return (90.0 - lat) / 180.0 * h; };

    // Build the night-side polygon.
    // The terminator curve separates day from night. We need to fill the night side.
    // If declination > 0 (northern summer), the night side is below the curve at most points.
    // If declination < 0, it's above.
    QPainterPath nightPath;

    // Start from the left edge of the terminator
    nightPath.moveTo(lonToX(terminator[0].x()), latToY(terminator[0].y()));

    // Follow the terminator curve
    for (int i = 1; i < terminator.size(); ++i) {
        nightPath.lineTo(lonToX(terminator[i].x()), latToY(terminator[i].y()));
    }

    // Close the polygon along the edges.
    // If declination > 0 (sun in northern hemisphere), night is on the south side.
    // We go along the bottom edge (south pole) to close.
    if (decl >= 0) {
        // Night is south of the terminator
        nightPath.lineTo(w, h);  // bottom-right
        nightPath.lineTo(0, h);  // bottom-left
    } else {
        // Night is north of the terminator
        nightPath.lineTo(w, 0);  // top-right
        nightPath.lineTo(0, 0);  // top-left
    }
    nightPath.closeSubpath();

    // Semi-transparent dark overlay for night side
    p.fillPath(nightPath, QColor(0, 0, 0, 100));

    // Draw the terminator line itself
    QPainterPath terminatorLine;
    terminatorLine.moveTo(lonToX(terminator[0].x()), latToY(terminator[0].y()));
    for (int i = 1; i < terminator.size(); ++i) {
        terminatorLine.lineTo(lonToX(terminator[i].x()), latToY(terminator[i].y()));
    }
    p.setPen(QPen(QColor(255, 200, 50, 180), 1.5));
    p.drawPath(terminatorLine);

    // Draw a small sun marker at the sub-solar point
    double sunLng = subSolarLongitude(utc);
    double sunX = lonToX(sunLng);
    double sunY = latToY(decl);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 220, 50, 200));
    p.drawEllipse(QPointF(sunX, sunY), 4.0, 4.0);

    return img;
}

} // namespace AetherSDR
