// ─── ArpSIDCanonicalEvents.h ─────────────────────────────────────────────────
// AU event helpers layered directly on top of the shared canonical event queue.
// Shared core owns the event schema; this header only provides AU-side shaping
// helpers and conversions without introducing a second event authority.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "arpsid/core/sid_event_queue.h"
#include "arpsid/core/sid_event_timing.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ArpSID {

enum class EventKind : uint8_t {
    NoteOn          = static_cast<uint8_t>(SidTimedEventType::MidiNoteOn),
    NoteOff         = static_cast<uint8_t>(SidTimedEventType::MidiNoteOff),
    PolyPressure    = static_cast<uint8_t>(SidTimedEventType::PolyPressure),
    ControlChange   = static_cast<uint8_t>(SidTimedEventType::MidiCC),
    PitchBend       = static_cast<uint8_t>(SidTimedEventType::PitchBend),
    ChannelPressure = static_cast<uint8_t>(SidTimedEventType::ChannelPressure),
    ProgramChange   = static_cast<uint8_t>(SidTimedEventType::ProgramChange),
    ParameterSet    = static_cast<uint8_t>(SidTimedEventType::AutomationPoint),
    ParameterRamp   = 250,
    TransportChange = static_cast<uint8_t>(SidTimedEventType::TransportChange),
    AllNotesOff     = static_cast<uint8_t>(SidTimedEventType::AllNotesOff),
    AllSoundOff     = static_cast<uint8_t>(SidTimedEventType::AllSoundOff),
    Panic           = static_cast<uint8_t>(SidTimedEventType::Panic),
};

// v910 ingress provenance: async ring events (midiQueue_/paramIntentQueue_)
// mirror held-note ingress at enqueue time, so dispatch must not mirror them
// again; host-provided events[] mirror at dispatch time. The flag rides on the
// event so parent-scope chunk normalization can inject ring events into the
// host events[] timeline without double-counting held-note depth.
inline constexpr uint8_t kIngressProvenanceHostEvents = 0u;
inline constexpr uint8_t kIngressProvenanceAsyncRing  = 1u;

struct TimedEvent {
    int32_t  sampleOffset = -1;
    uint16_t cycleOffset  = kSidUnresolvedCycleOffset;
    uint8_t  subphase     = 0;
    EventKind kind        = EventKind::NoteOn;
    uint8_t  channel      = kSidUnresolvedChannel;
    int16_t  pitch        = 0;
    float    value        = 0.f;
    uint16_t data14       = 0;
    uint8_t  ccNum        = 0;
    int32_t  noteId       = -1;
    uint64_t voiceToken   = 0;
    uint32_t rawOrder     = 0;
    uint32_t target       = 0;
    uint32_t value_u32    = 0;
    float    value_f32    = 0.f;
    // v910: which ingress path produced this event (held-mirror authority).
    // Appended last so positional aggregate initializers stay valid.
    uint8_t  ingressProvenance = kIngressProvenanceHostEvents;

    bool sanitize(int frameCount) noexcept {
        if (sampleOffset >= 0)
            sampleOffset = std::clamp(sampleOffset, 0, std::max(0, frameCount - 1));
        else
            sampleOffset = -1;
        if (channel != kSidUnresolvedChannel)
            channel = std::min(channel, (uint8_t)15u);
        pitch = std::clamp((int)pitch, 0, 127);
        if (!std::isfinite(value)) value = 0.f;
        data14 = std::min(data14, (uint16_t)16383u);
        if (!std::isfinite(value_f32)) value_f32 = 0.f;
        switch (kind) {
            case EventKind::PitchBend:
                value = std::clamp(value, -1.f, 1.f);
                break;
            case EventKind::TransportChange:
                break;
            default:
                value = std::clamp(value, 0.f, 1.f);
                break;
        }
        return true;
    }

    SidTimedEvent toCanonical() const noexcept {
        SidTimedEvent ev{};
        ev.sample_offset = (sampleOffset < 0) ? kSidUnresolvedSampleOffset : static_cast<uint32_t>(sampleOffset);
        ev.cycle_offset = (cycleOffset == kSidUnresolvedCycleOffset) ? kSidUnresolvedCycleOffset : cycleOffset;
        ev.subphase = subphase;
        ev.arrival_order = rawOrder;
        ev.sid_cycle_stamp = 0ull;
        ev.type = (kind == EventKind::ParameterRamp) ? SidTimedEventType::AutomationPoint
                                                      : static_cast<SidTimedEventType>(kind);
        ev.channel = channel;
        ev.pitch = pitch;
        ev.value = (kind == EventKind::PitchBend)
                       ? std::clamp(static_cast<float>(static_cast<int>(data14) - 8192) / 8192.0f, -1.0f, 1.0f)
                       : value;
        ev.data14 = data14;
        ev.ccNum = ccNum;
        ev.noteId = noteId;
        ev.voiceToken = voiceToken;
        ev.target = target;
        ev.value_u32 = value_u32;
        ev.value_f32 = value_f32;
        return ev;
    }

