// Copyright (C) 2024-2026 Ulf Bertilsson
// v956 DrSID factory-kit distinctness closure.
//
// The 40 canonical DrSID factory slots (80..119) are 8 primary-drum families x 5
// variants, but only the GM-percussion slots (112..119) had authored params —
// slots 80..111 all fell through to one generic default, so 32 slots produced
// the IDENTICAL kit. Root cause of the inaudibility of the variation: the default
// SidAuthentic drum model plays fixed canonical C64 drum microprograms and does
// not respond to the per-drum tune/decay/tone knobs (only AnalogX0X8 does).
//
// Fix: applyFactoryDrSidNewKitCharacter_() gives each generic slot (80..111) a
// deterministic per-(family,variant) character and selects the AnalogX0X8 model,
// so each slot is a genuinely distinct, tweakable kit with all 8 drums audible.
//
// This renders the real ArpSIDDSPKernel and requires (1) every drum audible, and
// (2) different slots to produce audibly different kicks.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "drsid_factory_kit_distinctness_v956_tests FAIL: %s\n", msg); std::exit(1); }
}

static std::vector<float> renderNote(int slot, int note, float& peak, int& machineModel) {
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    require(root.valid(), "DrSID factory slot must produce a valid root");
    sidCanonicalizeStateRootForApply(root);
    k->applyStateRootCanonical(root, false);

    constexpr int F = 512;
    std::array<float, F> l{}, r{};
    float* o[2] = { l.data(), r.data() };
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = F;
    k->processBlock(o, 2, F, nullptr, 0, t);
    machineModel = static_cast<int>(std::lround(k->runtimeParameterValues()[(size_t)kParamDrSidMachineModel]));

    std::vector<float> wave;
    peak = 0.0f;
    for (int b = 0; b < 16; ++b) {
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
            require(std::isfinite(l[(size_t)i]), "DrSID kit samples must remain finite");
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
    for (size_t i = 0; i < n; ++i) d += std::fabs(a[(size_t)i] - b[(size_t)i]);
    return d / static_cast<double>(std::max<size_t>(1, n));
}

int main() {
    ArpSID::prewarmAllSidTables();
    static const int drumNotes[8] = {36,38,42,46,39,56,47,37};

    // 1. Every drum audible on a couple of representative generic DrSID slots.
    for (int slot : {80, 100, 111}) {
        for (int d = 0; d < 8; ++d) {
            float pk = 0.0f; int mm = -1;
            (void)renderNote(slot, drumNotes[d], pk, mm);
            char msg[96];
            std::snprintf(msg, sizeof(msg), "DrSID slot %d drum %d must be audible", slot, d);
            require(pk > 0.03f, msg);
            require(mm == 1, "generic DrSID factory kits (80..111) must select AnalogX0X8 so knobs are expressive");
        }
    }

    // 2. Distinctness: different variants and families produce different kicks.
    float p0, p1, p5, p10; int m;
    const auto k80  = renderNote(80,  36, p0,  m);  // Kick family A
    const auto k81  = renderNote(81,  36, p1,  m);  // Kick family B (variant)
    const auto k85  = renderNote(85,  36, p5,  m);  // Snare family A
    const auto k90  = renderNote(90,  36, p10, m);  // ClosedHat family A
    std::printf("kick diffs: 80vs81=%.5f 80vs85=%.5f 80vs90=%.5f\n",
                meanAbsDiff(k80, k81), meanAbsDiff(k80, k85), meanAbsDiff(k80, k90));
    require(meanAbsDiff(k80, k81) > 1.0e-3, "adjacent DrSID kit variants must sound different (not aliases)");
    require(meanAbsDiff(k80, k85) > 1.0e-3, "different DrSID kit families must sound different");
    require(meanAbsDiff(k80, k90) > 1.0e-3, "different DrSID kit families must sound different");

    std::printf("drsid_factory_kit_distinctness_v956_tests PASS\n");
    return 0;
}
