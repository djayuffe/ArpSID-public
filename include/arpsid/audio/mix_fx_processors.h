// SPDX-License-Identifier: BSD-3-Clause
// mix_fx_processors.h — RT-safe FX processor implementations for MIX channel chain (v554).
//
// Five processor types corresponding to MixFxType in mix_panel_model.h:
// Eq3BandProcessor — low shelf + mid bell + high shelf biquad EQ
// TransientProcessor — dual-envelope attack/sustain shaper
// CompressorProcessor — threshold/ratio/attack/release feed-forward VCA
// SaturatorProcessor — tanh-based drive + character (tape/tube/transistor)
// BitcrusherProcessor — bit-depth reduction + sample-rate reduction (hold)
//
// RUNTIME GUARANTEES:
// - No dynamic allocation: all processor state is fixed-size POD
// - No virtual dispatch: MixFxProcessor uses a tagged struct (not a union)
// with per-type switch dispatch — O(1), no vtable, fully inlineable
// - No locking: single-threaded audio render usage assumed
// - Coefficients recomputed once per setParams() call (block-rate), not
// per sample — safe for typical DAW block sizes ≥ 32 samples
// - Drive parameter in SaturatorProcessor smoothed per-sample via
// ParameterSmoother<float> (skive v533) to suppress automation clicks
// - Compressor gain reduction uses first-order exponential ballistics
// (attack/release coefficients) — per-sample smooth, no ParameterSmoother
// overhead needed
//
// PARAM CONVENTIONS (from MixFxSlot::params[0..7], uint8 0..255 → 0..1 norm):
// Eq3BandProcessor:
// [0] low shelf gain 0..255 → -12..+12 dB (128 = 0 dB)
// [1] low shelf freq 0..255 → 50..1000 Hz (log)
// [2] mid bell gain 0..255 → -12..+12 dB (128 = 0 dB)
// [3] mid bell freq 0..255 → 200..8000 Hz (log)
// [4] mid Q 0..255 → 0.3..8.0 (log)
// [5] high shelf gain 0..255 → -12..+12 dB (128 = 0 dB)
// [6] high shelf freq 0..255 → 2000..16000 Hz (log)
// [7] reserved
// TransientProcessor:
// [0] attack gain 0..255 → -12..+12 dB applied during attack phase (128 = 0 dB)
// [1] sustain gain 0..255 → -12..+12 dB applied during sustain phase (128 = 0 dB)
// CompressorProcessor:
// [0] threshold 0..255 → -40..0 dB
// [1] ratio 0..255 → 1:1..20:1 (log)
// [2] attack 0..255 → 0.1..100 ms (log)
// [3] release 0..255 → 10..500 ms (log)
// [4] makeup 0..255 → 0..+24 dB
// SaturatorProcessor:
// [0] drive 0..255 → 0..+24 dB (linear input gain before soft-clip)
// [1] character 0..84=tape 85..170=tube 171..255=transistor
// BitcrusherProcessor:
// [0] bits 0..255 → 4..16 bits (255 = 16-bit, essentially transparent)
// [1] downsample 0..255 → 16x..1x hold factor (255 = no hold, transparent)
//
// LAYOUT INVARIANTS (pinned via static_assert):
// BiquadCoeffs == 20 bytes
// BiquadState == 16 bytes
// Eq3BandProcessor <= 256 bytes
// TransientProcessor <= 64 bytes
// CompressorProcessor <= 64 bytes
// SaturatorProcessor <= 48 bytes
// BitcrusherProcessor <= 48 bytes
// MixFxProcessor <= 512 bytes
//
// USAGE PATTERN (per channel, once at prepare time):
// MixFxProcessor proc;
// proc.type = slot.type;
// proc.bypass = slot.bypass;
// proc.prepare(sampleRate);
// proc.setParams(slot);
//
// USAGE PATTERN (per sample in render loop):
// if (!proc.bypass) proc.processStereo(L, R);
//
// When MixFxType changes at runtime:
// proc.type = newType;
// proc.prepare(sampleRate); // re-initialises the new processor
// proc.setParams(newSlot);

