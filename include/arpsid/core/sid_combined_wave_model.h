#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "math_utils.h"

namespace ArpSID {

struct SidCombinedWaveState {
    uint32_t stochasticSeed = 0x87654321u;
    uint8_t  lastWaveformWrite = 0u;
    uint32_t testBitCycleStamp = 0u;
};

template <typename ForensicConfig>
static inline uint8_t sidAnalogCombined12_Ultra(SidCombinedWaveState& state,
                                                uint8_t tri,
                                                uint8_t pulse,
                                                uint8_t wfCtrl,
                                                uint32_t cycle,
                                                const ForensicConfig& forensic) noexcept {
    uint8_t base = static_cast<uint8_t>(tri & pulse);

    const bool hasTriPulse = (wfCtrl & 0x50u) == 0x50u; // SID waveform ctrl: TRI=$10, PULSE=$40
    if (hasTriPulse) {
        float sag = 0.965f;
        if (forensic.active()) {
            sag -= (ArpSID_sanitizeFloat(forensic.junctionTempC, 25.0f) - 25.0f) * 0.00085f;
            sag -= (ArpSID_sanitizeFloat(forensic.supplyVoltage, 5.0f) - 5.0f) * 0.0135f;
        }
        sag = std::clamp(sag, 0.81f, 0.99f);
        base = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(static_cast<float>(base) * sag)), 0, 255));
        if (base > 0xFCu) base = 0xFCu;
    }

    if (wfCtrl & 0x08u) { // TEST bit
        state.testBitCycleStamp = cycle;
        return 0x00u;
    }

    if (wfCtrl & 0x80u) { // NOISE selected; latch current writeback byte for state/serialization.
        state.lastWaveformWrite = base;
    }

    return base;
}

// v895 single-authority SID pulse comparator. The audit found THREE separately
// maintained copies of this law (SIDVoice::generatePulse12, the register
// engine's sidRegisterPulseComparator12, and an inline lambda in
// SidReadbackModel that had silently DROPPED the 6581 comparator bias — a
// cross-engine split-brain where $D41B OSC3 readback of extreme pulse widths
// disagreed with the rendered audio comparator by 1-2 accumulator steps on
// 6581). All three now delegate here.
//
// Law (v854/v893 exactness lineage):
// * PW=$000: comparator t >= 0 is always true — constant high on both models.
//   The early return also bypasses the 6581 low-width bias, which must not
//   apply to the degenerate always-high case.
// * PW=$FFF is NOT constant low: t >= $FFF fires exactly when the top 12
//   accumulator bits equal $FFF (1/4096-duty spike train).
// * 6581 leak/asymmetry is modeled as comparator bias only (+1 for PW<=$020,
//   +2 for PW>=$F00) — register state is never mutated.
// * TEST forces the comparator output HIGH (reSID law, test-bit digis).
static inline uint16_t sidPulseComparator12(uint16_t phaseTop12,
                                            uint16_t pw12,
                                            bool is6581,
                                            bool test = false) noexcept {
    if (test) return 0x0FFFu;
    const uint16_t rawPw = static_cast<uint16_t>(pw12 & 0x0FFFu);
    if (rawPw == 0x000u) return 0x0FFFu;
    uint16_t effectivePw = rawPw;
    if (is6581) {
        if (rawPw <= 0x020u) effectivePw = static_cast<uint16_t>(std::min<uint16_t>(0x0FFFu, rawPw + 1u));
        else if (rawPw >= 0x0F00u) effectivePw = static_cast<uint16_t>(std::min<uint16_t>(0x0FFFu, rawPw + 2u));
    }
    return ((phaseTop12 & 0x0FFFu) >= effectivePw) ? 0x0FFFu : 0x0000u;
}

