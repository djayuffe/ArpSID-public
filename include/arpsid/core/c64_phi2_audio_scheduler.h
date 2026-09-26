// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/c64_phi2_types.h"
#include <cmath>
#include <limits>

namespace ArpSID::C64 {

struct Phi2AudioScheduler {
    double cpuHz = kC64PalCpuHzExact;
    double sampleRate = 48000.0;
    double phi2Debt = 0.0;
    uint64_t machinePhi2 = 0;

    void configure(double cpu, double sr) noexcept {
        cpuHz = (std::isfinite(cpu) && cpu >= 1.0 && cpu <= 100000000.0) ? cpu : kC64PalCpuHzExact;
        sampleRate = (std::isfinite(sr) && sr >= 1.0 && sr <= 10000000.0) ? sr : 48000.0;
        phi2Debt = 0.0;
        machinePhi2 = 0;
    }

    uint32_t cyclesForNextSample() noexcept {
        const double safeCpu = (std::isfinite(cpuHz) && cpuHz >= 1.0 && cpuHz <= 100000000.0)
            ? cpuHz : kC64PalCpuHzExact;
        const double safeRate = (std::isfinite(sampleRate) && sampleRate >= 1.0 && sampleRate <= 10000000.0)
            ? sampleRate : 48000.0;
        if (!std::isfinite(phi2Debt) || phi2Debt < 0.0) phi2Debt = 0.0;
        phi2Debt += safeCpu / safeRate;
        const double bounded = std::min(phi2Debt, static_cast<double>(std::numeric_limits<uint32_t>::max()));
        const uint32_t n = static_cast<uint32_t>(bounded);
        phi2Debt -= static_cast<double>(n);
        const uint64_t room = std::numeric_limits<uint64_t>::max() - machinePhi2;
        machinePhi2 += std::min<uint64_t>(room, n);
        return n;
    }
};

class SidScalarIntervalRenderer final : public ISidRegisterWriteSink {
public:
    void beginHostSample(uint64_t phi2Start, uint32_t phi2Len) noexcept {
        sampleStart_ = phi2Start;
        const uint64_t room = std::numeric_limits<uint64_t>::max() - phi2Start;
        sampleEnd_ = phi2Start + std::min<uint64_t>(room, phi2Len);
        segmentStart_ = phi2Start;
        accum_ = 0.0;
        writesThisSample_ = 0;
    }

    void writeSidRegisterPhi2(uint64_t phi2,
                              uint8_t reg,
                              uint8_t value,
                              bool rmwDummy) noexcept override {
        closeSegment_(std::min<uint64_t>(phi2, sampleEnd_));
        reg = static_cast<uint8_t>(reg & 0x1Fu);
        regs_[reg] = value;
        if (reg == 0x18u) current_ = static_cast<float>(value & 0x0Fu) / 15.0f;
        else current_ = static_cast<float>(value) / 255.0f;
        ++totalSidWrites_;
        if (rmwDummy) ++rmwDummySidWrites_;
        ++writesThisSample_;
    }

    uint8_t readSidRegisterPhi2(uint64_t, uint8_t reg, uint8_t openBus) noexcept override {
        switch (static_cast<uint8_t>(reg & 0x1Fu)) {
            case 0x19u:
            case 0x1Au:
            case 0x1Bu:
            case 0x1Cu:
                return regs_[static_cast<uint8_t>(reg & 0x1Fu)];
            default:
                return openBus;
        }
    }

    float finishHostSample(uint64_t phi2End) noexcept {
        closeSegment_(std::min<uint64_t>(phi2End, sampleEnd_));
        const uint64_t len = sampleEnd_ - sampleStart_;
        if (writesThisSample_ > maxWritesPerSample_) maxWritesPerSample_ = writesThisSample_;
        return len ? static_cast<float>(accum_ / static_cast<double>(len)) : current_;
    }

    float current() const noexcept { return current_; }
    uint64_t totalSidWrites() const noexcept { return totalSidWrites_; }
    uint64_t rmwDummySidWrites() const noexcept { return rmwDummySidWrites_; }
    uint64_t maxWritesPerSample() const noexcept { return maxWritesPerSample_; }

private:
    void closeSegment_(uint64_t untilPhi2) noexcept {
        if (untilPhi2 <= segmentStart_) return;
        const uint64_t len = untilPhi2 - segmentStart_;
        accum_ += static_cast<double>(current_) * static_cast<double>(len);
        segmentStart_ = untilPhi2;
    }

    std::array<uint8_t, 32> regs_{};
    uint64_t sampleStart_ = 0;
    uint64_t sampleEnd_ = 0;
    uint64_t segmentStart_ = 0;
    double accum_ = 0.0;
    float current_ = 0.0f;
    uint64_t totalSidWrites_ = 0;
    uint64_t rmwDummySidWrites_ = 0;
    uint64_t writesThisSample_ = 0;
    uint64_t maxWritesPerSample_ = 0;
};

} // namespace ArpSID::C64