#ifndef ARPSID_AUDIO_MIX_FX_PROCESSORS_H
#define ARPSID_AUDIO_MIX_FX_PROCESSORS_H

#include "arpsid/gui/mix_panel_model.h"
#include "arpsid/core/param_smoothing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ArpSID {
namespace Audio {

// ─── BiquadCoeffs ─────────────────────────────────────────────────────────────
// Normalised 2nd-order IIR coefficients.
// Convention: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
// (a0 normalised to 1, so a1/a2 stored as-is and SUBTRACTED in the recurrence.)
// Default (b0=1, rest 0) is the identity (pass-through) filter.
struct BiquadCoeffs {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
};
static_assert(sizeof(BiquadCoeffs) == 20, "BiquadCoeffs pinned at 20 bytes");
static_assert(std::is_trivially_copyable<BiquadCoeffs>::value,
              "BiquadCoeffs must be trivially copyable");

// ─── BiquadState ──────────────────────────────────────────────────────────────
// Per-channel (L or R) delay-line state for one 2nd-order section.
struct BiquadState {
    float x1 = 0.f, x2 = 0.f;
    float y1 = 0.f, y2 = 0.f;

    inline float process(float in, const BiquadCoeffs& c) noexcept {
        float out = c.b0 * in + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }
    inline void reset() noexcept { x1 = x2 = y1 = y2 = 0.f; }
};
static_assert(sizeof(BiquadState) == 16, "BiquadState pinned at 16 bytes");
static_assert(std::is_trivially_copyable<BiquadState>::value,
              "BiquadState must be trivially copyable");

// ─── Biquad coefficient computation helpers ───────────────────────────────────
// All formulas from the Audio EQ Cookbook (R. Bristow-Johnson).
// Shelf slope S=1 (maximum steepness). Normalised output (divided by a0).

inline BiquadCoeffs computeLowShelfCoeffs(float sampleRate,
                                           float freqHz,
                                           float gainDb) noexcept {
    const float A  = std::pow(10.f, gainDb / 40.f);  // linear amplitude ratio
    const float w0 = 2.f * 3.14159265358979f * freqHz / sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    // shelf slope S=1: alpha = sinw/2 * sqrt(2) = sinw/sqrt(2)
    const float alpha  = sinw * 0.70710678f;
    const float sqrtA  = std::sqrt(A);
    const float twoSqrtA = 2.f * sqrtA * alpha;

    // Audio EQ Cookbook low-shelf a0 uses PLUS on (A-1)*cosw (differs from high shelf).
    const float a0inv = 1.f / ((A + 1.f) + (A - 1.f) * cosw + twoSqrtA);

    BiquadCoeffs c;
    c.b0 = A * ((A + 1.f) - (A - 1.f) * cosw + twoSqrtA)    * a0inv;
    c.b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw)         * a0inv;
    c.b2 = A * ((A + 1.f) - (A - 1.f) * cosw - twoSqrtA)    * a0inv;
    c.a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw)            * a0inv;
    c.a2 = ((A + 1.f) + (A - 1.f) * cosw - twoSqrtA)        * a0inv;
    return c;
}

inline BiquadCoeffs computeHighShelfCoeffs(float sampleRate,
                                            float freqHz,
                                            float gainDb) noexcept {
    const float A  = std::pow(10.f, gainDb / 40.f);
    const float w0 = 2.f * 3.14159265358979f * freqHz / sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha  = sinw * 0.70710678f;
    const float sqrtA  = std::sqrt(A);
    const float twoSqrtA = 2.f * sqrtA * alpha;

    const float a0inv = 1.f / ((A + 1.f) - (A - 1.f) * cosw + twoSqrtA);
    // Note: high shelf a0 uses different sign pattern from low shelf
    // high shelf: a0 = (A+1) - (A-1)*cosw + twoSqrtA <-- same as low shelf actually
    // Let me recalculate properly from cookbook:
    // High shelf a0 = (A+1) - (A-1)*cosw + 2*sqrt(A)*alpha
    // Wait — that's the same. Let me double-check the cookbook:
    // Low shelf: a0 = (A+1) + (A-1)*cos(w0) + 2*sqrt(A)*alpha <-- NOTE: + (A-1)*cos
    // High shelf: a0 = (A+1) - (A-1)*cos(w0) + 2*sqrt(A)*alpha <-- NOTE: - (A-1)*cos
    // I have the wrong sign above. Let me redo:
    (void)a0inv; // discard the wrong one

    const float a0hs = (A + 1.f) - (A - 1.f) * cosw + twoSqrtA;
    const float inv = 1.f / a0hs;

    BiquadCoeffs c;
    c.b0 = A * ((A + 1.f) + (A - 1.f) * cosw + twoSqrtA)  * inv;
    c.b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw)      * inv;
    c.b2 = A * ((A + 1.f) + (A - 1.f) * cosw - twoSqrtA)  * inv;
    c.a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw)           * inv;
    c.a2 = ((A + 1.f) - (A - 1.f) * cosw - twoSqrtA)      * inv;
    return c;
}

