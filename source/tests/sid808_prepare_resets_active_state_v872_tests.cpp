// sid808_prepare_resets_active_state_v872_tests.cpp
//
// v872 P2-5 regression — Sid808Engine::prepare() must clear active drum voices.
//
// prepare() is called on device/sample-rate changes. It previously only updated the
// sample rate and re-prepared the SID engine, leaving active gates, auto-release
// timers and microstage schedules that were timed for the OLD sample rate — a source
// of stuck / mistimed hits until the host separately issued an all-notes-off. The fix
// resets active state at the top of prepare().

#include "arpsid/engines/sid808_engine.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID;

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "sid808_prepare_resets_active_state_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

void render(Sid808Engine& e, int frames) {
    std::vector<float> l((size_t)frames, 0.0f), r((size_t)frames, 0.0f);
    e.processBlock(l.data(), r.data(), frames);
}

void testPrepareClearsActiveVoices() {
    Sid808Engine e;
    e.prepare(48000.0);

    // Trigger a drum and let it establish an active gate.
    e.noteOn(Sid808Drum::Kick, 120u, 36u);
    render(e, 64);
    require(e.activeVoiceCount() > 0u, "a triggered drum must have an active voice");

    // A device/sample-rate change must not leave that voice scheduled at the old rate.
    e.prepare(44100.0);
    require(e.activeVoiceCount() == 0u,
            "prepare() must clear active drum voices (no stale gate across sample-rate change)");
}

} // namespace

int main() {
    testPrepareClearsActiveVoices();
    std::cout << "sid808_prepare_resets_active_state_v872_tests PASS\n";
    return 0;
}
