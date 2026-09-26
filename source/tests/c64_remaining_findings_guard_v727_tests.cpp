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

static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = static_cast<uint8_t>(v >> 8u);
    b[off + 1] = static_cast<uint8_t>(v & 0xFFu);
}
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = static_cast<uint8_t>(v >> 24u);
    b[off + 1] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    b[off + 2] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
    b[off + 3] = static_cast<uint8_t>(v & 0xFFu);
}

static std::vector<uint8_t> buildRsid(uint16_t loadAddr, const std::vector<uint8_t>& initCode) {
    std::vector<uint8_t> v(0x7Cu, 0u);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v, 0x04u, 2u); be16(v, 0x06u, 0x7Cu);
    be16(v, 0x08u, 0u); be16(v, 0x0Au, loadAddr); be16(v, 0x0Cu, 0u);
    be16(v, 0x0Eu, 1u); be16(v, 0x10u, 1u); be32(v, 0x12u, 0u);
    v[0x76]=0; v[0x77]=0x04;
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

static std::vector<uint8_t> buildCiaTimedPsid() {
    constexpr uint16_t dataOff = 0x7Cu;
    std::vector<uint8_t> b(dataOff, 0u);
    b[0]='P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b, 0x04u, 2u); be16(b, 0x06u, dataOff);
    be16(b, 0x08u, 0u); be16(b, 0x0Au, 0x0800u); be16(b, 0x0Cu, 0x0830u);
    be16(b, 0x0Eu, 1u); be16(b, 0x10u, 1u); be32(b, 0x12u, 0x00000001u);
    b.push_back(0x00u); b.push_back(0x08u);
    const uint8_t init[] = { 0xA9u, 0x40u, 0x8Du, 0x00u, 0x20u, 0x60u };
    b.insert(b.end(), init, init + sizeof(init));
    while (b.size() < dataOff + 2u + 0x30u) b.push_back(0xEAu);
    const uint8_t play[] = { 0xEEu, 0x00u, 0x20u, 0xADu, 0x00u, 0x20u, 0x8Du, 0x00u, 0xD4u, 0x60u };
    b.insert(b.end(), play, play + sizeof(play));
    return b;
}

int main() {
    {
        C64Runtime rt;
        rt.reset(true);
        rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
        const auto rsid = buildRsid(0x0800u, {0x60u});
        require(rt.loadPsid(rsid.data(), rsid.size()), "RSID loads");
        require(rt.runInit(1, 4096), "strict RSID PHI2 init succeeds");
        rt.enablePhi2Machine(true);
        require(rt.rsidStrictPhi2PathActive(), "strict PHI2 path is active");
        require(rt.rsidStrictStatusCode() == 2u, "strict PHI2 with known approximation reports downgraded status");

        (void)rt.sidSink().sidRead(0x1Bu, 1234u);
        const auto d = rt.rsidExactnessDowngradeReasons();
        require(!rsidDowngradeHas(d, RsidExactnessDowngrade::SidReadApprox), "SID OSC3/ENV3 cycle readback is not an approximation");
        require(rsidDowngradeHas(d, RsidExactnessDowngrade::MissingRealRoms), "HLE/missing ROM downgrade remains ledger-visible");
        require(!rsidDowngradeHas(d, RsidExactnessDowngrade::CiaModelApprox), "CIA cycle model is not downgraded");
        require(!rt.rsidPhysicallyExact(), "downgraded strict PHI2 path is not physically exact");
    }

    {
        C64Runtime rt;
        rt.reset(true);
        const auto psid = buildCiaTimedPsid();
        require(rt.loadPsid(psid.data(), psid.size()), "CIA-timed PSID loads");
        require(rt.runInit(1, 512), "CIA-timed PSID init succeeds");
        require(rt.usesCiaTiming(), "PSID CIA timing detected");
        require(rt.psidCiaLegacyServiceCount() == 0u, "PSID-CIA compatibility service counter starts at zero");
        require(rt.psidCiaCompatibilityServiceObservedCount() == 0u, "PSID-CIA aggregate counter starts at zero");
        require(rt.psidCiaRunPlayCompatibilityCount() == 0u, "PSID-CIA runPlay counter starts at zero");
        require(rt.psidCiaExplicitServiceCompatibilityCount() == 0u, "PSID-CIA explicit-service counter starts at zero");
        const auto snap = rt.runPsidCiaPlaybackServiceTicks(262144u);
        require(snap.playAddressEntered, "PSID-CIA service enters play routine");
        require(rt.psidCiaPhysicalPhi2ServiceActive(), "PSID-CIA service is now PHI2-machine active when mirrored state is available");
        require(rt.psidCiaPhi2ServiceCount() == 1u, "PSID-CIA PHI2 service counter tracks explicit service");
        require(!rt.psidCiaCompatibilityServiceUsed(), "PHI2 PSID-CIA service does not mark legacy compatibility use");
        require(rt.psidCiaCompatibilityServiceObservedCount() == 0u, "PSID-CIA aggregate compatibility counter remains zero on PHI2 success");
        require(rt.psidCiaExplicitServiceCompatibilityCount() == 0u, "PHI2 explicit service does not pollute compatibility explicit counter");
        require(rt.psidCiaRunPlayCompatibilityCount() == 0u, "PHI2 explicit service does not pollute runPlay compatibility counter");

        const auto blockers = rt.physicalExactnessBlockers();
        require(c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::MissingRealRoms), "physical blocker exposes missing real ROMs");
        require(!c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::Cia6526NotCycleExact), "cycle-exact CIA clears its capability blocker");
        require(!c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::VicIINotCycleExact), "cycle-table VIC-II clears its capability blocker");
        require(!c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::SidReadbackModelApprox), "cycle-clocked SID readback clears its capability blocker");
        require(c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::OpenBusModelApprox), "physical blocker exposes deterministic open-bus model approximation as capability");
        require(!c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::PsidCiaCompatibility), "PHI2 PSID-CIA service does not expose compatibility-service blocker");
        require(c64PhysicalBlockerHas(blockers, C64PhysicalExactnessBlocker::LegacyMos6510PathPresent)
                    == rt.legacyMos6510RuntimePathsReachable(),
                "legacy-path blocker exactly follows the compile-time reachability policy");
        require(!c64PhysicalBlockerHas(rt.totalPhysicalRiskBlockers(), C64PhysicalExactnessBlocker::ObservedDowngradeLedger), "non-RSID PSID-CIA PHI2 service does not create observed RSID downgrade ledger bit");
        require(rt.hasAnyPhysicalExactnessRisk(), "PSID-CIA PHI2 run still has ROM/open-bus/legacy-path physical exactness risk");
    }

    std::cout << "C64RemainingFindingsGuardV727Tests PASS\n";
    return 0;
}
