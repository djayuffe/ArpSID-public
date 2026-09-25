#pragma once
#include "sid_dynamic_state.h"
#include "sid_event_queue.h"
#include "sid_runtime_event_materializer.h"
#include "sid_ingress_lane.h"
#include <cstdint>
#include <algorithm>

// CANONICAL HELD REPLAY TRUTH (Pass N).
// Held journal replay, overlap-depth replay, token replay, replay ordering, and
// restore/apply-state reseed law all live here.
// Neither AU nor VST wrapper may own these laws. Both call into here.
//
// Invariants:
// - Token-aware replay: each overlapping same-note voice gets its own token.
// - Replay events are pushed to merge lanes (primary ingress), not direct queue.
// - Overlap-depth is bounded by the token voice table capacity.

namespace ArpSID {

// Descriptor for one held-note replay entry (AU or VST held journal entry).
struct SidHeldReplayEntry {
    int16_t  channel   = -1;
    int16_t  note      = -1;
    int32_t  noteId    = -1;
    uint8_t  velocity7 = 0;     // 0–127
    uint32_t arrivalOrder = 0;  // original press order for replay ordering
    bool     active    = false;
};

static constexpr int kMaxHeldReplayEntries = 128;

// Replay a held-note journal into a model's merge lanes.
// Each entry gets a distinct canonical token via the runtime-model token bridge.
// Replay ordering preserves original arrivalOrder.
// [canonical] Wrappers call this at restore/reseed; they must not build their own replay.
template <class RuntimeModel>
inline int sidReplayHeldNotes(RuntimeModel& model,
                               const SidHeldReplayEntry* entries,
                               int count,
                               int frameCount) noexcept {
    if (!entries || count <= 0) return 0;

    // Sort by arrivalOrder to preserve original press ordering.
    // Use a small local scratch — stack alloc is fine for bounded count.
    constexpr int kMax = kMaxHeldReplayEntries;
    const int n = std::min(count, kMax);
    int order[kMax]{};
    for (int i = 0; i < n; ++i) order[i] = i;
    // ARPSID_RT_SORT_CLASSIFICATION: restore/reseed boundary, fixed 128-entry
    // stack index array, scalar comparator, no allocation.
    std::sort(order, order + n, [&](int a, int b) noexcept {
        return entries[a].arrivalOrder < entries[b].arrivalOrder;
    });

    int replayed = 0;

    for (int si = 0; si < n; ++si) {
        const auto& e = entries[order[si]];
        if (!e.active || e.channel < 0 || e.note < 0) continue;

        const float vel = std::clamp(static_cast<float>(e.velocity7) / 127.f, 0.f, 1.f);

        // Bind a new canonical token for this replay voice.
        const uint64_t tok = model.bindVoiceTokenBridge(
            e.channel, e.note, e.noteId, vel, e.arrivalOrder);
        if (tok == 0) break;  // token table full — stop replay

        // Emit note-on into merge lane (MIDI priority band).
        SidTimedEvent ev = sidMaterializeNoteOn(
            e.channel, e.note, vel, e.noteId, kSidUnresolvedSampleOffset);
        // Tag token on the event so downstream scheduler can stamp it.
        ev.arrival_order = e.arrivalOrder;
        ev.voiceToken = tok;
        model.pushToLane(ev, SidIngressSourcePriority::MidiNoteControl, frameCount);
        ++replayed;
    }
    return replayed;
}

// Replay a single held-note entry with an explicit token.
// Used when the host journal already has a recorded token (AU held lane replay).
template <class RuntimeModel>
inline bool sidReplayHeldNoteWithToken(RuntimeModel& model,
                                        const SidHeldReplayEntry& e,
                                        uint64_t knownToken,
                                        int frameCount) noexcept {
    if (!e.active || e.channel < 0 || e.note < 0) return false;
    const float vel = std::clamp(static_cast<float>(e.velocity7) / 127.f, 0.f, 1.f);
    // Re-bind with the known token if the slot is free; otherwise allocate fresh.
    uint64_t tok = knownToken;
    if (tok == 0 || model.hasActiveVoiceToken(tok)) {
        tok = model.bindVoiceTokenBridge(e.channel, e.note, e.noteId, vel, e.arrivalOrder);
    } else {
        tok = model.bindVoiceTokenBridgeWithToken(e.channel, e.note, e.noteId, vel, e.arrivalOrder, tok);
    }
    if (tok == 0) return false;
    SidTimedEvent ev = sidMaterializeNoteOn(e.channel, e.note, vel, e.noteId,
                                             kSidUnresolvedSampleOffset);
    ev.arrival_order = e.arrivalOrder;
    ev.voiceToken = tok;
    return model.pushToLane(ev, SidIngressSourcePriority::MidiNoteControl, frameCount);
}

// All-notes-off replay cleanup: emit note-off for every active token voice,
// then clear both the token registry and the legacy identity table atomically.
// [canonical] Keeps both tables in sync — no divergence.
template <class RuntimeModel>
inline int sidReplayAllNotesOff(RuntimeModel& model, int frameCount) noexcept {
    int count = 0;
    model.forEachActiveTokenVoice([&](const auto& e) noexcept {
        SidTimedEvent ev = sidMaterializeNoteOff(
            e.token.channel, e.token.note, e.token.noteId, kSidUnresolvedSampleOffset);
        ev.voiceToken = e.token.token;
        model.pushToLane(ev, SidIngressSourcePriority::SystemSafety, frameCount);
        ++count;
    });
    // Clear both tables together for consistency.
    model.clearAllCanonicalVoiceState();
    model.clearIdentityMirrors();
    return count;
}

} // namespace ArpSID
