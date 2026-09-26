// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
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
    C64Runtime rt; rt.reset(true);
    auto rsid = buildRsid(0x0800u, {0x60u});
    require(rt.loadPsid(rsid.data(), rsid.size()), "RSID loads");
    require(rt.runInit(1, 2048), "RSID init succeeds");
    rt.enablePhi2Machine(true);

    Phi2BusPhase ph{};
    const uint8_t v = rt.phi2Machine().memory().cpuRead(rt.phi2Machine().phi2Cycle(), 0xDEADu, ph);
    (void)v;
    require(ph.openBusSource, "$DEAD read is open-bus source in no-cartridge C64 map");
    const auto d = rt.rsidExactnessDowngradeReasons();
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::OpenBusApprox),
            "open-bus observation is propagated to exactness ledger");
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::MissingRealRoms),
            "fallback HLE ROM use is still visible in exactness ledger");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::CiaModelApprox),
            "cycle-exact CIA no longer adds an approximation downgrade");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::VicBusStealApprox),
            "cycle-table VIC arbitration no longer adds an approximation downgrade");

    std::cout << "C64ExactnessLedgerV720Tests PASS\n";
    return 0;
}
