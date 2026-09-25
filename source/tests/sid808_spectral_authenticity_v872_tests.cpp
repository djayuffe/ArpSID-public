// sid808_spectral_authenticity_v872_tests.cpp
//
// v872 P1-10 — lifts the SID808 factory-drum guards from "musical shape exists"
// (waveform/envelope) to spectral authenticity. The audit asked for the actual
// sound identity to be measured, not just the register program:
//   * the kick must have real low-frequency (sub) body, not a click;
//   * the open hat must be brighter and ring longer than the closed hat;
//   * the clap must be a genuine multi-burst transient (peaks + valleys).
//
// Metrics are computed by a small windowed DFT over the rendered 48 kHz output,
// with deliberately generous margins so the checks track sound identity rather
// than exact synthesis coefficients.

#include "arpsid/engines/sid808_engine.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr double kSr = 48000.0;

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "sid808_spectral_authenticity_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

int msToSamples(double ms) { return static_cast<int>(std::lround(kSr * (ms / 1000.0))); }

// Canonical factory slot used for all authenticity checks (see factory range
// 120..149 guarded by sid808_factory_musical_shape_v871_tests).
constexpr int kFactorySlot = 120;

std::vector<float> renderMono(ArpSID::Sid808Engine& engine, ArpSID::Sid808Drum drum,
                              std::uint8_t note, int frames) {
    std::vector<float> l((size_t)frames, 0.0f), r((size_t)frames, 0.0f);
    engine.allNotesOff();
    engine.noteOn(drum, 120u, note);
    engine.processBlock(l.data(), r.data(), frames);
    std::vector<float> mono((size_t)frames, 0.0f);
    for (int i = 0; i < frames; ++i) {
        require(std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]), "non-finite sample");
        mono[(size_t)i] = 0.5f * (l[(size_t)i] + r[(size_t)i]);
    }
    return mono;
}

// Hann-windowed DFT magnitude at frequency f over [begin,end).
double magAt(const std::vector<float>& x, int begin, int end, double f) {
    begin = std::clamp(begin, 0, (int)x.size());
    end = std::clamp(end, begin, (int)x.size());
    const int n = end - begin;
    if (n <= 1) return 0.0;
    double re = 0.0, im = 0.0;
    const double w = 2.0 * M_PI * f / kSr;
    for (int i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        const double s = static_cast<double>(x[(size_t)(begin + i)]) * hann;
        re += s * std::cos(w * i);
        im += s * std::sin(w * i);
    }
    return std::hypot(re, im) / n;
}

// Summed magnitude across log-spaced probe frequencies within [fLo,fHi].
double bandEnergy(const std::vector<float>& x, int begin, int end, double fLo, double fHi) {
    const int bins = 40;
    double sum = 0.0;
    for (int b = 0; b < bins; ++b) {
        const double t = static_cast<double>(b) / (bins - 1);
        const double f = fLo * std::pow(fHi / fLo, t);
        sum += magAt(x, begin, end, f);
    }
    return sum;
}

double spectralCentroid(const std::vector<float>& x, int begin, int end, double fLo, double fHi) {
    const int bins = 60;
    double num = 0.0, den = 0.0;
    for (int b = 0; b < bins; ++b) {
        const double t = static_cast<double>(b) / (bins - 1);
        const double f = fLo * std::pow(fHi / fLo, t);
        const double m = magAt(x, begin, end, f);
        num += f * m;
        den += m;
    }
    return den > 0.0 ? num / den : 0.0;
}

double rmsRegion(const std::vector<float>& x, int begin, int end) {
    begin = std::clamp(begin, 0, (int)x.size());
    end = std::clamp(end, begin, (int)x.size());
    double s = 0.0;
    int n = 0;
    for (int i = begin; i < end; ++i) { s += (double)x[(size_t)i] * (double)x[(size_t)i]; ++n; }
    return n > 0 ? std::sqrt(s / n) : 0.0;
}

void testKickHasLowFrequencyBody() {
    using namespace ArpSID;
    Sid808Engine engine;
    engine.prepare(kSr);
    require(applyFactorySid808Kit(kFactorySlot, engine), "factory SID808 kit must apply");
    const auto kick = renderMono(engine, Sid808Drum::Kick, 36u, msToSamples(220.0));

    // Body window (skip the very first click transient).
    const int b0 = msToSamples(8.0), b1 = msToSamples(140.0);
    const double lowBand = bandEnergy(kick, b0, b1, 30.0, 250.0);
    const double highBand = bandEnergy(kick, b0, b1, 3000.0, 16000.0);
    require(lowBand > 0.0, "kick body must contain low-frequency energy");
    require(lowBand > 4.0 * highBand,
            "kick body must be low-frequency dominant (round sub, not a click)");

    const double centroid = spectralCentroid(kick, b0, b1, 30.0, 16000.0);
    require(centroid < 800.0, "kick spectral centroid must stay low (sub-body character)");
}

