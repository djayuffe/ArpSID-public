#pragma once

#include "arpsid/core/sid_chip.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace ArpSID {

/**
 * Voice State - Tracks per-voice information for polyphonic operation
 */
struct VoiceState {
    int midiNote = -1;          // MIDI note (-1 = inactive)
    int channel = -1;           // MIDI/VST3 channel when known
    int noteId = -1;            // VST3 note identity when known
    uint64_t voiceToken = 0;    // canonical token — primary identity truth; 0 = unbound
    float velocity = 0.0f;      // Note velocity (0-1)
    float age = 0.0f;           // Time since note on (for voice stealing)
    bool isActive = false;      // Voice allocated / still sounding (includes release tail)
    bool keyDown = false;       // Physical note still held
    bool isSustained = false;   // Held by sustain pedal after key release
    bool isSostenuto = false;   // Held by sostenuto latch after key release

    void reset() {
        midiNote = -1;
        channel = -1;
        noteId = -1;
        voiceToken = 0;
        velocity = 0.0f;
        age = 0.0f;
        isActive = false;
        keyDown = false;
        isSustained = false;
        isSostenuto = false;
    }
};

/**
 * Voice Manager - Handles polyphonic voice allocation
 * Supports up to 8 simultaneous voices with intelligent voice stealing
 */
class VoiceManager {
public:
    static constexpr int MAX_VOICES = 8;
    
    VoiceManager() {
        reset();
    }

    struct NoteOnResult {
        int voiceIndex = -1;
        bool wasRetrigger = false;
        bool wasStolen = false;
    };

    struct NoteOffResult {
        int voiceIndex = -1;
        uint64_t voiceToken = 0;
    };

    static bool voiceIdentityMatches(const VoiceState& voice, int midiNote, int channel, int noteId) {
        if (!voice.isActive) return false;
        if (voice.midiNote != midiNote) return false;
        // Only a real host noteId is a safe retrigger identity. Anonymous
        // same-note NoteOn events must allocate independent physical voices;
        // otherwise two held keys collapse into one voice and a later FIFO
        // NoteOff can release the wrong tail.
        if (noteId < 0) return false;
        const bool channelMatches = (channel < 0) || (voice.channel < 0) || (voice.channel == channel);
        const bool noteIdMatches = (voice.noteId == noteId);
        return channelMatches && noteIdMatches;
    }
    
    void reset() {
        for (auto& voice : voices) {
            voice.reset();
        }
        sustainPedalDown = false;
        sustainPedalDownByChannel.fill(false);
        sostenutoPedalDownByChannel.fill(false);
    }
    
