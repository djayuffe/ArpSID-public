// Copyright (C) 2024-2026 Ulf Bertilsson
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "parameter_ids.h"

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path.c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nMissing: " << needle << "\n";
        std::exit(1);
    }
}

void requireAbsent(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) != std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nUnexpected: " << needle << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string phase2 = readFile(root + "/source/arpsid_processor_phase2.cpp");

    requireContains(kernel,
        "v951: state-root apply must use the same param-specific sanitize law",
        "AU3 state-root apply must document the v951 staging parity law");
    requireContains(kernel,
        "const float clean = ArpSID::sanitizeNormalizedParamValue(\n"
        "                i,\n"
        "                v,\n"
        "                ArpSID::defaultNormalizedParamValue(i));\n"
        "            params_[(size_t)i].store(clean, std::memory_order_release);",
        "AU3 applyStateRootCanonical must use param-specific sanitize before publishing params");
    requireAbsent(kernel,
        "const float clean = std::clamp(std::isfinite(v) ? v : kParamInfos[(size_t)i].defaultNorm, 0.0f, 1.0f);",
        "AU3 root apply must not use generic clamp staging");

    requireContains(phase2,
        "v951: Phase2 state-root apply uses the shared param-specific sanitize",
        "Phase2 state-root apply must document the v951 staging parity law");
    requireContains(phase2,
        "const float clean = ArpSID::sanitizeNormalizedParamValue(\n"
        "            i,\n"
        "            v,\n"
        "            ArpSID::defaultNormalizedParamValue(i));\n"
        "        paramValues[(size_t)i] = clean;\n"
        "        lastAppliedParamValues[(size_t)i] = clean;\n"
        "        (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(i), clean);",
        "Phase2 root apply must sync paramValues, lastAppliedParamValues, and runtimeModel with clean value");
    requireAbsent(phase2,
        "paramValues[(size_t)i] = std::clamp(v, 0.0f, 1.0f);",
        "Phase2 root apply must not use generic clamp staging");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float def = ArpSID::defaultNormalizedParamValue(ArpSID::kParamMasterVolume);
    const float clean = ArpSID::sanitizeNormalizedParamValue(ArpSID::kParamMasterVolume, nan, def);
    require(std::isfinite(clean), "sanitizeNormalizedParamValue must return finite clean value for NaN");
    require(std::fabs(clean - def) <= 1.0e-6f, "NaN root value must sanitize to param default");

    std::cout << "StateRootStagingParityV951Tests PASS\n";
    return 0;
}
