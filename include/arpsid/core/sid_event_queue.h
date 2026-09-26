// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <memory>
#include "sid_variant_profile.h"
#include "sid_runtime_sizing.h"
#include "sid_realtime_guard.h"
#include <type_traits>

namespace ArpSID {

static constexpr uint32_t kSidUnresolvedSampleOffset = 0xFFFFFFFFu;
static constexpr uint16_t kSidUnresolvedCycleOffset = 0xFFFFu;
static constexpr uint8_t kSidUnresolvedChannel = 0xFFu;
// Internal render-generated arp gates share the canonical event timeline but
// must not be reinterpreted as new host-held MIDI identities. MIDI events do
// not otherwise use value_u32, so the high bit is a stable internal marker.
static constexpr uint32_t kSidTimedEventInternalArpGeneratedFlag = 0x80000000u;

enum class SidTimedEventType : uint8_t {
    MidiNoteOn = 0,
    MidiNoteOff,
    MidiCC,
    PitchBend,
    ChannelPressure,
    PolyPressure,
    AutomationPoint,
    SidRegisterWrite,
    TransportChange,
    TempoChange,
    VariantChange,
    ProgramChange,
    AllNotesOff,
    AllSoundOff,
    Panic
};

static inline bool sidTimedEventIsSampleBoundaryKill(SidTimedEventType type) noexcept {
    return type == SidTimedEventType::Panic ||
           type == SidTimedEventType::AllSoundOff ||
           type == SidTimedEventType::AllNotesOff;
}

// Release/control events must remain admissible during dense NoteOn/controller
// bursts. The queue reserves bounded headroom for these events instead of
// allowing lower-value traffic to consume the entire fixed RT capacity.
static inline bool sidTimedEventIsReleaseCritical(SidTimedEventType type) noexcept {
    return type == SidTimedEventType::MidiNoteOff ||
           type == SidTimedEventType::AllNotesOff ||
           type == SidTimedEventType::AllSoundOff ||
           type == SidTimedEventType::Panic ||
           type == SidTimedEventType::TransportChange;
}

static inline uint8_t sidTimedEventPriority(SidTimedEventType type) noexcept {
    switch (type) {
        case SidTimedEventType::Panic:           return 0;
        case SidTimedEventType::AllSoundOff:     return 1;
        case SidTimedEventType::AllNotesOff:     return 2;
        case SidTimedEventType::MidiNoteOff:     return 3;
        case SidTimedEventType::SidRegisterWrite:return 4;
        case SidTimedEventType::VariantChange:   return 5;
        case SidTimedEventType::ProgramChange:   return 6;
        case SidTimedEventType::TransportChange: return 7;
        case SidTimedEventType::TempoChange:     return 8;
        case SidTimedEventType::AutomationPoint: return 9;
        case SidTimedEventType::MidiNoteOn:      return 10;
        case SidTimedEventType::MidiCC:          return 11;
        case SidTimedEventType::PolyPressure:    return 12;
        case SidTimedEventType::ChannelPressure: return 13;
        case SidTimedEventType::PitchBend:       return 14;
        default:                                 return 15;
    }
}

struct SidTimedEvent {
    uint32_t sample_offset = kSidUnresolvedSampleOffset;
    uint64_t sid_cycle_stamp = 0;
    uint16_t cycle_offset = kSidUnresolvedCycleOffset;
    uint8_t subphase = 0xFFu;
    uint32_t arrival_order = 0;

    SidTimedEventType type = SidTimedEventType::MidiNoteOn;
    uint8_t channel = kSidUnresolvedChannel;
    int16_t pitch = 0;
    float value = 0.0f;
    uint16_t data14 = 0;
    uint8_t ccNum = 0;
    int32_t noteId = -1;
    uint64_t voiceToken = 0;
    uint32_t target = 0;
    uint32_t value_u32 = 0;
    float value_f32 = 0.0f;

