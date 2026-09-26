// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_psid_cia_latch_reuse_v872_tests.cpp
//
// v872 regression — CIA Timer-A latch must not leak across a reused C64Runtime.
//
// loadPsid()'s contract is that a runtime can be reused for a new tune without an
// intervening reset(). coldBootForSidLoad() previously did not reset the CIAs, so a
// reused runtime still held the prior tune's CIA1 Timer-A latch immediately after
// loadPsid(). installPsidCiaPlaybackBootstrap() reads latchA()!=0xFFFF as "the tune
// programmed its own tempo", so that stale latch is a real per-tune correctness gap.
//
// End to end the symptom is masked: runInit()'s reset-vector execution reprograms
// the CIA before the bootstrap heuristic runs, so a fully-initialised reused tune B
// already got the correct default latch. This test pins BOTH properties:
//   (1) the transient window is closed — right after a reused loadPsid() the latch
//       is the 0xFFFF sentinel, not the prior tune's value (the cold-boot hardening);
//   (2) end to end, a reused tune B that never programs the CIA gets exactly the
//       same default latch as a fresh load — never the prior tune's tempo.

#include "arpsid/core/c64_psid_runtime.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "c64_psid_cia_latch_reuse_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off] = uint8_t(x >> 8); v[off + 1] = uint8_t(x); }
void be32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = uint8_t(x >> 24); v[off + 1] = uint8_t(x >> 16); v[off + 2] = uint8_t(x >> 8); v[off + 3] = uint8_t(x);
}

// CIA-timed PSID (speed bit set).
std::vector<uint8_t> buildPsidCia(uint16_t load, uint16_t init, uint16_t play,
                                  const std::vector<uint8_t>& initCode,
                                  const std::vector<uint8_t>& playCode) {
    const uint16_t off = 0x7Cu;
    std::vector<uint8_t> v(off, 0u);
    v[0] = 'P'; v[1] = 'S'; v[2] = 'I'; v[3] = 'D';
    be16(v, 0x04, 2); be16(v, 0x06, off); be16(v, 0x08, load);
    be16(v, 0x0A, init); be16(v, 0x0C, play); be16(v, 0x0E, 1); be16(v, 0x10, 1);
    be32(v, 0x12, 1u); // CIA timing
    const uint32_t initOffset = init - load, playOffset = play - load;
    while (v.size() < off + initOffset) v.push_back(0xEAu);
    v.insert(v.end(), initCode.begin(), initCode.end());
    while (v.size() < off + playOffset) v.push_back(0xEAu);
    v.insert(v.end(), playCode.begin(), playCode.end());
    return v;
}

constexpr uint16_t kLoad = 0x1000u, kInit = 0x1000u, kPlay = 0x1030u;

// Init that programs CIA1 Timer A latch = $1234 (STA $DC04 lo, STA $DC05 hi).
std::vector<uint8_t> initProgramsCiaTimerA() {
    return { 0xA9, 0x34, 0x8D, 0x04, 0xDC,   // LDA #$34 / STA $DC04
             0xA9, 0x12, 0x8D, 0x05, 0xDC,   // LDA #$12 / STA $DC05
             0x60 };                         // RTS
}
// Init that never touches the CIA.
std::vector<uint8_t> initLeavesCiaAlone() { return { 0xA9, 0x00, 0x60 }; }
// Minimal play (writes a SID reg then RTS).
std::vector<uint8_t> simplePlay() { return { 0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60 }; }

void testCiaLatchDoesNotLeakAcrossReusedLoad() {
    auto tuneA = buildPsidCia(kLoad, kInit, kPlay, initProgramsCiaTimerA(), simplePlay());
    auto tuneB = buildPsidCia(kLoad, kInit, kPlay, initLeavesCiaAlone(), simplePlay());

    // Fresh runtime, tune A: it programs its own Timer-A latch.
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(tuneA.data(), tuneA.size()), "tune A must load");
    require(rt.runInit(1), "tune A init must complete");
    require(rt.platform().cia1().latchA() == 0x1234u,
            "tune A init must program CIA1 Timer-A latch to $1234");

    // Reuse the SAME runtime for tune B WITHOUT reset(). B never touches the CIA.
    require(rt.loadPsid(tuneB.data(), tuneB.size()), "tune B must load on a reused runtime");
    // (1) Transient window: the cold boot must have cleared tune A's stale latch to
    // the 0xFFFF sentinel immediately, before B's init runs.
    require(rt.platform().cia1().latchA() != 0x1234u,
            "reused loadPsid() must clear the prior tune's CIA latch (cold-boot hardening)");
    require(rt.platform().cia1().latchA() == 0xFFFFu,
            "reused loadPsid() must leave the CIA Timer-A latch at the 0xFFFF power-on sentinel");
    require(rt.runInit(1), "tune B init must complete");
    const uint16_t reusedLatch = rt.platform().cia1().latchA();
    require(reusedLatch != 0x1234u,
            "reused runtime must NOT inherit tune A's CIA Timer-A latch (leak fixed)");

    // A fresh runtime loading tune B is the ground truth for the correct default.
    C64Runtime fresh; fresh.reset(true);
    require(fresh.loadPsid(tuneB.data(), tuneB.size()), "fresh tune B must load");
    require(fresh.runInit(1), "fresh tune B init must complete");
    const uint16_t freshLatch = fresh.platform().cia1().latchA();

    require(reusedLatch == freshLatch,
            "reused tune B must get the same default CIA latch as a fresh tune B load");
}

