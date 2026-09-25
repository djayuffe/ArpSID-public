// v958 DrSID cold-first-block audibility closure.
//
// Pins a residual of the v955 class: in the DEFAULT SidAuthentic drum model the
// Cowbell and Tom voices are the only drums that route their OWN SID voice
// through the lowpass filter (cowbell routes voice 0, tom routes voice 1). Their
// microprograms set the filter cutoff low, so their short transient only survives
// because the SID filter's cutoff SMOOTHER is lagging down from a higher value.
//
// After a DrSID reset the smoother sat at the cold 20 Hz reset placeholder while
// configureSidCoreForDrums_ had actually opened the cutoff to ~0x680. A cowbell/
// tom hit that landed on the very FIRST rendered block (before any warm-up block
// had ramped the smoother up) was therefore muted (~0.018 vs ~0.25 warm). The
// v955 test masked this by draining a stopped block first; this test drives the
// hit on the absolute first processBlock.
//
// Fix: configureSidCoreForDrums_ now snaps the filter smoother onto the freshly
// configured cutoff (sidChip.snapFilterSmoothing()), so the cold first block
// behaves identically to a warmed one.

#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "drsid_cold_first_block_audibility_v958_tests FAIL: %s\n", msg); std::exit(1); }
}

// Render a GM drum whose NoteOn lands on the very first processBlock — i.e. no
// prior (warm-up) block has run against a fresh kernel.
static float renderDrumOnColdFirstBlock(int gmNote, bool playing) {
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    k->setParameter(static_cast<int>(kParamDrSidEnable), 1.0f);
    k->setParameter(static_cast<int>(kParamDrSidMachineModel), 0.0f); // SidAuthentic (default)

    constexpr int F = 512;
    std::array<float, F> l{}, r{};
    float* o[2] = { l.data(), r.data() };
    TransportState t{};
    t.isPlaying = playing; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = F;

    float peak = 0.0f;
    for (int b = 0; b < 24; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        if (b == 0) {
            TimedEvent e{};
            e.sampleOffset = 0; e.kind = EventKind::NoteOn; e.channel = 9;
            e.pitch = static_cast<int16_t>(gmNote); e.value = 1.0f;
            k->processBlock(o, 2, F, &e, 1, t); // hit lands on the cold first block
        } else {
            k->processBlock(o, 2, F, nullptr, 0, t);
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
    // Every GM drum must be audible when hit on the absolute first render block,
    // both with the transport already playing and while stopped (live audition).
    for (bool playing : {true, false}) {
        for (const auto& d : drums) {
            const float pk = renderDrumOnColdFirstBlock(d.note, playing);
            std::printf("  %-10s cold-first-block (playing=%d): peak=%.5f\n", d.name, (int)playing, pk);
            char msg[112];
            std::snprintf(msg, sizeof(msg),
                          "%s must be audible on the cold first render block (v958)", d.name);
            require(pk > 0.03f, msg);
        }
    }

    // The filter-routed voices (cowbell/tom) were the regressers: assert they now
    // reach a healthy level on the cold block, not just the >0.03 audibility floor.
    require(renderDrumOnColdFirstBlock(56, true) > 0.10f, "cowbell cold first block must be full-level, not muted");
    require(renderDrumOnColdFirstBlock(47, true) > 0.10f, "tom cold first block must be full-level, not muted");

    std::printf("drsid_cold_first_block_audibility_v958_tests PASS\n");
    return 0;
}
