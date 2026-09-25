// synthmode_voice_token_v871_tests.cpp
//
// Guards canonical voice-token carriage through held replay and SynthMode
// release. The runtime may still allocate tokens for ordinary NoteOn paths, but
// explicit replay/token NoteOff events must not be reduced back to noteId scans.

#include "arpsid/core/sid_event_queue.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "synthmode_voice_token_v871_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string readSource(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_DIR) + "/" + rel, std::ios::binary);
    require(stream.good(), "source file must be readable");
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    ArpSID::SidTimedEvent ev{};
    ev.voiceToken = 0x4000000000001234ull;
    require(ev.voiceToken == 0x4000000000001234ull,
            "SidTimedEvent must carry an explicit voice token");
    static_assert(sizeof(ArpSID::SidTimedEvent) <= 64,
                  "SidTimedEvent must remain compact after token carriage");

    const std::string queue = readSource("include/arpsid/core/sid_event_queue.h");
    const std::string held = readSource("include/arpsid/core/sid_runtime_held_replay.h");
    const std::string model = readSource("include/arpsid/core/sid_runtime_model.h");
    const std::string dynamic = readSource("include/arpsid/core/sid_dynamic_state.h");
    const std::string scheduler = readSource("include/arpsid/core/sid_runtime_synth_register_scheduler.h");
    const std::string kernel = readSource("source/au3/ArpSIDDSPKernel.hpp");
    const std::string canonical = readSource("source/au3/ArpSIDCanonicalEvents.h");

    require(contains(queue, "uint64_t voiceToken = 0"),
            "canonical timed event must expose voiceToken");
    require(contains(held, "ev.voiceToken = tok"),
            "held replay NoteOn must stamp the bound token");
    require(contains(held, "ev.voiceToken = e.token.token"),
            "all-notes-off replay must stamp token NoteOff events");
    require(contains(model, "bindVoiceTokenBridgeWithToken"),
            "runtime model must expose exact-token binding for replay");
    require(contains(dynamic, "bindVoiceTokenExplicit"),
            "dynamic state must bind explicit replay tokens");
    require(contains(scheduler, "noteOnWithToken"),
            "SynthMode scheduler must accept explicit NoteOn tokens");
    require(contains(kernel, "ev.voiceToken != 0"),
            "SynthMode NoteOff must prefer explicit event token");
    require(contains(canonical, "ev.voiceToken = voiceToken") &&
            contains(canonical, "ev.voiceToken = in.voiceToken"),
            "AU canonical conversions must preserve voiceToken");

    std::cout << "synthmode_voice_token_v871_tests PASS\n";
    return 0;
}
