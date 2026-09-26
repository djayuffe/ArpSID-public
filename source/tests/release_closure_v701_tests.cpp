// Copyright (C) 2024-2026 Ulf Bertilsson
// release_closure_v701_tests.cpp
//
// Pass126 release-closure guard. This is a source-shape contract test: it keeps
// the one-command validation entrypoints honest by requiring the release check
// to build and run the broader GUI/DIGI/factory/no-scaffold suite, not only the
// narrow D418 smoke subset.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#error "ARPSID_SOURCE_DIR must be defined"
#endif

namespace {
std::string readText(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "FAIL: could not open " << path << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}
void contains(const std::string& haystack, const char* needle, const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string cmake = readText(root + "/CMakeLists.txt");
    const std::string build = readText(root + "/build.sh");
    const std::string gui = readText(root + "/source/au3/ArpSIDViewController.mm");
    const std::string adapter = readText(root + "/source/au3/ArpSIDDSPKernelAdapter.h");
    const std::string kernel = readText(root + "/source/au3/ArpSIDDSPKernel.hpp");

    contains(build, "--release-check", "build.sh exposes release-check mode");
    contains(build, "arpsid_release_closure_suite", "release-check builds the release closure target");
    contains(build, "ReleaseClosureV701Tests", "release-check executes its own contract guard");
    contains(build, "ReleasePackagingV702Tests", "release-check executes package-release contract guard");
    contains(build, "TabNoScaffoldV631Tests", "release-check includes no-scaffold audit test");
    contains(build, "TabTooltipCoverageV586Tests", "release-check includes tab tooltip coverage");
    contains(build, "BankUITextAndTooltipV677Tests", "release-check includes bank UI text/tooltip coverage");
    contains(build, "NoDead127BankSlotDecodeV694Tests", "release-check includes stale/dead 127-slot decode guard");
    contains(build, "FactoryPayloadFinalDeepGuardV695Tests", "release-check includes factory payload deep guard");

    contains(cmake, "add_custom_target(arpsid_release_closure_suite", "CMake defines release closure suite");
    contains(cmake, "arpsid_tab_no_scaffold_v631_tests", "release suite builds no-scaffold target");
    contains(cmake, "arpsid_tab_tooltip_coverage_v586_tests", "release suite builds tooltip target");
    contains(cmake, "arpsid_bank_ui_text_and_tooltip_v677_tests", "release suite builds bank tooltip target");
    contains(cmake, "arpsid_no_dead_127_bank_slot_decode_v694_tests", "release suite builds stale slot guard");
    contains(cmake, "arpsid_release_closure_v701_tests", "CMake registers this release guard");
    contains(cmake, "arpsid_release_packaging_v702_tests", "CMake registers package-release guard");

    contains(gui, "triggerDigiPadSlot", "GUI audition pads are wired through adapter, not direct DSP mutation");
    contains(gui, "GUI pads bypass the MIDI channel filter; external MIDI note", "audition pad tooltip clarifies MIDI channel bypass without duplicate junk");
    contains(adapter, "clearDigiD418RuntimeTelemetry", "adapter exposes non-destructive HUD clear");
    contains(adapter, "triggerDigiPadSlot", "adapter exposes render-safe GUI pad trigger");
    contains(kernel, "triggerDigiPadForGui", "kernel has render-thread GUI pad queue API");
    contains(kernel, "telemetryDigiGuiPadAcceptedCount_", "kernel publishes GUI pad accepted telemetry");
    contains(kernel, "telemetryDigiGuiPadIgnoredCount_", "kernel publishes GUI pad ignored telemetry");

    std::cout << "ReleaseClosureV701Tests PASS\n";
    return 0;
}
