// Copyright (C) 2024-2026 Ulf Bertilsson
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

static std::vector<uint8_t> buildPsid(uint16_t load,
                                      uint16_t init,
                                      uint16_t play,
                                      const std::vector<uint8_t>& initCode,
                                      const std::vector<uint8_t>& playCode,
                                      bool ciaTiming = false) {
    const uint16_t off = 0x7Cu;
    std::vector<uint8_t> v(off, 0u);
    v[0]='P'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v, 0x04, 2); be16(v, 0x06, off); be16(v, 0x08, load);
    be16(v, 0x0A, init); be16(v, 0x0C, play); be16(v, 0x0E, 1); be16(v, 0x10, 1);
    be32(v, 0x12, ciaTiming ? 1u : 0u);
    const uint32_t initOffset = init - load;
    const uint32_t playOffset = play ? (play - load) : initOffset + static_cast<uint32_t>(initCode.size());
    while (v.size() < off + initOffset) v.push_back(0xEAu);
    v.insert(v.end(), initCode.begin(), initCode.end());
    while (v.size() < off + playOffset) v.push_back(0xEAu);
    v.insert(v.end(), playCode.begin(), playCode.end());
    return v;
}

static std::vector<uint8_t> buildRsid(uint16_t load,
                                      const std::vector<uint8_t>& initCode,
                                      bool basicFlag = false) {
    const uint16_t off = 0x7Cu;
    std::vector<uint8_t> v(off, 0u);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v, 0x04, 2); be16(v, 0x06, off); be16(v, 0x08, 0);
    be16(v, 0x0A, load); be16(v, 0x0C, 0); be16(v, 0x0E, 1); be16(v, 0x10, 1);
    be32(v, 0x12, 0);
    uint16_t flags = 0x0004u; // PAL
    if (basicFlag) flags |= 0x0002u;
    be16(v, 0x76, flags);
    v.push_back(static_cast<uint8_t>(load & 0xFFu));
    v.push_back(static_cast<uint8_t>(load >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

static void testPsidRtsInitAndPlayBootsThroughHleVectors() {
    const std::vector<uint8_t> init = {
        0xA9,0x11,0x8D,0x00,0xD4, // LDA #$11; STA $D400
        0x60                      // RTS to bootstrap
    };
    const std::vector<uint8_t> play = {
        0xA9,0x22,0x8D,0x01,0xD4, // LDA #$22; STA $D401
        0x60                      // RTS to play bootstrap
    };
    auto psid = buildPsid(0x1000u, 0x1000u, 0x1020u, init, play);
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "PSID loads");
    require(rt.runInit(1, 256), "PSID RTS init completes through reset-vector bootstrap");
    require(rt.platform().bootState().sidInitCompleted, "PSID boot state says init completed");
    require(rt.sidSink().regs[0] == 0x11u, "PSID init SID write reached $D400");
    const auto vectors = rt.validateInstalledInterruptBootstrap();
    require(vectors.cpuIrqVector == 0xFF48u, "HLE IRQ vector points to $FF48 trampoline");
    require(vectors.cpuNmiVector == 0xFE43u, "HLE NMI vector points to $FE43 trampoline");
    require(rt.platform().peekMemory(0xEA31u) == 0xADu &&
            rt.platform().peekMemory(0xEA32u) == 0x0Du &&
            rt.platform().peekMemory(0xEA33u) == 0xDCu,
            "HLE default IRQ ACK reads $DC0D");
    require(rt.runPlay(256), "PSID RTS play completes through play bootstrap");
    require(rt.platform().bootState().sidPlayCompleted, "PSID boot state says play completed");
    require(rt.sidSink().regs[1] == 0x22u, "PSID play SID write reached $D401");
}

static void testPsidKilInitDoesNotMasqueradeAsBootSuccess() {
    const std::vector<uint8_t> init = {
        0xA9,0x33,0x8D,0x00,0xD4, // visible write before fatal opcode
        0x02                      // KIL/JAM, never a valid boot completion sentinel
    };
    auto psid = buildPsid(0x1200u, 0x1200u, 0x1230u, init, {0x60});
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "KIL-init PSID loads");
    require(!rt.runInit(1, 256), "KIL/JAM during PSID init fails, not success-by-jam");
    require(!rt.platform().bootState().sidInitCompleted, "KIL-init boot state is not completed");
    require(!rt.platform().bootState().sidPlayReady, "KIL-init does not make play ready");
}