    static bool before(const TimedEvent& a, const TimedEvent& b) noexcept {
        return SidTimedEvent::before(a.toCanonical(), b.toCanonical());
    }
};

struct TransportState {
    double   bpm          = 120.0;
    double   beatPosition = 0.0;
    bool     isPlaying    = false;
    bool     playStateKnown = false;
    bool     isLooping    = false;
    double   loopStart    = 0.0;
    double   loopEnd      = 0.0;
    double   sampleRate   = 44100.0;
    int      frameCount   = 0;

    void sanitize() noexcept {
        if (!std::isfinite(bpm) || bpm < 1.0 || bpm > 1000.0) bpm = 120.0;
        if (!std::isfinite(beatPosition) || beatPosition < 0.0) beatPosition = 0.0;
        if (!std::isfinite(sampleRate)   || sampleRate < 1.0)   sampleRate   = 44100.0;
        if (!std::isfinite(loopStart)    || loopStart < 0.0)    loopStart    = 0.0;
        if (!std::isfinite(loopEnd)      || loopEnd < loopStart) loopEnd = loopStart;
        frameCount = std::max(0, frameCount);
    }

    bool valid() const noexcept {
        return std::isfinite(bpm) && bpm > 0.0 && std::isfinite(sampleRate) && sampleRate > 0.0;
    }
};

static constexpr int kMaxTimedEvents = kMaxSidTimedEvents;
// audit #13: the canonical->EventBuffer conversion (fromCanonicalQueueInto) copies
// up to src.count (<= kMaxSidTimedEvents) entries into an EventBuffer of capacity
// kMaxTimedEvents. This static_assert proves the conversion buffer can never
// overflow, so no separate conversion-overflow ledger needs publishing.
static_assert(kMaxTimedEvents >= kMaxSidTimedEvents,
              "EventBuffer capacity must be >= SidTimedEventQueue capacity so canonical conversion cannot overflow");

struct EventOverflowTelemetry {
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
    void recordDrop(EventKind kind) noexcept {
        overflowed = true;
        ++droppedTotal;
        switch (kind) {
            case EventKind::NoteOn: ++droppedNoteOn; break;
            case EventKind::NoteOff: ++droppedNoteOff; break;
            case EventKind::ParameterSet:
            case EventKind::ParameterRamp: ++droppedAutomation; break;
            case EventKind::ControlChange:
            case EventKind::PitchBend:
            case EventKind::ChannelPressure:
            case EventKind::PolyPressure:
            case EventKind::ProgramChange: ++droppedController; break;
            case EventKind::TransportChange: ++droppedTransport; break;
            case EventKind::AllNotesOff:
            case EventKind::AllSoundOff:
            case EventKind::Panic: ++droppedPanic; break;
        }
    }
    void recordReplacement() noexcept { overflowed = true; ++replacedLowerPriority; }
};

struct EventBuffer {
    TimedEvent events[kMaxTimedEvents]{};
    int count = 0;
    EventOverflowTelemetry overflowTelemetry{};

    bool push(const TimedEvent& ev) noexcept {
        if (count >= kMaxTimedEvents) {
            int replaceIdx = -1;
            uint8_t worstPriority = 0u;
            uint32_t worstArrival = 0u;
            for (int i = 0; i < count; ++i) {
                const uint8_t pri = sidTimedEventPriority(events[i].toCanonical().type);
                if (replaceIdx < 0 || pri > worstPriority || (pri == worstPriority && events[i].rawOrder > worstArrival)) {
                    replaceIdx = i;
                    worstPriority = pri;
                    worstArrival = events[i].rawOrder;
                }
            }
            const uint8_t incomingPriority = sidTimedEventPriority(ev.toCanonical().type);
            if (replaceIdx >= 0 && incomingPriority < worstPriority) {
                overflowTelemetry.recordDrop(events[replaceIdx].kind);
                overflowTelemetry.recordReplacement();
                events[replaceIdx] = ev;
                return true;
            }
            overflowTelemetry.recordDrop(ev.kind);
            return false;
        }
        events[count++] = ev;
        return true;
    }

