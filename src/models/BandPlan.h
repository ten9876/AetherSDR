#pragma once

#include <QColor>

namespace AetherSDR {

// ARRL band plan sub-segments (US allocation)
struct BandSegment {
    double lowMhz;
    double highMhz;
    const char* label;   // "CW", "DIGI", "SSB", "BCN", etc.
    int r, g, b;         // segment color
};

// Colors: CW=blue, Digital=green, SSB/Phone=yellow, Beacon=cyan, Mixed=orange
inline constexpr BandSegment kBandPlan[] = {
    // 160m (1.800 - 2.000)
    {1.800,  1.810,  "CW",    0x30, 0x80, 0xff},
    {1.810,  1.840,  "CW",    0x30, 0x80, 0xff},
    {1.840,  2.000,  "SSB",   0xff, 0xc0, 0x30},

    // 80m (3.500 - 4.000)
    {3.500,  3.600,  "CW",    0x30, 0x80, 0xff},
    {3.600,  3.700,  "DIGI",  0x30, 0xc0, 0x30},
    {3.700,  4.000,  "SSB",   0xff, 0xc0, 0x30},

    // 60m (5.330 - 5.405) — channelized, all modes
    {5.330,  5.405,  "ALL",   0x80, 0x80, 0x80},

    // 40m (7.000 - 7.300)
    {7.000,  7.025,  "CW",    0x30, 0x80, 0xff},
    {7.025,  7.125,  "DIGI",  0x30, 0xc0, 0x30},
    {7.125,  7.175,  "SSB",   0xff, 0xc0, 0x30},
    {7.175,  7.300,  "SSB",   0xff, 0xc0, 0x30},

    // 30m (10.100 - 10.150)
    {10.100, 10.130, "CW",    0x30, 0x80, 0xff},
    {10.130, 10.150, "DIGI",  0x30, 0xc0, 0x30},

    // 20m (14.000 - 14.350)
    {14.000, 14.025, "CW",    0x30, 0x80, 0xff},
    {14.025, 14.070, "CW",    0x30, 0x80, 0xff},
    {14.070, 14.095, "DIGI",  0x30, 0xc0, 0x30},
    {14.095, 14.099, "DIGI",  0x30, 0xc0, 0x30},
    {14.099, 14.101, "BCN",   0x00, 0xd0, 0xd0},
    {14.101, 14.150, "SSB",   0xff, 0xc0, 0x30},
    {14.150, 14.225, "SSB",   0xff, 0xc0, 0x30},
    {14.225, 14.350, "SSB",   0xff, 0xc0, 0x30},

    // 17m (18.068 - 18.168)
    {18.068, 18.100, "CW",    0x30, 0x80, 0xff},
    {18.100, 18.105, "DIGI",  0x30, 0xc0, 0x30},
    {18.105, 18.110, "BCN",   0x00, 0xd0, 0xd0},
    {18.110, 18.168, "SSB",   0xff, 0xc0, 0x30},

    // 15m (21.000 - 21.450)
    {21.000, 21.025, "CW",    0x30, 0x80, 0xff},
    {21.025, 21.070, "CW",    0x30, 0x80, 0xff},
    {21.070, 21.110, "DIGI",  0x30, 0xc0, 0x30},
    {21.110, 21.150, "DIGI",  0x30, 0xc0, 0x30},
    {21.150, 21.151, "BCN",   0x00, 0xd0, 0xd0},
    {21.151, 21.200, "SSB",   0xff, 0xc0, 0x30},
    {21.200, 21.450, "SSB",   0xff, 0xc0, 0x30},

    // 12m (24.890 - 24.990)
    {24.890, 24.920, "CW",    0x30, 0x80, 0xff},
    {24.920, 24.925, "DIGI",  0x30, 0xc0, 0x30},
    {24.925, 24.930, "BCN",   0x00, 0xd0, 0xd0},
    {24.930, 24.990, "SSB",   0xff, 0xc0, 0x30},

    // 10m (28.000 - 29.700)
    {28.000, 28.070, "CW",    0x30, 0x80, 0xff},
    {28.070, 28.150, "DIGI",  0x30, 0xc0, 0x30},
    {28.150, 28.190, "CW",    0x30, 0x80, 0xff},
    {28.190, 28.200, "BCN",   0x00, 0xd0, 0xd0},
    {28.200, 28.300, "BCN",   0x00, 0xd0, 0xd0},
    {28.300, 29.000, "SSB",   0xff, 0xc0, 0x30},
    {29.000, 29.200, "DIGI",  0x30, 0xc0, 0x30},
    {29.200, 29.300, "SSB",   0xff, 0xc0, 0x30},
    {29.300, 29.510, "SAT",   0xc0, 0x40, 0xc0},
    {29.510, 29.700, "FM",    0xc0, 0x80, 0x00},

    // 6m (50.000 - 54.000)
    {50.000, 50.100, "CW",    0x30, 0x80, 0xff},
    {50.100, 50.300, "SSB",   0xff, 0xc0, 0x30},
    {50.300, 50.600, "DIGI",  0x30, 0xc0, 0x30},
    {50.600, 51.000, "SSB",   0xff, 0xc0, 0x30},
    {51.000, 52.000, "FM",    0xc0, 0x80, 0x00},
    {52.000, 54.000, "ALL",   0x80, 0x80, 0x80},
};

inline constexpr int kBandPlanCount = static_cast<int>(std::size(kBandPlan));

} // namespace AetherSDR
