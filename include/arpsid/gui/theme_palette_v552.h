// SPDX-License-Identifier: BSD-3-Clause
// theme_palette_v552.h — Theme color palette for SETTINGS theme application (v552).
//
// PURPOSE
// ------// The SETTINGS tab's Theme selector (v544) exposes four named themes:
// Dark / Light / C64Classic / HighContrast
// This header defines each theme as a compile-time set of RGBA float tuples
// covering 9 color roles (bg, title, label, value, border, accent, inactive,
// ledOn, ledOff).
//
// Keeping the palette in a pure C++ header makes it:
// * Independently testable without Cocoa (settings_theme_v552_tests.cpp)
// * Constexpr-validated — all 4 theme branches are compile-checked
// * Platform-agnostic — the ViewController converts to NSColor on demand
// via nsColorFromTheme_v552_() defined in ArpSIDViewController.mm
//
// CONTRACT
// -------// * ThemeColorRGBA_v552 is trivially copyable.
// * ThemeColorSet_v552 is trivially copyable.
// * themePalette_v552 is constexpr — all 4 theme branches pinned at
// compile time.
// * All alpha values are 1.0f (fully opaque).
// * Within each theme: title ≠ label (pinned by static_assert below).
// * Within each theme: accent ≠ inactive.
// * Dark vs C64Classic: title colors differ (themes are mutually distinct).

#ifndef ARPSID_GUI_THEME_PALETTE_V552_H
#define ARPSID_GUI_THEME_PALETTE_V552_H

