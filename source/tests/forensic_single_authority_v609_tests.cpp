// Copyright (C) 2024-2026 Ulf Bertilsson
// forensic_single_authority_v609_tests.cpp
//
// Consolidation guard (Option A): the embedded forensic system inside the
// active SidRegisterEngine / sid_chip render path is THE single forensic
// authority. The old experimental parallel backend
// (source/au3/ArpSIDForensicEngine.h, namespace ArpSID::Forensic,
// class ForensicSIDEngine) was dead code with its own oscillator/envelope/
// filter and its own register-write timing cadence — a second truth source
// and a second timing authority. It has been retired.
//
// This test pins the invariant so the parallel engine cannot silently return:
// I. The dead header file no longer exists.
// II. No active SID/DSP source references the dead engine symbols.
// III. The embedded forensic authority (ArpSIDForensicConfig + setForensicConfig
// + kParamForensic* AU parameters) is present and intact.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#error "ARPSID_SOURCE_ROOT must be defined by CMake for this source-shape test"
#endif

namespace {

void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}

bool fileExists(const std::string& path) {
    std::ifstream in(path);
    return static_cast<bool>(in);
}

std::string readTextFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "FAIL: could not open " << path << "\n"; std::abort(); }
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

void requireContains(const std::string& text, const std::string& needle, const char* msg) {
    require(text.find(needle) != std::string::npos, msg);
}
void requireAbsent(const std::string& text, const std::string& needle, const char* msg) {
    require(text.find(needle) == std::string::npos, msg);
}

} // namespace

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;

    // I. The dead experimental backend header must be gone.
    require(!fileExists(root + "/source/au3/ArpSIDForensicEngine.h"),
            "retired experimental ArpSIDForensicEngine.h must not exist");

    // II. No active SID/DSP source may reference the dead engine symbols.
    const std::string kernel  = readTextFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string sreg    = readTextFile(root + "/include/arpsid/engines/sid_register_engine.h");
    const std::string sidchip = readTextFile(root + "/include/arpsid/core/sid_chip.h");
    const std::string params  = readTextFile(root + "/source/parameter_ids.h");

    for (const std::string* src : { &kernel, &sreg, &sidchip }) {
        requireAbsent(*src, "ForensicSIDEngine",
                      "active source must not reference the retired ForensicSIDEngine");
        requireAbsent(*src, "ArpSID::Forensic",
                      "active source must not reference the retired ArpSID::Forensic namespace");
        requireAbsent(*src, "ArpSIDForensicEngine.h",
                      "active source must not include the retired forensic engine header");
    }

    // III. The single embedded forensic authority is present and intact.
    requireContains(sidchip, "struct ArpSIDForensicConfig",
                    "embedded forensic authority: ArpSIDForensicConfig must live in sid_chip.h");
    requireContains(sreg, "setForensicConfig",
                    "embedded forensic authority: SidRegisterEngine must accept setForensicConfig");
    requireContains(sreg, "forensic_",
                    "embedded forensic authority: SidRegisterEngine renders with forensic_ state");
    requireContains(params, "kParamForensicEnable",
                    "embedded forensic authority: forensic AU parameters must exist");

    std::cout << "ForensicSingleAuthorityV609Tests PASS\n";
    return 0;
}
