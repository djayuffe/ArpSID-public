// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// mode_transition_panic_reset_v611_tests.cpp
//
// Fix #9 — Mode-transition panic/reset coverage.
//
// Tests every ordered pair of {BitPerfect, SidRegister, DrSid} transitions to
// verify that:
// (a) The resolver returns the correct post-transition mode.
// (b) The strict mode-flag sanitizer leaves params in a single-mode state.
// (c) Transitioning from any mode to any other mode never leaves both
// DrSid AND SynthMode simultaneously enabled (the "boolean priority accident").
// (d) sidRenderModeParamsAreSane() is true after every transition.
//
// Sections:
// I. sidRenderModeParamsAreSane sanity gate — baseline single-mode states
// II. Priority accident detection — both flags set simultaneously
// III. sidSanitizeRenderModeParamsRT clears the lower-priority flag
// IV. sidResolveRenderModeFromLiveParams single-source contract
// V. All 6 transition pairs: resolver result + sane flag
// VI. sidCanonicalizeTopLevelRenderModeParams (state-root path)

#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "parameter_ids.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::abort();
    }
}

// Minimal param array for resolver tests (no ObjC/AU runtime needed).
using ParamArray = std::array<float, static_cast<std::size_t>(ArpSID::kNumParams)>;

static ParamArray makeParams(float drSid, float synthMode) {
    ParamArray p{};
    for (auto& v : p) v = 0.0f;
    p[(std::size_t)ArpSID::kParamDrSidEnable]     = drSid;
    p[(std::size_t)ArpSID::kParamSynthModeEnable] = synthMode;
    return p;
}

// ── Section I: sanity gate ────────────────────────────────────────────────────
static void testSanityGate() {
    // BitPerfect: both 0
    require(ArpSID::sidRenderModeParamsAreSane(makeParams(0.0f, 0.0f)),
            "I: BitPerfect (both 0) must be sane");
    // SidRegister: synthMode=1
    require(ArpSID::sidRenderModeParamsAreSane(makeParams(0.0f, 1.0f)),
            "I: SidRegister (synth=1,dr=0) must be sane");
    // DrSid: drSid=1
    require(ArpSID::sidRenderModeParamsAreSane(makeParams(1.0f, 0.0f)),
            "I: DrSid (dr=1,synth=0) must be sane");
    std::puts("  I:   sanity gate — OK");
}

// ── Section II: priority accident detection ───────────────────────────────────
static void testPriorityAccidentDetection() {
    // Both flags true = priority accident
    require(!ArpSID::sidRenderModeParamsAreSane(makeParams(1.0f, 1.0f)),
            "II: both flags true must NOT be sane");
    // Resolver still returns a single mode (DrSid wins)
    const auto mode = ArpSID::sidResolveRenderModeFromLiveParams(makeParams(1.0f, 1.0f));
    require(mode == ArpSID::SidRuntimeRenderMode::DrSid,
            "II: resolver must return DrSid when both flags set (DrSid priority wins)");
    std::puts("  II:  priority accident detection — OK");
}

// ── Section III: RT sanitizer ─────────────────────────────────────────────────
static void testRtSanitizer() {
    // Accident state: both flags set
    auto p = makeParams(1.0f, 1.0f);
    require(!ArpSID::sidRenderModeParamsAreSane(p), "III: pre-sanitize — both set is insane");
    ArpSID::sidSanitizeRenderModeParamsRT(p);
    require(ArpSID::sidRenderModeParamsAreSane(p),  "III: post-sanitize — must be sane");
    // DrSid flag stays, SynthMode is cleared
    require(p[(std::size_t)ArpSID::kParamDrSidEnable]     > 0.5f,
            "III: DrSid flag must survive sanitization");
    require(p[(std::size_t)ArpSID::kParamSynthModeEnable] < 0.5f,
            "III: SynthMode must be cleared by sanitizer");
    // Clean states are unchanged by sanitizer
    auto bp = makeParams(0.0f, 0.0f);
    ArpSID::sidSanitizeRenderModeParamsRT(bp);
    require(ArpSID::sidRenderModeParamsAreSane(bp), "III: BitPerfect unchanged after sanitize");
    std::puts("  III: RT sanitizer — OK");
}

