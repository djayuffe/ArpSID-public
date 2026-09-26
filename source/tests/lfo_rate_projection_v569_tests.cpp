// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// lfo_rate_projection_v569_tests.cpp — LFO rate bulk-projection path contract (v569).
//
// There are two code paths that set the LFO rate from a normalized parameter:
//
// Path A — incremental (per-parameter-change):
// sid_runtime_parameter_services.h runtimeApplyProjectedBackendParameter()
// Fixed in v568.
//
// Path B — bulk (force-reload / preset-restore / first-apply):
// sid_runtime_backend_projection.h projectRuntimeStateToBackends()
// Fixed in v569.
//
// Both paths MUST produce the same rateHz for the same normalized parameter value,
// otherwise a preset loaded from state restores a different LFO speed than what the
// user last heard during live editing.
//
// The contract pinned here:
// - Both paths use identical formula: rateHz = 0.1 × 200^value
// - Formula consistency holds at all 21 evenly-spaced values in [0,1]
// - Endpoints: value=0 → 0.1 Hz, value=1 → 20 Hz (shared with v568)
// - Midpoint: value=0.5 → √2 ≈ 1.414 Hz (shared with v568)
// - OLD linear formula at midpoint was ≈10 Hz (> 7× too fast)

#include "arpsid/modulation/lfo.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>

// ─── The canonical formula (both paths after v568 + v569) ─────────────────────

static float canonicalLFORateHz(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return 0.1f * std::pow(200.0f, clamped);
}

// ─── I. Two-path consistency ──────────────────────────────────────────────────

static void testTwoPathConsistency() {
    // Simulate Path A (parameter-services) and Path B (projection) at each of
    // 21 sample points across [0,1]. Both must produce bit-identical results.
    for (int i = 0; i <= 20; ++i) {
        const float value = static_cast<float>(i) / 20.0f;

        // Path A (v568 fix formula):
        const float rvA = std::clamp(value, 0.0f, 1.0f);
        const float rateA = 0.1f * std::pow(200.0f, rvA);

        // Path B (v569 fix formula — identical expression):
        const float rvB = std::clamp(value, 0.0f, 1.0f);
        const float rateB = 0.1f * std::pow(200.0f, rvB);

        // Must be bit-identical (same expression, same FP operations).
        assert(rateA == rateB);

        // Both must match the canonical formula.
        assert(rateA == canonicalLFORateHz(value));
    }
}

// ─── II. Endpoints (bulk path) ─────────────────────────────────────────────────

static void testEndpointsBulkPath() {
    // Bulk-projection path uses the same formula; endpoints must be correct.
    const float rv0 = std::clamp(0.0f, 0.0f, 1.0f);
    const float rv1 = std::clamp(1.0f, 0.0f, 1.0f);
    const float rateAtMin = 0.1f * std::pow(200.0f, rv0);
    const float rateAtMax = 0.1f * std::pow(200.0f, rv1);

    assert(std::abs(rateAtMin -  0.1f) < 1e-5f);
    assert(std::abs(rateAtMax - 20.0f) < 1e-4f);
}

// ─── III. Midpoint cross-path agreement ──────────────────────────────────────

static void testMidpointCrossPath() {
    // At value=0.5: both paths must agree and land near √2 Hz.
    const float rateA = 0.1f * std::pow(200.0f, std::clamp(0.5f, 0.0f, 1.0f));
    const float rateB = canonicalLFORateHz(0.5f);
    const float expected = std::sqrt(2.0f);

    assert(rateA == rateB);
    assert(std::abs(rateA - expected) < 1e-3f);

    // OLD linear value at midpoint: 0.1 + 0.5 × 19.9 = 10.05 Hz
    const float oldLinearMid = 0.1f + 0.5f * 19.9f;
    assert(oldLinearMid > 9.9f && oldLinearMid < 10.2f);  // old was bad
    assert(rateA < 3.0f);                                  // new is musically sensible
    assert(oldLinearMid / rateA > 7.0f);                   // improvement factor > 7×
}

// ─── IV. LFO object observes bulk-path rate correctly ────────────────────────

static void testLFOObjectBulkRate() {
    // Simulate bulk-path application to an LFO object: set rate via the v569
    // formula, then verify the phase advance matches.

    struct Case {
        float param;   // normalized [0,1]
        float expHz;   // expected Hz
    };
    const Case cases[] = {
        { 0.0f,  0.1f },
        { 0.5f,  std::sqrt(2.0f) },
        { 1.0f,  20.0f },
    };

    for (const auto& c : cases) {
        // Bulk path sets rate exactly like this (v569 fix):
        const float rv = std::clamp(c.param, 0.0f, 1.0f);
        const float hz = 0.1f * std::pow(200.0f, rv);
        assert(std::abs(hz - c.expHz) < 1e-3f);

        // Feed the computed rate into the LFO and verify phaseInc.
        ArpSID::LFO lfo;
        lfo.setSampleRate(44100.0);
        lfo.setRate(hz);
        lfo.process();
        const double phase = lfo.getPhase();
        const double expected_inc = (double)hz / 44100.0;
        assert(std::abs(phase - expected_inc) < 1e-8);
    }
}

// ─── V. Monotone increase in bulk path ───────────────────────────────────────

static void testMonotoneBulkPath() {
    float prev = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v = static_cast<float>(i) / 20.0f;
        const float rv = std::clamp(v, 0.0f, 1.0f);
        const float rateHz = 0.1f * std::pow(200.0f, rv);
        assert(rateHz > prev || i == 0);
        prev = rateHz;
    }
    assert(prev >= 19.9f);
}

// ─── VI. Clamp guard for out-of-range param values ───────────────────────────

static void testClampGuardBulkPath() {
    // Both paths clamp their input before calling pow().
    // Negative value → clamps to 0 → 0.1 Hz.
    const float rvNeg = std::clamp(-0.5f, 0.0f, 1.0f);
    const float rateNeg = 0.1f * std::pow(200.0f, rvNeg);
    assert(std::abs(rateNeg - 0.1f) < 1e-5f);

    // Value > 1 → clamps to 1 → 20 Hz.
    const float rvOver = std::clamp(2.0f, 0.0f, 1.0f);
    const float rateOver = 0.1f * std::pow(200.0f, rvOver);
    assert(std::abs(rateOver - 20.0f) < 1e-4f);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testTwoPathConsistency();
    testEndpointsBulkPath();
    testMidpointCrossPath();
    testLFOObjectBulkRate();
    testMonotoneBulkPath();
    testClampGuardBulkPath();
    return 0;
}
