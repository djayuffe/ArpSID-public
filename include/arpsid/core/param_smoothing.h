// SPDX-License-Identifier: BSD-3-Clause
// param_smoothing.h — Canonical parameter-smoothing utilities (Audit #37).
//
// PROBLEM
// ------
// The audit identified that ArpSID's master-gain smoother handles
// discontinuous master-volume changes well, but other internal parameter
// changes (filter cutoff, resonance, waveform, raw SID register writes)
// can still produce hard discontinuities — audible as crackle when
// automation, modulation, or kit-recall fires.
//
// SIDFilter's per-sample smoothing law was already in place via a
// `smoothCoeff_` field, but the timing constant was inlined in multiple
// places with slight numeric variation. This header makes the smoothing
// law a SINGLE canonical type so:
//
// 1. New parameter paths (waveform, register, drive, etc.) can pick it
// up without re-deriving the coefficient.
// 2. Tests can pin the time constant to a documented value.
// 3. Future engines that want a different time constant just pass it
// to the constructor — the math stays canonical.
//
// CONTRACT
// -------
// * One-pole IIR smoother: `y[n] = y[n-1] + (target - y[n-1]) * coeff`
// * The coefficient is computed from the documented time-constant via
// `1 - exp(-1 / (sampleRate * timeConstantSeconds))`.
// * Default time constant is **1.5 ms** — short enough for snappy
// automation response, long enough to eliminate single-sample clicks.
// * RT-safe (no allocation, no float div in the per-sample step).

#ifndef ARPSID_CORE_PARAM_SMOOTHING_H
#define ARPSID_CORE_PARAM_SMOOTHING_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

// Canonical time constant for ALL audio-path parameter smoothing (ms).
// Audit-pinned: changing this value re-pitches every audible smoother.
inline constexpr double kCanonicalParamSmoothingMs = 1.5;

inline double paramSmoothingCoefficient(double sampleRate, double timeConstantMs) noexcept {
    const double sr = std::max(1.0, sampleRate);
    const double tc = std::max(0.05, timeConstantMs) * 1.0e-3;
    return 1.0 - std::exp(-1.0 / (sr * tc));
}

// One-pole IIR parameter smoother. Trivially copyable so it can live
// inside POD state structs and be snapshotted atomically.
template <typename Scalar>
struct ParameterSmoother {
    Scalar smoothed{};      ///< current smoothed value
    Scalar target{};        ///< user-set target
    double coefficient = 0; ///< computed from (sampleRate, timeConstantMs)

    void prepare(double sampleRate, double timeConstantMs = kCanonicalParamSmoothingMs) noexcept {
        coefficient = paramSmoothingCoefficient(sampleRate, timeConstantMs);
    }
    void snapTo(Scalar value) noexcept { smoothed = value; target = value; }
    void setTarget(Scalar value) noexcept { target = value; }
    Scalar tick() noexcept {
        // y[n] = y[n-1] + coeff * (target - y[n-1])
        smoothed = static_cast<Scalar>(static_cast<double>(smoothed)
                  + static_cast<double>(target - smoothed) * coefficient);
        return smoothed;
    }
    Scalar value() const noexcept { return smoothed; }
};

// Transient detector — counts per-sample output deltas that exceed a
// configurable threshold. Used in audit-#37 diagnostics to surface hard
// discontinuities (which a properly-smoothed parameter path should never
// produce).
//
// The detector is local to the output stage; it does NOT modify the audio.
// It exists so production diagnostics can prove a click-free render path
// and so regression tests can pin the absence of >threshold jumps.
template <typename Scalar>
struct OutputTransientDetector {
    Scalar    previousSample = Scalar(0);
    Scalar    threshold      = Scalar(0.25); // |delta| above which we count a transient
    std::uint64_t transientCount = 0;

    void reset() noexcept { previousSample = Scalar(0); transientCount = 0; }
    void observe(Scalar sample) noexcept {
        const Scalar delta = (sample > previousSample) ? (sample - previousSample)
                                                       : (previousSample - sample);
        if (delta > threshold) ++transientCount;
        previousSample = sample;
    }
};

} // namespace ArpSID

#endif // ARPSID_CORE_PARAM_SMOOTHING_H
