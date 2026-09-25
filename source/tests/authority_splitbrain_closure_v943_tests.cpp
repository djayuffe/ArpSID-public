// authority_splitbrain_closure_v943_tests.cpp
// Behavioral/source guard for v943 param/model/backend authority split-brain closure.

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
        std::cerr << "AuthoritySplitBrainClosureV943Tests FAIL: " << msg << "\n";
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

void test_effective_helpers_reject_hidden_secondary_authorities() {
    using namespace ArpSID;
    auto synthWithRawSecondaries = params(1.0f, 0.0f, 1.0f, 1.0f);
    require(sidResolveRenderModeFromLiveParams(synthWithRawSecondaries) == SidRuntimeRenderMode::SidRegister,
            "SynthMode still resolves above stale ARP/SEQ");
    require(!sidEffectiveArpAuthorityFromLiveParams(synthWithRawSecondaries),
            "raw ARP is not effective under SynthMode");
    require(!sidEffectiveSeqAuthorityFromLiveParams(synthWithRawSecondaries),
            "raw SEQ is not effective under SynthMode");

    auto drsidWithRawSecondaries = params(0.0f, 1.0f, 1.0f, 1.0f);
    require(sidResolveRenderModeFromLiveParams(drsidWithRawSecondaries) == SidRuntimeRenderMode::DrSid,
            "DrSID still resolves above stale ARP/SEQ");
    require(!sidEffectiveArpAuthorityFromLiveParams(drsidWithRawSecondaries),
            "raw ARP is not effective under DrSID");
    require(sidEffectiveSeqAuthorityFromLiveParams(drsidWithRawSecondaries),
            "raw SEQ is effective under DrSID/SID808 drum sequencer");
}

void test_source_contract_for_param_model_backend_single_authority() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    const std::string phase2 = readFile((root + "/source/arpsid_processor_phase2.cpp").c_str());
    const std::string paramServices = readFile((root + "/include/arpsid/core/sid_runtime_parameter_services.h").c_str());

    requireContains(kernel,
                    "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), clean);",
                    "AU3 flavor enforcement must stage via canonical helper");
    requireContains(kernel,
                    "sidStateRootParamValue(runtimeModel_.stateRoot(), pid)",
                    "AU3 flavor enforcement must compare runtimeModel state-root");
    requireAbsent(kernel,
                  "params_[idx].store(clean, std::memory_order_relaxed);\n            renderParams_[idx] = clean;\n            dirty_[idx].store(false, std::memory_order_relaxed);\n            changed = true;",
                  "AU3 flavor enforcement must not patch params/renderParams without runtimeModel");
    requireContains(kernel,
                    "v943 mirrors sanitized\n        // mode bits back through canonical staging",
                    "AU3 render-mode sanitizer must mirror back to params/runtimeModel");
    requireContains(kernel,
                    "v949: ARP and SEQ are different authority laws",
                    "AU3 flush must canonicalize ARP/SEQ with split DrSID sequencer authority");
    requireContains(kernel,
                    "DrSID structural authority clears ARP, but preserves SEQ",
                    "AU3 DrSID structural branch must clear ARP while preserving SEQ");
    requireContains(kernel,
                    "raw SeqEnable cannot arm a dormant sequencer under SynthMode",
                    "AU3 SeqEnable policy must reject hidden SEQ under SynthMode but allow DrSID/SID808");

    requireContains(phase2,
                    "runtimeModel_.applyAutomationPoint(target, clean)",
                    "Phase2 direct staging must update runtimeModel state-root");
    requireContains(phase2,
                    "raw SeqEnable cannot arm dormant VST sequencer state under",
                    "Phase2 SeqEnable policy must reject hidden SEQ under SynthMode and allow DrSID/SID808 via helper");

    requireContains(paramServices,
                    "raw ArpEnable is not allowed to mutate/arm the ARP",
                    "ARP backend application must reject raw hidden ARP outside BitPerfect");
    requireContains(paramServices,
                    "const bool willEnable = rawEnable && sidEffectiveArpAuthorityFromLiveParams(target.runtimeParameterValues());",
                    "ARP backend must use shared effective authority helper");
    requireContains(paramServices,
                    "target.runtimePolicyHandleSeqEnable(0.0f);",
                    "special-mode cleanup must apply concrete SEQ backend/policy cleanup");
}
}

int main() {
    test_effective_helpers_reject_hidden_secondary_authorities();
    test_source_contract_for_param_model_backend_single_authority();
    std::cout << "AuthoritySplitBrainClosureV943Tests PASS\n";
    return 0;
}
