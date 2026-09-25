#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "sid_voice_identity.h"
#include "sid_voice_token.h"
#include <array>

namespace ArpSID {

// --------------------------------------------------------------------------// Token-indexed active voice registry (canonical truth surface).
// All voice lifecycle paths MUST resolve by token first.
// (channel, note, noteId) scan paths are COMPAT-ONLY emergency fallback.
// --------------------------------------------------------------------------
static constexpr size_t kMaxTokenVoices = 64;
static constexpr size_t kCompatActiveIdentityCapacity = 16;
static constexpr uint64_t kIdentityTokenNamespace = (1ull << 62);

struct SidTokenVoiceEntry {
    SidVoiceToken    token{};
    float            velocity = 0.0f;
    float            polyPressure = 0.0f;
    bool             sustained = false;
    bool             sostenutoLatched = false;
};

struct SidTokenPolyPressureEntry {
    uint64_t token = 0;
    float    pressure = 0.0f;
};


struct SidDynamicState {
    static uint8_t encodePolyPressure(float v) noexcept {
        const float clamped = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::lround(clamped * 255.0f));
    }
    static float decodePolyPressure(uint8_t v) noexcept {
        return static_cast<float>(v) * (1.0f / 255.0f);
    }
    // ----------------------------------------------------------------------
    // Token-indexed voice registry (CANONICAL TRUTH)
    // ----------------------------------------------------------------------
    std::array<SidTokenVoiceEntry, kMaxTokenVoices> tokenVoices{};
    uint64_t focusedVoiceToken = 0;  // canonical focus; token 0 = none
    uint64_t nextTokenSerial_ = 1;   // local serial; namespace bit added at issuance
    static constexpr uint64_t kIdentityTokenSerialMask = ~(kIdentityTokenNamespace);
    uint32_t nextSyntheticNoteIdSerial_ = 1u;

    static bool isSyntheticAnonymousNoteId_(int32_t noteId) noexcept {
        // Synthetic anonymous IDs are allocated from the negative int32 range
        // (high bit set). Real VST3/AU host note IDs are non-negative. This
        // lets noteId-less NoteOff pair FIFO only with noteId-less NoteOn and
        // never steal a real host-identified voice of the same pitch/channel.
        return noteId < 0 && noteId != -1;
    }

    int32_t allocateSyntheticNoteId_() noexcept {
        for (;;) {
            const uint32_t raw = nextSyntheticNoteIdSerial_++;
            const int32_t candidate = static_cast<int32_t>(0x80000000u | (raw & 0x7fffffffu));
            if (candidate != -1) return candidate;
        }
    }

    // Per-channel/note poly-pressure atomic-safe last-seen value (COMPAT cache)
    // Canonical poly-pressure truth is in tokenVoices[].polyPressure.
    // This array is only read as a fallback when no active token exists.

    // Token registry operations -----------------------------------------
    SidTokenVoiceEntry* findActiveVoiceByToken(uint64_t tok) noexcept {
        if (tok == 0) return nullptr;
        for (auto& e : tokenVoices) {
            if (e.token.active && e.token.token == tok) return &e;
        }
        return nullptr;
    }

    SidTokenVoiceEntry* newestActiveTokenEntry() noexcept {
        SidTokenVoiceEntry* best = nullptr;
        uint32_t bestOrder = 0;
        for (auto& e : tokenVoices) {
            if (!e.token.active) continue;
            if (!best || e.token.arrivalOrder >= bestOrder) { best = &e; bestOrder = e.token.arrivalOrder; }
        }
        return best;
    }

    const SidTokenVoiceEntry* newestActiveTokenEntry() const noexcept {
        const SidTokenVoiceEntry* best = nullptr;
        uint32_t bestOrder = 0;
        for (const auto& e : tokenVoices) {
            if (!e.token.active) continue;
            if (!best || e.token.arrivalOrder >= bestOrder) { best = &e; bestOrder = e.token.arrivalOrder; }
        }
        return best;
    }

