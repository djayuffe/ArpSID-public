// cpu6510_micro_opcode_matrix_v853_tests.cpp
//
// Exhaustive 6510 micro-CPU opcode matrix + cycle-timing audit.
//
// A. Enumerates ALL 256 opcodes against the bus-cycle-accurate Cpu6510Micro:
//    every opcode must either retire normally or jam as a documented KIL —
//    an UnsupportedOpcode jam is a CPU implementation gap (a real playback flaw
//    for SID tunes using NMOS illegal opcodes) and fails this test.
// B. Verifies documented NMOS 6502/6510 cycle counts for a representative set of
//    official instructions across addressing modes, including the page-cross
//    penalty and branch taken/not-taken/page-cross timing.
//
// The harness drives tickPhi2Begin()/tickPhi2End() against a flat 64 KiB RAM,
// exactly one PHI2 cycle per pair, so the measured counts are true bus cycles.

#include "arpsid/core/c64_cpu6510_micro.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ArpSID::C64;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

namespace {

struct MicroHarness {
    Cpu6510Micro cpu;
    std::array<uint8_t, 65536> ram{};

    void reset(uint16_t pc) {
        cpu.powerOn();
        cpu.setPc(pc);
    }
    // Run exactly one PHI2 cycle. Returns the bus request that was serviced.
    CpuBusRequest tick() {
        const CpuBusRequest rq = cpu.tickPhi2Begin();
        uint8_t data = 0xFFu;
        if (rq.active()) {
            if (rq.isWrite()) ram[rq.address] = rq.dataOut;
            else data = ram[rq.address];
        }
        cpu.tickPhi2End(data);
        return rq;
    }
    // Run until `count` instructions retire or the CPU jams. Returns cycles used.
    int runInstructions(int count, int cycleBudget = 64) {
        const uint64_t start = cpu.retiredInstructionCount();
        int cycles = 0;
        while (cycles < cycleBudget) {
            tick();
            ++cycles;
            if (cpu.state().jammed) return cycles;
            if (cpu.retiredInstructionCount() >= start + (uint64_t)count) return cycles;
        }
        return cycles;
    }
};

constexpr bool isDocumentedKil(uint8_t op) {
    switch (op) {
        case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
        case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
            return true;
        default: return false;
    }
}

} // namespace

