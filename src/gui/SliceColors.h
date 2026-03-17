#pragma once

// Shared per-slice colour definitions used by SpectrumWidget, VfoWidget, and
// RxApplet to ensure all slice markers/badges use identical colours.
// Index by sliceId % 4.

namespace AetherSDR {

struct SliceColorEntry {
    int r, g, b;           // active colour (bright)
    int dr, dg, db;        // inactive / dim colour
    const char* hexActive; // for CSS stylesheets
};

inline constexpr SliceColorEntry kSliceColors[] = {
    {0x00, 0xd4, 0xff,  0x00, 0x60, 0x80, "#00d4ff"},  // A = cyan
    {0xff, 0x40, 0xff,  0x80, 0x20, 0x80, "#ff40ff"},  // B = magenta
    {0x40, 0xff, 0x40,  0x20, 0x80, 0x20, "#40ff40"},  // C = green
    {0xff, 0xff, 0x00,  0x80, 0x80, 0x00, "#ffff00"},  // D = yellow
};

inline constexpr int kSliceColorCount = 4;

} // namespace AetherSDR
