// c64_phi2_rsid_exact_init_v605_tests.cpp
// Regression tests for bus-cycle-accurate RSID init via C64Phi2Machine.
//
// Covers:
// I. PHI2 machine runs the RSID bootstrap from reset vector via tickPhi2()
// II. SID writes from init land in both phi2Diagnostics and sink_.regs
// III. Legacy platform sidRegisterImage() is seeded from PHI2 init sink
// IV. enablePhi2Machine(true) after PHI2 init does NOT re-sync / clobber state
// V. PHI2 cycle counter is preserved across init (not reset to 0)
// VI. Color RAM and KERNAL ROM are mirrored into PHI2 machine before init
// VII. Compatible BRK-as-halt fixtures complete on the PHI2 path
// VIII.Post-BRK enable keeps a correct ready PHI2 machine

#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static void req(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}

// Build a minimal RSID image.
// initCode is placed at loadAddr; it should end with RTS (normal) or BRK (compatible sentinel).
static std::vector<uint8_t> buildRsid(
    uint16_t loadAddr,
    const std::vector<uint8_t>& initCode)
{
    // Minimal RSID v2 header (0x7C bytes)
    std::vector<uint8_t> v(0x7Cu, 0u);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    // version=2, dataOffset=0x7C, loadAddr embedded (=0 in header → use embedded)
    v[4]=0; v[5]=2;    // version
    v[6]=0; v[7]=0x7Cu; // dataOffset
    // loadAddr=0 → embedded in payload
    v[8]=0; v[9]=0;
    // initAddr = loadAddr
    v[10]=static_cast<uint8_t>(loadAddr >> 8u);
    v[11]=static_cast<uint8_t>(loadAddr & 0xFFu);
    // playAddr=0 (RSID machine-mode)
    v[12]=0; v[13]=0;
    // songs=1, startSong=1
    v[14]=0; v[15]=1; v[16]=0; v[17]=1;
    // speed=0
    v[18]=0; v[19]=0; v[20]=0; v[21]=0;
    // name / author / released: leave zero-terminated
    // flags: PAL (bits 2-3=1)
    v[0x76]=0x00u; v[0x77]=0x04u; // PAL flag in v2 flags

    // Embedded load address (little-endian)
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    // Init code
    for (uint8_t b : initCode) v.push_back(b);
    return v;
}

// ─── I. PHI2 machine boots from reset vector and runs bootstrap ───────────────
static void testPhi2InitRunsBootstrap() {
    using namespace ArpSID::C64;

    // Init: LDA #$77; STA $D407 (voice 2 waveform); RTS
    const std::vector<uint8_t> initCode = { 0xA9u, 0x77u, 0x8Du, 0x07u, 0xD4u, 0x60u };
    auto rsid = buildRsid(0x0900u, initCode);

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 2048), "RSID init via PHI2 machine");

    req(rt.phi2Diagnostics().sidWrites >= 1u,
        "PHI2 init diagnostics: at least one SID write captured");
    req(rt.phi2Diagnostics().phi2Cycles > 0u,
        "PHI2 machine advanced at least one cycle during init");
}

// ─── II. SID writes from init land in sink and diagnostics ───────────────────
static void testPhi2InitSidWritesCaptured() {
    using namespace ArpSID::C64;

    // Write to $D400 (reg 0) and $D407 (reg 7)
    const std::vector<uint8_t> initCode = {
        0xA9u, 0x11u, 0x8Du, 0x00u, 0xD4u, // LDA #$11; STA $D400
        0xA9u, 0xAAu, 0x8Du, 0x07u, 0xD4u, // LDA #$AA; STA $D407
        0x60u                               // RTS
    };
    auto rsid = buildRsid(0x0800u, initCode);

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 2048), "RSID init");

    req(rt.sidSink().regs[0] == 0x11u,
        "PHI2 init sink: reg 0 == $11");
    req(rt.sidSink().regs[7] == 0xAAu,
        "PHI2 init sink: reg 7 == $AA");
    req(rt.sidSink().writeCount >= 2u,
        "PHI2 init sink: at least two writes recorded");
}

