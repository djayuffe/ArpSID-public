// Copyright (C) 2024-2026 Ulf Bertilsson
#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include "arpsid/core/sid_gm_drum_kit.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireFiniteUnit(float v, const char* msg) {
    if (!std::isfinite(v) || v < 0.0f || v > 1.0f) {
        std::cerr << "FAIL: " << msg << " value=" << v << "\n";
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

    // Authored DrSID / SID-808 / projection slots known to be exposed by GUI and factory bank.
    const std::array<int, 15> slots{{47,112,113,114,115,116,117,118,119,120,121,122,123,124,127}};

    for (const int slot : slots) {
        SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        require(root.valid(), "authored kit root must be valid");

        for (int pid = 0; pid < kNumParams; ++pid) {
            const float v = sidStateRootParamValue(root, pid);
            requireFiniteUnit(v, "factory kit param must be finite unit range");
        }

        require(sidStateRootParamValue(root, kParamDrSidEnable) > 0.5f, "authored drum kit must enable DrSID");
        require(sidStateRootParamValue(root, kParamSynthModeEnable) <= 0.5f, "authored drum kit must not enable synth mode");

        if (slot >= 120 && slot <= 124) {
            requireNear(sidStateRootParamValue(root, kParamDrSidMachineModel), 1.0f, "SID-808 kit must force Analog X0X model");
        }

        ArpSIDDSPKernel k;
        k.setup(48000.0, 512);
        k.setStickyPresetDisplaySlot(slot);
        SidStateRootV1 applied = root;
        k.applyStateRootCanonical(applied);
        require(k.stickyPresetDisplaySlot() == slot, "sticky slot must be set before reset sweep");

        std::array<float, kNumParams> snapshot{};
        for (int pid = 0; pid < kNumParams; ++pid) snapshot[(size_t)pid] = k.getParameter(pid);

        // Make a slot-specific deterministic edit that must be preserved across reset.
        snapshot[(size_t)kParamDrSidKickTune] = 0.10f + 0.005f * (float)(slot % 40);
        snapshot[(size_t)kParamDrSidSnareTone] = 0.80f - 0.003f * (float)(slot % 40);
        snapshot[(size_t)kParamProgram] = 0.0f;
        snapshot[(size_t)kParamBankSlot] = 0.0f;

        k.resetPreservingHostParameterSnapshot(snapshot.data(), kNumParams, slot, true);
        require(k.stickyPresetDisplaySlot() == slot, "reset must retain sticky slot for every authored kit");
        requireNear(k.getParameter(kParamBankSlot), canonicalNormalizedBankSlotValue(slot), "reset BankSlot must mirror selected kit slot");
        requireNear(k.getParameter(kParamProgram), canonicalNormalizedFactoryProgramValue(slot), "reset Program must mirror selected kit slot");
        requireNear(k.getParameter(kParamDrSidEnable), 1.0f, "reset must keep DrSID enabled");
        requireNear(k.getParameter(kParamSynthModeEnable), 0.0f, "reset must keep synth mode disabled");
        requireNear(k.getParameter(kParamDrSidKickTune), snapshot[(size_t)kParamDrSidKickTune], "reset must preserve kick edit for every kit");
        requireNear(k.getParameter(kParamDrSidSnareTone), snapshot[(size_t)kParamDrSidSnareTone], "reset must preserve snare edit for every kit");
    }

    for (int note = 35; note <= 81; ++note) {
        const auto spec = sidGMDrumSpecForNote((uint8_t)note);
        require(spec.drumClass != SidGMDrumClass::Unsupported, "GM percussion note must be supported");
        require(spec.velocityScale > 0.0f && spec.velocityScale <= 1.25f, "GM velocity scale range");
        require(spec.decayScale > 0.0f && spec.decayScale <= 1.80f, "GM decay scale range");
    }

    std::cout << "FactoryDrSidKitBankSweepV238Tests PASS\n";
    return 0;
}
