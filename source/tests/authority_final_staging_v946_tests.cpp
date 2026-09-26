// Copyright (C) 2024-2026 Ulf Bertilsson
// authority_final_staging_v946_tests.cpp
// Final guard for v946: no pre-clamp split-brain on canonical staging ingress.

#include "arpsid/core/sid_runtime_model.h"
#include "parameter_ids.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <limits>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {
void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "AuthorityFinalStagingV946Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}
std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
void requireContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) != std::string::npos, msg);
}
void requireAbsent(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) == std::string::npos, msg);
}

void test_shared_stage_helper_does_not_preclamp() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string services = readFile((root + "/include/arpsid/core/sid_runtime_parameter_services.h").c_str());
    requireContains(services,
                    "target.runtimeStageNormalizedParameterOnly(targetId, value);",
                    "shared runtimeStageNormalizedParameter must delegate raw value to target sanitizer");
    requireAbsent(services,
                  "target.runtimeStageNormalizedParameterOnly(targetId, canonicalClampedNormalizedValue(value));",
                  "shared staging helper must not pre-clamp and erase param-specific defaults");
    requireContains(services,
                    "v946: do not pre-clamp here",
                    "shared staging helper must document final v946 authority law");
}

void test_target_adapter_does_not_preclamp_body_apply() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string adapter = readFile((root + "/include/arpsid/core/sid_runtime_target_adapter.h").c_str());
    requireContains(adapter,
                    "target_.runtimeApplyProjectedParameterBody(targetId, value);",
                    "target adapter must pass raw value into param-specific target staging");
    requireAbsent(adapter,
                  "target_.runtimeApplyProjectedParameterBody(targetId, canonicalClampedNormalizedValue(value));",
                  "target adapter must not pre-clamp before target-specific sanitize");
}

void test_au3_legacy_stage_render_param_uses_canonical_staging() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    requireContains(kernel,
                    "void stageRenderParam_(ParamID pid, float v) noexcept {\n        // v946",
                    "AU3 legacy/local staging helper must carry v946 guard");
    requireContains(kernel,
                    "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);",
                    "AU3 legacy/local staging helper must sync params/render/runtimeModel through canonical staging");
    requireAbsent(kernel,
                  "void stageRenderParam_(ParamID pid, float v) noexcept {\n        const size_t idx = static_cast<size_t>(pid);",
                  "AU3 legacy/local staging helper must not manually write params/renderParams");
}

void test_param_specific_sanitize_default_contract() {
    using namespace ArpSID;
    // Behavioral core proof for the bug class: for a non-zero-default param, NaN
    // must sanitize to that param's default, not to the generic pre-clamp value 0.0.
    const float def = defaultNormalizedParamValue(static_cast<int>(kParamMasterVolume));
    const float clean = sanitizeNormalizedParamValue(static_cast<int>(kParamMasterVolume),
                                                     std::numeric_limits<float>::quiet_NaN(),
                                                     def);
    require(clean == def, "param-specific sanitize must preserve non-zero defaults for NaN");
    require(def != 0.0f, "test param must have a non-zero default to catch pre-clamp-to-zero regressions");
}
}

int main() {
    test_shared_stage_helper_does_not_preclamp();
    test_target_adapter_does_not_preclamp_body_apply();
    test_au3_legacy_stage_render_param_uses_canonical_staging();
    test_param_specific_sanitize_default_contract();
    std::cout << "AuthorityFinalStagingV946Tests PASS\n";
    return 0;
}