    SidTokenVoiceEntry* focusedTokenEntry() noexcept { return findActiveVoiceByToken(focusedVoiceToken); }
    const SidTokenVoiceEntry* focusedTokenEntry() const noexcept { return findActiveVoiceByToken(focusedVoiceToken); }

    const SidTokenVoiceEntry* findActiveVoiceByToken(uint64_t tok) const noexcept {
        if (tok == 0) return nullptr;
        for (const auto& e : tokenVoices) {
            if (e.token.active && e.token.token == tok) return &e;
        }
        return nullptr;
    }

    // Bind a new canonical token for a note-on. Returns the token assigned,
    // or 0 if the token table is full.
    //
    // Absent-noteId guard: when noteId < 0 (anonymous MIDI, raw CoreMIDI,
    // non-VST3 hosts), a synthetic noteId is generated from the token serial
    // to guarantee unique identity. This closes the gap where two simultaneous
    // same-note anonymous presses would collapse to one entry.
    uint64_t bindVoiceToken(int16_t ch, int16_t note, int32_t noteId,
                            float velocity, uint32_t arrivalOrd,
                            int32_t* effectiveNoteIdOut = nullptr) noexcept {
        // Generate synthetic noteId for absent-noteId paths.
        // Synthetic IDs use the high bit set (0x80000000) to distinguish them
        // from real host-supplied IDs which are always positive.
        const int32_t effectiveNoteId = (noteId >= 0) ? noteId : allocateSyntheticNoteId_();
        if (effectiveNoteIdOut) *effectiveNoteIdOut = effectiveNoteId;

        // First: try to find an exact identity match to rebind (retrigger).
        // For synthetic noteIds, retrigger is never triggered (each press is unique).
        if (noteId >= 0) {
            for (auto& e : tokenVoices) {
                if (!e.token.active) continue;
                if (e.token.channel != ch || e.token.note != note) continue;
                if (e.token.noteId != noteId) continue;
                // Retrigger in-place.
                e.velocity = std::clamp(velocity, 0.0f, 1.0f);
                e.token.arrivalOrder = arrivalOrd;
                focusedVoiceToken = e.token.token;
                return e.token.token;
            }
        }

        // Find a free slot.
        for (auto& e : tokenVoices) {
            if (e.token.active) continue;
            const uint64_t tok = kIdentityTokenNamespace | (nextTokenSerial_++ & kIdentityTokenSerialMask);
            if ((nextTokenSerial_ & kIdentityTokenSerialMask) == 0) {
                // Full uint64_t wrap: any surviving token with value 1..N could now
                // collide with a freshly issued token. Clear all voices to guarantee
                // no stale tokens persist across the wrap boundary.
                clearAllTokenVoices();
                nextTokenSerial_ = 1;
            }
            e = {};
            e.token.token        = tok;
            e.token.channel      = ch;
            e.token.note         = note;
            e.token.noteId       = effectiveNoteId;
            e.token.arrivalOrder = arrivalOrd;
            e.token.active       = true;
            e.velocity           = std::clamp(velocity, 0.0f, 1.0f);
            focusedVoiceToken    = tok;
            return tok;
        }
        return 0; // table full
    }

    uint64_t bindVoiceTokenExplicit(int16_t ch,
                                    int16_t note,
                                    int32_t noteId,
                                    float velocity,
                                    uint32_t arrivalOrd,
                                    uint64_t token,
                                    int32_t* effectiveNoteIdOut = nullptr) noexcept {
        if (token == 0) return bindVoiceToken(ch, note, noteId, velocity, arrivalOrd, effectiveNoteIdOut);
        if (findActiveVoiceByToken(token) != nullptr) return 0;
        const int32_t effectiveNoteId = (noteId >= 0) ? noteId : allocateSyntheticNoteId_();
        if (effectiveNoteIdOut) *effectiveNoteIdOut = effectiveNoteId;

        if (noteId >= 0) {
            for (auto& e : tokenVoices) {
                if (!e.token.active) continue;
                if (e.token.channel == ch && e.token.note == note && e.token.noteId == noteId) {
                    e = {};
                    break;
                }
            }
        }

        for (auto& e : tokenVoices) {
            if (e.token.active) continue;
            e = {};
            e.token.token = token;
            e.token.channel = ch;
            e.token.note = note;
            e.token.noteId = effectiveNoteId;
            e.token.arrivalOrder = arrivalOrd;
            e.token.active = true;
            e.velocity = std::clamp(velocity, 0.0f, 1.0f);
            focusedVoiceToken = token;
            if ((token & kIdentityTokenNamespace) != 0u) {
                const uint64_t serial = token & kIdentityTokenSerialMask;
                if (serial >= nextTokenSerial_) {
                    nextTokenSerial_ = (serial + 1u) & kIdentityTokenSerialMask;
                    if (nextTokenSerial_ == 0u) nextTokenSerial_ = 1u;
                }
            }
            return token;
        }
        return 0;
    }

