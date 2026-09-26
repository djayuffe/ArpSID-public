// Copyright (C) 2024-2026 Ulf Bertilsson
// v893 bit-exactness closure tests.
//
// Pins the low-level hardware-exactness fixes of the v893 audit:
//  1. Taken-branch dummy-read bus addresses (64doc 6510 timing):
//     T2 dummy read at the post-operand PC (next-instruction byte);
//     T3 (page-cross only) dummy read at the wrong-page address
//     (old PCH | new PCL) before PCH is fixed up.
//  2. ARR # (0x6B) exact NMOS semantics: result = (A&imm)>>1 | C<<7;
//     N = old C, Z from result, C = result bit 6, V = bit6 ^ bit5.
//     (Still counted in the approximate-opcode ledger for the strict
//     RSID policy contract — pinned by v608/v612/v721/v725.)
//  3. NMOS decimal-mode ADC/SBC flag exactness: decimal ADC takes Z from
//     the binary sum and N/V from the pre-correction high-nibble
//     intermediate; decimal SBC takes ALL flags from the binary difference.
//  4. CLI/SEI/PLP one-instruction IRQ recognition delay: the boundary poll
//     samples the I flag from before the instruction's final cycle.
//  5. SID TEST bit forces the pulse comparator output HIGH (reSID law,
//     basis of the test-bit digi technique). Pulse-only under TEST renders
//     full-scale / reads $FF from OSC3; noise/tri/saw under TEST stay 0
//     per the pinned v864 closure.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "arpsid/core/c64_cpu6510_micro.h"
#include "arpsid/core/c64_cpu6510_status.h"
#include "arpsid/core/c64_phi2_types.h"
#include "arpsid/core/c64_sid_readback.h"
#include "arpsid/core/sid_chip.h"

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "c64_sid_bitexact_v893_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

struct CpuHarness {
    ArpSID::C64::Cpu6510Micro cpu{};
    std::array<uint8_t, 65536> mem{};
    std::vector<ArpSID::C64::CpuBusRequest> log{};

    CpuHarness() { cpu.powerOn(); }

    void run(int cycles) {
        for (int i = 0; i < cycles; ++i) {
            const ArpSID::C64::CpuBusRequest r = cpu.tickPhi2Begin();
            uint8_t data = 0xFFu;
            if (r.active()) {
                if (r.isWrite()) mem[r.address] = r.dataOut;
                else data = mem[r.address];
                log.push_back(r);
            }
            cpu.tickPhi2End(data);
        }
    }

    // Index of the first sync (opcode fetch) at `addr` in the log, or -1.
    int firstSyncAt(uint16_t addr) const {
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i].sync && log[i].address == addr) return static_cast<int>(i);
        }
        return -1;
    }
    int firstVectorFetchAt(uint16_t addr) const {
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i].vectorFetch && log[i].address == addr) return static_cast<int>(i);
        }
        return -1;
    }
};

// ── 1. Branch dummy-read bus addresses ───────────────────────────────────

void branchTakenNoCrossDummyReadsNextInstruction() {
    CpuHarness h;
    h.mem[0x1000u] = 0xD0u; // BNE +$10 (power-on P has Z clear → taken)
    h.mem[0x1001u] = 0x10u;
    h.mem[0x1012u] = 0xEAu; // NOP at target
    h.cpu.setPc(0x1000u);
    h.run(4);
    require(h.log.size() == 4u, "taken no-cross branch is 3 cycles + next fetch");
    require(h.log[0].sync && h.log[0].address == 0x1000u, "T0 opcode fetch");
    require(h.log[1].kind == ArpSID::C64::CpuAccessKind::Read && h.log[1].address == 0x1001u,
            "T1 reads the displacement");
    require(h.log[2].kind == ArpSID::C64::CpuAccessKind::DummyRead && h.log[2].address == 0x1002u,
            "T2 dummy read is at the post-operand PC (next instruction), not the target");
    require(h.log[3].sync && h.log[3].address == 0x1012u,
            "next opcode fetch is at the branch target");
}