    // Note on - allocate voice, return voice index
    // Takes an optional callback invoked when a voice is stolen (for gate-off before steal).
    template<typename GateOffCallback>
    NoteOnResult noteOnDetailed(int midiNote, float velocity, int channel, int noteId, GateOffCallback&& onSteal) {
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voiceIdentityMatches(voices[i], midiNote, channel, noteId)) {
                voices[i].velocity = velocity;
                voices[i].channel = channel;
                voices[i].noteId = noteId;
                voices[i].age = 0.0f;
                voices[i].keyDown = true;
                voices[i].isSustained = false;
                voices[i].isSostenuto = false;
                return { i, true, false };
            }
        }

        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voices[i].isActive) {
                voices[i].midiNote = midiNote;
                voices[i].channel = channel;
                voices[i].noteId = noteId;
                voices[i].velocity = velocity;
                voices[i].age = 0.0f;
                voices[i].isActive = true;
                voices[i].keyDown = true;
                voices[i].isSustained = false;
                voices[i].isSostenuto = false;
                return { i, false, false };
            }
        }

        return { stealVoice(midiNote, velocity, channel, noteId, std::forward<GateOffCallback>(onSteal)), false, true };
    }

    // Token-stamping note-on: same as noteOnDetailed but stamps voiceToken on the allocated slot.
    template<typename GateOffCallback>
    NoteOnResult noteOnWithToken(int midiNote, float velocity, int channel, int noteId,
                                 uint64_t voiceToken, GateOffCallback&& onSteal) {
        auto res = noteOnDetailed(midiNote, velocity, channel, noteId, std::forward<GateOffCallback>(onSteal));
        if (res.voiceIndex >= 0) voices[(size_t)res.voiceIndex].voiceToken = voiceToken;
        return res;
    }

    // Token-first note-off: resolves by token only.
    // When token == 0 this function returns an empty result immediately — there is NO
    // compat/identity fallback. Callers that need note-off by identity must resolve the
    // token first via SidDynamicState::resolveVoiceTokenForEventIdentity(). This is
    // intentional and verified by closure_regression_tests T_NoteOffNoCompatFallback.
    NoteOffResult noteOffByToken(uint64_t voiceToken, int midiNote, int channel, int noteId) {
        if (voiceToken != 0) {
            for (int i = 0; i < MAX_VOICES; ++i) {
                auto& voice = voices[i];
                if (!voice.isActive || voice.voiceToken != voiceToken) continue;
                const uint64_t releasedToken = voice.voiceToken;
                voice.keyDown = false;
                const int sustainChannel = (voice.channel >= 0 && voice.channel < 16) ? voice.channel : channel;
                const bool channelSustain = (sustainChannel >= 0 && sustainChannel < 16)
                    ? sustainPedalDownByChannel[(size_t)sustainChannel] : sustainPedalDown;
                voice.isSustained = channelSustain;
                if (voice.isSustained || voice.isSostenuto) {
                    return {};
                }
                return { i, releasedToken };
            }
                        return {};
        }
        (void)midiNote; (void)channel; (void)noteId;
        return {};
    }

    template<typename GateOffCallback>
    NoteOnResult noteOnDetailed(int midiNote, float velocity, GateOffCallback&& onSteal) {
        return noteOnDetailed(midiNote, velocity, -1, -1, std::forward<GateOffCallback>(onSteal));
    }

    template<typename GateOffCallback>
    int noteOn(int midiNote, float velocity, int channel, int noteId, GateOffCallback&& onSteal) {
        return noteOnDetailed(midiNote, velocity, channel, noteId, std::forward<GateOffCallback>(onSteal)).voiceIndex;
    }

    template<typename GateOffCallback>
    int noteOn(int midiNote, float velocity, GateOffCallback&& onSteal) {
        return noteOnDetailed(midiNote, velocity, -1, -1, std::forward<GateOffCallback>(onSteal)).voiceIndex;
    }

    int noteOn(int midiNote, float velocity, int channel = -1, int noteId = -1) {
        return noteOnDetailed(midiNote, velocity, channel, noteId, [](int){}).voiceIndex;
    }
    
    // Note off
    // BUG-AUDIO-04 FIX: Must check voice.isActive before matching midiNote.
    // Without this, an already-released voice (isActive=false) holding the same
    // MIDI note would have isSustained set to true when sustain pedal is down,
    // locking it in sustained-but-inactive limbo permanently.
    NoteOffResult noteOffDetailedResult(int midiNote, int channel = -1, int noteId = -1) {
        int bestIndex = -1;
        bool bestExactChannel = false;
        bool bestExactNoteId = false;
        float bestAge = -1.0f;

        for (int i = 0; i < MAX_VOICES; ++i) {
            const auto& voice = voices[i];
            if (!voice.isActive || !voice.keyDown || voice.midiNote != midiNote) continue;

            const bool channelCompatible = (channel < 0) || (voice.channel < 0) || (voice.channel == channel);
            // A noteId-less NoteOff is anonymous and must pair with an anonymous
            // NoteOn only. It must not release a real host-noteId voice just
            // because channel+pitch match. Real noteId NoteOff remains exact.
            const bool noteIdCompatible = (noteId >= 0) ? (voice.noteId == noteId) : (voice.noteId < 0);
            if (!channelCompatible || !noteIdCompatible) continue;

            const bool exactChannel = (channel >= 0 && voice.channel == channel);
            const bool exactNoteId = (noteId >= 0 && voice.noteId == noteId);
            const bool better =
                (bestIndex < 0) ||
                (exactNoteId != bestExactNoteId && exactNoteId) ||
                (exactNoteId == bestExactNoteId && exactChannel != bestExactChannel && exactChannel) ||
                (exactNoteId == bestExactNoteId && exactChannel == bestExactChannel && voice.age > bestAge);

            if (better) {
                bestIndex = i;
                bestExactChannel = exactChannel;
                bestExactNoteId = exactNoteId;
                bestAge = voice.age;
            }
        }

        if (bestIndex < 0) return {};
        auto& voice = voices[(size_t)bestIndex];
        const uint64_t releasedToken = voice.voiceToken;
        voice.keyDown = false;
        const int sustainChannel = (voice.channel >= 0 && voice.channel < 16) ? voice.channel : channel;
        // FIX: Fall back to the global sustainPedalDown flag for unscoped voices
        // (voice.channel < 0 and channel < 0). Previously returned false, which
        // silently broke sustain for any voice allocated without channel metadata.
        const bool channelSustain = (sustainChannel >= 0 && sustainChannel < 16)
            ? sustainPedalDownByChannel[(size_t)sustainChannel] : sustainPedalDown;
        voice.isSustained = channelSustain;
        if (voice.isSustained || voice.isSostenuto) {
            return {};
        }
        return { bestIndex, releasedToken };
    }

    // Last-resort stuck-note guard for poly. The strict matcher above refuses to
    // let an anonymous (noteId<0) NoteOff release a voice that carries a real host
    // noteId, which is correct for same-note polyphony but leaves a stuck gate when
    // a host/AU path does not round-trip note ids symmetrically (note-on with an
    // id, note-off without — or vice versa). This releases the oldest still-key-down
    // voice for the same note+channel, IGNORING noteId, and is meant to be called
    // only after noteOffDetailedResult() returned no match. It still honours the
    // sustain/sostenuto pedals, so a pedal-held voice is not force-released.
    NoteOffResult noteOffLooseSameNote(int midiNote, int channel = -1) {
        int bestIndex = -1;
        float bestAge = -1.0f;
        for (int i = 0; i < MAX_VOICES; ++i) {
            const auto& voice = voices[i];
            if (!voice.isActive || !voice.keyDown || voice.midiNote != midiNote) continue;
            const bool channelCompatible = (channel < 0) || (voice.channel < 0) || (voice.channel == channel);
            if (!channelCompatible) continue;
            if (voice.age > bestAge) { bestIndex = i; bestAge = voice.age; }
        }
        if (bestIndex < 0) return {};
        auto& voice = voices[(size_t)bestIndex];
        const uint64_t releasedToken = voice.voiceToken;
        voice.keyDown = false;
        const int sustainChannel = (voice.channel >= 0 && voice.channel < 16) ? voice.channel : channel;
        const bool channelSustain = (sustainChannel >= 0 && sustainChannel < 16)
            ? sustainPedalDownByChannel[(size_t)sustainChannel] : sustainPedalDown;
        voice.isSustained = channelSustain;
        if (voice.isSustained || voice.isSostenuto) return {};
        return { bestIndex, releasedToken };
    }

    void noteOffDetailed(int midiNote, int channel = -1, int noteId = -1) {
        (void)noteOffDetailedResult(midiNote, channel, noteId);
    }

    void noteOff(int midiNote) {
        noteOffDetailed(midiNote, -1, -1);
    }


    void forceAssignVoice(int voiceIndex, int midiNote, float velocity, int channel, int noteId, uint64_t voiceToken) noexcept {
        if (voiceIndex < 0 || voiceIndex >= MAX_VOICES) return;
        auto& voice = voices[(size_t)voiceIndex];
        voice.midiNote = midiNote;
        voice.channel = channel;
        voice.noteId = noteId;
        voice.voiceToken = voiceToken;
        voice.velocity = velocity;
        voice.age = 0.0f;
        voice.isActive = true;
        voice.keyDown = true;
        voice.isSustained = false;
        voice.isSostenuto = false;
    }

    NoteOffResult forceReleaseVoice(int voiceIndex, bool sustained = false) noexcept {
        return forceReleaseVoicePedalAware(voiceIndex, sustained, false);
    }

    NoteOffResult forceReleaseVoicePedalAware(int voiceIndex, bool sustained, bool preserveSostenuto) noexcept {
        if (voiceIndex < 0 || voiceIndex >= MAX_VOICES) return {};
        auto& voice = voices[(size_t)voiceIndex];
        if (!voice.isActive) return {};
        const uint64_t releasedToken = voice.voiceToken;
        voice.keyDown = false;
        voice.isSustained = sustained;
        if (!preserveSostenuto) voice.isSostenuto = false;
        return { voiceIndex, releasedToken };
    }

    int forceReleaseVoicesByToken(uint64_t voiceToken, bool sustained = false) noexcept {
        return forceReleaseVoicesByTokenPedalAware(voiceToken, sustained, false);
    }

    int forceReleaseVoicesByTokenPedalAware(uint64_t voiceToken, bool sustained, bool preserveSostenuto) noexcept {
        if (voiceToken == 0) return 0;
        int released = 0;
        for (int i = 0; i < MAX_VOICES; ++i) {
            auto& voice = voices[i];
            if (!voice.isActive || voice.voiceToken != voiceToken) continue;
            voice.keyDown = false;
            voice.isSustained = sustained;
            if (!preserveSostenuto) voice.isSostenuto = false;
            ++released;
        }
        return released;
    }

    // All notes off (panic)
    void allNotesOff() {
        for (auto& voice : voices) {
            voice.reset();
        }
        orphanPrevMask_ = 0u;
    }

    void resetOrphanReconcile() noexcept { orphanPrevMask_ = 0u; }

    // Stuck-note safety net: reconcile gated voices against the authoritative set
    // of physically-held keys. Any key-down voice whose (channel, note) is
    // reported NOT held by isHeld() and NOT held by a pedal (isPedalHeld()) is an
    // orphan — its note-off was lost by some matching failure (asymmetric noteId,
    // channel mismatch, dropped note-off, etc.). To avoid clipping a note-off that
    // lands exactly on a buffer boundary (the held mirror can lead the rendered
    // note-off by up to one block), a voice must read as orphaned on two
    // consecutive passes before it is released; a genuinely stuck voice stays
    // orphaned indefinitely and is released on the next pass. gateOff(i) is called
    // so the caller can drop the physical SID gate. Returns the number released.
    template <typename HeldFn, typename PedalFn, typename GateOffFn>
    int reconcileUnheldVoices(HeldFn&& isHeld, PedalFn&& isPedalHeld, GateOffFn&& gateOff) {
        uint32_t nowOrphan = 0u;
        for (int i = 0; i < MAX_VOICES; ++i) {
            const auto& v = voices[i];
            if (!v.isActive || !v.keyDown) continue;
            if (v.channel < 0 || v.midiNote < 0) continue;
            if (isHeld(v.channel, v.midiNote)) continue;
            if (isPedalHeld(v.channel)) continue;
            nowOrphan |= (1u << i);
        }
        const uint32_t confirmed = nowOrphan & orphanPrevMask_;
        int released = 0;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!(confirmed & (1u << i))) continue;
            voices[i].keyDown = false;
            voices[i].isSustained = false;
            voices[i].isSostenuto = false;
            gateOff(i);
            ++released;
        }
        orphanPrevMask_ = nowOrphan;
        return released;
    }

    // Channel-scoped all-notes-off: reset only voices belonging to `channel`.
    // If channel < 0, resets all voices (same as allNotesOff()).
    void allNotesOffChannel(int channel) {
        if (channel < 0) { allNotesOff(); return; }
        for (auto& voice : voices) {
            // Channel-less/arp voices are logically compatible with the active
            // channel scope. Leaving channel=-1 voices alive here is a real
            // stuck-note source when hosts send channel-scoped CC123/CC120.
            if (voice.channel < 0 || voice.channel == channel) voice.reset();
        }
    }
    
    // Sustain pedal
    // FIX #04: setSustainPedal now takes an optional callback invoked for every
    // voice that is released when the pedal lifts. The BitPerfectEngine passes
    // a lambda that calls setGate(false) on the corresponding SID chip voices,
    // so the SID envelope actually stops. Without this, voice.isActive was set
    // to false but SIDVoice::gate remained true, causing infinite sustain.
    template<typename GateOffCallback>
    void setSustainPedal(bool down, GateOffCallback&& onGateOff) {
        sustainPedalDown = down;
        sustainPedalDownByChannel.fill(down);
        if (!down) {
            for (int i = 0; i < MAX_VOICES; ++i) {
                auto& voice = voices[i];
                if (!voice.isSustained) continue;
                voice.isSustained = false;
                if (!voice.keyDown && !voice.isSostenuto) {
                    onGateOff(i);
                    voice.reset();
                }
            }
        }
    }

    template<typename GateOffCallback>
    void setSustainPedal(int channel, bool down, GateOffCallback&& onGateOff) {
        if (channel < 0 || channel >= 16) {
            setSustainPedal(down, std::forward<GateOffCallback>(onGateOff));
            return;
        }
        sustainPedalDownByChannel[(size_t)channel] = down;
        sustainPedalDown = std::any_of(sustainPedalDownByChannel.begin(), sustainPedalDownByChannel.end(), [](bool v){ return v; });
        if (!down) {
            for (int i = 0; i < MAX_VOICES; ++i) {
                auto& voice = voices[i];
                if (!voice.isSustained || (voice.channel >= 0 && voice.channel != channel)) continue;
                voice.isSustained = false;
                if (!voice.keyDown && !voice.isSostenuto) {
                    onGateOff(i);
                    voice.reset();
                }
            }
        }
    }

    // Convenience overloads for callers that don't need the gate-off callback.
    void setSustainPedal(bool down) {
        setSustainPedal(down, [](int){});
    }
    void setSustainPedal(int channel, bool down) {
        setSustainPedal(channel, down, [](int){});
    }

    template<typename GateOffCallback>
    void setSostenutoPedal(int channel, bool down, GateOffCallback&& onGateOff) {
        if (channel < 0 || channel >= 16) return;
        sostenutoPedalDownByChannel[(size_t)channel] = down;
        if (down) {
            for (int i = 0; i < MAX_VOICES; ++i) {
                auto& voice = voices[i];
                if (!voice.isActive || (voice.channel >= 0 && voice.channel != channel)) continue;
                if (voice.keyDown) voice.isSostenuto = true;
            }
            return;
        }
        for (int i = 0; i < MAX_VOICES; ++i) {
            auto& voice = voices[i];
            if (!voice.isActive || (voice.channel >= 0 && voice.channel != channel) || !voice.isSostenuto) continue;
            voice.isSostenuto = false;
            if (!voice.keyDown && !voice.isSustained) {
                onGateOff(i);
                voice.reset();
            }
        }
    }

    void setSostenutoPedal(int channel, bool down) {
        setSostenutoPedal(channel, down, [](int){});
    }
    
    // Update voice ages (call once per buffer)
    void updateVoiceAges(float deltaTime) {
        for (auto& voice : voices) {
            if (voice.isActive) {
                voice.age += deltaTime;
            }
        }
    }
    
    // Get voice state
    const VoiceState& getVoiceState(int index) const {
        return voices[index];
    }
    
    // Count active voices
    int getActiveVoiceCount() const {
        int count = 0;
        for (const auto& voice : voices) {
            if (voice.isActive) ++count;
        }
        return count;
    }
    
    // Get all active voice indices
    // Realtime-safe: fill caller-provided buffer with active voice indices.
    // Returns the number of entries written to out (<= maxOut). Never allocates.
    int getActiveVoicesInto(int* out, int maxOut) const {
        int c = 0;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].isActive) {
                if (c < maxOut) out[c] = i;
                ++c;
            }
        }
        return std::min(c, maxOut);
    }

    void markVoiceSustained(int index, bool sustained) {
        if (index < 0 || index >= MAX_VOICES) return;
        voices[(size_t)index].isSustained = sustained;
    }

    void markVoiceSostenuto(int index, bool sostenuto) {
        if (index < 0 || index >= MAX_VOICES) return;
        voices[(size_t)index].isSostenuto = sostenuto;
    }

    void releaseFinished(int index) {
        if (index < 0 || index >= MAX_VOICES) return;
        auto& v = voices[(size_t)index];
        if (!v.keyDown && !v.isSustained && !v.isSostenuto) v.reset();
    }

