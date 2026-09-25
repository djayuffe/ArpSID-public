// c64_sidplay_rollback_behavioral_v872_tests.cpp
//
// v872 SIDPLAY closure — behavioral proof (not source-string) for the C64 render
// transaction and the passive-stepping CPU-suppression fix. The v871 render
// transaction test proved the snapshot/restore *primitives* in isolation and
// asserted the runtime/kernel wiring via source-string search. This test drives
// the real runtime failure paths end to end:
//
//   1. A PSID whose play routine mutates RAM + SID ($D401) + D418 ($D418) and then
//      never returns is executed under an open render transaction, forced to fail
//      (instruction budget), and rolled back. The authoritative PHI2 machine, the
//      runtime SID sink, and the platform mirror must all return to their exact
//      pre-frame values — this is the "next frame starts from the exact pre-frame
//      PHI2 state" guarantee the audit (P1-1/P1-3) asked to be behavior-proven.
//
//   2. Passive VBI catch-up stepping must advance CIA/VIC/PHI2 timing while freezing
//      the 6510 *without* forging cpu().state().jammed (audit P1-6). A genuine JAM
//      remains the only thing that sets that flag, and a completing play still runs
//      normally after a passive window.
//
//   3. resyncPlatformFromAuthoritativePhi2() (the P1-4 contamination-recovery
//      primitive invoked when a bounded platform-journal rollback cannot prove a
//      full restore) re-seeds the platform mirror from the authoritative PHI2 RAM.

#include "arpsid/core/c64_psid_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "c64_sidplay_rollback_behavioral_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string readSource(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_DIR) + "/" + rel, std::ios::binary);
    require(stream.good(), "source file must be readable");
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// ─── PSID builder (matches c64_psid_play_budget_v574_tests conventions) ────────
void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = uint8_t(v >> 8); b[off + 1] = uint8_t(v);
}
void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24); b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8); b[off + 3] = uint8_t(v);
}
void str32(std::vector<uint8_t>& b, size_t off, const char* s) {
    for (size_t i = 0; i < 32 && s[i]; ++i) b[off + i] = uint8_t(s[i]);
}

std::vector<uint8_t> buildPsid(uint16_t loadAddr, uint16_t initAddr, uint16_t playAddr,
                               const std::vector<uint8_t>& initCode,
                               const std::vector<uint8_t>& playCode) {
    const uint16_t dataOff = 0x7C;
    std::vector<uint8_t> b(dataOff, 0);
    b[0] = 'P'; b[1] = 'S'; b[2] = 'I'; b[3] = 'D';
    be16(b, 4, 2);        // version 2
    be16(b, 6, dataOff);  // data offset
    be16(b, 8, 0);        // embedded load address written after header (LE)
    be16(b, 0x0A, initAddr);
    be16(b, 0x0C, playAddr);
    be16(b, 0x0E, 1);     // songs
    be16(b, 0x10, 1);     // startSong
    be32(b, 0x12, 0);     // speed = 0 -> VBI timing
    str32(b, 0x16, "RollbackTestTune");
    str32(b, 0x36, "ArpSID");
    str32(b, 0x56, "2026");
    be16(b, 0x76, 0);     // PSID flags
    b.push_back(uint8_t(loadAddr));
    b.push_back(uint8_t(loadAddr >> 8));
    const size_t codeBase = b.size();
    const uint16_t highAddr = static_cast<uint16_t>(
        std::max<int>({static_cast<int>(initAddr) + static_cast<int>(initCode.size()),
                       static_cast<int>(playAddr) + static_cast<int>(playCode.size())}));
    const size_t totalBytes = static_cast<size_t>(highAddr - loadAddr);
    b.resize(codeBase + totalBytes, 0xEA); // NOP padding
    const size_t initOff = static_cast<size_t>(initAddr - loadAddr);
    for (size_t i = 0; i < initCode.size(); ++i) b[codeBase + initOff + i] = initCode[i];
    const size_t playOff = static_cast<size_t>(playAddr - loadAddr);
    for (size_t i = 0; i < playCode.size(); ++i) b[codeBase + playOff + i] = playCode[i];
    return b;
}

