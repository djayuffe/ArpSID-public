// bass_6581_authority_v507_tests.cpp
//
// Regression coverage for the 6581 vs 8580 bass treatment in
// applyFactoryAuthenticC64BassDefaults(). 6581 bass needs:
// • kParamSidAdsrBug6581 explicitly ON (authentic 6581 envelope behavior)
// • slightly lifted kParamFilterCutoff (6581 filter is darker)
// • tightened kParamFilterDrive (6581 saturates earlier)
// 8580 bass needs:
// • kParamSidAdsrBug6581 explicitly OFF (8580 has no ADSR bug)
//
// We don't have a real PatchDefinition with chip=MOS6581 in this isolated
// test, so we synthesise minimal handlers that name the role/chip and run them
// through the overlay function directly.

#include "factory_patch_params.h"
#include "parameter_ids.h"
#include "arpsid/patchbank/forensic_patch_bank.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

static ArpSID::PatchDefinition makeBassHandler(ArpSID::SidChipTarget chip,
                                            const char* displayName) {
    ArpSID::PatchDefinition def{};
    def.displayName = displayName;
    def.id = "TEST_BASS";
    def.staticState.chip = chip;
    def.usage.role = ArpSID::PatchRole::Bass;
    def.usage.family = ArpSID::HistoricalFamilyId::CoreBass;
    return def;
}

static std::array<float, static_cast<size_t>(ArpSID::kNumParams)>
defaultParams() {
    std::array<float, static_cast<size_t>(ArpSID::kNumParams)> p{};
    for (size_t i = 0; i < p.size(); ++i) p[i] = 0.0f;
    return p;
}

static void test_6581_bass_enables_adsr_bug() {
    auto def = makeBassHandler(ArpSID::SidChipTarget::MOS6581, "6581 Acoustic Bass");
    auto params = defaultParams();
    ArpSID::applyFactoryAuthenticC64BassDefaults(def, params);
    check(params[static_cast<size_t>(ArpSID::kParamSidAdsrBug6581)] > 0.5f,
          "6581 bass must enable kParamSidAdsrBug6581");
}

static void test_8580_bass_disables_adsr_bug() {
    auto def = makeBassHandler(ArpSID::SidChipTarget::MOS8580, "8580 Synth Bass");
    auto params = defaultParams();
    ArpSID::applyFactoryAuthenticC64BassDefaults(def, params);
    check(params[static_cast<size_t>(ArpSID::kParamSidAdsrBug6581)] < 0.5f,
          "8580 bass must disable kParamSidAdsrBug6581");
}

static void test_6581_bass_cutoff_floor_is_lifted() {
    auto def = makeBassHandler(ArpSID::SidChipTarget::MOS6581, "6581 Electric Bass");
    auto params = defaultParams();
    ArpSID::applyFactoryAuthenticC64BassDefaults(def, params);
    // Base 6581 cutoff is 0.24; with +0.04 floor lift the cutoff should be
    // strictly above 0.27 to keep the fundamental from collapsing into mud.
    check(params[static_cast<size_t>(ArpSID::kParamFilterCutoff)] > 0.27f,
          "6581 bass must have a lifted filter cutoff (>0.27)");
}

static void test_6581_bass_drive_is_tightened() {
    auto def = makeBassHandler(ArpSID::SidChipTarget::MOS6581, "6581 Pick Bass");
    auto params = defaultParams();
    ArpSID::applyFactoryAuthenticC64BassDefaults(def, params);
    check(params[static_cast<size_t>(ArpSID::kParamFilterDrive)] <= 0.12f + 1.0e-6f,
          "6581 bass must clamp filter drive ≤ 0.12 to avoid the chip's self-resonant region");
}

static void test_8580_bass_cutoff_not_overlifted() {
    auto def = makeBassHandler(ArpSID::SidChipTarget::MOS8580, "8580 Slap Bass");
    auto params = defaultParams();
    ArpSID::applyFactoryAuthenticC64BassDefaults(def, params);
    // 8580 base cutoff is 0.30; no chip-specific lift should be applied.
    check(params[static_cast<size_t>(ArpSID::kParamFilterCutoff)] < 0.34f,
          "8580 bass cutoff must not be lifted into the 6581 compensation range");
}

static void test_portamento_style_pinned_to_c64_register_slide() {
    auto def = makeBassHandler(ArpSID::SidChipTarget::MOS6581, "Any Bass");
    auto params = defaultParams();
    ArpSID::applyFactoryAuthenticC64BassDefaults(def, params);
    check(params[static_cast<size_t>(ArpSID::kParamPortamentoStyle)] < 0.01f,
          "Bass must pin kParamPortamentoStyle to C64 register-slide (index 0)");
}

int main() {
    std::printf("Running 6581 bass authority tests (v507)...\n");
    test_6581_bass_enables_adsr_bug();
    test_8580_bass_disables_adsr_bug();
    test_6581_bass_cutoff_floor_is_lifted();
    test_6581_bass_drive_is_tightened();
    test_8580_bass_cutoff_not_overlifted();
    test_portamento_style_pinned_to_c64_register_slide();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("All 6581 bass authority tests passed.\n");
    return 0;
}
