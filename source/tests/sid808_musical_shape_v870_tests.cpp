// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_musical_shape_v870_tests.cpp
//
// v870 guards the SID808 musical-shape closure:
// - Kick and Tom must run true downward pitch programs.
// - OpenHat must keep an audible ring window.
// - Clap must run multiple gated noise bursts.
// - Cowbell must alternate pulse partials and keep a ringing tail.

#include "arpsid/engines/sid808_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "sid808_musical_shape_v870_tests FAIL: "
                  << message << "\n";
        std::exit(1);
    }
}

std::string readSourceFile(const char* relativePath) {
#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif
    std::ifstream in(std::string(ARPSID_SOURCE_DIR) + "/" + relativePath);
    require(in.good(), "source file must be readable");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

float rmsRegion(const std::vector<float>& l,
                const std::vector<float>& r,
                int begin,
                int end) {
    begin = std::clamp(begin, 0, static_cast<int>(l.size()));
    end = std::clamp(end, begin, static_cast<int>(l.size()));
    double sum = 0.0;
    int count = 0;
    for (int i = begin; i < end; ++i) {
        require(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]),
                "render produced non-finite sample");
        const float mono = 0.5f * (l[(size_t)i] + r[(size_t)i]);
        sum += static_cast<double>(mono) * static_cast<double>(mono);
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

float peakRegion(const std::vector<float>& l,
                 const std::vector<float>& r,
                 int begin,
                 int end) {
    begin = std::clamp(begin, 0, static_cast<int>(l.size()));
    end = std::clamp(end, begin, static_cast<int>(l.size()));
    float peak = 0.0f;
    for (int i = begin; i < end; ++i) {
        require(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]),
                "render produced non-finite sample");
        peak = std::max(peak, std::fabs(l[(size_t)i]));
        peak = std::max(peak, std::fabs(r[(size_t)i]));
    }
    return peak;
}

struct RenderedHit {
    std::vector<float> l;
    std::vector<float> r;
};

RenderedHit renderHit(ArpSID::Sid808Engine& engine,
                      ArpSID::Sid808Drum drum,
                      std::uint8_t midiNote,
                      int frames = 48000) {
    RenderedHit hit{};
    hit.l.assign((size_t)frames, 0.0f);
    hit.r.assign((size_t)frames, 0.0f);
    engine.allNotesOff();
    engine.noteOn(drum, 120u, midiNote);
    engine.processBlock(hit.l.data(), hit.r.data(), frames);
    return hit;
}

int msToSamples(double ms) {
    return static_cast<int>(std::lround(48000.0 * (ms / 1000.0)));
}

void testKickAndTomPitchDrops() {
    using namespace ArpSID;

    Sid808Engine kick;
    kick.prepare(48000.0);
    const RenderedHit kickHit = renderHit(kick, Sid808Drum::Kick, 36u, msToSamples(180.0));
    const auto kickBase = sid808DefaultConfig(Sid808Drum::Kick);
    require(kick.microStageAppliedCount(Sid808Drum::Kick) >= 3u,
            "kick must apply at least three pitch-sweep stages");
    require(kick.lastInitialFreqForDrum(Sid808Drum::Kick) > kickBase.freq * 2u,
            "kick must start above its body pitch");
    require(kick.lastMicroStageFreqForDrum(Sid808Drum::Kick) < kickBase.freq,
            "kick final stage must drop below body pitch");
    require(!kick.lastMicroStageRaisedGateForDrum(Sid808Drum::Kick),
            "kick pitch stages must glide without repeated gate clicks");
    require(peakRegion(kickHit.l, kickHit.r, 0, msToSamples(40.0)) > 0.02f,
            "kick pitch program must remain audible at the attack");

    Sid808Engine tom;
    tom.prepare(48000.0);
    const RenderedHit tomHit = renderHit(tom, Sid808Drum::Tom, 47u, msToSamples(280.0));
    const auto tomBase = sid808DefaultConfig(Sid808Drum::Tom);
    require(tom.microStageAppliedCount(Sid808Drum::Tom) >= 3u,
            "tom must apply at least three pitch-drop stages");
    require(tom.lastInitialFreqForDrum(Sid808Drum::Tom) > tomBase.freq,
            "tom must start above its body pitch");
    require(tom.lastMicroStageFreqForDrum(Sid808Drum::Tom) < tomBase.freq,
            "tom final stage must drop below body pitch");
    require(!tom.lastMicroStageRaisedGateForDrum(Sid808Drum::Tom),
            "tom pitch stages must glide without repeated gate clicks");
    require(peakRegion(tomHit.l, tomHit.r, 0, msToSamples(80.0)) > 0.015f,
            "tom pitch program must remain audible");
}