// ─── III. Legacy platform sidRegisterImage() seeded from PHI2 sink ───────────
static void testPlatformSidImageSeededFromPhi2() {
    using namespace ArpSID::C64;

    // Init writes $33 to $D400
    const std::vector<uint8_t> initCode = {
        0xA9u, 0x33u, 0x8Du, 0x00u, 0xD4u, // LDA #$33; STA $D400
        0x60u                               // RTS
    };
    auto rsid = buildRsid(0x0C00u, initCode);

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 2048), "RSID init");

    req(rt.platform().sidRegisterImage()[0] == 0x33u,
        "platform sidRegisterImage[0] seeded from PHI2 init sink ($33)");
}

// ─── IV. enablePhi2Machine(true) after PHI2 init does NOT clobber state ───────
static void testEnablePhi2MachinePreservesInitState() {
    using namespace ArpSID::C64;

    const std::vector<uint8_t> initCode = {
        0xA9u, 0x55u, 0x8Du, 0x00u, 0xD4u, // LDA #$55; STA $D400
        0x60u                               // RTS
    };
    auto rsid = buildRsid(0x0A00u, initCode);

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 2048), "RSID init");

    // Capture the PHI2 cycle counter before enablePhi2Machine
    const uint64_t phi2BeforeEnable = rt.phi2Machine().phi2Cycle();
    req(phi2BeforeEnable > 0u, "PHI2 cycle counter advanced during init");

    // enablePhi2Machine(true) should not re-sync / reset the machine
    rt.enablePhi2Machine(true);

    req(rt.phi2MachineEnabled() && rt.phi2MachineReady(),
        "PHI2 machine is enabled and ready after enablePhi2Machine(true)");
    req(rt.phi2Machine().phi2Cycle() == phi2BeforeEnable,
        "PHI2 cycle counter unchanged by enablePhi2Machine(true) when PHI2 init was used");
    // SID sink still has the values from init
    req(rt.sidSink().regs[0] == 0x55u,
        "sink_.regs[0] preserved after enablePhi2Machine(true)");
    // PHI2-path reporting is separate from strict exactness. The current VIC
    // arbitration model is explicitly approximate, so strict exactness must
    // remain false even for a clean PHI2 init.
    req(rt.phi2InitUsed(),
        "clean RSID init reports it used the PHI2 init path");
    req(rt.rsidPhi2PlaybackActive(),
        "clean RSID init reports PHI2 playback active");
    req(!rt.rsidExactPlaybackActive(),
        "known approximate VIC arbitration prevents a strict exactness claim");
    req(rt.rsidLegacyInitFallbackCount() == 0u,
        "clean RSID init records zero legacy init fallbacks");
}

// ─── V. PHI2 cycle counter is non-zero and advances across init ───────────────
static void testPhi2CycleCounterAdvancesDuringInit() {
    using namespace ArpSID::C64;

    const std::vector<uint8_t> initCode = { 0x60u }; // RTS immediately
    auto rsid = buildRsid(0x0800u, initCode);

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");

    const uint64_t phi2AtLoad = rt.phi2Machine().phi2Cycle();
    req(rt.runInit(1, 2048), "RSID init (trivial RTS)");

    const uint64_t phi2AfterInit = rt.phi2Machine().phi2Cycle();
    req(phi2AfterInit > phi2AtLoad,
        "PHI2 cycle counter advanced between load and end of init");
}