// v872 P0-2 — a tune that programs its own CIA tempo must report ciaLatchCorrect,
// not be falsely flagged dirty for differing from the VBI default latch.
void testCustomTempoLatchReportsCorrect() {
    auto tune = buildPsidCia(kLoad, kInit, kPlay, initProgramsCiaTimerA(), simplePlay());
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(tune.data(), tune.size()), "custom-tempo tune must load");
    require(rt.runInit(1), "custom-tempo tune init must complete");
    require(rt.platform().cia1().latchA() == 0x1234u, "tune must have installed its own $1234 latch");

    const auto snap = rt.runPsidCiaPlaybackServiceTicks(262144u);
    require(snap.installed, "PSID-CIA bootstrap must be installed");
    require(snap.ciaLatchCorrect,
            "a tune-programmed CIA latch must validate as correct (not measured against the default)");
}

// v872 P0-1 — an intentional Timer-A latch of $FFFF (max period) must be preserved,
// not treated as the reset sentinel and overwritten with the default.
void testIntentionalMaxPeriodLatchPreserved() {
    // init: LDA #$FF / STA $DC04 / STA $DC05 / RTS  → Timer-A latch = $FFFF.
    const std::vector<uint8_t> init = { 0xA9, 0xFF, 0x8D, 0x04, 0xDC, 0x8D, 0x05, 0xDC, 0x60 };
    auto tune = buildPsidCia(kLoad, kInit, kPlay, init, simplePlay());
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(tune.data(), tune.size()), "max-period tune must load");
    require(rt.runInit(1), "max-period tune init must complete");
    require(rt.platform().cia1().latchA() == 0xFFFFu,
            "an intentionally tune-programmed $FFFF Timer-A latch must be preserved (P0-1)");
}

// v872 P0-2 — a tune-authored Timer-A control mode ($DC0E) must be preserved, not
// collapsed to the default continuous mode.
void testCustomTimerAControlModePreserved() {
    // init: program latch $1234 and CRA $09 (start + one-shot).
    const std::vector<uint8_t> init = {
        0xA9, 0x34, 0x8D, 0x04, 0xDC,   // STA $DC04 = $34
        0xA9, 0x12, 0x8D, 0x05, 0xDC,   // STA $DC05 = $12
        0xA9, 0x09, 0x8D, 0x0E, 0xDC,   // STA $DC0E = $09 (start + one-shot)
        0x60
    };
    auto tune = buildPsidCia(kLoad, kInit, kPlay, init, simplePlay());
    C64Runtime rt; rt.reset(true);
    require(rt.loadPsid(tune.data(), tune.size()), "custom-CRA tune must load");
    require(rt.runInit(1), "custom-CRA tune init must complete");
    require(rt.platform().cia1().latchA() == 0x1234u, "custom-CRA tune latch must be preserved");
    const uint8_t cra = rt.platform().cia1().peekControlA();
    require((cra & 0x08u) != 0u,
            "a tune-authored one-shot Timer-A control bit must survive the bootstrap (P0-2)");
    require((cra & 0x01u) != 0u, "the timer must still be started for playback");
}

} // namespace

int main() {
    testCiaLatchDoesNotLeakAcrossReusedLoad();
    testCustomTempoLatchReportsCorrect();
    testIntentionalMaxPeriodLatchPreserved();
    testCustomTimerAControlModePreserved();
    std::cout << "c64_psid_cia_latch_reuse_v872_tests PASS\n";
    return 0;
}
