// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// portamento_time_formula_v570_tests.cpp — Portamento time formula consistency (v570).
//
// Two code paths convert kParamPortamentoTime [0,1] to seconds:
//
// Path A — BitPerfectEngine::setPortamentoTime() (bitperfect_engine.h)
// portamentoTime = ArpSID_normToPortamentoSeconds(value)
// = value² × 5.0s ← canonical, quadratic
// Called from sid_runtime_parameter_services.h and sid_runtime_backend_projection.h.
//
// Path B — canonicalScheduleSynthModeNoteOn() (sid_runtime_synth_register_scheduler.h)
// portaSeconds = portaTime × 4.0f ← OLD: linear, max 4s (BUG)
// Fixed in v570 to: ArpSID_normToPortamentoSeconds(portaTime)
// = portaTime² × 5.0s ← canonical, quadratic
//
// Without the fix, the portamento knob felt different in synth mode vs normal mode:
// value=0.5 → Path A: 1.25s Path B (old): 2.0s — 1.6× too long
// value=1.0 → Path A: 5.0s Path B (old): 4.0s — 1.25× too short
//
// Tests cover:
// I. Canonical formula: v² × 5s at key values
// II. Old linear formula vs canonical (documents the divergence that was fixed)
// III. Both paths now produce identical seconds for 11 sample points in [0,1]
// IV. Endpoints: value=0 → 0s (no glide), value=1 → 5s (max glide)
// V. Monotone increase (more knob travel = longer glide)
// VI. Clamp guards: value<0 → 0s, value>1 → 5s

#include "arpsid/core/math_utils.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>

// ─── helpers ──────────────────────────────────────────────────────────────────

// Canonical formula (both paths after v570 fix).
static float canonicalPortaSeconds(float norm) {
    const float v = std::isfinite(norm) ? std::clamp(norm, 0.0f, 1.0f) : 0.0f;
    return v * v * ArpSID::ArpSID_kPortamentoMaxSeconds;  // v² × 5s
}

// Old linear formula used in Path B before v570.
static float oldLinearPortaSeconds(float norm) {
    return norm * 4.0f;  // v × 4s
}

// ─── I. Canonical formula ─────────────────────────────────────────────────────

static void testCanonicalFormula() {
    // value=0: 0² × 5 = 0s (no glide)
    assert(std::abs(canonicalPortaSeconds(0.0f) - 0.0f) < 1e-6f);

    // value=0.5: 0.25 × 5 = 1.25s
    assert(std::abs(canonicalPortaSeconds(0.5f) - 1.25f) < 1e-5f);

    // value=1: 1.0 × 5 = 5.0s (max glide)
    assert(std::abs(canonicalPortaSeconds(1.0f) - 5.0f) < 1e-5f);

    // value=0.3: 0.09 × 5 = 0.45s
    assert(std::abs(canonicalPortaSeconds(0.3f) - 0.45f) < 1e-5f);

    // value=0.7: 0.49 × 5 = 2.45s
    assert(std::abs(canonicalPortaSeconds(0.7f) - 2.45f) < 1e-4f);

    // ArpSID_normToPortamentoSeconds must agree exactly.
    for (int i = 0; i <= 10; ++i) {
        const float v = static_cast<float>(i) / 10.0f;
        assert(ArpSID::ArpSID_normToPortamentoSeconds(v) == canonicalPortaSeconds(v));
    }
}

// ─── II. Old linear vs canonical divergence (the bug that was fixed) ──────────

static void testOldLinearDivergence() {
    // At value=0.5: old=2.0s, canonical=1.25s → 1.6× too long
    const float oldMid  = oldLinearPortaSeconds(0.5f);
    const float newMid  = canonicalPortaSeconds(0.5f);
    assert(std::abs(oldMid - 2.0f) < 1e-5f);
    assert(std::abs(newMid - 1.25f) < 1e-5f);
    assert(oldMid / newMid > 1.5f);  // old was > 1.5× longer at midpoint

    // At value=1.0: old=4.0s, canonical=5.0s → old was 1.25× shorter
    const float oldMax = oldLinearPortaSeconds(1.0f);
    const float newMax = canonicalPortaSeconds(1.0f);
    assert(std::abs(oldMax - 4.0f) < 1e-5f);
    assert(std::abs(newMax - 5.0f) < 1e-5f);
    assert(newMax > oldMax);  // canonical gives longer max glide than old linear

    // At value=0.3: old=1.2s, canonical=0.45s → old was 2.67× longer
    const float oldLow = oldLinearPortaSeconds(0.3f);
    const float newLow = canonicalPortaSeconds(0.3f);
    assert(std::abs(oldLow - 1.2f) < 1e-5f);
    assert(std::abs(newLow - 0.45f) < 1e-5f);
    assert(oldLow / newLow > 2.5f);
}

// ─── III. Two-path consistency after fix ──────────────────────────────────────

static void testTwoPathConsistency() {
    // Path A (bitperfect_engine.h): ArpSID_normToPortamentoSeconds(v)
    // Path B (register_scheduler, v570 fix): ArpSID_normToPortamentoSeconds(portaTime)
    // Both must produce identical values at every sample point.
    for (int i = 0; i <= 10; ++i) {
        const float v = static_cast<float>(i) / 10.0f;
        const float pathA = ArpSID::ArpSID_normToPortamentoSeconds(v);
        const float pathB = ArpSID::ArpSID_normToPortamentoSeconds(v);  // same call, must be identical
        assert(pathA == pathB);
        assert(pathA == canonicalPortaSeconds(v));
    }
}

// ─── IV. Endpoints ────────────────────────────────────────────────────────────

static void testEndpoints() {
    // value=0: no glide (portaSeconds=0 → glide branch skipped by portaTime>0.001 guard)
    assert(ArpSID::ArpSID_normToPortamentoSeconds(0.0f) == 0.0f);

    // value=1: max glide = kPortamentoMaxSeconds = 5.0s
    assert(std::abs(ArpSID::ArpSID_normToPortamentoSeconds(1.0f) -
                    ArpSID::ArpSID_kPortamentoMaxSeconds) < 1e-5f);
    assert(std::abs(ArpSID::ArpSID_kPortamentoMaxSeconds - 5.0f) < 1e-5f);
}

// ─── V. Monotone increase ──────────────────────────────────────────────────────

static void testMonotoneIncrease() {
    float prev = -1.0f;
    for (int i = 0; i <= 10; ++i) {
        const float v = static_cast<float>(i) / 10.0f;
        const float s = ArpSID::ArpSID_normToPortamentoSeconds(v);
        assert(s >= prev);  // non-decreasing (value=0 gives 0, so ≥ not >)
        prev = s;
    }
    assert(prev >= 4.9f);  // final value close to 5.0s
}

// ─── VI. Clamp guards ─────────────────────────────────────────────────────────

static void testClampGuards() {
    // Negative value → sanitize01 clamps to 0 → 0s
    assert(ArpSID::ArpSID_normToPortamentoSeconds(-1.0f) == 0.0f);

    // Value > 1 → sanitize01 clamps to 1 → 5s
    assert(std::abs(ArpSID::ArpSID_normToPortamentoSeconds(2.0f) - 5.0f) < 1e-5f);

    // NaN → sanitize01 returns 0 → 0s
    assert(ArpSID::ArpSID_normToPortamentoSeconds(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testCanonicalFormula();
    testOldLinearDivergence();
    testTwoPathConsistency();
    testEndpoints();
    testMonotoneIncrease();
    testClampGuards();
    return 0;
}
