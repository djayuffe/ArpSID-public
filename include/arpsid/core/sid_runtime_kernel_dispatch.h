#pragma once
#include "sid_runtime_midi_ops.h"
#include "sid_event_queue.h"

namespace ArpSID {

template <class Target>
inline void runtimeKernelDispatchPitchBend(Target& target, const SidTimedEvent& ev) noexcept {
    target.runtimeDispatchPitchBendEvent(ev);
}

template <class Target>
inline void runtimeKernelDispatchPolyPressure(Target& target, const SidTimedEvent& ev) noexcept {
    target.runtimeDispatchPolyPressureEvent(ev);
}

template <class Target>
inline void runtimeKernelDispatchChannelPressure(Target& target, const SidTimedEvent& ev) noexcept {
    target.runtimeDispatchChannelPressureEvent(ev);
}

template <class Target>
inline void runtimeKernelDispatchMidiCC(Target& target, const SidTimedEvent& ev) noexcept {
    target.runtimeDispatchMidiCC(ev.channel, ev.ccNum, canonicalMidi7FromEventValue(ev));
}

} // namespace ArpSID