private:
    std::array<VoiceState, MAX_VOICES> voices;
    bool sustainPedalDown = false;
    std::array<bool, 16> sustainPedalDownByChannel{};
    std::array<bool, 16> sostenutoPedalDownByChannel{};
    uint32_t orphanPrevMask_ = 0u; // voices seen orphaned on the previous reconcile pass

    // Voice stealing algorithm — steal oldest non-sustained voice.
    // FIX MEDIUM: stealVoice now takes an optional gateOff callback so the engine
    // can call setGate(false) on the stolen voice's SID chip, preventing clicks
    // from a silent retrigger over a still-sounding envelope.
    template<typename GateOffCallback>
    int stealVoice(int midiNote, float velocity, int channel, int noteId, GateOffCallback&& onGateOff) {
        int oldestIndex = 0;
        float oldestAge = -1.0f;
        bool foundReleasedTail = false;
        bool foundKeyDown = false;

        // Best case: recycle the oldest released tail first. This preserves actively-held notes.
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voices[i].isActive) continue;
            if (voices[i].keyDown || voices[i].isSustained || voices[i].isSostenuto) continue;
            if (voices[i].age > oldestAge) {
                oldestAge = voices[i].age;
                oldestIndex = i;
                foundReleasedTail = true;
            }
        }

        // Next, steal the oldest non-sustained held voice.
        if (!foundReleasedTail) {
            oldestAge = -1.0f;
            for (int i = 0; i < MAX_VOICES; ++i) {
                if (!voices[i].isActive) continue;
                if (voices[i].isSustained || voices[i].isSostenuto) continue;
                if (voices[i].age > oldestAge) {
                    oldestAge = voices[i].age;
                    oldestIndex = i;
                    foundKeyDown = true;
                }
            }
        }

        // Last resort: every voice is pedal-held, so steal the oldest sustained voice.
        if (!foundReleasedTail && !foundKeyDown) {
            oldestAge = -1.0f;
            for (int i = 0; i < MAX_VOICES; ++i) {
                if (!voices[i].isActive) continue;
                if (voices[i].age > oldestAge) {
                    oldestAge = voices[i].age;
                    oldestIndex = i;
                }
            }
        }

        // Gate off the stolen voice before we overwrite it
        onGateOff(oldestIndex);
        
        // Steal it — clear old token, assign new one after caller stamps via noteOnWithToken
        voices[oldestIndex].voiceToken   = 0;
        voices[oldestIndex].midiNote     = midiNote;
        voices[oldestIndex].channel      = channel;
        voices[oldestIndex].noteId       = noteId;
        voices[oldestIndex].velocity     = velocity;
        voices[oldestIndex].age          = 0.0f;
        voices[oldestIndex].isActive     = true;
        voices[oldestIndex].keyDown      = true;
        voices[oldestIndex].isSustained  = false;
        voices[oldestIndex].isSostenuto  = false;  // FIX: clear stale sostenuto latch
        
        return oldestIndex;
    }

    int stealVoice(int midiNote, float velocity) {
        return stealVoice(midiNote, velocity, -1, -1, [](int){});
    }
};

} // namespace ArpSID