    bool hasResolvedTiming() const noexcept {
        return sample_offset != kSidUnresolvedSampleOffset;
    }
    bool hasResolvedCycleTiming() const noexcept {
        return sample_offset != kSidUnresolvedSampleOffset && cycle_offset != kSidUnresolvedCycleOffset;
    }

    bool sanitize(int frameCount) noexcept {
        const int maxFrame = std::max(0, frameCount - 1);
        const bool resolvedSample = (sample_offset != kSidUnresolvedSampleOffset);
        const bool resolvedCycle = (cycle_offset != kSidUnresolvedCycleOffset);
        const bool resolvedTiming = resolvedSample && resolvedCycle;

        if (resolvedSample)
            sample_offset = (uint32_t)std::clamp<int>((int)sample_offset, 0, maxFrame);
        else
            sample_offset = kSidUnresolvedSampleOffset;

        if (!resolvedCycle)
            cycle_offset = kSidUnresolvedCycleOffset;

        // Authority: sanitization is not allowed to invent physical
        // SID-cycle stamps. It only clamps host/sample/cycle ordering fields.
        // The runtime render core stamps events exactly once with the active
        // sample rate and variant clock.
        (void)resolvedTiming;
        sid_cycle_stamp = 0ull;

        if (channel != kSidUnresolvedChannel) channel = std::min<uint8_t>(channel, 15u);
        switch (type) {
            case SidTimedEventType::MidiNoteOn:
            case SidTimedEventType::MidiNoteOff:
            case SidTimedEventType::PolyPressure:
                pitch = (int16_t)std::clamp<int>((int)pitch, 0, 127);
                break;
            default:
                break;
        }
        if (!std::isfinite(value)) value = 0.0f;
        data14 = std::min<uint16_t>(data14, 16383u);
        if (!std::isfinite(value_f32)) value_f32 = 0.0f;

        switch (type) {
            case SidTimedEventType::PitchBend:
                value = std::clamp(value, -1.0f, 1.0f);
                break;
            case SidTimedEventType::TransportChange:
            case SidTimedEventType::TempoChange:
                break;
            default:
                value = std::clamp(value, 0.0f, 1.0f);
                break;
        }
        return true;
    }

    static bool before(const SidTimedEvent& a, const SidTimedEvent& b) noexcept {
        const bool aResolved = a.hasResolvedTiming();
        const bool bResolved = b.hasResolvedTiming();
        const bool aCycle = a.hasResolvedCycleTiming();
        const bool bCycle = b.hasResolvedCycleTiming();
        if (aResolved != bResolved) return aResolved && !bResolved;
        if (aResolved) {
            if (a.sample_offset != b.sample_offset) return a.sample_offset < b.sample_offset;
            // Same-sample policy (v891): sample-boundary emergency controls
            // (Panic/AllSoundOff/AllNotesOff) are host-edge destructive actions and
            // must preempt cycle-stamped writes in that same sample. Ordinary
            // sample-only events remain weaker than physically cycle-stamped writes.
            const bool aBoundaryKill = !aCycle && sidTimedEventIsSampleBoundaryKill(a.type);
            const bool bBoundaryKill = !bCycle && sidTimedEventIsSampleBoundaryKill(b.type);
            if (aBoundaryKill != bBoundaryKill) return aBoundaryKill && !bBoundaryKill;
            if (aCycle != bCycle) return aCycle && !bCycle;
            if (aCycle) {
                if (a.cycle_offset != b.cycle_offset) return a.cycle_offset < b.cycle_offset;
            }
        }
        // v892: subphase is physical ordering metadata only when both events are
        // cycle-stamped. Sample-only events use subphase=0xFF as an unresolved
        // sentinel (or may carry legacy/advisory subphase values) and must not let
        // that advisory value outrank canonical priority/order. In particular, a
        // sample-only Panic/AllSoundOff/AllNotesOff must remain before any lower
        // priority same-sample sample-only event regardless of its subphase.
        if (aCycle && bCycle && a.subphase != b.subphase) return a.subphase < b.subphase;
        // Canonical source merge assigns the final total-order token. Do not
        // overwrite that authority with a second event-type ordering law here.
        // Emergency sample-boundary kills and physical cycle/subphase ordering
        // have already been handled above. Event priority is therefore only a
        // deterministic last resort for malformed/legacy events that reuse an
        // arrival token.
        if (a.arrival_order != b.arrival_order) return a.arrival_order < b.arrival_order;
        const uint8_t ap = sidTimedEventPriority(a.type);
        const uint8_t bp = sidTimedEventPriority(b.type);
        return ap < bp;
    }
};

static constexpr int kMaxSidTimedEvents = kSidRuntimeTimedEventCapacity;

static inline bool sidTimedEventIsInternalArpGenerated(const SidTimedEvent& ev) noexcept {
    return (ev.type == SidTimedEventType::MidiNoteOn || ev.type == SidTimedEventType::MidiNoteOff) &&
           (ev.value_u32 & kSidTimedEventInternalArpGeneratedFlag) != 0u;
}

struct SidTimedEventOverflowTelemetry {
    // audit #3: 64-bit so long-session drop/overflow diagnostics never wrap.
    uint64_t droppedTotal = 0;
    uint64_t replacedLowerPriority = 0;
    uint64_t droppedNoteOn = 0;
    uint64_t droppedNoteOff = 0;
    uint64_t droppedAutomation = 0;
    uint64_t droppedController = 0;
    uint64_t droppedTransport = 0;
    uint64_t droppedPanic = 0;
    bool overflowed = false;

