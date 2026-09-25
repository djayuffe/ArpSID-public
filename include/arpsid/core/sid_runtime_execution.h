#pragma once
#include "sid_runtime_model.h"

namespace ArpSID {


template <class Sink>
inline void dispatchCanonicalTimedEvent(SidRuntimeModel& runtime, const SidTimedEvent& ev, Sink& sink) noexcept(noexcept(sink.onTransportChange(ev)) && noexcept(sink.onAutomationPoint(ev)) && noexcept(sink.onPanic(ev)) && noexcept(sink.onAllSoundOff(ev)) && noexcept(sink.onAllNotesOff(ev)) && noexcept(sink.onMidiNoteOn(ev)) && noexcept(sink.onMidiNoteOff(ev)) && noexcept(sink.onPitchBend(ev)) && noexcept(sink.onPolyPressure(ev)) && noexcept(sink.onChannelPressure(ev)) && noexcept(sink.onMidiCC(ev)) && noexcept(sink.onVariantChange(ev)) && noexcept(sink.onProgramChange(ev)) && noexcept(sink.onTempoChange(ev))) {
    switch (ev.type) {
        case SidTimedEventType::TransportChange:
            sink.onTransportChange(ev);
            return;
        case SidTimedEventType::AutomationPoint:
            sink.onAutomationPoint(ev);
            return;
        case SidTimedEventType::Panic:
            sink.onPanic(ev);
            return;
        case SidTimedEventType::AllSoundOff:
            sink.onAllSoundOff(ev);
            return;
        case SidTimedEventType::AllNotesOff:
            sink.onAllNotesOff(ev);
            return;
        case SidTimedEventType::MidiNoteOn:
            sink.onMidiNoteOn(ev);
            return;
        case SidTimedEventType::MidiNoteOff:
            sink.onMidiNoteOff(ev);
            return;
        case SidTimedEventType::PitchBend:
            sink.onPitchBend(ev);
            return;
        case SidTimedEventType::PolyPressure:
            sink.onPolyPressure(ev);
            return;
        case SidTimedEventType::ChannelPressure:
            sink.onChannelPressure(ev);
            return;
        case SidTimedEventType::MidiCC:
            sink.onMidiCC(ev);
            return;
        case SidTimedEventType::SidRegisterWrite:
            return;
        case SidTimedEventType::VariantChange:
            // SidRuntimeModel::processCanonicalBlockInto() has already applied
            // the event to canonical state before dispatch. The sink owns only
            // concrete-engine projection; applying the runtime mutation here a
            // second time caused duplicate state/root side effects.
            sink.onVariantChange(ev);
            return;
        case SidTimedEventType::ProgramChange:
            sink.onProgramChange(ev);
            return;
        case SidTimedEventType::TempoChange:
            sink.onTempoChange(ev);
            return;
        default:
            return;
    }
}

template <class Sink>
inline void dispatchCanonicalTimedQueue(SidRuntimeModel& runtime, const SidTimedEventQueue& q, Sink& sink) noexcept(noexcept(dispatchCanonicalTimedEvent(runtime, q.events[0], sink))) {
    for (int i = 0; i < q.count; ++i)
        dispatchCanonicalTimedEvent(runtime, q.events[i], sink);
}



template <class Sink>
inline void consumeAndDispatchCanonicalPendingEventsInto(SidRuntimeModel& runtime,
                                                         int frameCount,
                                                         Sink& sink,
                                                         SidTimedEventQueue& out) noexcept(noexcept(dispatchCanonicalTimedEvent(runtime, std::declval<const SidTimedEvent&>(), sink))) {
    runtime.consumePendingEventsInto(frameCount, out);
    dispatchCanonicalTimedQueue(runtime, out, sink);
}


template <class Sink, class SliceFn>
inline void consumeDispatchAndRenderCanonicalBlockInto(SidRuntimeModel& runtime,
                                                       int frameCount,
                                                       Sink& sink,
                                                       SliceFn&& renderSlice,
                                                       SidTimedEventQueue& out) noexcept(noexcept(dispatchCanonicalTimedEvent(runtime, std::declval<const SidTimedEvent&>(), sink)) && noexcept(renderSlice(0, 0))) {
    runtime.processCanonicalBlockInto(frameCount,
        [&](const SidTimedEvent& ev) noexcept(noexcept(dispatchCanonicalTimedEvent(runtime, ev, sink))) {
            dispatchCanonicalTimedEvent(runtime, ev, sink);
        },
        std::forward<SliceFn>(renderSlice),
        out);
}
// audit P0.3: the by-value consumeAndDispatchCanonicalPendingEvents() and
// consumeDispatchAndRenderCanonicalBlock() wrappers were render-path allocation
// traps (they default-constructed, and therefore allocated, a temporary
// SidTimedEventQueue). Removed — use the *Into variants above with caller-owned
// storage on any render-reachable path.

} // namespace ArpSID
