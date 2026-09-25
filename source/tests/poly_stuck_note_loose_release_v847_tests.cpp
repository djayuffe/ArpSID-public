// poly_stuck_note_loose_release_v847_tests.cpp
//
// Guards the v847 poly stuck-note fix. The strict note-off matcher
// (noteOffDetailedResult) deliberately refuses to let an anonymous NoteOff
// (noteId<0) release a voice that carries a real host noteId. That is correct for
// same-note polyphony but leaves a STUCK GATE when a host/AU path does not
// round-trip note ids symmetrically (note-on with an id, note-off without).
//
// BitPerfect's poly note-off now falls back to VoiceManager::noteOffLooseSameNote()
// when the strict match finds nothing, releasing the oldest still-key-down voice
// for the same note+channel while still honouring the sustain pedal. This test
// pins that fallback and its guardrails.

#include "arpsid/engines/voice_manager.h"

#include <cstdio>

using namespace ArpSID;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

int main() {
    // A) The asymmetric-noteId stuck note: on with id=5, off anonymous.
    {
        VoiceManager vm;
        vm.noteOnWithToken(60, 1.0f, /*ch*/0, /*noteId*/5, /*token*/111u, [](int){});

        const auto strict = vm.noteOffDetailedResult(60, /*ch*/0, /*noteId*/-1);
        require(strict.voiceIndex < 0,
                "strict matcher refuses to release an id-bearing voice for an anonymous note-off");

        const auto loose = vm.noteOffLooseSameNote(60, /*ch*/0);
        require(loose.voiceIndex >= 0,
                "loose fallback releases the otherwise-stuck id-bearing voice");
        require(!vm.getVoiceState(loose.voiceIndex).keyDown,
                "loosely-released voice is no longer key-down");
    }

    // B) The loose fallback must NOT release a pedal-held voice.
    {
        VoiceManager vm;
        vm.noteOnWithToken(64, 1.0f, /*ch*/0, /*noteId*/7, /*token*/222u, [](int){});
        vm.setSustainPedal(0, true, [](int){});
        // Key-up while the pedal is down: strict match latches the voice sustained.
        (void)vm.noteOffDetailedResult(64, /*ch*/0, /*noteId*/7);
        const auto loose = vm.noteOffLooseSameNote(64, /*ch*/0);
        require(loose.voiceIndex < 0,
                "loose fallback does not force-release a pedal-held (sustained) voice");
    }

    // C) The loose fallback is scoped to the same note and channel.
    {
        VoiceManager vm;
        vm.noteOnWithToken(60, 1.0f, /*ch*/1, /*noteId*/9, /*token*/333u, [](int){});
        require(vm.noteOffLooseSameNote(62, /*ch*/1).voiceIndex < 0,
                "loose fallback ignores a different note");
        require(vm.noteOffLooseSameNote(60, /*ch*/2).voiceIndex < 0,
                "loose fallback ignores a different channel");
        require(vm.noteOffLooseSameNote(60, /*ch*/1).voiceIndex >= 0,
                "loose fallback releases the matching note+channel");
    }

    // D) When the strict path already matches, no stuck note ever exists.
    {
        VoiceManager vm;
        vm.noteOnWithToken(72, 1.0f, /*ch*/0, /*noteId*/3, /*token*/444u, [](int){});
        const auto strict = vm.noteOffDetailedResult(72, /*ch*/0, /*noteId*/3);
        require(strict.voiceIndex >= 0, "exact note-off releases via the strict path");
        // Nothing left key-down to loosely release.
        require(vm.noteOffLooseSameNote(72, /*ch*/0).voiceIndex < 0,
                "no residual key-down voice after a clean strict release");
    }

    if (g_failures == 0) {
        std::printf("poly_stuck_note_loose_release_v847_tests: PASS\n");
        return 0;
    }
    std::printf("poly_stuck_note_loose_release_v847_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
