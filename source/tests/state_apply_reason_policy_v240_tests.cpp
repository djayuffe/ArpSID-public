#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireNear(float a, float b, const char* msg, float eps = 1.0e-5f) {
    if (!std::isfinite(a) || std::fabs(a - b) > eps) {
        std::cerr << "FAIL: " << msg << " got=" << a << " expected=" << b << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;

    static_assert(ArpSID::sidStateOverlayPolicyForApplyReason(ArpSID::SidStateApplyReason::ExplicitPresetSelection) ==
                  ArpSID::SidStateOverlayPolicy::FactoryRootIsAudibleAuthority,
                  "explicit user preset selection must not be live-overlay policy");
    static_assert(ArpSID::sidStateOverlayPolicyForApplyReason(ArpSID::SidStateApplyReason::HostTransportReset) ==
                  ArpSID::SidStateOverlayPolicy::FactoryRootSeedThenLiveAudioOverlay,
                  "host transport reset must seed factory root then live-overlay audio params");
    static_assert(ArpSID::sidStateOverlayPolicyForApplyReason(ArpSID::SidStateApplyReason::ProjectRestore) ==
                  ArpSID::SidStateOverlayPolicy::SerializedStateIsAuthority,
                  "project restore must be serialized-state authority");
    static_assert(ArpSID::sidStateOverlayPolicyForApplyReason(ArpSID::SidStateApplyReason::WrapperFullStateRestore) ==
                  ArpSID::SidStateOverlayPolicy::SnapshotIsAudibleAuthority,
                  "wrapper snapshot restore must be snapshot authority");

    ArpSIDDSPKernel k;
    k.setup(48000.0, 512);
    constexpr int slot = 121;
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    require(root.valid(), "slot 121 root must be valid");
    k.setStickyPresetDisplaySlot(slot);
    k.applyStateRootCanonical(root);

    std::array<float, kNumParams> snapshot{};
    for (int pid = 0; pid < kNumParams; ++pid) snapshot[(size_t)pid] = k.getParameter(pid);
    snapshot[(size_t)kParamDrSidEnable] = 1.0f;
    snapshot[(size_t)kParamSynthModeEnable] = 0.0f;
    snapshot[(size_t)kParamDrSidKickTune] = 0.79f;
    snapshot[(size_t)kParamDrSidOutputDrive] = 0.22f;
    snapshot[(size_t)kParamProgram] = 0.0f;
    snapshot[(size_t)kParamBankSlot] = 0.0f;

    k.resetPreservingHostParameterSnapshot(snapshot.data(), kNumParams, slot, true);
    require(k.stickyPresetDisplaySlot() == slot, "host transport reset policy must preserve selected sticky slot");
    requireNear(k.getParameter(kParamBankSlot), canonicalNormalizedBankSlotValue(slot), "host transport reset policy must reject stale BankSlot metadata");
    requireNear(k.getParameter(kParamProgram), canonicalNormalizedFactoryProgramValue(slot), "host transport reset policy must reject stale Program metadata");
    requireNear(k.getParameter(kParamDrSidKickTune), 0.79f, "host transport reset policy must overlay persistent audio edits");
    requireNear(k.getParameter(kParamDrSidOutputDrive), 0.22f, "host transport reset policy must overlay persistent DrSID drive edit");

    std::cout << "StateApplyReasonPolicyV240Tests PASS\n";
    return 0;
}
