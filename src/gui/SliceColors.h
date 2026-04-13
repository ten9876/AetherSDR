#pragma once

// Shared per-slice colour definitions used by SpectrumWidget, VfoWidget, and
// RxApplet to ensure all slice markers/badges use identical colours.
// Index by sliceId % kSliceColorCount.

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
    {0xff, 0x80, 0x00,  0x80, 0x40, 0x00, "#ff8000"},  // E = orange
    {0x80, 0x80, 0xff,  0x40, 0x40, 0x80, "#8080ff"},  // F = periwinkle
    {0xff, 0x60, 0x60,  0x80, 0x30, 0x30, "#ff6060"},  // G = coral
    {0x00, 0xff, 0xc0,  0x00, 0x80, 0x60, "#00ffc0"},  // H = teal
};

inline constexpr int kSliceColorCount = 8;

} // namespace AetherSDR
