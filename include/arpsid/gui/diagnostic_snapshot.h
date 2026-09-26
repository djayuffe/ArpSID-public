// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// diagnostic_snapshot.h — POD snapshot for C64 STATE diagnostic dashboard.
// (audit P1.11 added rsidPhysicalBlockerMask: 37 -> 38 uint64 counters.)
//
// PURPOSE:
// Provides a lock-free-safe read surface for all 24 audit-stabilization atomic
// counters that the C64 STATE tab displays. The snapshot is filled by
// ArpSIDDSPKernelAdapter::readDiagnosticCounters (pull from kernel) and
// overlaid with AUv2/AUv3 host-layer counters pushed from outside the kernel.
//
// CONTRACT:
// * POD struct — trivially copyable, zero-initialized by value-init.
// * Fields ordered to match the C64 STATE panel label list.
// * Fields #10 and #11 (stateRestoreOverlayAuthentic/Analog) are reserved
// (always 0) — the corresponding kernel counters don't exist yet.
// * kDiagSchemaVersion pins the layout. Increment when fields are added.

#ifndef ARPSID_GUI_DIAGNOSTIC_SNAPSHOT_H
#define ARPSID_GUI_DIAGNOSTIC_SNAPSHOT_H

#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// Schema version — increment when the struct layout changes.
inline constexpr std::uint32_t kDiagSchemaVersion = 8u;

/// Snapshot of all 24 audit-stabilization counters for the C64 STATE dashboard.
/// Indices match the C64 STATE panel label row order from _c64StatePanel_v544_:
/// [0] renderEpoch
/// [24] rsidStrictStatusCode (0=no strict path, 1=clean, 2=strict-PHI2 downgraded)
/// [25] rsidPlaybackModeCode (0=none/unloaded/non-RSID, 1=strict, 2=compatible)
/// [26] rsidExactnessDowngradeMask (RsidExactnessDowngrade bitmask)
/// [27] rsidExactPlaybackActive legacy alias for [24]
/// [28] phi2ApproximateOpcodeTotal (cumulative since last load)
/// [29] phi2UnsupportedOpcodeTotal (cumulative since last load)
/// [30] initBrkSentinelCount
/// [31] sidReadApproximationCount
/// [32] sidOpenBusReadCount
/// [33] invalidSidChipReadCount
/// [34] invalidSidChipWriteCount
/// [35] sidHoleWriteCount
/// [36] rmwSidWriteCount
/// [1] notifyCallbackViolationCount
/// [2] renderDrainTimeoutCount
/// [3] splitBrainDiagnosticCount
/// [4] scratchUnderCapacityCount (AUv2-side)
/// [5] pendingEventsDrainDroppedCount
/// [6] ingressFallbackEdgeOverflowCount
/// [7] zeroCycleSampleCount
/// [8] outputStageNoiseSampleCount
/// [9] filterAbsClampHitCount
/// [10] stateRestoreOverlayAuthenticCount (reserved, always 0)
/// [11] stateRestoreOverlayAnalogCount (reserved, always 0)
/// [12] invalidClockFrequencyRejectCount
/// [13] preNotifyFailureCount
/// [14] hostBufferScratchAttachCount
/// [15] renderScratchEpochAuv3
/// [16] scratchUnderCapacityCountAuv3
/// [17] c64PsidVideoStandardFallbackCount
/// [18] c64PsidRenderHandoffCount
/// [19] activityMutexRtViolations
/// [20] stateMutexRtViolations
/// [21] propListenerMutexRtViolations
/// [22] closeWaitMutexRtViolations
/// [23] bridgeDivertedRenderCount
struct ArpSIDDiagnosticCounterSnapshot {
    std::uint32_t schemaVersion{kDiagSchemaVersion};
    std::uint32_t reserved{0u};

    // AUv2 host-layer counters (pushed from ArpSIDAUv2Component.mm render thread)
    std::uint64_t renderEpoch{0};
    std::uint64_t notifyCallbackViolationCount{0};
    std::uint64_t renderDrainTimeoutCount{0};
    std::uint64_t splitBrainDiagnosticCount{0};
    std::uint64_t scratchUnderCapacityCountAuv2{0};
    std::uint64_t preNotifyFailureCount{0};
    std::uint64_t hostBufferScratchAttachCount{0};
    std::uint64_t bridgeDivertedRenderCount{0};
    std::uint64_t activityMutexRtViolations{0};
    std::uint64_t stateMutexRtViolations{0};
    std::uint64_t propListenerMutexRtViolations{0};
    std::uint64_t closeWaitMutexRtViolations{0};

    // AUv3 AudioUnit counters (pushed from ArpSIDAudioUnit.mm render thread)
    std::uint64_t renderScratchEpochAuv3{0};
    std::uint64_t scratchUnderCapacityCountAuv3{0};

