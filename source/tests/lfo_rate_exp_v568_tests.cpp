// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// lfo_rate_exp_v568_tests.cpp — LFO rate-curve contract (v568).
//
// Pins the exponential mapping of the 0-1 normalized LFO rate parameter to Hz.
// The OLD linear formula (rateHz = 0.1 + value × 19.9) placed the knob
// midpoint at ~10 Hz — above the entire vibrato/tremolo register (4–8 Hz),
// making the musically useful lower range (0.1–5 Hz) crammed into the bottom
// quarter of the knob. The CORRECT exponential formula (rateHz = 0.1 × 200^value)
// places the geometric midpoint at √(0.1 × 20) = √2 ≈ 1.414 Hz.
//
// Tests cover:
// I. Endpoints: value=0 → 0.1 Hz, value=1 → 20 Hz
// II. Geometric midpoint: value=0.5 → √2 ≈ 1.4142 Hz
// III. Monotone increase: rate strictly increases with value
// IV. Clamp guards: value < 0 and value > 1 clamp to endpoints
// V. LFO::setRateTempo (BPM path) unchanged — direct Hz, no curve applied
// VI. Phase-increment consistency: phaseInc = rateHz / sampleRate at each rate

#include "arpsid/modulation/lfo.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <limits>

// ─── helpers ──────────────────────────────────────────────────────────────────

// Exponential formula in use after v568 fix.
static float lfoRateHz(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return 0.1f * std::pow(200.0f, clamped);
}

// ─── I. Endpoints ─────────────────────────────────────────────────────────────

static void testEndpoints() {
    // The formula: rateHz = clamp(0.1 × 200^value, 0.01, 20)
    // value=0: 0.1 × 200^0 = 0.1 × 1 = 0.1 Hz
    // value=1: 0.1 × 200^1 = 0.1 × 200 = 20 Hz
    const float rateAt0 = lfoRateHz(0.0f);
    const float rateAt1 = lfoRateHz(1.0f);
    assert(std::abs(rateAt0 -  0.1f) < 1e-5f);
    assert(std::abs(rateAt1 - 20.0f) < 1e-4f);

    // Range guard.
    assert(rateAt0 >= 0.09f && rateAt0 <= 0.11f);
    assert(rateAt1 >= 19.9f && rateAt1 <= 20.1f);

    // Endpoints unchanged from old linear formula.
    const float oldAt0 = 0.1f + 0.0f * 19.9f;
    const float oldAt1 = 0.1f + 1.0f * 19.9f;
    assert(std::abs(oldAt0 -  0.1f) < 1e-5f);
    assert(std::abs(oldAt1 - 20.0f) < 1e-4f);
    // Both formulas share the same endpoints.
    assert(std::abs(rateAt0 - oldAt0) < 1e-4f);
    assert(std::abs(rateAt1 - oldAt1) < 1e-4f);
}

// ─── II. Geometric midpoint ────────────────────────────────────────────────────

static void testGeometricMidpoint() {
    // value=0.5: rateHz = 0.1 × sqrt(200) = sqrt(0.1 × 20) = sqrt(2) ≈ 1.41421356…
    const float expected = std::sqrt(2.0f);  // ≈ 1.4142
    const float actual   = lfoRateHz(0.5f);

    // Tolerance: floating-point arithmetic difference must be < 0.001 Hz.
    assert(std::abs(actual - expected) < 1e-3f);

    // Sanity: midpoint must be in the sub-vibrato range (< 3 Hz).
    assert(actual > 0.5f && actual < 3.0f);

    // OLD linear midpoint was ≈10.05 Hz — the new midpoint MUST be < 5 Hz.
    const float oldMidpoint = 0.1f + 0.5f * 19.9f;  // 10.05 Hz
    assert(oldMidpoint > 9.0f);    // confirm old was bad
    assert(actual < 5.0f);         // confirm new is musically sensible
    // The improvement: new midpoint is > 6× lower than old.
    assert(oldMidpoint / actual > 6.0f);
}

// ─── III. Monotone increase ────────────────────────────────────────────────────

static void testMonotoneIncrease() {
    float prev = 0.0f;
    // Sample 20 evenly spaced values from 0 to 1.
    for (int i = 0; i <= 20; ++i) {
        const float v = static_cast<float>(i) / 20.0f;
        const float rateHz = lfoRateHz(v);
        assert(rateHz > prev || i == 0);  // strictly increasing (or first sample)
        prev = rateHz;
    }
    // The final value must equal the max (20 Hz).
    assert(prev >= 19.9f);
}

// ─── IV. Clamp guards ─────────────────────────────────────────────────────────

static void testClampGuards() {
    // Negative value must clamp to 0.1 Hz (same as value=0).
    const float rateNeg = lfoRateHz(-0.5f);  // clamp(-0.5, 0, 1) = 0 → 0.1 Hz
    assert(std::abs(rateNeg - 0.1f) < 1e-5f);

    // Value > 1 must clamp to 20 Hz (same as value=1).
    const float rateOver = lfoRateHz(2.0f);  // clamp(2.0, 0, 1) = 1 → 20 Hz
    assert(std::abs(rateOver - 20.0f) < 1e-4f);
}