    void unbindVoiceToken(uint64_t tok) noexcept {
        if (tok == 0) return;
        for (auto& e : tokenVoices) {
            if (e.token.active && e.token.token == tok) {
                e = {};
                break;
            }
        }
        if (focusedVoiceToken == tok) recomputeFocusedVoiceToken_();
    }

    void bindPolyPressureToToken(uint64_t tok, float pressure) noexcept {
        auto* e = findActiveVoiceByToken(tok);
        if (e) e->polyPressure = std::clamp(std::isfinite(pressure) ? pressure : 0.0f, 0.0f, 1.0f);
    }

    void clearPolyPressureByToken(uint64_t tok) noexcept {
        auto* e = findActiveVoiceByToken(tok);
        if (e) e->polyPressure = 0.0f;
    }

    // Returns poly pressure for the focused token.
    // If a token is set but its entry has been stolen/released, the canonical
    // voice is gone: return 0.0f instead of falling back to stale note-grid data.
    float focusedTokenPolyPressure() const noexcept {
        if (focusedVoiceToken == 0) return 0.0f; // no focused token: no pressure, never stale sentinel
        const auto* e = findActiveVoiceByToken(focusedVoiceToken);
        if (e) return std::clamp(e->polyPressure, 0.0f, 1.0f);
        return 0.0f; // token requested, but no live voice remains
    }

    void clearAllTokenVoices() noexcept {
        for (auto& e : tokenVoices) e = {};
        focusedVoiceToken = 0;
        nextSyntheticNoteIdSerial_ = 1u;
        nextTokenSerial_ = 1u;
    }

    // Canonical token resolution for exact event identity only.
    uint64_t resolveVoiceTokenForEventIdentity(int16_t ch, int16_t note, int32_t noteId) const noexcept {
        if (noteId < 0) return 0;
        uint64_t bestTok = 0;
        uint32_t bestOrder = 0;
        for (const auto& e : tokenVoices) {
            if (!e.token.active) continue;
            if (e.token.channel != ch || e.token.note != note || e.token.noteId != noteId) continue;
            if (bestTok == 0 || e.token.arrivalOrder >= bestOrder) {
                bestTok = e.token.token;
                bestOrder = e.token.arrivalOrder;
            }
        }
        return bestTok;
    }

private:
    void recomputeFocusedVoiceToken_() noexcept {
        uint64_t bestTok = 0;
        uint32_t bestOrder = 0;
        for (const auto& e : tokenVoices) {
            if (!e.token.active) continue;
            if (bestTok == 0 || e.token.arrivalOrder >= bestOrder) {
                bestTok = e.token.token;
                bestOrder = e.token.arrivalOrder;
            }
        }
        focusedVoiceToken = bestTok;
    }
public:

    // ----------------------------------------------------------------------
    float supply_mod = 0.0f;
    float ground_bounce = 0.0f;
    float charge_injection = 0.0f;
    float filter_memory = 0.0f;
    float thermal_drift = 0.0f;
    float leakage_state = 0.0f;

