// Copyright (C) 2024-2026 Ulf Bertilsson
// auv2_attached_view_replacement_v755_tests.cpp
//
// audit P0-10 / fix-order #8: repeated editor open / attached-view replacement.
// When the host requests a new Cocoa view while the previous editor view is STILL
// attached to a view hierarchy, the factory used to dispose the outgoing controller
// and build a replacement WITHOUT detaching the old view — leaving the host
// displaying a view backed by a torn-down controller.
//
// The runtime coverage lives in arpsid_auv2_gui_lifecycle_smoke.mm (attached-view
// replacement case), which needs an installed component + window server and only
// runs in AUv2 GUI smoke builds. This default-suite test is a structural regression
// guard that the clean-detach contract stays wired in the replacement path.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(static_cast<bool>(f), rel);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string mm = readFile("source/au2/ArpSIDAUv2Component.mm");

    // Locate the editor build block and the replacement path within it.
    const size_t buildAt = mm.find("void (^buildBlock)(void)");
    require(buildAt != std::string::npos, "buildBlock present");

    // The detached-reuse branch must still exist (reuse only when not attached).
    require(mm.find("existingView.superview == nil", buildAt) != std::string::npos,
            "detached editor is reused only when superview == nil");

    // The replacement path must detach a still-attached outgoing view BEFORE
    // disposing its controller.
    const size_t detachAt = mm.find("existingView.superview != nil", buildAt);
    require(detachAt != std::string::npos,
            "replacement path checks for a still-attached outgoing view");
    const size_t removeAt = mm.find("[existingView removeFromSuperview]", buildAt);
    require(removeAt != std::string::npos,
            "replacement path detaches the outgoing view (removeFromSuperview)");

    const size_t disposeAt = mm.find("prepareForFinalEditorDisposal", removeAt);
    require(disposeAt != std::string::npos,
            "outgoing controller is disposed in the replacement path");
    require(removeAt < disposeAt,
            "outgoing view is detached BEFORE its controller is disposed");

    // Stale associations must be cleared before the replacement is installed.
    const size_t clearViewAssoc =
        mm.find("kArpSIDAUv2AudioUnitViewAssociationKey", disposeAt);
    const size_t clearCtrlAssoc =
        mm.find("kArpSIDAUv2AudioUnitControllerAssociationKey", disposeAt);
    const size_t buildNew = mm.find("[[ArpSIDViewController alloc] init]", disposeAt);
    require(buildNew != std::string::npos, "a replacement controller is built");
    require(clearViewAssoc != std::string::npos && clearViewAssoc < buildNew,
            "view association cleared before building the replacement");
    require(clearCtrlAssoc != std::string::npos && clearCtrlAssoc < buildNew,
            "controller association cleared before building the replacement");

    // The runtime smoke test must cover the attached-replacement scenario.
    const std::string smoke = readFile("source/tests/arpsid_auv2_gui_lifecycle_smoke.mm");
    require(smoke.find("attached") != std::string::npos &&
            smoke.find("second.superview != nil") != std::string::npos,
            "GUI lifecycle smoke exercises attached-view replacement detach");

    std::cout << "Auv2AttachedViewReplacementV755Tests PASS\n";
    return 0;
}
