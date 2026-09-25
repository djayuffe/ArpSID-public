// c64_sidplay_cia_multisid_rollback_v872_tests.cpp
//
// v872 SIDPLAY closure, part 2 — behavioral proof for the render-transaction
// rollback on the *CIA-timed* PSID service path and for multi-SID + D418 +
// oscillator-readback state, closing audit items P1-2, P1-3 and P1-9.
//
//   1. A CIA-timed PSID whose IRQ-entered play routine mutates RAM + SID + D418
//      and then KILs (never returns to idle) is serviced under an open render
//      transaction. serviceComplete is false while playAddressEntered is true —
//      exactly the kernel's CIA rollback condition — so the transaction is rolled
//      back and every authoritative surface must return to its pre-frame value.
//
//   2. A two-SID VBI PSID whose failed play writes both $D400 (chip 0) and $D420
//      (chip 1), a $D418 volume write, and reads $D41B (OSC3) is rolled back; the
//      per-chip register banks, D418 state and read-approximation model all restore.

#include "arpsid/core/c64_psid_runtime.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "c64_sidplay_cia_multisid_rollback_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = uint8_t(x >> 8); v[off + 1] = uint8_t(x);
}
void be32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = uint8_t(x >> 24); v[off + 1] = uint8_t(x >> 16);
    v[off + 2] = uint8_t(x >> 8); v[off + 3] = uint8_t(x);
}

// Build a PSID. speedCia selects CIA timing (speed bit0), and secondSidByte (if
// non-zero) declares a second SID chip via the v2NG selector byte at $7A. The
// second-SID selector is a PSID v3 field (v2 leaves $7A reserved), so the header
// is version 3 whenever a second SID is requested — matching the HVSC/libsidplayfp
// spec (v873 parser fix; a v2 header with $7A set is mono, not stereo).
std::vector<uint8_t> buildPsid(uint16_t load, uint16_t init, uint16_t play,
                               const std::vector<uint8_t>& initCode,
                               const std::vector<uint8_t>& playCode,
                               bool speedCia, uint8_t secondSidByte = 0u) {
    const uint16_t off = 0x7Cu;
    std::vector<uint8_t> v(off, 0u);
    v[0] = 'P'; v[1] = 'S'; v[2] = 'I'; v[3] = 'D';
    be16(v, 0x04, secondSidByte ? 3u : 2u); be16(v, 0x06, off); be16(v, 0x08, load);
    be16(v, 0x0A, init); be16(v, 0x0C, play); be16(v, 0x0E, 1); be16(v, 0x10, 1);
    be32(v, 0x12, speedCia ? 1u : 0u);
    v[0x7Au] = secondSidByte;  // 0x42 -> $D420 second SID
    const uint32_t initOffset = init - load;
    const uint32_t playOffset = play - load;
    while (v.size() < off + initOffset) v.push_back(0xEAu);
    v.insert(v.end(), initCode.begin(), initCode.end());
    while (v.size() < off + playOffset) v.push_back(0xEAu);
    v.insert(v.end(), playCode.begin(), playCode.end());
    return v;
}

const std::vector<uint8_t> kSimpleInit = { 0xA9, 0x00, 0x60 };
constexpr uint16_t kRamProbe = 0xC000;

// ─── 1. Forced failed-CIA-service rollback (P1-2) ──────────────────────────────
void testFailedCiaServiceRollback() {
    using namespace ArpSID::C64;

    // IRQ-entered play: STA $C000, STA $D418, STA $D401, then KIL ($02) so the
    // service enters play + writes SID but never returns to idle (serviceComplete
    // stays false, playAddressEntered true — the kernel's CIA rollback trigger).
    const uint16_t load = 0x2000u, init = 0x2000u, play = 0x2030u;
    const std::vector<uint8_t> playCode = {
        0xA9, 0x42, 0x8D, 0x00, 0xC0,   // LDA #$42 / STA $C000
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,   // LDA #$0F / STA $D418
        0xA9, 0x21, 0x8D, 0x01, 0xD4,   // LDA #$21 / STA $D401
        0x02                            // KIL
    };
    auto psid = buildPsid(load, init, play, kSimpleInit, playCode, /*speedCia=*/true);
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "CIA PSID must load");
    require(rt.usesCiaTimingForSong(1), "song must be marked CIA-timed");
    require(rt.runInit(1, 512), "CIA init must complete");
    require(rt.usesCiaTiming(), "runtime must record CIA timing");
    require(rt.phi2MachineReady(), "PHI2 machine must be ready");

    const uint8_t  preRam     = rt.phi2Machine().memory().peekRam(kRamProbe);
    const uint8_t  prePlatRam = rt.platform().peekMemory(kRamProbe);
    const uint8_t  preD418    = rt.sidSink().regs[0x18];
    const uint8_t  preD401    = rt.sidSink().regs[0x01];
    const uint64_t preCycle   = rt.phi2Machine().phi2Cycle();
    const uint64_t preWrites  = rt.sidSink().writeCount;

    auto tx = rt.beginRenderTransaction();
    require(tx.active && tx.platformJournalActive, "render transaction must open with journal");

    const auto service = rt.runPsidCiaPlaybackServiceTicks(262144u, &rt.sidSink());
    require(service.playAddressEntered, "CIA service must enter the play routine");
    require(!service.serviceComplete, "KIL play must leave the CIA service incomplete");

    // The frame really mutated authoritative state before rollback.
    require(rt.phi2Machine().memory().peekRam(kRamProbe) == 0x42, "CIA frame must write RAM before rollback");
    require(rt.sidSink().regs[0x18] == 0x0F, "CIA frame must write D418 before rollback");
    require(rt.sidSink().regs[0x01] == 0x21, "CIA frame must write SID freq before rollback");

    const bool rolledBack = rt.rollbackRenderTransaction(tx);
    require(rolledBack, "CIA service rollback must prove a full restore");

    require(rt.phi2Machine().memory().peekRam(kRamProbe) == preRam, "CIA rollback must restore PHI2 RAM");
    require(rt.phi2Machine().phi2Cycle() == preCycle, "CIA rollback must restore the PHI2 cycle counter");
    require(rt.sidSink().regs[0x18] == preD418, "CIA rollback must restore D418");
    require(rt.sidSink().regs[0x01] == preD401, "CIA rollback must restore SID freq register");
    require(rt.sidSink().writeCount == preWrites, "CIA rollback must restore the sink write count");
    require(rt.platform().peekMemory(kRamProbe) == prePlatRam, "CIA rollback must restore the platform mirror");
}

