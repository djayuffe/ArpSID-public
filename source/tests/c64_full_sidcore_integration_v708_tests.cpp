#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_d418_capture.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_memory_matrix.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID::C64;

    // CIA exact decrement-then-underflow: latch=1 needs two clocks.
    Cia6526 cia;
    cia.reset();
    cia.write(0x04, 0x01);
    cia.write(0x05, 0x00);
    cia.write(0x0D, 0x81);
    cia.write(0x0E, 0x11);
    require(!cia.tick(), "CIA latch=1 first PHI2 only decrements to zero");
    require(cia.timerPhaseSnapshot().timerAUnderflows == 0u, "no premature CIA underflow at timer==1");
    require(cia.tick(), "CIA second PHI2 decrements zero to $FFFF and underflows");
    require((cia.read(0x0D) & 0x81u) == 0x81u, "CIA ICR bit7 is synthesized on read and flags read-clear");
    require(!cia.irq(), "CIA ICR read deasserts IRQ line");

    // Unwired CNT decode: mode 1/3 must not silently tick from PHI2.
    cia.reset();
    cia.write(0x04, 0x00); cia.write(0x05, 0x00);
    cia.write(0x0D, 0x81);
    cia.write(0x0E, 0x31); // start + force-load + Timer A CNT source
    cia.step(32, 0, false);
    require(cia.timerPhaseSnapshot().timerAUnderflows == 0u, "CIA Timer A CNT mode does not tick from PHI2 when CNT is unwired");

    // TOD BCD alarm path and hour normalization.
    cia.reset();
    cia.write(0x0F, 0x80);
    cia.write(0x08, 0x01); cia.write(0x09, 0x00); cia.write(0x0A, 0x00); cia.write(0x0B, 0x99);
    auto snap = cia.timerPhaseSnapshot();
    require(snap.todAlarmHours == 0x92u, "CIA TOD hour writes normalize to BCD 1..12 with PM bit separated");

    // $D418 bridge must keep repeated identical writes with absolute PHI2.
    C64SidBridgeState bridge;
    bridge.reset();
    bridge.sidWrite(0x18, 0x0A, 100);
    bridge.sidWrite(0x18, 0x0A, 120);
    bridge.sidWrite(0x18, 0x0B, 140);
    require(bridge.d418WriteCount == 3u, "$D418 bridge counts every volume-register write");
    require(bridge.d418RepeatedValueWriteCount == 1u, "$D418 bridge explicitly counts repeated identical samples");
    D418CaptureSample samples[4]{};
    const size_t n = extractD418Samples(bridge, samples, 4, 1000u, 1000u);
    require(n == 3u && samples[0].hostFrame == 100u && samples[1].hostFrame == 120u,
            "$D418 extraction preserves absolute PHI2 timestamps");

    // Open bus persistence and color upper nibble are preserved in the memory matrix.
    ProcessorPort6510 port;
    OpenBusLatch bus;
    MemoryMatrix matrix;
    matrix.attach(&port, &bus, nullptr);
    matrix.powerOn(true);
    bus.drive(0xA0u, 0);
    matrix.pokeColorRam(0xD800u, 0x05u);
    Phi2BusPhase phase{};
    require(matrix.cpuRead(1, 0xD800u, phase) == 0xA5u,
            "color RAM upper nibble comes from open bus");

    // HLE KERNAL surface: IRQ entry saves A/X/Y then jumps through CINV; RESTOR exists.
    C64Platform platform;
    platform.reset(true);
    platform.installPsidSafeVectors();
    require(platform.peekMemory(0xFF48) == 0x48u && platform.peekMemory(0xFF4D) == 0x6Cu,
            "C64Platform HLE IRQ entry is PHA/TXA/PHA/TYA/PHA/JMP ($0314)");
    require(platform.peekMemory(0xFF8D) == 0xA2u && platform.peekMemory(0xFF98) == 0x60u,
            "C64Platform HLE RESTOR copies vector table and returns");
    require(platform.peekMemory(0xE000) == 0x78u && platform.peekMemory(0xE011) == 0x60u,
            "C64Platform HLE reset stub sets machine surface and returns");

    std::cout << "C64FullSidcoreIntegrationV708Tests PASS\n";
    return 0;
}