    float host_tempo_bpm = 0.0f;
    float last_note_velocity = 0.0f;
    uint8_t last_note = 0;
    bool transport_playing = false;
    bool mod_wheel_from_cc = false;
    float mod_wheel = 0.0f;
    float lfo_values[4]{};
    float env1_level = 0.0f;
    float random_bipolar = 0.0f;
    uint8_t synth_filter_route_low_nibble = 0x07u;
    float pitch_bend_norm[16]{};
    float bend_range_semis[16]{};
    uint8_t split_pitch_bend_msb[16]{};
    uint8_t split_pitch_bend_lsb[16]{};
    uint8_t split_pitch_bend_lsb_seen[16]{};
    uint8_t pn_active[16]{};           // 0=None, 1=RPN, 2=NRPN
    int16_t pn_number[16]{};           // selected RPN/NRPN number per channel, -1 = unset
    uint8_t pn_data_msb[16]{};
    uint8_t pn_data_lsb[16]{};
    float channel_pressure[16]{};
    uint8_t poly_pressure[16][128]{};
    uint8_t last_poly_pressure_note[16]{};
    uint8_t last_note_channel = 0;
    SidNoteIdentity focused_note{};
    float focused_poly_pressure_cached = 0.0f;
    std::array<SidActiveVoiceIdentity, kCompatActiveIdentityCapacity> active_note_identities{};
    std::array<std::array<int16_t, 128>, 16> newest_active_identity_index{};
    uint32_t note_on_counter = 0;
    uint32_t random_state = 0x12345678u;
    uint32_t random_scope_serial = 1u;
    uint32_t random_resolved_scope_serial = 0u;
    bool follow_host_tempo_seq = false;
    bool follow_host_tempo_arp = false;
    bool arp_active = false;
    int32_t seq_step = 0;
    int32_t seq_ping_dir = 1;
    double seq_samples_until_step = -1.0;
    int32_t seq_last_note = -1;
    int32_t seq_note_id_counter = 1;
    uint32_t seq_rng_state = 0x13572468u;
    bool seq_host_was_playing = false;
    double seq_last_project_time_ppq = -1.0;
    bool sustain[16]{};
    bool sostenuto[16]{};

    void resetActiveIdentityHints() noexcept {
        for (auto& ch : newest_active_identity_index)
            for (auto& idx : ch)
                idx = -1;
    }

    void recomputeNewestIdentityHint(int channel, int midiNote) noexcept {
        channel = std::clamp(channel, 0, 15);
        midiNote = std::clamp(midiNote, 0, 127);
        int16_t bestIdx = -1;
        uint32_t bestOrder = 0u;
        for (size_t i = 0; i < active_note_identities.size(); ++i) {
            const auto& v = active_note_identities[i];
            if (!v.active) continue;
            if (v.identity.channel != channel || v.identity.midi_note != midiNote) continue;
            if (bestIdx < 0 || v.identity.note_on_order >= bestOrder) {
                bestIdx = static_cast<int16_t>(i);
                bestOrder = v.identity.note_on_order;
            }
        }
        newest_active_identity_index[(size_t)channel][(size_t)midiNote] = bestIdx;
    }

    int activeIdentityHintIndex(int midiNote, int channel) const noexcept {
        if (channel < 0 || channel >= 16 || midiNote < 0 || midiNote >= 128) return -1;
        const int idx = newest_active_identity_index[(size_t)channel][(size_t)midiNote];
        if (idx < 0 || idx >= static_cast<int>(active_note_identities.size())) return -1;
        const auto& v = active_note_identities[(size_t)idx];
        if (!v.active) return -1;
        if (v.identity.channel != channel || v.identity.midi_note != midiNote) return -1;
        return idx;
    }

    void rebuildCompatActiveIdentitiesFromTokens() noexcept {
        for (auto& v : active_note_identities) v = {};
        resetActiveIdentityHints();
        size_t out = 0;
        for (const auto& e : tokenVoices) {
            if (!e.token.active) continue;
            if (out >= active_note_identities.size()) break;
            auto& dst = active_note_identities[out++];
            dst.identity.midi_note = e.token.note;
            dst.identity.channel = e.token.channel;
            dst.identity.note_id = e.token.noteId;
            dst.identity.note_on_order = e.token.arrivalOrder;
            dst.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
            dst.poly_pressure = std::clamp(e.polyPressure, 0.0f, 1.0f);
            dst.sustained = e.sustained;
            dst.sostenuto_latched = e.sostenutoLatched;
            dst.active = true;
            recomputeNewestIdentityHint(dst.identity.channel, dst.identity.midi_note);
        }
        recomputeFocusedNote();
    }

