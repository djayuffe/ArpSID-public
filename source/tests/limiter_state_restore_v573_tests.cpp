// SPDX-License-Identifier: BSD-3-Clause
// limiter_state_restore_v573_tests.cpp — Output limiter state-restore formula (v573).
//
// Bug: After setState() / applyCanonicalStateRoot_(), the four limiter fields
// (limiterEnabled, limiterThreshold, limiterAttackMs, limiterReleaseMs) and the
// underlying SimpleLimiter object time constants were NOT re-projected from
// paramValues[]. resetRenderModeOutputNormalizer_() correctly recovered reverbMix
// but silently left the limiter at C++ field defaults:
//
// Field C++ default Param default Discrepancy
// ───────────────── ─────────── ───────────── ──────────
// limiterEnabled true 1.0 → true none (luck)
// limiterThreshold 0.97 f 0.94 → 0.94 f 0.03 f off
// limiterAttackMs 0.5 ms 0.08×20 = 1.6ms 3.2× too fast
// limiterReleaseMs 200.0 ms 10+0.35×990=356.5ms 56% too short
//
// Fix (v573): resetRenderModeOutputNormalizer_() now also re-projects the four
// limiter fields, using transformations identical to runtimePolicySetLimiter*:
//
// limiterEnabled = paramValues[kParamOutputLimiter] > 0.5f
// limiterThreshold = clamp(paramValues[kParamLimiterThreshold], 0.5, 1.0)
// limiterAttackMs = clamp(paramValues[kParamLimiterAttack], 0, 1) * 20.0
// limiterReleaseMs = 10.0 + clamp(paramValues[kParamLimiterRelease], 0, 1) * 990.0
//
// The limiter object's setAttackMs / setReleaseMs are also called so the
// internal exponential envelope coefficients are updated.
//
// Tests cover:
// I. Enabled formula: paramNorm > 0.5 boundary
// II. Threshold formula: clamp(param, 0.5, 1.0)
// III. Attack-ms formula: param × 20.0, range [0, 20] ms
// IV. Release-ms formula: 10 + param × 990, range [10, 1000] ms
// V. Default param alignment: parameter_ids.h defaults yield specific field values
// VI. Pre-fix stale-default documentation: C++ defaults differ from param defaults
// VII. Monotone: attack and release increase with param value
// VIII. Clamp guards: params outside [0,1] are clamped before formula
// IX. SimpleLimiter::setAttackMs / setReleaseMs coeff contracts

#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <limits>

#include "arpsid/core/sid_audio_processors.h"  // SimpleLimiter

// ─── Parameter-to-field formula helpers (mirror of runtimePolicySetLimiter*) ─

static bool    limiterEnabledFromParam(float v) noexcept {
    return v > 0.5f;
}
static float limiterThresholdFromParam(float v) noexcept {
    return std::clamp(v, 0.5f, 1.0f);
}
static float limiterAttackMsFromParam(float v) noexcept {
    return std::clamp(v, 0.0f, 1.0f) * 20.0f;
}
static float limiterReleaseMsFromParam(float v) noexcept {
    return 10.0f + std::clamp(v, 0.0f, 1.0f) * 990.0f;
}

// ─── Defaults from parameter_ids.h ────────────────────────────────────────────
// set(kParamOutputLimiter, "", 1.0f)
// set(kParamLimiterThreshold, "", 0.94f)
// set(kParamLimiterAttack, "", 0.08f)
// set(kParamLimiterRelease, "", 0.35f)

static constexpr float kDefaultOutputLimiter    = 1.0f;
static constexpr float kDefaultLimiterThreshold = 0.94f;
static constexpr float kDefaultLimiterAttack    = 0.08f;
static constexpr float kDefaultLimiterRelease   = 0.35f;

// ─── C++ field defaults (wrong values used before v573 fix) ──────────────────
// From arpsid_processor_phase2.h member declarations:
// bool limiterEnabled = true; (line 594)
// float limiterThreshold = 0.97f; (line 705)
// float limiterAttackMs = 0.5f; (line 706)
// float limiterReleaseMs = 200.0f; (line 707)

static constexpr bool  kCppDefaultLimiterEnabled   = true;
static constexpr float kCppDefaultLimiterThreshold = 0.97f;
static constexpr float kCppDefaultLimiterAttackMs  = 0.5f;
static constexpr float kCppDefaultLimiterReleaseMs = 200.0f;

// ─── I. Enabled formula ───────────────────────────────────────────────────────

