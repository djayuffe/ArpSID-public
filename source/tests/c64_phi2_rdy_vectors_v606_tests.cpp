// c64_phi2_rdy_vectors_v606_tests.cpp
// Regressions for:
// I. RDY-low holds the EXACT read bus phase (same address) and does not
// advance the micro-state; RDY-high resumes cleanly.
// II. RDY does not stall write cycles.
// III. Standalone C64Phi2Machine::powerOn() installs deterministic reset/
// IRQ/NMI vectors and safe handler handlers in KERNAL ROM.

#include "arpsid/core/c64_cpu6510_micro.h"
#include "arpsid/core/c64_phi2_machine.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

namespace {
std::array<uint8_t, 65536> g_mem{};

// Drive one PHI2 cycle of the micro CPU against flat RAM. Returns the bus
// request that was issued this cycle (so tests can inspect address/kind).
ArpSID::C64::CpuBusRequest stepMicro(ArpSID::C64::Cpu6510Micro& cpu) {
    using namespace ArpSID::C64;
    const CpuBusRequest req = cpu.tickPhi2Begin();
    uint8_t din = 0xFFu;
    if (req.active()) {
        if (req.isWrite()) g_mem[req.address] = req.dataOut;
        else din = g_mem[req.address];
        cpu.tickPhi2End(din);
    }
    return req;
}
} // namespace

// ── I. RDY-low holds the exact read address and does not advance ──────────────
static void testRdyHoldsExactReadPhase() {
    using namespace ArpSID::C64;
    g_mem.fill(0xEAu);
    // $1000: LDA #$42 ; $1002: LDX #$99
    g_mem[0x1000] = 0xA9u; g_mem[0x1001] = 0x42u;
    g_mem[0x1002] = 0xA2u; g_mem[0x1003] = 0x99u;

    Cpu6510Micro cpu;
    cpu.powerOn();
    cpu.setPc(0x1000u);

    // Pull RDY low and tick many times — the opcode fetch must be held at the
    // exact PC address and the CPU must not advance.
    cpu.setRdy(false);
    for (int i = 0; i < 16; ++i) {
        const CpuBusRequest req = stepMicro(cpu);
        require(req.active() && !req.isWrite(), "RDY-low cycle issues a (held) read");
        require(req.address == 0x1000u, "RDY-low holds the exact opcode-fetch address");
        require(cpu.pc() == 0x1000u, "RDY-low does not advance PC");
        require(cpu.opcode() == 0u, "RDY-low does not latch an opcode");
        require(cpu.a() == 0u, "RDY-low does not execute the instruction");
    }

    // Release RDY: the held LDA #$42 now completes (2 cycles).
    cpu.setRdy(true);
    stepMicro(cpu); // opcode fetch commits
    stepMicro(cpu); // immediate operand read → A = $42
    require(cpu.a() == 0x42u, "RDY-high resumes and completes LDA #$42");
    require(cpu.pc() == 0x1002u, "PC advanced past LDA after resume");
}

// ── II. RDY does not stall write cycles ───────────────────────────────────────
static void testRdyDoesNotStallWrites() {
    using namespace ArpSID::C64;
    g_mem.fill(0xEAu);
    // $2000: LDA #$7E ; STA $2500
    g_mem[0x2000] = 0xA9u; g_mem[0x2001] = 0x7Eu;
    g_mem[0x2002] = 0x8Du; g_mem[0x2003] = 0x00u; g_mem[0x2004] = 0x25u;

    Cpu6510Micro cpu;
    cpu.powerOn();
    cpu.setPc(0x2000u);
    // Run LDA + STA with RDY high to reach and execute the write.
    for (int i = 0; i < 6; ++i) stepMicro(cpu);
    require(g_mem[0x2500] == 0x7Eu, "STA wrote with RDY high");

    // Prove a write cycle proceeds even when RDY is low DURING the write.
    // RDY-low stalls the reads that precede the write, so the only physically
    // correct way to test "writes don't stall" is to complete the preceding
    // reads with RDY high, then pull RDY low on the write cycle itself.
    //
    // LDA #$55 (2 cycles: fetch, operand) + STA $2600 (4 cycles: fetch, lo, hi,
    // write) → the write is the 6th cycle. Run 5 read cycles RDY-high, then
    // drop RDY for the write cycle.
    g_mem[0x2600] = 0x00u;
    cpu.powerOn();
    cpu.setPc(0x2010u);
    g_mem[0x2010] = 0xA9u; g_mem[0x2011] = 0x55u;       // LDA #$55
    g_mem[0x2012] = 0x8Du; g_mem[0x2013] = 0x00u; g_mem[0x2014] = 0x26u; // STA $2600

    cpu.setRdy(true);
    for (int i = 0; i < 5; ++i) stepMicro(cpu); // LDA(2) + STA fetch/lo/hi(3)
    require(g_mem[0x2600] == 0x00u, "write has not happened yet before the write cycle");

    cpu.setRdy(false); // RDY low exactly on the write cycle
    const CpuBusRequest wreq = stepMicro(cpu);
    require(wreq.active() && wreq.isWrite() && wreq.address == 0x2600u,
            "the write cycle issues a write to $2600 even with RDY low");
    require(g_mem[0x2600] == 0x55u, "STA value written with RDY low (writes never stall)");
}

// ── III. Standalone powerOn installs deterministic KERNAL vectors ─────────────
static void testStandalonePowerOnInstallsVectors() {
    using namespace ArpSID::C64;
    C64Phi2Machine m;
    Phi2MachineConfig cfg{};
    cfg.video = MachineVideoStandard::PAL;
    m.configure(cfg);
    m.powerOn();

    const auto& mem = m.memory();
    // RESET vector -> $E000 HLE reset stub
    require(mem.peekKernalRom(0xFFFCu) == 0x00u && mem.peekKernalRom(0xFFFDu) == 0xE0u,
            "powerOn installs RESET vector -> $E000");
    // NMI vector -> $FE43
    require(mem.peekKernalRom(0xFFFAu) == 0x43u && mem.peekKernalRom(0xFFFBu) == 0xFEu,
            "powerOn installs NMI vector -> $FE43");
    // IRQ/BRK vector -> $FF48
    require(mem.peekKernalRom(0xFFFEu) == 0x48u && mem.peekKernalRom(0xFFFFu) == 0xFFu,
            "powerOn installs IRQ vector -> $FF48");
    // HLE handlers
    require(mem.peekKernalRom(0xFF48u) == 0x48u && mem.peekKernalRom(0xFF4Du) == 0x6Cu,
            "IRQ handler saves registers then jumps through CINV");
    require(mem.peekKernalRom(0xFE43u) == 0x6Cu, "NMI handler jumps through NMINV");
    require(mem.peekKernalRom(0xE000u) == 0x78u, "RESET handler begins with SEI");

    // Reset-to-vector then a few cycles: PC must reach the deterministic reset
    // handler region (not a garbage NOP-fill address).
    m.resetToVector();
    m.runPhi2(10);
    require(m.cpu().pc() >= 0xE000u && m.cpu().pc() <= 0xE020u,
            "reset sequence lands in the deterministic HLE reset handler");
}

int main() {
    testRdyHoldsExactReadPhase();
    testRdyDoesNotStallWrites();
    testStandalonePowerOnInstallsVectors();
    std::cout << "C64Phi2RdyVectorsV606Tests PASS\n";
    return 0;
}
