// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_sid_bus_sink.h"

#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void load(ArpSID::C64::C64Phi2Machine& m, uint16_t pc, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) m.memory().pokeRam(static_cast<uint16_t>(pc + i), bytes[i]);
    m.setProgramCounter(pc);
}

static void testBranchNoPageCross();
static void testBranchPageCrossCycleCount();

int main() {
    using namespace ArpSID::C64;

    {
        C64Phi2Machine m;
        SidBusSink sid;
        m.attachSidSink(&sid);
        m.powerOn();
        const uint8_t program[] = {
            0xA9u, 0x34u,             // LDA #$34
            0x8Du, 0x00u, 0xD4u,     // STA $D400
            0x00u
        };
        load(m, 0x0800u, program, sizeof(program));
        m.runPhi2(6);
        require(sid.writeCount() == 1, "STA D400 emits exactly one SID write");
        require(sid.write(0).phi2 == 5u, "STA D400 write occurs on fourth STA microcycle");
        require(sid.write(0).reg == 0x00u && sid.write(0).value == 0x34u, "STA D400 write reg/value exact");
    }

    {
        C64Phi2Machine m;
        SidBusSink sid;
        m.attachSidSink(&sid);
        m.powerOn();
        const uint8_t program[] = {
            0xA9u, 0x80u,             // LDA #$80
            0x8Du, 0x04u, 0xD4u,     // STA $D404
            0x0Eu, 0x04u, 0xD4u,     // ASL $D404
            0x00u
        };
        load(m, 0x0800u, program, sizeof(program));
        m.runPhi2(12);
        require(sid.writeCount() == 3, "RMW ASL D404 emits STA, dummy, final SID writes");
        require(sid.write(0).phi2 == 5u && sid.write(0).reg == 0x04u && sid.write(0).value == 0x80u,
                "initial STA D404 write exact");
        require(sid.write(1).phi2 == 10u && sid.write(1).rmwDummy,
                "ASL D404 dummy write is visible and marked");
        require(sid.write(2).phi2 == 11u && !sid.write(2).rmwDummy,
                "ASL D404 final write follows dummy on next PHI2");
        require(m.diagnostics().rmwDummySidWrites == 1u, "machine diagnostics count RMW dummy SID writes");
    }

    testBranchNoPageCross();
    testBranchPageCrossCycleCount();
    std::cout << "c64_phi2_sid_write_bus_timing_tests PASS\n";
    return 0;
}


namespace {
static void load2(ArpSID::C64::C64Phi2Machine& m, uint16_t pc, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) m.memory().pokeRam(static_cast<uint16_t>(pc + i), bytes[i]);
    m.setProgramCounter(pc);
}
}

static void testBranchNoPageCross() {
    using namespace ArpSID::C64;
    C64Phi2Machine m;
    SidBusSink sid;
    m.attachSidSink(&sid);
    m.powerOn();
    // BNE $0802+rel → target $080A (same page): LDA #0; BNE +6; NOP (not taken); [target] STA $D400
    const uint8_t prog[] = {
        0xA9u, 0x00u,       // LDA #$00 sets Z=1
        0xD0u, 0x06u,       // BNE +6 (not taken, Z=1)
        0xA9u, 0xAAu,       // LDA #$AA ← this runs since BNE not taken
        0x8Du, 0x00u, 0xD4u // STA $D400
    };
    load2(m, 0x0800u, prog, sizeof(prog));
    m.runPhi2(11); // LDA(2) + BNE-not-taken(2) + LDA(2) + STA(4) + opfetch(1)
    require(sid.writeCount() == 1,   "BNE not-taken path reaches STA");
    require(sid.write(0).value == 0xAAu, "BNE not-taken: correct store value");
}

static void testBranchPageCrossCycleCount() {
    using namespace ArpSID::C64;
    // A page-crossing taken branch costs 4 cycles (T0+T1+T2+T3).
    // Verify: BCS placed near page boundary so target crosses to next page.
    // Put branch at $08FD (opcode) $08FE (displacement).
    // After T1 post-PC = $08FF. Set displacement = $10 → target $090F (cross).
    C64Phi2Machine m;
    SidBusSink sid;
    m.attachSidSink(&sid);
    m.powerOn();

    // Seed C=1 via SEC at $08FB, then BCS +$10 at $08FD
    m.memory().pokeRam(0x08FBu, 0x38u);             // SEC
    m.memory().pokeRam(0x08FCu, 0xEAu);             // NOP (filler)
    m.memory().pokeRam(0x08FDu, 0xB0u);             // BCS
    m.memory().pokeRam(0x08FEu, 0x10u);             // displacement +$10 → target $090F
    // Target $090F: STA $D400
    m.memory().pokeRam(0x090Fu, 0x8Du);
    m.memory().pokeRam(0x0910u, 0x00u);
    m.memory().pokeRam(0x0911u, 0xD4u);
    m.memory().pokeRam(0x0912u, 0xEAu); // NOP after STA
    m.setProgramCounter(0x08FBu);

    // SEC(2) + NOP(2) + BCS page-cross-taken(4) + STA_abs(4) = 12 cycles.
    // After STA completes, PC = $0912 (pointing to the NOP after STA — not yet fetched).
    m.runPhi2(12);
    require(sid.writeCount() == 1,    "BCS page-cross: STA $D400 executed");
    require(m.cpu().pc() == 0x0912u,  "BCS page-cross: PC at NOP after STA ($0912)");
}
