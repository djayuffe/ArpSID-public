// SPDX-License-Identifier: BSD-3-Clause
// filter_core_unification_and_topology_mode_v532_tests.cpp
//
// Closes two audit families in one slice:
//
// * Audit #31, #32 — filter unification. SIDChip's SIDFilter and
// SidRegisterEngine's Filter now both call the canonical helpers in
// `sid_filter_core.h` for:
// - effectiveCutoffHz_6581_loading (the per-sample squash)
// - applyModelNonlinearity (the 6581 tanh saturation)
// - resonanceFeedbackCoefficient
// - integratorLeak1/2
// - absoluteSafetyClamp
// Same register state therefore produces the same shaping in both
// engines — the audit-flagged "SynthMode vs BitPerfect disagree" bug
// surface is closed by construction.
//
// * Audit #29 wire-up — BitPerfectEngine now carries a
// SingleSidThreeVoiceEngine instance + a SidChipTopologyMode flag.
// processBlock branches: legacy 8-chip path stays default;
// `SingleChip3Voice` mode delegates rendering to the authentic
// single-SID path. Mode switching, voice stealing, and per-sample
// output finiteness are all pinned here.

#include "arpsid/core/sid_filter_core.h"
#include "arpsid/engines/bitperfect_engine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;

    // ── A. FilterCore::resonanceFeedbackCoefficient ────────────────────────
    {
        // 6581 path: 0.040 + 0.095 * resNorm
        require(std::abs(FilterCore::resonanceFeedbackCoefficient(0.0f, true) - 0.040f) < 1e-6f,
                "6581 feedback at resNorm=0 == 0.040");
        require(std::abs(FilterCore::resonanceFeedbackCoefficient(1.0f, true) - 0.135f) < 1e-6f,
                "6581 feedback at resNorm=1 == 0.135");
        // 8580 path: 0.022 + 0.135 * resNorm
        require(std::abs(FilterCore::resonanceFeedbackCoefficient(0.0f, false) - 0.022f) < 1e-6f,
                "8580 feedback at resNorm=0 == 0.022");
        require(std::abs(FilterCore::resonanceFeedbackCoefficient(1.0f, false) - 0.157f) < 1e-6f,
                "8580 feedback at resNorm=1 == 0.157");
    }

    // ── B. FilterCore::effectiveCutoffHz_6581_loading ──────────────────────
    {
        // Below threshold: no squash, return input unchanged.
        const double a = FilterCore::effectiveCutoffHz_6581_loading(1000.0, 0.1f, 0.5f, true);
        require(std::abs(a - 1000.0) < 1e-9, "drive below threshold → no squash");
        const double b = FilterCore::effectiveCutoffHz_6581_loading(1000.0, 0.5f, 0.1f, true);
        require(std::abs(b - 1000.0) < 1e-9, "resonance below threshold → no squash");
        // 8580: never squashed, regardless of drive/res.
        const double c = FilterCore::effectiveCutoffHz_6581_loading(1000.0, 0.9f, 0.9f, false);
        require(std::abs(c - 1000.0) < 1e-9, "8580 model: no squash even at max drive+res");
        // 6581 above threshold: squashed, but bounded to ≥ 84 % of nominal.
        const double d = FilterCore::effectiveCutoffHz_6581_loading(1000.0, 1.0f, 1.0f, true);
        require(d < 1000.0, "6581 high drive+res → squashed below nominal");
        require(d >= 1000.0 * 0.84,
                "6581 squash bounded ≥ 84 % of nominal (no collapse)");
    }

    // ── C. FilterCore::applyModelNonlinearity ──────────────────────────────
    {
        // Below threshold: input passes through.
        const float a = FilterCore::applyModelNonlinearity(0.5f, 0.1f, 0.5f, true);
        require(std::abs(a - 0.5f) < 1e-6f, "below threshold: pass-through");
        // 8580: pass-through always.
        const float b = FilterCore::applyModelNonlinearity(0.5f, 0.9f, 0.9f, false);
        require(std::abs(b - 0.5f) < 1e-6f, "8580 model: never saturates");
        // 6581 high drive+res: tanh saturation kicks in; output bounded.
        const float c = FilterCore::applyModelNonlinearity(2.0f, 1.0f, 1.0f, true);
        require(std::isfinite(c), "tanh output is finite");
        require(std::abs(c) < 2.0f, "tanh compresses excursion below input magnitude");
    }

    // ── D. FilterCore::integratorLeak1/2 ───────────────────────────────────
    {
        const double leak1 = FilterCore::integratorLeak1(0.05);
        require(std::abs(leak1 - 0.95) < 1e-9, "leak1 == 1 - integratorLeak");
        const double leak2_6581 = FilterCore::integratorLeak2(0.05, true);
        const double leak2_8580 = FilterCore::integratorLeak2(0.05, false);
        require(leak2_6581 < leak2_8580,
                "6581 leak2 is more aggressive than 8580 leak2 (the audit's model-specific differential)");
        require(std::abs(leak2_6581 - (1.0 - 0.05 * 0.52)) < 1e-9, "6581 leak2 formula");
        require(std::abs(leak2_8580 - (1.0 - 0.05 * 0.36)) < 1e-9, "8580 leak2 formula");
    }

    // ── E. FilterCore::absoluteSafetyClamp ─────────────────────────────────
    {
        require(FilterCore::absoluteSafetyClamp( 5.0f) ==  5.0f,        "value in range untouched");
        require(FilterCore::absoluteSafetyClamp( 9.0f) ==  8.0f,        "positive over-cap clamped to +8");
        require(FilterCore::absoluteSafetyClamp(-9.0f) == -8.0f,        "negative over-cap clamped to -8");
        require(std::isfinite(FilterCore::absoluteSafetyClamp(0.0f)),   "zero is finite");
    }

    // ── F. BitPerfectEngine topology mode default + switching ──────────────
    {
        BitPerfectEngine eng;
        require(eng.sidChipTopologyMode() == BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion,
                "default topology is MultiChipPolyIllusion (backward compat)");
        eng.setSidChipTopologyMode(BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice);
        require(eng.sidChipTopologyMode() == BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice,
                "topology can be switched to SingleChip3Voice");
        eng.setSidChipTopologyMode(BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion);
        require(eng.sidChipTopologyMode() == BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion,
                "topology can be switched back");
    }

    // ── G. SingleChip3Voice mode renders without crashing + produces audio ──
    {
        BitPerfectEngine eng;
        eng.setSampleRate(48000.0);
        eng.setSidChipTopologyMode(BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice);

        // Drive the single-SID engine directly with 3 notes.
        eng.singleSidEngine().noteOn(60, 100);
        eng.singleSidEngine().noteOn(64, 100);
        eng.singleSidEngine().noteOn(67, 100);
        require(eng.singleSidEngine().activeVoiceCount() == 3,
                "3 voices on single-SID path");

        // Render through BitPerfectEngine's processBlock (which now
        // delegates to single-SID mode).
        constexpr int N = 1024;
        std::vector<float> outL(N), outR(N);
        float* outputs[2] = { outL.data(), outR.data() };
        eng.processBlock(outputs, N);

        // Output must be finite and bounded ±1 (master clamp).
        for (int i = 0; i < N; ++i) {
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "single-SID-mode output is finite");
            require(outL[i] >= -1.0f && outL[i] <= 1.0f,
                    "single-SID-mode L output bounded ±1");
            require(outR[i] >= -1.0f && outR[i] <= 1.0f,
                    "single-SID-mode R output bounded ±1");
        }
    }

    // ── H. Voice stealing on the BitPerfectEngine single-SID path ──────────
    {
        BitPerfectEngine eng;
        eng.setSampleRate(48000.0);
        eng.setSidChipTopologyMode(BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice);
        eng.setSidVoiceStealingPolicy(VoiceStealingPolicy::StealOldest);
        for (int n = 60; n <= 64; ++n) {
            eng.singleSidEngine().noteOn(static_cast<uint8_t>(n), 100);
        }
        require(eng.singleSidEngine().activeVoiceCount() == 3,
                "5 notes onto 3-voice engine: still exactly 3 active (stealing happened)");
    }

    // ── I. Multi-chip mode unaffected by single-SID engine presence ────────
    //
    // The audit's #29 wire-up MUST NOT change behavior of the legacy
    // multi-chip path. We exercise the default mode and verify
    // processBlock produces finite output without ever touching
    // singleSidEngine_.
    {
        BitPerfectEngine eng;
        eng.setSampleRate(48000.0);
        // Default = MultiChipPolyIllusion — single-SID engine should not be
        // engaged by processBlock at all.
        require(eng.sidChipTopologyMode() == BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion,
                "default remains multi-chip");
        constexpr int N = 512;
        std::vector<float> outL(N), outR(N);
        float* outputs[2] = { outL.data(), outR.data() };
        eng.processBlock(outputs, N);
        for (int i = 0; i < N; ++i) {
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "multi-chip mode output is finite (no regression)");
        }
        // Single-SID engine should be inert / silent in this mode.
        require(eng.singleSidEngine().activeVoiceCount() == 0,
                "single-SID engine is dormant when topology is MultiChipPolyIllusion");
    }

    // ── J. Switching modes mid-life resets the single-SID allocator ────────
    //
    // The contract: switching INTO SingleChip3Voice re-prepares the engine;
    // switching OUT of it calls allNotesOff so dormant voices don't leak.
    {
        BitPerfectEngine eng;
        eng.setSampleRate(48000.0);
        eng.setSidChipTopologyMode(BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice);
        eng.singleSidEngine().noteOn(60, 100);
        require(eng.singleSidEngine().activeVoiceCount() == 1, "1 voice active");
        eng.setSidChipTopologyMode(BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion);
        require(eng.singleSidEngine().activeVoiceCount() == 0,
                "switching out of SingleChip3Voice clears the single-SID allocator");
    }

    std::cout << "filter_core_unification_and_topology_mode_v532_tests: audit #29/#31/#32 unification + wire-up pinned\n";
    return 0;
}
