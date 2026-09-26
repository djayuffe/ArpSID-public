// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// seq_swing_tempo_v571_tests.cpp — Sequencer swing tempo-preservation contract (v571).
//
// The sequencer step-duration lambda in arpsid_processor_phase2.cpp::processSequencer()
// used an asymmetric swing formula:
//
// OLD (broken):
// odd step = samplesPerStep × (1 + seqSwing × 0.5)
// even step = samplesPerStep × (1 - seqSwing × 0.5 × 0.5) ← 0.5×0.5 = 0.25
//
// The bug: odd stretches by 50% of swing, even compresses by only 25%.
// At any swing > 0, odd+even pair sums to more than 2×samplesPerStep:
// pair sum = 2 + seqSwing × 0.25 → at swing=1: 2.25× (25% tempo drift)
//
// NEW (correct):
// odd step = samplesPerStep × (1 + seqSwing × 0.5)
// even step = samplesPerStep × max(0.1, 1 − seqSwing × 0.5)
//
// Now pair sum = 2 + seqSwing×0.5 − seqSwing×0.5 = 2.0 ✓ (exact preservation).
//
// Tests cover:
// I. Pair sum = 2.0 at swing=0 (straight time, no drift)
// II. Pair sum = 2.0 at swing=0.5 (gentle swing)
// III. Pair sum = 2.0 at swing=1.0 (maximum swing)
// IV. Old formula's drift is documented and confirmed bad
// V. Odd step is always ≥ even step (swing can only stretch beat, not invert)
// VI. At swing=0 both steps are exactly samplesPerStep (unity)
// VII. Max swing: odd=1.5×, even=0.5× (75/25 triplet feel)
// VIII. std::max(0.1) guard — even step never drops below 10% of base

#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <limits>
#include "../au3/ArpSIDSequencerEngine.h"

// ─── Corrected formula (v571) ────────────────────────────────────────────────

static double oddDuration(double samplesPerStep, float seqSwing) {
    const double swingFactor = (double)seqSwing * 0.5;
    return samplesPerStep * (1.0 + swingFactor);
}

static double evenDuration(double samplesPerStep, float seqSwing) {
    const double swingFactor = (double)seqSwing * 0.5;
    return samplesPerStep * std::max(0.1, 1.0 - swingFactor);
}

static double pairDuration(double samplesPerStep, float seqSwing) {
    return oddDuration(samplesPerStep, seqSwing) + evenDuration(samplesPerStep, seqSwing);
}

// ─── Old (broken) formula for comparison ─────────────────────────────────────

static double oldOddDuration(double sps, float sw)  { return sps * (1.0 + (double)sw * 0.5); }
static double oldEvenDuration(double sps, float sw) { return sps * (1.0 - (double)sw * 0.5 * 0.5); }
static double oldPairDuration(double sps, float sw) { return oldOddDuration(sps,sw) + oldEvenDuration(sps,sw); }

// ─── I. Pair sum = 2.0 at swing=0 ────────────────────────────────────────────

static void testPairSumAtZeroSwing() {
    const double sps = 22050.0;
    const double pair = pairDuration(sps, 0.0f);
    assert(std::abs(pair - 2.0 * sps) < 1e-9);
}

// ─── II. Pair sum = 2.0 at swing=0.5 ─────────────────────────────────────────

static void testPairSumAtHalfSwing() {
    for (double sps : {22050.0, 24000.0, 11025.0}) {
        const double pair = pairDuration(sps, 0.5f);
        // odd = sps*1.25, even = sps*0.75 → sum = 2*sps
        assert(std::abs(pair - 2.0 * sps) < 1e-9);
    }
}

// ─── III. Pair sum = 2.0 at swing=1.0 ────────────────────────────────────────

static void testPairSumAtMaxSwing() {
    const double sps = 22050.0;
    const double pair = pairDuration(sps, 1.0f);
    // odd = sps*1.5, even = sps*0.5 → sum = 2*sps
    assert(std::abs(pair - 2.0 * sps) < 1e-9);
}

// ─── IV. Old formula drift — documents the fixed bug ─────────────────────────

