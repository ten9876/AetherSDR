#pragma once

#include <QString>
#include <cmath>

namespace AetherSDR {

// Format a frequency in MHz as grouped "XX.XXX.XXX" plain text.
inline QString formatFreqGrouped(double mhz)
{
    long long hz = static_cast<long long>(std::round(mhz * 1e6));
    int mhzPart = static_cast<int>(hz / 1000000);
    int khzPart = static_cast<int>((hz / 1000) % 1000);
    int hzPart  = static_cast<int>(hz % 1000);
    return QString("%1.%2.%3")
        .arg(mhzPart)
        .arg(khzPart, 3, 10, QChar('0'))
        .arg(hzPart, 3, 10, QChar('0'));
}

// Return the number of least-significant Hz digits that are fixed (don't
// change) for a given step size.  0 means no dimming, 1-3 dims the last
// 1-3 digits of the Hz group.
inline int fixedDigitCount(int stepHz)
{
    if (stepHz >= 1000) return 3;
    if (stepHz >= 100)  return 2;
    if (stepHz >= 10)   return 1;
    return 0;
}

// Format a frequency as grouped HTML with fixed digits dimmed.
// dimColor is the CSS color for the dimmed digits (e.g. "#505868").
// normalColor is the CSS color for normal digits (e.g. "#c8d8e8").
// When dimCount == 0, returns plain text (no HTML tags).
inline QString formatFreqGroupedHtml(double mhz, int stepHz,
                                     const QString& normalColor = QStringLiteral("#c8d8e8"),
                                     const QString& dimColor = QStringLiteral("#505868"))
{
    const QString plain = formatFreqGrouped(mhz);
    const int dimCount = fixedDigitCount(stepHz);
    if (dimCount <= 0)
        return plain;

    // plain is "N.NNN.NNN" — last 3 chars are Hz digits, preceded by a dot.
    // We need to dim the last dimCount digit chars from the right.
    // Walk from the end, count actual digits (skip dots), and split.
    int splitPos = plain.size();
    int digitsSeen = 0;
    for (int i = plain.size() - 1; i >= 0; --i) {
        if (plain[i] == '.')
            continue;
        ++digitsSeen;
        if (digitsSeen == dimCount) {
            splitPos = i;
            break;
        }
    }

    const QString bright = plain.left(splitPos);
    const QString dim    = plain.mid(splitPos);

    return QStringLiteral("<span style=\"color:%1\">%2</span>"
                          "<span style=\"color:%3\">%4</span>")
        .arg(normalColor, bright, dimColor, dim);
}

} // namespace AetherSDR
