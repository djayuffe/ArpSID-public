// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_audit_closure_v608_tests.cpp
// Pins the v608 audit-closure fixes:
// I. 6510 reset sequence: SP -= 3, I/U set & B clear, PC = reset vector.
// II. PLA decode honors RAM-under-IO (IO-visible write != RAM; IO-hidden = RAM).
// III. PLA decode honors cartridge GAME/EXROM (Ultimax + 16K), normal map intact.
// IV. CIA Timer B combined mode (CRB 11) counts Timer A underflows gated by
// the CNT LEVEL (not min(taUnderflows, cntPulses)).
// V. Micro-CPU approximate illegal-opcode counter increments for ARR/XAA/etc.
// VI. SID reads do not behave like normal register RAM.
// VII. SID/RMW/CIA/ROM/BASIC exactness downgrades are observable.
// VIII. CIA model explicitly declares timer/IRQ model non-cycle-exact.

#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_cpu6510_micro.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_sid_bus_sink.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static bool hasDowngrade_(ArpSID::C64::RsidExactnessDowngrade value,
                          ArpSID::C64::RsidExactnessDowngrade bit) {
    using U = uint32_t;
    return (static_cast<U>(value) & static_cast<U>(bit)) != 0u;
}


// ── I. Reset sequence: SP-=3, flags, PC=vector ───────────────────────────────
static void testResetSequence() {
    using namespace ArpSID::C64;
    C64Phi2Machine m;
    Phi2MachineConfig cfg{};
    m.configure(cfg);
    m.powerOn();
    // Point the reset vector at $C000 (override the deterministic handler).
    m.memory().pokeKernalRom(0xFFFCu, 0x00u);
    m.memory().pokeKernalRom(0xFFFDu, 0xC0u);

    const uint8_t spBefore = m.cpu().state().sp; // 0xFD after power-on
    m.resetToVector();
    m.runPhi2(8); // 8-cycle reset sequence (T0..T7)

    require(m.cpu().pc() == 0xC000u, "reset fetches PC from the reset vector");
    require(m.cpu().state().sp == static_cast<uint8_t>(spBefore - 3u),
            "reset decrements SP by 3 (suppressed-push stack cycles)");
    const uint8_t p = m.cpu().state().p;
    require((p & kFlagI) != 0u, "reset sets the I (interrupt-disable) flag");
    require((p & kFlagUnused) != 0u, "reset keeps the unused flag set");
    require((p & kFlagB) == 0u, "reset clears the B flag in P");
}

// ── II. RAM-under-IO decode ──────────────────────────────────────────────────
static void testRamUnderIoDecode() {
    using namespace ArpSID::C64;
    // IO visible: loram=hiram=charen=1, no cartridge.
    PlaState io{true, true, true, true, true};
    require(decodeCpuWrite(0xD400u, io) == WriteTarget::Sid,
            "IO-visible write to $D400 targets the SID device, not RAM");
    require(decodeCpuRead(0xD400u, io) == ReadTarget::Sid,
            "IO-visible read of $D400 targets the SID device");

    // IO hidden (charen=0): $D000-$DFFF is RAM-under-IO.
    PlaState ram{true, true, false, true, true};
    require(decodeCpuWrite(0xD400u, ram) == WriteTarget::Ram,
            "IO-hidden write to $D400 targets the RAM underneath");
    // charen=0 exposes CHAR ROM on reads at $D000-$DFFF (classic C64 behavior).
    require(decodeCpuRead(0xD400u, ram) == ReadTarget::CharRom,
            "IO-hidden read of $D000-$DFFF exposes CHAR ROM");

    // Fully banked out (loram=hiram=0): RAM everywhere in $D000-$DFFF.
    PlaState allram{false, false, false, true, true};
    require(decodeCpuRead(0xD400u, allram) == ReadTarget::Ram,
            "all-RAM map reads $D400 as RAM");
}

