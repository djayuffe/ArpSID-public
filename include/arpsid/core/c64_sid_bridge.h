#pragma once
// Timed $D400 SID bridge for C64Runtime/PSID and SID projection mirror paths.

#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_sid_readback.h"
#include "arpsid/engines/sid_register_engine.h"
#include <array>
#include <algorithm>
#include <cstdint>

namespace ArpSID::C64 {

static constexpr uint16_t kC64SidBase1 = 0xD400u;
static constexpr uint16_t kC64SidBase2 = 0xD420u;
static constexpr uint16_t kC64SidBase3 = 0xD440u;

struct C64SidBridgeTimedWrite final {
    uint8_t reg = 0;
    uint8_t value = 0;
    uint64_t phi2Cycle = 0;
    uint8_t chip = 0;
    bool rmwDummy = false;
};

struct C64SidBridgeState final : SidRegisterSink {
    static constexpr size_t kMaxTimedWrites = 4096;
    C64SidBridgeState() noexcept { reset(); }

    struct Snapshot final {
        std::array<uint8_t, 32> regs{};
        std::array<std::array<uint8_t, 32>, 5> regsByChip{};
        std::array<uint8_t, 5> osc3ByChip{};
        std::array<uint8_t, 5> env3ByChip{};
        std::array<SidReadbackModel, 5> readbackByChip{};
        std::array<C64SidBridgeTimedWrite, kMaxTimedWrites> timedWrites{};
        ArpSID::SidRegisterEngine* engine = nullptr;
        SidRegisterSink* mirrorSink = nullptr;
        bool deferEngineWrites = false;
        uint32_t timedWriteCount = 0;
        uint32_t timedWriteOverflow = 0;
        uint8_t lastReg = 0;
        uint8_t lastChip = 0;
        uint8_t lastValue = 0;
        uint64_t lastCycle = 0;
        uint32_t writeCount = 0;
        uint32_t d418WriteCount = 0;
        uint32_t d418RepeatedValueWriteCount = 0;
        uint64_t lastD418Cycle = 0;
        uint8_t lastD418Value = 0;
        bool d418Observed = false;
        uint32_t sidReadApproximationCount = 0;
        uint32_t sidOpenBusReadCount = 0;
        uint32_t invalidSidChipReadCount = 0;
        uint32_t invalidSidChipWriteCount = 0;
        uint32_t rmwDummyWriteCount = 0;
        uint32_t droppedSidHoleWriteCount = 0;
    };

    std::array<uint8_t, 32> regs{};
    std::array<std::array<uint8_t, 32>, 5> regsByChip{};
    std::array<uint8_t, 5> osc3ByChip{};
    std::array<uint8_t, 5> env3ByChip{};
    std::array<SidReadbackModel, 5> readbackByChip{};
    std::array<C64SidBridgeTimedWrite, kMaxTimedWrites> timedWrites{};
    ArpSID::SidRegisterEngine* engine = nullptr;
    SidRegisterSink* mirrorSink = nullptr;
    bool deferEngineWrites = false;
    uint32_t timedWriteCount = 0;
    uint32_t timedWriteOverflow = 0;
    uint8_t lastReg = 0;
    uint8_t lastChip = 0;
    uint8_t lastValue = 0;
    uint64_t lastCycle = 0;
    uint32_t writeCount = 0;
    uint32_t d418WriteCount = 0;
    uint32_t d418RepeatedValueWriteCount = 0;
    uint64_t lastD418Cycle = 0;
    uint8_t lastD418Value = 0;
    bool d418Observed = false;
    uint32_t sidReadApproximationCount = 0;
    uint32_t sidOpenBusReadCount = 0;
    uint32_t invalidSidChipReadCount = 0;
    uint32_t invalidSidChipWriteCount = 0;
    uint32_t rmwDummyWriteCount = 0;
    uint32_t droppedSidHoleWriteCount = 0;