inline BiquadCoeffs computePeakingBellCoeffs(float sampleRate,
                                              float freqHz,
                                              float gainDb,
                                              float Q) noexcept {
    const float A  = std::pow(10.f, gainDb / 40.f);
    const float w0 = 2.f * 3.14159265358979f * freqHz / sampleRate;
    const float cosw  = std::cos(w0);
    const float sinw  = std::sin(w0);
    const float alpha = sinw / (2.f * std::max(Q, 0.05f));

    const float inv = 1.f / (1.f + alpha / A);

    BiquadCoeffs c;
    c.b0 = (1.f + alpha * A)   * inv;
    c.b1 = (-2.f * cosw)       * inv;
    c.b2 = (1.f - alpha * A)   * inv;
    c.a1 = (-2.f * cosw)       * inv;
    c.a2 = (1.f - alpha / A)   * inv;
    return c;
}

// ─── Param-to-value helpers ───────────────────────────────────────────────────

inline float eqGainDb(std::uint8_t p) noexcept {
    return static_cast<float>(p) * (24.f / 255.f) - 12.f;   // 0..255 → -12..+12 dB
}
inline float eqLowFreqHz(std::uint8_t p) noexcept {
    // 0..255 → 50..1000 Hz, log scale: 50 * 20^(p/255)
    return 50.f * std::pow(20.f, static_cast<float>(p) / 255.f);
}
inline float eqMidFreqHz(std::uint8_t p) noexcept {
    // 0..255 → 200..8000 Hz
    return 200.f * std::pow(40.f, static_cast<float>(p) / 255.f);
}
inline float eqHighFreqHz(std::uint8_t p) noexcept {
    // 0..255 → 2000..16000 Hz
    return 2000.f * std::pow(8.f, static_cast<float>(p) / 255.f);
}
inline float eqQValue(std::uint8_t p) noexcept {
    // 0..255 → 0.3..8.0, log scale
    return 0.3f * std::pow(8.f / 0.3f, static_cast<float>(p) / 255.f);
}

// ─── Eq3BandProcessor ────────────────────────────────────────────────────────
struct Eq3BandProcessor {
    // Coefficients — recomputed in setParams(), fixed between calls
    BiquadCoeffs lowCoeffs;   // 20
    BiquadCoeffs midCoeffs;   // 20
    BiquadCoeffs highCoeffs;  // 20

    // Per-channel biquad state (L/R × 3 bands = 6 × 16 bytes = 96)
    BiquadState lowL, lowR;
    BiquadState midL, midR;
    BiquadState highL, highR;

    // Last-set params (kept so prepare() can recalculate after SR change)
    float sampleRate_  = 48000.f;
    float lowFreqHz_   = 200.f;
    float lowGainDb_   = 0.f;
    float midFreqHz_   = 1000.f;
    float midGainDb_   = 0.f;
    float midQ_        = 1.0f;
    float highFreqHz_  = 8000.f;
    float highGainDb_  = 0.f;
    // 8 × 4 = 32 bytes

    // Total: 60 + 96 + 32 = 188 bytes

