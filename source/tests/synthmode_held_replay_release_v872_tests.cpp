// synthmode_held_replay_release_v872_tests.cpp
//
// v872 P1-7 — pins the canonical held-note replay + release law that the audit's
// guitar-patch-replay stuck-note concern depends on. sid_runtime_held_replay.h is
// the single source of truth ("Neither AU nor VST wrapper may own these laws"),
// so the AU kernel's "apply patch/state while a note is held -> replay held notes"
// path and the raw-MIDI release path both flow through here.
//
// This drives the canonical replay against a minimal model and proves:
//   * held notes replay as NoteOns in original press order, each with a distinct
//     non-zero token (independent voices — the property that stops same-note
//     overlaps collapsing into one stuck voice);
//   * an all-notes-off / release pass emits a NoteOff for every replayed voice and
//     clears the canonical voice state — no stuck gate survives a held replay.
//
// The full factory "Overdriven/Distortion Guitar" patch-apply-while-held chain is
// an AU-kernel (plugin-build) behavior; the allocator-side release guarantees it
// relies on are additionally covered by synthmode_anonymous_release_lifo_v872 and
// synthmode_held_replay_identity_v869.

#include "arpsid/core/sid_runtime_held_replay.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ArpSID;

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "synthmode_held_replay_release_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

// Minimal RuntimeModel satisfying the canonical replay template contract.
struct MockReplayModel {
    struct Tok { int channel = -1; int note = -1; int noteId = -1; uint64_t token = 0; };
    struct ActiveVoice { Tok token; };
    std::vector<ActiveVoice> activeVoices;
    std::vector<SidTimedEvent> lane;
    uint64_t nextToken = 500u;

    uint64_t bindVoiceTokenBridge(int ch, int note, int nid, float, uint32_t) noexcept {
        const uint64_t t = nextToken++;
        activeVoices.push_back(ActiveVoice{ Tok{ ch, note, nid, t } });
        return t;
    }
    bool pushToLane(const SidTimedEvent& ev, SidIngressSourcePriority, int) noexcept {
        lane.push_back(ev);
        return true;
    }
    template <class F>
    void forEachActiveTokenVoice(F&& f) noexcept {
        for (auto& v : activeVoices) f(v);
    }
    void clearAllCanonicalVoiceState() noexcept { activeVoices.clear(); }
    void clearIdentityMirrors() noexcept {}
};

void testHeldReplayThenReleaseLeavesNoStuckVoice() {
    // Three held keys captured in a scrambled buffer order, but with press
    // (arrival) orders 0,1,2 for notes 60,62,64.
    SidHeldReplayEntry entries[3];
    entries[0] = SidHeldReplayEntry{ /*channel=*/1, /*note=*/64, /*noteId=*/-1, /*vel7=*/100, /*arrivalOrder=*/2u, /*active=*/true };
    entries[1] = SidHeldReplayEntry{ 1, 60, -1, 100, 0u, true };
    entries[2] = SidHeldReplayEntry{ 1, 62, -1, 100, 1u, true };

    MockReplayModel m;
    const int replayed = sidReplayHeldNotes(m, entries, 3, /*frameCount=*/512);
    require(replayed == 3, "all three held notes must replay");
    require(m.lane.size() == 3, "replay must emit one NoteOn per held note");

    // Ordered by press order, not buffer order: 60, 62, 64.
    require(m.lane[0].type == SidTimedEventType::MidiNoteOn && m.lane[0].pitch == 60,
            "replay must preserve press order (first press first)");
    require(m.lane[1].pitch == 62, "replay order note 2");
    require(m.lane[2].pitch == 64, "replay order note 3");

    // Distinct, non-zero tokens => independent voices.
    require(m.lane[0].voiceToken != 0 && m.lane[1].voiceToken != 0 && m.lane[2].voiceToken != 0,
            "each replayed voice must carry a non-zero token");
    require(m.lane[0].voiceToken != m.lane[1].voiceToken &&
            m.lane[1].voiceToken != m.lane[2].voiceToken &&
            m.lane[0].voiceToken != m.lane[2].voiceToken,
            "replayed same-note-overlap voices must have distinct tokens");
    require(m.activeVoices.size() == 3, "three canonical token voices must be live after replay");

    // Release pass: every replayed voice gets a NoteOff and canonical state clears.
    m.lane.clear();
    const int releasedOff = sidReplayAllNotesOff(m, /*frameCount=*/512);
    require(releasedOff == 3, "release must emit a NoteOff for every replayed voice");
    require(m.lane.size() == 3, "release must push three NoteOff events");
    for (const auto& ev : m.lane)
        require(ev.type == SidTimedEventType::MidiNoteOff, "release events must be NoteOff");
    require(m.activeVoices.empty(),
            "canonical voice state must be empty after release — no stuck held voice");
}

} // namespace

int main() {
    testHeldReplayThenReleaseLeavesNoStuckVoice();
    std::cout << "synthmode_held_replay_release_v872_tests PASS\n";
    return 0;
}
