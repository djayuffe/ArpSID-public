// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// diag_counter_snapshot_v549_tests.cpp — Contract tests for v549 diagnostic snapshot.
//
// Tests:
// Section I — Layout pinning: schema version, struct size, trivially-copyable
// Section II — Default construction: all counters are 0, schema version correct
// Section III — Round-trip: write all 24 fields, read back via array offsets
// Section IV — SidRegisterEngine filterAbsClampHitCount accessor exists
// Section V — SidRuntimeModel accessor visibility (compile-time)

#include "arpsid/gui/diagnostic_snapshot.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/core/sid_runtime_model.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <type_traits>

// ─── Section I: Layout pinning ───────────────────────────────────────────────

static_assert(ArpSID::GUI::kDiagSchemaVersion == 8u,
              "kDiagSchemaVersion must be 8");

static_assert(std::is_trivially_copyable<ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot>::value,
              "ArpSIDDiagnosticCounterSnapshot must be trivially copyable");

static_assert(sizeof(ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot) ==
              sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t) * 56,
              "Struct layout must be 2×uint32 + 56×uint64");

// ─── Section II: Default construction ────────────────────────────────────────

static void testDefaultConstruction() {
    ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot snap{};
    assert(snap.schemaVersion == ArpSID::GUI::kDiagSchemaVersion);
    assert(snap.reserved == 0u);
    assert(snap.renderEpoch == 0u);
    assert(snap.notifyCallbackViolationCount == 0u);
    assert(snap.renderDrainTimeoutCount == 0u);
    assert(snap.splitBrainDiagnosticCount == 0u);
    assert(snap.scratchUnderCapacityCountAuv2 == 0u);
    assert(snap.preNotifyFailureCount == 0u);
    assert(snap.hostBufferScratchAttachCount == 0u);
    assert(snap.bridgeDivertedRenderCount == 0u);
    assert(snap.activityMutexRtViolations == 0u);
    assert(snap.stateMutexRtViolations == 0u);
    assert(snap.propListenerMutexRtViolations == 0u);
    assert(snap.closeWaitMutexRtViolations == 0u);
    assert(snap.renderScratchEpochAuv3 == 0u);
    assert(snap.scratchUnderCapacityCountAuv3 == 0u);
    assert(snap.c64PsidVideoStandardFallbackCount == 0u);
    assert(snap.c64PsidRenderHandoffCount == 0u);
    assert(snap.zeroCycleSampleCount == 0u);
    assert(snap.outputStageNoiseSampleCount == 0u);
    assert(snap.filterAbsClampHitCount == 0u);
    assert(snap.invalidClockFrequencyRejectCount == 0u);
    assert(snap.pendingEventsDrainDroppedCount == 0u);
    assert(snap.ingressFallbackEdgeOverflowCount == 0u);
    assert(snap.stateRestoreOverlayAuthenticCount == 0u);
    assert(snap.stateRestoreOverlayAnalogCount == 0u);
    assert(snap.rsidStrictStatusCode == 0u);
    assert(snap.rsidPlaybackModeCode == 0u);
    assert(snap.rsidExactnessDowngradeMask == 0u);
    assert(snap.rsidExactPlaybackActive == 0u);
    assert(snap.phi2ApproximateOpcodeTotal == 0u);
    assert(snap.phi2UnsupportedOpcodeTotal == 0u);
    assert(snap.initBrkSentinelCount == 0u);
    assert(snap.sidReadApproximationCount == 0u);
    assert(snap.sidOpenBusReadCount == 0u);
    assert(snap.invalidSidChipReadCount == 0u);
    assert(snap.invalidSidChipWriteCount == 0u);
    assert(snap.sidHoleWriteCount == 0u);
    assert(snap.rmwSidWriteCount == 0u);
    // v840/v841 render-path stall counters default to zero.
    assert(snap.c64CallbackLastBlockMicros == 0u);
    assert(snap.c64CallbackMaxBlockMicros == 0u);
    assert(snap.c64CallbackOverrunCount == 0u);
    assert(snap.c64PreC64LastBlockMicros == 0u);
    assert(snap.c64PreC64MaxBlockMicros == 0u);
    assert(snap.c64RenderLastBlockMicros == 0u);
    assert(snap.c64RenderMaxBlockMicros == 0u);
    assert(snap.c64RenderOverrunCount == 0u);
    assert(snap.c64PostC64LastBlockMicros == 0u);
    assert(snap.c64PostC64MaxBlockMicros == 0u);
    assert(snap.c64TelemetryLastBlockMicros == 0u);
    assert(snap.c64TelemetryMaxBlockMicros == 0u);
    assert(snap.c64MaxHostGapMicros == 0u);
    assert(snap.c64MaxCatchupCycles == 0u);
    assert(snap.c64MaxContinuousCatchupCycles == 0u);
    assert(snap.c64MaxCiaCatchupCycles == 0u);
    assert(snap.c64MaxVbiCatchupCycles == 0u);
    assert(snap.c64LastPassiveDebtCycles == 0u);
    (void)snap;
    std::puts("  II: default construction — OK");
}

