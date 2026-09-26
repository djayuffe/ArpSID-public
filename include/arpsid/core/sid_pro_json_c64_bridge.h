// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
// SID-PRO JSON trace playback bridge for ArpSID's C64 bus/projection runtime.
// Import/UI/offline path only. The bridge is deterministic, allocation-free
// after parser allocation, and never mutates a second SID shadow authority.

#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_sid_projection_bridge.h"
#include "arpsid/core/sid_pro_json_trace.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID {

enum class SidProJsonBridgeResult : uint8_t {
    OK = 0,
    NullPlatform,
    MultiSidRequiresExternalSink,
    BadRegister,
    QueueOverflow,
};

static inline const char* sidProJsonBridgeResultName(SidProJsonBridgeResult r) noexcept {
    switch (r) {
        case SidProJsonBridgeResult::OK: return "OK";
        case SidProJsonBridgeResult::NullPlatform: return "NullPlatform";
        case SidProJsonBridgeResult::MultiSidRequiresExternalSink: return "MultiSidRequiresExternalSink";
        case SidProJsonBridgeResult::BadRegister: return "BadRegister";
        case SidProJsonBridgeResult::QueueOverflow: return "QueueOverflow";
    }
    return "Unknown";
}

struct SidProJsonBridgeTelemetry {
    uint64_t applied = 0;
    uint64_t scheduled = 0;
    uint64_t immediate = 0;
    uint64_t rejectedMultiSid = 0;
    uint64_t rejectedReadOnly = 0;
    uint64_t queueOverflow = 0;
    uint64_t lastCycle = 0;
    uint8_t lastChip = 0;
    uint8_t lastReg = 0;
    uint8_t lastValue = 0;

    void clear() noexcept { *this = {}; }
};

class SidProJsonMultiSidRegisterBank {
public:
    void reset() noexcept {
        for (auto& c : chips_) c.fill(0);
        sidCount_ = 1;
        lastChip_ = lastReg_ = lastValue_ = 0;
        lastCycle_ = 0;
        writeObserved_ = false;
    }

    bool configure(uint8_t sidCount) noexcept {
        if (sidCount < 1u || sidCount > 3u) return false;
        sidCount_ = sidCount;
        for (auto& c : chips_) c.fill(0);
        return true;
    }

    bool apply(const SidProJsonWrite& w) noexcept {
        if (w.chip >= sidCount_ || w.chip >= chips_.size()) return false;
        if (w.reg >= 32u) return false;
        if (!C64::c64SidRegWriteable(w.reg)) return false;
        chips_[w.chip][w.reg] = w.value;
        lastChip_ = w.chip;
        lastReg_ = w.reg;
        lastValue_ = w.value;
        lastCycle_ = w.cycle;
        writeObserved_ = true;
        return true;
    }

    const std::array<uint8_t, 32>& chip(size_t i) const noexcept { return chips_[i < chips_.size() ? i : 0]; }
    uint8_t sidCount() const noexcept { return sidCount_; }
    bool writeObserved() const noexcept { return writeObserved_; }
    uint8_t lastChip() const noexcept { return lastChip_; }
    uint8_t lastReg() const noexcept { return lastReg_; }
    uint8_t lastValue() const noexcept { return lastValue_; }
    uint64_t lastCycle() const noexcept { return lastCycle_; }

private:
    std::array<std::array<uint8_t, 32>, 3> chips_{};
    uint8_t sidCount_ = 1;
    uint8_t lastChip_ = 0;
    uint8_t lastReg_ = 0;
    uint8_t lastValue_ = 0;
    uint64_t lastCycle_ = 0;
    bool writeObserved_ = false;
};

class SidProJsonC64Bridge {
public:
    void reset() noexcept {
        player_.reset();
        telemetry_.clear();
    }

    const SidProJsonBridgeTelemetry& telemetry() const noexcept { return telemetry_; }
    size_t cursor() const noexcept { return player_.cursor(); }

