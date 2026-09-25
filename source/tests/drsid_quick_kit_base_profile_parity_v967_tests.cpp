// SPDX-License-Identifier: BSD-3-Clause
// drsid_quick_kit_base_profile_parity_v967_tests.cpp — v967 closure.
//
// The on-screen DrSID quick-kit popup applies a GUI-side tone/motion table
// (kArpSIDDrumKitToneProfiles in ArpSIDViewController.mm) on top of the factory
// patch. Row 0 "Standard" is authored to mirror the canonical factory DrSID
// base voicing (applyFactoryDrSidDefaults in factory_patch_params.h) so that
// selecting "Standard" from the drum tab sounds identical to recalling the
// same default kit from the host program list.
//
// v909 lengthened the factory base tom decay (kParamDrSidTomDecay 0.30→0.44)
// but the GUI table was not updated, so the popup kept playing the shorter
// pre-v909 tom — a shipped split-brain between the two authorities. v967 syncs
// the GUI base to the factory base and pins the invariant here so any future
// change to one side without the other fails the build.
//
// This is a source-contract test (the GUI table lives in an Objective-C++ .mm
// that a portable C++ test cannot include), matching the repo's existing
// source-contract style. It also pins the "Standard" (slot -1) no-op fix.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "drsid_quick_kit_base_profile_parity_v967_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), (std::string("cannot open ") + path).c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "drsid_quick_kit_base_profile_parity_v967_tests FAIL: " << msg
                  << "\n  missing: " << needle << "\n";
        std::exit(1);
    }
}

void requireAbsent(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) != std::string::npos) {
        std::cerr << "drsid_quick_kit_base_profile_parity_v967_tests FAIL: " << msg
                  << "\n  unexpected: " << needle << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string factory = readFile(root + "/source/factory_patch_params.h");
    const std::string gui = readFile(root + "/source/au3/ArpSIDViewController.mm");

    // (1) Canonical factory base tom decay is 0.44 (the v909 correction).
    requireContains(factory, "sp(kParamDrSidTomDecay, 0.44f)",
                    "factory DrSID base tom decay must remain the canonical 0.44");

    // (2) The GUI ArpSIDDrumKitToneProfile default must equal the factory base,
    //     not the pre-v909 0.30.
    requireContains(gui, "float tomDecay = 0.44f;",
                    "GUI quick-kit base tom decay default must match factory base (0.44)");
    requireAbsent(gui, "float tomDecay = 0.30f;",
                  "pre-v909 GUI base tom decay (0.30) must not return");

    // (3) Row 0 "Standard" mirrors the factory base voicing including tom decay.
    requireContains(gui, "{0.84f,0.44f,0.30f,0.55f,0.70f,0.72f,0.20f,0.26f,0.74f,0.38f,0.55f,0.44f}",
                    "Standard quick-kit row must mirror the factory base voicing (tom decay 0.44)");
    requireAbsent(gui, "{0.84f,0.44f,0.30f,0.55f,0.70f,0.72f,0.20f,0.26f,0.74f,0.38f,0.55f,0.30f}",
                  "the drifted pre-v909 Standard row (tom decay 0.30) must not return");

    // (4) GUI-only quick kits (slot -1, e.g. "Standard") must still apply their
    //     curated realtime profile instead of being a silent no-op.
    requireContains(gui, "const BOOL hasFactorySlot = (slot >= 0 && slot <= ArpSID::kCanonicalFactoryPatchSlotMax);",
                    "quick-kit apply must distinguish GUI-only kits (slot < 0) from factory-backed kits");
    requireContains(gui, "if(hasFactorySlot) [strongSelf _applyPatchSelectionNumber:slot];",
                    "factory-patch load must be gated on a real backing slot");
    requireAbsent(gui, "if(slot < 0 || slot > ArpSID::kCanonicalFactoryPatchSlotMax || !_au) return;",
                  "the old guard that made slot -1 quick kits a silent no-op must not return");

    std::cout << "drsid_quick_kit_base_profile_parity_v967_tests: OK\n";
    return 0;
}