    void reset() noexcept {
        regs.fill(0);
        for (auto& chipRegs : regsByChip) chipRegs.fill(0);
        osc3ByChip.fill(0);
        env3ByChip.fill(0);
        for (auto& readback : readbackByChip) readback.reset();
        for (auto& w : timedWrites) w = C64SidBridgeTimedWrite{};
        lastReg = 0;
        lastChip = 0;
        lastValue = 0;
        lastCycle = 0;
        writeCount = 0;
        d418WriteCount = 0;
        d418RepeatedValueWriteCount = 0;
        lastD418Cycle = 0;
        lastD418Value = 0;
        d418Observed = false;
        sidReadApproximationCount = 0;
        sidOpenBusReadCount = 0;
        invalidSidChipReadCount = 0;
        invalidSidChipWriteCount = 0;
        rmwDummyWriteCount = 0;
        droppedSidHoleWriteCount = 0;
        timedWriteCount = 0;
        timedWriteOverflow = 0;
    }

    void resetTimedWrites() noexcept {
        const uint32_t n = std::min<uint32_t>(timedWriteCount, static_cast<uint32_t>(timedWrites.size()));
        for (uint32_t i = 0; i < n; ++i) timedWrites[i] = C64SidBridgeTimedWrite{};
        timedWriteCount = 0;
        timedWriteOverflow = 0;
    }

    void captureSnapshot(Snapshot& out) const noexcept {
        out.regs = regs;
        out.regsByChip = regsByChip;
        out.osc3ByChip = osc3ByChip;
        out.env3ByChip = env3ByChip;
        out.readbackByChip = readbackByChip;
        out.timedWrites = timedWrites;
        out.engine = engine;
        out.mirrorSink = mirrorSink;
        out.deferEngineWrites = deferEngineWrites;
        out.timedWriteCount = timedWriteCount;
        out.timedWriteOverflow = timedWriteOverflow;
        out.lastReg = lastReg;
        out.lastChip = lastChip;
        out.lastValue = lastValue;
        out.lastCycle = lastCycle;
        out.writeCount = writeCount;
        out.d418WriteCount = d418WriteCount;
        out.d418RepeatedValueWriteCount = d418RepeatedValueWriteCount;
        out.lastD418Cycle = lastD418Cycle;
        out.lastD418Value = lastD418Value;
        out.d418Observed = d418Observed;
        out.sidReadApproximationCount = sidReadApproximationCount;
        out.sidOpenBusReadCount = sidOpenBusReadCount;
        out.invalidSidChipReadCount = invalidSidChipReadCount;
        out.invalidSidChipWriteCount = invalidSidChipWriteCount;
        out.rmwDummyWriteCount = rmwDummyWriteCount;
        out.droppedSidHoleWriteCount = droppedSidHoleWriteCount;
    }

    void restoreSnapshot(const Snapshot& in) noexcept {
        regs = in.regs;
        regsByChip = in.regsByChip;
        osc3ByChip = in.osc3ByChip;
        env3ByChip = in.env3ByChip;
        readbackByChip = in.readbackByChip;
        timedWrites = in.timedWrites;
        engine = in.engine;
        mirrorSink = in.mirrorSink;
        deferEngineWrites = in.deferEngineWrites;
        timedWriteCount = in.timedWriteCount;
        timedWriteOverflow = in.timedWriteOverflow;
        lastReg = in.lastReg;
        lastChip = in.lastChip;
        lastValue = in.lastValue;
        lastCycle = in.lastCycle;
        writeCount = in.writeCount;
        d418WriteCount = in.d418WriteCount;
        d418RepeatedValueWriteCount = in.d418RepeatedValueWriteCount;
        lastD418Cycle = in.lastD418Cycle;
        lastD418Value = in.lastD418Value;
        d418Observed = in.d418Observed;
        sidReadApproximationCount = in.sidReadApproximationCount;
        sidOpenBusReadCount = in.sidOpenBusReadCount;
        invalidSidChipReadCount = in.invalidSidChipReadCount;
        invalidSidChipWriteCount = in.invalidSidChipWriteCount;
        rmwDummyWriteCount = in.rmwDummyWriteCount;
        droppedSidHoleWriteCount = in.droppedSidHoleWriteCount;
    }

