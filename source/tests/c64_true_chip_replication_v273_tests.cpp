#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_projection_bridge.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void req(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

struct Sink final : SidRegisterSink {
    uint8_t lastReg = 0xff;
    uint8_t lastVal = 0xff;
    int writes = 0;
    void sidWrite(uint8_t reg, uint8_t value, uint64_t) noexcept override {
        lastReg = reg; lastVal = value; ++writes;
    }
    uint8_t sidRead(uint8_t reg, uint64_t) noexcept override { return reg == lastReg ? lastVal : 0x5a; }
};

static void testPlaBankingAndCpuPort() {
    C64Platform c64;
    c64.reset(true);
    c64.roms().pokeBasic(0xA000, 0xB4);
    c64.roms().pokeKernal(0xE000, 0xE4);
    c64.roms().pokeCharacter(0xD000, 0xC4);
    c64.pokeMemory(0xA000, 0x11);
    c64.pokeMemory(0xD000, 0x22);
    c64.pokeMemory(0xE000, 0x33);

    req(c64.visibleDevice(0xA000) == C64VisibleDevice::BasicRom, "default CPU port exposes BASIC ROM");
    req(c64.visibleDevice(0xD000) == C64VisibleDevice::Io, "default CPU port exposes IO at $D000");
    req(c64.visibleDevice(0xE000) == C64VisibleDevice::KernalRom, "default CPU port exposes KERNAL ROM");
    req(c64.cpuRead(0xA000) == 0xB4, "BASIC ROM read wins over RAM");
    req(c64.cpuRead(0xE000) == 0xE4, "KERNAL ROM read wins over RAM");

    c64.cpuWrite(0x0001, 0x33); // LORAM/HIRAM set, CHAREN clear
    req(c64.visibleDevice(0xD000) == C64VisibleDevice::CharacterRom, "CHAREN=0 exposes character ROM under $D000");
    req(c64.cpuRead(0xD000) == 0xC4, "character ROM read wins over RAM/IO when CHAREN=0");

    c64.cpuWrite(0x0001, 0x30); // LORAM/HIRAM/CHAREN low -> all RAM in ROM/IO windows
    req(c64.visibleDevice(0xA000) == C64VisibleDevice::Ram, "LORAM/HIRAM low exposes RAM at BASIC window");
    req(c64.visibleDevice(0xD000) == C64VisibleDevice::Ram, "LORAM/HIRAM low exposes RAM at IO window");
    req(c64.visibleDevice(0xE000) == C64VisibleDevice::Ram, "HIRAM low exposes RAM at KERNAL window");
    req(c64.cpuRead(0xA000) == 0x11, "RAM-under-BASIC preserved");
    req(c64.cpuRead(0xD000) == 0x22, "RAM-under-IO preserved");
    req(c64.cpuRead(0xE000) == 0x33, "RAM-under-KERNAL preserved");
}

static void testSidIoHiddenByPlaDoesNotWriteSid() {
    C64Platform c64;
    Sink sink;
    c64.reset(true);
    c64.attachSid(&sink);
    c64.cpuWrite(0x0001, 0x30); // IO hidden, RAM visible at $D400
    c64.cpuWrite(0xD400, 0x77);
    req(sink.writes == 0, "hidden IO does not forward $D400 write to SID sink");
    req(c64.sidRegisterImage()[0] == 0x00, "hidden IO does not mutate SID register image");
    req(c64.peekMemory(0xD400) == 0x77, "hidden IO write lands in RAM under IO");

    c64.cpuWrite(0x0001, 0x37); // IO visible again
    c64.cpuWrite(0xD400, 0x88);
    req(sink.writes == 1, "visible IO forwards $D400 write to SID sink");
    req(c64.sidRegisterImage()[0] == 0x88, "visible IO updates single SID mirror authority");
    req(c64.peekMemory(0xD400) == 0x77, "visible IO does not overwrite RAM under IO");
}

static void testVicBadlineCpuStallBeforeInstruction() {
    C64Platform c64;
    c64.reset(true);
    c64.cpuWrite(0xD011, 0x10); // display enabled, yscroll=0 -> badlines on raster % 8 == 0
    c64.runCycles(static_cast<uint64_t>(VicII::kPalCyclesPerLine) * 0x30u + 16u);
    req(c64.vic().badline(), "VIC reports badline in display area");
    req(!c64.vic().cpuCanUseBus(), "VIC BA/AEC steals CPU bus in character-fetch window");

    c64.pokeMemory(0x0200, 0xEA); // NOP
    c64.cpu().state().pc = 0x0200;
    const uint64_t beforePhi2 = c64.phi2Cycle();
    c64.executeInstruction();
    req(c64.phi2Cycle() > beforePhi2 + 2u, "executeInstruction stalls through VIC bus-steal before CPU opcode completes");
    req(c64.cpu().state().pc == 0x0201, "CPU still executes the pending opcode after BA/AEC release");
}

static void testProjectionBridgeRespectsPlaVisibility() {
    C64Platform c64;
    Sink sink;
    c64.reset(true);
    c64.attachSid(&sink);
    c64.cpuWrite(0x0001, 0x30); // hide IO
    const bool queued = projectSidTimedWriteThroughC64Bus(c64, 0x00, 0x44, 0);
    req(queued, "projection write queues even when IO visibility later decides destination");
    c64.runCycles(1);
    req(c64.sidRegisterImage()[0] == 0x00, "projection bridge does not bypass PLA when IO is hidden");
    req(c64.peekMemory(0xD400) == 0x44, "projection bridge writes RAM-under-IO when PLA hides SID");

    c64.cpuWrite(0x0001, 0x37);
    req(projectSidTimedWriteThroughC64Bus(c64, 0x00, 0x55, 0), "projection write queues with IO visible");
    c64.runCycles(1);
    req(c64.sidRegisterImage()[0] == 0x55, "projection bridge reaches SID only through visible $D400 bus");
}

int main() {
    testPlaBankingAndCpuPort();
    testSidIoHiddenByPlaDoesNotWriteSid();
    testVicBadlineCpuStallBeforeInstruction();
    testProjectionBridgeRespectsPlaVisibility();
    std::cout << "C64TrueChipReplicationV273Tests PASS\n";
    return 0;
}
