// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::vector<uint8_t> buildRsid(uint16_t loadAddr, const std::vector<uint8_t>& initCode) {
    std::vector<uint8_t> v(0x7Cu, 0u);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    v[4]=0; v[5]=2; v[6]=0; v[7]=0x7Cu;
    v[10]=static_cast<uint8_t>(loadAddr >> 8u);
    v[11]=static_cast<uint8_t>(loadAddr & 0xFFu);
    v[14]=0; v[15]=1; v[16]=0; v[17]=1;
    v[0x76]=0; v[0x77]=0x04;
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

int main() {
    C64Runtime rt;
    rt.reset(true);
    require(rt.rsidStrictStatusCode() == 0u, "unloaded runtime has no strict status");
    require(!rt.rsidStrictPhi2PathActive(), "unloaded runtime has no strict PHI2 path");

    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    const auto rsid = buildRsid(0x0800u, {0x60u}); // RTS init via stack guard
    require(rt.loadPsid(rsid.data(), rsid.size()), "RSID loads");
    require(rt.runInit(1, 4096), "strict PHI2 init succeeds");
    rt.enablePhi2Machine(true);

    require(rt.rsidStrictPhi2PathActive(), "strict PHI2 path is active");
    require(rt.rsidStrictStatusCode() == 2u,
            "strict PHI2 path with fallback HLE ROMs reports status 2, not status 1");
    require(!rt.rsidKnownDowngradeFree(), "known downgrade free is false");
    require(!rt.rsidPhysicallyExact(), "physically exact remains false with ledger downgrades");
    require(!rt.rsidExactPlaybackActive(), "legacy spelling remains physically-exact only");

    const auto d = rt.rsidExactnessDowngradeReasons();
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::MissingRealRoms), "HLE ROM downgrade present");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::CiaModelApprox), "CIA exact core adds no model downgrade");

    std::cout << "C64RsidStatusCodeV726Tests PASS\n";
    return 0;
}