// ─── 2. Multi-SID + D418 + OSC3 readback rollback (P1-3 / P1-9) ─────────────────
void testMultiSidAndReadbackRollback() {
    using namespace ArpSID::C64;

    // Second SID at $D420 (selector 0x42). Failed VBI play writes chip 0 ($D401),
    // chip 1 ($D420), D418 ($D418), reads OSC3 ($D41B) to move the readback model,
    // then loops forever -> budget failure.
    const uint16_t load = 0x0800u, init = 0x0800u, play = 0x0810u;
    const uint16_t self = static_cast<uint16_t>(play + 18u);
    const std::vector<uint8_t> playCode = {
        0xA9, 0x21, 0x8D, 0x01, 0xD4,   // 0:  LDA #$21 / STA $D401  (chip 0)
        0xA9, 0x33, 0x8D, 0x20, 0xD4,   // 5:  LDA #$33 / STA $D420  (chip 1)
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,   // 10: LDA #$0F / STA $D418  (volume)
        0xAD, 0x1B, 0xD4,               // 15: LDA $D41B            (OSC3 read)
        0x4C, uint8_t(self & 0xFF), uint8_t(self >> 8)  // 18: JMP self
    };
    auto psid = buildPsid(load, init, play, kSimpleInit, playCode,
                          /*speedCia=*/false, /*secondSidByte=*/0x42u);
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "two-SID PSID must load");
    require(rt.image().header.sidChipCount >= 2u, "runtime must see two SID chips");
    require(rt.runInit(), "two-SID init must complete");
    require(rt.phi2MachineReady(), "PHI2 machine must be ready");

    const uint8_t  preChip0     = rt.sidSink().regsByChip[0][0x01];
    const uint8_t  preChip1     = rt.sidSink().regsByChip[1][0x00];
    const uint8_t  preD418      = rt.sidSink().regsByChip[0][0x18];
    const uint64_t preReadApprox = rt.sidSink().sidReadApproximationCount;
    const uint64_t preWrites    = rt.sidSink().writeCount;

    auto tx = rt.beginRenderTransaction();
    require(tx.active, "render transaction must open");

    require(!rt.runPlay(64u), "forced-failure two-SID play must report incomplete");

    require(rt.sidSink().regsByChip[0][0x01] == 0x21, "chip 0 must be written before rollback");
    require(rt.sidSink().regsByChip[1][0x00] == 0x33, "chip 1 must be written before rollback");
    require(rt.sidSink().regsByChip[0][0x18] == 0x0F, "D418 must be written before rollback");
    require(rt.sidSink().writeCount > preWrites, "writes must be recorded before rollback");

    require(rt.rollbackRenderTransaction(tx), "two-SID rollback must prove a full restore");

    require(rt.sidSink().regsByChip[0][0x01] == preChip0, "rollback must restore chip 0 register bank");
    require(rt.sidSink().regsByChip[1][0x00] == preChip1, "rollback must restore chip 1 register bank");
    require(rt.sidSink().regsByChip[0][0x18] == preD418, "rollback must restore D418 register bank");
    require(rt.sidSink().sidReadApproximationCount == preReadApprox,
            "rollback must restore the OSC3/read-approximation model");
    require(rt.sidSink().writeCount == preWrites, "rollback must restore the sink write count");
}

} // namespace

int main() {
    testFailedCiaServiceRollback();
    testMultiSidAndReadbackRollback();
    std::cout << "c64_sidplay_cia_multisid_rollback_v872_tests PASS\n";
    return 0;
}