void branchTakenPageCrossDummyReadsWrongPageThenTarget() {
    CpuHarness h;
    h.mem[0x10F0u] = 0xD0u; // BNE +$20 → post-operand PC 0x10F2, target 0x1112
    h.mem[0x10F1u] = 0x20u;
    h.mem[0x1112u] = 0xEAu;
    h.cpu.setPc(0x10F0u);
    h.run(5);
    require(h.log.size() == 5u, "taken page-cross branch is 4 cycles + next fetch");
    require(h.log[2].kind == ArpSID::C64::CpuAccessKind::DummyRead && h.log[2].address == 0x10F2u,
            "T2 dummy read is at the post-operand PC");
    require(h.log[3].kind == ArpSID::C64::CpuAccessKind::DummyRead && h.log[3].address == 0x1012u,
            "T3 dummy read is at the wrong-page address (old PCH | new PCL)");
    require(h.log[4].sync && h.log[4].address == 0x1112u,
            "next opcode fetch is at the corrected target");
}

void branchNotTakenIsTwoCycles() {
    CpuHarness h;
    h.mem[0x1000u] = 0xF0u; // BEQ (Z clear at power-on → not taken)
    h.mem[0x1001u] = 0x10u;
    h.mem[0x1002u] = 0xEAu;
    h.cpu.setPc(0x1000u);
    h.run(3);
    require(h.log.size() == 3u, "not-taken branch is 2 cycles + next fetch");
    require(h.log[2].sync && h.log[2].address == 0x1002u,
            "not-taken branch falls through to the next instruction");
}

// ── 2. ARR # exact NMOS semantics ────────────────────────────────────────

void arrBinaryExact() {
    using namespace ArpSID::C64;
    {
        CpuHarness h;
        h.mem[0x2000u] = 0x6Bu; // ARR #$FF
        h.mem[0x2001u] = 0xFFu;
        h.cpu.setPc(0x2000u);
        h.cpu.setA(0xC0u);
        h.cpu.setStatus(0x24u); // C clear, D clear
        h.run(2);
        require(h.cpu.a() == 0x60u, "ARR: A=$C0 & $FF >> 1 with C=0 gives $60");
        require((h.cpu.p() & kFlagC) != 0u, "ARR: C = result bit 6 (set for $60)");
        require((h.cpu.p() & kFlagV) == 0u, "ARR: V = bit6 ^ bit5 (clear for $60)");
        require((h.cpu.p() & kFlagN) == 0u, "ARR: N = old carry (clear)");
        require((h.cpu.p() & kFlagZ) == 0u, "ARR: Z from result");
    }
    {
        CpuHarness h;
        h.mem[0x2000u] = 0x6Bu; // ARR #$FF with A=$FF, C=1
        h.mem[0x2001u] = 0xFFu;
        h.cpu.setPc(0x2000u);
        h.cpu.setA(0xFFu);
        h.cpu.setStatus(0x25u); // C set
        h.run(2);
        require(h.cpu.a() == 0xFFu, "ARR: ($FF>>1)|($80) = $FF");
        require((h.cpu.p() & kFlagC) != 0u, "ARR: C = bit 6 of $FF");
        require((h.cpu.p() & kFlagV) == 0u, "ARR: V clear (bit6 == bit5)");
        require((h.cpu.p() & kFlagN) != 0u, "ARR: N = old carry (set)");
    }
    {
        // Bit6/bit5 disagreement → V set. A&imm = $40 → r = $20 with C=0.
        CpuHarness h;
        h.mem[0x2000u] = 0x6Bu;
        h.mem[0x2001u] = 0x40u;
        h.cpu.setPc(0x2000u);
        h.cpu.setA(0xFFu);
        h.cpu.setStatus(0x24u);
        h.run(2);
        require(h.cpu.a() == 0x20u, "ARR: ($40>>1) = $20");
        require((h.cpu.p() & kFlagC) == 0u, "ARR: C = bit 6 of $20 (clear)");
        require((h.cpu.p() & kFlagV) != 0u, "ARR: V = bit6 ^ bit5 of $20 (set)");
    }
}

