// Copyright (C) 2024-2026 Ulf Bertilsson
// poly_orphan_reconcile_v852_tests.cpp
//
// Guards the v852 stuck-note reconciliation. Any gated poly voice whose key is no
// longer physically held (per the host's authoritative held mirror) and not held
// by a pedal is an orphan whose note-off was lost by some matching failure. The
// render thread reconciles these each block. This pins the core logic in
// VoiceManager::reconcileUnheldVoices: the two-consecutive-pass grace period (so a
// boundary note-off is never clipped), pedal preservation, held-note preservation,
// and grace reset when the orphan condition breaks.

#include "arpsid/engines/voice_manager.h"

#include <cstdio>

using namespace ArpSID;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

int main() {
    // A) Grace period: an orphan must persist two consecutive passes to be released.
    {
        VoiceManager vm;
        auto a = vm.noteOnWithToken(60, 1.0f, 0, 5, 111u, [](int){});
        auto b = vm.noteOnWithToken(64, 1.0f, 0, 6, 222u, [](int){});
        auto held  = [](int, int note){ return note == 64; }; // 64 held, 60 orphaned
        auto pedal = [](int){ return false; };
        int gated = 0; auto gate = [&](int){ ++gated; };

        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "pass 1 releases nothing (grace)");
        require(vm.getVoiceState(a.voiceIndex).keyDown, "orphan still key-down after pass 1");
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 1, "pass 2 releases the confirmed orphan");
        require(!vm.getVoiceState(a.voiceIndex).keyDown, "orphan is key-up after pass 2");
        require(vm.getVoiceState(b.voiceIndex).keyDown, "the held voice is preserved");
        require(gated == 1, "gate-off callback fired exactly once");
    }

    // B) A pedal-held voice is never reconciled away even with its key up.
    {
        VoiceManager vm;
        auto a = vm.noteOnWithToken(60, 1.0f, 0, 5, 111u, [](int){});
        auto held  = [](int, int){ return false; }; // key up
        auto pedal = [](int){ return true; };        // but pedal down
        auto gate  = [](int){};
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "pedal pass 1");
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "pedal pass 2 keeps the voice");
        require(vm.getVoiceState(a.voiceIndex).keyDown, "pedal-held voice preserved");
    }

    // C) Everything held → nothing is ever released.
    {
        VoiceManager vm;
        vm.noteOnWithToken(60, 1.0f, 0, 5, 111u, [](int){});
        auto held  = [](int, int){ return true; };
        auto pedal = [](int){ return false; };
        auto gate  = [](int){};
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "all-held pass 1");
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "all-held pass 2");
    }

    // D) The grace resets if the orphan condition lapses between passes.
    {
        VoiceManager vm;
        auto a = vm.noteOnWithToken(60, 1.0f, 0, 5, 111u, [](int){});
        bool keyUp = true;
        auto held  = [&](int, int){ return !keyUp; };
        auto pedal = [](int){ return false; };
        auto gate  = [](int){};
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "orphan pass 1");
        keyUp = false; // pressed again
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "re-held pass resets grace");
        keyUp = true;  // released again
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "orphan again, grace was reset");
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 1, "second consecutive orphan releases");
        require(!vm.getVoiceState(a.voiceIndex).keyDown, "voice finally released");
    }

    // E) resetOrphanReconcile clears the grace state.
    {
        VoiceManager vm;
        vm.noteOnWithToken(60, 1.0f, 0, 5, 111u, [](int){});
        auto held  = [](int, int){ return false; };
        auto pedal = [](int){ return false; };
        auto gate  = [](int){};
        (void)vm.reconcileUnheldVoices(held, pedal, gate); // arms grace
        vm.resetOrphanReconcile();
        require(vm.reconcileUnheldVoices(held, pedal, gate) == 0, "reset clears grace so next pass does not release");
    }

    if (g_failures == 0) { std::printf("poly_orphan_reconcile_v852_tests: PASS\n"); return 0; }
    std::printf("poly_orphan_reconcile_v852_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
