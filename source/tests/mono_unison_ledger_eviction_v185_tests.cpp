#include "arpsid/core/sid_runtime_voice_policy.h"
#include <cstdlib>
#include <iostream>

static void require(bool cond, const char* msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

struct GateLog { int count = 0; int last = -1; };
static void gateOff(void* ctx, int idx) {
    auto* g = static_cast<GateLog*>(ctx);
    ++g->count;
    g->last = idx;
}

int main() {
    using namespace ArpSID;
    VoiceAllocator va;
    GateLog log{};
    va.setSustainGateOffCallback(&gateOff, &log);
    va.setPlayMode(PlayMode::Poly);

    VoiceEventBuffer out{};
    // Fill held ledger beyond capacity while voices are sostenuto-held. The
    // oldest ledger entry is evicted. Eviction is not a musical NoteOff, so it
    // must hard-gate the matching voice even if pedal state would normally hold.
    for (int i = 0; i < kMaxHeldNotes; ++i) {
        out.reset();
        va.noteOn(36 + (i % 40), 0.7f, 0, i, out);
    }
    va.setSostenutoPedal(0, true);
    for (int i = 0; i < kMaxHeldNotes; ++i) {
        out.reset();
        va.noteOff(36 + (i % 40), 0, i, out);
    }
    const int before = log.count;
    out.reset();
    va.noteOn(90, 0.8f, 0, 999, out);
    require(log.count > before, "held-ledger overflow must gate off evicted pedal-held token");

    // The allocator must still be usable after hard eviction.
    out.reset();
    va.panic(out);
    require(out.count >= 1 && out.events[0].kind == VoiceEvent::Kind::AllOff,
            "panic still emits AllOff after ledger eviction closure");
    return 0;
}