    void clearActiveNoteIdentities() noexcept {
        for (auto& v : active_note_identities) v = {};
        resetActiveIdentityHints();
        focused_note = {};
        focused_poly_pressure_cached = 0.0f;
        // Also clear canonical token registry.
        clearAllTokenVoices();
    }

    void noteOnCanonical(int midiNote, int channel, int noteId, float velocity) noexcept {
        midiNote = std::clamp(midiNote, 0, 127);
        channel = std::clamp(channel, 0, 15);
        velocity = std::clamp(std::isfinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f);
        int32_t effectiveNoteId = noteId;
        bindVoiceToken(static_cast<int16_t>(channel), static_cast<int16_t>(midiNote),
                       noteId, velocity, ++note_on_counter, &effectiveNoteId);
        rebuildCompatActiveIdentitiesFromTokens();
    }


    void advanceRandomScope() noexcept {
        ++random_scope_serial;
        if (random_scope_serial == 0u) random_scope_serial = 1u;
    }

    void resolveRandomForCurrentScope() noexcept {
        if (random_resolved_scope_serial == random_scope_serial) return;
        uint32_t& rs = random_state;
        if (rs == 0u) rs = 0x12345678u;
        rs ^= rs << 13;
        rs ^= rs >> 17;
        rs ^= rs << 5;
        const float uni = static_cast<float>(rs & 0x00FFFFFFu) / 16777215.0f;
        random_bipolar = std::clamp(uni * 2.0f - 1.0f, -1.0f, 1.0f);
        random_resolved_scope_serial = random_scope_serial;
    }

    void noteOffCanonical(int midiNote, int channel, int noteId) noexcept {
        midiNote = std::clamp(midiNote, 0, 127);
        channel  = std::clamp(channel,  0, 15);

        // Token-first: resolve the canonical token, snapshot its exact identity,
        // then retire both token and legacy identity surfaces for the SAME voice.
        uint64_t resolvedTok = 0;
        {
            uint64_t tok = resolveVoiceTokenForEventIdentity(
                static_cast<int16_t>(channel),
                static_cast<int16_t>(midiNote),
                noteId);
            if (tok == 0 && noteId < 0) {
                // Anonymous MIDI/CoreMIDI note-off has no host identity. Pair it
                // FIFO with the oldest still-active same-note token, restricted
                // to anonymous/synthetic identities only.
                // Do not consume a real host-noteId voice merely because pitch and
                // channel match; a real noteId NoteOff must release that voice.
                uint32_t bestOrder = 0u;
                for (const auto& e : tokenVoices) {
                    if (!e.token.active) continue;
                    if (e.token.channel != channel || e.token.note != midiNote) continue;
                    if (!isSyntheticAnonymousNoteId_(e.token.noteId)) continue;
                    if (tok == 0 || e.token.arrivalOrder < bestOrder) {
                        tok = e.token.token;
                        bestOrder = e.token.arrivalOrder;
                    }
                }
            }
            if (tok != 0 && findActiveVoiceByToken(tok) != nullptr) {
                resolvedTok = tok;
            }
        }

        // Sustain pedal law: note-off does not destroy the token while CC64 is
        // held. It marks the exact resolved token/compat identity as sustained;
        // the token is released only by the canonical CC64-off transition. This
        // prevents stuck notes and prevents anonymous same-note releases from
        // tearing down a newer token.
        if (sustain[(size_t)channel]) {
            if (resolvedTok != 0) {
                if (auto* e = findActiveVoiceByToken(resolvedTok)) e->sustained = true;
            }
            rebuildCompatActiveIdentitiesFromTokens();
            return;
        }

        if (resolvedTok != 0) unbindVoiceToken(resolvedTok);
        rebuildCompatActiveIdentitiesFromTokens();
    }

