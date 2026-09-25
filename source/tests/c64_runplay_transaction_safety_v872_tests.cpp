// c64_runplay_transaction_safety_v872_tests.cpp
//
// v872 P1-1 regression — direct C64Runtime::runPlay() must be transaction-safe.
//
// A failed/budgeted VBI play mutates the PHI2 machine and SID sink (e.g. it writes
// $D418 and then budgets out in an infinite loop). The AU render path wraps runPlay
// in its own render transaction, but a DIRECT caller (or a test) previously got
// contaminated SID state after a failed play. runPlay() now owns a transaction when
// none is active and rolls back on failure, so the public API leaves no partial
// state behind.

#include "arpsid/core/c64_psid_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "c64_runplay_transaction_safety_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off] = uint8_t(v >> 8); b[off + 1] = uint8_t(v); }
void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24); b[off + 1] = uint8_t(v >> 16); b[off + 2] = uint8_t(v >> 8); b[off + 3] = uint8_t(v);
}

std::vector<uint8_t> buildVbiPsid(uint16_t load, uint16_t init, uint16_t play,
                                  const std::vector<uint8_t>& initCode,
                                  const std::vector<uint8_t>& playCode) {
    const uint16_t off = 0x7Cu;
    std::vector<uint8_t> b(off, 0u);
    b[0]='P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b, 4, 2); be16(b, 6, off); be16(b, 8, 0);
    be16(b, 0x0A, init); be16(b, 0x0C, play); be16(b, 0x0E, 1); be16(b, 0x10, 1);
    be32(b, 0x12, 0u); // VBI
    b.push_back(uint8_t(load)); b.push_back(uint8_t(load >> 8));
    const size_t base = b.size();
    const uint16_t hi = static_cast<uint16_t>(std::max<int>(int(init) + int(initCode.size()), int(play) + int(playCode.size())));
    b.resize(base + size_t(hi - load), 0xEAu);
    for (size_t i = 0; i < initCode.size(); ++i) b[base + size_t(init - load) + i] = initCode[i];
    for (size_t i = 0; i < playCode.size(); ++i) b[base + size_t(play - load) + i] = playCode[i];
    return b;
}

void testDirectRunPlayFailureDoesNotContaminateSink() {
    const uint16_t load = 0x0900u, init = 0x0900u, play = 0x0910u;
    // play: LDA #$0F / STA $D418 / JMP self  → writes D418 then never returns.
    const uint16_t self = static_cast<uint16_t>(play + 5u);
    const std::vector<uint8_t> playCode = {
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,
        0x4C, uint8_t(self & 0xFF), uint8_t(self >> 8)
    };
    auto psid = buildVbiPsid(load, init, play, { 0xA9, 0x00, 0x60 }, playCode);

    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "PSID must load");
    require(rt.runInit(), "init must complete");
    require(rt.phi2MachineReady(), "PHI2 machine must be ready");

    const uint8_t  preD418 = rt.sidSink().regsByChip[0][0x18];
    const uint64_t preWrites = rt.sidSink().writeCount;
    const uint64_t preCycle = rt.phi2Machine().phi2Cycle();

    // Tiny budget → the infinite loop cannot complete → runPlay returns false.
    const bool ok = rt.runPlay(8u);
    require(!ok, "budgeted play must report failure");

    // The public API must have rolled its own transaction back: no leaked D418
    // write, write count, or advanced PHI2 cycle.
    require(rt.sidSink().regsByChip[0][0x18] == preD418,
            "failed direct runPlay() must not leak the D418 write into the sink");
    require(rt.sidSink().writeCount == preWrites,
            "failed direct runPlay() must not leak the sink write count");
    require(rt.phi2Machine().phi2Cycle() == preCycle,
            "failed direct runPlay() must restore the PHI2 cycle counter");
}

void testSuccessfulRunPlayStillCommits() {
    const uint16_t load = 0x0900u, init = 0x0900u, play = 0x0910u;
    // play: LDA #$21 / STA $D401 / RTS  → completes.
    const std::vector<uint8_t> playCode = { 0xA9, 0x21, 0x8D, 0x01, 0xD4, 0x60 };
    auto psid = buildVbiPsid(load, init, play, { 0xA9, 0x00, 0x60 }, playCode);

    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "PSID must load");
    require(rt.runInit(), "init must complete");
    require(rt.runPlay(4096u), "a completing play must succeed");
    require(rt.sidSink().regsByChip[0][0x01] == 0x21,
            "a committed play must keep its SID write (transaction committed, not rolled back)");
}

} // namespace

int main() {
    testDirectRunPlayFailureDoesNotContaminateSink();
    testSuccessfulRunPlayStillCommits();
    std::cout << "c64_runplay_transaction_safety_v872_tests PASS\n";
    return 0;
}
