// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_runtime_voice_policy.h"
#include <cstdio>
#include <cstdlib>

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;
    VoiceAllocator va;
    va.setPlayMode(PlayMode::Poly);
    va.setMaxPolyVoices(1);
    VoiceEventBuffer out;

    va.noteOn(60, 1.0f, 0, 100, out);
    require(out.count == 1 && out.events[0].kind == VoiceEvent::Kind::Start, "initial poly note starts");
    const uint64_t firstToken = out.events[0].voiceToken;
    require(firstToken != 0, "initial voice has token");

    out.reset();
    va.noteOn(64, 1.0f, 0, 101, out);
    require(out.count >= 2, "steal emits gate-off and start");
    require(out.events[0].kind == VoiceEvent::Kind::GateOff, "steal gates old voice first");
    require(out.events[0].voiceToken == firstToken, "steal gate-off references old token");
    const uint64_t secondToken = out.events[out.count - 1].voiceToken;
    require(secondToken != 0 && secondToken != firstToken, "stolen slot gets new token");

    out.reset();
    va.noteOff(60, 0, 100, out);
    require(out.count == 0, "note-off for stolen/orphaned note produces no stale gate-off");

    out.reset();
    va.noteOff(64, 0, 101, out);
    require(out.count == 1 && out.events[0].kind == VoiceEvent::Kind::GateOff, "current stolen-in note releases normally");
    require(out.events[0].voiceToken == secondToken, "current note releases by current token");
    return 0;
}