    void releaseSustainedVoicesForChannel(int channel) noexcept {
        channel = std::clamp(channel, 0, 15);
        for (auto& e : tokenVoices) {
            if (!e.token.active) continue;
            if (!e.sustained) continue;
            if (e.token.channel >= 0 && e.token.channel != channel) continue;
            const uint64_t tok = e.token.token;
            unbindVoiceToken(tok);
        }
        rebuildCompatActiveIdentitiesFromTokens();
    }


    void recomputeFocusedNote(int preferredChannel = -1) noexcept {
        const SidActiveVoiceIdentity* best = nullptr;
        for (const auto& v : active_note_identities) {
            if (!v.active || !v.identity.valid()) continue;
            if (preferredChannel >= 0 && v.identity.channel != preferredChannel) continue;
            if (!best || v.identity.note_on_order > best->identity.note_on_order)
                best = &v;
        }
        if (!best && preferredChannel >= 0) {
            for (const auto& v : active_note_identities) {
                if (!v.active || !v.identity.valid()) continue;
                if (!best || v.identity.note_on_order > best->identity.note_on_order)
                    best = &v;
            }
        }
        focused_note = best ? best->identity : SidNoteIdentity{};
        if (best && best->identity.channel >= 0 && best->identity.channel < 16)
            last_note_channel = static_cast<uint8_t>(best->identity.channel);
        refreshFocusedPolyPressureCache();
    }


    void refreshFocusedPolyPressureCache() noexcept {
        focused_poly_pressure_cached = 0.0f;
        if (!focused_note.valid()) return;
        const int channel = std::clamp<int>(focused_note.channel, 0, 15);
        const int note = std::clamp<int>(focused_note.midi_note, 0, 127);
        const SidActiveVoiceIdentity* exact = nullptr;
        const SidActiveVoiceIdentity* fallback = nullptr;
        for (const auto& v : active_note_identities) {
            if (!v.active) continue;
            if (v.identity.channel != channel || v.identity.midi_note != note) continue;
            if (!fallback || v.identity.note_on_order > fallback->identity.note_on_order) fallback = &v;
            if (focused_note.note_id >= 0 && v.identity.note_id == focused_note.note_id) {
                if (!exact || v.identity.note_on_order > exact->identity.note_on_order) exact = &v;
            }
        }
        const SidActiveVoiceIdentity* chosen = exact ? exact : fallback;
        if (chosen) {
            focused_poly_pressure_cached = std::clamp(chosen->poly_pressure, 0.0f, 1.0f);
            return;
        }
        focused_poly_pressure_cached = decodePolyPressure(poly_pressure[channel][note]);
    }

    // Focused poly pressure: token-first. Only falls back to the note-grid cache
    // when there is no focused token. If a focused token exists but no active
    // token entry remains, pressure is 0.0 because the voice is gone.
    float focusedPolyPressure() const noexcept {
        if (focusedVoiceToken != 0) {
            const float tp = focusedTokenPolyPressure();
            if (tp >= 0.0f) return tp;  // valid token entry: use it
            // token set but entry gone: fall through to cache
        }
        return std::clamp(focused_poly_pressure_cached, 0.0f, 1.0f);
    }

    // Compat-facing rendered poly-pressure update. The note-grid value is kept for
    // legacy readback only; token voice pressure must be mutated by canonical callers.
    void setRenderedPolyPressureForNote(int midiNote, int channel, float pressure) noexcept {
        midiNote = std::clamp(midiNote, 0, 127);
        channel = std::clamp(channel, 0, 15);
        pressure = std::clamp(std::isfinite(pressure) ? pressure : 0.0f, 0.0f, 1.0f);
        poly_pressure[(size_t)channel][(size_t)midiNote] = encodePolyPressure(pressure);
        refreshFocusedPolyPressureCache();
    }

