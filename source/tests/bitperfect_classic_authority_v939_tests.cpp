// Copyright (C) 2024-2026 Ulf Bertilsson
// bitperfect_classic_authority_v939_tests.cpp
// Behavioral + source guard for v939 first-class CLASSIC / BitPerfect authority.

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
        std::cerr << "bitperfect_classic_authority_v939_tests FAIL: " << msg << "\n";
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

void test_effective_helpers_are_first_class_bitperfect_authority() {
    using namespace ArpSID;
    auto classic = params(0.0f, 0.0f, 1.0f, 1.0f);
    require(sidResolveRenderModeFromLiveParams(classic) == SidRuntimeRenderMode::BitPerfect,
            "classic flags resolve to BitPerfect");
    require(sidEffectiveArpAuthorityFromLiveParams(classic),
            "ARP can be effective only as explicit BitPerfect secondary authority");
    require(sidEffectiveSeqAuthorityFromLiveParams(classic),
            "SEQ can be effective only as explicit BitPerfect secondary authority");

    auto synth = params(1.0f, 0.0f, 1.0f, 1.0f);
    require(sidResolveRenderModeFromLiveParams(synth) == SidRuntimeRenderMode::SidRegister,
            "SynthMode has priority over stale ARP/SEQ");
    require(!sidEffectiveArpAuthorityFromLiveParams(synth),
            "stale ARP is not effective in SynthMode");
    require(!sidEffectiveSeqAuthorityFromLiveParams(synth),
            "stale SEQ is not effective in SynthMode");

    auto drsid = params(0.0f, 1.0f, 1.0f, 1.0f);
    require(sidResolveRenderModeFromLiveParams(drsid) == SidRuntimeRenderMode::DrSid,
            "DrSID has priority over stale ARP/SEQ");
    require(!sidEffectiveArpAuthorityFromLiveParams(drsid),
            "stale ARP is not effective in DrSID");
    require(sidEffectiveSeqAuthorityFromLiveParams(drsid),
            "SEQ is effective in DrSID/SID808 drum sequencer");
}

void test_state_root_model_masks_stale_arp_under_synth_and_drsid() {
    using namespace ArpSID;
    SidRuntimeModel model;
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamArpEnable), 1.0f);
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamDrSidEnable), 0.0f);
    require(model.resolveRenderMode() == SidRuntimeRenderMode::BitPerfect, "model starts in BitPerfect");
    require(model.isArpEnabled(), "model ARP is effective in BitPerfect");

    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamSynthModeEnable), 1.0f);
    require(model.resolveRenderMode() == SidRuntimeRenderMode::SidRegister, "model enters SynthMode");
    require(!model.isArpEnabled(), "model masks stale ARP under SynthMode");

    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamDrSidEnable), 1.0f);
    require(model.resolveRenderMode() == SidRuntimeRenderMode::DrSid, "model enters DrSID");
    require(!model.isArpEnabled(), "model masks stale ARP under DrSID");
}

void test_source_contract_no_stale_mode_switch_or_cleanup() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string vc = readFile((root + "/source/au3/ArpSIDViewController.mm").c_str());
    const std::string kernel = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    const std::string phase2 = readFile((root + "/source/arpsid_processor_phase2.cpp").c_str());
    const std::string model = readFile((root + "/include/arpsid/core/sid_runtime_model.h").c_str());

    requireContains(vc, "const float arp = 0.f;\n    const float seq = modeIsDrSid",
                    "mode selector must clear ARP while preserving DrSID/SID808 SEQ transport");
    requireContains(vc, "_cachedParamValue:ArpSID::kParamSeqEnable",
                    "mode selector must only preserve SeqEnable through the explicit DrSID/SID808 transport cache");
    requireAbsent(vc, "const float arp = (_componentFlavor == Instrument)",
                  "mode selector must not preserve stale ArpEnable cache");
    requireContains(kernel, "const bool seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);",
                    "AU3 sequencer must use effective SEQ helper");
    requireContains(kernel, "seqEngine_.resetPhase();\n        prevSeqEnabled_ = false;",
                    "AU3 mode transition must reset sequencer phase and enabled latch");
    requireContains(phase2, "const bool seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(paramValues);",
                    "Phase2 sequencer must use effective SEQ helper");
    requireContains(phase2, "const bool arpMode = ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues);",
                    "Phase2 virtual gate must use the shared effective ARP helper");
    const std::string renderHost = readFile((root + "/include/arpsid/core/sid_runtime_render_host.h").c_str());
    requireContains(renderHost, "if (bank.bitPerfect) bank.bitPerfect->allNotesOff();",
                    "mode transition helper must silence latent BitPerfect voices");
    requireContains(renderHost, "target.runtimeModel().clearAllCanonicalVoiceState();",
                    "mode transition helper must clear canonical voice state");
    requireContains(renderHost, "target.runtimeModel().setSeqSamplesUntilStep(-1.0);",
                    "mode transition helper must clear sequencer countdown");
    const std::string paramServices = readFile((root + "/include/arpsid/core/sid_runtime_parameter_services.h").c_str());
    requireContains(paramServices, "enabling DrSID/SID808 must not force SeqEnable off",
                    "DrSID enable must clear Synth/ARP but preserve SEQ drum transport authority");
    requireContains(model, "inline bool sidEffectiveArpAuthorityFromLiveParams",
                    "shared effective ARP helper must exist");
    requireContains(model, "inline bool sidEffectiveSeqAuthorityFromLiveParams",
                    "shared effective SEQ helper must exist");
}
}

int main() {
    test_effective_helpers_are_first_class_bitperfect_authority();
    test_state_root_model_masks_stale_arp_under_synth_and_drsid();
    test_source_contract_no_stale_mode_switch_or_cleanup();
    std::cout << "BitPerfectClassicAuthorityV939Tests PASS\n";
    return 0;
}
