// sid_core_golden_trace_v872_tests.cpp
//
// v872 P2-2 (partial) — a deterministic golden-trace REGRESSION harness for the
// SID core (SidRegisterEngine). It renders a fixed register program and pins the
// audible result's structural signature: the synthesized pitch (from zero-crossing
// rate) must match the programmed SID frequency, the gate-on output must be
// audible and bounded, and gate-off must decay the voice. This guards the SID core
// against silent output drift without depending on exact per-sample floats.
//
// NOTE: this is the reusable comparison rig, not the full reSIDfp ORACLE the audit
// asked for. A bit-exact SIDChip-vs-reSIDfp OSC3/ENV3 comparison additionally
// requires vendoring reSIDfp as an external reference; the reference-capture step
// is the remaining external work. This harness is the platform-independent half.

#include "arpsid/engines/sid_register_engine.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace ArpSID;

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "sid_core_golden_trace_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

constexpr double kSr = 44100.0;
constexpr double kPalClock = 985248.0;      // PAL 6581/8580 phi2
constexpr double kSidAcc = 16777216.0;      // 2^24 phase accumulator

// SID frequency register value for a target Hz at the PAL clock.
uint16_t sidFreqReg(double hz) {
    return static_cast<uint16_t>(std::lround(hz * kSidAcc / kPalClock));
}

std::vector<float> renderMono(SidRegisterEngine& sid, int frames) {
    std::vector<float> l((size_t)frames, 0.0f), r((size_t)frames, 0.0f);
    sid.renderBlock(l.data(), r.data(), frames);
    std::vector<float> mono((size_t)frames, 0.0f);
    for (int i = 0; i < frames; ++i) {
        require(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]), "non-finite SID output");
        mono[(size_t)i] = 0.5f * (l[(size_t)i] + r[(size_t)i]);
    }
    return mono;
}

double rms(const std::vector<float>& x, int begin, int end) {
    double s = 0.0; int n = 0;
    for (int i = begin; i < end && i < (int)x.size(); ++i) { s += (double)x[(size_t)i] * x[(size_t)i]; ++n; }
    return n ? std::sqrt(s / n) : 0.0;
}

// Count zero-crossings (sign changes) in [begin,end) after removing DC bias.
int zeroCrossings(const std::vector<float>& x, int begin, int end) {
    double mean = 0.0; int n = 0;
    for (int i = begin; i < end && i < (int)x.size(); ++i) { mean += x[(size_t)i]; ++n; }
    if (n == 0) return 0;
    mean /= n;
    int zc = 0; int prevSign = 0;
    for (int i = begin; i < end && i < (int)x.size(); ++i) {
        const double v = x[(size_t)i] - mean;
        const int sign = (v > 1e-5) ? 1 : (v < -1e-5 ? -1 : 0);
        if (sign != 0 && prevSign != 0 && sign != prevSign) ++zc;
        if (sign != 0) prevSign = sign;
    }
    return zc;
}

void programTriangleVoice(SidRegisterEngine& sid, double hz) {
    const uint16_t f = sidFreqReg(hz);
    sid.write(0x00u, static_cast<uint8_t>(f & 0xFFu));    // voice 1 freq lo
    sid.write(0x01u, static_cast<uint8_t>(f >> 8u));      // voice 1 freq hi
    sid.write(0x05u, 0x00u);                              // attack=0, decay=0 (fast)
    sid.write(0x06u, 0xF0u);                              // sustain=F, release=0
    sid.write(0x18u, 0x0Fu);                              // volume max
    sid.write(0x04u, 0x11u);                              // triangle + gate on
}

void testSidCorePitchAndEnvelopeGoldenTrace() {
    SidRegisterEngine sid;
    sid.setClockFrequency(kPalClock);
    sid.prepare(kSr);

    constexpr double targetHz = 440.0;
    programTriangleVoice(sid, targetHz);

    // Render ~140 ms; measure in a settled sustain window.
    const int frames = static_cast<int>(kSr * 0.14);
    const auto sustain = renderMono(sid, frames);

    const int w0 = static_cast<int>(kSr * 0.04);   // skip attack/settling
    const int w1 = static_cast<int>(kSr * 0.13);
    const double sustainRms = rms(sustain, w0, w1);
    require(sustainRms > 1.0e-4, "gated SID triangle must produce audible sustain output");

    float peak = 0.0f;
    for (float v : sustain) peak = std::max(peak, std::fabs(v));
    require(peak < 4.0f, "SID output must stay bounded (no runaway)");

    // Golden pitch: zero-crossing rate must reflect the programmed 440 Hz.
    const int zc = zeroCrossings(sustain, w0, w1);
    const double windowSec = static_cast<double>(w1 - w0) / kSr;
    const double measuredHz = (zc / 2.0) / windowSec;   // two crossings per cycle
    require(measuredHz > targetHz * 0.80 && measuredHz < targetHz * 1.20,
            "SID core must synthesize the programmed pitch (zero-crossing rate ~= 440 Hz)");

    // Gate off: the voice must decay toward silence (release=0 => fast).
    sid.write(0x04u, 0x10u);   // triangle, gate low
    const auto tail = renderMono(sid, static_cast<int>(kSr * 0.12));
    const double tailRms = rms(tail, static_cast<int>(kSr * 0.06), static_cast<int>(kSr * 0.11));
    require(tailRms < sustainRms * 0.5,
            "SID core must decay after gate-off (envelope release active)");
}

// Two identical programs must produce identical output — determinism is the
// precondition for any golden/oracle trace comparison.
void testSidCoreRenderIsDeterministic() {
    SidRegisterEngine a, b;
    a.setClockFrequency(kPalClock); a.prepare(kSr);
    b.setClockFrequency(kPalClock); b.prepare(kSr);
    programTriangleVoice(a, 330.0);
    programTriangleVoice(b, 330.0);

    const int frames = static_cast<int>(kSr * 0.05);
    const auto ra = renderMono(a, frames);
    const auto rb = renderMono(b, frames);
    for (int i = 0; i < frames; ++i)
        require(ra[(size_t)i] == rb[(size_t)i],
                "identical SID programs must render bit-identical output (deterministic trace)");
}

} // namespace

int main() {
    testSidCorePitchAndEnvelopeGoldenTrace();
    testSidCoreRenderIsDeterministic();
    std::cout << "sid_core_golden_trace_v872_tests PASS\n";
    return 0;
}