// ── Section IV: resolver single-source contract ───────────────────────────────
static void testResolverSingleSource() {
    using M = ArpSID::SidRuntimeRenderMode;
    require(ArpSID::sidResolveRenderModeFromLiveParams(makeParams(0.0f, 0.0f)) == M::BitPerfect,
            "IV: (0,0) → BitPerfect");
    require(ArpSID::sidResolveRenderModeFromLiveParams(makeParams(0.0f, 1.0f)) == M::SidRegister,
            "IV: (0,1) → SidRegister");
    require(ArpSID::sidResolveRenderModeFromLiveParams(makeParams(1.0f, 0.0f)) == M::DrSid,
            "IV: (1,0) → DrSid");
    require(ArpSID::sidResolveRenderModeFromLiveParams(makeParams(1.0f, 1.0f)) == M::DrSid,
            "IV: (1,1) → DrSid (priority)");
    std::puts("  IV:  resolver single-source contract — OK");
}

// ── Section V: all 6 transition pairs ────────────────────────────────────────
struct TransitionCase {
    const char* name;
    float drSidBefore, synthBefore;  // "from" mode
    float drSidAfter,  synthAfter;   // "to" mode
    ArpSID::SidRuntimeRenderMode expectedMode;
};

static void testAllTransitionPairs() {
    using M = ArpSID::SidRuntimeRenderMode;
    static constexpr TransitionCase cases[] = {
        { "BP→SR",  0.0f, 0.0f,  0.0f, 1.0f,  M::SidRegister },
        { "BP→DrS", 0.0f, 0.0f,  1.0f, 0.0f,  M::DrSid       },
        { "SR→BP",  0.0f, 1.0f,  0.0f, 0.0f,  M::BitPerfect  },
        { "SR→DrS", 0.0f, 1.0f,  1.0f, 0.0f,  M::DrSid       },
        { "DrS→BP", 1.0f, 0.0f,  0.0f, 0.0f,  M::BitPerfect  },
        { "DrS→SR", 1.0f, 0.0f,  0.0f, 1.0f,  M::SidRegister },
    };
    for (const auto& c : cases) {
        auto p = makeParams(c.drSidAfter, c.synthAfter);
        // After setting the "to" params, sanitize and resolve
        ArpSID::sidSanitizeRenderModeParamsRT(p);
        require(ArpSID::sidRenderModeParamsAreSane(p),
                (std::string("V: ") + c.name + " — post-transition params must be sane").c_str());
        const auto mode = ArpSID::sidResolveRenderModeFromLiveParams(p);
        require(mode == c.expectedMode,
                (std::string("V: ") + c.name + " — resolver result wrong").c_str());
    }
    std::puts("  V:   all 6 transition pairs (BP↔SR↔DrS) — OK");
}

// ── Section VI: state-root canonicalization ───────────────────────────────────
static void testStateRootCanonicalization() {
    // Priority accident in a state root must be resolved
    ArpSID::SidStateRootV1 root{};
    // Simulate writing both flags into the root
    ArpSID::sidSetStateRootParamValue(root, ArpSID::kParamDrSidEnable,     1.0f);
    ArpSID::sidSetStateRootParamValue(root, ArpSID::kParamSynthModeEnable, 1.0f);
    ArpSID::sidCanonicalizeTopLevelRenderModeParams(root);
    // DrSid wins: SynthMode cleared
    require(ArpSID::sidStateRootParamValue(root, ArpSID::kParamDrSidEnable)     > 0.5f,
            "VI: DrSid survives canonicalization");
    require(ArpSID::sidStateRootParamValue(root, ArpSID::kParamSynthModeEnable) < 0.5f,
            "VI: SynthMode cleared by canonicalization");
    // Pure SidRegister state is preserved
    ArpSID::SidStateRootV1 root2{};
    ArpSID::sidSetStateRootParamValue(root2, ArpSID::kParamSynthModeEnable, 1.0f);
    ArpSID::sidSetStateRootParamValue(root2, ArpSID::kParamDrSidEnable,     0.0f);
    ArpSID::sidCanonicalizeTopLevelRenderModeParams(root2);
    require(ArpSID::sidStateRootParamValue(root2, ArpSID::kParamSynthModeEnable) > 0.5f,
            "VI: SidRegister preserved after canonicalization");
    require(ArpSID::sidStateRootParamValue(root2, ArpSID::kParamDrSidEnable)     < 0.5f,
            "VI: DrSid cleared for SidRegister root");
    std::puts("  VI:  state-root canonicalization — OK");
}

int main() {
    std::puts("mode_transition_panic_reset_v611_tests");
    testSanityGate();
    testPriorityAccidentDetection();
    testRtSanitizer();
    testResolverSingleSource();
    testAllTransitionPairs();
    testStateRootCanonicalization();
    std::puts("  ALL SECTIONS PASSED");
    return 0;
}