static inline uint16_t sidCombinedApply6581Wave12Sag(uint16_t value, bool is6581, bool hasTri, bool hasPulse, bool hasSaw, bool hasNoise, uint8_t revision) noexcept {
    uint16_t v = static_cast<uint16_t>(value & 0x0FFFu);
    if (!is6581 || hasNoise) return v;
    const bool triPulse = hasTri && hasPulse && !hasSaw;
    const bool complex12 = hasTri && hasPulse;
    if (!complex12) return v;
    const uint16_t ceiling = triPulse ? static_cast<uint16_t>(0x0FC0u) : static_cast<uint16_t>(0x0FE0u);
    if (v > ceiling) v = ceiling;
    if (v > 0x0E00u) {
        const uint32_t sag = static_cast<uint32_t>((v - 0x0E00u) * (revision <= 3u ? 5u : 3u)) >> 5u;
        v = static_cast<uint16_t>((v > sag) ? (v - sag) : 0u);
    }
    return static_cast<uint16_t>(v & 0x0FFFu);
}

static inline uint16_t sidCombinedBlendNeighborBits12(uint16_t x) noexcept {
    uint16_t out = 0u;
    for (int bit = 0; bit < 12; ++bit) {
        const uint16_t self = static_cast<uint16_t>((x >> bit) & 1u);
        const uint16_t left = (bit > 0) ? static_cast<uint16_t>((x >> (bit - 1)) & 1u) : self;
        const uint16_t right = (bit < 11) ? static_cast<uint16_t>((x >> (bit + 1)) & 1u) : self;
        const uint16_t v = static_cast<uint16_t>(std::min<uint16_t>(1u, self + ((left + right) >= 2u ? 1u : 0u)));
        out |= static_cast<uint16_t>(v << bit);
    }
    return static_cast<uint16_t>(out & 0x0FFFu);
}

static inline uint16_t sidCombinedSmooth12Tap(uint16_t x, int bit) noexcept {
    const int b0 = static_cast<int>((x >> std::clamp(bit - 2, 0, 11)) & 1u);
    const int b1 = static_cast<int>((x >> std::clamp(bit - 1, 0, 11)) & 1u);
    const int b2 = static_cast<int>((x >> bit) & 1u);
    const int b3 = static_cast<int>((x >> std::clamp(bit + 1, 0, 11)) & 1u);
    const int b4 = static_cast<int>((x >> std::clamp(bit + 2, 0, 11)) & 1u);
    return static_cast<uint16_t>(b0 + b1 * 2 + b2 * 3 + b3 * 2 + b4);
}

static inline uint16_t sidCombinedBitWeightedLadder12(uint16_t x, bool is6581) noexcept {
    static constexpr uint16_t w6581[12] = { 2, 4, 8, 15, 29, 57, 114, 228, 456, 912, 1826, 3644 };
    static constexpr uint16_t w8580[12] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048 };
    const uint16_t* w = is6581 ? w6581 : w8580;
    uint32_t acc = 0u;
    uint32_t full = 0u;
    for (int bit = 0; bit < 12; ++bit) {
        full += static_cast<uint32_t>(w[bit]);
        if ((x >> bit) & 1u) acc += static_cast<uint32_t>(w[bit]);
    }
    if (full == 0u) return 0u;
    return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(static_cast<double>(acc) * 4095.0 / static_cast<double>(full))), 0, 4095));
}

static inline uint8_t sidCombinedRevisionForModel(uint8_t revision, bool is6581) noexcept {
    if (is6581) return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(revision), 2, 4));
    return 5u;
}