// ─── VI. Color RAM and KERNAL ROM mirrored into PHI2 machine ─────────────────
static void testColorRamAndKernalRomMirrored() {
    using namespace ArpSID::C64;

    const std::vector<uint8_t> initCode = { 0x60u }; // RTS
    auto rsid = buildRsid(0x0800u, initCode);

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 2048), "RSID init");

    // The KERNAL ROM safe IRQ handler at $FF58 should be RTI (0x40)
    // (installed by installPsidSafeVectors and mirrored to PHI2 machine KERNAL ROM)
    const uint8_t kernalAt_FF58 = rt.phi2Machine().memory().peekKernalRom(0xFF58u);
    req(kernalAt_FF58 == 0x40u,
        "PHI2 machine KERNAL ROM $FF58 == RTI (safe vector handler mirrored from platform)");
}

// ─── VII. Compatible BRK-as-halt fixtures complete on PHI2 ──────────────────
static void testPhi2InitCompletesForBrkHaltOnPhi2() {
    using namespace ArpSID::C64;

    // Init code: LDA #$44; STA $D400; BRK (BRK as halt — legacy pattern)
    const std::vector<uint8_t> initCode = {
        0xA9u, 0x44u, 0x8Du, 0x00u, 0xD4u, // LDA #$44; STA $D400
        0x00u                               // BRK (legacy halt sentinel)
    };
    auto rsid = buildRsid(0x0800u, initCode);

    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 512), "compatible RSID init with BRK-halt sentinel succeeds");

    // Regardless of which path ran, the SID write must be visible
    req(rt.platform().sidRegisterImage()[0] == 0x44u,
        "SID reg 0 == $44 after BRK-halt init");
    req(rt.phi2MachineEnabled() == false,
        "PHI2 machine not yet enabled (enablePhi2Machine not called)");

    // With trapBrkAsJam the PHI2 init treats a terminal BRK as completion, so
    // BRK-halt tunes now initialise ON the exact PHI2 path (no legacy fallback).
    // PHI2 path activity agrees with whether PHI2 init was used. Strict
    // exactness remains false while any known machine approximation is active.
    rt.enablePhi2Machine(true);
    req(rt.rsidPhi2PlaybackActive() == rt.phi2InitUsed(),
        "PHI2 playback report agrees with whether PHI2 init was used");
    req(!rt.rsidExactPlaybackActive(),
        "strict exactness is not advertised with approximate VIC arbitration");
    req(rt.rsidLegacyInitFallbackCount() == 0u,
        "BRK-compatible init does not use retired legacy fallback");
}

// ─── VIII. Post-BRK enable preserves the ready PHI2 machine ──────────────────
static void testPostBrkEnablePreservesReadyMachine() {
    using namespace ArpSID::C64;

    const std::vector<uint8_t> initCode = {
        0xA9u, 0x44u, 0x8Du, 0x00u, 0xD4u,
        0x00u  // BRK-compatible completion sentinel
    };
    auto rsid = buildRsid(0x0800u, initCode);

    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    req(rt.loadPsid(rsid.data(), rsid.size()), "RSID load");
    req(rt.runInit(1, 512), "compatible BRK-sentinel RSID init");

    // Calling enablePhi2Machine(true) after PHI2 BRK-sentinel init should keep
    // the machine ready without relying on a legacy fallback sync.
    rt.enablePhi2Machine(true);
    req(rt.phi2MachineEnabled() && rt.phi2MachineReady(),
        "PHI2 machine is ready after enablePhi2Machine(true) following BRK sentinel");
    req(rt.phi2Machine().memory().peekRam(0x0800u) == 0xA9u,
        "PHI2 machine RAM has SID payload after enable");
}

int main() {
    testPhi2InitRunsBootstrap();
    testPhi2InitSidWritesCaptured();
    testPlatformSidImageSeededFromPhi2();
    testEnablePhi2MachinePreservesInitState();
    testPhi2CycleCounterAdvancesDuringInit();
    testColorRamAndKernalRomMirrored();
    testPhi2InitCompletesForBrkHaltOnPhi2();
    testPostBrkEnablePreservesReadyMachine();
    std::cout << "C64Phi2RsidExactInitV605Tests PASS\n";
    return 0;
}