// ─── Section III: Round-trip assignment ──────────────────────────────────────

static void testRoundTrip() {
    ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot snap{};
    snap.renderEpoch                     = 0xAABBCCDD00000001ULL;
    snap.notifyCallbackViolationCount    = 2u;
    snap.renderDrainTimeoutCount         = 3u;
    snap.splitBrainDiagnosticCount       = 4u;
    snap.scratchUnderCapacityCountAuv2   = 5u;
    snap.preNotifyFailureCount           = 6u;
    snap.hostBufferScratchAttachCount    = 7u;
    snap.bridgeDivertedRenderCount       = 8u;
    snap.activityMutexRtViolations       = 9u;
    snap.stateMutexRtViolations          = 10u;
    snap.propListenerMutexRtViolations   = 11u;
    snap.closeWaitMutexRtViolations      = 12u;
    snap.renderScratchEpochAuv3          = 13u;
    snap.scratchUnderCapacityCountAuv3   = 14u;
    snap.c64PsidVideoStandardFallbackCount = 15u;
    snap.c64PsidRenderHandoffCount       = 16u;
    snap.zeroCycleSampleCount            = 17u;
    snap.outputStageNoiseSampleCount     = 18u;
    snap.filterAbsClampHitCount          = 19u;
    snap.invalidClockFrequencyRejectCount = 20u;
    snap.pendingEventsDrainDroppedCount  = 21u;
    snap.ingressFallbackEdgeOverflowCount = 22u;
    snap.stateRestoreOverlayAuthenticCount = 0u;  // reserved
    snap.stateRestoreOverlayAnalogCount   = 0u;   // reserved
    snap.rsidStrictStatusCode = 2u;
    snap.rsidPlaybackModeCode = 1u;
    snap.rsidExactnessDowngradeMask = 0x1200u;
    snap.rsidExactPlaybackActive = 2u;
    snap.phi2ApproximateOpcodeTotal = 23u;
    snap.phi2UnsupportedOpcodeTotal = 24u;
    snap.initBrkSentinelCount = 25u;
    snap.sidReadApproximationCount = 26u;
    snap.sidOpenBusReadCount = 27u;
    snap.invalidSidChipReadCount = 28u;
    snap.invalidSidChipWriteCount = 29u;
    snap.sidHoleWriteCount = 30u;
    snap.rmwSidWriteCount = 31u;
    snap.c64CallbackLastBlockMicros = 32u;
    snap.c64CallbackMaxBlockMicros = 33u;
    snap.c64CallbackOverrunCount = 34u;
    snap.c64PreC64LastBlockMicros = 35u;
    snap.c64PreC64MaxBlockMicros = 36u;
    snap.c64RenderLastBlockMicros = 37u;
    snap.c64RenderMaxBlockMicros = 38u;
    snap.c64RenderOverrunCount = 39u;
    snap.c64PostC64LastBlockMicros = 40u;
    snap.c64PostC64MaxBlockMicros = 41u;
    snap.c64TelemetryLastBlockMicros = 42u;
    snap.c64TelemetryMaxBlockMicros = 43u;
    snap.c64MaxHostGapMicros = 44u;
    snap.c64MaxCatchupCycles = 45u;
    snap.c64MaxContinuousCatchupCycles = 46u;
    snap.c64MaxCiaCatchupCycles = 47u;
    snap.c64MaxVbiCatchupCycles = 48u;
    snap.c64LastPassiveDebtCycles = 49u;

    // Trivial-copy round-trip
    ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot copy = snap;
    assert(copy.renderEpoch == 0xAABBCCDD00000001ULL);
    assert(copy.notifyCallbackViolationCount == 2u);
    assert(copy.renderDrainTimeoutCount == 3u);
    assert(copy.splitBrainDiagnosticCount == 4u);
    assert(copy.scratchUnderCapacityCountAuv2 == 5u);
    assert(copy.preNotifyFailureCount == 6u);
    assert(copy.hostBufferScratchAttachCount == 7u);
    assert(copy.bridgeDivertedRenderCount == 8u);
    assert(copy.activityMutexRtViolations == 9u);
    assert(copy.stateMutexRtViolations == 10u);
    assert(copy.propListenerMutexRtViolations == 11u);
    assert(copy.closeWaitMutexRtViolations == 12u);
    assert(copy.renderScratchEpochAuv3 == 13u);
    assert(copy.scratchUnderCapacityCountAuv3 == 14u);
    assert(copy.c64PsidVideoStandardFallbackCount == 15u);
    assert(copy.c64PsidRenderHandoffCount == 16u);
    assert(copy.zeroCycleSampleCount == 17u);
    assert(copy.outputStageNoiseSampleCount == 18u);
    assert(copy.filterAbsClampHitCount == 19u);
    assert(copy.invalidClockFrequencyRejectCount == 20u);
    assert(copy.pendingEventsDrainDroppedCount == 21u);
    assert(copy.ingressFallbackEdgeOverflowCount == 22u);
    assert(copy.stateRestoreOverlayAuthenticCount == 0u);
    assert(copy.stateRestoreOverlayAnalogCount == 0u);
    assert(copy.rsidStrictStatusCode == 2u);
    assert(copy.rsidPlaybackModeCode == 1u);
    assert(copy.rsidExactnessDowngradeMask == 0x1200u);
    assert(copy.rsidExactPlaybackActive == 2u);
    assert(copy.phi2ApproximateOpcodeTotal == 23u);
    assert(copy.phi2UnsupportedOpcodeTotal == 24u);
    assert(copy.initBrkSentinelCount == 25u);
    assert(copy.sidReadApproximationCount == 26u);
    assert(copy.sidOpenBusReadCount == 27u);
    assert(copy.invalidSidChipReadCount == 28u);
    assert(copy.invalidSidChipWriteCount == 29u);
    assert(copy.sidHoleWriteCount == 30u);
    assert(copy.rmwSidWriteCount == 31u);
    assert(copy.c64CallbackLastBlockMicros == 32u);
    assert(copy.c64CallbackMaxBlockMicros == 33u);
    assert(copy.c64CallbackOverrunCount == 34u);
    assert(copy.c64PreC64LastBlockMicros == 35u);
    assert(copy.c64PreC64MaxBlockMicros == 36u);
    assert(copy.c64RenderLastBlockMicros == 37u);
    assert(copy.c64RenderMaxBlockMicros == 38u);
    assert(copy.c64RenderOverrunCount == 39u);
    assert(copy.c64PostC64LastBlockMicros == 40u);
    assert(copy.c64PostC64MaxBlockMicros == 41u);
    assert(copy.c64TelemetryLastBlockMicros == 42u);
    assert(copy.c64TelemetryMaxBlockMicros == 43u);
    assert(copy.c64MaxHostGapMicros == 44u);
    assert(copy.c64MaxCatchupCycles == 45u);
    assert(copy.c64MaxContinuousCatchupCycles == 46u);
    assert(copy.c64MaxCiaCatchupCycles == 47u);
    assert(copy.c64MaxVbiCatchupCycles == 48u);
    assert(copy.c64LastPassiveDebtCycles == 49u);
    (void)copy;
    std::puts("  III: round-trip assignment — OK");
}