    inline void prepare(float sampleRate) noexcept {
        sampleRate_ = std::max(8000.f, sampleRate);
        _recomputeCoeffs();
        lowL.reset(); lowR.reset();
        midL.reset(); midR.reset();
        highL.reset(); highR.reset();
    }

    inline void setParams(const GUI::MixFxSlot& slot) noexcept {
        lowGainDb_  = eqGainDb(slot.params[0]);
        lowFreqHz_  = eqLowFreqHz(slot.params[1]);
        midGainDb_  = eqGainDb(slot.params[2]);
        midFreqHz_  = eqMidFreqHz(slot.params[3]);
        midQ_       = eqQValue(slot.params[4]);
        highGainDb_ = eqGainDb(slot.params[5]);
        highFreqHz_ = eqHighFreqHz(slot.params[6]);
        _recomputeCoeffs();
    }

    inline void processStereo(float& L, float& R) noexcept {
        L = highL.process(midL.process(lowL.process(L, lowCoeffs), midCoeffs), highCoeffs);
        R = highR.process(midR.process(lowR.process(R, lowCoeffs), midCoeffs), highCoeffs);
    }

    inline void reset() noexcept {
        lowL.reset(); lowR.reset();
        midL.reset(); midR.reset();
        highL.reset(); highR.reset();
    }

private:
    inline void _recomputeCoeffs() noexcept {
        const float sr = sampleRate_;
        lowCoeffs  = computeLowShelfCoeffs (sr, lowFreqHz_,  lowGainDb_);
        midCoeffs  = computePeakingBellCoeffs(sr, midFreqHz_, midGainDb_, midQ_);
        highCoeffs = computeHighShelfCoeffs(sr, highFreqHz_, highGainDb_);
    }
};
static_assert(sizeof(Eq3BandProcessor) <= 256,
              "Eq3BandProcessor <= 256 bytes");
static_assert(std::is_trivially_copyable<Eq3BandProcessor>::value,
              "Eq3BandProcessor must be trivially copyable");

// ─── TransientProcessor ──────────────────────────────────────────────────────
// Dual-envelope attack/sustain shaper.
// Fast envelope: instantaneous attack, fast release → detects transient onset.
// Slow envelope: slow attack + slow release → tracks body/sustain level.
// When fast >> slow: transient phase → apply attackGain.
// When fast ≈ slow: sustain phase → apply sustainGain.
// Mono sidechain (|L|+|R|)*0.5 drives both envelopes; gain applied to L and R.
struct TransientProcessor {
    float sampleRate_     = 48000.f;
    float attackGainLin_  = 1.f;   // linear multiplier for attack phase
    float sustainGainLin_ = 1.f;   // linear multiplier for sustain phase
    float fastReleaseCoeff_ = 0.f; // per-sample decay factor for fast env
    float slowAttackCoeff_  = 0.f; // per-sample rise factor for slow env
    float slowReleaseCoeff_ = 0.f; // per-sample decay factor for slow env
    float fastEnv_ = 0.f;
    float slowEnv_ = 0.f;
    // padding to reach 32 bytes
    float pad_[2] = {0.f, 0.f};

    // Fast envelope: 0.5 ms release; instant attack (replace if bigger)
    // Slow envelope: 20 ms attack, 200 ms release
    inline void prepare(float sampleRate) noexcept {
        sampleRate_ = std::max(8000.f, sampleRate);
        const float sr = sampleRate_;
        fastReleaseCoeff_ = std::exp(-1.f / (sr * 0.0005f));  // 0.5 ms
        slowAttackCoeff_  = 1.f - std::exp(-1.f / (sr * 0.020f));  // 20 ms
        slowReleaseCoeff_ = std::exp(-1.f / (sr * 0.200f));  // 200 ms
        fastEnv_ = 0.f;
        slowEnv_ = 0.f;
    }

    inline void setParams(const GUI::MixFxSlot& slot) noexcept {
        // params[0] attack gain: 0..255 → -12..+12 dB → linear
        const float attackDb  = eqGainDb(slot.params[0]);
        const float sustainDb = eqGainDb(slot.params[1]);
        attackGainLin_  = std::pow(10.f, attackDb  / 20.f);
        sustainGainLin_ = std::pow(10.f, sustainDb / 20.f);
    }

