#pragma once
#include "sid_voice_identity.h"
#include "sid_voice_token.h"
#include "sid_voice_token_pool.h"
#include "sid_event_queue.h"
#include "arpsid/engines/voice_manager.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <utility>

namespace ArpSID {

enum class PlayMode    : uint8_t { Poly=0, Mono=1, Legato=2, Unison=3 };
enum class NotePriority: uint8_t { Last=0, High=1, Low=2 };
enum class StealMode   : uint8_t { Oldest=0, Quietest=1, LowestEnergy=2 };
enum class RetrigMode  : uint8_t { Always=0, OnNewNote=1, Never=2 };

inline PlayMode sidPlayModeFromCanonicalVoiceMode(int mode) noexcept;

struct VoiceEvent {
    enum class Kind : uint8_t { Start, GateOn, GateOff, Glide, Retrigger, AllOff };
    Kind    kind      = Kind::Start;
    int     voiceIdx  = -1;
    int     midiNote  = -1;
    float   velocity  = 0.f;
    int     channel   = ArpSID::kSidUnresolvedChannel;
    int     noteId    = -1;
    uint64_t voiceToken = 0;  // canonical token assigned at note-on
    bool    retrigged = false;
};

static constexpr int kMaxVoiceEvents = 128;
struct VoiceEventBuffer {
    VoiceEvent events[kMaxVoiceEvents];
    int count = 0;
    uint32_t dropped = 0;
    void push(const VoiceEvent& ev) noexcept {
        if (count < kMaxVoiceEvents) {
            events[count++] = ev;
        } else {
            ++dropped;
        }
    }
    void reset() noexcept { count = 0; dropped = 0; }
};

struct HeldNote {
    SidNoteIdentity identity{};
    float velocity = 0.f;
    uint64_t voiceToken = 0;  // canonical token assigned at note-on
    bool active = false;
    int16_t midiNote() const noexcept { return identity.midi_note; }
    int16_t channel() const noexcept { return identity.channel; }
    int32_t noteId() const noexcept { return identity.note_id; }
    uint32_t order() const noexcept { return identity.note_on_order; }
};

static constexpr int kMaxHeldNotes = 32;

class VoiceAllocator {
public:
    using SustainGateOffFn = void(*)(void*, int);
    VoiceAllocator() { reset(); }
    void setSustainGateOffCallback(SustainGateOffFn fn, void* ctx) noexcept { sustainGateOffFn_ = fn; sustainGateOffCtx_ = ctx; }
    void setPlayMode(PlayMode m) noexcept { playMode_ = m; }
    void setNotePriority(NotePriority p) noexcept { notePriority_ = p; }
    void setStealMode(StealMode s) noexcept { stealMode_ = s; }
    void setRetrigMode(RetrigMode r) noexcept { retrigMode_ = r; }
    void setUnisonCount(int n) noexcept { unisonCount_ = std::clamp(n, 1, 8); }
    void setMaxPolyVoices(int n) noexcept { maxPolyVoices_ = std::clamp(n, 1, VoiceManager::MAX_VOICES); }
    void setSustainPedal(bool down) noexcept {
        sustainDown_ = down;
        sustainDownByChannel_.fill(down);
        vm_.setSustainPedal(down, [this](int i){ emitSustainGateOff_(i); });
    }
    void setSustainPedal(int channel, bool down) noexcept {
        if (channel < 0 || channel >= 16) { setSustainPedal(down); return; }
        sustainDownByChannel_[(size_t)channel] = down;
        sustainDown_ = std::any_of(sustainDownByChannel_.begin(), sustainDownByChannel_.end(), [](bool v){ return v; });
        vm_.setSustainPedal(channel, down, [this](int i){ emitSustainGateOff_(i); });
    }
    void setSostenutoPedal(bool down) noexcept {
        sostenutoDown_ = down;
        sostenutoDownByChannel_.fill(down);
        for (int ch = 0; ch < 16; ++ch) {
            vm_.setSostenutoPedal(ch, down, [this](int i){ emitSustainGateOff_(i); });
        }
    }
    void setSostenutoPedal(int channel, bool down) noexcept {
        if (channel < 0 || channel >= 16) { setSostenutoPedal(down); return; }
        sostenutoDownByChannel_[(size_t)channel] = down;
        sostenutoDown_ = std::any_of(sostenutoDownByChannel_.begin(), sostenutoDownByChannel_.end(), [](bool v){ return v; });
        vm_.setSostenutoPedal(channel, down, [this](int i){ emitSustainGateOff_(i); });
    }
    void reset() noexcept {
        vm_.reset();
        for (auto& n : held_) n = {};
        heldCount_ = 0;
        pressOrder_ = 0;
        sustainDown_ = false;
        sostenutoDown_ = false;
        sustainDownByChannel_.fill(false);
        sostenutoDownByChannel_.fill(false);
        playMode_ = PlayMode::Poly;
        notePriority_ = NotePriority::Last;
        stealMode_ = StealMode::Oldest;
        retrigMode_ = RetrigMode::Always;
        unisonCount_ = 1;
        unisonActiveCount_ = 0;
        maxPolyVoices_ = VoiceManager::MAX_VOICES;
        tokenPool_.reset();
    }

