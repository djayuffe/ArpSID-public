// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_event_queue.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/arpeggiator.h"
#include "arpsid/engines/sid_register_engine.h"
#include <algorithm>

namespace ArpSID {

inline DrSidEngine::DrumType canonicalMidiNoteToDrumType(int note) noexcept {
    return DrSidEngine::drumTypeForMidiNote(note);
}


inline void canonicalRenderDrSid(DrSidEngine* eng, float* left, float* right, int n) noexcept {
    if (!eng) return;
    float* buf2[2] = { left, right };
    eng->processBlock(buf2, n);
}

inline void canonicalRenderBitPerfect(BitPerfectEngine* eng, float* left, float* right, int n) noexcept {
    if (!eng) return;
    float* buf2[2] = { left, right };
    eng->processBlock(buf2, n);
}


inline void canonicalDispatchArpTimedEvent(Arpeggiator* arp, BitPerfectEngine* bpe,
                                           const Arpeggiator::TimedArpEvent& ae) noexcept {
    if (!arp || !bpe) return;
    const bool chordMode = arp->isChordMode();
    if (ae.note.gate) {
        if (chordMode) {
            static constexpr int kMaxChord = 32;
            int chordNotes[kMaxChord]; float chordVels[kMaxChord];
            const int nc = arp->getChordNotesForStep(ae.stepIndexBefore, chordNotes, chordVels, kMaxChord, true);
            if (nc > 0) arp->rememberEmittedChordNotes(chordNotes, chordVels, nc);
            for (int ci = 0; ci < nc; ++ci) bpe->noteOn(chordNotes[ci], chordVels[ci], 0, -1);
        } else if (ae.note.midiNote >= 0) {
            // A non-chord gate-on invalidates any older chord cache. Without
            // this, a later mode switch back to chord could replay stale cached
            // notes from a previous randomized chord.
            arp->clearLastEmittedChordNotes();
            bpe->noteOn(ae.note.midiNote, ae.note.velocity, 0, -1);
        }
        return;
    }

    // Gate-off event: release the noted MIDI note, or the exact chord step that emitted it.
    if (ae.note.midiNote < 0) return;
    if (chordMode) {
        static constexpr int kMaxChord = 32;
        int chordNotes[kMaxChord]; float chordVels[kMaxChord];
        int nc = arp->getLastEmittedChordNotes(chordNotes, chordVels, kMaxChord);
        if (nc <= 0) nc = arp->getChordNotesForStep(ae.stepIndexBefore, chordNotes, chordVels, kMaxChord, false);
        for (int ci = 0; ci < nc; ++ci) bpe->noteOff(chordNotes[ci], 0, -1);
        arp->clearLastEmittedChordNotes();
    } else {
        bpe->noteOff(ae.note.midiNote, 0, -1);
        arp->clearLastEmittedChordNotes();
    }
}


inline bool canonicalArpTimedEventLess_(const Arpeggiator::TimedArpEvent& a,
                                        const Arpeggiator::TimedArpEvent& b) noexcept {
    if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
    // At the same sample, release the previous gate before opening the next one.
    // This avoids a one-sample overlap/double-trigger when deferred/pending events
    // collapse onto offset 0 or a host buffer boundary.
    if (a.note.gate != b.note.gate) return !a.note.gate && b.note.gate;
    return a.stepIndexBefore < b.stepIndexBefore;
}

inline void canonicalSortArpTimedEvents_(Arpeggiator::TimedArpEvent* evts, int nEvts, int blockFrames) noexcept {
    if (!evts || nEvts <= 1) {
        if (evts && nEvts == 1) evts[0].sampleOffset = std::clamp(evts[0].sampleOffset, 0, std::max(0, blockFrames - 1));
        return;
    }
    const int maxOffset = std::max(0, blockFrames - 1);
    for (int i = 0; i < nEvts; ++i) evts[i].sampleOffset = std::clamp(evts[i].sampleOffset, 0, maxOffset);
    // Small fixed-size RT-safe insertion sort; no heap, deterministic order.
    for (int i = 1; i < nEvts; ++i) {
        auto key = evts[i];
        int j = i - 1;
        while (j >= 0 && canonicalArpTimedEventLess_(key, evts[j])) {
            evts[j + 1] = evts[j];
            --j;
        }
        evts[j + 1] = key;
    }
}

inline int canonicalAppendArpTimedEventsSegment(Arpeggiator* arp,
                                                SidTimedEventQueue& queue,
                                                int segmentFrames,
                                                int baseSampleOffset,
                                                int parentBlockFrames,
                                                uint32_t& nextArrival) noexcept {
    if (!arp || segmentFrames <= 0 || parentBlockFrames <= 0) return 0;
    static constexpr int kMaxArpEvt = 64;
    Arpeggiator::TimedArpEvent evts[kMaxArpEvt];
    const int nEvts = arp->collectTimedEvents(segmentFrames, evts, kMaxArpEvt);
    canonicalSortArpTimedEvents_(evts, nEvts, segmentFrames);

    int appended = 0;
    auto appendNote = [&](bool gate, int note, float velocity) noexcept {
        SidTimedEvent ev{};
        ev.type = gate ? SidTimedEventType::MidiNoteOn : SidTimedEventType::MidiNoteOff;
        ev.sample_offset = 0u; // overwritten by caller below
        ev.cycle_offset = kSidUnresolvedCycleOffset;
        ev.subphase = 0xFFu;
        ev.arrival_order = nextArrival;
        if (nextArrival != UINT32_MAX) ++nextArrival;
        ev.channel = 0u;
        ev.pitch = static_cast<int16_t>(std::clamp(note, 0, 127));
        ev.value = gate ? std::clamp(velocity, 0.0f, 1.0f) : 0.0f;
        ev.noteId = -1;
        ev.value_u32 = kSidTimedEventInternalArpGeneratedFlag;
        return ev;
    };

    const bool chordMode = arp->isChordMode();
    for (int ei = 0; ei < nEvts; ++ei) {
        const auto& ae = evts[ei];
        const uint32_t offset = static_cast<uint32_t>(std::clamp(
            baseSampleOffset + ae.sampleOffset, 0, std::max(0, parentBlockFrames - 1)));
        if (ae.note.gate) {
            if (chordMode) {
                static constexpr int kMaxChord = 32;
                int notes[kMaxChord]; float velocities[kMaxChord];
                const int count = arp->getChordNotesForStep(
                    ae.stepIndexBefore, notes, velocities, kMaxChord, true);
                if (count > 0) arp->rememberEmittedChordNotes(notes, velocities, count);
                for (int i = 0; i < count; ++i) {
                    SidTimedEvent ev = appendNote(true, notes[i], velocities[i]);
                    ev.sample_offset = offset;
                    if (queue.push(ev)) ++appended;
                }
            } else if (ae.note.midiNote >= 0) {
                arp->clearLastEmittedChordNotes();
                SidTimedEvent ev = appendNote(true, ae.note.midiNote, ae.note.velocity);
                ev.sample_offset = offset;
                if (queue.push(ev)) ++appended;
            }
            continue;
        }

        if (ae.note.midiNote < 0) continue;
        if (chordMode) {
            static constexpr int kMaxChord = 32;
            int notes[kMaxChord]; float velocities[kMaxChord];
            int count = arp->getLastEmittedChordNotes(notes, velocities, kMaxChord);
            if (count <= 0)
                count = arp->getChordNotesForStep(
                    ae.stepIndexBefore, notes, velocities, kMaxChord, false);
            for (int i = 0; i < count; ++i) {
                SidTimedEvent ev = appendNote(false, notes[i], 0.0f);
                ev.sample_offset = offset;
                if (queue.push(ev)) ++appended;
            }
            arp->clearLastEmittedChordNotes();
        } else {
            SidTimedEvent ev = appendNote(false, ae.note.midiNote, 0.0f);
            ev.sample_offset = offset;
            if (queue.push(ev)) ++appended;
            arp->clearLastEmittedChordNotes();
        }
    }
    return appended;
}

inline int canonicalAppendArpTimedEvents(Arpeggiator* arp,
                                         SidTimedEventQueue& queue,
                                         int blockFrames) noexcept {
    uint32_t nextArrival = 1u;
    for (int i = 0; i < queue.count; ++i) {
        const uint32_t arrival = queue.events[(size_t)i].arrival_order;
        if (arrival != UINT32_MAX) nextArrival = std::max(nextArrival, arrival + 1u);
    }
    return canonicalAppendArpTimedEventsSegment(arp, queue, blockFrames, 0,
                                                blockFrames, nextArrival);
}

inline void canonicalRenderBitPerfectWithArp(Arpeggiator* arp, BitPerfectEngine* bpe,
                                             float* left, float* right, int n) noexcept {
    if (!bpe) return;
    if (!left || !right || n <= 0) return;
    if (!arp) {
        canonicalRenderBitPerfect(bpe, left, right, n);
        return;
    }

    static constexpr int kMaxArpEvt = 64;
    Arpeggiator::TimedArpEvent evts[kMaxArpEvt];
    const int nEvts = arp->collectTimedEvents(n, evts, kMaxArpEvt);
    canonicalSortArpTimedEvents_(evts, nEvts, n);

    int cursor = 0;
    for (int e = 0; e < nEvts; ++e) {
        const int eventOffset = std::clamp(evts[e].sampleOffset, 0, std::max(0, n - 1));
        if (eventOffset > cursor) {
            canonicalRenderBitPerfect(bpe, left + cursor, right + cursor, eventOffset - cursor);
            cursor = eventOffset;
        }
        canonicalDispatchArpTimedEvent(arp, bpe, evts[e]);
    }
    if (cursor < n) canonicalRenderBitPerfect(bpe, left + cursor, right + cursor, n - cursor);
}

inline void canonicalEmitArpTimedEvents(Arpeggiator* arp, BitPerfectEngine* bpe, int n) noexcept {
    if (!arp || !bpe) return;
    static constexpr int kMaxArpEvt = 64;
    Arpeggiator::TimedArpEvent evts[kMaxArpEvt];
    const int nEvts = arp->collectTimedEvents(n, evts, kMaxArpEvt);
    canonicalSortArpTimedEvents_(evts, nEvts, n);
    for (int e = 0; e < nEvts; ++e) canonicalDispatchArpTimedEvent(arp, bpe, evts[e]);
}

inline void canonicalTriggerDrumMidi(DrSidEngine* eng, uint8_t note, float velocity) noexcept {
    if (eng) eng->triggerMidiNote((int)note, velocity);
}
inline void canonicalArpNoteOn(Arpeggiator* arp, uint8_t note, float velocity) noexcept {
    if (arp) arp->noteOn((int)note, velocity);
}
inline void canonicalArpNoteOff(Arpeggiator* arp, uint8_t note) noexcept {
    if (arp) arp->noteOff((int)note);
}
inline void canonicalBitPerfectNoteOn(BitPerfectEngine* eng, const SidTimedEvent& ev) noexcept {
    if (eng) eng->noteOn(ev.pitch, ev.value, ev.channel, ev.noteId);
}
inline void canonicalBitPerfectNoteOff(BitPerfectEngine* eng, const SidTimedEvent& ev) noexcept {
    if (eng) eng->noteOff(ev.pitch, ev.channel, ev.noteId);
}

} // namespace ArpSID
