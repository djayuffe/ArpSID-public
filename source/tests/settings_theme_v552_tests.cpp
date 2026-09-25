// SPDX-License-Identifier: BSD-3-Clause
// settings_theme_v552_tests.cpp — Contract tests for v552 theme palette.
//
// Tests:
// Section I — Layout: ThemeColorRGBA_v552 and ThemeColorSet_v552 trivially copyable (static_assert)
// Section II — Compile-time alpha invariants (static_assert)
// Section III — All 4 themes fully opaque at runtime
// Section IV — Dark theme palette values pinned
// Section V — C64Classic theme palette values pinned
// Section VI — HighContrast theme palette values pinned
// Section VII — Light theme palette values pinned
// Section VIII — Color roles distinct within each theme
// Section IX — Themes are mutually distinct

#include "arpsid/gui/theme_palette_v552.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <type_traits>

using namespace ArpSID::GUI;

// ─── helpers ─────────────────────────────────────────────────────────────────

static bool approxEq(float a, float b) noexcept { return std::fabs(a - b) < 1e-4f; }
[[maybe_unused]]
static bool colorEq(const ThemeColorRGBA_v552& a, const ThemeColorRGBA_v552& b) noexcept {
    return approxEq(a.r, b.r) && approxEq(a.g, b.g)
        && approxEq(a.b, b.b) && approxEq(a.a, b.a);
}

// ─── Section I: Layout pinning (static_assert) ───────────────────────────────

static_assert(std::is_trivially_copyable<ThemeColorRGBA_v552>::value,
              "ThemeColorRGBA_v552 must be trivially copyable");
static_assert(std::is_trivially_copyable<ThemeColorSet_v552>::value,
              "ThemeColorSet_v552 must be trivially copyable");

// ─── Section II: Compile-time alpha invariants (static_assert) ───────────────

static_assert(themePalette_v552(Theme::Dark).bg.a        == 1.f, "Dark bg alpha");
static_assert(themePalette_v552(Theme::Dark).title.a     == 1.f, "Dark title alpha");
static_assert(themePalette_v552(Theme::Light).bg.a       == 1.f, "Light bg alpha");
static_assert(themePalette_v552(Theme::C64Classic).bg.a  == 1.f, "C64Classic bg alpha");
static_assert(themePalette_v552(Theme::HighContrast).bg.a== 1.f, "HighContrast bg alpha");
static_assert(themePalette_v552(Theme::Dark).title !=
              themePalette_v552(Theme::Dark).label,
              "Dark theme: title and label must differ");
static_assert(themePalette_v552(Theme::Dark).accent !=
              themePalette_v552(Theme::Dark).inactive,
              "Dark theme: accent and inactive must differ");

// ─── Section III: All 4 themes fully opaque ──────────────────────────────────

static void testAllThemesFullyOpaque() {
    const Theme allThemes[] = {
        Theme::Dark, Theme::Light, Theme::C64Classic, Theme::HighContrast
    };
    for (Theme t : allThemes) {
        const ThemeColorSet_v552 s = themePalette_v552(t);
        (void)s;
        assert(s.bg.a      == 1.f);
        assert(s.title.a   == 1.f);
        assert(s.label.a   == 1.f);
        assert(s.value.a   == 1.f);
        assert(s.border.a  == 1.f);
        assert(s.accent.a  == 1.f);
        assert(s.inactive.a== 1.f);
        assert(s.ledOn.a   == 1.f);
        assert(s.ledOff.a  == 1.f);
    }
    std::puts("  III:  all 4 themes fully opaque — OK");
}

// ─── Section IV: Dark theme palette pinned ───────────────────────────────────

static void testDarkThemePinned() {
    const auto s = themePalette_v552(Theme::Dark);
    // title = c64Yellow = (.886, .886, .314)
    assert(approxEq(s.title.r, .886f) && approxEq(s.title.g, .886f)
        && approxEq(s.title.b, .314f));
    // label = c64LtBlue = (.459, .420, .906)
    assert(approxEq(s.label.r, .459f) && approxEq(s.label.g, .420f)
        && approxEq(s.label.b, .906f));
    // value = c64LtGreen = (.596, .933, .529)
    assert(approxEq(s.value.r, .596f) && approxEq(s.value.g, .933f)
        && approxEq(s.value.b, .529f));
    // accent = c64Cyan = (.408, .769, .816)
    assert(approxEq(s.accent.r, .408f) && approxEq(s.accent.g, .769f)
        && approxEq(s.accent.b, .816f));
    // bg = c64Blue = (.118, .094, .549)
    assert(approxEq(s.bg.r, .118f) && approxEq(s.bg.g, .094f)
        && approxEq(s.bg.b, .549f));
    (void)s;
    std::puts("  IV:   Dark theme palette pinned — OK");
}

// ─── Section V: C64Classic theme palette pinned ──────────────────────────────

