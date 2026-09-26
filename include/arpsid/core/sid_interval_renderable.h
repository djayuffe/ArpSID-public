// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// CANONICAL RENDER TRUTH: Backends own interval audio. No wrapper or shared helper
// may define final physical sub-sample sound law. All interval audio originates
// from a backend implementing this interface.
//
// Invariants:
// - renderIntervalAccurate() advances exactly [beginCycle+beginSubphase, endCycle+endSubphase).
// - The backend must clamp repeated/overlapping requests correctly.
// - No wrapper-level "render full cycle then scale" law is primary truth.
// - Subphase events are [0,255]; render spans are half-open [start,end) and may use 256 as the exclusive cycle boundary.
// - Backends may use a documented fixed-lattice analogue approximation; this
// interface guarantees interval ownership and ordering, not transistor-level
// physical exactness.

namespace ArpSID {

static constexpr uint16_t kSidSubcycleResolution = 256u;
static_assert(kSidSubcycleResolution == 256u, "Subphase must stay power-of-two");
static_assert((kSidSubcycleResolution & (kSidSubcycleResolution - 1u)) == 0u, "Subphase resolution must remain power-of-two");
static constexpr uint8_t  kSidSubcycleLast = static_cast<uint8_t>(kSidSubcycleResolution - 1u);
static constexpr uint16_t kSidSubcycleBoundary = kSidSubcycleResolution;
static constexpr uint8_t  kSidSubcycleMax = kSidSubcycleLast;

inline uint16_t boundedSidCyclesPerHostSampleEstimate(double clockHz,
                                                      double sampleRate) noexcept {
    if (!std::isfinite(clockHz) || !std::isfinite(sampleRate) ||
        clockHz <= 0.0 || sampleRate <= 1.0) return 1u;
    const double cps = clockHz / sampleRate;
    if (!std::isfinite(cps) || cps >= 65535.0) return 65535u;
    if (cps <= 1.0) return 1u;
    return static_cast<uint16_t>(std::lround(cps));
}

struct SidRenderInterval {
    uint32_t beginCycle    = 0;
    uint16_t beginSubphase = 0;   // event position [0,255], or 256 as an exclusive boundary
    uint32_t endCycle      = 0;
    uint16_t endSubphase   = 0;   // event position [0,255], or 256 as an exclusive boundary

    bool valid() const noexcept {
        if (beginSubphase > kSidSubcycleBoundary || endSubphase > kSidSubcycleBoundary) return false;
        return endPositionSubphases_() > beginPositionSubphases_();
    }

    // Width in fractional cycles at 8-bit fractional-cycle resolution (256 steps/cycle).
    uint64_t widthSubphases() const noexcept {
        if (!valid()) return 0u;
        return endPositionSubphases_() - beginPositionSubphases_();
    }

private:
    uint64_t beginPositionSubphases_() const noexcept {
        return static_cast<uint64_t>(beginCycle) * kSidSubcycleResolution +
               static_cast<uint64_t>(beginSubphase);
    }
    uint64_t endPositionSubphases_() const noexcept {
        return static_cast<uint64_t>(endCycle) * kSidSubcycleResolution +
               static_cast<uint64_t>(endSubphase);
    }
};

class ISidIntervalRenderable {
public:
    virtual ~ISidIntervalRenderable() = default;

    // Render a precise interval of SID output into outL/outR (accumulated).
    // Backend must advance its internal cursor by exactly this span.
    // Backends must clamp if interval extends beyond remaining cycles for the sample.
    virtual void renderIntervalAccurate(const SidRenderInterval& iv,
                                        float& outL,
                                        float& outR) noexcept = 0;

    // Returns estimated SID cycles per host sample at current sample rate.
    virtual uint16_t estimatedCyclesPerHostSample() const noexcept = 0;

    // Reset internal interval cursor (call at start of each host sample).
    virtual void resetIntervalCursor() noexcept = 0;
};

} // namespace ArpSID
