#pragma once
#include "sid_ingress_lane.h"
#include "sid_event_queue.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include "sid_runtime_sizing.h"

// CANONICAL ORDERING TRUTH: This file is the single authority for arrival_order assignment.
// No wrapper may assign final global arrival_order. That happens only here, at merge.
//
// Sort law (per checklist §4.2):
// 1. sample_offset
// 2. cycle_offset
// 3. subphase
// 4. source priority (SidIngressSourcePriority)
// 5. source-local sequence
// 6. final global arrival_order (assigned here, monotonically)

namespace ArpSID {

static constexpr size_t kMergeLaneCount    = 5;    // one per SidIngressSourcePriority band
static constexpr size_t kMergeLaneCapacity = kSidRuntimeMergeLaneCapacity; // compact default; overridable for full desktop builds

using SidMergeEventLane = SidIngressLane<SidTimedEvent, kMergeLaneCapacity>;
static constexpr size_t kMergeScratchMax = kMergeLaneCount * (kMergeLaneCapacity + kSidCriticalRingCapacity);
static_assert(kMergeLaneCount == 5, "Update merge lane count if SidIngressSourcePriority changes");

inline constexpr size_t sidIngressPriorityToLaneIndex(SidIngressSourcePriority priority) noexcept {
    return static_cast<size_t>(priority);
}

struct SidMergeCandidate {
    SidTimedEvent event{};
    uint8_t       sourcePriority = 0;
    uint32_t      sourceLocalSeq = 0;
    int           laneIndex = 0;
};

// Merge all lanes, sort deterministically, assign canonical arrival_order,
// and emit into the runtime pending queue.
//
// Returns number of events merged. Overflow per lane is telemetry only.
// Primary overload: caller supplies persistent scratch storage.
// This avoids a large stack allocation inside the render-thread merge path
// which can blow the validator stack on some hosts.
inline int sidIngressMerge(
    std::array<SidMergeEventLane, kMergeLaneCount>& lanes,
    SidTimedEventQueue& pendingOut,
    std::atomic<uint32_t>& arrivalCounter,
    int frameCount,
    std::atomic<uint64_t>& totalOverflowTelemetry,   // audit #12: 64-bit so long-session drop totals never wrap
    SidMergeCandidate* scratch,
    size_t scratchCapacity) noexcept
{
    int count = 0;

    for (size_t li = 0; li < kMergeLaneCount; ++li) {
        auto& lane = lanes[li];
        // Collect overflow telemetry before drain.
        const uint64_t ov = lane.consumeOverflowCount();
        if (ov > 0) totalOverflowTelemetry.fetch_add(ov, std::memory_order_relaxed);

        SidIngressEntry<SidTimedEvent> entry{};
        while (lane.pop(entry)) {
            entry.event.sanitize(frameCount);
            if (count >= static_cast<int>(scratchCapacity)) {
                totalOverflowTelemetry.fetch_add(1, std::memory_order_relaxed);
                entry.event.arrival_order = arrivalCounter.fetch_add(1, std::memory_order_relaxed);
                // Timing-authority: merge owns arrival_order only.
                // Physical SID-cycle timing is finalized only by the active
                // runtime/kernel where sampleRate and SID clock are known.
                entry.event.sid_cycle_stamp = 0ull;
                if (!pendingOut.push(entry.event)) {
                    totalOverflowTelemetry.fetch_add(1, std::memory_order_relaxed);
                }
                continue;
            }
            scratch[count].event          = entry.event;
            // Preserve the explicit priority selected at push time. The lane index
            // is expected to match, but carrying the field forward keeps the sort
            // contract self-describing and robust against future refactors.
            scratch[count].sourcePriority = entry.sourcePriority;
            scratch[count].sourceLocalSeq = entry.sourceLocalSequence;
            scratch[count].laneIndex      = static_cast<int>(li);
            ++count;
        }
    }

    if (count == 0) return 0;

    // Sort: sample_offset, cycle_offset, subphase, source priority, source-local seq.
    // ARPSID_RT_SORT_CLASSIFICATION: render-thread fixed scratch supplied by the
    // caller, strict total order, noexcept comparator, no allocation.
    std::sort(scratch, scratch + count, [](const SidMergeCandidate& a, const SidMergeCandidate& b) noexcept {
        // 1. sample_offset: unresolved sorts after all resolved
        const bool aResolved = (a.event.sample_offset != kSidUnresolvedSampleOffset);
        const bool bResolved = (b.event.sample_offset != kSidUnresolvedSampleOffset);
        if (aResolved != bResolved) return aResolved && !bResolved;
        if (aResolved && bResolved && a.event.sample_offset != b.event.sample_offset)
            return a.event.sample_offset < b.event.sample_offset;

        // 2. cycle_offset: unresolved (0xFFFF) sorts after resolved — treat symmetrically
        const bool aCycleRes = (a.event.cycle_offset != kSidUnresolvedCycleOffset);
        const bool bCycleRes = (b.event.cycle_offset != kSidUnresolvedCycleOffset);
        if (aCycleRes != bCycleRes) return aCycleRes && !bCycleRes;
        if (aCycleRes && bCycleRes && a.event.cycle_offset != b.event.cycle_offset)
            return a.event.cycle_offset < b.event.cycle_offset;

        // 3. subphase (0xFF = unresolved — also sorts after resolved)
        const bool aSubRes = (a.event.subphase != 0xFFu);
        const bool bSubRes = (b.event.subphase != 0xFFu);
        if (aSubRes != bSubRes) return aSubRes && !bSubRes;
        if (a.event.subphase != b.event.subphase)
            return a.event.subphase < b.event.subphase;

        // 4. source priority (lower value = higher priority)
        if (a.sourcePriority != b.sourcePriority)
            return a.sourcePriority < b.sourcePriority;

        // 5. source-local sequence
        return a.sourceLocalSeq < b.sourceLocalSeq;
        // 6. final arrival_order assigned after sort — no tiebreak needed here
    });

    // Assign final canonical arrival_order and push to pending queue.
    int emitted = 0;
    for (int i = 0; i < count; ++i) {
        scratch[i].event.arrival_order = arrivalCounter.fetch_add(1, std::memory_order_relaxed);
        // Timing-authority: do not stamp here. Merge order is
        // deterministic; physical SID-cycle timing belongs to runtime finalization.
        scratch[i].event.sid_cycle_stamp = 0ull;
        if (pendingOut.push(scratch[i].event)) {
            ++emitted;
        } else {
            totalOverflowTelemetry.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return emitted;
}


// Convenience: push a single event into the appropriate lane by priority.
inline bool sidIngressPushToLane(
    std::array<SidMergeEventLane, kMergeLaneCount>& lanes,
    const SidTimedEvent& ev,
    SidIngressSourcePriority priority) noexcept
{
    const size_t li = sidIngressPriorityToLaneIndex(priority);
    if (li >= kMergeLaneCount) return false;
    SidIngressEntry<SidTimedEvent> entry{};
    entry.event = ev;
    entry.sourceLocalSequence = lanes[li].nextLocalSeq();
    entry.sourcePriority = static_cast<uint8_t>(priority);
    return lanes[li].push(entry);
}

} // namespace ArpSID