    inline void processStereo(float& L, float& R) noexcept {
        const float sc = (std::abs(L) + std::abs(R)) * 0.5f;

        // Fast envelope: peak follower with instantaneous attack
        if (sc > fastEnv_)
            fastEnv_ = sc;
        else
            fastEnv_ *= fastReleaseCoeff_;

        // Slow envelope: first-order IIR
        if (sc > slowEnv_)
            slowEnv_ += (sc - slowEnv_) * slowAttackCoeff_;
        else
            slowEnv_ *= slowReleaseCoeff_;

        // Transient ratio: how much bigger is fast vs slow? Clamp 0..1.
        const float slowSafe = slowEnv_ + 1e-6f;
        const float tr = std::min(1.f, std::max(0.f, (fastEnv_ - slowEnv_) / slowSafe));

        // Interpolate: sustain gain + (attack - sustain) * tr
        const float gain = sustainGainLin_ + (attackGainLin_ - sustainGainLin_) * tr;
        L *= gain;
        R *= gain;
    }

    inline void reset() noexcept { fastEnv_ = 0.f; slowEnv_ = 0.f; }
};
static_assert(sizeof(TransientProcessor) <= 64,
              "TransientProcessor <= 64 bytes");
static_assert(std::is_trivially_copyable<TransientProcessor>::value,
              "TransientProcessor must be trivially copyable");

// ─── CompressorProcessor ─────────────────────────────────────────────────────
// Feed-forward VCA compressor with peak level detection.
// Gain reduction computed in dB domain; ballistics applied per-sample via
// first-order hold (separate attack/release envelopes on the gain reduction).
struct CompressorProcessor {
    float sampleRate_     = 48000.f;
    float thresholdDb_    = -12.f;
    float ratio_          = 4.f;
    float attackCoeff_    = 0.f;
    float releaseCoeff_   = 0.f;
    float makeupGainLin_  = 1.f;
    float envL_           = 0.f;
    float envR_           = 0.f;
    float grSmoothedL_    = 0.f;   // current gain reduction in dB (≤ 0)
    float grSmoothedR_    = 0.f;
    // padding
    float pad_[2] = {0.f, 0.f};

    inline void prepare(float sampleRate) noexcept {
        sampleRate_ = std::max(8000.f, sampleRate);
        envL_ = envR_ = 0.f;
        grSmoothedL_ = grSmoothedR_ = 0.f;
    }

    inline void setParams(const GUI::MixFxSlot& slot) noexcept {
        // threshold: 0..255 → -40..0 dB
        thresholdDb_ = static_cast<float>(slot.params[0]) * (40.f / 255.f) - 40.f;
        // ratio: 0..255 → 1:1..20:1, log
        ratio_ = 1.f + 19.f * std::pow(static_cast<float>(slot.params[1]) / 255.f, 1.5f);
        // attack ms: 0..255 → 0.1..100 ms, log
        const float atkMs = 0.1f * std::pow(1000.f, static_cast<float>(slot.params[2]) / 255.f);
        // release ms: 0..255 → 10..500 ms, log
        const float relMs = 10.f * std::pow(50.f, static_cast<float>(slot.params[3]) / 255.f);
        const float sr = sampleRate_;
        attackCoeff_  = std::exp(-1.f / (sr * atkMs * 0.001f));
        releaseCoeff_ = std::exp(-1.f / (sr * relMs * 0.001f));
        // makeup: 0..255 → 0..+24 dB
        const float mkupDb = static_cast<float>(slot.params[4]) * (24.f / 255.f);
        makeupGainLin_ = std::pow(10.f, mkupDb / 20.f);
    }

