#include "arpsid/core/c64_6510.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

class TinyBus final : public Mos6510Bus {
public:
    uint8_t mem[65536]{};
    uint8_t cpuRead(uint16_t a) noexcept override { return mem[a]; }
    void cpuWrite(uint16_t a, uint8_t v) noexcept override { mem[a] = v; }
};

static void runApprox(uint8_t opcode, const char* name) {
    TinyBus bus{};
    Mos6510 cpu{};
    cpu.reset(0x0800u);
    cpu.state().a = 0xF0u;
    cpu.state().x = 0x0Fu;
    bus.mem[0x0800] = opcode;
    bus.mem[0x0801] = 0xAAu;
    (void)cpu.stepInstruction(bus);
    require(cpu.state().approximateIllegalOpcodeCount == 1u, name);
    require(cpu.state().lastTickUsedApproximateIllegal, "last tick approximate flag set");
    require(cpu.state().lastApproximateIllegalOpcode == opcode, "last approximate opcode recorded");
}

int main() {
    runApprox(0x6Bu, "ARR marks legacy approximate opcode");
    runApprox(0x8Bu, "XAA marks legacy approximate opcode");
    std::cout << "C64LegacyApproximateOpcodeLedgerV721Tests PASS\n";
    return 0;
}
