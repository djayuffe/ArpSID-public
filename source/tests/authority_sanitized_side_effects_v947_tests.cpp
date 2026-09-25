#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "parameter_ids.h"

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nMissing: " << needle << "\n";
        std::exit(1);
    }
}

static void requireAbsent(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) != std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nForbidden: " << needle << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string au3 = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string phase2 = readFile("source/arpsid_processor_phase2.cpp");
    const std::string services = readFile("include/arpsid/core/sid_runtime_parameter_services.h");

    requireContains(au3,
        "const float clean = ArpSID::canonicalIsValidParamTarget(target, static_cast<uint32_t>(kNumParams))\n            ? renderParams_[static_cast<size_t>(target)]",
        "AU3 projected parameter body must read the sanitized staged value from renderParams_");
    requireContains(au3,
        "runtimeApplyProjectedBackendParameter(*this, target, clean)",
        "AU3 backend side effects must receive the staged clean value");
    requireContains(au3,
        "runtimeApplyProjectedAdapterPolicyParameter(*this, target, clean)",
        "AU3 policy side effects must receive the staged clean value");
    requireAbsent(au3,
        "runtimeApplyProjectedBackendParameter(*this, target, value)) {\n            (void)ArpSID::runtimeApplyProjectedAdapterPolicyParameter(*this, target, value)",
        "AU3 must not pass the raw value into backend/policy after staging");

    requireContains(phase2,
        "const float clean = ArpSID::sanitizeNormalizedParamValue(",
        "Phase2 projected parameter body must compute a sanitized clean value before skip/backend");
    requireContains(phase2,
        "std::fabs(lastAppliedParamValues[(size_t)id] - clean) < 1e-6f",
        "Phase2 duplicate guard must compare sanitized clean values");
    requireContains(phase2,
        "const float stagedClean = paramValues[(size_t)id];",
        "Phase2 backend side effects must use the staged canonical value");
    requireContains(phase2,
        "runtimeApplyProjectedBackendParameter(*this, target, stagedClean)",
        "Phase2 backend side effects must receive stagedClean");
    requireContains(phase2,
        "runtimeApplyProjectedAdapterPolicyParameter(*this, target, stagedClean)",
        "Phase2 policy side effects must receive stagedClean");
    requireAbsent(phase2,
        "lastAppliedParamValues[(size_t)id] = value;\n\n    runtimeStageNormalizedParameterOnly(target, value);\n\n    const bool backendHandled = ArpSID::runtimeApplyProjectedBackendParameter(*this, target, value);",
        "Phase2 must not keep raw-value duplicate/backend law");

    requireContains(services,
        "const float clean = sanitizeNormalizedParamValue(",
        "Special parameter path must compute sanitized clean value");
    requireContains(services,
        "runtimeStageNormalizedParameter(target, targetId, value);",
        "Special parameter path must stage the raw value so target-specific sanitizer remains authoritative");
    requireContains(services,
        "if (clean > 0.5f)",
        "Special parameter branch decisions must use clean values");
    requireContains(services,
        "runtimeApplyProjectedHostCtrl(target, targetId, clean)",
        "Host-control special side effects must use clean values");
    requireContains(services,
        "target.runtimeImportSidRegisterNormalized(targetId, clean);",
        "SID-register import side effect must use clean values");
    requireAbsent(services,
        "const float clamped = canonicalClampedNormalizedValue(value);\n    if (pid == static_cast<int>(kParamSynthModeEnable))",
        "Special parameter path must not pre-clamp before staging");
    requireAbsent(services,
        "runtimeStageNormalizedParameter(target, targetId, clamped);",
        "Special parameter path must not stage pre-clamped values");

    const float mvDefault = ArpSID::defaultNormalizedParamValue(ArpSID::kParamMasterVolume);
    const float mvClean = ArpSID::sanitizeNormalizedParamValue(
        ArpSID::kParamMasterVolume,
        std::numeric_limits<float>::quiet_NaN(),
        mvDefault);
    require(std::isfinite(mvClean), "NaN master volume sanitize must produce finite value");
    require(std::fabs(mvClean - mvDefault) < 1.0e-6f,
            "NaN master volume sanitize must preserve param-specific default");

    const float pwClean = ArpSID::sanitizeNormalizedParamValue(
        ArpSID::kParamVCO1PulseWidth,
        std::numeric_limits<float>::infinity(),
        ArpSID::defaultNormalizedParamValue(ArpSID::kParamVCO1PulseWidth));
    require(std::isfinite(pwClean), "Inf pulse width sanitize must produce finite value");
    require(pwClean >= 0.0f && pwClean <= 1.0f,
            "Inf pulse width sanitize must remain normalized after cleanup");

    std::cout << "Authority sanitized side-effects v947 closure OK\n";
    return 0;
}