    // Kernel-accessible counters (pulled from ArpSIDDSPKernel on GUI thread)
    std::uint64_t c64PsidVideoStandardFallbackCount{0};
    std::uint64_t c64PsidRenderHandoffCount{0};
    std::uint64_t zeroCycleSampleCount{0};
    std::uint64_t outputStageNoiseSampleCount{0};
    std::uint64_t filterAbsClampHitCount{0};
    std::uint64_t invalidClockFrequencyRejectCount{0};
    std::uint64_t pendingEventsDrainDroppedCount{0};
    std::uint64_t ingressFallbackEdgeOverflowCount{0};

    // Reserved overlay counters report zero until a runtime source publishes them.
    std::uint64_t stateRestoreOverlayAuthenticCount{0};
    std::uint64_t stateRestoreOverlayAnalogCount{0};

    // RSID strict/physical status fields.
    // rsidStrictStatusCode:
    //   0 = no active strict-PHI2 RSID path,
    //   1 = strict-PHI2 path active and no known exactness downgrade,
    //   2 = strict-PHI2 path active with known downgrade(s).
    // rsidExactPlaybackActive is kept as a legacy ABI/display alias for the
    // same status code. New code should use rsidStrictStatusCode plus the
    // explicit mode/downgrade fields below.
    std::uint64_t rsidStrictStatusCode{0};
    std::uint64_t rsidPlaybackModeCode{0};
    std::uint64_t rsidExactnessDowngradeMask{0};
    // audit P1.11: capability blockers that prevent TRUE physical C64 exactness
    // (CIA/VIC/SID-readback/ROM-identity/open-bus/legacy-path). Separate from the
    // observed-downgrade ledger above so the UI can present "PHYSICAL EXACTNESS:
    // blocked" with the actual blocker reasons instead of collapsing strict-PHI2,
    // downgrade-free, and physical-exact into one claim.
    std::uint64_t rsidPhysicalBlockerMask{0};
    std::uint64_t rsidExactPlaybackActive{0};
    std::uint64_t phi2ApproximateOpcodeTotal{0};
    std::uint64_t phi2UnsupportedOpcodeTotal{0};
    std::uint64_t initBrkSentinelCount{0};
    std::uint64_t sidReadApproximationCount{0};
    std::uint64_t sidOpenBusReadCount{0};
    std::uint64_t invalidSidChipReadCount{0};
    std::uint64_t invalidSidChipWriteCount{0};
    std::uint64_t sidHoleWriteCount{0};
    std::uint64_t rmwSidWriteCount{0};

    // v840/v841: C64P render-path stall instrumentation (localizes the
    // "play-stop" choppiness — emulation measured ~12x realtime, so the cause is
    // a periodic render-path stall, not compute). *Micros are wall-clock; the
    // audio deadline for an N-frame block is N*1e6/sampleRate µs.
    std::uint64_t c64CallbackLastBlockMicros{0};      // full processBlock C64 callback wall µs
    std::uint64_t c64CallbackMaxBlockMicros{0};       // peak full C64 callback wall µs
    std::uint64_t c64CallbackOverrunCount{0};         // full callback µs exceeded audio deadline
    std::uint64_t c64PreC64LastBlockMicros{0};        // pre-C64 branch work wall µs
    std::uint64_t c64PreC64MaxBlockMicros{0};         // peak pre-C64 branch wall µs
    std::uint64_t c64RenderLastBlockMicros{0};        // C64 emulation/render wall µs
    std::uint64_t c64RenderMaxBlockMicros{0};         // peak C64 emulation/render wall µs
    std::uint64_t c64RenderOverrunCount{0};           // C64 render µs exceeded the deadline
    std::uint64_t c64PostC64LastBlockMicros{0};       // post-C64 audio/finalize wall µs
    std::uint64_t c64PostC64MaxBlockMicros{0};        // peak post-C64 wall µs
    std::uint64_t c64TelemetryLastBlockMicros{0};     // C64 light telemetry wall µs
    std::uint64_t c64TelemetryMaxBlockMicros{0};      // peak C64 light telemetry wall µs
    std::uint64_t c64MaxHostGapMicros{0};             // peak wall gap between C64 deliveries (host jitter)
    std::uint64_t c64MaxCatchupCycles{0};             // aggregate max catch-up burst (legacy/summary)
    std::uint64_t c64MaxContinuousCatchupCycles{0};   // peak continuous RSID/playAddress==0 catch-up
    std::uint64_t c64MaxCiaCatchupCycles{0};          // peak CIA Timer-A service catch-up
    std::uint64_t c64MaxVbiCatchupCycles{0};          // peak VBI passive catch-up
    std::uint64_t c64LastPassiveDebtCycles{0};        // passive-cycle debt left after last block (~0 if healthy)
};

static_assert(std::is_trivially_copyable<ArpSIDDiagnosticCounterSnapshot>::value,
              "ArpSIDDiagnosticCounterSnapshot must be trivially copyable");
static_assert(sizeof(ArpSIDDiagnosticCounterSnapshot) ==
              sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t) * 56,
              "ArpSIDDiagnosticCounterSnapshot layout pinned (schema v8: split C64 render-stall counters)");

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_DIAGNOSTIC_SNAPSHOT_H
