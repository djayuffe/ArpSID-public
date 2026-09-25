#include "arpsid/core/c64_6510.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

struct TraceBus final : Mos6510Bus {
    std::array<uint8_t, 65536> mem{};
    std::vector<uint16_t> reads;
    std::vector<std::pair<uint16_t,uint8_t>> writes;
    uint8_t cpuRead(uint16_t a) noexcept override { reads.push_back(a); return mem[a]; }
    void cpuWrite(uint16_t a, uint8_t v) noexcept override { writes.emplace_back(a,v); mem[a]=v; }
    void clearTrace() { reads.clear(); writes.clear(); }
};

static void testRmwDummyWriteOfficialAndIllegal() {
    TraceBus bus;
    Mos6510 cpu;
    cpu.reset(0x0200);
    bus.mem[0x0200] = 0xE6; // INC $44
    bus.mem[0x0201] = 0x44;
    bus.mem[0x0044] = 0x7F;
    const uint8_t c1 = cpu.stepInstruction(bus);
    require(c1 == 5, "INC zp cycle count");
    require(bus.mem[0x0044] == 0x80, "INC writes incremented value");
    require(bus.writes.size() == 2, "official RMW emits dummy write plus final write");
    require(bus.writes[0].first == 0x0044 && bus.writes[0].second == 0x7F, "official RMW dummy-writes old value");
    require(bus.writes[1].first == 0x0044 && bus.writes[1].second == 0x80, "official RMW final-writes new value");

    bus.clearTrace();
    cpu.reset(0x0300);
    cpu.state().a = 0x01;
    bus.mem[0x0300] = 0x07; // SLO $55: ASL mem then ORA A
    bus.mem[0x0301] = 0x55;
    bus.mem[0x0055] = 0x40;
    const uint8_t c2 = cpu.stepInstruction(bus);
    require(c2 == 5, "SLO zp cycle count");
    require(bus.mem[0x0055] == 0x80, "SLO stores shifted memory value");
    require(cpu.state().a == 0x81, "SLO ORs shifted value into A");
    require(bus.writes.size() == 2, "illegal RMW emits dummy write plus final write");
    require(bus.writes[0].second == 0x40 && bus.writes[1].second == 0x80, "illegal RMW dummy/final write values");
}

static void testPageCrossDummyReadForReadOpcodes() {
    TraceBus bus;
    Mos6510 cpu;
    cpu.reset(0x0400);
    cpu.state().x = 1;
    bus.mem[0x0400] = 0xBD; // LDA $12FF,X => effective $1300, dummy read $1200
    bus.mem[0x0401] = 0xFF;
    bus.mem[0x0402] = 0x12;
    bus.mem[0x1300] = 0x5A;
    const uint8_t c = cpu.stepInstruction(bus);
    require(c == 5, "LDA abs,X page-cross cycle count");
    require(cpu.state().a == 0x5A, "LDA abs,X reads effective address");
    bool sawDummy = false;
    for (uint16_t r : bus.reads) if (r == 0x1200) sawDummy = true;
    require(sawDummy, "page-cross read performs NMOS dummy read from wrapped high-byte address");
}

static void testExpandedIllegalOpcodeSet() {
    TraceBus bus;
    Mos6510 cpu;
    cpu.reset(0x0500);
    cpu.state().a = 0xFF;
    cpu.state().x = 0x10;
    bus.mem[0x0500] = 0xD7; // DCP $20,X => DEC $30 then CMP A
    bus.mem[0x0501] = 0x20;
    bus.mem[0x0030] = 0x22;
    const uint8_t dcpCycles = cpu.stepInstruction(bus);
    require(dcpCycles == 6, "DCP zp,X cycle count");
    require(bus.mem[0x0030] == 0x21, "DCP zp,X decrements memory");
    require((cpu.state().p & Mos6510::C) != 0, "DCP compares decremented value with A");

    bus.clearTrace();
    cpu.reset(0x0600);
    cpu.state().a = 0xF0;
    cpu.state().x = 0x0F;
    bus.mem[0x0600] = 0xCB; // AXS #$01 => X=(A&X)-imm
    bus.mem[0x0601] = 0x01;
    const uint8_t axsCycles = cpu.stepInstruction(bus);
    require(axsCycles == 2, "AXS immediate cycle count");
    require(cpu.state().x == 0xFF, "AXS updates X from A&X minus immediate with borrow wrap");
    require((cpu.state().p & Mos6510::C) == 0, "AXS clears carry on borrow");

    bus.clearTrace();
    cpu.reset(0x0700);
    cpu.state().a = 0xFF;
    cpu.state().x = 0xAA;
    cpu.state().y = 0x02;
    bus.mem[0x0700] = 0x9F; // AHX $2000,Y => write A&X&(high+1)
    bus.mem[0x0701] = 0x00;
    bus.mem[0x0702] = 0x20;
    const uint8_t ahxCycles = cpu.stepInstruction(bus);
    require(ahxCycles == 5, "AHX abs,Y cycle count");
    require(bus.mem[0x2002] == (0xFF & 0xAA & 0x21), "AHX writes high-byte masked A&X");
}

static void testRmwClassifierCoversOfficialAndIllegal() {
    require(c64Mos6510OpcodeIsReadModifyWrite(0xE6), "RMW classifier covers INC zp");
    require(c64Mos6510OpcodeIsReadModifyWrite(0x1F), "RMW classifier covers SLO abs,X");
    require(c64Mos6510OpcodeIsReadModifyWrite(0xD7), "RMW classifier covers DCP zp,X");
    require(!c64Mos6510OpcodeIsReadModifyWrite(0xA9), "RMW classifier rejects LDA immediate");
}

int main() {
    testRmwDummyWriteOfficialAndIllegal();
    testPageCrossDummyReadForReadOpcodes();
    testExpandedIllegalOpcodeSet();
    testRmwClassifierCoversOfficialAndIllegal();
    std::cout << "C64Cpu6510CompleteV414 runtime checks passed\n";
    return 0;
}