    void reset() noexcept { *this = {}; }
    void recordDrop(SidTimedEventType type) noexcept {
        overflowed = true;
        ++droppedTotal;
        switch (type) {
            case SidTimedEventType::MidiNoteOn: ++droppedNoteOn; break;
            case SidTimedEventType::MidiNoteOff: ++droppedNoteOff; break;
            case SidTimedEventType::AutomationPoint:
            case SidTimedEventType::SidRegisterWrite:
            case SidTimedEventType::VariantChange: ++droppedAutomation; break;
            case SidTimedEventType::MidiCC:
            case SidTimedEventType::PitchBend:
            case SidTimedEventType::ChannelPressure:
            case SidTimedEventType::PolyPressure:
            case SidTimedEventType::ProgramChange: ++droppedController; break;
            case SidTimedEventType::TransportChange:
            case SidTimedEventType::TempoChange: ++droppedTransport; break;
            case SidTimedEventType::AllNotesOff:
            case SidTimedEventType::AllSoundOff:
            case SidTimedEventType::Panic: ++droppedPanic; break;
        }
    }
    void recordReplacement() noexcept { overflowed = true; ++replacedLowerPriority; }
};

struct SidTimedEventQueue {
    // 1/16th of the fixed queue is held back for release/stop traffic. With the
    // compact default this is 256 events, comfortably above the bounded active
    // voice-token population plus sustain releases and emergency controls, without
    // allocating on the RT thread.
    static constexpr int kReleaseReserve = std::max(32, kMaxSidTimedEvents / 16);

    std::unique_ptr<SidTimedEvent[]> events;
    int count = 0;
    uint32_t dropped = 0;
    bool overflowed = false;
    SidTimedEventOverflowTelemetry overflowTelemetry{};
    bool hasResolvedCycleTimingFlag = false;

    // audit P0.3: the default constructor is now NON-ALLOCATING and safe to run
    // on any thread, including (accidentally) the render thread. Backing storage
    // is acquired explicitly and off the render thread via the AllocateStorage
    // tag constructor or ensureStorageNonRealtimeOnly(). A queue without storage drops every
    // push() (counted), so owners that are actually written to MUST be given
    // storage during setup. The dangerous by-value wrappers that used to default-
    // construct (and therefore allocate) a temporary on render-reachable paths
    // have been removed — only the *Into / *Direct variants remain.
    struct AllocateStorage {};

    SidTimedEventQueue() noexcept = default;

