// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "sid_event_queue.h"
#include "sid_chip.h"
#include "math_utils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ArpSID {

// Phase 2 canonical timing state. This is the render-time physical SID clock
// authority for mapping a continuous host-sample stream onto integer SID cycles.
// It carries the fractional remainder across host samples and blocks; no caller
// may use rounded cycles-per-sample as physical audio authority.
struct SidCycleClockState {
    double sampleRate = 0.0;
    double sidClockHz = 0.0;
    uint64_t cyclesPerSampleQ32 = 0ull;
    uint64_t fractionalQ32 = 0ull;

    void reset() noexcept { fractionalQ32 = 0ull; }

    void configure(double sr, double clk) noexcept {
        if (!(std::isfinite(sr) && sr > 1.0) || !(std::isfinite(clk) && clk > 1.0)) {
            sampleRate = 0.0;
            sidClockHz = 0.0;
            cyclesPerSampleQ32 = 0ull;
            fractionalQ32 = 0ull;
            return;
        }
        if (std::fabs(sampleRate - sr) > 0.000001 || std::fabs(sidClockHz - clk) > 0.000001) {
            sampleRate = sr;
            sidClockHz = clk;
            cyclesPerSampleQ32 = ArpSID_cyclesPerSampleQ32(sr, clk);
            fractionalQ32 = 0ull;
        }
    }

    void configureFixedCyclesPerSample(uint16_t cycles) noexcept {
        if (cycles == 0u) {
            sampleRate = 0.0;
            sidClockHz = 0.0;
            cyclesPerSampleQ32 = 0ull;
            fractionalQ32 = 0ull;
            return;
        }
        sampleRate = 2.0;
        sidClockHz = static_cast<double>(cycles) * sampleRate;
        cyclesPerSampleQ32 = static_cast<uint64_t>(cycles) << 32u;
        fractionalQ32 = 0ull;
    }

    uint16_t cyclesForNextHostSample() noexcept {
        if (!(std::isfinite(sampleRate) && sampleRate > 1.0) || !(std::isfinite(sidClockHz) && sidClockHz > 1.0)) return 0u;
        const uint64_t stepQ32 = cyclesPerSampleQ32;
        if (stepQ32 == 0ull) return 0u;
        const uint64_t after = fractionalQ32 + stepQ32;
        fractionalQ32 = after & 0xFFFFFFFFull;
        const uint64_t cycles = after >> 32u;
        return static_cast<uint16_t>(std::clamp<uint64_t>(cycles, 0ull, 0xFFFFull));
    }

    uint64_t cyclesForNextHostBlock(uint32_t frameCount) noexcept {
        if (frameCount == 0u) return 0ull;
        if (!(std::isfinite(sampleRate) && sampleRate > 1.0) || !(std::isfinite(sidClockHz) && sidClockHz > 1.0)) return 0ull;
        const uint64_t stepQ32 = cyclesPerSampleQ32;
        if (stepQ32 == 0ull) return 0ull;
        uint64_t totalCycles = 0ull;
        uint32_t remaining = frameCount;
        while (remaining > 0u) {
            const uint64_t maxSafeFrames = (stepQ32 > 0ull) ? ((std::numeric_limits<uint64_t>::max() - fractionalQ32) / stepQ32) : 0ull;
            const uint32_t chunk = static_cast<uint32_t>(std::min<uint64_t>(remaining, std::max<uint64_t>(1ull, maxSafeFrames)));
            #if defined(__SIZEOF_INT128__)
            const __uint128_t total = static_cast<__uint128_t>(fractionalQ32) +
                                      static_cast<__uint128_t>(stepQ32) * static_cast<__uint128_t>(chunk);
            fractionalQ32 = static_cast<uint64_t>(total) & 0xFFFFFFFFull;
            totalCycles += static_cast<uint64_t>(total >> 32u);
        #else
            const uint64_t total = fractionalQ32 + (stepQ32 * static_cast<uint64_t>(chunk));
            fractionalQ32 = total & 0xFFFFFFFFull;
            totalCycles += (total >> 32u);
        #endif
            remaining -= chunk;
        }
        return totalCycles;
    }
};

inline double exactSidCyclesPerHostSample(double sampleRate, double sidClockHz) noexcept {
    if (!(std::isfinite(sampleRate) && sampleRate > 1.0) || !(std::isfinite(sidClockHz) && sidClockHz > 1.0)) return 0.0;
    const double cps = sidClockHz / sampleRate;
    return (std::isfinite(cps) && cps > 0.0) ? cps : 0.0;
}

// Legacy presentation helper only. It intentionally returns ceil() rather than
// round() so spans sized from it are conservative. It is not physical render authority.
inline uint16_t estimateSidCyclesPerHostSample(double sampleRate, double sidClockHz) noexcept {
    const double cps = exactSidCyclesPerHostSample(sampleRate, sidClockHz);
    if (!(cps > 0.0)) return 0u;
    return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::ceil(cps)), 1, 0xFFFF));
}

inline uint16_t sidCyclesInHostSample(uint64_t absoluteSampleIndex, double sampleRate, double sidClockHz) noexcept {
    const uint64_t q32 = ArpSID_cyclesPerSampleQ32(sampleRate, sidClockHz);
    return ArpSID_cyclesInHostSampleQ32(absoluteSampleIndex, q32);
}