// ── III. Cartridge GAME/EXROM decode ─────────────────────────────────────────
static void testCartridgeDecode() {
    using namespace ArpSID::C64;
    // Normal map (both lines high) — must be byte-identical to the classic decode.
    PlaState normal{true, true, true, true, true};
    require(decodeCpuRead(0xE000u, normal) == ReadTarget::KernalRom,
            "normal map: $E000 is KERNAL ROM");
    require(decodeCpuRead(0xA000u, normal) == ReadTarget::BasicRom,
            "normal map: $A000 is BASIC ROM");
    require(decodeCpuRead(0x8000u, normal) == ReadTarget::Ram,
            "normal map: $8000 is RAM (no cartridge)");

    // Ultimax (game=0, exrom=1): KERNAL/BASIC gone; ROML/ROMH cartridge windows.
    PlaState ultimax{true, true, true, /*game*/false, /*exrom*/true};
    require(decodeCpuRead(0xE000u, ultimax) == ReadTarget::Cartridge,
            "Ultimax: $E000 is cartridge ROMH, not KERNAL");
    require(decodeCpuRead(0x8000u, ultimax) == ReadTarget::Cartridge,
            "Ultimax: $8000 is cartridge ROML");
    require(decodeCpuRead(0x0500u, ultimax) == ReadTarget::Ram,
            "Ultimax: $0500 (<= $0FFF) is RAM");
    require(decodeCpuRead(0x4000u, ultimax) == ReadTarget::OpenBus,
            "Ultimax: $4000 (open window) floats as open bus");
    require(decodeCpuRead(0xD400u, ultimax) == ReadTarget::Sid,
            "Ultimax: $D400 IO is still the SID device");

    // 16K cartridge (game=0, exrom=0): ROML $8000, ROMH $A000.
    PlaState cart16{true, true, true, /*game*/false, /*exrom*/false};
    require(decodeCpuRead(0x8000u, cart16) == ReadTarget::Cartridge,
            "16K cart: $8000 is cartridge ROML");
    require(decodeCpuRead(0xA000u, cart16) == ReadTarget::Cartridge,
            "16K cart: $A000 is cartridge ROMH");
    require(decodeCpuRead(0xE000u, cart16) == ReadTarget::KernalRom,
            "16K cart: $E000 is still KERNAL ROM");
}

// ── IV. CIA Timer B combined mode gated by CNT level ─────────────────────────
static void testCiaTimerBCombinedCntLevel() {
    using namespace ArpSID::C64;

    auto runCase = [](uint8_t cntLevel) -> uint16_t {
        Cia6526 cia;
        cia.reset();
        cia.setClockHz(985248u);
        // Timer A latch = 4 (underflows every ~4 PHI2 cycles), PHI2-clocked.
        cia.write(0x04u, 0x04u); cia.write(0x05u, 0x00u);
        // Timer B latch = 200 (won't underflow during the test window).
        cia.write(0x06u, 0xC8u); cia.write(0x07u, 0x00u);
        cia.setCntInput(cntLevel);
        // CRA: force-load ($10) + start ($01), source = PHI2.
        cia.write(0x0Eu, 0x11u);
        // CRB: force-load ($10) + start ($01) + mode 11 (bits 5,6 = $60) → count
        // Timer A underflows gated by CNT level.
        cia.write(0x0Fu, 0x71u);
        const uint16_t tbStart = cia.timerB();
        for (int i = 0; i < 64; ++i) cia.tick();
        const uint16_t tbEnd = cia.timerB();
        // Return how much Timer B counted down.
        return static_cast<uint16_t>(tbStart - tbEnd);
    };

    const uint16_t countedHigh = runCase(1u); // CNT high → counts TA underflows
    const uint16_t countedLow  = runCase(0u); // CNT low → gated off, no counting

    require(countedHigh > 0u,
            "CIA Timer B mode 11 counts Timer A underflows while CNT is high");
    require(countedLow == 0u,
            "CIA Timer B mode 11 does NOT count while CNT is held low");
}