int main() {
    // ── A. Full opcode matrix — no UnsupportedOpcode gaps. ──────────────────
    {
        int retired = 0, kil = 0, unsupported = 0;
        for (int opi = 0; opi < 256; ++opi) {
            const uint8_t op = (uint8_t)opi;
            MicroHarness h;
            h.reset(0x1000);
            // opcode + two harmless operand bytes; zp pointers land in zeroed RAM.
            h.ram[0x1000] = op; h.ram[0x1001] = 0x20; h.ram[0x1002] = 0x11;
            // Give indirect vectors a sane target too.
            h.ram[0x0020] = 0x00; h.ram[0x0021] = 0x30;
            (void)h.runInstructions(1, 64);

            if (h.cpu.state().jammed) {
                const CpuJamReason r = h.cpu.lastJamReason();
                if (r == CpuJamReason::UnsupportedOpcode) {
                    std::printf("GAP: opcode %02X jams as UnsupportedOpcode\n", op);
                    ++unsupported;
                } else {
                    require(isDocumentedKil(op),
                            "only documented KIL opcodes may halt the CPU");
                    ++kil;
                }
            } else {
                require(h.cpu.retiredInstructionCount() >= 1,
                        "non-jammed opcode retires within budget");
                ++retired;
            }
        }
        std::printf("[6510 matrix] retired=%d kil=%d unsupported=%d\n", retired, kil, unsupported);
        require(unsupported == 0, "all 256 opcodes are implemented (no UnsupportedOpcode gap)");
        require(kil == 12, "exactly the 12 documented KIL opcodes halt");
        require(retired == 244, "the remaining 244 opcodes execute");
    }

    // ── B. Documented cycle counts (bus-cycle-accurate timing audit). ───────
    {
        struct TimingCase { const char* name; std::vector<uint8_t> code; int cycles; };
        const TimingCase cases[] = {
            {"LDA #imm (2)",        {0xA9, 0x42},             2},
            {"LDA zp (3)",          {0xA5, 0x20},             3},
            {"LDA zp,X (4)",        {0xB5, 0x20},             4},
            {"LDA abs (4)",         {0xAD, 0x00, 0x30},       4},
            {"LDA abs,X no-cross (4)", {0xBD, 0x00, 0x30},    4},
            {"STA abs,X always-5 (5)", {0x9D, 0x00, 0x30},    5},
            {"LDA (zp,X) (6)",      {0xA1, 0x20},             6},
            {"LDA (zp),Y no-cross (5)", {0xB1, 0x20},         5},
            {"ASL zp RMW (5)",      {0x06, 0x20},             5},
            {"ASL abs RMW (6)",     {0x0E, 0x00, 0x30},       6},
            {"INC abs,X RMW (7)",   {0xFE, 0x00, 0x30},       7},
            {"JMP abs (3)",         {0x4C, 0x00, 0x30},       3},
            {"JMP (ind) (5)",       {0x6C, 0x20, 0x00},       5},
            {"JSR (6)",             {0x20, 0x00, 0x30},       6},
            {"NOP (2)",             {0xEA},                   2},
            {"PHA (3)",             {0x48},                   3},
            {"PLA (4)",             {0x68},                   4},
            {"SLO zp illegal RMW (5)", {0x07, 0x20},          5},
            {"LAX zp illegal (3)",  {0xA7, 0x20},             3},
        };
        for (const auto& tc : cases) {
            MicroHarness h;
            h.reset(0x1000);
            for (size_t i = 0; i < tc.code.size(); ++i) h.ram[0x1000 + i] = tc.code[i];
            h.ram[0x0020] = 0x00; h.ram[0x0021] = 0x30;
            const int used = h.runInstructions(1, 32);
            if (used != tc.cycles) {
                std::printf("TIMING: %s took %d cycles, expected %d\n", tc.name, used, tc.cycles);
                ++g_failures;
            }
        }

        // Page-cross read penalty: LDA abs,X crossing a page = 5 cycles.
        {
            MicroHarness h; h.reset(0x1000);
            h.ram[0x1000]=0xA2; h.ram[0x1001]=0xFF;              // LDX #$FF (2)
            h.ram[0x1002]=0xBD; h.ram[0x1003]=0x01; h.ram[0x1004]=0x30; // LDA $3001,X -> $3100 cross (5)
            const int used = h.runInstructions(2, 32);
            require(used == 2 + 5, "LDA abs,X page-cross costs the +1 penalty (5 cycles)");
        }
        // Branch timing: not-taken 2; taken same-page 3; taken page-cross 4.
        {
            MicroHarness h; h.reset(0x1000);
            h.ram[0x1000]=0x18;                                   // CLC (2)
            h.ram[0x1001]=0xB0; h.ram[0x1002]=0x10;               // BCS +16 not taken (2)
            const int used = h.runInstructions(2, 16);
            require(used == 2 + 2, "branch not taken costs 2 cycles");
        }
        {
            MicroHarness h; h.reset(0x1000);
            h.ram[0x1000]=0x38;                                   // SEC (2)
            h.ram[0x1001]=0xB0; h.ram[0x1002]=0x10;               // BCS +16 taken same page (3)
            const int used = h.runInstructions(2, 16);
            require(used == 2 + 3, "branch taken same-page costs 3 cycles");
        }
        {
            MicroHarness h; h.reset(0x10F0);
            h.ram[0x10F0]=0x38;                                   // SEC (2)
            h.ram[0x10F1]=0xB0; h.ram[0x10F2]=0x7F;               // BCS +127 -> crosses into $1172 (4)
            const int used = h.runInstructions(2, 16);
            require(used == 2 + 4, "branch taken page-cross costs 4 cycles");
        }
        // BRK (with trapBrkAsJam disabled) = 7 cycles into the IRQ vector.
        {
            MicroHarness h; h.reset(0x1000);
            h.ram[0x1000]=0x00; h.ram[0x1001]=0x00;               // BRK
            h.ram[0xFFFE]=0x00; h.ram[0xFFFF]=0x30;               // IRQ vector -> $3000
            const int used = h.runInstructions(1, 16);
            require(used == 7, "BRK costs 7 cycles");
            require(h.cpu.pc() == 0x3000, "BRK vectors through $FFFE/$FFFF");
        }
    }

    if (g_failures == 0) { std::printf("cpu6510_micro_opcode_matrix_v853_tests: PASS\n"); return 0; }
    std::printf("cpu6510_micro_opcode_matrix_v853_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
