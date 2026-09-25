// authority_postbatch_canonicalization_v944_tests.cpp
// Guard for post-batch structural authority canonicalization.

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
        std::cerr << "AuthorityPostBatchCanonicalizationV944Tests FAIL: " << msg << "\n";
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

std::array<float, ArpSID::kNumParams> params(float synth, float drsid, float arp, float seq) {
    std::array<float, ArpSID::kNumParams> p{};
    p[(size_t)ArpSID::kParamSynthModeEnable] = synth;
    p[(size_t)ArpSID::kParamDrSidEnable] = drsid;
    p[(size_t)ArpSID::kParamArpEnable] = arp;
    p[(size_t)ArpSID::kParamSeqEnable] = seq;
    return p;
}

void test_effective_helpers_keep_secondaries_bitperfect_only() {
    using namespace ArpSID;
    auto classicSecondaries = params(0.0f, 0.0f, 1.0f, 1.0f);
    require(sidResolveRenderModeFromLiveParams(classicSecondaries) == SidRuntimeRenderMode::BitPerfect,
            "Classic resolves to BitPerfect");
    require(sidEffectiveArpAuthorityFromLiveParams(classicSecondaries),
            "ARP may only be effective in BitPerfect");
    require(sidEffectiveSeqAuthorityFromLiveParams(classicSecondaries),
            "SEQ may only be effective in BitPerfect");

    auto synthSecondaries = params(1.0f, 0.0f, 1.0f, 1.0f);
    require(!sidEffectiveArpAuthorityFromLiveParams(synthSecondaries),
            "ARP cannot be effective under SynthMode");
    require(!sidEffectiveSeqAuthorityFromLiveParams(synthSecondaries),
            "SEQ cannot be effective under SynthMode");

    auto drsidSecondaries = params(0.0f, 1.0f, 1.0f, 1.0f);
    require(!sidEffectiveArpAuthorityFromLiveParams(drsidSecondaries),
            "ARP cannot be effective under DrSID");
    require(sidEffectiveSeqAuthorityFromLiveParams(drsidSecondaries),
            "SEQ must be effective under DrSID/SID808 drum sequencer");
}

void test_source_contract_for_same_block_order_canonicalization() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    const std::string phase2 = readFile((root + "/source/arpsid_processor_phase2.cpp").c_str());
    const std::string phase2h = readFile((root + "/source/arpsid_processor_phase2.h").c_str());

    requireContains(kernel,
                    "const auto modeBeforeFlush = ArpSID::sidResolveRenderModeFromLiveParams(renderParams_);",
                    "AU3 must snapshot pre-batch mode before dirty param flush");
    requireContains(kernel,
                    "const bool structuralModeReturnToBitPerfect =",
                    "AU3 must detect same-block structural return to BitPerfect");
    requireContains(kernel,
                    "cannot revive old secondaries on the transition block",
                    "AU3 must document/order-guard same-block ARP/SEQ re-arm prevention");

    requireContains(phase2h,
                    "canonicalizeStructuralAuthorityAfterCanonicalBlock_(ArpSID::SidRuntimeRenderMode modeBeforeBlock)",
                    "Phase2 must expose post-canonical structural authority cleanup");
    requireContains(phase2,
                    "const auto modeBeforeCanonicalBlock = resolveTopLevelRenderMode_();",
                    "Phase2 must snapshot pre-canonical block mode");
    requireContains(phase2,
                    "canonicalizeStructuralAuthorityAfterCanonicalBlock_(modeBeforeCanonicalBlock);",
                    "Phase2 must canonicalize after timed automation has executed");
    requireContains(phase2,
                    "Same-block return to pure Classic",
                    "Phase2 must explicitly guard same-block ARP/SEQ re-arm");
}
}

int main() {
    test_effective_helpers_keep_secondaries_bitperfect_only();
    test_source_contract_for_same_block_order_canonicalization();
    std::cout << "AuthorityPostBatchCanonicalizationV944Tests PASS\n";
    return 0;
}