static void testPsidKilPlayDoesNotMasqueradeAsFrameSuccess() {
    const std::vector<uint8_t> init = {0x60};
    const std::vector<uint8_t> play = {
        0xA9,0x44,0x8D,0x02,0xD4, // visible write before fatal opcode
        0x02                      // KIL/JAM, not a valid frame completion sentinel
    };
    auto psid = buildPsid(0x1400u, 0x1400u, 0x1420u, init, play);
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "KIL-play PSID loads");
    require(rt.runInit(1, 256), "KIL-play PSID init succeeds");
    require(!rt.runPlay(256), "KIL/JAM during PSID play fails, not success-by-jam");
    require(!rt.platform().bootState().sidPlayCompleted, "KIL-play boot state is not completed");
}

static void testPsidBrkCompatibilitySentinelStillWorks() {
    const std::vector<uint8_t> init = {
        0xA9,0x55,0x8D,0x00,0xD4,
        0x00                      // BRK-as-halt compatibility sentinel
    };
    const std::vector<uint8_t> play = {
        0xA9,0x66,0x8D,0x01,0xD4,
        0x00                      // BRK-as-halt compatibility sentinel
    };
    auto psid = buildPsid(0x1600u, 0x1600u, 0x1620u, init, play);
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "BRK-sentinel PSID loads");
    require(rt.runInit(1, 256), "BRK-sentinel PSID init remains compatibility-success");
    require(rt.sidSink().regs[0] == 0x55u, "BRK-sentinel init write visible");
    require(rt.runPlay(256), "BRK-sentinel PSID play remains compatibility-success");
    require(rt.sidSink().regs[1] == 0x66u, "BRK-sentinel play write visible");
}

static void testStrictRsidRtsInitBootsAndMachineRunAdvances() {
    auto rsid = buildRsid(0x1800u, {
        0xA9,0x77,0x8D,0x04,0xD4, // LDA #$77; STA $D404
        0x60                      // RTS to RSID bootstrap idle loop
    });
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "strict RSID loads");
    require(rt.runInit(1, 4096), "strict RSID RTS init completes through PHI2 reset/bootstrap");
    require(rt.phi2InitUsed(), "strict RSID init used PHI2 machine");
    require(rt.sidSink().regs[4] == 0x77u, "strict RSID init SID write visible");
    rt.enablePhi2Machine(true);
    const uint64_t before = rt.phi2Machine().phi2Cycle();
    require(rt.runPlay(4096), "strict RSID machine-mode runPlay advances PHI2 runtime");
    require(rt.phi2Machine().phi2Cycle() > before, "strict RSID PHI2 cycle advances after runPlay");
    require(!rsidDowngradeHas(rt.rsidExactnessDowngradeReasons(), RsidExactnessDowngrade::LegacyCpuPlayback),
            "strict RSID boot/run did not use legacy playback");
}

static void testStrictRsidKilInitFailsWithoutLegacyFallback() {
    auto rsid = buildRsid(0x1A00u, {0x02});
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "strict KIL RSID loads");
    require(!rt.runInit(1, 512), "strict RSID KIL init fails instead of compatibility fallback");
    require(!rt.phi2InitUsed(), "strict KIL RSID did not complete PHI2 init");
    require(rt.rsidLegacyInitFallbackCount() == 0u, "strict KIL RSID did not legacy-fallback");
    require(rsidDowngradeHas(rt.rsidExactnessDowngradeReasons(), RsidExactnessDowngrade::StrictRsidNotPhi2),
            "strict KIL RSID records strict-not-PHI2 downgrade/refusal");
}

int main() {
    testPsidRtsInitAndPlayBootsThroughHleVectors();
    testPsidKilInitDoesNotMasqueradeAsBootSuccess();
    testPsidKilPlayDoesNotMasqueradeAsFrameSuccess();
    testPsidBrkCompatibilitySentinelStillWorks();
    testStrictRsidRtsInitBootsAndMachineRunAdvances();
    testStrictRsidKilInitFailsWithoutLegacyFallback();
    std::puts("C64PsidRsidBootContractV739Tests PASS");
    return 0;
}
