// synthmode_anonymous_release_lifo_v872_tests.cpp
//
// v872 P1-8 — pins the anonymous same-note release policy so it cannot silently
// drift, and proves it leaves no stuck gate. The policy (documented on
// VoiceAllocator::heldTokenForRelease_) is:
//
//   * anonymous same-note NoteOns (nid < 0) allocate INDEPENDENT voices
//     (voiceIdentityMatches refuses to retrigger for nid < 0);
//   * an anonymous NoteOff releases the NEWEST matching voice (LIFO / last-note
//     priority), with the gate release and held-ledger removal using the SAME
//     token so they can never target different voices;
//   * repeated anonymous NoteOffs release every voice — no stuck tail.

#include "arpsid/core/sid_runtime_voice_policy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace ArpSID;

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "synthmode_anonymous_release_lifo_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

// Return the voiceToken of the single GateOff event in the buffer (0 if none).
uint64_t gateOffToken(const VoiceEventBuffer& out) {
    uint64_t tok = 0;
    int count = 0;
    for (int i = 0; i < out.count; ++i) {
        if (out.events[i].kind == VoiceEvent::Kind::GateOff) { tok = out.events[i].voiceToken; ++count; }
    }
    require(count == 1, "each anonymous NoteOff must emit exactly one GateOff");
    return tok;
}

void testAnonymousReleaseIsLifoAndLeavesNoStuckGate() {
    VoiceAllocator va;                 // constructor resets to Poly / Last priority
    va.setPlayMode(PlayMode::Poly);

    constexpr int note = 60;
    constexpr int ch = 0;
    constexpr int anon = -1;           // anonymous (no host note id)
    constexpr uint64_t T1 = 1001u;     // first (older) press
    constexpr uint64_t T2 = 1002u;     // second (newer) press

    VoiceEventBuffer on1{}, on2{};
    va.noteOnWithToken(note, 0.8f, ch, anon, T1, on1);
    va.noteOnWithToken(note, 0.8f, ch, anon, T2, on2);

    // Two anonymous same-note presses must occupy two independent voices.
    require(on1.count > 0 && on2.count > 0, "each anonymous NoteOn must produce a voice event");
    require(on1.events[0].voiceIdx != on2.events[0].voiceIdx,
            "anonymous same-note NoteOns must allocate independent voices (no retrigger collapse)");

    // First anonymous NoteOff releases the NEWEST voice (LIFO): token T2.
    VoiceEventBuffer off1{};
    va.noteOff(note, ch, anon, off1);
    require(gateOffToken(off1) == T2,
            "anonymous NoteOff must release the newest same-note voice (LIFO / last-note priority)");

    // Second anonymous NoteOff releases the remaining (older) voice: token T1.
    // Proves no stuck gate — both voices are released by anonymous NoteOffs.
    VoiceEventBuffer off2{};
    va.noteOff(note, ch, anon, off2);
    require(gateOffToken(off2) == T1,
            "second anonymous NoteOff must release the older same-note voice, leaving no stuck gate");

    // A third anonymous NoteOff has nothing to release.
    VoiceEventBuffer off3{};
    va.noteOff(note, ch, anon, off3);
    bool anyGateOff = false;
    for (int i = 0; i < off3.count; ++i)
        if (off3.events[i].kind == VoiceEvent::Kind::GateOff) anyGateOff = true;
    require(!anyGateOff, "a NoteOff with no remaining held voice must not emit a spurious GateOff");
}

void testAnonymousNoteOffCannotStealIdentifiedVoice() {
    // Guards the pinned boundary: an anonymous NoteOff releases only anonymous
    // voices; a real host-noteId voice for the same note stays held.
    VoiceAllocator va;
    va.setPlayMode(PlayMode::Poly);

    constexpr int note = 64;
    constexpr int ch = 0;
    VoiceEventBuffer onId{}, onAnon{};
    va.noteOnWithToken(note, 0.8f, ch, /*noteId=*/7, /*token=*/2001u, onId);
    va.noteOnWithToken(note, 0.8f, ch, /*noteId=*/-1, /*token=*/2002u, onAnon);

    VoiceEventBuffer offAnon{};
    va.noteOff(note, ch, /*noteId=*/-1, offAnon);
    require(gateOffToken(offAnon) == 2002u,
            "anonymous NoteOff must release the anonymous voice, not the id-bearing one");

    // The identified voice is still held: releasing it by its exact id works.
    VoiceEventBuffer offId{};
    va.noteOff(note, ch, /*noteId=*/7, offId);
    require(gateOffToken(offId) == 2001u,
            "identified voice must remain held until released by its exact note id");
}

} // namespace

int main() {
    testAnonymousReleaseIsLifoAndLeavesNoStuckGate();
    testAnonymousNoteOffCannotStealIdentifiedVoice();
    std::cout << "synthmode_anonymous_release_lifo_v872_tests PASS\n";
    return 0;
}
