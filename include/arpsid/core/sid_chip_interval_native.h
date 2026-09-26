// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
// sid_chip_interval_native.h — True interval-native SID physics.
//
// This file provides the authoritative physical simulation of SID audio for
// sub-sample intervals. It replaces earlier lerp/blend approximations with:
// 1. Fractional-cycle phase advancement on the shared subcycle lattice
// 2. Exact waveform evaluation at each sub-step position
// 3. Hard-sync and ring-mod resolved at subphase boundaries
// 4. Filter advanced at full-cycle boundaries only (matches hardware)
// 5. Envelope advanced at full-cycle boundaries only (matches hardware)
// 6. Combined-wave authority via the existing SIDVoice render path
// 7. Forensic corrections applied at the final accumulation stage
//
// Design law: no global state mutation happens inside these helpers.
// They take the chip by reference, advance it in-place, and return accumulated
// audio. The caller (BitPerfectEngine, DrSidEngine, SidRegisterEngine) owns
// the cursor bookkeeping.

#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_interval_renderable.h"
#include "sid_interval_renderable.h"
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace ArpSID {

// ─────────────────────────────────────────────────────────────────────────────
// Low-level SIDChip interval-native advance helpers
// ─────────────────────────────────────────────────────────────────────────────

// Advance a SIDChip by exactly `numCycles` full SID clock cycles.
// Returns accumulated audio (sum of per-cycle samples, NOT divided by count).
// The chip's internal state is mutated — this IS the canonical advance.
inline float sidChipAdvanceCyclesNative(SIDChip& chip, int numCycles) noexcept {
    return chip.advanceSidCyclesNative(numCycles);
}

// Advance a SIDChip by exactly `numSubphases` fractional steps on the shared subcycle lattice.
// Returns the blended audio sample for that sub-interval.
//
// Physics model: the SID oscillator phase register increments by freq_reg
// per full SID clock cycle. Within a fractional subcycle window it advances
// on the shared fixed-resolution lattice. Combined-wave output,
// pulse comparison, and ring-mod are evaluated at each sub-step.
// Envelope edges remain cycle-boundary driven. Backend analogue/filter state
// may be numerically integrated at subphase evaluations.
//
// This is invoked by renderIntervalAccurate when iv has non-zero subphase
// boundaries; it delegates to the SIDChip's own subphase render path which
// already implements this contract correctly.
inline float sidChipAdvanceSubphasesNative(SIDChip& chip, uint16_t cycleIdx,
                                           uint16_t subStart, uint16_t subEnd,
                                           int /*totalSampleCycles*/) noexcept {
    (void)cycleIdx; // absolute cursor is owned by the caller; chip state is already at interval begin.
    return chip.advanceSidSubcyclesNative(subStart, subEnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Native interval render — backend-owned fixed-lattice integration, no
// wrapper-level lerp approximation
// ─────────────────────────────────────────────────────────────────────────────

// Canonical entry point for a SIDChip interval render.
// Advances the chip by exactly [beginCycle+beginSubphase, endCycle+endSubphase).
// Returns L and R audio accumulated across the interval.
// This is the ONLY place final physical audio law is computed for a SIDChip.
//
// Algorithm:
// Phase 1: If beginSubphase > 0, render remaining subphases of beginCycle.
// Phase 2: Render whole cycles [beginCycle+1, endCycle).
// Phase 3: If endSubphase > 0, render partial subphases of endCycle.
//
// No wrapper lerp or chip-copy blend. Samples come from the backend's
// fixed-lattice state transitions.
inline void sidChipRenderIntervalNative(SIDChip& chip,
                                         const SidRenderInterval& iv,
                                         float& outL, float& outR) noexcept {
    outL = outR = 0.0f;
    if (!iv.valid()) return;

    const uint32_t bCyc = iv.beginCycle, eCyc = iv.endCycle;
    const uint16_t bSub = iv.beginSubphase, eSub = iv.endSubphase;

    // Count total half-open sub-steps for normalization:
    // Each full cycle = kSidSubcycleResolution fractional steps; partial cycles contribute their actual count.
    // We accumulate and normalize at the end.
    float accum = 0.0f;
    int   steps = 0;

    // Estimate cycles-per-sample for SIDChip planned sample sizing.
    // We read it via the chip's own estimatedCyclesPerHostSample equivalent.
    // Use a conservative estimate: just count actual steps we'll render.

    // Phase 1: leading partial-cycle subphases.
    if (bSub != 0u) {
        const uint16_t subEnd = (bCyc == eCyc) ? eSub : kSidSubcycleBoundary;
        if (subEnd > bSub) {
            const float subAvg = chip.advanceSidSubcyclesNative(bSub, subEnd);
            const int subSteps = static_cast<int>(subEnd - bSub);
            accum += subAvg * static_cast<float>(subSteps);
            steps += subSteps;
        }
    }

    // Phase 2: whole cycles strictly inside the half-open interval.
    // Respect the exclusive end boundary: [0,0)→[1,0) contains exactly cycle 0.
    const uint32_t wholeCycleStart = (bSub > 0) ? bCyc + 1u : bCyc;
    const uint32_t wholeCycleEnd   = eCyc;

    if (wholeCycleEnd > wholeCycleStart) {
        const uint32_t span = wholeCycleEnd - wholeCycleStart;
        const float cycleAccum = chip.advanceSidCyclesNative(static_cast<int>(span));
        const int spanSteps = static_cast<int>(span * static_cast<uint32_t>(kSidSubcycleResolution));
        accum += cycleAccum * static_cast<float>(kSidSubcycleResolution);
        steps += spanSteps;
    }

    // Phase 3: trailing partial-cycle subphases.
    if (eSub > 0 && eCyc >= bCyc && (eCyc != bCyc || bSub == 0u)) {
        const uint16_t subStart = (eCyc == bCyc) ? bSub : 0u;
        if (eSub > subStart) {
            const float subAvg = chip.advanceSidSubcyclesNative(subStart, eSub);
            accum += subAvg * static_cast<float>(eSub - subStart);
            steps += static_cast<int>(eSub - subStart);
        }
    }

    // Re-normalize.
    const float s = (steps > 0) ? (accum / static_cast<float>(steps)) : 0.0f;
    outL = outR = std::clamp(s, -1.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SidRegisterEngine subphase-native write dispatch
// ─────────────────────────────────────────────────────────────────────────────

// Describes a single SID register write with exact sub-sample timing.
struct SidSubphaseWrite {
    uint8_t  regIndex  = 0;
    uint8_t  value     = 0;
    uint16_t cycle     = 0;    // SID clock cycle within the host sample
    uint8_t  subphase  = 0;    // [0, 255): fractional-cycle position
};

// Apply all writes whose timing falls within [beginCycle+beginSub, endCycle+endSub).
// Returns number of writes applied.
template <typename WriteRange, typename ApplyFn>
inline int sidApplyWritesInInterval(WriteRange& writes, int writeCount,
                                     uint32_t beginCycle, uint16_t beginSub,
                                     uint32_t endCycle,   uint16_t endSub,
                                     bool applyToEngine,
                                     ApplyFn&& applyWrite) noexcept {
    int applied = 0;
    for (int i = 0; i < writeCount; ++i) {
        const auto& w = writes[i];
        const uint32_t wPos = (static_cast<uint32_t>(w.cycle) << 8u) + w.subphase;
        const uint32_t bPos = (beginCycle << 8u) + beginSub;
        const uint32_t ePos = (endCycle << 8u) + endSub;
        if (wPos >= bPos && wPos < ePos) {
            if (applyToEngine) applyWrite(w.regIndex, w.value);
            ++applied;
        }
    }
    return applied;
}

// ─────────────────────────────────────────────────────────────────────────────
// Combined-wave + filter authority marker
// ─────────────────────────────────────────────────────────────────────────────

// This type tag marks that the caller has established combined-wave + filter
// authority through the engine's own physics path (SIDChip::renderCurrentState_
// or SidRegisterEngine::renderCurrentState_). No external approximation is
// accepted. The tag is used by higher-level dispatch code to verify the chain.
struct SidCombinedWaveFilterAuthorityTag {
    static constexpr bool isCanonical = true;
};

} // namespace ArpSID
