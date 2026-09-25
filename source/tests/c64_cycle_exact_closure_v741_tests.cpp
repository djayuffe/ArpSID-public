#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_sid_readback.h"
#include "arpsid/core/c64_vic.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static void testCiaCyclePipeline() {
    static_assert(Cia6526::kTimerIrqModelIsCycleExact);

    Cia6526 batched;
    Cia6526 scalar;
    batched.reset();
    scalar.reset();
    for (Cia6526* cia : {&batched, &scalar}) {
        cia->write(0x04u, 0x02u);
        cia->write(0x05u, 0x00u);
        cia->write(0x06u, 0x01u);
        cia->write(0x07u, 0x00u);
        cia->write(0x0Du, 0x83u);
        cia->write(0x0Eu, 0x11u);
        cia->write(0x0Fu, 0x51u);
    }
    batched.step(12u);
    for (int i = 0; i < 12; ++i) scalar.tick();
    const auto a = batched.timerPhaseSnapshot();
    const auto b = scalar.timerPhaseSnapshot();
    require(a.timerA == b.timerA && a.timerB == b.timerB,
            "CIA batched and scalar timer state are identical");
    require(a.timerAUnderflows == b.timerAUnderflows &&
            a.timerBUnderflows == b.timerBUnderflows &&
            a.irqEdges == b.irqEdges,
            "CIA batched and scalar edge ledgers are identical");

    Cia6526 serial;
    serial.reset();
    serial.write(0x0Cu, 0x00u);
    serial.setSpInput(1u);
    serial.step(32u);
    require(serial.serialShift() == 0u,
            "CIA serial input does not self-clock from PHI2");
    for (int i = 0; i < 8; ++i) {
        serial.setCntInput(0u);
        serial.setCntInput(1u);
        serial.step(0u);
    }
    require(serial.serialShift() == 0xFFu,
            "CIA serial input shifts exactly on CNT rising edges");
    require((serial.irqFlags() & Cia6526::IcrSdr) != 0u,
            "CIA serial input raises SDR after exactly eight CNT edges");
}

static void testVicCycleTables() {
    static_assert(VicII::kBusStealIsCycleExact);

    VicII pal;
    pal.reset(true);
    pal.write(0x11u, 0x10u); // DEN, YSCROLL 0
    pal.step(0x30u * VicII::kPalCyclesPerLine);
    require(pal.rasterLine() == 0x30u, "VIC reaches first PAL badline");
    const auto warning = pal.cycleInfo(11u); // hardware cycle 12
    const auto firstDma = pal.cycleInfo(14u); // hardware cycle 15
    require(!warning.ba && warning.aec,
            "badline BA warns three cycles before AEC drops");
    require(!firstDma.ba && !firstDma.aec &&
            firstDma.phi2Access == VicAccessKind::Matrix,
            "badline matrix DMA starts on hardware cycle 15");
    require(pal.previewStolen(VicII::kPalCyclesPerLine) == 40u,
            "PAL badline steals exactly 40 PHI2 cycles");

    VicII spritePal;
    spritePal.reset(true);
    spritePal.write(0x01u, 0x00u); // sprite 0 Y
    spritePal.write(0x15u, 0x01u); // sprite 0 enable
    spritePal.step(57u);           // pass DMA start check on cycles 55/56
    require(spritePal.spriteDmaActive(0u), "PAL sprite 0 DMA activates on Y match");
    require(!spritePal.cycleInfo(54u).ba && spritePal.cycleInfo(54u).aec,
            "PAL sprite 0 BA warning starts on hardware cycle 55");
    require(!spritePal.cycleInfo(57u).aec && !spritePal.cycleInfo(58u).aec,
            "PAL sprite 0 steals hardware cycles 58 and 59");

    VicII spriteNtsc;
    spriteNtsc.reset(false);
    spriteNtsc.write(0x01u, 0x00u);
    spriteNtsc.write(0x15u, 0x01u);
    spriteNtsc.step(57u);
    require(spriteNtsc.spriteDmaActive(0u), "NTSC sprite 0 DMA activates on Y match");
    require(!spriteNtsc.cycleInfo(59u).aec && !spriteNtsc.cycleInfo(60u).aec,
            "NTSC sprite 0 steals hardware cycles 60 and 61");
}

static void testSidPhysicalReadback() {
    static_assert(SidReadbackModel::kCycleExact);

    SidReadbackModel sid;
    sid.reset(true);
    sid.write(0u, 0x0Eu, 0x00u);
    sid.write(0u, 0x0Fu, 0x10u); // voice 3 frequency $1000
    sid.write(0u, 0x13u, 0x00u);
    sid.write(0u, 0x14u, 0x00u);
    sid.write(0u, 0x12u, 0x21u); // saw + gate
    require(sid.read(16u, 0x1Bu) == 0x01u,
            "OSC3 returns selected saw output, not a register-write hash");
    require(sid.read(16u, 0x1Cu) != 0u,
            "ENV3 returns the live envelope counter");
    require(sid.read(16u, 0x05u, 0xA5u) == 0xA5u,
            "write-only SID registers preserve open bus");

    sid.write(16u, 0x12u, 0x08u); // TEST
    require(sid.read(64u, 0x1Bu) == 0u,
            "OSC3 is held at zero while TEST is asserted");

    SidReadbackModel pots;
    pots.reset();
    pots.setPotTargets(0x40u, 0x80u);
    require(pots.read(0u, 0x19u) == 0xFFu, "POT scan starts disconnected/high");
    require(pots.read(511u, 0x19u) == 0xFFu, "POT result does not update before conversion completes");
    require(pots.read(512u, 0x19u) == 0x40u && pots.read(512u, 0x1Au) == 0x80u,
            "POTX/POTY latch at the 512-cycle conversion boundary");
}

int main() {
    testCiaCyclePipeline();
    testVicCycleTables();
    testSidPhysicalReadback();
    std::cout << "C64CycleExactClosureV741Tests PASS\n";
    return 0;
}
