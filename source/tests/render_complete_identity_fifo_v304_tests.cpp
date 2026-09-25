#include "arpsid/engines/voice_manager.h"
#include "arpsid/core/sid_dynamic_state.h"
#include "arpsid/core/sid_runtime_voice_policy.h"
#include <cstdlib>
#include <iostream>
#include <vector>

static void require(bool cond, const char* msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;

    // Physical BitPerfect VoiceManager: anonymous NoteOff must not consume a
    // real host-noteId voice of the same pitch/channel.
    {
        VoiceManager vm;
        const int real = vm.noteOn(60, 0.7f, 1, 777);
        const int anonA = vm.noteOn(60, 0.5f, 1, -1);
        const int anonB = vm.noteOn(60, 0.6f, 1, -1);
        require(real >= 0 && anonA >= 0 && anonB >= 0, "voices must allocate");
        require(real != anonA && real != anonB && anonA != anonB, "same-note identities must remain physically distinct");
        auto off1 = vm.noteOffDetailedResult(60, 1, -1);
        require(off1.voiceIndex == anonA, "anonymous physical NoteOff must release oldest anonymous voice, not real noteId voice");
        auto off2 = vm.noteOffDetailedResult(60, 1, -1);
        require(off2.voiceIndex == anonB, "second anonymous physical NoteOff must release next anonymous voice");
        auto offReal = vm.noteOffDetailedResult(60, 1, 777);
        require(offReal.voiceIndex == real, "real noteId physical NoteOff must still match exact host identity");
    }

    // Canonical dynamic state: anonymous synthetic tokens are negative; real
    // host note IDs are non-negative. Anonymous NoteOff must FIFO only over
    // synthetic anonymous tokens.
    {
        SidDynamicState dyn;
        dyn.noteOnCanonical(64, 3, 777, 0.7f);
        uint64_t realTok = dyn.resolveVoiceTokenForEventIdentity(3, 64, 777);
        dyn.noteOnCanonical(64, 3, -1, 0.5f);
        dyn.noteOnCanonical(64, 3, -1, 0.6f);
        uint64_t oldestAnon = 0, newestAnon = 0;
        uint32_t oldestOrder = 0, newestOrder = 0;
        for (const auto& e : dyn.tokenVoices) {
            if (!e.token.active || e.token.channel != 3 || e.token.note != 64) continue;
            if (e.token.noteId >= 0) continue;
            if (oldestAnon == 0 || e.token.arrivalOrder < oldestOrder) { oldestAnon = e.token.token; oldestOrder = e.token.arrivalOrder; }
            if (newestAnon == 0 || e.token.arrivalOrder > newestOrder) { newestAnon = e.token.token; newestOrder = e.token.arrivalOrder; }
        }
        require(realTok != 0 && oldestAnon != 0 && newestAnon != 0 && oldestAnon != newestAnon,
                "canonical real and anonymous tokens must coexist distinctly");
        dyn.noteOffCanonical(64, 3, -1);
        require(dyn.findActiveVoiceByToken(realTok) != nullptr, "anonymous canonical NoteOff must not consume real noteId token");
        require(dyn.findActiveVoiceByToken(oldestAnon) == nullptr, "anonymous canonical NoteOff must release oldest anonymous token");
        require(dyn.findActiveVoiceByToken(newestAnon) != nullptr, "anonymous canonical NoteOff must leave newer anonymous token held");
        dyn.noteOffCanonical(64, 3, 777);
        require(dyn.findActiveVoiceByToken(realTok) == nullptr, "exact real noteId canonical NoteOff must release real token");
    }

    // VoiceAllocator held-token layer used by synth/mono/unison policy: the
    // held table must use the same anonymous FIFO law and must not return a
    // real held token for noteId-less NoteOff.
    {
        VoiceAllocator va;
        va.setPlayMode(PlayMode::Poly);
        VoiceEventBuffer events;
        va.noteOn(67, 0.7f, 2, 900, events);
        va.noteOn(67, 0.5f, 2, -1, events);
        va.noteOn(67, 0.6f, 2, -1, events);
        events.reset();
        va.noteOff(67, 2, -1, events);
        require(events.count == 1, "anonymous allocator NoteOff must emit one gate-off");
        require(events.events[0].noteId == -1, "anonymous allocator NoteOff must gate anonymous identity only");
        const int firstAnonVoice = events.events[0].voiceIdx;
        events.reset();
        va.noteOff(67, 2, -1, events);
        require(events.count == 1, "second anonymous allocator NoteOff must emit one gate-off");
        require(events.events[0].noteId == -1, "second anonymous allocator NoteOff must still be anonymous");
        require(events.events[0].voiceIdx != firstAnonVoice, "anonymous allocator NoteOff must advance FIFO, not repeat release tail");
        events.reset();
        va.noteOff(67, 2, 900, events);
        require(events.count == 1, "real allocator NoteOff must still emit exact gate-off");
        require(events.events[0].noteId == 900, "real allocator NoteOff must preserve exact host identity");
    }

    std::cout << "RenderCompleteIdentityFifoV304Tests PASS\n";
    return 0;
}
