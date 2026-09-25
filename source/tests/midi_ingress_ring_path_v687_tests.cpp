// Ingress-path parity: a NoteOn delivered through the midiQueue_ ingress ring
// (enqueueMidiIntent — the host MusicDevice / UI MIDI path) must produce the
// SAME audio as the identical NoteOn delivered through the processBlock
// events[] parameter.
//
// v687 originally only asserted the ring path was non-silent, which let a
// ~42x (-32.6 dB) ingress mismatch pass: the transport-start edge flushed the
// live midiQueue_ (clearRuntimeStateForTransportStart_ →
// resetTransientRenderState_(true) → midiQueue_.clear()) before the drain, so
// any note arriving as the transport started was silently eaten while host
// events[] survived. v910 fixes the flush, dispatches both paths through the
// single dispatchCanonicalIngressEvent_ authority, and this test now enforces
// hard parity: same RMS, same peak, same first-audible sample.
#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace ArpSID;

struct PathResult {
    double rms = 0.0;
    double peak = 0.0;
    long firstAudible = -1;
};

static PathResult renderPath(ArpSIDDSPKernel& k, bool useRing, int blocks) {
    constexpr int frames = 1024;
    std::array<float, frames> l{}, r{};
    float* outs[2] = {l.data(), r.data()};
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = frames;

    PathResult res{};
    double sum = 0.0; long n = 0;
    for (int b = 0; b < blocks; ++b) {
        if (b == 0) {
            if (useRing) {
                const uint8_t noteOn[3] = {0x99, 36, 100}; // ch10 NoteOn note36 vel100
                k.enqueueMidiIntent(noteOn, 3, 0, 0);       // midiQueue_ ring path
                k.processBlock(outs, 2, frames, nullptr, 0, t); // EMPTY events param
            } else {
                TimedEvent ev{};
                ev.sampleOffset = 0; ev.kind = EventKind::NoteOn;
                ev.channel = 9; ev.pitch = 36;
                ev.value = 100.0f / 127.0f; // SAME velocity as the ring bytes
                k.processBlock(outs, 2, frames, &ev, 1, t);     // events[] path
            }
        } else {
            k.processBlock(outs, 2, frames, nullptr, 0, t);
        }
        for (int i = 0; i < frames; ++i) {
            const double v = 0.5 * (l[(size_t)i] + r[(size_t)i]);
            sum += v * v; ++n;
            const double av = std::fabs(v);
            if (av > res.peak) res.peak = av;
            if (res.firstAudible < 0 && av > 1.0e-4)
                res.firstAudible = (long)b * frames + i;
        }
    }
    res.rms = std::sqrt(sum / (double)std::max(1L, n));
    return res;
}

static std::unique_ptr<ArpSIDDSPKernel> makeKick() {
    constexpr int slot = 120;
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    std::array<float, kNumParams> edited{};
    for (int pid = 0; pid < kNumParams; ++pid) edited[(size_t)pid] = sidStateRootParamValue(root, pid);
    edited[(size_t)kParamDrSidEnable] = 1.0f;
    edited[(size_t)kParamSynthModeEnable] = 0.0f;
    edited[(size_t)kParamDrSidMachineModel] = 1.0f;
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    k->setStickyPresetDisplaySlot(slot);
    k->resetPreservingHostParameterSnapshot(edited.data(), kNumParams, slot, true);
    return k;
}

int main() {
    prewarmAllSidTables();

    const PathResult ev = renderPath(*makeKick(), /*useRing=*/false, 6);
    const PathResult ring = renderPath(*makeKick(), /*useRing=*/true, 6);

    std::printf("events[] RMS=%.6f peak=%.4f first=%ld | ring RMS=%.6f peak=%.4f first=%ld\n",
                ev.rms, ev.peak, ev.firstAudible, ring.rms, ring.peak, ring.firstAudible);

    if (ev.rms <= 1.0e-4) {
        std::fprintf(stderr, "FAIL: events-param path itself is silent (test setup issue)\n");
        return 2;
    }
    if (ring.rms <= 1.0e-4) {
        std::fprintf(stderr, "FAIL: midiQueue_ ingress-ring NoteOn produced SILENCE\n");
        return 1;
    }
    // v910 parity contract: same event through both ingress paths must produce
    // equivalent audio. Ratio window per the v909 audit rule (0.8 .. 1.25);
    // the unified dispatch authority currently produces bit-identical audio.
    const double rmsRatio = ring.rms / ev.rms;
    if (rmsRatio < 0.8 || rmsRatio > 1.25) {
        std::fprintf(stderr,
                     "FAIL: ring/events RMS ratio %.4f outside [0.8, 1.25] — ingress paths diverged\n",
                     rmsRatio);
        return 1;
    }
    const double peakRatio = ring.peak / std::max(1.0e-12, ev.peak);
    if (peakRatio < 0.8 || peakRatio > 1.25) {
        std::fprintf(stderr,
                     "FAIL: ring/events peak ratio %.4f outside [0.8, 1.25]\n", peakRatio);
        return 1;
    }
    if (ev.firstAudible != ring.firstAudible) {
        std::fprintf(stderr,
                     "FAIL: first-audible sample differs (events=%ld ring=%ld)\n",
                     ev.firstAudible, ring.firstAudible);
        return 1;
    }
    std::printf("midi_ingress_ring_path_v687_tests: PASS (ring/events ingress parity, ratio=%.4f)\n",
                rmsRatio);
    return 0;
}
