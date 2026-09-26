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
    v[12]=0; v[13]=0;
    v[14]=0; v[15]=1; v[16]=0; v[17]=1;
    v[0x76]=0; v[0x77]=0x04;
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

static void testDirectStrictCycleRunRefusalDoesNotClaimLegacyPlayback() {
    auto rsid = buildRsid(0x0800u, {0x60u});
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "RSID loads");
    require(rt.runInit(1, 2048), "strict RSID init succeeds via PHI2");
    rt.enablePhi2Machine(false);

    const C64RunResult r = rt.runRsidMachineCycles(128, 256, nullptr);
    require(r.executedCycles == 0 && r.passiveCycles == 0, "strict direct run refuses without PHI2");
    const auto d = rt.rsidExactnessDowngradeReasons();
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::StrictRsidNotPhi2),
            "direct strict refusal records StrictRsidNotPhi2");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::LegacyCpuPlayback),
            "direct strict refusal does not claim legacy Mos6510 playback occurred");
}

int main() {
    testDirectStrictCycleRunRefusalDoesNotClaimLegacyPlayback();
    std::cout << "C64StrictDirectPhi2RefusalV722Tests PASS\n";
    return 0;
}
