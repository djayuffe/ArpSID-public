// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_factory_definition_v504_tests.cpp
//
// Regression coverage for SID-808 factory metadata.
//
// Before this fix, factory slots 120-124 fell through the GM SFX family
// (Breath/Seashore/Bird/Telephone/Helicopter) and were only relabelled as
// drums by a parameter overlay. The PatchDefinition itself still claimed
// role=FX / synthMode=true, which left role-keyed code paths (applyFactory// DrSidDefaults guard, GUI flavour detection, persistence) seeing the wrong
// identity. These tests pin the new authored SID-808 PatchDefinitions so the
// metadata, display name, id, role, engine and authored parameter overlay all
// agree on each of the five kits.

#include "arpsid/patchbank/forensic_patch_bank.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

static void test_each_sid808_slot_has_authored_definition() {
    for (int slot = 120; slot <= 124; ++slot) {
        const ArpSID::PatchDefinition* def = ArpSID::getFactoryPatchDefinition(slot);
        check(def != nullptr, "SID-808 slot must have a PatchDefinition");
        if (!def) continue;
        // displayName starts with "SID-808 " and is non-empty.
        check(def->displayName.rfind("SID-808 ", 0) == 0,
              "SID-808 slot displayName must start with 'SID-808 '");
        // id starts with "SID_808_" so persistence / identity look-ups can
        // disambiguate these slots from any GM SFX slot they replaced.
        check(def->id.rfind("SID_808_", 0) == 0,
              "SID-808 slot id must start with 'SID_808_'");
        // role / engine identity:
        check(def->usage.role == ArpSID::PatchRole::Drum,
              "SID-808 slot usage.role must be Drum");
        check(def->staticState.drSidMode == true,
              "SID-808 slot staticState.drSidMode must be true");
        check(def->staticState.synthMode == false,
              "SID-808 slot staticState.synthMode must be false");
        // 8580 is the canonical x0x chip target for the SID-808 kits.
        check(def->staticState.chip == ArpSID::SidChipTarget::MOS8580,
              "SID-808 slot staticState.chip must be MOS8580");
        // SID-808 must be mono drum-gate driven (no poly/legato semantics).
        check(def->usage.monoPreferred == true,
              "SID-808 slot usage.monoPreferred must be true");
        check(def->usage.polyAllowed == false,
              "SID-808 slot usage.polyAllowed must be false");
        check(def->start.id == ArpSID::StartPolicyId::DrumGate,
              "SID-808 slot start.id must be DrumGate");
        // MIDI range covers the GM drum-channel range 35-81.
        check(def->usage.validMidiMin <= 35 && def->usage.validMidiMax >= 81,
              "SID-808 slot must accept GM drum range 35-81");
    }
}

static void test_each_sid808_kit_has_distinct_identity() {
    std::array<std::string, 5> names;
    std::array<std::string, 5> ids;
    for (int i = 0; i < 5; ++i) {
        const auto* def = ArpSID::getFactoryPatchDefinition(120 + i);
        if (!def) { check(false, "all 5 SID-808 slots must exist"); return; }
        names[i] = def->displayName;
        ids[i] = def->id;
    }
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            check(names[i] != names[j], "SID-808 kit names must be distinct");
            check(ids[i] != ids[j], "SID-808 kit ids must be distinct");
        }
    }
}

static void test_param_overlay_agrees_with_definition() {
    for (int slot = 120; slot <= 124; ++slot) {
        std::array<float, static_cast<size_t>(ArpSID::kNumParams)> params{};
        const bool ok = ArpSID::loadFactoryPatchNormalizedParamsForSlot(slot, params);
        check(ok, "loadFactoryPatchNormalizedParamsForSlot must succeed for SID-808 slots");
        if (!ok) continue;
        // Engine identity in the param image:
        check(params[static_cast<size_t>(ArpSID::kParamDrSidEnable)] > 0.5f,
              "SID-808 params must enable DrSID");
        check(params[static_cast<size_t>(ArpSID::kParamSynthModeEnable)] < 0.5f,
              "SID-808 params must NOT enable Synth Mode");
        // Analog X0X machine model (kParamDrSidMachineModel == 1.0).
        check(std::fabs(params[static_cast<size_t>(ArpSID::kParamDrSidMachineModel)] - 1.0f) < 0.05f,
              "SID-808 params must set kParamDrSidMachineModel to Analog X0X (1.0)");
        // Each kit must have an audible kick — non-zero kick tune and decay.
        check(params[static_cast<size_t>(ArpSID::kParamDrSidKickTune)] > 0.0001f,
              "SID-808 kit must have a non-zero kick tune");
        check(params[static_cast<size_t>(ArpSID::kParamDrSidKickDecay)] > 0.0001f,
              "SID-808 kit must have a non-zero kick decay");
        // Each kit must have an audible snare and hat.
        check(params[static_cast<size_t>(ArpSID::kParamDrSidSnareTone)] > 0.0001f,
              "SID-808 kit must have a non-zero snare tone");
        check(params[static_cast<size_t>(ArpSID::kParamDrSidHatTune)] > 0.0001f,
              "SID-808 kit must have a non-zero hat tune");
        // v859: the authored pattern is published but the sequencer defaults OFF
        // so loading a kit is silent until the user plays or enables SEQ.
        check(params[static_cast<size_t>(ArpSID::kParamSeqEnable)] < 0.5f,
              "SID-808 kit loads with the step sequencer OFF");
    }
}

static void test_each_kit_is_distinct_per_pad() {
    // Two adjacent kits must differ on at least the kick tune.
    for (int slot = 120; slot < 124; ++slot) {
        std::array<float, static_cast<size_t>(ArpSID::kNumParams)> a{}, b{};
        if (!ArpSID::loadFactoryPatchNormalizedParamsForSlot(slot, a)) continue;
        if (!ArpSID::loadFactoryPatchNormalizedParamsForSlot(slot + 1, b)) continue;
        const float ak = a[static_cast<size_t>(ArpSID::kParamDrSidKickTune)];
        const float bk = b[static_cast<size_t>(ArpSID::kParamDrSidKickTune)];
        check(std::fabs(ak - bk) > 0.001f,
              "adjacent SID-808 kits must differ on kick tune (variant identity)");
    }
}

int main() {
    std::printf("Running SID-808 factory definition tests (v504)...\n");
    test_each_sid808_slot_has_authored_definition();
    test_each_sid808_kit_has_distinct_identity();
    test_param_overlay_agrees_with_definition();
    test_each_kit_is_distinct_per_pad();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("All SID-808 factory definition tests passed.\n");
    return 0;
}
