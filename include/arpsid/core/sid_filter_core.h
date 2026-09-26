// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sid_filter_core.h — Canonical SID-filter helpers shared by SIDChip's
// `SIDFilter` and SidRegisterEngine's `Filter` (Audit #31, #32).
//
// PROBLEM
// ------// The audit identified that ArpSID has TWO trapezoidal-SVF filter
// implementations that were drifting silently:
//
// * `SIDChip::SIDFilter` (sid_chip.h:813+) carries 6581-specific
// op-amp loading "squash" + tanh saturation under high-drive +
// high-resonance.
// * `SidRegisterEngine::Filter` (sid_register_engine.h:1224+) used the
// same SVF math but WITHOUT the 6581 nonlinearity — so a 6581 bass
// patch driven with the same register state through the BitPerfect
// path and the SidRegister path produced two different envelopes.
//
// The audit demanded the nonlinearity be MIRRORED in both engines so
// identical register state always produces identical audio.
//
// SCOPE OF THIS HEADER
// -------------------// * Canonical `applyModelNonlinearity_6581_loadingAndTanh()` helper.
// Both filter classes call it. If anyone ever modifies the law in
// one place, the build still uses the shared implementation, so
// there is no possible drift.
// * `effectiveCutoffSquashFor6581_loading()` — the same per-sample
// cutoff squashing both filters share.
// * Documented invariants the audit cares about, pinned with
// static_assert / constexpr where possible.
//
// This header is intentionally lightweight: it does NOT take over the
// SVF integrator math (the two filters have different g-coefficient
// pre-warp policies — SIDChip uses `2π·cutoff/clock`, the register
// engine uses `tan(π·cutoff/sampleRate)`. Those are conscious model
// choices, not bugs — see the audit's #31 note that "drift between
// them" is the bug, not "two implementations" per se. By sharing the
// nonlinearity we kill the most audible drift surface — the 6581
// op-amp character — while keeping the integrator pre-warp policy
// each engine was tuned for.

#ifndef ARPSID_CORE_SID_FILTER_CORE_H
#define ARPSID_CORE_SID_FILTER_CORE_H

#include "arpsid/core/math_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

// Forward decl to avoid pulling sid_chip.h here (the enum is small and
// stable enough to mirror).
enum class SIDModel : std::uint8_t;

namespace FilterCore {

// Audit-#32 thresholds at which the 6581 op-amp loading effect kicks in.
// Mirrored across both filter implementations so drift is impossible.
inline constexpr float kLoadingDriveThreshold     = 0.35f;
inline constexpr float kLoadingResonanceThreshold = 0.45f;

// Per-sample cutoff squash for the 6581 op-amp loading effect. Returns
// the EFFECTIVE cutoff (in Hz) given the smoothed nominal cutoff, the
// instantaneous drive level, and the resonance setting. The squash is
// bounded so it cannot collapse the cutoff below ~84 % of nominal even
// at maximum drive + resonance. This is the canonical formula both
// filters share — see audit #32.
//
// `driveNorm` is the per-sample |input| level, scaled into ~[0,1].
// `resNorm` is the resonance setting in [0,1].
inline double effectiveCutoffHz_6581_loading(double smoothedCutoffHz,
                                              float  driveNorm,
                                              float  resNorm,
                                              bool   is6581) noexcept {
    if (!is6581) return smoothedCutoffHz;
    if (driveNorm <= kLoadingDriveThreshold || resNorm <= kLoadingResonanceThreshold) {
        return smoothedCutoffHz;
    }
    const double squash = 1.0 - std::clamp(
        (double)(driveNorm - kLoadingDriveThreshold) * (double)resNorm * 0.18,
        0.0, 0.16);
    return smoothedCutoffHz * squash;
}

// Apply the 6581 op-amp loading nonlinearity + tanh saturation to the
// pre-integrator filter input. Returns the saturated input.
// Both filters apply this immediately AFTER the resonance feedback
// subtraction, BEFORE the SVF integrator step. Mirrored across engines
// per audit #32.
inline float applyModelNonlinearity(float input,
                                    float driveNorm,
                                    float resNorm,
                                    bool  is6581) noexcept {
    if (!is6581) return input;
    if (driveNorm <= kLoadingDriveThreshold || resNorm <= kLoadingResonanceThreshold) {
        return input;
    }
    const float sat = std::clamp((driveNorm - kLoadingDriveThreshold) * resNorm, 0.0f, 1.0f);
    return std::tanh(input * (1.0f + 0.85f * sat)) / (1.0f + 0.35f * sat);
}

// Canonical 6581 vs 8580 feedback coefficient. Mirrored across engines
// per audit #31.
inline float resonanceFeedbackCoefficient(float resNorm, bool is6581) noexcept {
    return is6581 ? (0.040f + 0.095f * resNorm)
                  : (0.022f + 0.135f * resNorm);
}

// Canonical 6581 vs 8580 integrator-leak coefficients. The leak2 factor
// differs by model because the second-stage integrator has different
// loading on each chip.
inline double integratorLeak1(double integratorLeak) noexcept {
    return std::clamp(1.0 - integratorLeak, 0.0, 1.0);
}
inline double integratorLeak2(double integratorLeak, bool is6581) noexcept {
    return std::clamp(1.0 - integratorLeak * (is6581 ? 0.52 : 0.36), 0.0, 1.0);
}

// Canonical absolute safety clamp the audit #38 fix introduced. Both
// filters apply this to their output. Audit #31 demanded the output
// range be identical across engines.
inline float absoluteSafetyClamp(float x) noexcept {
    return std::clamp(x, -8.0f, 8.0f);
}

} // namespace FilterCore
} // namespace ArpSID

#endif // ARPSID_CORE_SID_FILTER_CORE_H
