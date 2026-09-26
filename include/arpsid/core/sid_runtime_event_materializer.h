// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_event_queue.h"
#include "sid_ingress_lane.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

// CANONICAL EVENT MATERIALIZATION TRUTH (Pass N).
// CC interpretation tables and host-input-to-canonical-event policy live here.
// Both AU and VST wrappers must use these functions — they must not implement
// their own CC tables or materialisation logic. This ensures AU/VST parity.
//
// Any wrapper that previously owned CC interpretation now calls into here.

namespace ArpSID {

// Classify an event into its canonical source priority band for lane routing.
inline SidIngressSourcePriority sidCanonicalSourcePriority(const SidTimedEvent& ev) noexcept {
    switch (ev.type) {
        case SidTimedEventType::Panic:
        case SidTimedEventType::AllSoundOff:
        case SidTimedEventType::AllNotesOff:
            return SidIngressSourcePriority::SystemSafety;
        case SidTimedEventType::TempoChange:
        case SidTimedEventType::TransportChange:
            return SidIngressSourcePriority::TransportTempo;
        case SidTimedEventType::MidiNoteOn:
        case SidTimedEventType::MidiNoteOff:
        case SidTimedEventType::MidiCC:
        case SidTimedEventType::PitchBend:
        case SidTimedEventType::PolyPressure:
        case SidTimedEventType::ChannelPressure:
        case SidTimedEventType::ProgramChange:
            return SidIngressSourcePriority::MidiNoteControl;
        case SidTimedEventType::AutomationPoint:
        case SidTimedEventType::VariantChange:
            return SidIngressSourcePriority::AutomationRefinement;
        default:
            return SidIngressSourcePriority::DerivedFollowup;
    }
}

// Materialise a note-on event with canonical fields.
inline SidTimedEvent sidMaterializeNoteOn(int channel, int note, float velocity,
                                           int noteId, int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type         = SidTimedEventType::MidiNoteOn;
    ev.channel      = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.pitch        = static_cast<int16_t>(std::clamp(note, 0, 127));
    ev.value        = std::clamp(std::isfinite(velocity) ? velocity : 0.f, 0.f, 1.f);
    ev.value_f32    = ev.value;
    ev.noteId       = noteId;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a note-off event with canonical fields.
inline SidTimedEvent sidMaterializeNoteOff(int channel, int note, int noteId,
                                            int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type         = SidTimedEventType::MidiNoteOff;
    ev.channel      = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.pitch        = static_cast<int16_t>(std::clamp(note, 0, 127));
    ev.value        = 0.f; ev.value_f32 = 0.f;
    ev.noteId       = noteId;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a CC event with canonical fields.
inline SidTimedEvent sidMaterializeCC(int channel, uint8_t ccNum, float normValue,
                                       int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type         = SidTimedEventType::MidiCC;
    ev.channel      = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.ccNum        = ccNum;
    ev.value        = std::clamp(std::isfinite(normValue) ? normValue : 0.f, 0.f, 1.f);
    ev.value_f32    = ev.value;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a pitch-bend event.
inline SidTimedEvent sidMaterializePitchBend(int channel, int raw14, int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type      = SidTimedEventType::PitchBend;
    ev.channel   = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.data14    = static_cast<uint16_t>(std::clamp(raw14, 0, 16383));
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a poly-pressure event.
inline SidTimedEvent sidMaterializePolyPressure(int channel, int note, float pressure,
                                                 int noteId, int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type      = SidTimedEventType::PolyPressure;
    ev.channel   = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.pitch     = static_cast<int16_t>(std::clamp(note, 0, 127));
    ev.value     = std::clamp(std::isfinite(pressure) ? pressure : 0.f, 0.f, 1.f);
    ev.value_f32 = ev.value;
    ev.noteId    = noteId;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a channel-pressure event.
inline SidTimedEvent sidMaterializeChannelPressure(int channel, float pressure,
                                                    int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type      = SidTimedEventType::ChannelPressure;
    ev.channel   = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.value     = std::clamp(std::isfinite(pressure) ? pressure : 0.f, 0.f, 1.f);
    ev.value_f32 = ev.value;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a panic event.
inline SidTimedEvent sidMaterializePanic() noexcept {
    SidTimedEvent ev{};
    ev.type          = SidTimedEventType::Panic;
    ev.sample_offset = kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a tempo change event.
inline SidTimedEvent sidMaterializeTempoChange(float bpm, int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type      = SidTimedEventType::TempoChange;
    ev.value_f32 = std::clamp(std::isfinite(bpm) ? bpm : 120.f, 0.f, 400.f);
    ev.value     = ev.value_f32;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Materialise a transport-change event.
inline SidTimedEvent sidMaterializeTransportChange(bool playing, int sampleOffset) noexcept {
    SidTimedEvent ev{};
    ev.type      = SidTimedEventType::TransportChange;
    ev.value     = playing ? 1.f : 0.f;
    ev.value_f32 = ev.value;
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint16_t>(sampleOffset)
        : kSidUnresolvedSampleOffset;
    return ev;
}

// Push a pre-materialised event to a model's lane with correct priority.
// [canonical] Wrappers call this instead of writing directly to pending queue.
template <class RuntimeModel>
inline bool sidWrapperPushEvent(RuntimeModel& model, const SidTimedEvent& ev,
                                 int frameCount) noexcept {
    return model.pushToLane(ev, sidCanonicalSourcePriority(ev), frameCount);
}

} // namespace ArpSID
