// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_snare_one_shot_v867_tests.cpp
//
// v867 guards the "wrong SID808 snare" audit:
// - factory snares must include noise, not pulse-only tone;
// - factory one-shot drums must not author nonzero sustain;
// - rendered factory snares must decay instead of droning after the hit.

#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "sid808_snare_one_shot_v867_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

template <std::size_t N>
float rmsForSpan(const std::array<float, N>& l,
                 const std::array<float, N>& r,
                 std::size_t begin,
                 std::size_t end) {
    begin = std::min(begin, N);
    end = std::clamp(end, begin, N);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = begin; i < end; ++i) {
        require(std::isfinite(l[i]) && std::isfinite(r[i]), "render produced non-finite sample");
        sum += static_cast<double>(l[i]) * static_cast<double>(l[i]);
        sum += static_cast<double>(r[i]) * static_cast<double>(r[i]);
        count += 2u;
    }
    return count > 0u ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

template <std::size_t N>
float zeroCrossingRate(const std::array<float, N>& l, std::size_t begin, std::size_t end) {
    begin = std::min(begin, N);
    end = std::clamp(end, begin, N);
    int previousSign = 0;
    std::size_t crossings = 0;
    std::size_t observed = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const float x = l[i];
        if (std::fabs(x) < 1.0e-5f) continue;
        const int sign = x > 0.0f ? 1 : -1;
        if (previousSign != 0 && sign != previousSign) ++crossings;
        previousSign = sign;
        ++observed;
    }
    return observed > 1u ? static_cast<float>(crossings) / static_cast<float>(observed - 1u) : 0.0f;
}

void testFactoryConfigShape() {
    using namespace ArpSID;
    for (int slot = 120; slot <= 149; ++slot) {
        const Sid808KitConfigTable kit = factorySid808ResolvedKitForSlot(slot);
        const Sid808VoiceConfig& snare = kit[static_cast<std::size_t>(Sid808Drum::Snare)];
        require((sid808NormalizeWaveformControl(snare.waveform) & 0x80u) != 0u,
                "SID808 factory snare must include noise");
        require(((snare.sustainRelease >> 4u) & 0x0Fu) == 0u,
                "SID808 factory snare must not sustain");

        for (std::size_t i = 0; i < kit.size(); ++i) {
            const Sid808VoiceConfig& cfg = kit[i];
            require(((cfg.sustainRelease >> 4u) & 0x0Fu) == 0u,
                    "SID808 factory one-shot drum must not author nonzero sustain");
        }
    }
}

void testFactorySnareTailAndNoise() {
    using namespace ArpSID;
    constexpr std::size_t kFrames = 48000u;
    constexpr std::size_t kAttackEnd = 2400u;   // 50 ms @ 48 kHz
    constexpr std::size_t kTailBegin = 24000u;  // 500 ms @ 48 kHz

    std::array<float, kFrames> l{};
    std::array<float, kFrames> r{};

    for (int slot = 120; slot <= 149; ++slot) {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        require(bridge.loadFactorySlot(slot), "SID808 factory slot must load");
        l.fill(0.0f);
        r.fill(0.0f);
        bridge.noteOn(SidGMDrumClass::Snare, 120u, 38u);
        bridge.processBlock(l.data(), r.data(), static_cast<int>(kFrames));

        const float attackRms = rmsForSpan(l, r, 0u, kAttackEnd);
        const float tailRms = rmsForSpan(l, r, kTailBegin, kFrames);
        // The pulse+noise snare has its clearest noise signature at the transient;
        // later samples are dominated by the tonal pulse body's decay.
        const float zcr = zeroCrossingRate(l, 0u, 1024u);
        if (!(attackRms > 0.001f && tailRms < attackRms * 0.02f && zcr > 0.045f)) {
            std::cerr << "slot " << slot
                      << " attackRms=" << attackRms
                      << " tailRms=" << tailRms
                      << " zcr=" << zcr << "\n";
        }
        require(attackRms > 0.001f, "SID808 factory snare must be audible at attack");
        require(tailRms < attackRms * 0.02f, "SID808 factory snare must not drone after 500 ms");
        require(zcr > 0.045f, "SID808 factory snare transient must have noise-like crossing density");
        require(bridge.sid808Engine().activeVoiceCount() == 0u,
                "SID808 one-shot auto-release must free the snare voice after tail");
    }
}

} // namespace

int main() {
    testFactoryConfigShape();
    testFactorySnareTailAndNoise();
    std::cout << "sid808_snare_one_shot_v867_tests PASS\n";
    return 0;
}