    void noteOn(int midiNote, float velocity, int channel, int noteId, VoiceEventBuffer& out) noexcept {
        midiNote = std::clamp(midiNote, 0, 127);
        velocity = std::clamp(std::isfinite(velocity) ? velocity : 0.5f, 0.f, 1.f);
        pushHeld_(midiNote, channel, noteId, velocity);
        switch (playMode_) {
            case PlayMode::Poly: noteOnPoly_(midiNote, velocity, channel, noteId, out); break;
            case PlayMode::Mono: noteOnMono_(midiNote, velocity, channel, noteId, out); break;
            case PlayMode::Legato: noteOnLegato_(midiNote, velocity, channel, noteId, out); break;
            case PlayMode::Unison: noteOnUnison_(midiNote, velocity, channel, noteId, out); break;
        }
    }

    void noteOnWithToken(int midiNote,
                         float velocity,
                         int channel,
                         int noteId,
                         uint64_t voiceToken,
                         VoiceEventBuffer& out) noexcept {
        midiNote = std::clamp(midiNote, 0, 127);
        velocity = std::clamp(std::isfinite(velocity) ? velocity : 0.5f, 0.f, 1.f);
        pushHeldWithToken_(midiNote, channel, noteId, velocity, voiceToken);
        switch (playMode_) {
            case PlayMode::Poly: noteOnPoly_(midiNote, velocity, channel, noteId, out); break;
            case PlayMode::Mono: noteOnMono_(midiNote, velocity, channel, noteId, out); break;
            case PlayMode::Legato: noteOnLegato_(midiNote, velocity, channel, noteId, out); break;
            case PlayMode::Unison: noteOnUnison_(midiNote, velocity, channel, noteId, out); break;
        }
    }

    void noteOff(int midiNote, int channel, int noteId, VoiceEventBuffer& out) noexcept {
        const uint64_t releasedHeldToken = heldTokenForRelease_(midiNote, channel, noteId);
        if (!removeHeldByVoiceToken_(releasedHeldToken)) removeHeld_(midiNote, channel, noteId);
        switch (playMode_) {
            case PlayMode::Poly: {
                // Token-first authoritative release. Resolves the voice by token.
                // FIX 2.2: When the voice was stolen before this note-off arrives
                // (releasedHeldToken valid but already reassigned), noteOffByToken
                // returns voiceIndex=-1. Fall back to the note/channel/noteId scan
                // via noteOffDetailedResult to prevent a stuck gate.
                auto off = vm_.noteOffByToken(releasedHeldToken, midiNote, channel, noteId);
                if (off.voiceIndex < 0 && releasedHeldToken == 0) {
                    // No token was ever assigned (pre-token note) — use identity scan.
                    off = vm_.noteOffDetailedResult(midiNote, channel, noteId);
                } else if (off.voiceIndex < 0 && releasedHeldToken != 0) {
                    // Token present but voice was stolen. Scan for oldest active
                    // voice with matching note to release. This prevents stuck gates
                    // under pathological same-note overlap + heavy polyphony.
                    off = vm_.noteOffDetailedResult(midiNote, channel, noteId);
                }
                if (off.voiceIndex >= 0) {
                    VoiceEvent ev{};
                    ev.kind = VoiceEvent::Kind::GateOff;
                    ev.voiceIdx = off.voiceIndex;
                    ev.midiNote = midiNote;
                    ev.channel = channel;
                    ev.noteId = noteId;
                    ev.voiceToken = off.voiceToken != 0 ? off.voiceToken : releasedHeldToken;
                    out.push(ev);
                }
                break;
            }
            case PlayMode::Mono: noteOffMono_(releasedHeldToken, releasedHeldToken != 0, out); break;
            case PlayMode::Legato: noteOffLegato_(releasedHeldToken, releasedHeldToken != 0, out); break;
            case PlayMode::Unison: noteOffUnison_(releasedHeldToken, releasedHeldToken != 0, out); break;
        }
    }