// ── 3. NMOS decimal-mode flag exactness ──────────────────────────────────

void decimalAdcFlagsAreNmosExact() {
    using namespace ArpSID::C64;
    {
        CpuHarness h;
        h.mem[0x2000u] = 0x69u; // ADC #$01, A=$99, D=1, C=0 → A=$00, C=1, Z=0
        h.mem[0x2001u] = 0x01u;
        h.cpu.setPc(0x2000u);
        h.cpu.setA(0x99u);
        h.cpu.setStatus(0x2Cu); // D set, C clear
        h.run(2);
        require(h.cpu.a() == 0x00u, "decimal ADC: $99 + $01 = $00 (BCD wrap)");
        require((h.cpu.p() & kFlagC) != 0u, "decimal ADC: carry out of BCD wrap");
        require((h.cpu.p() & kFlagZ) == 0u,
                "decimal ADC: Z comes from the BINARY sum ($9A != 0), not the BCD result");
        require((h.cpu.p() & kFlagN) != 0u,
                "decimal ADC: N from the pre-correction intermediate ($A0 bit 7)");
    }
    {
        CpuHarness h;
        h.mem[0x2000u] = 0x69u; // ADC #$01, A=$79, D=1, C=0 → A=$80, N=1, V=1
        h.mem[0x2001u] = 0x01u;
        h.cpu.setPc(0x2000u);
        h.cpu.setA(0x79u);
        h.cpu.setStatus(0x2Cu);
        h.run(2);
        require(h.cpu.a() == 0x80u, "decimal ADC: $79 + $01 = $80");
        require((h.cpu.p() & kFlagN) != 0u, "decimal ADC: N set from intermediate $80");
        require((h.cpu.p() & kFlagV) != 0u, "decimal ADC: V set from intermediate $80");
        require((h.cpu.p() & kFlagC) == 0u, "decimal ADC: no carry");
    }
}

void decimalSbcFlagsAreBinary() {
    using namespace ArpSID::C64;
    CpuHarness h;
    h.mem[0x2000u] = 0xE9u; // SBC #$01, A=$00, D=1, C=1 → A=$99, C=0, N=1
    h.mem[0x2001u] = 0x01u;
    h.cpu.setPc(0x2000u);
    h.cpu.setA(0x00u);
    h.cpu.setStatus(0x2Du); // D set, C set
    h.run(2);
    require(h.cpu.a() == 0x99u, "decimal SBC: $00 - $01 = $99 (BCD borrow)");
    require((h.cpu.p() & kFlagC) == 0u, "decimal SBC: C from binary borrow");
    require((h.cpu.p() & kFlagN) != 0u, "decimal SBC: N from binary difference ($FF)");
    require((h.cpu.p() & kFlagZ) == 0u, "decimal SBC: Z from binary difference");
}

// ── 4. CLI/SEI one-instruction IRQ delay ─────────────────────────────────

void cliDelaysIrqByOneInstruction() {
    CpuHarness h;
    h.mem[0x3000u] = 0x58u; // CLI
    h.mem[0x3001u] = 0xEAu; // NOP — must execute before the IRQ is taken
    h.mem[0xFFFEu] = 0x00u; // IRQ vector → $8000
    h.mem[0xFFFFu] = 0x80u;
    h.mem[0x8000u] = 0xEAu;
    h.cpu.setPc(0x3000u);
    h.cpu.setStatus(0x24u | ArpSID::C64::kFlagI); // I set before CLI
    h.cpu.setIrqLine(true);
    h.run(16);
    const int nopFetch = h.firstSyncAt(0x3001u);
    const int vecFetch = h.firstVectorFetchAt(0xFFFEu);
    const int handlerFetch = h.firstSyncAt(0x8000u);
    require(nopFetch >= 0, "instruction after CLI is fetched");
    require(vecFetch >= 0, "IRQ is eventually taken after CLI");
    require(nopFetch < vecFetch,
            "CLI delays IRQ recognition by one instruction (NOP runs first)");
    require(handlerFetch > vecFetch, "execution continues at the IRQ handler");
}

