// Copyright (C) 2024-2026 Ulf Bertilsson
// authority_staging_policy_v945_tests.cpp
// Guard for v945 canonical param-specific staging and Phase2/AU3 policy hardening.

#include "arpsid/core/sid_runtime_model.h"
#include "parameter_ids.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {
void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "AuthorityStagingPolicyV945Tests FAIL: " << msg << "\n";
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

std::array<float, ArpSID::kNumParams> params(float synth, float drsid, float arp, float seq) {
    std::array<float, ArpSID::kNumParams> p{};
    p[(size_t)ArpSID::kParamSynthModeEnable] = synth;
    p[(size_t)ArpSID::kParamDrSidEnable] = drsid;
    p[(size_t)ArpSID::kParamArpEnable] = arp;
    p[(size_t)ArpSID::kParamSeqEnable] = seq;
    return p;
}

void test_shared_effective_authority_contract() {
    using namespace ArpSID;
    auto synth = params(1.0f, 0.0f, 1.0f, 1.0f);
    auto drsid = params(0.0f, 1.0f, 1.0f, 1.0f);
    auto bitperfect = params(0.0f, 0.0f, 1.0f, 1.0f);
    require(!sidEffectiveArpAuthorityFromLiveParams(synth), "ARP must stay ineffective under SynthMode");
    require(!sidEffectiveSeqAuthorityFromLiveParams(synth), "SEQ must stay ineffective under SynthMode");
    require(!sidEffectiveArpAuthorityFromLiveParams(drsid), "ARP must stay ineffective under DrSID");
    require(sidEffectiveSeqAuthorityFromLiveParams(drsid), "SEQ must be effective under DrSID/SID808 drum sequencer");
    require(sidEffectiveArpAuthorityFromLiveParams(bitperfect), "ARP is allowed only under BitPerfect");
    require(sidEffectiveSeqAuthorityFromLiveParams(bitperfect), "SEQ is allowed only under BitPerfect");
}

void test_source_contract_for_param_specific_staging_and_policy_cleanup() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    const std::string phase2 = readFile((root + "/source/arpsid_processor_phase2.cpp").c_str());
    const std::string classic = readFile((root + "/source/tests/classic_mode_authority_closure_v909_tests.cpp").c_str());

    requireContains(kernel,
                    "const float clean = ArpSID::sanitizeNormalizedParamValue(\n            static_cast<int>(target),",
                    "AU3 runtimeStageNormalizedParameterOnly must use param-specific sanitize");
    requireContains(kernel,
                    "AU3 and Phase2 share the same param-specific staging law",
                    "AU3 staging must document shared v945 law");
    requireAbsent(kernel,
                  "const float clamped = ArpSID::canonicalClampedNormalizedValue(value);\n        params_[idx].store(clamped",
                  "AU3 staging must not use generic clamp-only writes");

    requireContains(phase2,
                    "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);\n        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);",
                    "Phase2 SynthMode policy must clear ARP/SEQ through canonical staging");
    requireContains(phase2,
                    "arp->allNotesOff();\n            arp->setEnabled(false);",
                    "Phase2 SynthMode policy must concretely disable ARP backend state");
    requireContains(phase2,
                    "runtimePolicyHandleSeqEnable(0.0f);",
                    "Phase2 SynthMode policy must apply SEQ backend/policy cleanup");
    requireAbsent(phase2,
                  "paramValues[(size_t)kParamArpEnable] = 0.0f;\n        paramValues[(size_t)kParamSeqEnable] = 0.0f;",
                  "Phase2 SynthMode policy must not bypass canonical staging");

    requireContains(classic,
                    "through canonical staging",
                    "legacy closure guard must protect canonical Phase2 clearing, not direct paramValues writes");
}
}

int main() {
    test_shared_effective_authority_contract();
    test_source_contract_for_param_specific_staging_and_policy_cleanup();
    std::cout << "AuthorityStagingPolicyV945Tests PASS\n";
    return 0;
}
