// v955 drum transport-start audibility closure.
//
// Pins the P1 that in the DEFAULT drum machine model (SidAuthentic), the Cowbell
// and Tom drums were silenced when a hit landed on the exact block where the host
// transport flips stopped->playing (e.g. a sequenced pattern whose step 1 is a
// cowbell/tom). Root cause: clearRuntimeStateForTransportStart_ hard-reset the
// DrSID engine (drs->reset()), and the freshly-reset engine renders those
// choke-family, filter+pitch-swept voices (SID voice 0/1) as zero through the
// fractional interval render path (the same reset+hit renders fine via slice /
// processBlock). DrSID drums are one-shots and the engine preserves its own
// transport state (v950), so the transport boundary now releases notes instead
// of destructively resetting the engine.
//
// This drives the REAL ArpSIDDSPKernel across a Stop->Play boundary and requires
// every GM drum (kick/snare/hats/clap/cowbell/tom/rim) to be audible.

#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "drum_transport_start_audibility_v955_tests FAIL: %s\n", msg); std::exit(1); }
}

// Render one GM drum, hitting it on the block where the transport starts.
static float renderDrumOnTransportStart(int gmNote) {
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    k->enqueueParameterIntent(kParamDrSidEnable, 1.0f);
    k->enqueueParameterIntent(kParamDrSidMachineModel, 0.0f); // SidAuthentic (default)
    k->enqueueParameterIntent(kParamDrSidVolume, 0.8f);

    constexpr int F = 512;
    std::array<float, F> l{}, r{};
    float* o[2] = { l.data(), r.data() };

    // Drain params with the transport STOPPED, so the drum hit below lands on the
    // stopped->playing start edge.
    TransportState stopped{};
    stopped.isPlaying = false; stopped.playStateKnown = true;
    stopped.sampleRate = 48000.0; stopped.frameCount = F;
    k->processBlock(o, 2, F, nullptr, 0, stopped);

    TransportState playing{};
    playing.isPlaying = true; playing.playStateKnown = true; playing.bpm = 120.0;
    playing.sampleRate = 48000.0; playing.frameCount = F;

    float peak = 0.0f;
    for (int b = 0; b < 40; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        if (b == 0) {
            TimedEvent e{};
            e.sampleOffset = 0; e.kind = EventKind::NoteOn; e.channel = 9;
            e.pitch = static_cast<int16_t>(gmNote); e.value = 1.0f;
            k->processBlock(o, 2, F, &e, 1, playing); // hit lands on the start edge
        } else {
            k->processBlock(o, 2, F, nullptr, 0, playing);
        }
        for (int i = 0; i < F; ++i) {
            require(std::isfinite(l[(size_t)i]), "drum samples must remain finite");
            const float a = std::fabs(l[(size_t)i]);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main() {
    ArpSID::prewarmAllSidTables();
    struct D { const char* name; int note; };
    const D drums[8] = {
        {"Kick",36}, {"Snare",38}, {"ClosedHat",42}, {"OpenHat",46},
        {"Clap",39}, {"Cowbell",56}, {"Tom",47}, {"Rim",37}
    };
    for (const auto& d : drums) {
        const float pk = renderDrumOnTransportStart(d.note);
        std::printf("  %-10s on transport start: peak=%.5f\n", d.name, pk);
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "%s must be audible when hit on a transport start edge (v955 P1)", d.name);
        require(pk > 0.03f, msg);
    }
    std::printf("drum_transport_start_audibility_v955_tests PASS\n");
    return 0;
}