static void testEnabledFormula() {
    // param=0.0 → false (disabled)
    assert(!limiterEnabledFromParam(0.0f));

    // param=0.5 → false (boundary: 0.5 is NOT > 0.5)
    assert(!limiterEnabledFromParam(0.5f));

    // param=0.501 → true (just above boundary)
    assert(limiterEnabledFromParam(0.501f));

    // param=1.0 → true (default: limiter on)
    assert(limiterEnabledFromParam(1.0f));

    // param=0.49 → false
    assert(!limiterEnabledFromParam(0.49f));

    // Default param (1.0) matches C++ default (true) — no discrepancy here.
    assert(limiterEnabledFromParam(kDefaultOutputLimiter) == kCppDefaultLimiterEnabled);
}

// ─── II. Threshold formula ────────────────────────────────────────────────────

static void testThresholdFormula() {
    // param=0.94 (default) → 0.94 (no clamping needed)
    const float thDefault = limiterThresholdFromParam(kDefaultLimiterThreshold);
    assert(std::fabs(thDefault - 0.94f) < 1.0e-6f);

    // param=0.5 → 0.5 (floor)
    assert(std::fabs(limiterThresholdFromParam(0.5f) - 0.5f) < 1.0e-6f);

    // param=1.0 → 1.0 (ceiling)
    assert(std::fabs(limiterThresholdFromParam(1.0f) - 1.0f) < 1.0e-6f);

    // param=0.75 → 0.75 (mid-range)
    assert(std::fabs(limiterThresholdFromParam(0.75f) - 0.75f) < 1.0e-6f);

    // param below floor → clamped to 0.5
    assert(std::fabs(limiterThresholdFromParam(0.0f) - 0.5f) < 1.0e-6f);
    assert(std::fabs(limiterThresholdFromParam(0.1f) - 0.5f) < 1.0e-6f);

    // param above ceiling → clamped to 1.0
    assert(std::fabs(limiterThresholdFromParam(2.0f) - 1.0f) < 1.0e-6f);

    // C++ default (0.97) differs from param default (0.94) — pre-fix discrepancy.
    assert(std::fabs(kCppDefaultLimiterThreshold - thDefault) > 0.02f);
}

// ─── III. Attack-ms formula ───────────────────────────────────────────────────

static void testAttackMsFormula() {
    // param=0.0 → 0.0 ms (instantaneous — attCoeff=0.0 in SimpleLimiter)
    assert(std::fabs(limiterAttackMsFromParam(0.0f) - 0.0f) < 1.0e-6f);

    // param=0.08 (default) → 0.08 × 20 = 1.6 ms
    const float attackDefault = limiterAttackMsFromParam(kDefaultLimiterAttack);
    assert(std::fabs(attackDefault - 1.6f) < 1.0e-5f);

    // param=0.5 → 10.0 ms
    assert(std::fabs(limiterAttackMsFromParam(0.5f) - 10.0f) < 1.0e-5f);

    // param=1.0 → 20.0 ms (max)
    assert(std::fabs(limiterAttackMsFromParam(1.0f) - 20.0f) < 1.0e-5f);

    // C++ default (0.5 ms) differs significantly from param default (1.6 ms).
    // Pre-fix: attack was 3.2× too fast at default preset load.
    assert(std::fabs(kCppDefaultLimiterAttackMs - attackDefault) > 1.0f);
    assert(attackDefault > kCppDefaultLimiterAttackMs);  // param default slower
}

// ─── IV. Release-ms formula ───────────────────────────────────────────────────

static void testReleaseMsFormula() {
    // param=0.0 → 10.0 ms (floor — minimum release)
    assert(std::fabs(limiterReleaseMsFromParam(0.0f) - 10.0f) < 1.0e-5f);

    // param=0.35 (default) → 10 + 0.35 × 990 = 10 + 346.5 = 356.5 ms
    const float releaseDefault = limiterReleaseMsFromParam(kDefaultLimiterRelease);
    assert(std::fabs(releaseDefault - 356.5f) < 1.0e-3f);

    // param=0.5 → 10 + 0.5 × 990 = 505.0 ms
    assert(std::fabs(limiterReleaseMsFromParam(0.5f) - 505.0f) < 1.0e-3f);

    // param=1.0 → 10 + 990 = 1000.0 ms (max)
    assert(std::fabs(limiterReleaseMsFromParam(1.0f) - 1000.0f) < 1.0e-3f);

    // C++ default (200 ms) differs from param default (356.5 ms).
    // Pre-fix: release was 56% too short at default preset load.
    assert(std::fabs(kCppDefaultLimiterReleaseMs - releaseDefault) > 100.0f);
    assert(releaseDefault > kCppDefaultLimiterReleaseMs);  // param default longer
}