static void testC64ClassicPinned() {
    const auto s = themePalette_v552(Theme::C64Classic);
    // title = white = (1, 1, 1)
    assert(approxEq(s.title.r, 1.f) && approxEq(s.title.g, 1.f)
        && approxEq(s.title.b, 1.f));
    // accent = c64Yellow = (.886, .886, .314)
    assert(approxEq(s.accent.r, .886f) && approxEq(s.accent.g, .886f)
        && approxEq(s.accent.b, .314f));
    // label = c64Cyan = (.408, .769, .816)
    assert(approxEq(s.label.r, .408f) && approxEq(s.label.g, .769f)
        && approxEq(s.label.b, .816f));
    // value = c64LtGreen = (.596, .933, .529)  (v873: was c64Cyan == label)
    assert(approxEq(s.value.r, .596f) && approxEq(s.value.g, .933f)
        && approxEq(s.value.b, .529f));
    // bg = #4141B1-ish = (.255, .255, .694)
    assert(approxEq(s.bg.r, .255f) && approxEq(s.bg.g, .255f)
        && approxEq(s.bg.b, .694f));
    (void)s;
    std::puts("  V:    C64Classic theme palette pinned — OK");
}

// ─── Section VI: HighContrast theme palette pinned ───────────────────────────

static void testHighContrastPinned() {
    const auto s = themePalette_v552(Theme::HighContrast);
    // bg = pure black = (0, 0, 0)
    assert(approxEq(s.bg.r, 0.f) && approxEq(s.bg.g, 0.f)
        && approxEq(s.bg.b, 0.f));
    // title = pure yellow = (1, 1, 0)
    assert(approxEq(s.title.r, 1.f) && approxEq(s.title.g, 1.f)
        && approxEq(s.title.b, 0.f));
    // label = pure white = (1, 1, 1)
    assert(approxEq(s.label.r, 1.f) && approxEq(s.label.g, 1.f)
        && approxEq(s.label.b, 1.f));
    // value = pure green = (0, 1, 0)
    assert(approxEq(s.value.r, 0.f) && approxEq(s.value.g, 1.f)
        && approxEq(s.value.b, 0.f));
    // accent = pure cyan = (0, 1, 1)
    assert(approxEq(s.accent.r, 0.f) && approxEq(s.accent.g, 1.f)
        && approxEq(s.accent.b, 1.f));
    (void)s;
    std::puts("  VI:   HighContrast theme palette pinned — OK");
}

// ─── Section VII: Light theme palette pinned ─────────────────────────────────

static void testLightThemePinned() {
    const auto s = themePalette_v552(Theme::Light);
    assert(approxEq(s.bg.r, .92f) && approxEq(s.bg.g, .92f)
        && approxEq(s.bg.b, .95f));
    assert(approxEq(s.title.r, .08f) && approxEq(s.title.g, .08f)
        && approxEq(s.title.b, .35f));
    assert(approxEq(s.label.r, .20f) && approxEq(s.label.g, .20f)
        && approxEq(s.label.b, .50f));
    assert(approxEq(s.accent.r, .10f) && approxEq(s.accent.g, .30f)
        && approxEq(s.accent.b, .70f));
    (void)s;
    std::puts("  VII:  Light theme palette pinned — OK");
}

// ─── Section VIII: Color roles distinct within each theme ────────────────────

static void testRolesDistinctPerTheme() {
    const Theme allThemes[] = {
        Theme::Dark, Theme::Light, Theme::C64Classic, Theme::HighContrast
    };
    for (Theme t : allThemes) {
        const ThemeColorSet_v552 s = themePalette_v552(t);
        (void)s;

        // title ≠ label for every theme
        assert(!colorEq(s.title, s.label));

        // value ≠ label for every theme (v873: C64Classic readouts are now
        // c64LtGreen, distinct from their cyan labels).
        assert(!colorEq(s.value, s.label));

        // accent ≠ inactive for every theme
        assert(!colorEq(s.accent, s.inactive));

        // ledOn ≠ ledOff for every theme
        assert(!colorEq(s.ledOn, s.ledOff));
    }
    std::puts("  VIII: color roles distinct within each theme — OK");
}

// ─── Section IX: Themes are mutually distinct ────────────────────────────────

static void testThemesMutuallyDistinct() {
    const auto dark  = themePalette_v552(Theme::Dark);
    const auto light = themePalette_v552(Theme::Light);
    const auto c64   = themePalette_v552(Theme::C64Classic);
    const auto hc    = themePalette_v552(Theme::HighContrast);
    (void)dark; (void)light; (void)c64; (void)hc;

    // bg colors all differ
    assert(!colorEq(dark.bg, light.bg));
    assert(!colorEq(dark.bg, c64.bg));
    assert(!colorEq(dark.bg, hc.bg));
    assert(!colorEq(light.bg, c64.bg));
    assert(!colorEq(light.bg, hc.bg));
    assert(!colorEq(c64.bg, hc.bg));

    // title colors differ across at least the most important pairs
    assert(!colorEq(dark.title, c64.title));    // yellow vs white
    assert(!colorEq(dark.title, hc.title));     // .886y vs pure yellow
    assert(!colorEq(light.title, hc.title));
    assert(!colorEq(c64.title, hc.title));

    // Dark label ≠ Light label
    assert(!colorEq(dark.label, light.label));

    std::puts("  IX:   themes are mutually distinct — OK");
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::puts("settings_theme_v552_tests");
    std::puts("  I:    static_assert layout pinning — OK");
    std::puts("  II:   static_assert alpha invariants — OK");
    testAllThemesFullyOpaque();
    testDarkThemePinned();
    testC64ClassicPinned();
    testHighContrastPinned();
    testLightThemePinned();
    testRolesDistinctPerTheme();
    testThemesMutuallyDistinct();
    std::puts("ALL PASS");
    return 0;
}
