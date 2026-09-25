#pragma once

// HOST-NEAR CYCLE DISPATCHER
//
// Single, explicit authority for mapping a host audio block (frameCount
// samples) to SID-cycle-accurate event dispatch and render calls.
//
// Responsibilities:
// - Interleave sorted canonical events with render slices at
// sample and sub-sample (cycle / subphase) granularity.
// - Compute all cycle boundaries from (sampleIndex × cyclesPerSample),
// anchoring the SID cycle timeline to the host audio context.
// - Carry no mutable state; called once per host render block.
//
// This struct replaces the inline dispatch loop that previously lived
// inside SidRuntimeModel::processCanonicalBlockInto().

#include "sid_event_queue.h"
#include "sid_event_timing.h"
#include "sid_interval_renderable.h"

namespace ArpSID {

struct SidHostCycleDispatcher {

    /// Dispatch one host render block.
    ///
    /// @param events Sorted canonical event queue (produced by
    /// SidRuntimeModel::consumePendingEventsInto()).
    /// @param frameCount Host audio block size in samples.
    /// @param cyclesPerSample Pre-computed SID cycles per host sample
    /// (0 if backend does not support sub-sample spans).
    /// @param fractionalCapable True when the active backend can render
    /// sub-sample cycle and subphase spans.
    /// @param onEvent Called for each event: must apply canonical
    /// state AND forward to the physical engine.
    /// @param onSlice Render [offset, offset+frames) host samples.
    /// @param onSubSampleSpan Render SID cycles [cycleStart, cycleEnd) within
    /// host sample `sampleOffset`.
    /// @param onSubPhaseSpan Render subphases [subStart, subEnd) of
    /// cycle `cycleIndex` within host sample `sampleOffset`.
    template <class OnEvent,
              class OnSlice,
              class OnSubSampleSpan,
              class OnSubPhaseSpan>
    static void dispatchBlock(
            SidTimedEventQueue&  events,
            int                  frameCount,
            SidCycleClockState&   cycleClock,
            bool                 fractionalCapable,
            OnEvent&&            onEvent,
            OnSlice&&            onSlice,
            OnSubSampleSpan&&    onSubSampleSpan,
            OnSubPhaseSpan&&     onSubPhaseSpan) noexcept {

        int rendered = 0;
        int cei      = 0;

        // ----------------------------------------------------------------------
        // Fast path: all events have no timing info → materialise at t=0
        // so UI/CoreMIDI note-ons without host timestamps are audible now.
        // ----------------------------------------------------------------------
        bool allUnresolved = (events.count > 0);
        for (int i = 0; i < events.count; ++i) {
            if (events.events[i].sample_offset != kSidUnresolvedSampleOffset) {
                allUnresolved = false; break;
            }
        }
        if (allUnresolved && !fractionalCapable) {
            while (cei < events.count) { onEvent(events.events[cei]); ++cei; }
            if (frameCount > 0) onSlice(0, frameCount);
            return;
        }

        // ----------------------------------------------------------------------
        // Queue metadata is maintained on push/replacement so dispatch does
        // not rescan the whole block on the audio thread just to choose the
        // cycle-accurate path.
        // ----------------------------------------------------------------------
        const bool hasResolvedCycleTiming = events.hasAnyResolvedCycleTiming();

        // ----------------------------------------------------------------------
        // Cycle-accurate path: per-sample loop with optional sub-sample spans.
        // v887: the unresolved-event fast path is intentionally disabled for
        // fractional-capable backends. Otherwise an unresolved no-op/note-on would
        // render the whole block through onSlice() and only advance SidCycleClockState
        // afterward, while a resolved event would advance audio through the per-sample
        // physical clock law. Event metadata must never select the render clock law.
        // v886: fractional-capable backends always use this physical clock law,
        // even for unresolved-only or no-op metadata. Event metadata must decide
        // when writes are applied, not whether oscillator/envelope/filter phase is
        // advanced by a different render path.
        // ----------------------------------------------------------------------
        const bool usePhysicalCycleClockLaw = fractionalCapable || hasResolvedCycleTiming;
        if (usePhysicalCycleClockLaw) {
            int dispatchCount = events.count;
            if (fractionalCapable) {
                // v889: unresolved events are host-"now" events, but they must
                // not bypass normal sample-0 ordering. v888 pre-applied the
                // unresolved tail before every resolved sample-0 event; that made
                // event metadata override the normal priority/order rules (for
                // example an unresolved NoteOn could run before a resolved Panic at
                // the same host boundary). Instead, materialise unresolved events
                // as resolved sample-0/cycle-0 events, then re-sort the queue. The
                // physical clock law remains identical for unresolved-only, mixed,
                // resolved-cycle and no-event blocks, while sample-0 priority is
                // still governed by SidTimedEvent::before().
                bool materialisedUnresolved = false;
                for (int i = 0; i < events.count; ++i) {
                    auto& ev = events.events[i];
                    if (!ev.hasResolvedTiming()) {
                        ev.sample_offset = 0u;
                        // v890: host-now/unresolved events are sample-0 events,
                        // not physically cycle-stamped events. v889 stamped them
                        // as cycle=0/subphase=0, which made them sort ahead of
                        // already-resolved sample-0 events without cycle metadata
                        // before priority/order could decide. Keep them sample-only;
                        // dispatch still applies sample-only events at cycle 0, but
                        // SidTimedEvent::before() can preserve canonical sample-0
                        // priority/order against other sample-only events.
                        ev.cycle_offset = kSidUnresolvedCycleOffset;
                        ev.subphase = 0xFFu;
                        ev.sid_cycle_stamp = 0ull;
                        materialisedUnresolved = true;
                    }
                }
                if (materialisedUnresolved) {
                    for (int i = 1; i < events.count; ++i) {
                        SidTimedEvent key = events.events[i];
                        int j = i;
                        while (j > 0 && SidTimedEvent::before(key, events.events[j - 1])) {
                            events.events[j] = events.events[j - 1];
                            --j;
                        }
                        events.events[j] = key;
                    }
                }
                dispatchCount = events.count;
            } else if (allUnresolved) {
                // Non-fractional resolved-cycle path fallback: unresolved-only
                // events are still immediate.
                while (cei < events.count) { onEvent(events.events[cei]); ++cei; }
                dispatchCount = 0;
            }
            while (rendered < frameCount) {
                const uint16_t cyclesPerSample = fractionalCapable ? cycleClock.cyclesForNextHostSample() : 0u;
                const bool canRenderCycles = fractionalCapable && cyclesPerSample > 0u;
                // Dispatch events that fell before this sample.
                while (cei < dispatchCount
                       && events.events[cei].hasResolvedTiming()
                       && static_cast<int>(events.events[cei].sample_offset) < rendered) {
                    onEvent(events.events[cei]); ++cei;
                }

                uint16_t renderedCycle    = 0u;
                uint8_t  renderedSubphase = 0u;

                // Cycle estimates are advisory and can be one cycle high when
                // the fixed-point clock emits the shorter member of a floor/ceil
                // pair. Clamp every event in this sample to its actual budget
                // before grouping or rendering so no backend advances beyond it.
                int clampIndex = cei;
                while (clampIndex < dispatchCount
                       && events.events[clampIndex].hasResolvedTiming()
                       && static_cast<int>(events.events[clampIndex].sample_offset) == rendered) {
                    auto& ev = events.events[clampIndex];
                    if (ev.hasResolvedCycleTiming()) {
                        const uint16_t lastCycle = cyclesPerSample > 0u
                            ? static_cast<uint16_t>(cyclesPerSample - 1u)
                            : 0u;
                        if (ev.cycle_offset > lastCycle) {
                            ev.cycle_offset = lastCycle;
                            ev.sid_cycle_stamp = 0ull;
                        }
                    }
                    ++clampIndex;
                }
                for (int i = cei + 1; i < clampIndex; ++i) {
                    SidTimedEvent key = events.events[i];
                    int j = i;
                    while (j > cei && SidTimedEvent::before(key, events.events[j - 1])) {
                        events.events[j] = events.events[j - 1];
                        --j;
                    }
                    events.events[j] = key;
                }

                // Dispatch events landing exactly on this sample, interleaved
                // with sub-sample span renders at their cycle positions.
                while (cei < dispatchCount
                       && events.events[cei].hasResolvedTiming()
                       && static_cast<int>(events.events[cei].sample_offset) == rendered) {

                    const bool evHasCycle = events.events[cei].hasResolvedCycleTiming();
                    const uint16_t evCycle    = evHasCycle ? events.events[cei].cycle_offset : 0u;
                    // v891: sample-only events are sample-boundary events. Their
                    // stored subphase is 0xFF as an unresolved sentinel and must not
                    // cause rendering of subphase 0..255 before the event. Treat them
                    // as happening at the start of the sample for render interleaving;
                    // ordering against other sample-0 events is handled by
                    // SidTimedEvent::before().
                    const uint8_t  evSubphase = evHasCycle ? events.events[cei].subphase : 0u;

                    if (canRenderCycles) {
                        if (evCycle > renderedCycle) {
                            // Close any open subphase span before advancing cycles.
                            if (renderedSubphase > 0u) {
                                onSubPhaseSpan(rendered, renderedCycle, renderedSubphase, ArpSID::kSidSubcycleBoundary);
                                ++renderedCycle;
                                renderedSubphase = 0u;
                            }
                            if (evCycle > renderedCycle)
                                onSubSampleSpan(rendered, renderedCycle, evCycle);
                            renderedCycle = evCycle;
                        }
                        if (evCycle == renderedCycle && evSubphase > renderedSubphase) {
                            onSubPhaseSpan(rendered, evCycle, renderedSubphase, evSubphase);
                            renderedSubphase = evSubphase;
                        }
                    }

                    // Consume all events at exactly (sample, cycle, subphase).
                    while (cei < dispatchCount
                           && events.events[cei].hasResolvedTiming()
                           && static_cast<int>(events.events[cei].sample_offset) == rendered
                           && ((!events.events[cei].hasResolvedCycleTiming() && evCycle == 0u && evSubphase == 0u)
                               || (events.events[cei].hasResolvedCycleTiming()
                                   && events.events[cei].cycle_offset == evCycle
                                   && events.events[cei].subphase == evSubphase))) {
                        onEvent(events.events[cei]); ++cei;
                    }
                }

                // Close trailing partial spans for this sample.
                if (canRenderCycles) {
                    if (renderedSubphase > 0u) {
                        onSubPhaseSpan(rendered, renderedCycle, renderedSubphase, ArpSID::kSidSubcycleBoundary);
                        renderedSubphase = 0u;
                        ++renderedCycle;
                    }
                    if (cyclesPerSample > renderedCycle)
                        onSubSampleSpan(rendered, renderedCycle, cyclesPerSample);
                }

                // Materialise any remaining unresolved events at current position.
                // Unresolved events were already materialised at sample 0 for
                // fractional-capable physical-clock dispatch.

                onSlice(rendered, 1);
                ++rendered;
            }

            // Flush any resolved tail events that sort past the last rendered sample.
            // Unresolved tail events were already materialised at sample 0.
            while (cei < dispatchCount) { onEvent(events.events[cei]); ++cei; }
            return;
        }

        // ----------------------------------------------------------------------
        // Sample-boundary path: render slices between event boundaries.
        // ----------------------------------------------------------------------
        while (rendered < frameCount) {
            int  nextBoundary   = frameCount;
            bool nextIsResolved = (cei < events.count) && events.events[cei].hasResolvedTiming();
            if (nextIsResolved)
                nextBoundary = std::min(frameCount, static_cast<int>(events.events[cei].sample_offset));

            while (cei < events.count
                   && events.events[cei].hasResolvedTiming()
                   && static_cast<int>(events.events[cei].sample_offset) <= rendered) {
                onEvent(events.events[cei]); ++cei;
            }

            // Unresolved events after resolved boundaries: apply now so they
            // affect the tail of this block rather than being deferred.
            if (cei < events.count && !events.events[cei].hasResolvedTiming()) {
                while (cei < events.count) { onEvent(events.events[cei]); ++cei; }
                nextBoundary = rendered;
            }

            if (nextBoundary <= rendered) nextBoundary = frameCount;
            const int sliceFrames = nextBoundary - rendered;
            if (sliceFrames > 0) { onSlice(rendered, sliceFrames); rendered = nextBoundary; }
            else break;
        }

        // Flush tail events (hold sustained notes alive into the next block).
        while (cei < events.count) { onEvent(events.events[cei]); ++cei; }
        if (fractionalCapable) { for (int i = 0; i < frameCount; ++i) (void)cycleClock.cyclesForNextHostSample(); }
    }
};

} // namespace ArpSID