// ── V. Approximate illegal-opcode counter ────────────────────────────────────
static void testApproximateOpcodeCounter() {
    using namespace ArpSID::C64;
    require(Cpu6510Micro::isApproximateOpcode(0x6Bu), "ARR # is classed approximate");
    require(Cpu6510Micro::isApproximateOpcode(0x8Bu), "XAA # is classed approximate");
    require(Cpu6510Micro::isApproximateOpcode(0x9Bu), "TAS abs,Y is classed approximate");
    require(!Cpu6510Micro::isApproximateOpcode(0xA9u), "LDA # is NOT approximate");
    require(!Cpu6510Micro::isApproximateOpcode(0xEAu), "NOP is NOT approximate");

    // Drive a tiny program through a flat-RAM machine and execute ARR #$00.
    C64Phi2Machine m;
    Phi2MachineConfig cfg{};
    m.configure(cfg);
    m.powerOn();
    m.memory().pokeRam(0x1000u, 0x6Bu); // ARR #
    m.memory().pokeRam(0x1001u, 0x00u);
    m.memory().pokeRam(0x1002u, 0xEAu); // NOP
    m.cpu().setPc(0x1000u);
    const uint64_t before = m.cpu().approximateOpcodeTotal();
    m.runPhi2(4); // fetch + immediate (2 cycles) + margin
    require(m.cpu().approximateOpcodeTotal() == before + 1u,
            "executing ARR # increments the approximate-opcode counter");
    require(m.cpu().lastApproximateOpcode() == 0x6Bu,
            "lastApproximateOpcode pins the exact ARR opcode");
    require(m.cpu().unsupportedOpcodeTotal() == 0u,
            "approximate opcodes are NOT counted as unsupported");
}

// ── VI. SID reads are not normal register RAM ────────────────────────────────
static void testSidReadNotRegisterRam() {
    using namespace ArpSID::C64;
    C64SidBridgeState bridge;
    bridge.reset();
    bridge.sidWrite(0x05u, 0x7Bu, 10u);
    require(bridge.sidRead(0x05u, 11u) == 0xFFu,
            "write-only SID register read does not return mirrored register RAM");
    bridge.sidWrite(0x1Bu, 0x42u, 12u);
    require(bridge.sidRead(0x1Bu, 13u) != 0x42u,
            "readable SID OSC3 register is deterministic state, not raw register mirror");
}