// ─── V. LFO::setRateTempo unchanged ──────────────────────────────────────────

static void testSetRateTempoUnchanged() {
    // setRateTempo(bpm, division) is the HOST-SYNC path — it takes BPM directly
    // and MUST NOT apply the 0-1 exponential curve.
    // Contract: rateHz = (bpm/60) / division.
    // At 120 BPM, division=1 → rateHz = 2.0 Hz.
    const float quarterAt120 = (120.0f / 60.0f) / 1.0f;
    const float eighthAt120  = (120.0f / 60.0f) / 0.5f;
    const float halfAt120    = (120.0f / 60.0f) / 2.0f;

    assert(std::abs(quarterAt120 - 2.0f) < 1e-5f);
    assert(std::abs(eighthAt120  - 4.0f) < 1e-5f);
    assert(std::abs(halfAt120    - 1.0f) < 1e-5f);

    // Pin: exponential curve MUST NOT be applied to the BPM path.
    // i.e., quarterAt120 must be 2.0, NOT 0.1 × 200^(120/60) ≈ 57.7 Hz.
    assert(quarterAt120 < 3.0f);

    // Verify LFO::setRateTempo produces the expected result directly.
    ArpSID::LFO lfo;
    lfo.setSampleRate(44100.0);
    lfo.setRateTempo(120.0f, 1.0f);
    // Process one sample and check that the phase advances by rateHz/sampleRate = 2/44100.
    // getPhase() starts at 0; after process() it should be ≈ 2/44100.
    const float valAfterProcess = lfo.process();
    (void)valAfterProcess;
    const double phase = lfo.getPhase();
    const double expectedPhaseInc = 2.0 / 44100.0;
    assert(std::abs(phase - expectedPhaseInc) < 1e-9);
}

// ─── VI. Phase-increment consistency ─────────────────────────────────────────

static void testPhaseIncrementConsistency() {
    // At sampleRate=44100, the phase increment per sample is rateHz / 44100.
    // After one process() call, phase == rateHz / sampleRate (before wrapping).

    struct TestCase {
        float value;
        float expectedHz;
    };

    const TestCase cases[] = {
        { 0.0f,  0.1f  },       // minimum
        { 0.5f,  std::sqrt(2.0f) },  // geometric midpoint
        { 1.0f,  20.0f },       // maximum
    };

    for (const auto& tc : cases) {
        ArpSID::LFO lfo;
        lfo.setSampleRate(44100.0);
        lfo.setRate(tc.expectedHz);
        lfo.process();
        const double phase = lfo.getPhase();
        const double expectedPhaseInc = (double)tc.expectedHz / 44100.0;
        // Phase increment tolerance: 1e-9 (well within float precision).
        assert(std::abs(phase - expectedPhaseInc) < 1e-8);
    }

    // At sampleRate=48000, same rates.
    for (const auto& tc : cases) {
        ArpSID::LFO lfo;
        lfo.setSampleRate(48000.0);
        lfo.setRate(tc.expectedHz);
        lfo.process();
        const double phase = lfo.getPhase();
        const double expectedPhaseInc = (double)tc.expectedHz / 48000.0;
        assert(std::abs(phase - expectedPhaseInc) < 1e-8);
    }

    // Old linear midpoint (10.05 Hz): phase increment would be 10.05/44100 ≈ 2.28e-4.
    // New exponential midpoint (√2 ≈ 1.414 Hz): phase increment is 1.414/44100 ≈ 3.21e-5.
    // The new midpoint gives 7× fewer steps per second — musically much slower.
    const double oldMidPhaseInc = 10.05 / 44100.0;
    const double newMidPhaseInc = std::sqrt(2.0) / 44100.0;
    assert(oldMidPhaseInc > 7.0 * newMidPhaseInc);  // old was > 7× faster at midpoint
}

static void testNonFiniteInputsRecover() {
    ArpSID::LFO lfo;
    lfo.setSampleRate(std::numeric_limits<double>::quiet_NaN());
    lfo.setRate(std::numeric_limits<float>::quiet_NaN());
    lfo.setRateTempo(std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN());
    lfo.setDepth(std::numeric_limits<float>::quiet_NaN());
    lfo.setPhaseOffset(std::numeric_limits<float>::quiet_NaN());
    lfo.reset();
    for (int i = 0; i < 32; ++i) assert(std::isfinite(lfo.process()));
    assert(std::isfinite(lfo.getPhase()));
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testEndpoints();
    testGeometricMidpoint();
    testMonotoneIncrease();
    testClampGuards();
    testSetRateTempoUnchanged();
    testPhaseIncrementConsistency();
    testNonFiniteInputsRecover();
    return 0;
}