    void clearAllNotesOffStateOnly() noexcept {
        vm_.allNotesOff();
        for (auto& n : held_) n = {};
        heldCount_ = 0;
        pressOrder_ = 0;
        unisonActiveCount_ = 0;
        sustainDown_ = false;
        sostenutoDown_ = false;
        sustainDownByChannel_.fill(false);
        sostenutoDownByChannel_.fill(false);
    }
    void allNotesOff(VoiceEventBuffer& out) noexcept {
        clearAllNotesOffStateOnly();
        VoiceEvent ev{}; ev.kind = VoiceEvent::Kind::AllOff; out.push(ev);
    }
    void allNotesOffChannel(int channel) noexcept {
        if (channel < 0) {
            clearAllNotesOffStateOnly();
            return;
        }
        vm_.allNotesOffChannel(channel);

        // heldCount_ is maintained by push/remove paths, but GCC14 cannot
        // prove the invariant after the DSP-kernel inline chain. Clamp the
        // compaction span at the boundary so held_[write] is statically and
        // dynamically bounded by kMaxHeldNotes.
        const int boundedHeldCount = std::clamp(heldCount_, 0, kMaxHeldNotes);
        HeldNote compacted[kMaxHeldNotes]{};
        int write = 0;
        for (int read = 0; read < boundedHeldCount; ++read) {
            const HeldNote h = held_[read];
            if (!h.active || h.identity.channel < 0 || h.identity.channel == channel) continue;
            if (write < kMaxHeldNotes) {
                compacted[write++] = h;
            }
        }
        for (int i = 0; i < kMaxHeldNotes; ++i) {
            held_[i] = (i < write) ? compacted[i] : HeldNote{};
        }
        heldCount_ = std::clamp(write, 0, kMaxHeldNotes);
        if (heldCount_ == 0) {
            pressOrder_ = 0;
            unisonActiveCount_ = 0;
            return;
        }
        int liveVoices = 0;
        for (int i = 0; i < VoiceManager::MAX_VOICES; ++i) {
            const auto& st = vm_.getVoiceState(i);
            if (!st.isActive && !st.keyDown && !st.isSustained && !st.isSostenuto) continue;
            ++liveVoices;
        }
        unisonActiveCount_ = std::clamp(liveVoices, 0, VoiceManager::MAX_VOICES);
    }
    void panic(VoiceEventBuffer& out) noexcept {
        sustainDown_ = false;
        sostenutoDown_ = false;
        sustainDownByChannel_.fill(false);
        sostenutoDownByChannel_.fill(false);
        allNotesOff(out);
    }
    VoiceManager& voiceManager() noexcept { return vm_; }
    const VoiceManager& voiceManager() const noexcept { return vm_; }
    void resetOrphanReconcile() noexcept { vm_.resetOrphanReconcile(); }

    template <typename HeldFn, typename PedalFn, typename GateOffFn>
    int reconcileUnheldVoices(HeldFn&& isHeld, PedalFn&& isPedalHeld, GateOffFn&& gateOff) noexcept {
        return vm_.reconcileUnheldVoices(
            std::forward<HeldFn>(isHeld),
            std::forward<PedalFn>(isPedalHeld),
            [this, &gateOff](int voiceIdx) noexcept {
                const auto prev = vm_.getVoiceState(voiceIdx);
                if (!removeHeldByVoiceToken_(prev.voiceToken)) {
                    removeHeld_(prev.midiNote, prev.channel, prev.noteId);
                }
                gateOff(voiceIdx);
            });
    }

    void clearHeldNotes() noexcept {
        for (auto& n : held_) n = {};
        heldCount_ = 0;
        pressOrder_ = 0;
    }
    void trackHeldNoteOn(int midiNote, float velocity, int channel, int noteId) noexcept {
        pushHeld_(midiNote, channel, noteId, velocity);
    }
    void trackHeldNoteOff(int midiNote, int channel, int noteId) noexcept {
        removeHeld_(midiNote, channel, noteId);
    }
    int topHeldNote() const noexcept {
        const HeldNote* h = priorityNote_();
        return h ? h->identity.midi_note : -1;
    }
    const HeldNote* priorityHeldNote() const noexcept { return priorityNote_(); }
    template <class Voices>
    void rebuildHeldFromVoices(int canonicalVoiceMode, const Voices& voices) noexcept {
        clearHeldNotes();
        tokenPool_.reset();
        setPlayMode(sidPlayModeFromCanonicalVoiceMode(canonicalVoiceMode));
        for (const auto& v : voices) {
            if (!v.active) continue;
            pushHeldWithToken_(v.midiNote, v.channel, v.noteId, 1.0f, v.voiceToken);
        }
    }

private:
    VoiceManager vm_{};
    PlayMode playMode_ = PlayMode::Poly;
    NotePriority notePriority_ = NotePriority::Last;
    StealMode stealMode_ = StealMode::Oldest;
    RetrigMode retrigMode_ = RetrigMode::Always;
    int unisonCount_ = 1;
    int unisonActiveCount_ = 0;
    int maxPolyVoices_ = VoiceManager::MAX_VOICES;
    bool sustainDown_ = false;
    bool sostenutoDown_ = false;
    std::array<bool,16> sustainDownByChannel_{};
    std::array<bool,16> sostenutoDownByChannel_{};
    uint32_t pressOrder_ = 0;
    HeldNote held_[kMaxHeldNotes]{};
    int heldCount_ = 0;
    SidVoiceTokenPool tokenPool_{};
    SustainGateOffFn sustainGateOffFn_ = nullptr;
    void* sustainGateOffCtx_ = nullptr;