// ── VII. Exactness downgrades for SID reads/RMW/ROM/CIA ─────────────────────
// v893 refresh: RSID_BASIC images are now honestly REFUSED at parse/load
// (PsidParseResult::UnsupportedRsidBasic — see psid_header.h) instead of
// loading a pseudo-RSID that can never execute its BASIC bootstrap. The stale
// v608 expectation ("minimal BASIC-flag RSID loads" + BasicStartupUnsupported
// downgrade) is replaced by pinning the refusal taxonomy; the remaining
// downgrade checks run against a loadable non-BASIC RSID.
static void testRsidAdditionalExactnessDowngrades() {
    using namespace ArpSID::C64;
    auto makeImg = [](bool basicFlag) {
        std::array<std::uint8_t, 0x7c + 10> data{};
        data[0] = 'R'; data[1] = 'S'; data[2] = 'I'; data[3] = 'D';
        data[0x04] = 0x00; data[0x05] = 0x02;
        data[0x06] = 0x00; data[0x07] = 0x7c;
        data[0x08] = 0x00; data[0x09] = 0x00; // embedded load address
        data[0x0a] = 0x00; data[0x0b] = 0x00;
        data[0x0c] = 0x00; data[0x0d] = 0x00;
        data[0x0e] = 0x00; data[0x0f] = 0x01;
        data[0x10] = 0x00; data[0x11] = 0x01;
        data[0x76] = 0x00; data[0x77] = basicFlag ? 0x02 : 0x00;
        data[0x7c + 0] = 0x00; data[0x7c + 1] = 0x10;
        data[0x7c + 2] = 0x60;
        return data;
    };

    {
        C64Runtime rt;
        auto img = makeImg(true);
        require(!rt.loadPsid(img.data(), img.size()),
                "BASIC-flag RSID is honestly refused at load");
        require(rt.lastLoadFailure() == PsidLoadFailure::UnsupportedRsidBasic,
                "BASIC-flag RSID refusal is classed UnsupportedRsidBasic");
    }

    C64Runtime rt;
    auto img = makeImg(false);
    require(rt.loadPsid(img.data(), img.size()), "minimal non-BASIC RSID loads");

    Phi2BusPhase ph{};
    (void)rt.phi2Machine().memory().cpuRead(0u, 0xD400u, ph);
    rt.phi2Machine().memory().cpuWrite(1u, 0xD400u, 0x55u, ph, true);

    const auto r = rt.rsidExactnessDowngradeReasons();
    require(hasDowngrade_(r, RsidExactnessDowngrade::MissingRealRoms),
            "missing external ROM set downgrades RSID exactness");
    require(!hasDowngrade_(r, RsidExactnessDowngrade::BasicStartupUnsupported),
            "non-BASIC RSID does not carry the BASIC startup downgrade");
    require(!hasDowngrade_(r, RsidExactnessDowngrade::CiaModelApprox),
            "cycle-exact CIA timer/IRQ model does not downgrade exactness");
    require(hasDowngrade_(r, RsidExactnessDowngrade::SidReadApprox),
            "write-only SID register read is counted as open-bus approximation");
    require(hasDowngrade_(r, RsidExactnessDowngrade::RmwSidWrite),
            "RMW SID write downgrades exactness");
}

// ── VIII. CIA exactness contract ─────────────────────────────────────────────
static void testCiaExactnessContract() {
    using namespace ArpSID::C64;
    require(Cia6526::kTimerIrqModelIsCycleExact,
            "CIA timer/IRQ model is explicitly cycle-exact");
}


// ── IX. RMW SID timed-write flag preservation ───────────────────────────────
static void testRmwSidTimedWritePreserved() {
    using namespace ArpSID::C64;
    C64SidBridgeState bridge;
    bridge.reset();
    bridge.sidWrite(0x04u, 0x11u, 100u);
    bridge.sidWrite(0x04u, 0x22u, 101u);
    // C64SidBridgeState::sidWrite() is the legacy non-PHI2 sink and cannot mark
    // RMW by itself; verify the storage field defaults false.
    require(bridge.timedWriteCount == 2u, "plain SID bridge writes are queued");
    require(!bridge.timedWrites[0].rmwDummy && !bridge.timedWrites[1].rmwDummy,
            "plain SID bridge timed writes default to non-RMW");

    C64Phi2Machine m;
    Phi2MachineConfig cfg{};
    m.configure(cfg);
    m.powerOn();

    SidBusSink sink;
    m.attachSidSink(&sink);
    Phi2BusPhase ph{};
    m.memory().cpuWrite(10u, 0xD400u, 0x55u, ph, true);
    require(sink.writeCount() == 1u, "PHI2 RMW SID write reaches capture sink");
    require(sink.write(0).rmwDummy, "PHI2 SID write preserves rmwDummy flag");
    require(ph.rmwDummyWrite,
            "PHI2 bus phase preserves RMW SID write flag");
    require(sink.rmwDummyWrites() == 1u,
            "PHI2 SID sink counts RMW SID writes");
}


int main() {
    testResetSequence();
    testRamUnderIoDecode();
    testCartridgeDecode();
    testCiaTimerBCombinedCntLevel();
    testApproximateOpcodeCounter();
    testSidReadNotRegisterRam();
    testRsidAdditionalExactnessDowngrades();
    testCiaExactnessContract();
    testRmwSidTimedWritePreserved();
    std::cout << "C64AuditClosureV608Tests PASS\n";
    return 0;
}
