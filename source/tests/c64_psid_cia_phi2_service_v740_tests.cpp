#include "arpsid/core/c64_psid_runtime.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}
static void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = static_cast<uint8_t>(x >> 8u);
    v[off + 1] = static_cast<uint8_t>(x & 0xFFu);
}
static void be32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = static_cast<uint8_t>(x >> 24u);
    v[off + 1] = static_cast<uint8_t>((x >> 16u) & 0xFFu);
    v[off + 2] = static_cast<uint8_t>((x >> 8u) & 0xFFu);
    v[off + 3] = static_cast<uint8_t>(x & 0xFFu);
}

static std::vector<uint8_t> buildPsidCia(uint16_t load, uint16_t init, uint16_t play,
                                         const std::vector<uint8_t>& initCode,
                                         const std::vector<uint8_t>& playCode) {
    const uint16_t off = 0x7Cu;
    std::vector<uint8_t> v(off, 0u);
    v[0]='P'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v, 0x04, 2); be16(v, 0x06, off); be16(v, 0x08, load);
    be16(v, 0x0A, init); be16(v, 0x0C, play); be16(v, 0x0E, 1); be16(v, 0x10, 1);
    be32(v, 0x12, 1u); // song 1 uses CIA timing
    const uint32_t initOffset = init - load;
    const uint32_t playOffset = play - load;
    while (v.size() < off + initOffset) v.push_back(0xEAu);
    v.insert(v.end(), initCode.begin(), initCode.end());
    while (v.size() < off + playOffset) v.push_back(0xEAu);
    v.insert(v.end(), playCode.begin(), playCode.end());
    return v;
}

static void testPsidCiaRunPlayUsesPhi2MachineService() {
    auto psid = buildPsidCia(0x2000u, 0x2000u, 0x2030u,
        { 0xA9,0x10,0x8D,0x00,0xD4, 0x60 },
        { 0xA9,0x7Bu,0x8D,0x06,0xD4, 0x60 });
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "PSID-CIA image loads");
    require(rt.usesCiaTimingForSong(1), "PSID speed word marks CIA timing");
    require(rt.runInit(1, 512), "PSID-CIA init completes");
    require(rt.usesCiaTiming(), "runtime boot state records CIA timing");
    require(rt.phi2Machine().cia1().latchA() == rt.platform().cia1().latchA(),
            "PHI2 CIA1 latch is mirrored from PSID-CIA bootstrap");
    const uint32_t compatBefore = rt.psidCiaCompatibilityServiceObservedCount();
    const uint32_t phi2Before = rt.psidCiaPhi2ServiceCount();
    const uint64_t cycleBefore = rt.phi2Machine().phi2Cycle();
    const auto preSnap = rt.runPsidCiaPhi2PlaybackServiceTicks(262144);
    require(preSnap.serviceComplete, "explicit PSID-CIA PHI2 service completes full IRQ/play/RTI/idle path");
    require(preSnap.playAddressEntered, "PHI2 service enters play routine");
    require(preSnap.ciaAckObserved, "PHI2 service observes CIA1 ACK before completion");
    require(preSnap.returnedToIdleAfterPlay, "PHI2 service returns through RTI to idle loop");
    require(preSnap.sidWriteObserved, "PHI2 service observes SID write during play");
    require(preSnap.sidWriteAddress == 0xD406u, "PHI2 service records play SID write address");
    require(preSnap.sidWriteValue == 0x7Bu, "PHI2 service records play SID write value");
    require(preSnap.ticksToIrq <= preSnap.ticksToVector && preSnap.ticksToVector <= preSnap.ticksToPlay,
            "PSID-CIA event ordering is IRQ -> vector -> play");
    require(preSnap.ticksToPlay <= preSnap.ticksToIdleAfterPlay,
            "PSID-CIA idle return occurs after play entry");
    require(rt.runPlay(262144), "PSID-CIA runPlay requires full PHI2 service completion, not just play entry");
    require(rt.psidCiaPhi2ServiceCount() > phi2Before, "PSID-CIA PHI2 service counter increments");
    require(rt.psidCiaCompatibilityServiceObservedCount() == compatBefore,
            "PSID-CIA PHI2 success does not use legacy compatibility service");
    require(rt.psidCiaPhysicalPhi2ServiceActive(), "PSID-CIA physical PHI2 service is reported active");
    require(rt.phi2Machine().phi2Cycle() > cycleBefore, "PSID-CIA PHI2 service advances PHI2 cycle");
    require(rt.sidSink().regs[6] == 0x7Bu, "PSID-CIA play SID write reached $D406");
}

static void testPsidCiaKilPlayDoesNotMasqueradeAsServiceSuccess() {
    auto psid = buildPsidCia(0x2400u, 0x2400u, 0x2430u,
        { 0xA9,0x12,0x8D,0x00,0xD4, 0x60 },
        { 0xA9,0x5Au,0x8D,0x06,0xD4, 0x02 });
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "PSID-CIA KIL-play image loads");
    require(rt.runInit(1, 512), "PSID-CIA KIL-play init completes");
    const auto snap = rt.runPsidCiaPhi2PlaybackServiceTicks(262144);
    require(snap.playAddressEntered, "KIL-play service still observes play entry");
    require(snap.sidWriteObserved, "KIL-play service observes pre-KIL SID write");
    require(snap.cpuJammed, "KIL-play service records CPU jam");
    require(!snap.returnedToIdleAfterPlay, "KIL-play service does not return to idle");
    require(!snap.serviceComplete, "KIL-play service is not complete merely because play was entered");
    require(!rt.runPlay(262144), "PSID-CIA KIL play fails runPlay instead of success-by-play-entry");
}

int main() {
    testPsidCiaRunPlayUsesPhi2MachineService();
    testPsidCiaKilPlayDoesNotMasqueradeAsServiceSuccess();
    std::puts("C64PsidCiaPhi2ServiceV740Tests PASS");
    return 0;
}