static void testOldFormulaDrift() {
    const double sps = 22050.0;

    // swing=0: both formulas agree (no swing → no drift).
    assert(std::abs(oldPairDuration(sps, 0.0f) - 2.0 * sps) < 1e-9);

    // swing=0.5: old pair = 2 + 0.25*0.5 = 2.125× → 6.25% slow
    const double oldHalf = oldPairDuration(sps, 0.5f);
    assert(oldHalf > 2.0 * sps + 0.001);  // old is too long

    // swing=1.0: old pair = 2 + 0.25 = 2.25× → 12.5% slow
    const double oldFull = oldPairDuration(sps, 1.0f);
    assert(std::abs(oldFull - 2.25 * sps) < 1.0);  // approx 2.25×

    // New formula fixes it:
    assert(std::abs(pairDuration(sps, 0.5f) - 2.0 * sps) < 1e-9);
    assert(std::abs(pairDuration(sps, 1.0f) - 2.0 * sps) < 1e-9);
}

// ─── V. Odd step ≥ even step at all swing values ──────────────────────────────

static void testOddGeqEven() {
    const double sps = 22050.0;
    for (int i = 0; i <= 20; ++i) {
        const float sw = static_cast<float>(i) / 20.0f;
        assert(oddDuration(sps, sw) >= evenDuration(sps, sw));
    }
}

// ─── VI. At swing=0 both steps = samplesPerStep exactly ──────────────────────

static void testUnityAtZeroSwing() {
    const double sps = 22050.0;
    assert(std::abs(oddDuration(sps, 0.0f) - sps) < 1e-9);
    assert(std::abs(evenDuration(sps, 0.0f) - sps) < 1e-9);
}

// ─── VII. Max swing gives 75/25 triplet feel ─────────────────────────────────

static void testMaxSwingRatios() {
    const double sps = 22050.0;
    const double odd  = oddDuration(sps, 1.0f);
    const double even = evenDuration(sps, 1.0f);
    // odd = 1.5×, even = 0.5× (triplet ratio 3:1 split of 2 beats)
    assert(std::abs(odd  - 1.5 * sps) < 1e-9);
    assert(std::abs(even - 0.5 * sps) < 1e-9);
    // ratio odd:even = 3:1
    assert(std::abs(odd / even - 3.0) < 1e-9);
}

// ─── VIII. std::max(0.1) guard — even never < 10% ─────────────────────────────

static void testEvenFloorGuard() {
    const double sps = 22050.0;
    // At swing=1.0, even = max(0.1, 0.5) = 0.5 → no floor needed; check guard works for extreme cases.
    // Guard activates only at swing > 1.8 (beyond the 0..1 clamp in production code).
    // Here we test the formula directly with extreme values.
    // For swing=2.0 (hypothetical): swingFactor=1.0 → 1-1.0=0.0 → floor to 0.1.
    const double evenExtr = sps * std::max(0.1, 1.0 - 2.0 * 0.5);
    assert(evenExtr >= 0.1 * sps);

    // Within normal range (0..1), floor should never activate.
    for (int i = 0; i <= 20; ++i) {
        const float sw = static_cast<float>(i) / 20.0f;
        const double ev = evenDuration(sps, sw);
        assert(ev >= 0.5 * sps);  // even at max swing = 0.5× > 0.1
    }
}

static void testProductionSequencerPreservesPairTempo() {
    ArpSID::SequencerEngine seq;
    ArpSID::SeqPattern pattern{};
    pattern.length = 2;
    pattern.steps[0].active = false;
    pattern.steps[1].active = false;
    seq.setPattern(pattern);
    seq.setEnabled(true);
    seq.setStepsPerBeat(4.0f);
    seq.setSwing(1.0f);
    seq.syncToBeatPosition(0.0);
    ArpSID::EventBuffer events{};
    seq.advanceWindow(0.0, 0.5, 48000, events);
    assert(seq.currentStep() == 0 &&
           "two production sequencer steps at full swing must still consume exactly 0.5 beats");
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testPairSumAtZeroSwing();
    testPairSumAtHalfSwing();
    testPairSumAtMaxSwing();
    testOldFormulaDrift();
    testOddGeqEven();
    testUnityAtZeroSwing();
    testMaxSwingRatios();
    testEvenFloorGuard();
    testProductionSequencerPreservesPairTempo();
    return 0;
}
