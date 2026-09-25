#include "arpsid/core/c64_cia.h"
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

    Cia6526 cia;
    cia.reset();

    // Force-load must load latch without setting bit 4 sticky.
    cia.write(0x04, 0x34);
    cia.write(0x05, 0x12);
    cia.write(0x0E, 0x10);
    require(cia.timerA() == 0x1234u, "CRA force-load loads Timer A from latch");
    require((cia.read(0x0E) & 0x10u) == 0u, "CRA force-load bit self-clears");

    // Timer A one-shot underflow: IRQ flag, edge count, just-underflow marker, stop.
    cia.write(0x04, 0x01);
    cia.write(0x05, 0x00);
    cia.write(0x0D, 0x81); // enable Timer A IRQ
    cia.write(0x0E, 0x19); // start + one-shot + force-load
    bool edge = cia.tick();
    auto snap = cia.timerPhaseSnapshot();
    require(!edge && snap.timerAUnderflows == 0u, "Timer A latch=1 does not underflow until decrement wraps through $FFFF");
    edge = cia.tick();
    snap = cia.timerPhaseSnapshot();
    require(edge, "Timer A underflow reports new IRQ edge after decrement-to-$FFFF");
    require(snap.timerAUnderflows == 1u, "Timer A underflow counted");
    require(snap.timerAJustUnderflowed, "Timer A just-underflow phase reported");
    require(snap.irqEdges >= 1u, "CIA IRQ edge counter increments");
    require((cia.read(0x0E) & 0x01u) == 0u, "Timer A one-shot stops after underflow");
    const uint8_t icr = cia.read(0x0D);
    require((icr & 0x81u) == 0x81u, "ICR read reports master IRQ and Timer A flag");
    require(!cia.irq(), "ICR read clears IRQ line");

    // CNT edge input queues one pulse and Timer A CNT mode consumes it.
    cia.reset();
    cia.write(0x04, 0x01);
    cia.write(0x05, 0x00);
    cia.write(0x0D, 0x81);
    cia.write(0x0E, 0x31); // start + force-load + CNT source
    cia.setCntInput(0);
    cia.setCntInput(1); // rising edge
    require(cia.timerPhaseSnapshot().cntRisingEdges == 1u, "CNT rising edge counted");
    cia.step(0);
    snap = cia.timerPhaseSnapshot();
    require(snap.timerAUnderflows == 0u, "first CNT pulse decrements Timer A without underflow");
    cia.setCntInput(0);
    cia.setCntInput(1);
    cia.step(0);
    snap = cia.timerPhaseSnapshot();
    require(snap.timerAUnderflows == 1u, "second CNT pulse clocks Timer A through $FFFF underflow");
    require(cia.irq(), "CNT-driven Timer A IRQ asserted");

    // Timer B counts Timer A underflows in CRB mode 10.
    cia.reset();
    cia.write(0x04, 0x01); cia.write(0x05, 0x00);
    cia.write(0x06, 0x01); cia.write(0x07, 0x00);
    cia.write(0x0D, 0x82);
    cia.write(0x0E, 0x11); // Timer A start + force-load
    cia.write(0x0F, 0x51); // Timer B start + force-load + count Timer A underflows
    cia.tick();
    cia.tick();
    snap = cia.timerPhaseSnapshot();
    require(snap.timerAUnderflows == 1u, "Timer A underflow source for Timer B counted");
    require(snap.timerBUnderflows == 0u, "first Timer A underflow decrements Timer B but does not underflow yet");
    cia.tick();
    cia.tick();
    snap = cia.timerPhaseSnapshot();
    require(snap.timerBUnderflows == 1u, "second Timer A underflow clocks Timer B through $FFFF");
    require(cia.irq(), "Timer B IRQ asserted from Timer A underflow source");

    std::cout << "C64CiaProperV618Tests PASS\n";
    return 0;
}
