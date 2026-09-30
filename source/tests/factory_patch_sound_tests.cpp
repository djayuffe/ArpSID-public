// Copyright (C) 2024-2026 Ulf Bertilsson
// Factory patch sound regressions (a quick subset of the full
// arpsid_factory_patch_sound_audit tool):
//   - the Forensic System Noise hiss follows voice activity, so a patch is
//     silent (below -120 dBFS) once its notes have released (SYNTH and CLASSIC);
//   - the final output stage removes DC (the CLASSIC init sound and the
//     overdriven guitars used to carry a steady offset);
//   - every sample is finite and below full scale.

#include "vst3/arpsid_vst3_kernel_host.h"

#include "au3/ArpSIDCanonicalEvents.h"
#include "parameter_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace ArpSID;

namespace {

int failures = 0;
void check(bool ok, int slot, const char* msg, double value) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: slot %d: %s (%g)\n", slot, msg, value);
        ++failures;
    }
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Result {
    double holdDc = 0.0, holdPeak = 0.0, tailPeak = 0.0;
    long nonFinite = 0;
};

Result render(int slot) {
    Vst3KernelHost h;
    h.setup(kRate, kBlock);
    if (slot >= 0) h.loadFactorySlot(slot);
    std::vector<float> l(kBlock), r(kBlock);
    float* out[2] = {l.data(), r.data()};
    TransportState t{};
    t.sampleRate = kRate;
    t.frameCount = kBlock;
    Result res;
    auto run = [&](int blocks, const TimedEvent* ev, double* sum, long* n, double* peak) {
        for (int b = 0; b < blocks; ++b) {
            h.render(out, 2, kBlock, b == 0 ? ev : nullptr, (b == 0 && ev) ? 1 : 0, t);
            for (int i = 0; i < kBlock; ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { ++res.nonFinite; continue; }
                if (sum) *sum += 0.5 * ((double)l[i] + r[i]);
                if (n) ++*n;
                if (peak) *peak = std::max({*peak, (double)std::fabs(l[i]), (double)std::fabs(r[i])});
            }
        }
    };
    run(47, nullptr, nullptr, nullptr, nullptr);  // 0.5 s: power-on settles
    TimedEvent on{};
    on.kind = EventKind::NoteOn;
    on.pitch = 60;
    on.value = 0.9f;
    double sum = 0.0;
    long n = 0;
    run(94, &on, &sum, &n, &res.holdPeak);  // ~1 s held
    res.holdDc = n ? std::fabs(sum / n) : 0.0;
    TimedEvent off = on;
    off.kind = EventKind::NoteOff;
    off.value = 0.0f;
    run(470, &off, nullptr, nullptr, nullptr);  // 5 s release
    run(94, nullptr, nullptr, nullptr, &res.tailPeak);  // last second
    return res;
}

} // namespace

int main() {
    // -1 = init sound (CLASSIC), 0 piano, 29/30 overdriven guitars,
    // 40 violin (reverb), 80 synth lead region.
    for (int slot : {-1, 0, 29, 30, 40, 80}) {
        const Result r = render(slot);
        check(r.nonFinite == 0, slot, "non-finite samples", (double)r.nonFinite);
        check(r.holdPeak > 0.01, slot, "note is silent", r.holdPeak);
        check(r.holdPeak < 0.999, slot, "note clips", r.holdPeak);
        check(r.holdDc < 0.002, slot, "DC offset while a note is held", r.holdDc);
        check(r.tailPeak < 1.0e-6, slot, "output not silent (-120 dBFS) after release", r.tailPeak);
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("factory patch sound tests passed\n");
    return 0;
}
