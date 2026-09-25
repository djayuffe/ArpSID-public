#include "arpsid/core/sid_runtime_voice_policy.h"
#include <cstdlib>
#include <iostream>

static bool require(bool v, const char* msg) {
    if (!v) std::cerr << "FAIL: " << msg << "\n";
    return v;
}

static bool test_unison_sustain_noteoff_keeps_voice_active_without_gateoff() {
    ArpSID::VoiceAllocator va;
    va.setPlayMode(ArpSID::PlayMode::Unison);
    va.setUnisonCount(3);
    ArpSID::VoiceEventBuffer ev{};
    va.noteOn(60, 1.0f, 0, 10, ev);
    ev.reset();
    va.setSustainPedal(0, true);
    va.noteOff(60, 0, 10, ev);
    bool ok = true;
    ok &= require(ev.count == 0, "sustained unison note-off must not emit GateOff while pedal is down");
    ok &= require(va.voiceManager().getVoiceState(0).isActive, "unison voice 0 remains active under sustain");
    ok &= require(va.voiceManager().getVoiceState(0).isSustained, "unison voice 0 is marked sustained");
    ev.reset();
    va.noteOn(64, 1.0f, 0, 11, ev);
    ok &= require(ev.count >= 3, "next unison note-on emits events for active unison bank");
    if (ev.count >= 1) {
        ok &= require(ev.events[0].kind != ArpSID::VoiceEvent::Kind::Start,
                      "next unison note-on must not cold-start over pedal-held active bank");
    }
    return ok;
}

static bool test_token_release_helper_compiles_and_releases() {
    ArpSID::VoiceManager vm;
    vm.forceAssignVoice(0, 60, 1.0f, 0, 1, 0xABC);
    const int n = vm.forceReleaseVoicesByTokenPedalAware(0xABC, true, true);
    bool ok = true;
    ok &= require(n == 1, "token pedal-aware release finds one voice");
    ok &= require(!vm.getVoiceState(0).keyDown, "token pedal-aware release clears keyDown");
    ok &= require(vm.getVoiceState(0).isSustained, "token pedal-aware release can preserve as sustained");
    return ok;
}

int main() {
    bool ok = true;
    ok &= test_unison_sustain_noteoff_keeps_voice_active_without_gateoff();
    ok &= test_token_release_helper_compiles_and_releases();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