void seiWithPendingIrqStillTakesIrq() {
    CpuHarness h;
    h.mem[0x4000u] = 0x78u; // SEI
    h.mem[0x4001u] = 0xEAu; // must NOT execute before the IRQ
    h.mem[0xFFFEu] = 0x00u;
    h.mem[0xFFFFu] = 0x80u;
    h.mem[0x8000u] = 0xEAu;
    h.cpu.setPc(0x4000u);
    h.cpu.setStatus(0x20u); // I clear before SEI
    h.run(1);               // fetch SEI
    h.cpu.setIrqLine(true); // IRQ arrives during SEI
    h.run(12);
    const int nextFetch = h.firstSyncAt(0x4001u);
    const int vecFetch = h.firstVectorFetchAt(0xFFFEu);
    require(vecFetch >= 0, "IRQ pending during SEI is still taken (old I polled)");
    require(nextFetch < 0 || nextFetch > vecFetch,
            "the instruction after SEI does not run before the IRQ");
}

// ── 5. SID TEST bit forces pulse comparator high ─────────────────────────

void testBitPulseIsForcedHigh() {
    using namespace ArpSID;
    SIDVoice::initTablesOnce();
    ArpSIDForensicConfig fc{};
    fc.enable = false;
    fc.bitPerfectMode = true;

    SIDVoice v;
    v.reset();
    v.setModel(SIDModel::MOS8580);
    v.setPulseWidth(0x800u);
    v.setWaveform(0x40u); // pulse only
    v.setTestBit(true);
    (void)v.renderFromPhase(fc);
    require(v.readOscillatorByte() == 0xFFu,
            "pulse-only OSC readback is $FF while TEST is set");

    v.setWaveform(0x80u); // noise only — pinned v864 law unchanged
    (void)v.renderFromPhase(fc);
    require(v.readOscillatorByte() == 0x00u,
            "noise-only OSC readback stays 0 while TEST is set");

    v.setWaveform(0x40u);
    v.setGate(true);
    for (int i = 0; i < 400; ++i) v.stepCycle(); // raise the envelope under TEST
    const float s = v.renderFromPhase(fc);
    require(s > 0.0f,
            "pulse-only under TEST renders the positive full-scale rail (test-bit digi)");
}

void readbackModelMirrorsTestBitPulseLaw() {
    ArpSID::C64::SidReadbackModel rb;
    rb.reset(true);
    uint64_t phi2 = 100u;
    rb.write(++phi2, 0x12u, 0x48u); // voice 3 control: pulse | TEST
    require(rb.read(++phi2, 0x1Bu) == 0xFFu,
            "OSC3 pulse-only under TEST reads $FF");
    rb.write(++phi2, 0x12u, 0x88u); // noise | TEST — pinned v864 law unchanged
    require(rb.read(++phi2, 0x1Bu) == 0x00u,
            "OSC3 noise under TEST stays 0");
    rb.write(++phi2, 0x12u, 0x58u); // tri+pulse | TEST — combined stays low
    require(rb.read(++phi2, 0x1Bu) == 0x00u,
            "OSC3 combined pulse waveform under TEST stays pulled low");
}

} // namespace

int main() {
    branchTakenNoCrossDummyReadsNextInstruction();
    branchTakenPageCrossDummyReadsWrongPageThenTarget();
    branchNotTakenIsTwoCycles();
    arrBinaryExact();
    decimalAdcFlagsAreNmosExact();
    decimalSbcFlagsAreBinary();
    cliDelaysIrqByOneInstruction();
    seiWithPendingIrqStillTakesIrq();
    testBitPulseIsForcedHigh();
    readbackModelMirrorsTestBitPulseLaw();
    std::cout << "c64_sid_bitexact_v893_tests PASS\n";
    return 0;
}
