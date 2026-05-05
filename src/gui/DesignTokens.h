#pragma once

#include <QLatin1StringView>

namespace AetherSDR::DesignTokens {

// ── Surfaces ────────────────────────────────────────────────────────────────
constexpr QLatin1StringView kSurfaceBase   {"#0f0f1a"};
constexpr QLatin1StringView kSurfacePanel  {"#141422"};
constexpr QLatin1StringView kSurfaceSunken {"#0a0a14"};
constexpr QLatin1StringView kSurfaceOverlay{"#1c2030"};

// ── Borders ──────────────────────────────────────────────────────────────────
constexpr QLatin1StringView kBorderSubtle     {"#1a2535"};
constexpr QLatin1StringView kBorderControl    {"#253545"};
constexpr QLatin1StringView kBorderInteractive{"#2a4060"};

// ── Text ──────────────────────────────────────────────────────────────────────
constexpr QLatin1StringView kTextPrimary   {"#c8d8e8"};
constexpr QLatin1StringView kTextSecondary {"#7a8fa0"};
constexpr QLatin1StringView kTextTertiary  {"#404f5e"};
constexpr QLatin1StringView kTextAccent    {"#00b4d8"};

// ── Semantic ──────────────────────────────────────────────────────────────────
constexpr QLatin1StringView kColorAccent  {"#00b4d8"};
constexpr QLatin1StringView kColorDanger  {"#cc3030"};
constexpr QLatin1StringView kColorWarning {"#d08020"};
constexpr QLatin1StringView kColorSuccess {"#20a060"};

// ── Control metrics (used in both stylesheets and layout code) ────────────────
constexpr int kCtrlRadius    = 3;
constexpr int kCtrlBtnHeight = 22;
constexpr int kCtrlSliderH   = 4;
constexpr int kCtrlHandleW   = 10;
constexpr int kFontSm        = 11;
constexpr int kFontMd        = 13;
constexpr int kFontLg        = 16;
constexpr int kFontFreq      = 22;

} // namespace AetherSDR::DesignTokens
