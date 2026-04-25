#include "XvtrPolicy.h"

#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace AetherSDR::XvtrPolicy {

namespace {

bool isNativeBandKey(const QString& key)
{
    static const QSet<QString> kNativeBandKeys = {
        QStringLiteral("160"), QStringLiteral("80"), QStringLiteral("60"),
        QStringLiteral("40"),  QStringLiteral("30"), QStringLiteral("20"),
        QStringLiteral("17"),  QStringLiteral("15"), QStringLiteral("12"),
        QStringLiteral("10"),  QStringLiteral("6"),
        QStringLiteral("2200"), QStringLiteral("630")
    };
    return kNativeBandKeys.contains(key);
}

QString normalizedNativeBandKey(const QString& bandName)
{
    QString key = bandName;
    if (key.endsWith('m') && key.length() > 1) {
        const QString stripped = key.chopped(1);
        if (isNativeBandKey(stripped))
            key = stripped;
    }
    return key;
}

double tileBandwidth(double lowMhz, double highMhz)
{
    return highMhz - lowMhz;
}

double tileCenter(double lowMhz, double highMhz)
{
    return (lowMhz + highMhz) / 2.0;
}

} // namespace

BandStackKeyResult resolveBandStackKey(const QString& bandName,
                                       const QVector<Transverter>& xvtrs)
{
    static const QMap<QString, int> kNumericBandSlots = {
        { QStringLiteral("WWV"), 33 },
        { QStringLiteral("GEN"), 34 },
    };

    const QString radioKey = normalizedNativeBandKey(bandName);
    if (isNativeBandKey(radioKey))
        return {radioKey, {}};

    if (kNumericBandSlots.contains(bandName))
        return {QString::number(kNumericBandSlots.value(bandName)), {}};

    for (const auto& xvtr : xvtrs) {
        if (!xvtr.isValid || xvtr.name != bandName)
            continue;

        if (xvtr.order < 0) {
            return {
                {},
                QString("XVTR %1 has no setup order; cannot form Flex band= key")
                    .arg(bandName)
            };
        }

        return {QString("X%1").arg(xvtr.order), {}};
    }

    return {
        {},
        QString("Band %1 has no Flex display pan band= mapping").arg(bandName)
    };
}

bool isWaterfallTileOutsidePan(double lowMhz, double highMhz, double panCenterMhz)
{
    const double bw = tileBandwidth(lowMhz, highMhz);
    if (bw <= 0.0)
        return false;

    return std::abs(tileCenter(lowMhz, highMhz) - panCenterMhz) > bw;
}

bool waterfallTileMatchesTransverterOffset(double lowMhz, double highMhz,
                                           double panCenterMhz,
                                           const QVector<Transverter>& xvtrs)
{
    const double bw = tileBandwidth(lowMhz, highMhz);
    if (bw <= 0.0 || !isWaterfallTileOutsidePan(lowMhz, highMhz, panCenterMhz))
        return false;

    const double observedOffset = panCenterMhz - tileCenter(lowMhz, highMhz);
    const double toleranceMhz = std::max(bw, 0.25);
    for (const auto& xvtr : xvtrs) {
        if (!xvtr.isValid || xvtr.rfFreqMhz <= 0.0 || xvtr.ifFreqMhz <= 0.0)
            continue;

        const double expectedOffset = xvtr.rfFreqMhz - xvtr.ifFreqMhz;
        if (std::abs(observedOffset - expectedOffset) <= toleranceMhz)
            return true;
    }

    return false;
}

WaterfallTileRange mapWaterfallTileRange(double lowMhz, double highMhz,
                                         double panCenterMhz,
                                         const QVector<Transverter>& xvtrs,
                                         bool hasXvtrSliceAntenna)
{
    if (!isWaterfallTileOutsidePan(lowMhz, highMhz, panCenterMhz))
        return {lowMhz, highMhz, false};

    if (!hasXvtrSliceAntenna &&
        !waterfallTileMatchesTransverterOffset(lowMhz, highMhz, panCenterMhz, xvtrs)) {
        return {lowMhz, highMhz, false};
    }

    const double offset = panCenterMhz - tileCenter(lowMhz, highMhz);
    return {lowMhz + offset, highMhz + offset, true};
}

} // namespace AetherSDR::XvtrPolicy
