// Copyright (C) 2024-2026 Ulf Bertilsson
// v963 GUI accessibility + tooltip coverage closure.
//
// Full-GUI audit pass (after v962 wiring): layout math verified robust at the
// 720x520 minimum (bank grid cells stay positive after the v962 user-kit row);
// refresh hygiene verified (CVDisplayLink occlusion pausing + v822 bounded
// poller); CGPath create/release balanced; the one NSTimer is invalidated.
// Two real gaps were found and fixed:
//
// 1. TOOLTIPS: the mixer strip's volume/pan sliders, one-letter Solo/Mute
//    toggles, and the five insert-FX popups had no tooltips (the neighbouring
//    reverb-send did), and the DIGI START/LEN/VOL sliders had none. All are
//    interactive controls whose semantics are not self-evident — they now
//    carry per-channel/per-slot tooltips.
//
// 2. ACCESSIBILITY: the custom interactive views ArpSIDStripView (pitch/mod/
//    breath performance strips), ArpSIDDrumPadGridView, and ArpSIDPianoView
//    were invisible to assistive tech (no accessibility element). Knobs
//    already exposed a slider role; the strips now do too (with label+value),
//    and the pad grid / piano announce themselves with usage descriptions.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "gui_a11y_tooltip_coverage_v963_tests FAIL: " << message << "\n"; std::exit(1); }
}

static std::string readFile(const char* relativePath) {
    std::ifstream file(std::string(ARPSID_SOURCE_ROOT) + "/" + relativePath, std::ios::binary);
    require(static_cast<bool>(file), relativePath);
    std::ostringstream out; out << file.rdbuf(); return out.str();
}

// The block of `cls`'s @implementation, up to its @end.
static std::string classBody(const std::string& s, const std::string& cls) {
    const std::string tag = "@implementation " + cls;
    const std::size_t a = s.find(tag);
    require(a != std::string::npos, ("class implementation must exist: " + cls).c_str());
    const std::size_t e = s.find("\n@end", a);
    require(e != std::string::npos, ("class implementation must terminate: " + cls).c_str());
    return s.substr(a, e - a);
}

int main() {
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");

    // ── 1. Mixer + DIGI tooltip coverage ────────────────────────────────────
    require(vc.find("volSlider.toolTip") != std::string::npos,
            "mixer volume sliders must carry a tooltip");
    require(vc.find("panSlider.toolTip") != std::string::npos,
            "mixer pan sliders must carry a tooltip");
    require(vc.find("soloBtn.toolTip") != std::string::npos,
            "the one-letter Solo toggle must explain itself via tooltip");
    require(vc.find("muteBtn.toolTip") != std::string::npos,
            "the one-letter Mute toggle must explain itself via tooltip");
    require(vc.find("fx.toolTip") != std::string::npos,
            "mixer insert-FX popups must carry a tooltip");
    require(vc.find("startSl.toolTip") != std::string::npos &&
            vc.find("lenSl.toolTip") != std::string::npos &&
            vc.find("volSl.toolTip") != std::string::npos,
            "DIGI START/LEN/VOL sliders must carry tooltips");

    // ── 2. Accessibility of custom interactive views ────────────────────────
    const std::string strip = classBody(vc, "ArpSIDStripView");
    require(strip.find("isAccessibilityElement") != std::string::npos &&
            strip.find("NSAccessibilitySliderRole") != std::string::npos &&
            strip.find("accessibilityValue") != std::string::npos,
            "performance strips must expose a slider role with a value");
    const std::string pads = classBody(vc, "ArpSIDDrumPadGridView");
    require(pads.find("isAccessibilityElement") != std::string::npos &&
            pads.find("accessibilityLabel") != std::string::npos,
            "the drum pad grid must be visible to assistive tech");
    const std::string piano = classBody(vc, "ArpSIDPianoView");
    require(piano.find("isAccessibilityElement") != std::string::npos &&
            piano.find("accessibilityLabel") != std::string::npos,
            "the piano keyboard must be visible to assistive tech");

    std::printf("gui_a11y_tooltip_coverage_v963_tests PASS\n");
    return 0;
}
