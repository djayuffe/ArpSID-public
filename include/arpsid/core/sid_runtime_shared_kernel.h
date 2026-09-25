#pragma once
#include "sid_runtime_model.h"
#include "sid_runtime_primitive_surface.h"
#include <algorithm>
#include <cmath>

namespace ArpSID {


inline uint8_t canonicalProgramChangeFromEvent(const SidTimedEvent& ev) noexcept {
    if ((ev.value_u32 & 0x7Fu) != 0u) return static_cast<uint8_t>(ev.value_u32 & 0x7Fu);
    return static_cast<uint8_t>(ev.pitch & 0x7F);
}

inline void runtimeKernelOnTransportChange(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    const bool nowPlaying = (ev.value > 0.5f);
    const bool wasPlayingBeforeDispatch = runtime.transportPlayingFlag();
    // Canonical dispatch may be reached through two paths:
    // 1. processCanonicalBlockInto(), which calls applyCanonicalEventToState(ev)
    // before this dispatcher. In that path a Stop event has already written
    // transportPlaying=false, so a wasPlaying guard here would be false and
    // the concrete engine all-notes-off would be skipped.
    // 2. direct dispatch helpers, where this function is the first state writer.
    // Therefore Stop must be an idempotent concrete-engine cleanup command, not
    // conditional on the model flag still showing the old state. This closes the
    // transport-stop stuck-note path while preserving DrSID kit state.
    runtime.setTransportPlayingFlag(nowPlaying);
    surface.setTransportPlaying(nowPlaying);
    if (!nowPlaying) {
        // Logic/AUv2 transport boundaries must not destructively reset DrSID kits.
        // DrSID is an event-driven drum instrument; authored kit/mode state must
        // survive Stop->Play and AudioUnitReset metadata storms.
        if (!runtime.isDrSidModeEnabled()) surface.allNotesOff();
        return;
    }
    // Rewind ONLY on the stopped->playing edge or explicit reposition. In the
    // pre-applied canonical path wasPlayingBeforeDispatch may already be true for
    // a Play event; that is fine because rewind is not a safety gate-off path.
    if (!wasPlayingBeforeDispatch && !runtime.isDrSidModeEnabled()) {
        surface.rewindTransport();
    }
}

inline void runtimeKernelOnAutomationPoint(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    (void)runtime;
    surface.applyNormalizedParameter(ev.target, ev.value);
}

inline void runtimeKernelOnPanic(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    (void)runtime; (void)ev;
    surface.panic();
}

inline void runtimeKernelOnAllSoundOff(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    // Model-level channel scoping is done in applyCanonicalEventToState().
    // Use channel-scoped allNotesOffChannel() so voices on other MIDI channels
    // are not incorrectly silenced. The default implementation falls back to
    // the global allNotesOff() for surfaces that do not override it.
    (void)runtime;
    surface.allNotesOffChannel(ev.channel);
}

inline void runtimeKernelOnAllNotesOff(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    // Model-level channel scoping is done in applyCanonicalEventToState().
    // FIX: Use channel-scoped allNotesOffChannel() instead of the global
    // allNotesOff(), which was silencing voices on all channels even when
    // only one channel sent the AllNotesOff message.
    (void)runtime;
    surface.allNotesOffChannel(ev.channel);
}

inline void runtimeKernelOnMidiNoteOn(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    if (sidTimedEventIsInternalArpGenerated(ev)) { surface.bitPerfectNoteOn(ev); return; }
    surface.observeNoteActivity(static_cast<uint8_t>(ev.pitch & 0x7F), ev.value);
    if (runtime.isDrSidModeEnabled()) { surface.triggerDrumMidi(static_cast<uint8_t>(ev.pitch & 0x7F), ev.value); return; }
    if (runtime.isSynthModeEnabled()) { surface.synthNoteOn(ev); return; }
    if (runtime.isArpEnabled()) { surface.arpNoteOn(static_cast<uint8_t>(ev.pitch & 0x7F), ev.value); return; }
    const uint64_t voiceToken = runtime.resolveVoiceTokenForIdentity((int)ev.channel, (int)(ev.pitch & 0x7F), ev.noteId);
    surface.bitPerfectNoteOnWithToken(ev, voiceToken);
}

inline void runtimeKernelOnMidiNoteOff(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    if (sidTimedEventIsInternalArpGenerated(ev)) { surface.bitPerfectNoteOff(ev); return; }
    (void)runtime;
    if (runtime.isDrSidModeEnabled()) {
        surface.releaseDrumMidi(static_cast<uint8_t>(ev.pitch & 0x7F));
        return;
    }
    if (runtime.isSynthModeEnabled()) {
        surface.synthNoteOff(ev);
        return;
    }
    if (runtime.isArpEnabled()) {
        surface.arpNoteOff(static_cast<uint8_t>(ev.pitch & 0x7F));
        if (ev.noteId >= 0) {
            // Modern-host safety: if arp was enabled while a direct BitPerfect
            // note with this concrete host identity was already sounding, route
            // the same NoteOff to BitPerfect as well. Anonymous arp voices use
            // noteId=-1, so this cannot release legacy/anonymous arp voices.
            surface.bitPerfectNoteOff(ev);
        }
        return;
    }
    surface.bitPerfectNoteOff(ev);
}

inline void runtimeKernelOnPitchBend(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    (void)runtime;
    surface.applyPitchBend(ev);
}

inline void runtimeKernelOnPolyPressure(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    // Token-first: update canonical token poly-pressure before legacy surface.
    if (ev.channel < 16 && ev.pitch < 128) {
        const float pressure = std::clamp(ev.value, 0.0f, 1.0f);
        const int16_t ch   = static_cast<int16_t>(ev.channel);
        const int16_t note = static_cast<int16_t>(ev.pitch);
        // Try exact noteId first, then newest token for this (ch, note).
        uint64_t tok = runtime.resolveVoiceTokenForIdentity(ch, note, ev.noteId);
        if (tok != 0) runtime.bindPolyPressureToToken(tok, pressure);
    }
    surface.applyPolyPressure(ev);
}

inline void runtimeKernelOnChannelPressure(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    (void)runtime;
    surface.applyChannelPressure(ev);
}

inline void runtimeKernelOnMidiCC(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    (void)runtime;
    surface.applyMidiCC(ev);
}

inline void runtimeKernelOnVariantChange(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    const SidVariantProfile profile = canonicalVariantProfileFromEvent(ev, runtime.variantProfile());
    surface.applyVariantProfile(profile);
}

inline void runtimeKernelOnProgramChange(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    (void)runtime; (void)surface; (void)ev;
    // MIDI/host ProgramChange is GM/channel metadata only. ArpSID patch selection
    // is controlled exclusively by kParamBankSlot and AU/VST preset APIs.
}

inline void runtimeKernelOnTempoChange(SidRuntimeModel& runtime, SidRuntimePrimitiveSurface& surface, const SidTimedEvent& ev) noexcept {
    if (!std::isfinite(ev.value_f32) || ev.value_f32 <= 0.0f) return;
    const float bpm = std::clamp(ev.value_f32, 1.0f, 400.0f);
    // Update canonical dynamic state (primary truth for all tempo consumers).
    runtime.setHostTempoBpm(bpm);
    // Propagate to primitive surface (arp, sequencer, LFO sync).
    surface.setHostTempo(bpm);
}

} // namespace ArpSID