    explicit SidTimedEventQueue(AllocateStorage) {
        ArpSID::sidRealtimeGuardForbidAllocation("SidTimedEventQueue(AllocateStorage) allocated backing storage");
        events = std::make_unique<SidTimedEvent[]>(kMaxSidTimedEvents);
    }

    // Acquire backing storage if not already present. Off-render setup only.
    // Named ...NonRealtimeOnly to make the allocation contract impossible to miss
    // at call sites: it may allocate, so it must never run on the render thread.
    bool ensureStorageNonRealtimeOnly() {
        if (!events) {
            ArpSID::sidRealtimeGuardForbidAllocation("SidTimedEventQueue::ensureStorageNonRealtimeOnly allocated backing storage");
            events = std::make_unique<SidTimedEvent[]>(kMaxSidTimedEvents);
        }
        return static_cast<bool>(events);
    }

    bool hasStorage() const noexcept { return static_cast<bool>(events); }

private:
    bool hasStorage_() const noexcept { return static_cast<bool>(events); }

public:
    // True move: steal the backing store instead of allocating/copying on every block.
    SidTimedEventQueue(SidTimedEventQueue&& o) noexcept
        : events(std::move(o.events)), count(o.count), dropped(o.dropped), overflowed(o.overflowed), overflowTelemetry(o.overflowTelemetry), hasResolvedCycleTimingFlag(o.hasResolvedCycleTimingFlag) {
        o.count = 0;
        o.dropped = 0;
        o.overflowed = false;
        o.overflowTelemetry.reset();
        o.hasResolvedCycleTimingFlag = false;
    }
    SidTimedEventQueue& operator=(SidTimedEventQueue&& o) noexcept {
        if (this != &o) {
            events = std::move(o.events);
            count = o.count;
            dropped = o.dropped;
            overflowed = o.overflowed;
            overflowTelemetry = o.overflowTelemetry;
            hasResolvedCycleTimingFlag = o.hasResolvedCycleTimingFlag;
            o.count = 0;
            o.dropped = 0;
            o.overflowed = false;
            o.overflowTelemetry.reset();
            o.hasResolvedCycleTimingFlag = false;
        }
        return *this;
    }
    // audit #6: copy operations are DELETED. They allocated backing storage, which
    // is a hidden real-time hazard if a copy ever lands on the render path. Use
    // move (steals storage, no alloc) for ownership transfer, or cloneNonRealtime()
    // for an explicit off-render deep copy.
    SidTimedEventQueue(const SidTimedEventQueue&) = delete;
    SidTimedEventQueue& operator=(const SidTimedEventQueue&) = delete;

    // Explicit, clearly off-render deep copy (allocates). Never call from render.
    SidTimedEventQueue cloneNonRealtime() const {
        SidTimedEventQueue q{AllocateStorage{}};
        q.count = count;
        q.dropped = dropped;
        q.overflowed = overflowed;
        q.overflowTelemetry = overflowTelemetry;
        q.hasResolvedCycleTimingFlag = hasResolvedCycleTimingFlag;
        if (events && q.events && count > 0)
            for (size_t i = 0; i < static_cast<size_t>(count); ++i) q.events[i] = events[i];
        return q;
    }

