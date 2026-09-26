// Copyright (C) 2024-2026 Ulf Bertilsson
#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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

    ArpSIDDSPKernel k;
    k.setup(48000.0, 512);

    constexpr int selectedSlot = 120;
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(selectedSlot);
    require(root.valid(), "selected DrSID/SID-808 factory root must be valid");
    k.setStickyPresetDisplaySlot(selectedSlot);
    k.applyStateRootCanonical(root);

    std::array<float, kNumParams> snapshot{};
    for (int i = 0; i < kNumParams; ++i) snapshot[(size_t)i] = k.getParameter(i);

    // Live edits that must remain audible authority across Logic Stop/Start
    // AudioUnitReset. These are intentionally not the factory defaults.
    snapshot[(size_t)kParamDrSidEnable] = 1.0f;
    snapshot[(size_t)kParamSynthModeEnable] = 0.0f;
    snapshot[(size_t)kParamDrSidMachineModel] = 1.0f;
    snapshot[(size_t)kParamDrSidKickTune] = 0.73f;
    snapshot[(size_t)kParamDrSidKickDecay] = 0.61f;
    snapshot[(size_t)kParamDrSidSnareTone] = 0.42f;
    snapshot[(size_t)kParamDrSidOutputDrive] = 0.31f;
    snapshot[(size_t)kParamDrSidAccentAmount] = 0.57f;
    snapshot[(size_t)kParamDrSidHatMetal] = 0.86f;
    snapshot[(size_t)kParamDrSidClapSpread] = 0.24f;

    // Host poison: Logic may replay stale slot/program values around reset.
    // The reset path must use selectedSlot/sticky factory authority for metadata,
    // while the live persistent audio snapshot stays audible authority.
    snapshot[(size_t)kParamProgram] = 0.0f;
    snapshot[(size_t)kParamBankSlot] = 0.0f;
    snapshot[(size_t)kParamVirtualGate] = 1.0f;
    snapshot[(size_t)kParamVirtualNote] = 1.0f;

    k.resetPreservingHostParameterSnapshot(snapshot.data(), kNumParams, selectedSlot, true);

    require(k.stickyPresetDisplaySlot() == selectedSlot, "sticky preset slot must survive reset");
    requireNear(k.getParameter(kParamBankSlot), canonicalNormalizedBankSlotValue(selectedSlot), "BankSlot readback must ignore stale slot 0");
    requireNear(k.getParameter(kParamProgram), canonicalNormalizedFactoryProgramValue(selectedSlot), "Program readback must mirror selected slot");

    requireNear(k.getParameter(kParamDrSidEnable), 1.0f, "DrSID enable must survive transport reset");
    requireNear(k.getParameter(kParamSynthModeEnable), 0.0f, "Synth mode must stay disabled for DrSID kit");
    requireNear(k.getParameter(kParamDrSidMachineModel), 1.0f, "DrSID machine model must survive reset");
    requireNear(k.getParameter(kParamDrSidKickTune), 0.73f, "Kick tune edit must survive reset");
    requireNear(k.getParameter(kParamDrSidKickDecay), 0.61f, "Kick decay edit must survive reset");
    requireNear(k.getParameter(kParamDrSidSnareTone), 0.42f, "Snare tone edit must survive reset");
    requireNear(k.getParameter(kParamDrSidOutputDrive), 0.31f, "Output drive edit must survive reset");
    requireNear(k.getParameter(kParamDrSidAccentAmount), 0.57f, "Accent edit must survive reset");
    requireNear(k.getParameter(kParamDrSidHatMetal), 0.86f, "Hat metal edit must survive reset");
    requireNear(k.getParameter(kParamDrSidClapSpread), 0.24f, "Clap spread edit must survive reset");

    // Transients and metadata are never allowed to become reset authority.
    requireNear(k.getParameter(kParamVirtualGate), kParamInfos[(size_t)kParamVirtualGate].defaultNorm, "VirtualGate must not be overlaid from reset snapshot");
    requireNear(k.getParameter(kParamVirtualNote), kParamInfos[(size_t)kParamVirtualNote].defaultNorm, "VirtualNote must not be overlaid from reset snapshot");

    std::cout << "LogicStopStartDrSidKitV238Tests PASS\n";
    return 0;
}