    inline void processStereo(float& L, float& R) noexcept {
        // Peak level detection (per channel, no RMS window for RT-simplicity)
        const float absL = std::abs(L);
        const float absR = std::abs(R);
        envL_ = absL > envL_ ? absL : envL_ * releaseCoeff_;
        envR_ = absR > envR_ ? absR : envR_ * releaseCoeff_;

        // Compute gain reduction target in dB
        auto gainReduction = [&](float env) -> float {
            if (env < 1e-8f) return 0.f;
            const float envDb = 20.f * std::log10(env);
            if (envDb <= thresholdDb_) return 0.f;
            return (thresholdDb_ - envDb) * (1.f - 1.f / ratio_); // negative value
        };
        const float grTargL = gainReduction(envL_);
        const float grTargR = gainReduction(envR_);

        // Smooth gain reduction with attack/release ballistics
        grSmoothedL_ = (grTargL < grSmoothedL_)
                       ? grTargL  + (grSmoothedL_ - grTargL)  * attackCoeff_
                       : grTargL  + (grSmoothedL_ - grTargL)  * releaseCoeff_;
        grSmoothedR_ = (grTargR < grSmoothedR_)
                       ? grTargR  + (grSmoothedR_ - grTargR)  * attackCoeff_
                       : grTargR  + (grSmoothedR_ - grTargR)  * releaseCoeff_;

        // Apply gain reduction + makeup
        L *= std::pow(10.f, grSmoothedL_ / 20.f) * makeupGainLin_;
        R *= std::pow(10.f, grSmoothedR_ / 20.f) * makeupGainLin_;
    }

    inline void reset() noexcept {
        envL_ = envR_ = 0.f;
        grSmoothedL_ = grSmoothedR_ = 0.f;
    }
};
static_assert(sizeof(CompressorProcessor) <= 64,
              "CompressorProcessor <= 64 bytes");
static_assert(std::is_trivially_copyable<CompressorProcessor>::value,
              "CompressorProcessor must be trivially copyable");

// ─── SaturatorProcessor ──────────────────────────────────────────────────────
// Soft-saturation via drive + character curve.
// Drive ParameterSmoother<float> (v533) prevents automation clicks.
// Character modes (from params[1]):
// 0..84: tape — tanh(x) / tanh(1) — symmetric, smooth
// 85..170: tube — asymmetric tanh with positive DC offset character
// 171..255: transistor — harder clip: x/(1+|x|)^0.5 * sqrt(2)
enum class SatCharacter : std::uint8_t { Tape=0, Tube=1, Transistor=2 };

struct SaturatorProcessor {
    ParameterSmoother<float> driveSmoother_;   // 16 bytes
    float sampleRate_     = 48000.f;
    float driveLinear_    = 1.f;
    SatCharacter character_ = SatCharacter::Tape;
    std::uint8_t pad_[3]   = {};

    inline void prepare(float sampleRate) noexcept {
        sampleRate_ = std::max(8000.f, sampleRate);
        driveSmoother_.prepare(static_cast<double>(sampleRate_));
        driveSmoother_.snapTo(driveLinear_);
    }

    inline void setParams(const GUI::MixFxSlot& slot) noexcept {
        // drive: 0..255 → 0..+24 dB
        const float driveDb = static_cast<float>(slot.params[0]) * (24.f / 255.f);
        driveLinear_ = std::pow(10.f, driveDb / 20.f);
        driveSmoother_.setTarget(driveLinear_);

        // character: 0..84=tape, 85..170=tube, 171..255=transistor
        const std::uint8_t ch = slot.params[1];
        if      (ch <= 84u)  character_ = SatCharacter::Tape;
        else if (ch <= 170u) character_ = SatCharacter::Tube;
        else                 character_ = SatCharacter::Transistor;
    }

    inline void processStereo(float& L, float& R) noexcept {
        const float drive = driveSmoother_.tick();
        const float safeDrive = std::max(drive, 1e-4f);
        L = _saturate(L, drive, safeDrive);
        R = _saturate(R, drive, safeDrive);
    }

