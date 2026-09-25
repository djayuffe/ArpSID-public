#pragma once
#include <cstdint>

// COMPAT-ONLY: sid_voice_identity.h
// This file has been demoted to compat/import status as a compatibility/import surface.
// All types and helpers here are LEGACY. No main scheduler, voice, or performance
// hot path may depend on SidNoteIdentity::matches() or newestMatching* as primary truth.
//
// Remaining legitimate uses:
// - Legacy event import and pre-token state migration
// - Stale state recovery during restore/reseed
// //
// [compat-only] — DO NOT add new primary-truth code here.
// Canonical voice truth is now token-based: see sid_voice_token.h, sid_dynamic_state.h.

namespace ArpSID {

// [compat-only] Legacy note identity. Used only for:
// - mapping old events to the token registry during import
// - mod-matrix/UI consumers that still address voices by (note, channel, noteId)
struct SidNoteIdentity {
    int16_t midi_note = -1;
    int16_t channel = -1;
    int32_t note_id = -1;
    uint32_t note_on_order = 0;

    bool valid() const noexcept { return midi_note >= 0 && midi_note <= 127; }

    // [compat-only] Wildcard matching for readback/import only.
    // Primary runtime resolution is token lookup via SidDynamicState::findActiveVoiceByToken().
    bool matches(int midiNote, int ch, int nid) const noexcept {
        if (midi_note != midiNote) return false;
        if (ch >= 0) { if (channel != ch) return false; }
        if (nid >= 0 && note_id != nid) return false;
        return true;
    }
};

// [compat-only] Active voice identity entry for mod-matrix and UI consumers.
// Hot audio paths must not scan this table; use the token registry instead.
struct SidActiveVoiceIdentity {
    SidNoteIdentity identity{};
    float velocity = 0.0f;
    float poly_pressure = 0.0f;
    bool active = false;
    bool sustained = false;
    bool sostenuto_latched = false;
};

} // namespace ArpSID