    void emitSustainGateOff_(int voiceIdx) noexcept {
        if (sustainGateOffFn_) sustainGateOffFn_(sustainGateOffCtx_, voiceIdx);
    }
    // Hard token teardown used only when the held-note ledger itself evicts an
    // entry. This is not a musical NoteOff and must not honor sustain/sostenuto;
    // otherwise an evicted held entry can leave an orphaned pedal-held voice with
    // no identity left in the ledger to ever release it.
    int forceGateOffTokenFromHeldLedger_(uint64_t voiceToken) noexcept {
        if (voiceToken == 0) return 0;
        int released = 0;
        for (int i = 0; i < VoiceManager::MAX_VOICES; ++i) {
            const auto prev = vm_.getVoiceState(i);
            if (!prev.isActive || prev.voiceToken != voiceToken) continue;
            (void)vm_.forceReleaseVoicePedalAware(i, false, false);
            emitSustainGateOff_(i);
            ++released;
        }
        return released;
    }
    bool sustainDownForChannel_(int channel) const noexcept {
        return (channel >= 0 && channel < 16)
            ? sustainDownByChannel_[(size_t)channel]
            : sustainDown_;
    }
    bool releasedVoiceShouldRemainPedalHeld_(const VoiceState& voice) const noexcept {
        const int ch = (voice.channel >= 0 && voice.channel < 16) ? voice.channel : -1;
        return voice.isSostenuto || sustainDownForChannel_(ch);
    }
    bool releaseVoicePedalAware_(int voiceIdx, const VoiceState& prev) noexcept {
        const bool hold = releasedVoiceShouldRemainPedalHeld_(prev);
        (void)vm_.forceReleaseVoicePedalAware(voiceIdx, hold && !prev.isSostenuto, prev.isSostenuto);
        return hold;
    }

    void pushHeldWithToken_(int note, int ch, int nid, float vel, uint64_t token) noexcept {
        // Only exact identity-bearing events dedupe in-place. Anonymous/same-note overlaps
        // must remain distinct held entries so note-off resolves LIFO-correctly.
        if (ch >= 0 && nid >= 0) removeHeld_(note, ch, nid);
        if (heldCount_ >= kMaxHeldNotes) {
            // Before evicting the oldest held entry, emit a GateOff for its
            // voice via the sustain-gate-off callback so the voice manager does not
            // remain gated on with an orphaned token. Without this, the evicted note's
            // SID gate bit stays high and the voice is stuck until a panic.
            int steal = 0;
            uint32_t bestOrder = held_[0].identity.note_on_order;
            for (int i = 1; i < heldCount_; ++i) {
                if (held_[i].identity.note_on_order < bestOrder) {
                    bestOrder = held_[i].identity.note_on_order;
                    steal = i;
                }
            }
            // Hard-release the voice manager entry for the evicted token. This is
            // a ledger-capacity eviction, not a musical note-off, so sustain and
            // sostenuto must not keep the evicted voice alive as an orphan.
            const uint64_t evictedTok = held_[steal].voiceToken;
            (void)forceGateOffTokenFromHeldLedger_(evictedTok);
            for (int j = steal; j < heldCount_ - 1; ++j) held_[j] = held_[j + 1];
            held_[heldCount_ - 1] = {};
            --heldCount_;
        }
        if (heldCount_ < kMaxHeldNotes) {
            auto& h = held_[heldCount_++];
            h.identity.midi_note = static_cast<int16_t>(note);
            h.identity.channel = static_cast<int16_t>(ch);
            h.identity.note_id = nid;
            h.velocity = vel;
            h.identity.note_on_order = ++pressOrder_;
            h.active = true;
            h.voiceToken = (token != 0) ? token : tokenPool_.nextToken();
        }
    }
    void pushHeld_(int note, int ch, int nid, float vel) noexcept {
        pushHeldWithToken_(note, ch, nid, vel, 0);
    }
    bool removeHeldByVoiceToken_(uint64_t voiceToken) noexcept {
        if (voiceToken == 0) return false;
        bool removed = false;
        int write = 0;
        for (int read = 0; read < heldCount_; ++read) {
            const auto& h = held_[read];
            if (h.active && h.voiceToken == voiceToken) {
                removed = true;
                continue;
            }
            if (write != read) held_[write] = held_[read];
            ++write;
        }
        for (int i = write; i < heldCount_; ++i) held_[i] = {};
        heldCount_ = write;
        if (heldCount_ == 0) pressOrder_ = 0;
        return removed;
    }

