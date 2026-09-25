#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_d418_capture.h"
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_processor_port.h"
#include "arpsid/core/c64_sid_bus_sink.h"
#include "arpsid/core/c64_vic.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;

    // Phase 1 / TS MemoryBank.write contract: offline changed-register traces may
    // filter unchanged values, but $D418 must bypass the changed filter because
    // repeated identical volume nibbles are real PCM samples.
    require(!c64SidWriteShouldEnterTrace(0x04u, false), "unchanged non-D418 register may be filtered");
    require(c64SidWriteShouldEnterTrace(0x04u, true), "changed non-D418 register enters trace");
    require(c64SidWriteShouldEnterTrace(0x18u, false), "$D418 enters trace even when value is unchanged");
    require(c64SidWriteShouldEnterTrace(0x38u, false), "$D418 mirror/encoded reg enters trace even when unchanged");

    C64Phi2Machine m;
    SidBusSink sink;
    m.powerOn();
    m.attachSidSink(&sink);
    // C64 reset processor port: IO visible, so $D418 is a SID write, not RAM.
    m.port().write(0x0000u, 0x2Fu);
    m.port().write(0x0001u, 0x37u);
    Phi2BusPhase phase{};
    m.memory().cpuWrite(1000u, 0xD418u, 0x07u, phase, false);
    m.memory().cpuWrite(1001u, 0xD418u, 0x07u, phase, false);
    m.memory().cpuWrite(1002u, 0xD418u, 0x0Fu, phase, false);
    require(sink.writeCount() == 3u, "PHI2 memory matrix preserves repeated identical $D418 writes");
    require(sink.write(0).phi2 == 1000u && sink.write(1).phi2 == 1001u && sink.write(2).phi2 == 1002u,
            "$D418 writes carry absolute PHI2 timestamps");

    C64SidBridgeState bridge;
    bridge.sidWrite(0x18u, 0x07u, 1000u);
    bridge.sidWrite(0x18u, 0x07u, 1001u);
    bridge.sidWrite(0x18u, 0x0Fu, 1002u);
    D418CaptureSample samples[4]{};
    const size_t sampleCount = extractD418Samples(bridge, samples, 4, 1000u, 1000u);
    require(sampleCount == 3u, "D418 extractor keeps repeated identical samples");
    require(samples[0].hostFrame == 1000u && samples[1].hostFrame == 1001u && samples[2].hostFrame == 1002u,
            "D418 extractor maps absolute PHI2 to host frames deterministically");
    require(std::fabs(samples[0].unit - (((7.0f / 15.0f) * 2.0f) - 1.0f)) < 0.0001f,
            "$D418 low nibble maps to bipolar 4-bit PCM");

    // Phase 2 / MemoryBank contract: $01 floating inputs and color-RAM upper
    // nibble from open bus.
    ProcessorPort6510 port;
    port.powerOn();
    port.write(0x0000u, 0x00u); // all low six bits inputs/floating high-on-C64
    port.write(0x0001u, 0x00u);
    require(port.read(0x0001u, 0x00u) == 0xFFu, "$01 input bits float high with high two bits set");
    port.write(0x0000u, 0x07u);
    port.write(0x0001u, 0x04u);
    require((port.read(0x0001u, 0x00u) & 0x07u) == 0x04u, "$01 driven LORAM/HIRAM/CHAREN bits honour DDR/data");

    C64Phi2Machine color;
    color.powerOn();
    color.port().write(0x0000u, 0x2Fu);
    color.port().write(0x0001u, 0x37u);
    color.memory().cpuWrite(2000u, 0x0400u, 0xA0u, phase, false); // drive open bus high nibble A
    color.memory().cpuWrite(2001u, 0xD800u, 0x05u, phase, false);
    const uint8_t colorRead = color.memory().cpuRead(2002u, 0xD800u, phase);
    require((colorRead & 0x0Fu) == 0x05u, "color RAM preserves low nibble");
    require((colorRead & 0xF0u) == ((color.openBus().value() & 0xF0u)), "color RAM upper nibble comes from open bus");

    // Phase 3 / CIA ICR: bit7 synthesized, read clears flags and deasserts IRQ,
    // mask writes re-evaluate level immediately.
    Cia6526 cia;
    cia.reset();
    cia.write(0x04u, 0x01u);
    cia.write(0x05u, 0x00u);
    cia.write(0x0Du, 0x81u); // enable Timer A IRQ
    cia.write(0x0Eu, 0x11u); // force load + start
    cia.step(2u);
    require(cia.irq(), "CIA Timer A underflow asserts masked IRQ level");
    const uint8_t icr = cia.read(0x0Du);
    require((icr & 0x81u) == 0x81u, "CIA ICR read synthesizes bit7 from flags & mask");
    require(!cia.irq(), "CIA ICR read clears flags and deasserts IRQ level");
    require(cia.read(0x0Du) == 0x00u, "CIA ICR second read observes cleared flags");

    Cia6526 ciaMask;
    ciaMask.reset();
    ciaMask.write(0x04u, 0x01u);
    ciaMask.write(0x05u, 0x00u);
    ciaMask.write(0x0Eu, 0x11u);
    ciaMask.step(2u);
    require(!ciaMask.irq(), "unmasked CIA event does not assert IRQ level");
    ciaMask.write(0x0Du, 0x81u);
    require(ciaMask.irq(), "CIA mask-write immediately re-evaluates pending flag to IRQ level");

    // Phase 5 / VIC: 9-bit raster compare, D019 ACK and badline 40-cycle steal.
    VicII vic;
    vic.reset(true);
    vic.write(0x1Au, 0x01u);
    vic.write(0x11u, 0x80u);
    vic.write(0x12u, 0x00u);
    const uint32_t onePalFrame = VicII::kPalCyclesPerLine * VicII::kPalRasterLines;
    for (uint32_t i = 0; i < onePalFrame; ++i) {
        if ((vic.read(0x19u) & 0x81u) == 0x81u) break;
        vic.tick();
    }
    require((vic.read(0x19u) & 0x81u) == 0x81u, "VIC 9-bit raster compare uses $D011 bit7 + $D012");
    vic.write(0x19u, 0x01u);
    require(!vic.irq(), "VIC $D019 write-one ACK clears raster IRQ");

    VicII bad;
    bad.reset(true);
    bad.write(0x11u, 0x10u); // DEN on, yscroll 0
    for (uint32_t i = 0; i < 0x30u * VicII::kPalCyclesPerLine; ++i) bad.tick();
    require(bad.rasterLine() == 0x30u, "badline test is positioned at first PAL badline");
    require(bad.previewStolen(VicII::kPalCyclesPerLine) == 40u, "VIC badline steals exactly 40 PHI2 cycles in current model");

    // Phase 6 / HLE vectors: PSID-safe no-ROM surface must contain CINV/NMINV
    // indirection and CIA ACK helpers.
    C64Phi2Machine hle;
    hle.powerOn();
    require(hle.memory().peekKernalRom(0xFFFEu) == 0x48u && hle.memory().peekKernalRom(0xFFFFu) == 0xFFu,
            "HLE IRQ vector points to $FF48");
    require(hle.memory().peekKernalRom(0xFF48u) == 0x48u && hle.memory().peekKernalRom(0xFF4Du) == 0x6Cu,
            "HLE IRQ entry saves A/X/Y then JMP ($0314)");
    require(hle.memory().peekKernalRom(0xEA31u) == 0xADu && hle.memory().peekKernalRom(0xEA33u) == 0xDCu,
            "HLE default IRQ ACK reads CIA1 ICR");
    require(hle.memory().peekKernalRom(0xFE47u) == 0xADu && hle.memory().peekKernalRom(0xFE49u) == 0xDDu,
            "HLE default NMI ACK reads CIA2 ICR");
    require(hle.memory().peekKernalRom(0xFF8Du) == 0xA2u && hle.memory().peekKernalRom(0xFF98u) == 0x60u,
            "HLE RESTOR copies vectors $FD30->$0314 and returns");

    std::cout << "C64TsReferenceIntegrationV735Tests PASS\n";
    return 0;
}
