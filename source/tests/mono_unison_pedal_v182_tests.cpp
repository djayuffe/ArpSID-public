#include "arpsid/core/sid_runtime_voice_policy.h"
#include <cstdio>

static bool require(bool cond, const char* msg) {
    if (!cond) std::fprintf(stderr, "FAIL: %s\n", msg);
    return cond;
}

int main() {
    using namespace ArpSID;
    bool ok = true;

    {
        VoiceAllocator va;
        VoiceEventBuffer out;
        va.setPlayMode(PlayMode::Mono);
        va.setSustainPedal(0, true);
        va.noteOn(60, 0.8f, 0, 10, out);
        out.reset();
        va.noteOff(60, 0, 10, out);
        ok &= require(out.count == 0, "mono note-off under sustain must not emit GateOff");
        ok &= require(va.voiceManager().getVoiceState(0).isActive, "mono sustained voice remains active");
        ok &= require(!va.voiceManager().getVoiceState(0).keyDown, "mono sustained note keyDown cleared");
        ok &= require(va.voiceManager().getVoiceState(0).isSustained, "mono sustained note marked sustained");
    }

    {
        VoiceAllocator va;
        VoiceEventBuffer out;
        va.setPlayMode(PlayMode::Unison);
        va.setUnisonCount(3);
        va.setSustainPedal(0, true);
        va.noteOn(67, 0.7f, 0, 20, out);
        out.reset();
        va.noteOff(67, 0, 20, out);
        ok &= require(out.count == 0, "unison note-off under sustain must not emit GateOff events");
        for (int v = 0; v < 3; ++v) {
            const auto& st = va.voiceManager().getVoiceState(v);
            ok &= require(st.isActive, "unison sustained voice remains active");
            ok &= require(!st.keyDown, "unison sustained keyDown cleared");
            ok &= require(st.isSustained, "unison sustained flag set");
        }
    }

    {
        VoiceManager vm;
        vm.forceAssignVoice(0, 72, 0.5f, 0, 1, 1234);
        vm.markVoiceSostenuto(0, true);
        vm.forceReleaseVoicePedalAware(0, false, true);
        vm.releaseFinished(0);
        ok &= require(vm.getVoiceState(0).isActive, "releaseFinished must not clear sostenuto-held voice");
        ok &= require(vm.getVoiceState(0).isSostenuto, "sostenuto latch preserved by pedal-aware force release");
    }

    return ok ? 0 : 1;
}