    void removeHeld_(int note, int ch, int nid) noexcept {
        int best = -1;
        uint32_t bestOrder = 0;
        for (int i=0;i<heldCount_;++i) {
            auto& h = held_[i];
            if (!h.active || h.identity.midi_note != note) continue;
            if (ch >= 0 && h.identity.channel >= 0 && h.identity.channel != ch) continue;
            if (nid >= 0) {
                if (h.identity.note_id != nid) continue;
            } else {
                // Legacy no-token fallback (see removeHeldByVoiceToken_ callers):
                // reached only when an anonymous held entry carries no voice token.
                // It removes the OLDEST anonymous held entry (FIFO), matching the
                // identity-scan gate release (noteOffDetailedResult releases the
                // oldest key-down voice), and must not consume a real host-noteId
                // held entry. The primary token path is LIFO — see the pinned
                // anonymous release policy on heldTokenForRelease_ below.
                if (h.identity.note_id >= 0) continue;
            }
            if (best < 0 || h.identity.note_on_order < bestOrder) {
                best = i;
                bestOrder = h.identity.note_on_order;
            }
        }
        if (best < 0) return;
        for (int j=best;j<heldCount_-1;++j) held_[j] = held_[j+1];
        held_[--heldCount_] = {};
    }
    HeldNote* priorityNote_() noexcept {
        if (heldCount_ == 0) return nullptr;
        HeldNote* best = nullptr;
        for (int i=0;i<heldCount_;++i) {
            if (!held_[i].active) continue;
            if (!best) { best=&held_[i]; continue; }
            switch (notePriority_) {
                case NotePriority::Last: if (held_[i].identity.note_on_order > best->identity.note_on_order) best=&held_[i]; break;
                case NotePriority::High: if (held_[i].identity.midi_note > best->identity.midi_note) best=&held_[i]; break;
                case NotePriority::Low: if (held_[i].identity.midi_note < best->identity.midi_note) best=&held_[i]; break;
            }
        }
        return best;
    }
    const HeldNote* priorityNote_() const noexcept {
        if (heldCount_ == 0) return nullptr;
        const HeldNote* best = nullptr;
        for (int i=0;i<heldCount_;++i) {
            if (!held_[i].active) continue;
            if (!best) { best=&held_[i]; continue; }
            switch (notePriority_) {
                case NotePriority::Last: if (held_[i].identity.note_on_order > best->identity.note_on_order) best=&held_[i]; break;
                case NotePriority::High: if (held_[i].identity.midi_note > best->identity.midi_note) best=&held_[i]; break;
                case NotePriority::Low: if (held_[i].identity.midi_note < best->identity.midi_note) best=&held_[i]; break;
            }
        }
        return best;
    }
    // Helper: fetch token from held table by identity; returns 0 if not found.
    uint64_t heldTokenFor_(int note, int ch, int nid) const noexcept {
        return heldTokenForOrder_(note, ch, nid, false);
    }
    uint64_t newestHeldTokenFor_(int note, int ch, int nid) const noexcept {
        return heldTokenForOrder_(note, ch, nid, true);
    }
    // ── Pinned anonymous same-note release policy (v872 P1-8) ──────────────────
    // A NoteOff without a host note id (nid < 0) releases the NEWEST matching
    // anonymous voice: LIFO / last-note priority. This is deliberate and pinned:
    //   * voiceIdentityMatches() refuses to retrigger for nid < 0, so anonymous
    //     same-note NoteOns always allocate independent physical voices;
    //   * the gate release (noteOffByToken) and the held-ledger removal
    //     (removeHeldByVoiceToken_) both use this SAME newest token, so they can
    //     never target different voices — no stuck tail under same-note overlap.
    // The legacy no-token fallback (removeHeld_ + noteOffDetailedResult) is FIFO
    // (oldest); it is order-consistent within itself and only runs for pre-token
    // held entries. See synthmode_anonymous_release_lifo_v872_tests.
    uint64_t heldTokenForRelease_(int note, int ch, int nid) const noexcept {
        const bool newestAnonymous = (nid < 0);
        return heldTokenForOrder_(note, ch, nid, newestAnonymous);
    }
    uint64_t heldTokenForOrder_(int note, int ch, int nid, bool newestAnonymous) const noexcept {
        uint64_t bestTok = 0;
        uint32_t bestOrder = 0;
        for (int i = 0; i < heldCount_; ++i) {
            const auto& h = held_[i];
            if (!h.active || h.identity.midi_note != note) continue;
            if (ch >= 0 && h.identity.channel >= 0 && h.identity.channel != ch) continue;
            if (nid >= 0) {
                if (h.identity.note_id != nid) continue;
            } else {
                // NoteId-less NoteOff scans anonymous held notes only; real
                // host-noteId voices are released only by their exact ID. The
                // scan ORDER is parameterized by newestAnonymous: true = newest
                // (LIFO, the pinned anonymous release policy), false = oldest
                // (FIFO, used only by heldTokenFor_ for identified lookups).
                if (h.identity.note_id >= 0) continue;
            }
            const bool better = bestTok == 0 ||
                (newestAnonymous ? (h.identity.note_on_order > bestOrder)
                                 : (h.identity.note_on_order < bestOrder));
            if (better) {
                bestTok = h.voiceToken;
                bestOrder = h.identity.note_on_order;
            }
        }
        return bestTok;
    }
    int selectStealVoiceIndex_() const noexcept {
        auto classify = [](const VoiceState& v) noexcept {
            if (!v.isActive) return 0;
            if (!v.keyDown && !v.isSustained && !v.isSostenuto) return 3; // best steal candidate: fully released tail
            if (!v.isSustained && !v.isSostenuto) return 2;                 // next: actively held with no pedal latch
            return 1;                                                       // last resort: pedal-latched
        };
        auto score = [this](const VoiceState& v) noexcept {
            switch (stealMode_) {
                case StealMode::Quietest:
                    return v.velocity;
                case StealMode::LowestEnergy:
                    // Approximate energy from velocity and held state. Lower is safer to steal.
                    return v.velocity * (v.keyDown ? 1.0f : 0.5f) * ((v.isSustained || v.isSostenuto) ? 1.25f : 1.0f);
                case StealMode::Oldest:
                default:
                    return -v.age; // older => smaller score => preferred below
            }
        };

        int best = -1;
        int bestClass = -1;
        float bestScore = 0.0f;
        float bestAge = -1.0f;
        for (int i = 0; i < maxPolyVoices_; ++i) {
            const auto& v = vm_.getVoiceState(i);
            if (!v.isActive) continue;
            const int cls = classify(v);
            const float sc = score(v);
            const bool better =
                (best < 0) ||
                (cls > bestClass) ||
                (cls == bestClass && sc < bestScore) ||
                (cls == bestClass && std::fabs(sc - bestScore) <= 1.0e-6f && v.age > bestAge);
            if (better) {
                best = i;
                bestClass = cls;
                bestScore = sc;
                bestAge = v.age;
            }
        }
        return best;
    }

