#include "arpsid/core/sid_runtime_state_apply_policy.h"

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

void testCanonicalMapping() {
    using ArpSID::SidStateApplyReason;
    using ArpSID::SidStateOverlayPolicy;
    using ArpSID::sidStateOverlayPolicyForApplyReason;

    static_assert(sidStateOverlayPolicyForApplyReason(SidStateApplyReason::ExplicitPresetSelection) ==
                  SidStateOverlayPolicy::FactoryRootIsAudibleAuthority,
                  "factory preset selection is factory-root authority");
    static_assert(sidStateOverlayPolicyForApplyReason(SidStateApplyReason::ImportBankPatch) ==
                  SidStateOverlayPolicy::FactoryRootIsAudibleAuthority,
                  "imported bank patch is factory-root authority");
    static_assert(sidStateOverlayPolicyForApplyReason(SidStateApplyReason::HostTransportReset) ==
                  SidStateOverlayPolicy::FactoryRootSeedThenLiveAudioOverlay,
                  "host transport reset seeds root then overlays live audio authority");
    static_assert(sidStateOverlayPolicyForApplyReason(SidStateApplyReason::ProjectRestore) ==
                  SidStateOverlayPolicy::SerializedStateIsAuthority,
                  "project restore is serialized-state authority");
    static_assert(sidStateOverlayPolicyForApplyReason(SidStateApplyReason::WrapperFullStateRestore) ==
                  SidStateOverlayPolicy::SnapshotIsAudibleAuthority,
                  "wrapper snapshot restore is snapshot authority");
}

void testWrappersDoNotOwnPolicyTable() {
    const std::string au = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string vst = readText("source/arpsid_processor_phase2.h");

    require(au.find("#include \"arpsid/core/sid_runtime_state_apply_policy.h\"") != std::string::npos,
            "AU wrapper must include the canonical state-apply policy header");
    require(vst.find("#include \"arpsid/core/sid_runtime_state_apply_policy.h\"") != std::string::npos,
            "VST wrapper must include the canonical state-apply policy header");

    require(au.find("enum class StateApplyReason") == std::string::npos,
            "AU wrapper must not own a StateApplyReason enum table");
    require(au.find("enum class StateOverlayPolicy") == std::string::npos,
            "AU wrapper must not own a StateOverlayPolicy enum table");
    require(au.find("policyForStateApplyReason(") == std::string::npos,
            "AU wrapper must not own the state-apply policy mapper");
    require(au.find("sidStateOverlayPolicyForApplyReason") != std::string::npos,
            "AU wrapper must call the canonical core mapper");
}

} // namespace

int main() {
    testCanonicalMapping();
    testWrappersDoNotOwnPolicyTable();
    std::cout << "wrapper_state_apply_policy_core_v762_tests: PASS\n";
    return 0;
}