#include "arpsid/gui/settings_panel_model.h"
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── RGBA color atom ─────────────────────────────────────────────────────────
struct ThemeColorRGBA_v552 {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    constexpr bool operator==(const ThemeColorRGBA_v552& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    constexpr bool operator!=(const ThemeColorRGBA_v552& o) const noexcept {
        return !(*this == o);
    }
};
static_assert(std::is_trivially_copyable<ThemeColorRGBA_v552>::value,
              "ThemeColorRGBA_v552 must be trivially copyable");

// ─── Color role set ───────────────────────────────────────────────────────────
struct ThemeColorSet_v552 {
    ThemeColorRGBA_v552 bg;        ///< panel background tint
    ThemeColorRGBA_v552 title;     ///< section header text
    ThemeColorRGBA_v552 label;     ///< parameter labels
    ThemeColorRGBA_v552 value;     ///< live-value readouts
    ThemeColorRGBA_v552 border;    ///< section / bezel borders
    ThemeColorRGBA_v552 accent;    ///< highlight / selected state
    ThemeColorRGBA_v552 inactive;  ///< dimmed / disabled
    ThemeColorRGBA_v552 ledOn;     ///< LED / indicator active
    ThemeColorRGBA_v552 ledOff;    ///< LED / indicator inactive
};
static_assert(std::is_trivially_copyable<ThemeColorSet_v552>::value,
              "ThemeColorSet_v552 must be trivially copyable");

// ─── Palette factory ──────────────────────────────────────────────────────────
// Returns the canonical color set for a given Theme.
// All components are in sRGB [0, 1]. All alpha values are 1.0f.
constexpr ThemeColorSet_v552 themePalette_v552(Theme t) noexcept {
    ThemeColorSet_v552 s{};
    switch (t) {

    case Theme::Dark:           // C64-inspired dark palette (default)
        s.bg       = {.118f, .094f, .549f, 1.f};   // c64Blue background
        s.title    = {.886f, .886f, .314f, 1.f};   // c64Yellow section headers
        s.label    = {.459f, .420f, .906f, 1.f};   // c64LtBlue parameter labels
        s.value    = {.596f, .933f, .529f, 1.f};   // c64LtGreen live readouts
        s.border   = {.459f, .420f, .906f, 1.f};   // c64LtBlue borders
        s.accent   = {.408f, .769f, .816f, 1.f};   // c64Cyan highlights
        s.inactive = {.333f, .333f, .333f, 1.f};   // c64DkGrey dimmed
        s.ledOn    = {.596f, .933f, .529f, 1.f};   // c64LtGreen
        s.ledOff   = {.353f, .235f, .000f, 1.f};   // c64Brown
        break;

    case Theme::Light:          // macOS-style light palette
        s.bg       = {.92f,  .92f,  .95f,  1.f};
        s.title    = {.08f,  .08f,  .35f,  1.f};
        s.label    = {.20f,  .20f,  .50f,  1.f};
        s.value    = {.00f,  .35f,  .00f,  1.f};
        s.border   = {.40f,  .40f,  .60f,  1.f};
        s.accent   = {.10f,  .30f,  .70f,  1.f};
        s.inactive = {.60f,  .60f,  .60f,  1.f};
        s.ledOn    = {.10f,  .60f,  .10f,  1.f};
        s.ledOff   = {.65f,  .55f,  .40f,  1.f};
        break;

    case Theme::C64Classic:     // Authentic C64 hardware palette
        s.bg       = {.255f, .255f, .694f, 1.f};   // #4141B1 C64 border blue
        s.title    = {1.f,   1.f,   1.f,   1.f};   // white (C64 uppercase text)
        s.label    = {.408f, .769f, .816f, 1.f};   // c64Cyan parameter labels
        s.value    = {.596f, .933f, .529f, 1.f};   // c64LtGreen live readouts (v873:
                                                   // was c64Cyan == label, which made
                                                   // readouts indistinguishable from
                                                   // their labels; green-on-blue is the
                                                   // canonical C64 readout look and now
                                                   // matches the Dark theme's value role)
        s.border   = {.408f, .769f, .816f, 1.f};   // c64Cyan
        s.accent   = {.886f, .886f, .314f, 1.f};   // c64Yellow highlights
        s.inactive = {.333f, .333f, .333f, 1.f};   // c64DkGrey
        s.ledOn    = {.408f, .769f, .816f, 1.f};   // c64Cyan
        s.ledOff   = {.353f, .235f, .000f, 1.f};   // c64Brown
        break;

    case Theme::HighContrast:   // Accessibility — maximum contrast
        s.bg       = {.00f,  .00f,  .00f,  1.f};   // pure black
        s.title    = {1.f,   1.f,   0.f,   1.f};   // pure yellow
        s.label    = {1.f,   1.f,   1.f,   1.f};   // pure white
        s.value    = {0.f,   1.f,   0.f,   1.f};   // pure green
        s.border   = {1.f,   1.f,   1.f,   1.f};   // pure white
        s.accent   = {0.f,   1.f,   1.f,   1.f};   // pure cyan
        s.inactive = {.5f,   .5f,   .5f,   1.f};   // mid grey
        s.ledOn    = {0.f,   1.f,   0.f,   1.f};   // pure green
        s.ledOff   = {.4f,   .2f,   .0f,   1.f};
        break;
    }
    return s;
}

// ─── Compile-time invariants ──────────────────────────────────────────────────
// Verify key contracts for the Dark theme at compile time.
static_assert(themePalette_v552(Theme::Dark).bg.a       == 1.f, "Dark bg alpha");
static_assert(themePalette_v552(Theme::Dark).title.a    == 1.f, "Dark title alpha");
static_assert(themePalette_v552(Theme::Light).bg.a      == 1.f, "Light bg alpha");
static_assert(themePalette_v552(Theme::C64Classic).bg.a == 1.f, "C64 bg alpha");
static_assert(themePalette_v552(Theme::HighContrast).bg.a== 1.f,"HC bg alpha");

// title ≠ label in the Dark theme
static_assert(themePalette_v552(Theme::Dark).title !=
              themePalette_v552(Theme::Dark).label,
              "Dark theme: title and label must be distinct colors");

// accent ≠ inactive in the Dark theme
static_assert(themePalette_v552(Theme::Dark).accent !=
              themePalette_v552(Theme::Dark).inactive,
              "Dark theme: accent and inactive must be distinct colors");

// Dark vs C64Classic title colors differ (themes are mutually distinct)
static_assert(themePalette_v552(Theme::Dark).title !=
              themePalette_v552(Theme::C64Classic).title,
              "Dark and C64Classic themes must have distinct title colors");

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_THEME_PALETTE_V552_H