    void noteOnPoly_(int note, float vel, int ch, int nid, VoiceEventBuffer& out) noexcept {
        const uint64_t tok = newestHeldTokenFor_(note, ch, nid);

        for (int i = 0; i < maxPolyVoices_; ++i) {
            const auto& voice = vm_.getVoiceState(i);
            if (!VoiceManager::voiceIdentityMatches(voice, note, ch, nid)) continue;
            vm_.forceAssignVoice(i, note, vel, ch, nid, tok);
            VoiceEvent ev{};
            ev.kind = VoiceEvent::Kind::Retrigger;
            ev.voiceIdx = i;
            ev.midiNote = note; ev.velocity = vel; ev.channel = ch; ev.noteId = nid;
            ev.retrigged = true;
            ev.voiceToken = tok;
            out.push(ev);
            return;
        }

        for (int i = 0; i < maxPolyVoices_; ++i) {
            const auto& voice = vm_.getVoiceState(i);
            if (voice.isActive) continue;
            vm_.forceAssignVoice(i, note, vel, ch, nid, tok);
            VoiceEvent ev{};
            ev.kind = VoiceEvent::Kind::Start;
            ev.voiceIdx = i;
            ev.midiNote = note; ev.velocity = vel; ev.channel = ch; ev.noteId = nid;
            ev.voiceToken = tok;
            out.push(ev);
            return;
        }

        const int stolen = selectStealVoiceIndex_();
        if (stolen < 0) return;
        const auto stolenVoice = vm_.getVoiceState(stolen);
        // Voice stealing is a hard identity transfer. The old token no longer
        // owns a live voice after this point, so remove any held-ledger entry
        // for it before assigning the new note. Otherwise a later NoteOff for
        // the stolen note can gate the wrong current voice or remain eligible
        // for mono/unison priority replay.
        (void)removeHeldByVoiceToken_(stolenVoice.voiceToken);
        emitSustainGateOff_(stolen);
        vm_.forceAssignVoice(stolen, note, vel, ch, nid, tok);

        VoiceEvent off{};
        off.kind = VoiceEvent::Kind::GateOff;
        off.voiceIdx = stolen;
        off.midiNote = stolenVoice.midiNote;
        off.channel = stolenVoice.channel;
        off.noteId = stolenVoice.noteId;
        off.voiceToken = stolenVoice.voiceToken;
        out.push(off);

        VoiceEvent on{};
        on.kind = VoiceEvent::Kind::Start;
        on.voiceIdx = stolen;
        on.midiNote = note; on.velocity = vel; on.channel = ch; on.noteId = nid;
        on.voiceToken = tok;
        out.push(on);
    }
    void noteOnMono_(int note, float vel, int ch, int nid, VoiceEventBuffer& out) noexcept {
        const auto prev = vm_.getVoiceState(0);
        const bool wasPlaying = prev.isActive && prev.keyDown;
        const bool sameIdentity = VoiceManager::voiceIdentityMatches(prev, note, ch, nid);
        const uint64_t tok = newestHeldTokenFor_(note, ch, nid);
        vm_.forceAssignVoice(0, note, vel, ch, nid, tok);
        VoiceEvent ev{};
        if (!wasPlaying) {
            ev.kind = VoiceEvent::Kind::Start;
        } else if (sameIdentity) {
            ev.kind = VoiceEvent::Kind::Retrigger;
        } else {
            switch (retrigMode_) {
                case RetrigMode::Always:
                case RetrigMode::OnNewNote:
                    ev.kind = VoiceEvent::Kind::Retrigger;
                    break;
                case RetrigMode::Never:
                    ev.kind = VoiceEvent::Kind::Glide;
                    break;
            }
        }
        ev.voiceIdx = 0; ev.midiNote = note; ev.velocity = vel; ev.channel = ch; ev.noteId = nid;
        ev.retrigged = (ev.kind == VoiceEvent::Kind::Retrigger);
        ev.voiceToken = tok;
        out.push(ev);
    }
    void noteOffMono_(uint64_t releasedToken, bool releasedHeldMatched, VoiceEventBuffer& out) noexcept {
        // FIX: Only act if the released note was the currently sounding mono voice.
        // Previously, any held-note release triggered a voice rewrite and Retrigger/Glide
        // event, even if the sounding voice was a different (higher/lower priority) note.
        // Now: if the released token does NOT match voice 0's current token, the release
        // is purely a held-state removal — voice 0 keeps playing undisturbed.
        const auto prev = vm_.getVoiceState(0);
        const bool releasedWasSounding =
            releasedHeldMatched &&
            ((releasedToken != 0 && prev.voiceToken != 0 && releasedToken == prev.voiceToken) ||
             (releasedToken == 0 && prev.isActive && prev.keyDown));
        if (!releasedWasSounding) {
            // Released note was not the authoritative voice — do nothing to the hardware voice.
            return;
        }
        HeldNote* next = priorityNote_();
        if (next) {
            const bool sameIdentity = VoiceManager::voiceIdentityMatches(prev, next->identity.midi_note, next->identity.channel, next->identity.note_id);
            vm_.forceAssignVoice(0, next->identity.midi_note, next->velocity, next->identity.channel, next->identity.note_id, next->voiceToken);
            VoiceEvent ev{};
            ev.kind = sameIdentity ? VoiceEvent::Kind::Retrigger : VoiceEvent::Kind::Glide;
            ev.voiceIdx = 0;
            ev.midiNote = next->identity.midi_note; ev.velocity = next->velocity;
            ev.channel = next->identity.channel; ev.noteId = next->identity.note_id;
            ev.voiceToken = next->voiceToken;
            out.push(ev);
        } else {
            if (releaseVoicePedalAware_(0, prev)) return;
            VoiceEvent ev{}; ev.kind = VoiceEvent::Kind::GateOff; ev.voiceIdx = 0;
            ev.voiceToken = (releasedToken != 0 ? releasedToken : prev.voiceToken);
            out.push(ev);
        }
    }
    void noteOnLegato_(int note, float vel, int ch, int nid, VoiceEventBuffer& out) noexcept {
        const bool gateAlreadyOpen = (heldCount_ > 1);
        const uint64_t tok = newestHeldTokenFor_(note, ch, nid);
        vm_.forceAssignVoice(0, note, vel, ch, nid, tok);
        VoiceEvent ev{};
        ev.kind = gateAlreadyOpen ? VoiceEvent::Kind::Glide : VoiceEvent::Kind::Start;
        ev.voiceIdx = 0; ev.midiNote = note; ev.velocity = vel; ev.channel = ch; ev.noteId = nid;
        ev.voiceToken = tok;
        out.push(ev);
    }
    void noteOffLegato_(uint64_t releasedToken, bool releasedHeldMatched, VoiceEventBuffer& out) noexcept { noteOffMono_(releasedToken, releasedHeldMatched, out); }
    void noteOnUnison_(int note, float vel, int ch, int nid, VoiceEventBuffer& out) noexcept {
        const int n = std::min(unisonCount_, VoiceManager::MAX_VOICES);
        const uint64_t tok = newestHeldTokenFor_(note, ch, nid);
        const int active = std::max(unisonActiveCount_, 0);
        for (int v = n; v < active; ++v) {
            const auto prev = vm_.getVoiceState(v);
            if (!prev.isActive && !prev.keyDown && !prev.isSustained && !prev.isSostenuto) continue;
            (void)vm_.forceReleaseVoice(v, false);
            VoiceEvent off{};
            off.kind = VoiceEvent::Kind::GateOff;
            off.voiceIdx = v;
            off.voiceToken = prev.voiceToken;
            out.push(off);
        }
        for (int v=0; v<n; ++v) {
            const auto prev = vm_.getVoiceState(v);
            const bool sameIdentity = VoiceManager::voiceIdentityMatches(prev, note, ch, nid);
            const bool wasOpen = prev.isActive || prev.keyDown || prev.isSustained || prev.isSostenuto;
            vm_.forceAssignVoice(v, note, vel, ch, nid, tok);
            VoiceEvent ev{};
            ev.kind = !wasOpen ? VoiceEvent::Kind::Start
                               : (sameIdentity ? VoiceEvent::Kind::Retrigger : VoiceEvent::Kind::Glide);
            ev.voiceIdx = v;
            ev.midiNote = note; ev.velocity = vel; ev.channel = ch; ev.noteId = nid;
            ev.voiceToken = tok;
            out.push(ev);
        }
        unisonActiveCount_ = n;
    }
    void noteOffUnison_(uint64_t releasedToken, bool releasedHeldMatched, VoiceEventBuffer& out) noexcept {
        if (!releasedHeldMatched) return;
        bool releasedWasSounding = false;
        if (releasedToken != 0) {
            const int activeScan = std::max(unisonActiveCount_, 0);
            for (int v = 0; v < activeScan && v < VoiceManager::MAX_VOICES; ++v) {
                const auto& prev = vm_.getVoiceState(v);
                if (prev.isActive && prev.voiceToken == releasedToken) { releasedWasSounding = true; break; }
            }
        }
        if (!releasedWasSounding) return;
        HeldNote* next = priorityNote_();
        const int targetCount = std::min(unisonCount_, VoiceManager::MAX_VOICES);
        const int active = std::max(unisonActiveCount_, targetCount);
        if (next) {
            for (int v = targetCount; v < active; ++v) {
                const auto prev = vm_.getVoiceState(v);
                if (!prev.isActive && !prev.keyDown && !prev.isSustained && !prev.isSostenuto) continue;
                (void)vm_.forceReleaseVoice(v, false);
                VoiceEvent off{};
                off.kind = VoiceEvent::Kind::GateOff;
                off.voiceIdx = v;
                off.voiceToken = prev.voiceToken;
                out.push(off);
            }
            for (int v=0; v<targetCount; ++v) {
                const auto prev = vm_.getVoiceState(v);
                const bool sameIdentity = VoiceManager::voiceIdentityMatches(prev, next->identity.midi_note, next->identity.channel, next->identity.note_id);
                vm_.forceAssignVoice(v, next->identity.midi_note, next->velocity, next->identity.channel, next->identity.note_id, next->voiceToken);
                VoiceEvent ev{};
                ev.kind = sameIdentity ? VoiceEvent::Kind::Retrigger : VoiceEvent::Kind::Glide;
                ev.voiceIdx = v;
                ev.midiNote = next->identity.midi_note; ev.velocity = next->velocity;
                ev.channel = next->identity.channel; ev.noteId = next->identity.note_id;
                ev.voiceToken = next->voiceToken;
                out.push(ev);
            }
            unisonActiveCount_ = targetCount;
        } else {
            int stillHeldByPedal = 0;
            for (int v=0; v<active; ++v) {
                const auto prev = vm_.getVoiceState(v);
                if (releasedToken != 0 && prev.voiceToken != releasedToken) {
                    if (prev.isActive || prev.keyDown || prev.isSustained || prev.isSostenuto) ++stillHeldByPedal;
                    continue;
                }
                if (releasedToken == 0 && !prev.isActive && !prev.keyDown && !prev.isSustained && !prev.isSostenuto) continue;
                if (releaseVoicePedalAware_(v, prev)) {
                    ++stillHeldByPedal;
                    continue;
                }
                VoiceEvent ev{};
                ev.kind = VoiceEvent::Kind::GateOff; ev.voiceIdx = v;
                ev.voiceToken = (releasedToken != 0 ? releasedToken : prev.voiceToken);
                out.push(ev);
            }
            unisonActiveCount_ = std::clamp(stillHeldByPedal, 0, VoiceManager::MAX_VOICES);
        }
    }
};

inline PlayMode sidPlayModeFromCanonicalVoiceMode(int mode) noexcept {
    switch (mode) {
        case 1: return PlayMode::Mono;
        case 2: return PlayMode::Legato;
        case 3: return PlayMode::Unison;
        default: return PlayMode::Poly;
    }
}

} // namespace ArpSID