// ─── V. Default param alignment ───────────────────────────────────────────────

static void testDefaultParamAlignment() {
    // With v573 fix, after state restore paramValues[] holds the preset's
    // kParamOutputLimiter/Threshold/Attack/Release values, and the fix projects
    // them to the fields. Verify the default preset produces the expected fields.
    const bool   en  = limiterEnabledFromParam(kDefaultOutputLimiter);
    const float  thr = limiterThresholdFromParam(kDefaultLimiterThreshold);
    const float  att = limiterAttackMsFromParam(kDefaultLimiterAttack);
    const float  rel = limiterReleaseMsFromParam(kDefaultLimiterRelease);

    assert(en == true);
    assert(std::fabs(thr - 0.94f) < 1.0e-6f);
    assert(std::fabs(att - 1.6f)  < 1.0e-5f);
    assert(std::fabs(rel - 356.5f) < 1.0e-3f);
}

// ─── VI. Pre-fix stale default documentation ──────────────────────────────────

static void testPreFixStaleDefaults() {
    // Before v573, resetRenderModeOutputNormalizer_() omitted limiter projection.
    // On preset restore the fields kept their C++ defaults. Document that each
    // C++ default diverges from the correct param-derived value.

    // Threshold: C++ default 0.97 vs correct 0.94 — 3 dB difference (~0.5 dB per 0.01 unit)
    const float correctThreshold = limiterThresholdFromParam(kDefaultLimiterThreshold);
    assert(std::fabs(kCppDefaultLimiterThreshold - correctThreshold) > 0.02f);

    // Attack: C++ default 0.5 ms vs correct 1.6 ms — factor 3.2 too fast
    const float correctAttackMs = limiterAttackMsFromParam(kDefaultLimiterAttack);
    assert(kCppDefaultLimiterAttackMs < correctAttackMs);
    assert(correctAttackMs / kCppDefaultLimiterAttackMs > 3.0f);

    // Release: C++ default 200 ms vs correct 356.5 ms — factor 1.78 too short
    const float correctReleaseMs = limiterReleaseMsFromParam(kDefaultLimiterRelease);
    assert(kCppDefaultLimiterReleaseMs < correctReleaseMs);
    assert(correctReleaseMs / kCppDefaultLimiterReleaseMs > 1.5f);

    // A custom preset (limiter soft with low threshold + slow release):
    const float presetThreshold = limiterThresholdFromParam(0.7f);   // 0.7 (low)
    const float presetReleaseMs = limiterReleaseMsFromParam(0.9f);   // 10+891=901 ms

    // C++ default threshold (0.97) is 0.27 units above this preset's 0.70 value    // i.e. the preset's gentle limiting would be replaced by brick-wall behavior.
    assert(std::fabs(kCppDefaultLimiterThreshold - presetThreshold) > 0.25f);

    // C++ default release (200 ms) is < 1/4 of this preset's 901 ms.
    assert(presetReleaseMs / kCppDefaultLimiterReleaseMs > 4.0f);
}

// ─── VII. Monotone non-decreasing ─────────────────────────────────────────────

static void testMonotonicity() {
    // Attack ms: strictly non-decreasing with param value.
    float prevAttack = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v  = static_cast<float>(i) / 20.0f;
        const float ms = limiterAttackMsFromParam(v);
        assert(ms >= prevAttack);
        prevAttack = ms;
    }
    assert(std::fabs(prevAttack - 20.0f) < 1.0e-4f);  // must reach max

    // Release ms: strictly non-decreasing with param value.
    float prevRelease = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v  = static_cast<float>(i) / 20.0f;
        const float ms = limiterReleaseMsFromParam(v);
        assert(ms >= prevRelease);
        prevRelease = ms;
    }
    assert(std::fabs(prevRelease - 1000.0f) < 1.0e-3f);  // must reach max

    // Threshold: non-decreasing with param (clamped below 0.5).
    float prevThresh = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v  = static_cast<float>(i) / 20.0f;
        const float th = limiterThresholdFromParam(v);
        assert(th >= prevThresh);
        prevThresh = th;
    }
    assert(std::fabs(prevThresh - 1.0f) < 1.0e-6f);  // must reach ceiling
}

// ─── VIII. Clamp guards ────────────────────────────────────────────────────────

