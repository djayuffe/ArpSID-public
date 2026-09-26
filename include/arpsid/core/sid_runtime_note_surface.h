// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "arpsid/core/sid_event_queue.h"

namespace ArpSID {

template <class Target>
inline void runtimeHandleRenderedNoteOn(Target& t, uint8_t ch, uint8_t note, uint8_t vel, int noteId = -1) noexcept {
    t.runtimeObserveDefaultEventChannel(static_cast<int>(ch));
    const float v = static_cast<float>(vel) / 127.0f;
    t.runtimeSetLastNoteVelocity(v);
    t.runtimeStoreTelemetryLastNote(static_cast<int>(note));
    auto& runtime = t.runtimeModel();
    runtime.setLastNoteState(static_cast<int>(note), static_cast<int>(ch), std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f));
    runtime.noteOnCanonical(static_cast<int>(note), static_cast<int>(ch), noteId, runtime.lastNoteVelocity());
    const uint64_t voiceToken = runtime.resolveVoiceTokenForIdentity(static_cast<int>(ch), static_cast<int>(note), noteId);
    if (t.runtimeIsDrSidEnabled() && t.runtimeHasDrSidEngine()) {
        const float safeVel = std::clamp(std::isfinite(v) ? v : 0.5f, 0.0f, 1.0f);
        t.runtimeTriggerDrSidNote(static_cast<int>(note), safeVel);
        return;
    }
    if (t.runtimeIsSynthModeEnabled()) {
        // v928: SynthMode/SID-register is a render-mode authority, not a
        // BitPerfect/ARP decoration. Internal/virtual AU3 notes must schedule
        // SID-register writes even when ArpEnable is stale/on; otherwise the
        // playable synth can disappear behind the BitPerfect/ARP authority.
        SidTimedEvent ev{};
        ev.type = SidTimedEventType::MidiNoteOn;
        ev.channel = static_cast<uint8_t>(ch & 0x0Fu);
        ev.pitch = static_cast<int16_t>(note & 0x7Fu);
        ev.value = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
        ev.sample_offset = 0u;
        ev.cycle_offset = 0u;
        ev.noteId = noteId;
        ev.voiceToken = voiceToken;
        t.kernelSynthNoteOn(ev);
        return;
    }
    if (t.runtimeIsArpEnabled() && t.runtimeHasArpEngine()) {
        t.runtimeArpNoteOn(static_cast<int>(note), v);
        return;
    }
    if (!t.runtimeHasBitPerfectEngine()) return;
    t.runtimeBitPerfectNoteOn(static_cast<int>(note), v, static_cast<int>(ch), noteId, voiceToken);
}

template <class Target>
inline void runtimeHandleRenderedNoteOff(Target& t, uint8_t ch, uint8_t note, int noteId = -1) noexcept {
    t.runtimeObserveDefaultEventChannel(static_cast<int>(ch));
    auto& runtime = t.runtimeModel();

    // Engine authorities are notified before canonical token retirement. This
    // keeps BitPerfect/DrSID/ARP physical gate state synchronized with the
    // canonical decision, and prevents the token registry from forgetting the
    // identity before the concrete engine has had a chance to release it.
    if (t.runtimeIsDrSidEnabled() && t.runtimeHasDrSidEngine()) {
        t.runtimeReleaseDrSidNote(static_cast<int>(note));
        runtime.noteOffCanonical(static_cast<int>(note), static_cast<int>(ch), noteId);
        runtime.refreshLastNoteFromTokenState(std::clamp<int>(ch, 0, 15));
        return;
    }
    // DrSID release above is intentionally before this legacy guard marker:
    // if (!t.runtimeHasBitPerfectEngine()) return;

    if (t.runtimeIsSynthModeEnabled()) {
        // v928: release the same SID-register authority that accepted NoteOn,
        // even if ArpEnable is stale/on. Arp remains a BitPerfect/legacy mode
        // authority until it emits SID-register events explicitly.
        SidTimedEvent ev{};
        ev.type = SidTimedEventType::MidiNoteOff;
        ev.channel = static_cast<uint8_t>(ch & 0x0Fu);
        ev.pitch = static_cast<int16_t>(note & 0x7Fu);
        ev.value = 0.0f;
        ev.sample_offset = 0u;
        ev.cycle_offset = 0u;
        ev.noteId = noteId;
        ev.voiceToken = runtime.resolveVoiceTokenForIdentity(static_cast<int>(ch), static_cast<int>(note), noteId);
        t.kernelSynthNoteOff(ev);
    } else if (t.runtimeIsArpEnabled() && t.runtimeHasArpEngine()) {
        t.runtimeArpNoteOff(static_cast<int>(note));
        if (noteId >= 0 && t.runtimeHasBitPerfectEngine()) {
            // Modern hosts provide a concrete note identity. During arp OFF -> ON
            // handoff there may still be a pre-arp direct BitPerfect voice with that
            // identity. Clear it as a safety net. Anonymous arp voices use noteId=-1,
            // so this cannot release the arp voice path or legacy MIDI-1.0 anonymous
            // notes.
            t.runtimeBitPerfectNoteOff(static_cast<int>(note), static_cast<int>(ch), noteId);
        }
    } else if (t.runtimeHasBitPerfectEngine()) {
        t.runtimeBitPerfectNoteOff(static_cast<int>(note), static_cast<int>(ch), noteId);
    }

    runtime.noteOffCanonical(static_cast<int>(note), static_cast<int>(ch), noteId);
    runtime.refreshLastNoteFromTokenState(std::clamp<int>(ch, 0, 15));
}

} // namespace ArpSID