    // Apply all writes up to an absolute PHI2 cycle into the real C64Platform
    // bus. Since C64Platform currently has one hardware SID decode window, this
    // method intentionally rejects chip>0 writes instead of silently folding
    // stereo/3-SID traces onto chip 0. Use applyUntilToRegisterBank() or an
    // explicit multi-SID sink for those traces.
    SidProJsonBridgeResult applyUntilToC64Platform(const SidProJsonTrace& trace,
                                                   C64::C64Platform& platform,
                                                   uint64_t absoluteCycleInclusive) noexcept {
        SidProJsonBridgeResult result = SidProJsonBridgeResult::OK;
        player_.applyUntil(trace, absoluteCycleInclusive, [&](uint8_t chip, uint8_t reg, uint8_t value, uint64_t cycle) noexcept {
            telemetry_.lastCycle = cycle;
            telemetry_.lastChip = chip;
            telemetry_.lastReg = reg;
            telemetry_.lastValue = value;
            if (chip != 0u) {
                ++telemetry_.rejectedMultiSid;
                result = SidProJsonBridgeResult::MultiSidRequiresExternalSink;
                return;
            }
            if (reg >= 32u) {
                result = SidProJsonBridgeResult::BadRegister;
                return;
            }
            if (!C64::c64SidRegWriteable(reg)) {
                ++telemetry_.rejectedReadOnly;
                return;
            }
            const uint64_t now = platform.phi2Cycle();
            const uint64_t offset = (cycle > now) ? (cycle - now) : 0u;
            const bool ok = C64::projectSidTimedWriteThroughC64Bus(platform, reg, value, offset);
            if (!ok) {
                ++telemetry_.queueOverflow;
                result = SidProJsonBridgeResult::QueueOverflow;
                return;
            }
            ++telemetry_.applied;
            if (offset == 0u) ++telemetry_.immediate;
            else ++telemetry_.scheduled;
        });
        return result;
    }

    SidProJsonBridgeResult applyUntilToRegisterBank(const SidProJsonTrace& trace,
                                                    SidProJsonMultiSidRegisterBank& bank,
                                                    uint64_t absoluteCycleInclusive) noexcept {
        (void)bank.configure(trace.metadata.sidCount);
        SidProJsonBridgeResult result = SidProJsonBridgeResult::OK;
        player_.applyUntil(trace, absoluteCycleInclusive, [&](uint8_t chip, uint8_t reg, uint8_t value, uint64_t cycle) noexcept {
            SidProJsonWrite w{cycle, chip, reg, value};
            telemetry_.lastCycle = cycle;
            telemetry_.lastChip = chip;
            telemetry_.lastReg = reg;
            telemetry_.lastValue = value;
            if (!bank.apply(w)) {
                if (!C64::c64SidRegWriteable(reg)) ++telemetry_.rejectedReadOnly;
                result = SidProJsonBridgeResult::BadRegister;
                return;
            }
            ++telemetry_.applied;
            ++telemetry_.immediate;
        });
        return result;
    }

    template <class Sink>
    SidProJsonBridgeResult applyUntilToExternalSink(const SidProJsonTrace& trace,
                                                    uint64_t absoluteCycleInclusive,
                                                    Sink&& sink) noexcept {
        SidProJsonBridgeResult result = SidProJsonBridgeResult::OK;
        player_.applyUntil(trace, absoluteCycleInclusive, [&](uint8_t chip, uint8_t reg, uint8_t value, uint64_t cycle) noexcept {
            telemetry_.lastCycle = cycle;
            telemetry_.lastChip = chip;
            telemetry_.lastReg = reg;
            telemetry_.lastValue = value;
            if (reg >= 32u) { result = SidProJsonBridgeResult::BadRegister; return; }
            if (!C64::c64SidRegWriteable(reg)) { ++telemetry_.rejectedReadOnly; return; }
            sink(chip, reg, value, cycle);
            ++telemetry_.applied;
            ++telemetry_.immediate;
        });
        return result;
    }

private:
    SidProJsonTracePlayer player_{};
    SidProJsonBridgeTelemetry telemetry_{};
};

} // namespace ArpSID