// ─── Section IV: memcpy round-trip (trivially-copyable guarantee) ─────────────

static void testMemcpyRoundTrip() {
    ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot src{};
    src.renderEpoch = 0xDEADBEEFCAFEBABEULL;
    src.bridgeDivertedRenderCount = 42u;

    ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot dst{};
    std::memcpy(&dst, &src, sizeof(src));
    assert(dst.renderEpoch == 0xDEADBEEFCAFEBABEULL);
    assert(dst.bridgeDivertedRenderCount == 42u);
    assert(dst.schemaVersion == ArpSID::GUI::kDiagSchemaVersion);
    std::puts("  IV: memcpy round-trip (trivially-copyable) — OK");
}

// ─── Section V: SidRegisterEngine filterAbsClampHitCount accessor ─────────────

static void testSidRegEngineAccessor() {
    // Confirm the accessor compiles and returns 0 on a fresh instance.
    // Note: SidRegisterEngine has a complex dependency on prewarmed SID tables.
    // We only verify the accessor is callable; we don't call renderBlock.
    ArpSID::prewarmAllSidTables();
    ArpSID::SidRegisterEngine eng;
    eng.prepare(44100.0);
    const uint64_t count = eng.filterAbsClampHitCount();
    assert(count == 0u);
    (void)count;
    std::puts("  V: SidRegisterEngine::filterAbsClampHitCount accessor — OK");
}

// ─── Section VI: SidRuntimeModel diagnostic accessor visibility ───────────────

static void testSidRuntimeModelAccessors() {
    // Compile-time verification: these methods must be public.
    // We don't call them to avoid complex model initialization.
    using M = ArpSID::SidRuntimeModel;
    // If these lines compile, the accessors are public.
    (void)static_cast<uint64_t (M::*)() const noexcept>(&M::pendingEventsDrainDroppedCount);
    (void)static_cast<uint64_t (M::*)() const noexcept>(&M::ingressFallbackEdgeOverflowCount);
    std::puts("  VI: SidRuntimeModel diagnostic accessor visibility — OK");
}

int main() {
    std::puts("diag_counter_snapshot_v549_tests");
    std::puts("  I: static_assert layout pinning — OK");
    testDefaultConstruction();
    testRoundTrip();
    testMemcpyRoundTrip();
    testSidRegEngineAccessor();
    testSidRuntimeModelAccessors();
    std::puts("ALL PASS");
    return 0;
}