    void reset() noexcept {
        *this = {};
        for (float& v : bend_range_semis) v = 2.0f;
        for (int16_t& v : pn_number) v = -1;
        seq_samples_until_step = -1.0;
        seq_last_note = -1;
        seq_ping_dir = 1;
        seq_note_id_counter = 1;
        seq_last_project_time_ppq = -1.0;
        random_state = 0x12345678u;
        random_scope_serial = 1u;
        random_resolved_scope_serial = 0u;
        seq_rng_state = 0x13572468u;
        clearActiveNoteIdentities();
        resetActiveIdentityHints();
    }
    void sanitize() noexcept {
        auto fix = [](float& v) noexcept {
            if (!std::isfinite(v)) v = 0u;
            v = std::clamp(v, -4.0f, 4.0f);
        };
        fix(supply_mod);
        fix(ground_bounce);
        fix(charge_injection);
        fix(filter_memory);
        fix(thermal_drift);
        fix(leakage_state);
        if (!std::isfinite(host_tempo_bpm)) host_tempo_bpm = 0.0f;
        host_tempo_bpm = std::clamp(host_tempo_bpm, 0.0f, 400.0f);
        if (!std::isfinite(last_note_velocity)) last_note_velocity = 0.0f;
        last_note_velocity = std::clamp(last_note_velocity, 0.0f, 1.0f);
        mod_wheel = std::isfinite(mod_wheel) ? std::clamp(mod_wheel, 0.0f, 1.0f) : 0.0f;
        for (float& v : lfo_values) {
            if (!std::isfinite(v)) v = 0u;
            v = std::clamp(v, -1.0f, 1.0f);
        }
        env1_level = std::isfinite(env1_level) ? std::clamp(env1_level, 0.0f, 1.0f) : 0.0f;
        random_bipolar = std::isfinite(random_bipolar) ? std::clamp(random_bipolar, -1.0f, 1.0f) : 0.0f;
        synth_filter_route_low_nibble &= 0x07u;
        for (float& v : pitch_bend_norm) {
            if (!std::isfinite(v)) v = 0u;
            v = std::clamp(v, -1.0f, 1.0f);
        }
        for (float& v : bend_range_semis) {
            if (!std::isfinite(v)) v = 2.0f;
            v = std::clamp(v, 0.0f, 48.0f);
        }
        for (float& v : channel_pressure) {
            if (!std::isfinite(v)) v = 0u;
            v = std::clamp(v, 0.0f, 1.0f);
        }
        for (auto& row : poly_pressure) {
            for (uint8_t& v : row) {
                v = std::min<uint8_t>(v, 255u);
            }
        }
        if (last_note > 127) last_note = 0;
        if (last_note_channel > 15) last_note_channel = 0;
        if (!focused_note.valid()) focused_note = {};
        for (uint8_t& v : split_pitch_bend_lsb_seen) v = (v != 0) ? 1 : 0;
        for (uint8_t& v : pn_active) if (v > 2) v = 0;
        for (int16_t& v : pn_number) if (v < -1) v = -1;
        if (!std::isfinite(seq_samples_until_step)) seq_samples_until_step = -1.0;
        if (!std::isfinite(seq_last_project_time_ppq)) seq_last_project_time_ppq = -1.0;
        if (seq_ping_dir == 0) seq_ping_dir = 1;
        for (uint8_t& note : last_poly_pressure_note) {
            if (note > 127) note = 0;
        }
        for (auto& v : active_note_identities) {
            if (!v.active) { v = {}; continue; }
            if (!v.identity.valid()) { v = {}; continue; }
            v.identity.channel = static_cast<int16_t>(std::clamp<int>(v.identity.channel, 0, 15));
            v.identity.midi_note = static_cast<int16_t>(std::clamp<int>(v.identity.midi_note, 0, 127));
            if (!std::isfinite(v.velocity)) v.velocity = 0.0f;
            v.velocity = std::clamp(v.velocity, 0.0f, 1.0f);
            if (!std::isfinite(v.poly_pressure)) v.poly_pressure = 0.0f;
            v.poly_pressure = std::clamp(v.poly_pressure, 0.0f, 1.0f);
        }
        if (random_state == 0u) random_state = 0x12345678u;
        if (seq_rng_state == 0u) seq_rng_state = 0x13572468u;
    }
};

} // namespace ArpSID