static inline uint16_t sidAnalogCombined12_Ultra(uint16_t tri12,
                                                 uint16_t saw12,
                                                 uint16_t pul12,
                                                 uint16_t noise12,
                                                 bool hasTri,
                                                 bool hasSaw,
                                                 bool hasPulse,
                                                 bool hasNoise,
                                                 bool is6581,
                                                 uint16_t lastCombinedWave,
                                                 float temperatureCelsius,
                                                 float supplyVoltage,
                                                 uint8_t revision,
                                                 uint32_t& stochasticSeed) noexcept {
    uint16_t src[4]{};
    bool srcIsNoise[4]{};
    int ns = 0;
    if (hasTri)   src[ns] = static_cast<uint16_t>(tri12 & 0x0FFFu),  srcIsNoise[ns++] = false;
    if (hasSaw)   src[ns] = static_cast<uint16_t>(saw12 & 0x0FFFu),  srcIsNoise[ns++] = false;
    if (hasPulse) src[ns] = static_cast<uint16_t>(pul12 & 0x0FFFu),  srcIsNoise[ns++] = false;
    if (hasNoise) src[ns] = static_cast<uint16_t>(noise12 & 0x0FFFu), srcIsNoise[ns++] = true;
    if (ns <= 0) return 0u;
    if (ns == 1) return src[0];

    revision = sidCombinedRevisionForModel(revision, is6581);
    const float temp = std::clamp(std::isfinite(temperatureCelsius) ? temperatureCelsius : 35.0f, 20.0f, 60.0f);
    const float supply = std::clamp(std::isfinite(supplyVoltage) ? supplyVoltage : 5.0f, 4.5f, 5.5f);

    static constexpr float leakBase[6] = {0.0f, 0.0f, 0.048f, 0.041f, 0.035f, 0.018f};
    float leak = leakBase[revision];
    const float tempFactor = 1.0f + (temp - 25.0f) * (is6581 ? 0.0092f : 0.0048f);
    const float voltFactor = (supply - 5.0f) * (is6581 ? 0.18f : 0.09f) + 1.0f;
    leak = std::clamp(leak * tempFactor * voltFactor, 0.0f, is6581 ? 0.24f : 0.12f);

    const uint16_t prev = static_cast<uint16_t>(lastCombinedWave & 0x0FFFu);
    uint16_t driveMask = 0u;
    for (int i = 0; i < ns; ++i) driveMask |= src[i];

    const uint32_t callStableSeed = (stochasticSeed != 0u)
        ? stochasticSeed
        : (0xA341316Cu ^ static_cast<uint32_t>(prev)
            ^ (static_cast<uint32_t>(driveMask) << 12u)
            ^ static_cast<uint32_t>(revision));

    double charge[12]{};
    for (int bit = 0; bit < 12; ++bit) {
        double drive = 0.0;
        double strength = 0.0;
        for (int i = 0; i < ns; ++i) {
            const uint16_t s = src[i];
            const double center = ((s >> bit) & 1u) ? 1.0 : 0.0;
            const double local = static_cast<double>(sidCombinedSmooth12Tap(s, bit)) * (1.0 / 9.0);
            const double w = srcIsNoise[i] ? (is6581 ? 0.68 : 0.52) : 1.0;
            drive += (0.72 * center + 0.28 * local) * w;
            strength += w;
        }
        const double prevMem = ((prev >> bit) & 1u) ? static_cast<double>(leak) : 0.0;
        charge[bit] = (strength > 0.0 ? drive / strength : 0.0) + prevMem;
    }

    const double selfKeep = is6581 ? 0.672 : 0.821;
    const double neighborPull = is6581 ? 0.241 : 0.119;
    const double farPull = is6581 ? 0.058 : 0.023;
    const double drivenBoost = is6581 ? 1.041 : 1.009;

    for (int pass = 0; pass < 12; ++pass) {
        double next[12]{};
        for (int bit = 0; bit < 12; ++bit) {
            const double left = (bit > 0) ? charge[bit - 1] : charge[bit];
            const double right = (bit < 11) ? charge[bit + 1] : charge[bit];
            const double farLeft = (bit > 1) ? charge[bit - 2] : charge[bit];
            const double farRight = (bit < 10) ? charge[bit + 2] : charge[bit];

            // Deterministic per-instance impurity. Do not advance mutable RNG state
            // inside the inner render loop; that made combined waveforms depend
            // on call history across live/offline paths.
            uint32_t impuritySeed = callStableSeed
                ^ (static_cast<uint32_t>(pass + 1) * 0x9E3779B9u)
                ^ (static_cast<uint32_t>(bit + 1)  * 0x85EBCA6Bu)
                ^ (static_cast<uint32_t>(driveMask) << 16u)
                ^ static_cast<uint32_t>(prev);
            impuritySeed = ArpSID_xorshift32(impuritySeed);
            const double impurity = (static_cast<double>(impuritySeed & 0xFFFFu) / 65535.0)
                                  * (is6581 ? 0.022 : 0.009);
            const double driven = ((driveMask >> bit) & 1u) ? drivenBoost : 0.0;

            next[bit] = selfKeep * charge[bit]
                      + neighborPull * 0.5 * (left + right)
                      + farPull * 0.5 * (farLeft + farRight)
                      + driven
                      + impurity;
        }
        for (int bit = 0; bit < 12; ++bit)
            charge[bit] = std::clamp(next[bit], 0.0, 1.38);
    }

    uint16_t out = 0u;
    for (int bit = 0; bit < 12; ++bit) {
        double th = is6581 ? 0.528 : 0.571;
        if (bit >= 9) th += is6581 ? 0.045 : 0.026;

        if (hasNoise && (hasTri || hasSaw || hasPulse)) {
            static constexpr float special[4][12] = {
                {0.51f,0.49f,0.47f,0.52f,0.48f,0.53f,0.46f,0.54f,0.45f,0.55f,0.44f,0.56f},
                {0.50f,0.50f,0.48f,0.51f,0.49f,0.52f,0.47f,0.53f,0.46f,0.54f,0.45f,0.55f},
                {0.49f,0.51f,0.49f,0.50f,0.50f,0.51f,0.48f,0.52f,0.47f,0.53f,0.46f,0.54f},
                {0.48f,0.52f,0.50f,0.49f,0.51f,0.50f,0.49f,0.51f,0.48f,0.52f,0.47f,0.53f}
            };
            const int idx = static_cast<int>(sidCombinedRevisionForModel(revision, is6581)) - 2;
            th = special[idx][bit];
        }

        if (charge[bit] >= th) out |= static_cast<uint16_t>(1u << bit);
    }

    uint32_t mixed = static_cast<uint32_t>(out & 0x0FFFu);
    const uint16_t edgeSmeared = sidCombinedBlendNeighborBits12(static_cast<uint16_t>(mixed));
    const uint16_t ladderMixed = sidCombinedBitWeightedLadder12(static_cast<uint16_t>(mixed), is6581);

    if (hasNoise && (hasTri || hasSaw || hasPulse)) {
        mixed = (mixed * static_cast<uint32_t>(is6581 ? 3u : 4u)
               + static_cast<uint32_t>(prev) * static_cast<uint32_t>(is6581 ? 2u : 1u)
               + static_cast<uint32_t>(noise12 & 0x0FFFu)
               + static_cast<uint32_t>(edgeSmeared)) / static_cast<uint32_t>(is6581 ? 7u : 6u);
    }

    mixed = is6581
        ? (mixed * 2u + static_cast<uint32_t>(edgeSmeared) * 2u + static_cast<uint32_t>(prev) + static_cast<uint32_t>(ladderMixed)) / 6u
        : (mixed * 5u + static_cast<uint32_t>(edgeSmeared) + static_cast<uint32_t>(prev) + static_cast<uint32_t>(ladderMixed)) / 8u;

    uint16_t result = static_cast<uint16_t>(mixed & 0x0FFFu);
    result = sidCombinedApply6581Wave12Sag(result, is6581, hasTri, hasPulse, hasSaw, hasNoise, revision);
    // Determinism guard: this renderer must be a pure function of oscillator
    // state plus the caller-supplied per-voice seed. Advancing stochasticSeed
    // here made offline/live renders diverge when hosts split buffers
    // differently. Voice/block owners may rotate their seed outside this
    // per-sample combined-wave function if they intentionally want slow analog
    // drift; the hot waveform law itself is call-history independent.
    return result;
}

} // namespace ArpSID