    bool push(const SidTimedEvent& ev) noexcept {
        // Phase-0 RT law: push() is render-callable and must never allocate.
        if (!hasStorage_()) {
            overflowed = true;
            ++dropped;
            overflowTelemetry.recordDrop(ev.type);
            return false;
        }
        const bool releaseCritical = sidTimedEventIsReleaseCritical(ev.type);
        const int admissionLimit = releaseCritical ? kMaxSidTimedEvents
                                                   : (kMaxSidTimedEvents - kReleaseReserve);
        if (count >= admissionLimit) {
            overflowed = true;
            overflowTelemetry.overflowed = true;
            int replaceIdx = -1;
            uint8_t worstPriority = 0u;
            uint32_t worstArrival = 0u;
            for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
                const uint8_t pri = sidTimedEventPriority(events[i].type);
                if (replaceIdx < 0 || pri > worstPriority || (pri == worstPriority && events[i].arrival_order > worstArrival)) {
                    replaceIdx = static_cast<int>(i);
                    worstPriority = pri;
                    worstArrival = events[i].arrival_order;
                }
            }
            const uint8_t incomingPriority = sidTimedEventPriority(ev.type);
            if (replaceIdx >= 0 && incomingPriority < worstPriority) {
                overflowTelemetry.recordDrop(events[static_cast<size_t>(replaceIdx)].type);
                overflowTelemetry.recordReplacement();
                events[static_cast<size_t>(replaceIdx)] = ev;
                recomputeResolvedCycleTimingFlag_();
                return true;
            }
            ++dropped;
            overflowTelemetry.recordDrop(ev.type);
            return false;
        }
        events[static_cast<size_t>(count++)] = ev;
        if (ev.hasResolvedCycleTiming()) hasResolvedCycleTimingFlag = true;
        return true;
    }

    void recomputeResolvedCycleTimingFlag_() noexcept {
        hasResolvedCycleTimingFlag = false;
        if (!events) return;
        for (int i = 0; i < count; ++i) {
            if (events[static_cast<size_t>(i)].hasResolvedCycleTiming()) {
                hasResolvedCycleTimingFlag = true;
                return;
            }
        }
    }

    void sort() noexcept {
        if (!events || count <= 1) return;
        // Audit #10 fix: replace the O(n²) bounded insertion sort with
        // std::sort. SidTimedEvent::before() already implements a strict
        // weak ordering with arrival_order as the final tiebreaker, and
        // arrival_order is unique per push, so the comparator is a strict
        // total order — std::sort yields the same result as a stable sort.
        //
        // On libc++ (the macOS toolchain we target) std::sort is implemented
        // as introsort: in-place, allocation-free, O(n log n) worst case
        // with a guaranteed heapsort fallback. std::stable_sort would
        // potentially allocate a temporary buffer, which is forbidden on
        // render-adjacent paths — that is why this swap keeps the strict
        // total-order comparator instead of dropping back to stable_sort.
        // ARPSID_RT_SORT_CLASSIFICATION: render-thread fixed preallocated event
        // storage, total-order noexcept comparator, no allocation.
        std::sort(events.get(), events.get() + count,
                  [](const SidTimedEvent& a, const SidTimedEvent& b) noexcept {
                      return SidTimedEvent::before(a, b);
                  });
    }

    bool hasAnyResolvedCycleTiming() const noexcept { return hasResolvedCycleTimingFlag; }
    bool consumeOverflowed() noexcept { const bool v = overflowed; overflowed = false; return v; }
    const SidTimedEventOverflowTelemetry& overflow() const noexcept { return overflowTelemetry; }
    void reset() noexcept { count = 0; dropped = 0; overflowed = false; overflowTelemetry.reset(); hasResolvedCycleTimingFlag = false; }
    void swap(SidTimedEventQueue& o) noexcept {
        events.swap(o.events);
        std::swap(count, o.count);
        std::swap(dropped, o.dropped);
        std::swap(overflowed, o.overflowed);
        std::swap(overflowTelemetry, o.overflowTelemetry);
        std::swap(hasResolvedCycleTimingFlag, o.hasResolvedCycleTimingFlag);
    }
    const SidTimedEvent* begin() const noexcept { return events.get(); }
    const SidTimedEvent* end() const noexcept { return events.get() + count; }
};

inline SidVariantProfile canonicalVariantProfileFromEvent(const SidTimedEvent& ev, const SidVariantProfile& fallback) noexcept {
    return canonicalVariantProfileFromPackedBits(ev.value_u32, fallback);
}

} // namespace ArpSID

static_assert(sizeof(ArpSID::SidTimedEvent) <= 64, "SidTimedEvent unexpectedly large; keep queues compact");