    void sidWrite(uint8_t reg, uint8_t value, uint64_t phi2Cycle) noexcept override {
        const uint8_t chip = static_cast<uint8_t>(reg / 32u);
        if (chip >= regsByChip.size()) {
            ++invalidSidChipWriteCount;
            return;
        }
        const uint8_t r = static_cast<uint8_t>(reg & 0x1Fu);
        if (r > 0x18u) {
            ++droppedSidHoleWriteCount;
            return;
        }
        regsByChip[chip][r] = value;
        readbackByChip[chip].write(phi2Cycle, r, value);
        regs[r] = regsByChip[0][r];
        if (r == 0x18u) {
            if (d418Observed && lastD418Value == value) ++d418RepeatedValueWriteCount;
            ++d418WriteCount;
            lastD418Cycle = phi2Cycle;
            lastD418Value = value;
            d418Observed = true;
        }
        lastChip = chip;
        lastReg = r;
        lastValue = value;
        lastCycle = phi2Cycle;
        ++writeCount;
        if (timedWriteCount < kMaxTimedWrites) {
            timedWrites[timedWriteCount++] = C64SidBridgeTimedWrite{r, value, phi2Cycle, chip, false};
        } else {
            ++timedWriteOverflow;
        }
        if (mirrorSink) {
            mirrorSink->sidWrite(static_cast<uint8_t>(chip * 32u + r), value, phi2Cycle);
        }
        // MULTI-SID NOTE: the live-engine direct-write path (non-deferred) is
        // primary-SID-only (chip == 0). Secondary chips (chip 1..4) receive
        // timed writes only — they are rendered audibly by the kernel's
        // renderC64SidPlayer_ loop via queueSubphaseWrite() on each chip's
        // SidRegisterEngine. To enable live-engine writes for secondary chips,
        // extend this path to call sregForChip_(chip)->write(r, value) via a
        // registered multi-chip engine table.
        if (!deferEngineWrites && chip == 0u && engine && c64SidRegWriteable(r)) {
            engine->write(r, value);
        }
    }

    uint8_t sidRead(uint8_t reg, uint64_t phi2) noexcept override {
        const uint8_t chip = static_cast<uint8_t>(reg / 32u);
        if (chip >= regsByChip.size()) {
            ++invalidSidChipReadCount;
            ++sidOpenBusReadCount;
            ++sidReadApproximationCount;
            return 0xFFu;
        }
        const uint8_t r = static_cast<uint8_t>(reg & 0x1Fu);
        if (r < 0x19u || r > 0x1Cu) {
            ++sidOpenBusReadCount;
            ++sidReadApproximationCount;
            return 0xFFu;
        }
        const uint8_t value = readbackByChip[chip].read(phi2, r, 0xFFu);
        if (r == 0x1Bu) osc3ByChip[chip] = value;
        if (r == 0x1Cu) env3ByChip[chip] = value;
        return value;
    }
};

inline void c64SidBridgeInstall(C64Platform& platform,
                                C64SidBridgeState& bridge,
                                ArpSID::SidRegisterEngine* engine) noexcept {
    bridge.engine = engine;
    bridge.mirrorSink = nullptr;
    bridge.deferEngineWrites = false;
    platform.attachSid(&bridge);
}

inline void c64SidBridgeInstallWithSink(C64Platform& platform,
                                        C64SidBridgeState& bridge,
                                        ArpSID::SidRegisterEngine* engine,
                                        SidRegisterSink* mirrorSink) noexcept {
    bridge.engine = engine;
    bridge.mirrorSink = mirrorSink;
    bridge.deferEngineWrites = true;
    platform.attachSid(&bridge);
}

inline void c64SidBridgeRemove(C64Platform& platform,
                               C64SidBridgeState& bridge) noexcept {
    bridge.engine = nullptr;
    bridge.mirrorSink = nullptr;
    bridge.deferEngineWrites = false;
    platform.attachSid(nullptr);
}

} // namespace ArpSID::C64