// LDA #$00 / RTS — init writes nothing so pre-frame SID state is deterministic.
const std::vector<uint8_t> kSimpleInit = { 0xA9, 0x00, 0x60 };

constexpr uint16_t kLoadAddr = 0x0800;
constexpr uint16_t kInitAddr = 0x0800;
constexpr uint16_t kPlayAddr = 0x0810;
constexpr uint16_t kRamProbe = 0xC000; // RAM (no I/O) so peekRam/peekMemory are direct

// Play routine that mutates RAM + SID + D418 then loops forever (never RTS), so
// runPlay() exhausts its instruction budget and returns false with the machine
// mid-frame — exactly the failure a render transaction must roll back.
std::vector<uint8_t> makeWritingLoopPlay() {
    // 0:  A9 42        LDA #$42
    // 2:  8D 00 C0     STA $C000     (RAM write)
    // 5:  A9 0F        LDA #$0F
    // 7:  8D 18 D4     STA $D418     (volume/filter-mode = D418)
    // 10: A9 21        LDA #$21
    // 12: 8D 01 D4     STA $D401     (SID voice-1 freq hi)
    // 15: 4C 1F 08     JMP $081F     (self-loop; kPlayAddr + 15)
    const uint16_t self = static_cast<uint16_t>(kPlayAddr + 15u);
    return { 0xA9, 0x42,
             0x8D, uint8_t(kRamProbe & 0xFF), uint8_t(kRamProbe >> 8),
             0xA9, 0x0F,
             0x8D, 0x18, 0xD4,
             0xA9, 0x21,
             0x8D, 0x01, 0xD4,
             0x4C, uint8_t(self & 0xFF), uint8_t(self >> 8) };
}

// Play routine that writes SID then returns cleanly (completes within budget).
std::vector<uint8_t> makeCompletingPlay() {
    // LDA #$33 / STA $D401 / RTS
    return { 0xA9, 0x33, 0x8D, 0x01, 0xD4, 0x60 };
}

// ─── 1. Forced failed-VBI-play rollback restores authoritative + mirror state ──
void testFailedVbiPlayRollback() {
    using namespace ArpSID::C64;

    auto psid = buildPsid(kLoadAddr, kInitAddr, kPlayAddr, kSimpleInit, makeWritingLoopPlay());
    C64Runtime rt;
    rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "loadPsid must accept the synthetic PSID");
    require(rt.runInit(), "runInit must complete");
    require(rt.phi2MachineReady(), "PHI2 machine must be ready for VBI play");

    // Snapshot the exact pre-frame state we expect rollback to restore.
    const uint8_t  preRam      = rt.phi2Machine().memory().peekRam(kRamProbe);
    const uint8_t  prePlatRam  = rt.platform().peekMemory(kRamProbe);
    const uint8_t  preD418     = rt.sidSink().regs[0x18];
    const uint8_t  preD401     = rt.sidSink().regs[0x01];
    const uint64_t preCycle    = rt.phi2Machine().phi2Cycle();
    const uint64_t preWrites   = rt.sidSink().writeCount;

    auto tx = rt.beginRenderTransaction();
    require(tx.active, "render transaction must open");
    require(tx.platformJournalActive, "platform mutation journal must be armed");

    const bool played = rt.runPlay(64u); // tiny budget -> the self-loop cannot complete
    require(!played, "forced-failure play must report incomplete");

    // Prove the frame really mutated authoritative + mirror state (else rollback
    // would trivially "pass" by restoring identical values).
    require(rt.phi2Machine().memory().peekRam(kRamProbe) == 0x42,
            "failed frame must have written RAM before rollback");
    require(rt.sidSink().regs[0x18] == 0x0F, "failed frame must have written D418 before rollback");
    require(rt.sidSink().regs[0x01] == 0x21, "failed frame must have written SID freq before rollback");
    require(rt.phi2Machine().phi2Cycle() > preCycle, "failed frame must have advanced PHI2 cycles");
    require(rt.sidSink().writeCount > preWrites, "failed frame must have recorded SID writes");

    const bool rolledBack = rt.rollbackRenderTransaction(tx);
    require(rolledBack, "bounded journal must prove a full rollback for a small frame");

    // Authoritative PHI2 machine + runtime SID sink restored exactly.
    require(rt.phi2Machine().memory().peekRam(kRamProbe) == preRam,
            "rollback must restore PHI2 RAM");
    require(rt.phi2Machine().phi2Cycle() == preCycle, "rollback must restore the PHI2 cycle counter");
    require(rt.sidSink().regs[0x18] == preD418, "rollback must restore D418 register in the runtime sink");
    require(rt.sidSink().regs[0x01] == preD401, "rollback must restore SID freg register in the runtime sink");
    require(rt.sidSink().writeCount == preWrites, "rollback must restore the runtime sink write count");
    // Platform inspection mirror restored via the bounded journal.
    require(rt.platform().peekMemory(kRamProbe) == prePlatRam,
            "rollback must restore the platform RAM mirror");
    require(!tx.active, "transaction token must be cleared after rollback");
}