inline uint16_t cycleOffsetFromSubphaseForCyclesInSample(uint8_t subphase, uint16_t cyclesInSample) noexcept {
    if (cyclesInSample == 0u) return kSidUnresolvedCycleOffset;
    const uint16_t maxCycle = static_cast<uint16_t>(cyclesInSample - 1u);
    if (subphase == 0u || subphase == 1u) return 0u;
    if (subphase == 2u) return static_cast<uint16_t>(cyclesInSample / 2u);
    if (subphase == 3u) return maxCycle;
    const uint32_t scaled = (static_cast<uint32_t>(subphase) * static_cast<uint32_t>(cyclesInSample)) /
                            static_cast<uint32_t>(kSidSubcycleResolution);
    return static_cast<uint16_t>(std::min<uint32_t>(scaled, maxCycle));
}

inline uint16_t estimateCycleOffsetFromSubphase(uint8_t subphase, double sampleRate, double sidClockHz) noexcept {
    const double cps = exactSidCyclesPerHostSample(sampleRate, sidClockHz);
    if (!(cps > 0.0)) return kSidUnresolvedCycleOffset;
    const uint16_t cycles = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::ceil(cps)), 1, 0xFFFF));
    return cycleOffsetFromSubphaseForCyclesInSample(subphase, cycles);
}

inline uint64_t makeSidCycleOrderingStamp(uint32_t sampleOffset, uint16_t cycleOffset, uint8_t subphase, uint32_t arrivalOrder) noexcept {
    // Advisory/debug stamp only. SidTimedEvent::before() compares full-width
    // arrival_order directly; this low byte must never be used as the final sort law.
    return (static_cast<uint64_t>(sampleOffset) << 32) |
           (static_cast<uint64_t>(cycleOffset) << 16) |
           (static_cast<uint64_t>(subphase) << 8) |
           static_cast<uint64_t>(arrivalOrder & 0xFFu);
}

namespace detail {

inline uint64_t makeSidCycleStampFromRuntimeClock_(uint32_t sampleOffset,
                                                   uint16_t cycleOffset,
                                                   uint8_t subphase,
                                                   uint32_t arrivalOrder,
                                                   double sampleRate,
                                                   double sidClockHz) noexcept {
    if (!(std::isfinite(sampleRate) && sampleRate > 1.0) ||
        !(std::isfinite(sidClockHz) && sidClockHz > 1.0)) {
        return makeSidCycleOrderingStamp(sampleOffset, cycleOffset, subphase, arrivalOrder);
    }
    const uint64_t q32 = ArpSID_cyclesPerSampleQ32(sampleRate, sidClockHz);
    if (q32 == 0ull) {
        return makeSidCycleOrderingStamp(sampleOffset, cycleOffset, subphase, arrivalOrder);
    }
    const uint64_t absoluteCycle = ArpSID_absoluteCycleAtSampleQ32(sampleOffset, q32) + static_cast<uint64_t>(cycleOffset);
    return (absoluteCycle << 16) |
           (static_cast<uint64_t>(subphase) << 8) |
           static_cast<uint64_t>(arrivalOrder & 0xFFu);
}

} // namespace detail

inline int32_t canonicalHostSampleOffsetFromSeconds(double deltaSeconds, double sampleRate, int frameCount) noexcept {
    if (!(std::isfinite(deltaSeconds) && std::isfinite(sampleRate)) || !(sampleRate > 1.0) || frameCount <= 0) return 0;
    const double raw = deltaSeconds * sampleRate;
    if (!std::isfinite(raw)) return deltaSeconds > 0.0 ? std::max(0, frameCount - 1) : 0;
    const double bounded = std::clamp(raw, 0.0, static_cast<double>(std::max(0, frameCount - 1)));
    // v902: host events are not allowed to be pulled into a future sample by
    // nearest-sample rounding. Floor with a tiny epsilon keeps edge values stable
    // without creating +/-0.5-sample musical jitter at tight gate boundaries.
    return static_cast<int32_t>(std::floor(bounded + 1.0e-9));
}

inline void assignBestEffortIntraSampleTiming(SidTimedEvent& ev,
                                              int32_t sampleOffset,
                                              uint32_t arrivalOrder,
                                              uint8_t subphase,
                                              double sampleRate,
                                              double sidClockHz,
                                              uint16_t explicitCycleOffset = kSidUnresolvedCycleOffset) noexcept {
    ev.sample_offset = (sampleOffset >= 0)
                           ? static_cast<uint32_t>(sampleOffset)
                           : kSidUnresolvedSampleOffset;
    ev.subphase = subphase;
    ev.arrival_order = arrivalOrder;
    ev.cycle_offset = explicitCycleOffset;

    (void)sampleRate;
    (void)sidClockHz;
    ev.sid_cycle_stamp = 0ull;
}

inline void finalizeSidCycleStampFromRuntimeClock(SidTimedEvent& ev,
                                                  double sampleRate,
                                                  double sidClockHz) noexcept {
    if (ev.sample_offset == kSidUnresolvedSampleOffset) {
        ev.sid_cycle_stamp = 0ull;
        return;
    }
    uint16_t cycleOffset = ev.cycle_offset;
    if (cycleOffset == kSidUnresolvedCycleOffset) {
        cycleOffset = estimateCycleOffsetFromSubphase(ev.subphase, sampleRate, sidClockHz);
        ev.cycle_offset = cycleOffset;
    }
    ev.sid_cycle_stamp = detail::makeSidCycleStampFromRuntimeClock_(ev.sample_offset,
                                                                    cycleOffset,
                                                                    ev.subphase,
                                                                    ev.arrival_order,
                                                                    sampleRate,
                                                                    sidClockHz);
}
} // namespace ArpSID