void testOpenHatBrighterAndLongerThanClosedHat() {
    using namespace ArpSID;
    Sid808Engine engine;
    engine.prepare(kSr);
    require(applyFactorySid808Kit(kFactorySlot, engine), "factory SID808 kit must apply");
    const auto openHat = renderMono(engine, Sid808Drum::OpenHat, 46u, msToSamples(400.0));
    const auto closedHat = renderMono(engine, Sid808Drum::ClosedHat, 42u, msToSamples(400.0));

    // Tail window: the defining open-vs-closed difference is sustain there.
    const int t0 = msToSamples(120.0), t1 = msToSamples(300.0);
    const double openTailHigh = bandEnergy(openHat, t0, t1, 4000.0, 16000.0);
    const double closedTailHigh = bandEnergy(closedHat, t0, t1, 4000.0, 16000.0);
    require(openTailHigh > closedTailHigh,
            "open hat must retain more high-band energy in the tail than the closed hat");

    // Both hats are metallic/noisy: high spectral centroid, clearly above a kick.
    const double openCentroid = spectralCentroid(openHat, msToSamples(5.0), msToSamples(120.0), 200.0, 18000.0);
    require(openCentroid > 2500.0, "open hat must be spectrally bright (metallic, not tonal)");

    Sid808Engine kEngine; kEngine.prepare(kSr);
    require(applyFactorySid808Kit(kFactorySlot, kEngine), "factory SID808 kit must apply");
    const auto kick = renderMono(kEngine, Sid808Drum::Kick, 36u, msToSamples(200.0));
    const double kickCentroid = spectralCentroid(kick, msToSamples(8.0), msToSamples(140.0), 30.0, 16000.0);
    require(openCentroid > 3.0 * kickCentroid,
            "open hat centroid must be far above the kick centroid");
}

void testClapIsMultiBurstWithValleys() {
    using namespace ArpSID;
    Sid808Engine engine;
    engine.prepare(kSr);
    require(applyFactorySid808Kit(kFactorySlot, engine), "factory SID808 kit must apply");
    const auto clap = renderMono(engine, Sid808Drum::Clap, 39u, msToSamples(150.0));

    // 808 clap = a burst train then a reverb tail. Measured against the actual
    // envelope, the deep inter-burst valleys land at ~6 ms and ~10.5 ms with
    // bursts around ~2, ~8 and ~11.5 ms. Requiring each burst to stand well above
    // the adjacent valleys proves a genuine multi-burst transient (peaks separated
    // by valleys) — the temporal identity the audit (P1-10) asked to be proven,
    // not one smeared blob.
    const double burst1  = rmsRegion(clap, msToSamples(1.0), msToSamples(4.5));
    const double valley1 = rmsRegion(clap, msToSamples(6.0), msToSamples(6.8));
    const double burst2  = rmsRegion(clap, msToSamples(7.5), msToSamples(9.0));
    const double valley2 = rmsRegion(clap, msToSamples(10.2), msToSamples(11.0));
    const double burst3  = rmsRegion(clap, msToSamples(11.3), msToSamples(12.8));

    require(burst1 > 0.0007, "clap first burst must be audible");
    require(burst2 > 0.0 && burst3 > 0.0, "clap must produce three bursts");
    // Each burst must clearly stand above the adjacent valley(s): true peak/valley.
    require(burst1 > valley1 * 2.2, "clap burst 1 must stand above the following valley");
    require(burst2 > valley1 * 2.2, "clap burst 2 must rise from the preceding valley");
    require(burst2 > valley2 * 2.0, "clap burst 2 must stand above the following valley");
    require(burst3 > valley2 * 1.6, "clap burst 3 must rise from the preceding valley");
}

} // namespace

int main() {
    testKickHasLowFrequencyBody();
    testOpenHatBrighterAndLongerThanClosedHat();
    testClapIsMultiBurstWithValleys();
    std::cout << "sid808_spectral_authenticity_v872_tests PASS\n";
    return 0;
}