    void sort() noexcept {
        // ARPSID_RT_SORT_CLASSIFICATION: render-thread, fixed-capacity POD
        // storage, total-order comparator, libc++ introsort performs no allocation.
        std::sort(events, events + count, TimedEvent::before);
    }

    const EventOverflowTelemetry& overflow() const noexcept { return overflowTelemetry; }
    void reset() noexcept { count = 0; overflowTelemetry.reset(); }
    const TimedEvent* begin() const noexcept { return events; }
    const TimedEvent* end() const noexcept { return events + count; }
};



// Off-render helper only (allocates). audit P0.3: SidTimedEventQueue's default
// ctor no longer allocates, so storage must be requested explicitly here.
inline SidTimedEventQueue toCanonicalQueue(const EventBuffer& src, int frameCount) noexcept {
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    for (int i = 0; i < src.count; ++i) {
        TimedEvent ev = src.events[i];
        ev.sanitize(frameCount);
        q.push(ev.toCanonical());
    }
    q.sort();
    return q;
}

inline EventBuffer fromCanonicalQueue(const SidTimedEventQueue& src, int frameCount) noexcept {
    EventBuffer out{};
    for (int i = 0; i < src.count && i < kMaxTimedEvents; ++i) {
        const SidTimedEvent& in = src.events[i];
        TimedEvent ev{};
        ev.sampleOffset = (in.sample_offset == kSidUnresolvedSampleOffset)
                              ? -1
                              : static_cast<int32_t>(in.sample_offset);
        ev.cycleOffset = in.cycle_offset;
        ev.subphase = in.subphase;
        ev.kind = static_cast<EventKind>(in.type);
        ev.channel = (in.channel == kSidUnresolvedChannel) ? kSidUnresolvedChannel : in.channel;
        ev.pitch = in.pitch;
        ev.value = (in.type == SidTimedEventType::PitchBend)
                       ? std::clamp(static_cast<float>(static_cast<int>(in.data14) - 8192) / 8192.0f, -1.0f, 1.0f)
                       : in.value;
        ev.data14 = in.data14;
        ev.ccNum = in.ccNum;
        ev.noteId = in.noteId;
        ev.voiceToken = in.voiceToken;
        ev.rawOrder = in.arrival_order;
        ev.target = in.target;
        ev.value_u32 = in.value_u32;
        ev.value_f32 = in.value_f32;
        ev.sanitize(frameCount);
        out.push(ev);
    }
    out.sort();
    return out;
}

// In-place variant: writes into a caller-supplied EventBuffer to avoid
// a 655 KB stack allocation in processBlock.
//
// audit P1.8: this MUST stay behavior-identical to the by-value fromCanonicalQueue
// above — it sanitizes each event against frameCount and sorts the result. The
// previous version silently skipped both, so parity-trace / GUI-trace consumers
// could receive unsanitized or unsorted events if upstream ever stopped
// guaranteeing sorted/sanitized input. EventBuffer uses fixed POD storage and
// EventBuffer::sort() is allocation-free libc++ introsort, so this stays RT-safe.
inline void fromCanonicalQueueInto(const SidTimedEventQueue& src, int frameCount, EventBuffer& out) noexcept {
    out.count = 0;
    for (int i = 0; i < src.count; ++i) {
        const SidTimedEvent& in = src.events[i];
        TimedEvent ev{};
        ev.sampleOffset = (in.sample_offset == kSidUnresolvedSampleOffset)
                              ? -1
                              : static_cast<int32_t>(in.sample_offset);
        ev.cycleOffset = in.cycle_offset;
        ev.subphase = in.subphase;
        ev.kind = static_cast<EventKind>(in.type);
        ev.channel = (in.channel == kSidUnresolvedChannel) ? kSidUnresolvedChannel : in.channel;
        ev.pitch = in.pitch;
        ev.value = (in.type == SidTimedEventType::PitchBend)
                       ? std::clamp(static_cast<float>(static_cast<int>(in.data14) - 8192) / 8192.0f, -1.0f, 1.0f)
                       : in.value;
        ev.data14 = in.data14;
        ev.ccNum = in.ccNum;
        ev.noteId = in.noteId;
        ev.voiceToken = in.voiceToken;
        ev.rawOrder = in.arrival_order;
        ev.target = in.target;
        ev.value_u32 = in.value_u32;
        ev.value_f32 = in.value_f32;
        ev.sanitize(frameCount);
        out.push(ev);
    }
    out.sort();
}

} // namespace ArpSID