// ─── 2. Passive stepping is honest about CPU state (no forged jam) ─────────────
void testPassiveSteppingDoesNotForgeJam() {
    using namespace ArpSID::C64;

    // A completing play so we can also prove passive+play cooperation afterwards.
    auto psid = buildPsid(kLoadAddr, kInitAddr, kPlayAddr, kSimpleInit, makeCompletingPlay());
    C64Runtime rt;
    rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "loadPsid must accept the synthetic PSID");
    require(rt.runInit(), "runInit must complete");
    require(rt.phi2MachineReady(), "PHI2 machine must be ready");

    require(!rt.phi2Machine().cpu().state().jammed, "CPU must not be jammed after a clean init");

    const uint64_t retiredBefore = rt.phi2Machine().cpu().retiredInstructionCount();
    const uint64_t cyclesBefore  = rt.phi2Machine().phi2Cycle();

    const uint64_t requested = 4000u;
    const auto passive = rt.runPsidVbiPassivePhi2Cycles(requested, &rt.sidSink());
    (void)passive;

    // Timing advanced (CIA/VIC/open-bus ticked) …
    require(rt.phi2Machine().phi2Cycle() >= cyclesBefore + requested,
            "passive stepping must advance PHI2 timing by the requested window");
    // … while the 6510 stayed frozen …
    require(rt.phi2Machine().cpu().retiredInstructionCount() == retiredBefore,
            "passive stepping must not retire any 6510 instructions");
    // … and the CPU's real fault flag was never forged, nor left suppressed.
    require(!rt.phi2Machine().cpu().state().jammed,
            "passive stepping must not forge cpu().state().jammed (v872 P1-6)");
    require(!rt.phi2Machine().cpuExecutionSuppressed(),
            "passive stepping must clear execution suppression when it returns");

    // The machine is left runnable: a normal completing play still succeeds after
    // a passive window (previously this depended on runPlay clearing a forged jam).
    require(rt.runPlay(4096u), "a completing play must run normally after passive stepping");
    require(!rt.phi2Machine().cpu().state().jammed, "a completing play must not leave the CPU jammed");
}

