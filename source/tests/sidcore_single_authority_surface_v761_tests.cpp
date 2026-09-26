// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

std::string readText(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(static_cast<bool>(f), rel);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void testRuntimeSurfaceNames() {
    const std::string model = readText("include/arpsid/core/sid_runtime_model.h");

    require(model.find("SidDynamicState& dynamicState() noexcept") == std::string::npos,
            "generic mutable dynamicState() surface must stay removed");
    require(model.find("SidDynamicState& dynamicStateInternalForCanonicalRuntimeOnly() noexcept") != std::string::npos,
            "mutable dynamic state access must use the explicit canonical-runtime-only spelling");
    require(model.find("const SidDynamicState& dynamicState() const noexcept") != std::string::npos,
            "read-only diagnostic dynamicState() view remains available");

    require(model.find("SidTimedEventQueue& pendingEvents() noexcept") == std::string::npos,
            "generic mutable pendingEvents() surface must stay removed");
    require(model.find("SidTimedEventQueue& pendingEventsNonRealtimeOnly() noexcept") != std::string::npos,
            "mutable pending queue access must be explicitly non-realtime/internal");
    require(model.find("single-authority") != std::string::npos,
            "header must document the single-authority mutation law");
}

void testNoCompatibilityBypassCallers() {
    const std::string proc = readText("source/arpsid_processor_phase2.cpp");
    const std::string reset = readText("include/arpsid/core/sid_runtime_reset_policy.h");
    const std::string forensic = readText("source/tests/sid_forensic_acceptance.cpp");
    const std::string closure = readText("source/tests/closure_regression_tests.cpp");

    require(proc.find(".dynamicState()") == std::string::npos,
            "legacy processor must not use generic mutable dynamicState()");
    require(forensic.find(".dynamicState()") == std::string::npos,
            "tests that mutate dynamic state must use the explicit internal spelling");
    require(reset.find("pendingEvents().reset()") == std::string::npos,
            "reset policy must not mutate through generic pendingEvents()");
    require(reset.find("pendingEventsNonRealtimeOnly().reset()") != std::string::npos,
            "reset policy must opt into non-realtime queue mutation explicitly");
    require(closure.find("static_cast<const ArpSID::SidRuntimeModel&>(runtime).dynamicState()") != std::string::npos,
            "read-only test access must intentionally bind to the const diagnostic surface");
}

} // namespace

int main() {
    testRuntimeSurfaceNames();
    testNoCompatibilityBypassCallers();
    std::cout << "sidcore_single_authority_surface_v761_tests: PASS\n";
    return 0;
}
