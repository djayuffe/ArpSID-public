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

    // FLAG event: ICR bit + IRQ when enabled; physical flag latch remains
    // observable until explicitly cleared.
    cia.write(0x0D, 0x90); // enable FLAG IRQ
    cia.pulseFlag();
    require(cia.irq(), "FLAG pulse asserts IRQ when mask enabled");
    require(cia.flagLatched(), "FLAG latch observable after pulse");
    uint8_t icr = cia.read(0x0D);
    require((icr & 0x90u) == 0x90u, "ICR read reports master IRQ and FLAG bit");
    require(!cia.irq(), "ICR read clears IRQ level");
    require(cia.flagLatched(), "ICR read does not erase physical FLAG latch observability");
    cia.clearFlagLatch();
    require(!cia.flagLatched(), "clearFlagLatch clears physical FLAG latch observability");

    // TOD write stop/resume and read latch. Physical 6526 write protocol is
    // HOURS stops the clock, TENTHS restarts it after the full value is set.
    cia.reset();
    cia.write(0x0B, 0x01); // hours stops TOD during time-set sequence
    require(cia.timerPhaseSnapshot().todStopped, "TOD stops after hours write during time-set sequence");
    cia.write(0x0A, 0x20);
    cia.write(0x09, 0x10);
    cia.write(0x08, 0x05); // tenths resumes
    require(!cia.timerPhaseSnapshot().todStopped, "TOD resumes after tenths write");

    // TOD alarm mode via CRB bit 7.
    cia.reset();
    cia.write(0x0B, 0x01); // set current TOD to 01:00:00.0; tenths write resumes TOD
    cia.write(0x0A, 0x00);
    cia.write(0x09, 0x00);
    cia.write(0x08, 0x00);
    cia.write(0x0F, 0x80); // alarm write mode
    cia.write(0x08, 0x01);
    cia.write(0x09, 0x00);
    cia.write(0x0A, 0x00);
    cia.write(0x0B, 0x01);
    cia.write(0x0F, 0x00);
    cia.write(0x0D, 0x84); // enable TOD IRQ
    cia.step(5);
    require(cia.irq(), "TOD alarm match asserts IRQ when enabled after TOD reaches alarm");


    // TOD read latch: reading hours latches a stable snapshot until tenths is read.
    cia.reset();
    cia.write(0x0B, 0x05);
    cia.write(0x0A, 0x04);
    cia.write(0x09, 0x03);
    cia.write(0x08, 0x02);
    const uint8_t latchedHours = cia.read(0x0B);
    require(cia.timerPhaseSnapshot().todLatched, "TOD hours read sets read latch");
    cia.step(25); // advance live TOD, latched snapshot must remain stable
    require(cia.read(0x0B) == latchedHours, "TOD latched hours remain stable while live TOD advances");
    (void)cia.read(0x08);
    require(!cia.timerPhaseSnapshot().todLatched, "TOD tenths read clears read latch");

    // Alarm write mode writes alarm registers, not current TOD time.
    cia.reset();
    cia.write(0x08, 0x01);
    cia.write(0x09, 0x02);
    cia.write(0x0A, 0x03);
    cia.write(0x0B, 0x04);
    cia.write(0x0F, 0x80);
    require(cia.timerPhaseSnapshot().todAlarmWriteMode, "CRB bit 7 enters TOD alarm write mode");
    cia.write(0x08, 0x09);
    cia.write(0x09, 0x59);
    cia.write(0x0A, 0x59);
    cia.write(0x0B, 0x11);
    auto todSnap = cia.timerPhaseSnapshot();
    require(todSnap.todTenths == 0x01 && todSnap.todSeconds == 0x02 &&
            todSnap.todMinutes == 0x03 && todSnap.todHours == 0x04,
            "TOD alarm write mode does not alter current TOD time");
    require(todSnap.todAlarmTenths == 0x09 && todSnap.todAlarmSeconds == 0x59 &&
            todSnap.todAlarmMinutes == 0x59 && todSnap.todAlarmHours == 0x11,
            "TOD alarm write mode updates alarm snapshot");

    // Serial output mode: Timer A underflow clocks serial bits and eventually SDR IRQ.
    cia.reset();
    cia.write(0x0C, 0xA5);
    cia.write(0x04, 0x01);
    cia.write(0x05, 0x00);
    cia.write(0x0D, 0x88); // SDR IRQ
    cia.write(0x0E, 0x51); // start + force-load + serial output
    for (int i = 0; i < 64; ++i) cia.tick();
    icr = cia.read(0x0D);
    require((icr & 0x88u) == 0x88u, "serial output completion asserts SDR IRQ flag");

    std::cout << "C64CiaExtendedV619Tests PASS\n";
    return 0;
}