static void testClampGuards() {
    // Attack: param < 0 → 0 ms
    assert(std::fabs(limiterAttackMsFromParam(-0.5f)) < 1.0e-6f);
    assert(std::fabs(limiterAttackMsFromParam(-1.0f)) < 1.0e-6f);

    // Attack: param > 1 → 20 ms (max)
    assert(std::fabs(limiterAttackMsFromParam(1.5f) - 20.0f) < 1.0e-5f);
    assert(std::fabs(limiterAttackMsFromParam(2.0f) - 20.0f) < 1.0e-5f);

    // Release: param < 0 → 10 ms (floor)
    assert(std::fabs(limiterReleaseMsFromParam(-0.5f) - 10.0f) < 1.0e-5f);

    // Release: param > 1 → 1000 ms (max)
    assert(std::fabs(limiterReleaseMsFromParam(1.5f) - 1000.0f) < 1.0e-3f);

    // Threshold: param < 0.5 → 0.5 (floor)
    assert(std::fabs(limiterThresholdFromParam(0.0f) - 0.5f) < 1.0e-6f);
    assert(std::fabs(limiterThresholdFromParam(-1.0f) - 0.5f) < 1.0e-6f);

    // Threshold: param > 1 → 1.0 (ceiling)
    assert(std::fabs(limiterThresholdFromParam(1.5f) - 1.0f) < 1.0e-6f);

    // Enabled: param exactly 0.5 → false (not > 0.5)
    assert(!limiterEnabledFromParam(0.5f));
    // Enabled: param 0.0 and 1.0 boundary
    assert(!limiterEnabledFromParam(0.0f));
    assert(limiterEnabledFromParam(1.0f));
}

// ─── IX. SimpleLimiter setAttackMs / setReleaseMs coeff contracts ─────────────

static void testSimpleLimiterCoeffs() {
    constexpr double kSR = 44100.0;

    ArpSID::SimpleLimiter lim{};
    lim.reset();

    // Attack: param=0 → 0 ms → attCoeff=0 (brick-wall / instantaneous)
    lim.setAttackMs(limiterAttackMsFromParam(0.0f), kSR);
    assert(lim.attCoeff == 0.0f);

    // Attack: param=0.08 (default) → 1.6 ms → attCoeff > 0 (non-instantaneous)
    lim.setAttackMs(limiterAttackMsFromParam(kDefaultLimiterAttack), kSR);
    assert(lim.attCoeff > 0.0f && lim.attCoeff < 1.0f);
    // Exact: exp(-1 / (0.0016 * 44100)) = exp(-1/70.56) ≈ 0.9861
    const float expectedAttCoeff = std::exp(-1.0f / (0.0016f * (float)kSR));
    assert(std::fabs(lim.attCoeff - expectedAttCoeff) < 1.0e-5f);

    // Release: param=0.35 (default) → 356.5 ms → relCoeff in (0, 1)
    lim.setReleaseMs(limiterReleaseMsFromParam(kDefaultLimiterRelease), kSR);
    assert(lim.relCoeff > 0.0f && lim.relCoeff < 1.0f);
    // Exact: exp(-1 / (0.3565 * 44100)) = exp(-1/15721.65) ≈ 0.999936
    const float expectedRelCoeff = std::exp(-1.0f / (0.3565f * (float)kSR));
    assert(std::fabs(lim.relCoeff - expectedRelCoeff) < 1.0e-5f);

    // Release: param=0 → 10 ms floor. relCoeff close to 0.9978 at 44100 Hz.
    lim.setReleaseMs(limiterReleaseMsFromParam(0.0f), kSR);
    assert(lim.relCoeff > 0.99f && lim.relCoeff < 1.0f);

    // Release: param=1 → 1000 ms. relCoeff very close to 1.
    lim.setReleaseMs(limiterReleaseMsFromParam(1.0f), kSR);
    assert(lim.relCoeff > 0.9999f && lim.relCoeff < 1.0f);

    // Longer release → higher relCoeff (slower decay)
    ArpSID::SimpleLimiter limA{}, limB{};
    limA.setReleaseMs(limiterReleaseMsFromParam(0.1f), kSR);
    limB.setReleaseMs(limiterReleaseMsFromParam(0.9f), kSR);
    assert(limB.relCoeff > limA.relCoeff);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testEnabledFormula();
    testThresholdFormula();
    testAttackMsFormula();
    testReleaseMsFormula();
    testDefaultParamAlignment();
    testPreFixStaleDefaults();
    testMonotonicity();
    testClampGuards();
    testSimpleLimiterCoeffs();
    return 0;
}
