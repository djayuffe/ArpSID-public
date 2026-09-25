#include "arpsid/core/math_utils.h"
#include "parameter_ids.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static std::string readFile(const char* relativePath) {
    std::ifstream file(std::string(ARPSID_SOURCE_ROOT) + "/" + relativePath,
                       std::ios::binary);
    require(static_cast<bool>(file), relativePath);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main() {
    const std::string gui = readFile("source/au3/ArpSIDViewController.mm");
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string bitperfect = readFile("include/arpsid/engines/bitperfect_engine.h");

    require(gui.find("static const CGFloat kBottomSafeInset=10") != std::string::npos,
            "bottom chrome must reserve a host-safe inset");
    require(gui.find("contentBaseY=footerBaseY+kFtrH+kTabH") != std::string::npos,
            "tab content must start above both footer and navigation lanes");
    require(gui.find("navY=kBW+kBottomSafeInset+kFtrH+4.f") != std::string::npos,
            "bottom navigation must be raised with the footer safe inset");

    require(gui.find("-(void)cancelHeldNote") != std::string::npos,
            "piano exposes one idempotent held-note cancellation endpoint");
    require(gui.find("NSWindowDidResignKeyNotification") != std::string::npos &&
            gui.find("NSApplicationDidResignActiveNotification") != std::string::npos,
            "piano releases notes at window/application lifecycle boundaries");
    require(gui.find("-(void)mouseExited:(NSEvent*)event") != std::string::npos &&
            gui.find("[self.window makeFirstResponder:self]") != std::string::npos,
            "piano captures responder ownership and releases when pointer exits");

    require(kernel.find("pendingDroppedNoteOffCount_") != std::string::npos &&
            kernel.find("latchDroppedNoteOff_") != std::string::npos &&
            kernel.find("drainPendingDroppedNoteOffs_") != std::string::npos,
            "raw MIDI overflow must preserve per-note note-offs");
    const auto queueDrain = kernel.find("while (midiQueue_.pop(ev))");
    const auto fallbackDrain = kernel.find(
        "drainPendingDroppedNoteOffs_(numFrames, blockStartHostTime)");
    require(queueDrain != std::string::npos && fallbackDrain > queueDrain,
            "overflowed note-offs must append after primary raw MIDI queue drain");

    require(ArpSID::defaultNormalizedParamValue(ArpSID::kParamVCO1Detune) == 0.5f &&
            ArpSID::defaultNormalizedParamValue(ArpSID::kParamVCO2Detune) == 0.5f &&
            ArpSID::defaultNormalizedParamValue(ArpSID::kParamVCO3Detune) == 0.5f,
            "all generated oscillators must default to true concert pitch");
    const double palClock = 985248.0;
    const double a4Raw = 440.0 * 16777216.0 / palClock;
    require(ArpSID::ArpSID_hzToSidFrequencyRegister(440.0, palClock) ==
                static_cast<std::uint16_t>(std::lround(a4Raw)),
            "SID frequency conversion must round instead of biasing notes flat");
    require(bitperfect.find("return ArpSID_hzToSidFrequencyRegister(hz, safeClock);") !=
                std::string::npos,
            "BitPerfect melodic path must use canonical rounded SID tuning");

    require(gui.find("@interface ArpSIDDrumBrandView") != std::string::npos &&
            gui.find("@\"SID•808\"") != std::string::npos &&
            gui.find("@\"DrSID\"") != std::string::npos,
            "dedicated vector logos must exist for SID-808 and DrSID");
    require(gui.find("_drumBrandView.brandKind =") != std::string::npos &&
            gui.find("_drsidBrandView.brandKind = 0") != std::string::npos,
            "brand artwork must refresh from component flavor while DrSID stays distinct");
    require(gui.find("SID-808 DRUM SYNTHESIS") == std::string::npos,
            "DrSID tab must not carry the stale SID-808 title");

    require(gui.find("kArpSIDKnobVisualScale=0.70f") != std::string::npos,
            "all shared knobs must render at the reduced visual scale");
    require(gui.find("[self.window makeFirstResponder:self]") != std::string::npos &&
            gui.find("-(void)keyDown:(NSEvent*)e") != std::string::npos &&
            gui.find("ArpSIDKnobTooltipForParam") != std::string::npos,
            "knobs must retain large hit targets and expose keyboard/discoverable interaction");
    require(gui.find("-(BOOL)isAccessibilityElement{return YES;}") != std::string::npos &&
            gui.find("NSAccessibilitySliderRole") != std::string::npos,
            "knobs must expose native slider accessibility");

    std::cout << "GuiKeyboardTuningBrandClosureV684Tests PASS\n";
    return 0;
}
