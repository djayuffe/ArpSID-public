// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::vector<uint8_t> buildRsid(uint16_t loadAddr, const std::vector<uint8_t>& initCode) {
    std::vector<uint8_t> v(0x7Cu, 0u);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    v[4]=0; v[5]=2;
    v[6]=0; v[7]=0x7Cu;
    v[8]=0; v[9]=0;
    v[10]=static_cast<uint8_t>(loadAddr >> 8u);
    v[11]=static_cast<uint8_t>(loadAddr & 0xFFu);
    v[12]=0; v[13]=0; // RSID machine mode; no explicit play address.
    v[14]=0; v[15]=1; v[16]=0; v[17]=1;
    v[0x76]=0; v[0x77]=0x04; // PAL
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

static void testStrictRsidRunPlayUsesPhi2WithoutPlayAddress() {
    // RTS-only init is enough to reach the PHI2 idle loop. runPlay() must still
    // advance a machine-mode RSID even though the header playAddress is zero.
    auto rsid = buildRsid(0x0800u, {0x60u});
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "strict RSID loads");
    require(rt.runInit(1, 2048), "strict RSID init succeeds via PHI2");
    rt.enablePhi2Machine(true);
    const uint64_t before = rt.phi2Machine().phi2Cycle();
    require(rt.runPlay(4096), "strict RSID runPlay advances via C64Phi2Machine");
    require(rt.phi2Machine().phi2Cycle() > before, "PHI2 cycle counter advances during runPlay");
    const auto d = rt.rsidExactnessDowngradeReasons();
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::LegacyCpuPlayback),
            "strict RSID runPlay did not use legacy Mos6510 playback");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::StrictRsidNotPhi2),
            "strict RSID runPlay remained on PHI2 path");
}

static void testStrictRsidRefusesNonPhi2Playback() {
    auto rsid = buildRsid(0x0800u, {0x60u});
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "strict RSID loads");
    require(rt.runInit(1, 2048), "strict RSID init succeeds");
    rt.enablePhi2Machine(false);
    require(!rt.runPlay(128), "strict RSID refuses playback when PHI2 is disabled");
    const auto d = rt.rsidExactnessDowngradeReasons();
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::LegacyCpuPlayback),
            "strict non-PHI2 refusal does not claim legacy playback was executed");
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::StrictRsidNotPhi2),
            "strict non-PHI2 refusal sets StrictRsidNotPhi2 downgrade");
}

static void testStrictRsidInitDoesNotFallbackToLegacy() {
    // BRK-as-halt is a compatibility fixture. In strict mode BRK vectors instead
    // of becoming a JAM sentinel. With a tiny budget, PHI2 init cannot complete;
    // strict mode must fail rather than silently falling back to Mos6510.
    auto rsid = buildRsid(0x0800u, {0x00u});
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "strict BRK RSID loads");
    require(!rt.runInit(1, 4), "strict RSID init failure does not fallback to legacy");
    const auto d = rt.rsidExactnessDowngradeReasons();
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::StrictRsidNotPhi2),
            "strict failed init records StrictRsidNotPhi2 downgrade");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::LegacyFallback),
            "strict failed init does not record compatibility legacy fallback");
}

int main() {
    testStrictRsidRunPlayUsesPhi2WithoutPlayAddress();
    testStrictRsidRefusesNonPhi2Playback();
    testStrictRsidInitDoesNotFallbackToLegacy();
    std::cout << "C64StrictRsidPhi2OnlyV719Tests PASS\n";
    return 0;
}