    inline void reset() noexcept {
        driveSmoother_.snapTo(driveLinear_);
    }

private:
    inline float _saturate(float x, float drive, float safeDrive) const noexcept {
        const float xd = x * safeDrive;
        switch (character_) {
            case SatCharacter::Tape: {
                // tanh normalised by tanh(drive): DC gain preserved for |x| << 1
                const float tDrive = std::tanh(safeDrive);
                return tDrive > 1e-6f ? std::tanh(xd) / tDrive : x;
            }
            case SatCharacter::Tube: {
                // Asymmetric: positive half uses tanh, negative uses softer curve
                const float tDrive = std::tanh(safeDrive);
                if (xd >= 0.f)
                    return tDrive > 1e-6f ? std::tanh(xd) / tDrive : x;
                else
                    return xd / (1.f + std::abs(xd * 0.5f)) / safeDrive;
            }
            case SatCharacter::Transistor: {
                // Harder algebraic clip: x / sqrt(1 + x^2), normalised
                const float norm = safeDrive / std::sqrt(1.f + safeDrive * safeDrive);
                return norm > 1e-6f ? (xd / std::sqrt(1.f + xd * xd)) / norm : x;
            }
        }
        return x;
    }
};
static_assert(sizeof(SaturatorProcessor) <= 48,
              "SaturatorProcessor <= 48 bytes");
static_assert(std::is_trivially_copyable<SaturatorProcessor>::value,
              "SaturatorProcessor must be trivially copyable");

// ─── BitcrusherProcessor ─────────────────────────────────────────────────────
// Bit-depth reduction + sample-rate reduction (zero-order hold).
// At full settings (params[0]=255, params[1]=255): transparent (pass-through).
struct BitcrusherProcessor {
    float    sampleRate_  = 48000.f;
    float    maxQuant_    = 32767.f;  // 2^(quantBits-1) - 1
    float    heldL_       = 0.f;
    float    heldR_       = 0.f;
    std::uint32_t holdCounter_  = 0u;
    std::uint8_t  quantBits_    = 16u;
    std::uint8_t  holdFactor_   = 1u;
    std::uint8_t  pad_[2]       = {};

    inline void prepare(float sampleRate) noexcept {
        sampleRate_ = std::max(8000.f, sampleRate);
        heldL_ = heldR_ = 0.f;
        holdCounter_ = 0u;
    }

    inline void setParams(const GUI::MixFxSlot& slot) noexcept {
        // bits: 0..255 → 4..16 bits (255 = 16-bit)
        const std::uint8_t rawBits = slot.params[0];
        quantBits_ = static_cast<std::uint8_t>(
            4u + static_cast<unsigned>(rawBits) * 12u / 255u);  // 4..16
        maxQuant_ = static_cast<float>((1u << (quantBits_ - 1u)) - 1u);

        // downsample: 0..255 → holdFactor 16..1
        const std::uint8_t rawDs = slot.params[1];
        holdFactor_ = static_cast<std::uint8_t>(
            16u - static_cast<unsigned>(rawDs) * 15u / 255u);  // 16..1
    }

    inline void processStereo(float& L, float& R) noexcept {
        // Sample-rate reduction: advance held values at holdFactor intervals
        if (holdFactor_ <= 1u) {
            heldL_ = L;
            heldR_ = R;
        } else {
            if (holdCounter_ == 0u) {
                heldL_ = L;
                heldR_ = R;
            }
            ++holdCounter_;
            if (holdCounter_ >= static_cast<std::uint32_t>(holdFactor_))
                holdCounter_ = 0u;
        }

        // Bit-depth reduction: quantise to quantBits_
        const float q = maxQuant_;
        if (q > 0.5f) {
            L = std::round(heldL_ * q) / q;
            R = std::round(heldR_ * q) / q;
        } else {
            L = 0.f;
            R = 0.f;
        }
    }

    inline void reset() noexcept {
        heldL_ = heldR_ = 0.f;
        holdCounter_ = 0u;
    }
};
static_assert(sizeof(BitcrusherProcessor) <= 48,
              "BitcrusherProcessor <= 48 bytes");
static_assert(std::is_trivially_copyable<BitcrusherProcessor>::value,
              "BitcrusherProcessor must be trivially copyable");

// ─── MixFxProcessor ──────────────────────────────────────────────────────────
// Tagged struct containing all 5 processor types.
// Only the processor matching `type` is active; others hold zero state
// after construction or reset(). No union is used — avoids C++ union
// complexity while remaining trivially copyable.
//
// Invariants:
// - After default construction: type=None, bypass=0, all processor state zero.
// - After prepare(sr): active processor's prepare() called; state cleared.
// - After setParams(slot): active processor's coefficients updated.
// - processStereo(L,R): dispatched via switch, zero overhead for None.
struct MixFxProcessor {
    GUI::MixFxType       type   = GUI::MixFxType::None;
    std::uint8_t         bypass = 0u;
    std::uint8_t         pad_[2] = {};

