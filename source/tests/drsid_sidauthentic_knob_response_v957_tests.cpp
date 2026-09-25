// v957 SidAuthentic drum-knob response closure.
//
// The default DrSID drum model (SidAuthentic, machine model 0) plays fixed
// canonical C64 drum microprograms. Before this fix those programs were emitted
// verbatim, so the per-drum Tune/Decay/Tone knobs (kParamDrSidKickTune,
// KickDecay, SnareTone, SnareSnap, HatTune, HatDecay, ClapDecay, CowbellTune,
// CowbellDecay, TomTune, TomDecay) were completely inaudible in the default
// model — every knob position produced a BIT-IDENTICAL hit. Only the analog
// AnalogX0X8 overlay responded.
//
// Fix: DrSidEngine::makeKnobModulatedWavetableProgram_() applies the knobs to a
// per-voice modulated copy of the canonical microprogram at note-on — Tune
// scales oscillator frequency, Decay scales the step schedule + release tail —
// centered so a default patch reproduces the authored hit bit-for-bit.
//
// This test renders the real ArpSIDDSPKernel in the default SidAuthentic model
// and requires (1) each drum audible, and (2) the low vs high position of every
// per-drum knob to produce audibly different audio.

#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "drsid_sidauthentic_knob_response_v957_tests FAIL: %s\n", msg); std::exit(1); }
}

// Render a drum note on channel 9 in the DEFAULT SidAuthentic DrSID model, with
// an optional single-knob override. Reports peak and the resolved machine model.
static std::vector<float> render(int note, int knobParam, float knobVal,
                                 float& peak, int& machineModel) {
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    k->setParameter(static_cast<int>(kParamDrSidEnable), 1.0f);        // DrSid render mode
    k->setParameter(static_cast<int>(kParamDrSidMachineModel), 0.0f);  // SidAuthentic (default)
    if (knobParam >= 0) k->setParameter(knobParam, knobVal);

    constexpr int F = 512;
    std::array<float, F> l{}, r{};
    float* o[2] = { l.data(), r.data() };
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = F;
    k->processBlock(o, 2, F, nullptr, 0, t);  // settle params
    machineModel = static_cast<int>(std::lround(k->runtimeParameterValues()[(size_t)kParamDrSidMachineModel]));

    std::vector<float> wave;
    peak = 0.0f;
    for (int b = 0; b < 20; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        if (b == 0) {
            TimedEvent e{};
            e.sampleOffset = 0; e.kind = EventKind::NoteOn; e.channel = 9;
            e.pitch = static_cast<int16_t>(note); e.value = 1.0f;
            k->processBlock(o, 2, F, &e, 1, t);
        } else {
            k->processBlock(o, 2, F, nullptr, 0, t);
        }
        for (int i = 0; i < F; ++i) {
            require(std::isfinite(l[(size_t)i]), "SidAuthentic drum samples must remain finite");
            wave.push_back(l[(size_t)i]);
            const float a = std::fabs(l[(size_t)i]);
            if (a > peak) peak = a;
        }
    }
    return wave;
}

static double meanAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    double d = 0.0;
    for (size_t i = 0; i < n; ++i) d += std::fabs(a[i] - b[i]);
    return d / static_cast<double>(std::max<size_t>(1, n));
}

struct KnobCase { const char* name; int note; int param; };

int main() {
    ArpSID::prewarmAllSidTables();

    // Every per-drum Tune/Decay/Tone knob must reshape the default-model audio.
    const KnobCase cases[] = {
        {"Kick Tune",     36, static_cast<int>(kParamDrSidKickTune)},
        {"Kick Decay",    36, static_cast<int>(kParamDrSidKickDecay)},
        {"Snare Tone",    38, static_cast<int>(kParamDrSidSnareTone)},
        {"Snare Snap",    38, static_cast<int>(kParamDrSidSnareSnap)},
        {"CHat Tune",     42, static_cast<int>(kParamDrSidHatTune)},
        {"CHat Decay",    42, static_cast<int>(kParamDrSidHatDecay)},
        {"OHat Tune",     46, static_cast<int>(kParamDrSidHatTune)},
        {"Clap Decay",    39, static_cast<int>(kParamDrSidClapDecay)},
        {"Cowbell Tune",  56, static_cast<int>(kParamDrSidCowbellTune)},
        {"Cowbell Decay", 56, static_cast<int>(kParamDrSidCowbellDecay)},
        {"Tom Tune",      47, static_cast<int>(kParamDrSidTomTune)},
        {"Tom Decay",     47, static_cast<int>(kParamDrSidTomDecay)},
    };
    for (const auto& c : cases) {
        float plo = 0.0f, phi = 0.0f; int mlo = -1, mhi = -1;
        const auto wlo = render(c.note, c.param, 0.05f, plo, mlo);
        const auto whi = render(c.note, c.param, 0.95f, phi, mhi);
        require(mlo == 0 && mhi == 0, "knob response must be measured in the SidAuthentic model");
        const double d = meanAbsDiff(wlo, whi);
        std::printf("%-14s peak(lo=%.3f,hi=%.3f) diff=%.6f\n", c.name, plo, phi, d);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "SidAuthentic drum must stay audible across '%s' range", c.name);
        require(plo > 0.02f && phi > 0.02f, msg);
        std::snprintf(msg, sizeof(msg), "SidAuthentic '%s' knob must audibly reshape the drum (was bit-identical)", c.name);
        require(d > 1.0e-4, msg);
    }

    // The default (center) knob positions keep every GM drum voice audible.
    static const int notes[8] = {36,38,42,46,39,56,47,37};
    for (int i = 0; i < 8; ++i) {
        float pk = 0.0f; int mm = -1;
        (void)render(notes[i], -1, 0.0f, pk, mm);
        char msg[96];
        std::snprintf(msg, sizeof(msg), "default SidAuthentic drum note %d must be audible", notes[i]);
        require(pk > 0.02f, msg);
    }

    std::printf("drsid_sidauthentic_knob_response_v957_tests PASS\n");
    return 0;
}
