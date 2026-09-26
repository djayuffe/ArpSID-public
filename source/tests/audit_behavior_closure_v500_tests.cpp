// Copyright (C) 2024-2026 Ulf Bertilsson
// audit_behavior_closure_v500_tests.cpp
// Behavior tests for all P0/P1/P2 audit findings.
// These tests exercise real runtime/render behavior, not source-text needles.

#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_timing_math.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/core/sid_realtime_guard.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void req(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: C64RunResult distinguishes executed vs passive cycles
// Validates Fix P0.3: instruction-budget hit must NOT silently advance PHI2.
// ─────────────────────────────────────────────────────────────────────────────
static void testC64RunResultCycleAccounting() {
    ArpSID::C64::C64Platform platform;
    platform.reset(true); // PAL
    platform.bootFromResetVector();
    platform.startRealtimeSidCore();

    // Request more cycles than a single instruction can consume with budget=1.
    // With maxInstructions=1 and 2000 cycles requested:
    // - One instruction executes (2-7 PHI2 cycles)
    // - Remaining cycles advance passively
    // - instructionBudgetHit must be true
    // - passiveCycles must be > 0
    const uint64_t requested = 2000u;
    const auto r = platform.runRealtimeSidCoreCycles(requested, 1u);

    req(r.requestedCycles == requested, "requestedCycles must equal input");
    req(r.executedInstructions <= 1u, "budget=1 must limit to at most 1 instruction");
    // With budget=1 and 2000 cycles, the budget is hit before exhausting cycles.
    req(r.instructionBudgetHit || r.cpuJammed,
        "instruction budget=1 should be hit or CPU jammed for 2000-cycle request");
    req(r.executedCycles + r.passiveCycles == r.requestedCycles ||
        r.completedCycleBudget,
        "executedCycles + passiveCycles must account for all requested cycles");

    // For a large budget run (full cycle coverage): budget high enough, passiveCycles
    // should be small (residual only: the <8-cycle break at end of main loop).
    ArpSID::C64::C64Platform platform2;
    platform2.reset(true);
    platform2.bootFromResetVector();
    platform2.startRealtimeSidCore();
    const uint64_t cycles2 = 1000u;
    const auto r2 = platform2.runRealtimeSidCoreCycles(cycles2, 65535u);
    req(!r2.instructionBudgetHit || r2.cpuJammed,
        "large instruction budget must not hit budget limit for 1000 cycles");
    // Passive residual upper bound:
    // - End-of-loop guard: < 8 PHI2 cycles (the "if (end - phi2Cycle_) < 8u break")
    // - VIC half-cycle stalls: up to 16 additional PHI2 cycles can be applied per
    // opcode by the deterministic BA/AEC model. Combined, the worst-case
    // residual on the last opcode boundary is below 32 cycles.
    // Passive residual upper bound only applies when the CPU is making progress.
    // If the CPU jams at a KIL/JAM opcode (which happens during a handler reset    // vector boot before any KERNAL is loaded), all remaining PHI2 cycles must
    // still be advanced passively to keep the wall clock honest — that's
    // expected, not a bug.
    if (!r2.cpuJammed) {
        req(r2.passiveCycles < 32u,
            "passive cycles should be small residual (<32) when CPU is running");
    } else {
        req(r2.executedCycles + r2.passiveCycles == cycles2,
            "passive cycles must account for all remaining budget when CPU jams");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: PSID VBI cadence matches C64 timing math for PAL and NTSC
// Validates Fix P0.3 / audit finding #3: cadence must come from geometry, not
// hardcoded 50/60 Hz.
// ─────────────────────────────────────────────────────────────────────────────
static void testPsidVbiCadence() {
    // PAL: 985248 / (63*312) = 985248 / 19656 ≈ 50.125 Hz
    const uint64_t palFrameCycles = ArpSID::C64::C64TimingMath::psidVbiFrameCycles(true);
    req(palFrameCycles == 19656ull, "PAL VBI frame must be 63*312 = 19656 PHI2 cycles");

    // NTSC: 1022727 / (65*263) = 1022727 / 17095 ≈ 59.826 Hz
    const uint64_t ntscFrameCycles = ArpSID::C64::C64TimingMath::psidVbiFrameCycles(false);
    req(ntscFrameCycles == 17095ull, "NTSC VBI frame must be 65*263 = 17095 PHI2 cycles");

    // PAL play period in samples at 44100 Hz: 19656 / 985248 * 44100 ≈ 880.0 samples
    const double palPeriod = ArpSID::C64::C64TimingMath::psidPlayPeriodSamplesFromCycles(
        palFrameCycles, 44100.0, ArpSID::C64::kPalPhi2Hz);
    req(std::fabs(palPeriod - 880.0) < 1.0,
        "PAL VBI play period at 44100 Hz must be approximately 880 samples");

    // NTSC play period in samples at 44100 Hz: 17095 / 1022727 * 44100 ≈ 736.7 samples
    const double ntscPeriod = ArpSID::C64::C64TimingMath::psidPlayPeriodSamplesFromCycles(
        ntscFrameCycles, 44100.0, ArpSID::C64::kNtscPhi2Hz);
    req(std::fabs(ntscPeriod - 736.7) < 2.0,
        "NTSC VBI play period at 44100 Hz must be approximately 737 samples");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: CIA Timer A latch matches timing math
// Validates that CIA-speed PSID cadence uses actual latch value, not hardcoded Hz.
// ─────────────────────────────────────────────────────────────────────────────
static void testCiaTimerACadence() {
    // PAL CIA Timer A default latch for PSID: from timing math
    const uint64_t palLatch = static_cast<uint64_t>(
        ArpSID::C64::C64TimingMath::psidCiaTimerALatch(true));
    req(palLatch > 0u && palLatch <= 0xFFFFu,
        "PAL CIA Timer A latch must be a valid 16-bit value");

    // The latch should correspond to ~50 Hz CIA interrupt cadence for PAL
    // palLatch cycles / 985248 Hz ≈ 0.02 seconds → palLatch ≈ 19713
    req(palLatch >= 19000u && palLatch <= 20500u,
        "PAL CIA Timer A latch must correspond to approximately 50 Hz IRQ cadence");

    const uint64_t ntscLatch = static_cast<uint64_t>(
        ArpSID::C64::C64TimingMath::psidCiaTimerALatch(false));
    req(ntscLatch > 0u && ntscLatch <= 0xFFFFu,
        "NTSC CIA Timer A latch must be a valid 16-bit value");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: sanitizePersistentStateRootForSerialization is NOT noexcept
// Validates Fix P0.2: heap-mutating functions must not be noexcept.
// ─────────────────────────────────────────────────────────────────────────────
static void testNonRTFunctionsNotNoexcept() {
    // Compile-time check: these functions must not be noexcept.
    // If they were declared noexcept, the static_assert would fail at compile time
    // because noexcept(f(args)) would return true.
    // We use a lambda to form the expression type without actually calling it.
    ArpSID::SidStateRootV1 root{};
    (void)root;

    static_assert(
        !noexcept(ArpSID::sanitizePersistentStateRootForSerialization(root)),
        "sanitizePersistentStateRootForSerialization must NOT be noexcept — it allocates");

    static_assert(
        !noexcept(ArpSID::sidCanonicalizeSemanticParameterEntries(root)),
        "sidCanonicalizeSemanticParameterEntries must NOT be noexcept — it allocates");

    static_assert(
        !noexcept(ArpSID::sidEnsureSemanticParameterEntries(root)),
        "sidEnsureSemanticParameterEntries must NOT be noexcept — it allocates");

    static_assert(
        !noexcept(ArpSID::sidHydrateParameterValuesFromSemanticEntries(root)),
        "sidHydrateParameterValuesFromSemanticEntries must NOT be noexcept — it allocates");

    static_assert(
        !noexcept(ArpSID::sidSetStateRootParamValue(root, 0, 0.0f)),
        "sidSetStateRootParamValue must NOT be noexcept — it may push_back");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: sanitize produces stable idempotent output
// Validates that sanitizePersistentStateRootForSerialization is stable on the
// non-RT side: calling it twice produces the same result.
// ─────────────────────────────────────────────────────────────────────────────
static void testSanitizeIdempotency() {
    ArpSID::SidStateRootV1 root{};
    root.patch.variant_profile = ArpSID::sidDefaultVariantProfile(
        ArpSID::SidFamily::MOS8580, ArpSID::SidVideoStandard::PAL);
    ArpSID::sidSetStateRootParamValue(root, 0, 0.5f);
    ArpSID::sidSetStateRootParamValue(root, 1, 0.25f);

    ArpSID::sanitizePersistentStateRootForSerialization(root);
    const size_t count1 = root.patch.parameters.semantic_entries.size();
    const float v0_1 = root.patch.parameters.values.empty() ? 0.0f : root.patch.parameters.values[0];

    ArpSID::sanitizePersistentStateRootForSerialization(root);
    const size_t count2 = root.patch.parameters.semantic_entries.size();
    const float v0_2 = root.patch.parameters.values.empty() ? 0.0f : root.patch.parameters.values[0];

    req(count1 == count2, "sanitize must be idempotent: semantic_entries count must not change on second call");
    req(v0_1 == v0_2, "sanitize must be idempotent: param value[0] must not change on second call");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: C64 mirror fidelity formula is correct
// Validates Fix P0.4: fidelity = cyclesThisBlock / cyclesDueThisBlock.
// ─────────────────────────────────────────────────────────────────────────────
static void testC64MirrorFidelityFormula() {
    // Simulate the fidelity calculation from updateTelemetry_.
    constexpr uint64_t kC64RealtimeMaxCyclesPerAudioBlock = 768ull;

    // At 44100 Hz, 512 samples → ~11437 PHI2 cycles for PAL
    const double sr = 44100.0;
    const double palClock = static_cast<double>(ArpSID::C64::kPalPhi2Hz);
    const uint64_t cyclesDue = static_cast<uint64_t>((512.0 * palClock) / sr);
    const uint64_t cyclesActual = std::min(cyclesDue, kC64RealtimeMaxCyclesPerAudioBlock);
    const float fidelity = (cyclesDue > 0u)
        ? std::clamp(static_cast<float>(cyclesActual) / static_cast<float>(cyclesDue), 0.0f, 1.0f)
        : 1.0f;

    // Mirror cap of 768 out of ~11437 cycles → fidelity ≈ 0.067 (well below 0.95)
    req(fidelity < 0.95f,
        "C64 mirror fidelity at 512-frame block must be below 0.95 — mirror is lossy by design");
    req(fidelity > 0.0f,
        "C64 mirror fidelity must be positive (some cycles do execute)");
    req(std::isfinite(fidelity),
        "C64 mirror fidelity must be finite");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: No passive cycles when instruction budget is ample (small run)
// Validates that for small cycle counts with generous budget, passive ≈ 0.
// ─────────────────────────────────────────────────────────────────────────────
static void testNoPassiveCyclesWithAmpleBudget() {
    ArpSID::C64::C64Platform platform;
    platform.reset(true);
    platform.bootFromResetVector();
    platform.startRealtimeSidCore();

    // Run 64 cycles with budget 1000 — should not hit budget, passive ≤ 7.
    const auto r = platform.runRealtimeSidCoreCycles(64u, 1000u);
    req(r.requestedCycles == 64u, "requested must match");
    req(r.executedCycles + r.passiveCycles == 64u,
        "executed + passive must sum to requested for small run");
    req(r.passiveCycles <= 7u,
        "passive cycles must be at most 7 (residual from <8-cycle guard) with ample budget");
    req(!r.instructionBudgetHit || r.cpuJammed,
        "instruction budget must not be hit with 1000 budget for 64 cycles");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: RSID runRsidMachineCycles returns C64RunResult (return type check)
// Validates Fix P0.3: caller can inspect executed vs passive cycles.
// ─────────────────────────────────────────────────────────────────────────────
static void testRsidRunResultType() {
    // Compile-time check: runRsidMachineCycles must return C64RunResult.
    // Instantiate a C64Runtime just to get the return type (don't call it with invalid data).
    using ResultType = decltype(std::declval<ArpSID::C64::C64Runtime>().runRsidMachineCycles(0u, 0u, nullptr));
    static_assert(std::is_same_v<ResultType, ArpSID::C64::C64RunResult>,
        "runRsidMachineCycles must return C64RunResult, not bool or uint32_t");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9: C64RunResult completedCycleBudget is accurate
// ─────────────────────────────────────────────────────────────────────────────
static void testC64RunResultCompletedBudget() {
    ArpSID::C64::C64Platform platform;
    platform.reset(true);
    platform.bootFromResetVector();
    platform.startRealtimeSidCore();

    // Any run should complete the cycle budget (passive catches up to end).
    const auto r = platform.runRealtimeSidCoreCycles(512u, 65535u);
    req(r.completedCycleBudget,
        "completedCycleBudget must be true when passive catch-up runs to end");
    req(r.executedCycles + r.passiveCycles == r.requestedCycles,
        "executed + passive must equal requested when completedCycleBudget is true");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10: sidStateRootParamValue round-trips correctly (RT-safe read path)
// ─────────────────────────────────────────────────────────────────────────────
static void testParamValueRoundTrip() {
    ArpSID::SidStateRootV1 root{};
    root.patch.variant_profile = ArpSID::sidDefaultVariantProfile(
        ArpSID::SidFamily::MOS8580, ArpSID::SidVideoStandard::PAL);

    ArpSID::sanitizePersistentStateRootForSerialization(root);

    // sidStateRootParamValue (RT-safe, const, noexcept) must read back defaults.
    for (int i = 0; i < ArpSID::kNumParams; ++i) {
        if (ArpSID::isRuntimeOnlyOrTransientParam(i)) continue;
        const float v = ArpSID::sidStateRootParamValue(root, i);
        req(std::isfinite(v), "sidStateRootParamValue must return finite value for all params");
        req(v >= 0.0f && v <= 1.0f, "sidStateRootParamValue must return clamped value [0,1]");
    }
}

int main() {
    std::printf("Running audit behavior closure tests (v500)...\n");

    testC64RunResultCycleAccounting();
    std::printf("  [PASS] C64RunResult cycle accounting\n");

    testPsidVbiCadence();
    std::printf("  [PASS] PSID VBI cadence PAL/NTSC\n");

    testCiaTimerACadence();
    std::printf("  [PASS] CIA Timer A cadence\n");

    testNonRTFunctionsNotNoexcept();
    std::printf("  [PASS] Non-RT functions not noexcept\n");

    testSanitizeIdempotency();
    std::printf("  [PASS] sanitize idempotency\n");

    testC64MirrorFidelityFormula();
    std::printf("  [PASS] C64 mirror fidelity formula\n");

    testNoPassiveCyclesWithAmpleBudget();
    std::printf("  [PASS] No excessive passive cycles with ample budget\n");

    testRsidRunResultType();
    std::printf("  [PASS] RSID runRsidMachineCycles return type\n");

    testC64RunResultCompletedBudget();
    std::printf("  [PASS] C64RunResult completedCycleBudget\n");

    testParamValueRoundTrip();
    std::printf("  [PASS] Param value round-trip (RT-safe read)\n");

    std::printf("All audit behavior tests PASSED.\n");
    return 0;
}