    Eq3BandProcessor     eq3Band;
    TransientProcessor   transient;
    CompressorProcessor  compressor;
    SaturatorProcessor   saturator;
    BitcrusherProcessor  bitcrusher;

    inline void prepare(float sampleRate) noexcept {
        switch (type) {
            case GUI::MixFxType::Eq3Band:    eq3Band.prepare(sampleRate);    return;
            case GUI::MixFxType::Transient:  transient.prepare(sampleRate);  return;
            case GUI::MixFxType::Compressor: compressor.prepare(sampleRate); return;
            case GUI::MixFxType::Saturator:  saturator.prepare(sampleRate);  return;
            case GUI::MixFxType::Bitcrusher: bitcrusher.prepare(sampleRate); return;
            case GUI::MixFxType::None:       return;
        }
    }

    inline void setParams(const GUI::MixFxSlot& slot) noexcept {
        type   = slot.type;
        bypass = slot.bypass;
        switch (type) {
            case GUI::MixFxType::Eq3Band:    eq3Band.setParams(slot);    return;
            case GUI::MixFxType::Transient:  transient.setParams(slot);  return;
            case GUI::MixFxType::Compressor: compressor.setParams(slot); return;
            case GUI::MixFxType::Saturator:  saturator.setParams(slot);  return;
            case GUI::MixFxType::Bitcrusher: bitcrusher.setParams(slot); return;
            case GUI::MixFxType::None:       return;
        }
    }

    inline void processStereo(float& L, float& R) noexcept {
        if (bypass) return;
        switch (type) {
            case GUI::MixFxType::Eq3Band:    eq3Band.processStereo(L, R);    return;
            case GUI::MixFxType::Transient:  transient.processStereo(L, R);  return;
            case GUI::MixFxType::Compressor: compressor.processStereo(L, R); return;
            case GUI::MixFxType::Saturator:  saturator.processStereo(L, R);  return;
            case GUI::MixFxType::Bitcrusher: bitcrusher.processStereo(L, R); return;
            case GUI::MixFxType::None:       return;
        }
    }

    inline void reset() noexcept {
        switch (type) {
            case GUI::MixFxType::Eq3Band:    eq3Band.reset();    return;
            case GUI::MixFxType::Transient:  transient.reset();  return;
            case GUI::MixFxType::Compressor: compressor.reset(); return;
            case GUI::MixFxType::Saturator:  saturator.reset();  return;
            case GUI::MixFxType::Bitcrusher: bitcrusher.reset(); return;
            case GUI::MixFxType::None:       return;
        }
    }
};
static_assert(sizeof(MixFxProcessor) <= 512,
              "MixFxProcessor <= 512 bytes");
static_assert(std::is_trivially_copyable<MixFxProcessor>::value,
              "MixFxProcessor must be trivially copyable");

// ─── applyMixFxChain ─────────────────────────────────────────────────────────
// Convenience: apply all 5 FX slots in a channel's chain (serial processing).
// slots[] matches the MixChannel::fxSlots layout (kMixFxSlotsPerChannel = 5).
// processors[] must have been prepared at the current sample rate and have
// their params set via setParams() before this function is called.
inline void applyMixFxChain(MixFxProcessor processors[GUI::kMixFxSlotsPerChannel],
                             const GUI::MixFxSlot slots[GUI::kMixFxSlotsPerChannel],
                             float& L,
                             float& R) noexcept {
    for (std::uint8_t i = 0; i < GUI::kMixFxSlotsPerChannel; ++i) {
        if (slots[i].type == GUI::MixFxType::None) continue;
        if (slots[i].bypass) continue;
        processors[i].processStereo(L, R);
    }
}

} // namespace Audio
} // namespace ArpSID

#endif // ARPSID_AUDIO_MIX_FX_PROCESSORS_H