// ─── 3. Contamination recovery primitive re-seeds the platform mirror ──────────
void testResyncPlatformFromAuthoritativePhi2() {
    using namespace ArpSID::C64;

    auto psid = buildPsid(kLoadAddr, kInitAddr, kPlayAddr, kSimpleInit, makeCompletingPlay());
    C64Runtime rt;
    rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "loadPsid must accept the synthetic PSID");
    require(rt.runInit(), "runInit must complete");
    require(rt.phi2MachineReady(), "PHI2 machine must be ready");

    // Force a divergence: authoritative PHI2 RAM says one thing, the platform
    // mirror still holds the old value (this is exactly the residue a failed
    // bounded-journal rollback can leave behind).
    const uint8_t platBefore = rt.platform().peekMemory(kRamProbe);
    const uint8_t authoritative = static_cast<uint8_t>(platBefore ^ 0x5Au); // guaranteed != platBefore
    rt.phi2Machine().memory().pokeRam(kRamProbe, authoritative);
    require(rt.platform().peekMemory(kRamProbe) == platBefore,
            "platform mirror must still be stale before recovery");
    require(rt.phi2Machine().memory().peekRam(kRamProbe) == authoritative,
            "authoritative PHI2 RAM must hold the new value");

    rt.resyncPlatformFromAuthoritativePhi2();

    require(rt.platform().peekMemory(kRamProbe) == authoritative,
            "resync must re-seed the platform mirror from authoritative PHI2 RAM");
}

// ─── 4. Kernel + runtime wiring guards (repo source-string convention) ─────────
// The kernel (ArpSIDDSPKernel.hpp) is only compiled into the AU plugin build, so —
// like the v871 render-transaction test — the kernel-side closure is guarded by
// source-string search here alongside the behavioral proofs above.
void testKernelAndRuntimeWiring() {
    const std::string runtime = readSource("include/arpsid/core/c64_psid_runtime.h");
    const std::string phi2 = readSource("include/arpsid/core/c64_phi2_machine.h");
    const std::string kernel = readSource("source/au3/ArpSIDDSPKernel.hpp");

    // P1-6: passive stepping uses explicit suppression, not a forged CPU jam.
    require(contains(phi2, "setCpuExecutionSuppressed"),
            "PHI2 machine must expose explicit CPU-execution suppression");
    require(contains(phi2, "if (!cpuExecutionSuppressed_) {"),
            "tickPhi2 must freeze the CPU when execution is suppressed");
    require(contains(runtime, "setCpuExecutionSuppressed(true)"),
            "passive VBI stepping must suppress CPU execution explicitly");
    require(!contains(runtime, "phi2Machine_.cpu().state().jammed = true"),
            "passive stepping must no longer forge cpu().state().jammed");

    // P1-4: contamination recovery + latched flag.
    require(contains(runtime, "void resyncPlatformFromAuthoritativePhi2()"),
            "runtime must provide the platform-resync recovery primitive");
    require(contains(kernel, "player->resyncPlatformFromAuthoritativePhi2()"),
            "kernel must recover the platform mirror on rollback failure");
    require(contains(kernel, "c64RenderContaminated_.store(1u"),
            "kernel must latch the contamination flag on rollback failure");
    require(contains(kernel, "telemetryC64RenderContaminated_"),
            "kernel must publish the contamination flag to telemetry");

    // P1-5: failure/drop counters reset on tune handoff.
    require(contains(kernel, "void resetC64FailureCountersForHandoff_()"),
            "kernel must define the per-handoff failure-counter reset");
    require(contains(kernel, "resetC64FailureCountersForHandoff_();"),
            "kernel must invoke the per-handoff failure-counter reset");

    // P2-1: continuous-machine-path health counters exist and are populated.
    require(contains(kernel, "c64ContinuousBudgetHitCount_") &&
            contains(kernel, "c64ContinuousCpuJamCount_") &&
            contains(kernel, "c64ContinuousUnsupportedOpcodeCount_") &&
            contains(kernel, "c64ContinuousIncompleteRunCount_"),
            "kernel must expose continuous-run health counters");
    require(contains(kernel, "rsidResult.instructionBudgetHit"),
            "kernel must populate the continuous budget-hit counter from the run result");
    require(contains(kernel, "telemetryC64ContinuousIncompleteRunCount_"),
            "kernel must publish continuous-run health to telemetry");
}

} // namespace

int main() {
    testFailedVbiPlayRollback();
    testPassiveSteppingDoesNotForgeJam();
    testResyncPlatformFromAuthoritativePhi2();
    testKernelAndRuntimeWiring();
    std::cout << "c64_sidplay_rollback_behavioral_v872_tests PASS\n";
    return 0;
}