void testOpenHatRing() {
    using namespace ArpSID;

    Sid808Engine engine;
    engine.prepare(48000.0);
    const RenderedHit hit = renderHit(engine, Sid808Drum::OpenHat, 46u, msToSamples(480.0));
    const float early = rmsRegion(hit.l, hit.r, msToSamples(0.0), msToSamples(45.0));
    const float ring = rmsRegion(hit.l, hit.r, msToSamples(110.0), msToSamples(230.0));
    const float late = rmsRegion(hit.l, hit.r, msToSamples(250.0), msToSamples(380.0));

    require(engine.microStageAppliedCount(Sid808Drum::OpenHat) >= 3u,
            "open hat must apply a multi-stage ring program");
    require(engine.activeFilterModeForVoice(2) == FilterMode::None ||
            engine.lastMicroStageWaveformForDrum(Sid808Drum::OpenHat) == 0x80u,
            "open hat staged program must stay on noise");
    require(early > 0.001f, "open hat attack must be audible");
    require(ring > early * 0.025f && ring > 0.00008f,
            "open hat must keep a measurable ring window");
    require(late > early * 0.006f,
            "open hat late tail must not collapse immediately after the attack");
}

void testClapBurstTrain() {
    using namespace ArpSID;

    Sid808Engine engine;
    engine.prepare(48000.0);
    const RenderedHit hit = renderHit(engine, Sid808Drum::Clap, 39u, msToSamples(140.0));
    const float first = rmsRegion(hit.l, hit.r, msToSamples(0.4), msToSamples(2.8));
    const float second = rmsRegion(hit.l, hit.r, msToSamples(3.6), msToSamples(5.8));
    const float third = rmsRegion(hit.l, hit.r, msToSamples(7.0), msToSamples(9.2));
    const float tail = rmsRegion(hit.l, hit.r, msToSamples(12.0), msToSamples(32.0));

    require(engine.microStageAppliedCount(Sid808Drum::Clap) >= 3u,
            "clap must apply three scheduled burst/tail stages");
    require(engine.lastMicroStageRaisedGateForDrum(Sid808Drum::Clap),
            "clap burst stages must retrigger the gate");
    require(first > 0.0008f, "clap first burst must be audible");
    require(second > first * 0.08f, "clap second burst must be audible");
    require(third > first * 0.05f, "clap third burst must be audible");
    require(tail > first * 0.03f, "clap tail must outlive the burst train");
}

void testCowbellDualPartialRing() {
    using namespace ArpSID;

    Sid808Engine engine;
    engine.prepare(48000.0);
    const RenderedHit hit = renderHit(engine, Sid808Drum::Cowbell, 56u, msToSamples(420.0));
    const auto base = sid808DefaultConfig(Sid808Drum::Cowbell);
    const float attack = rmsRegion(hit.l, hit.r, msToSamples(0.0), msToSamples(45.0));
    const float ring = rmsRegion(hit.l, hit.r, msToSamples(100.0), msToSamples(260.0));

    require(engine.microStageAppliedCount(Sid808Drum::Cowbell) >= 3u,
            "cowbell must apply alternating partial stages");
    require(engine.lastInitialFreqForDrum(Sid808Drum::Cowbell) == base.freq,
            "cowbell must begin on the authored first partial");
    require(engine.lastMicroStageFreqForDrum(Sid808Drum::Cowbell) != base.freq,
            "cowbell final stage must use a second partial frequency");
    require(engine.lastMicroStageWaveformForDrum(Sid808Drum::Cowbell) == 0x40u,
            "cowbell staged program must stay pulse-based");
    require(attack > 0.001f, "cowbell attack must be audible");
    require(ring > attack * 0.018f && ring > 0.00006f,
            "cowbell must keep a measurable ringing tail");
}

void testSourceGuards() {
    const std::string sid808 = readSourceFile("include/arpsid/engines/sid808_engine.h");
    require(contains(sid808, "kMicroStageProgramCapacity_"),
            "SID808 must keep a bounded multi-stage program per voice");
    require(contains(sid808, "armKickPitchSweep_"),
            "SID808 must keep the kick pitch-sweep program");
    require(contains(sid808, "armOpenHatRingProgram_"),
            "SID808 must keep the open-hat ring program");
    require(contains(sid808, "armClapBurstProgram_"),
            "SID808 must keep the clap burst program");
    require(contains(sid808, "armCowbellPartialProgram_"),
            "SID808 must keep the cowbell partial program");
    require(contains(sid808, "armTomPitchDrop_"),
            "SID808 must keep the tom pitch-drop program");
}

} // namespace

int main() {
    testKickAndTomPitchDrops();
    testOpenHatRing();
    testClapBurstTrain();
    testCowbellDualPartialRing();
    testSourceGuards();
    std::cout << "sid808_musical_shape_v870_tests PASS\n";
    return 0;
}
