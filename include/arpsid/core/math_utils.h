// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ArpSID {

using Fixed64_32 = uint64_t;
static constexpr Fixed64_32 kArpSIDQ32One = 1ull << 32u;
static_assert(sizeof(Fixed64_32) == 8, "Fixed64_32 must be 64-bit");
static_assert(kArpSIDQ32One == UINT64_C(4294967296), "Q32 constant error");

static inline float ArpSID_fixedQ32ToFloat(Fixed64_32 q) noexcept {
    return static_cast<float>(static_cast<long double>(q) / 4294967296.0L);
}

// Shared pi constant for DSP code; avoids non-portable M_PI usage.
static constexpr double ArpSID_pi() noexcept {
    return 3.141592653589793238462643383279502884;
}

// Correct Hamming weight for 32-bit integers.
// Uses the standard branch-free bit-manipulation algorithm with unsigned
// intermediates, so there is no signed overflow or high-bit corruption.
static inline int ArpSID_popcount(uint32_t x) noexcept {
    x = x - ((x >> 1u) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2u) & 0x33333333u);
    x = (x + (x >> 4u)) & 0x0F0F0F0Fu;
    return static_cast<int>((x * 0x01010101u) >> 24u);
}

// Flush a float to zero if it is denormal/subnormal, NaN, or Inf.
// Normal finite values are returned unchanged.
static inline float ArpSID_flushDenormal(float v) noexcept {
    uint32_t bits = 0u;
    std::memcpy(&bits, &v, sizeof(bits));
    const uint32_t exponentBits = (bits >> 23u) & 0xFFu;

    // exponent==0   -> zero or subnormal
    // exponent==255 -> NaN or Inf
    if (exponentBits == 0u || exponentBits == 255u) {
        return 0.0f;
    }
    return v;
}

// Clamp to [0,1] and sanitize NaN/Inf/subnormal inputs.
static inline float ArpSID_sanitize01(float v) noexcept {
    if (!std::isfinite(v)) {
        return 0.0f;
    }
    v = ArpSID_flushDenormal(v);
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

// Sanitize an arbitrary float, replacing non-finite values with defaultValue.
// Denormals/subnormals are flushed to zero to avoid stalls in DSP paths.
static inline float ArpSID_sanitizeFloat(float v, float defaultValue = 0.0f) noexcept {
    if (!std::isfinite(v)) {
        return defaultValue;
    }
    return ArpSID_flushDenormal(v);
}

static inline uint8_t ArpSID_sanitizeNormToByte(float v) noexcept {
    const float clean = ArpSID_sanitize01(v);
    return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(clean * 255.0f + 0.5f), 0, 255));
}

static inline uint8_t ArpSID_sanitizeNormToNibble(float v) noexcept {
    const float clean = ArpSID_sanitize01(v);
    return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(clean * 15.0f + 0.5f), 0, 15));
}

static inline uint16_t ArpSID_sanitizeNormToUInt12(float v) noexcept {
    const float clean = ArpSID_sanitize01(v);
    return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(clean * 4095.0f + 0.5f), 0, 4095));
}

// Per-instance xorshift32 seed mixer.
// Combines a base seed with a discriminating value such as an instance counter
// or pointer hash so plugin instances do not share identical RNG streams.
static inline uint32_t ArpSID_mixSeed(uint32_t base, uint32_t discriminant) noexcept {
    uint32_t h = base ^ discriminant;
    h ^= h >> 16u;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13u;
    h *= 0xC2B2AE35u;
    h ^= h >> 16u;
    return (h == 0u) ? 0x6D2B79F5u : h;
}

// Canonical realtime-safe xorshift32 RNG.
// The zero-state is guarded before shifting, so the trap state never enters the
// recurrence.
static inline uint32_t ArpSID_xorshift32(uint32_t& s) noexcept {
    if (s == 0u) {
        s = 0x6D2B79F5u;
    }
    s ^= s << 13u;
    s ^= s >> 17u;
    s ^= s << 5u;
    return s;
}

static inline float ArpSID_rand_bipolar(uint32_t& s) noexcept {
    const uint32_t v = ArpSID_xorshift32(s) >> 8u; // 24-bit mantissa
    return static_cast<float>(v) * (1.0f / 16777215.0f) * 2.0f - 1.0f;
}

// Deterministic SID-cycle fixed-point timing helpers.
// Q32 stores physical SID cycles per host sample as unsigned 32.32 fixed-point.
// All sample<->cycle mapping below is integer after the rate pair has been
// quantized once. This avoids platform-dependent floor()/llround() boundary
// drift in render-time write scheduling and stamp ordering.
static inline uint64_t ArpSID_cyclesPerSampleQ32(double sampleRate, double sidClockHz) noexcept {
    if (!(std::isfinite(sampleRate) && sampleRate > 1.0) ||
        !(std::isfinite(sidClockHz) && sidClockHz > 1.0)) {
        return 0ull;
    }

    const long double q =
        (static_cast<long double>(sidClockHz) / static_cast<long double>(sampleRate)) * 4294967296.0L;
    if (!(q > 0.0L) || q >= static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
        return 0ull;
    }
    return static_cast<uint64_t>(q + 0.5L);
}

static inline uint64_t ArpSID_absoluteCycleAtSampleQ32(uint64_t sampleIndex,
                                                       uint64_t cyclesPerSampleQ32) noexcept {
#if defined(__SIZEOF_INT128__)
    const __uint128_t product =
        static_cast<__uint128_t>(sampleIndex) * static_cast<__uint128_t>(cyclesPerSampleQ32);
    const __uint128_t shifted = product >> 32u;
    return (shifted > static_cast<__uint128_t>(std::numeric_limits<uint64_t>::max()))
        ? std::numeric_limits<uint64_t>::max()
        : static_cast<uint64_t>(shifted);
#else
    const long double product =
        static_cast<long double>(sampleIndex) * static_cast<long double>(cyclesPerSampleQ32);
    return static_cast<uint64_t>(std::min<long double>(
        product / 4294967296.0L,
        static_cast<long double>(std::numeric_limits<uint64_t>::max())));
#endif
}

static inline uint16_t ArpSID_cyclesInHostSampleQ32(uint64_t sampleIndex,
                                                    uint64_t cyclesPerSampleQ32) noexcept {
    if (cyclesPerSampleQ32 == 0ull) {
        return 0u;
    }
    const uint64_t c0 = ArpSID_absoluteCycleAtSampleQ32(sampleIndex, cyclesPerSampleQ32);
    const uint64_t c1 = ArpSID_absoluteCycleAtSampleQ32(sampleIndex + 1ull, cyclesPerSampleQ32);
    return static_cast<uint16_t>(std::clamp<uint64_t>((c1 >= c0) ? (c1 - c0) : 0ull,
                                                     0ull,
                                                     0xFFFFull));
}

static inline uint32_t ArpSID_sampleForAbsoluteCycleQ32(uint64_t absoluteCycle,
                                                        uint64_t cyclesPerSampleQ32,
                                                        uint32_t maxSampleInclusive = 65535u) noexcept {
    if (cyclesPerSampleQ32 == 0ull || maxSampleInclusive == 0u) {
        return 0u;
    }

    uint32_t lo = 0u;
    uint32_t hi = maxSampleInclusive;
    while (lo < hi) {
        const uint32_t mid = static_cast<uint32_t>(lo + ((hi - lo + 1u) >> 1u));
        if (ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(mid), cyclesPerSampleQ32) <= absoluteCycle) {
            lo = mid;
        } else {
            hi = static_cast<uint32_t>(mid - 1u);
        }
    }

    while (lo < maxSampleInclusive &&
           ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(lo + 1u), cyclesPerSampleQ32) <= absoluteCycle) {
        ++lo;
    }
    while (lo > 0u &&
           ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(lo), cyclesPerSampleQ32) > absoluteCycle) {
        --lo;
    }
    return lo;
}

// Shared parameter mappings. Keep controller/UI, processor, and engines in
// lockstep so the same normalized value means the same real-world quantity
// everywhere in the code base.
static constexpr float ArpSID_kPortamentoMaxSeconds = 5.0f;
static constexpr float ArpSID_kDetuneMaxCents = 100.0f;

static inline float ArpSID_normToPortamentoSeconds(float norm) noexcept {
    const float v = ArpSID_sanitize01(norm);
    return v * v * ArpSID_kPortamentoMaxSeconds;
}

static inline float ArpSID_portamentoSecondsToNorm(float seconds) noexcept {
    const float s = std::clamp(std::isfinite(seconds) ? seconds : 0.0f,
                               0.0f, ArpSID_kPortamentoMaxSeconds);
    return std::sqrt(s / ArpSID_kPortamentoMaxSeconds);
}

static inline float ArpSID_normToDetuneCents(float norm) noexcept {
    const float v = ArpSID_sanitize01(norm);
    return (v - 0.5f) * (ArpSID_kDetuneMaxCents * 2.0f);
}

static inline float ArpSID_detuneCentsToNorm(float cents) noexcept {
    const float c = std::clamp(std::isfinite(cents) ? cents : 0.0f,
                               -ArpSID_kDetuneMaxCents, ArpSID_kDetuneMaxCents);
    return c / (ArpSID_kDetuneMaxCents * 2.0f) + 0.5f;
}

// v966 host-presentation lockstep laws. Each pair below is the single
// authority for one automatable quantity: the runtime/DSP side calls the
// normToX direction and the host display/parse service inverts through
// xToNorm. Wrappers must never re-derive these formulas locally.

// Exponential LFO rate curve (v568): rateHz = 0.1 × 200^norm, so
// norm 0 → 0.1 Hz, norm 0.5 → 1.41 Hz (geometric mean), norm 1 → 20 Hz.
static constexpr float ArpSID_kLfoRateMinHz = 0.1f;
static constexpr float ArpSID_kLfoRateSpanRatio = 200.0f;

static inline float ArpSID_normToLfoRateHz(float norm) noexcept {
    return ArpSID_kLfoRateMinHz * std::pow(ArpSID_kLfoRateSpanRatio, ArpSID_sanitize01(norm));
}

static inline float ArpSID_lfoRateHzToNorm(float hz) noexcept {
    const float maxHz = ArpSID_kLfoRateMinHz * ArpSID_kLfoRateSpanRatio;
    const float clamped = std::clamp(std::isfinite(hz) ? hz : ArpSID_kLfoRateMinHz,
                                     ArpSID_kLfoRateMinHz, maxHz);
    return ArpSID_sanitize01(std::log(clamped / ArpSID_kLfoRateMinHz) /
                             std::log(ArpSID_kLfoRateSpanRatio));
}

static constexpr float ArpSID_kLimiterAttackMaxMs = 20.0f;

static inline float ArpSID_normToLimiterAttackMs(float norm) noexcept {
    return ArpSID_sanitize01(norm) * ArpSID_kLimiterAttackMaxMs;
}

static inline float ArpSID_limiterAttackMsToNorm(float ms) noexcept {
    const float m = std::clamp(std::isfinite(ms) ? ms : 0.0f, 0.0f, ArpSID_kLimiterAttackMaxMs);
    return m / ArpSID_kLimiterAttackMaxMs;
}

static constexpr float ArpSID_kLimiterReleaseMinMs = 10.0f;
static constexpr float ArpSID_kLimiterReleaseSpanMs = 990.0f;

static inline float ArpSID_normToLimiterReleaseMs(float norm) noexcept {
    return ArpSID_kLimiterReleaseMinMs + ArpSID_sanitize01(norm) * ArpSID_kLimiterReleaseSpanMs;
}

static inline float ArpSID_limiterReleaseMsToNorm(float ms) noexcept {
    const float m = std::clamp(std::isfinite(ms) ? ms : ArpSID_kLimiterReleaseMinMs,
                               ArpSID_kLimiterReleaseMinMs,
                               ArpSID_kLimiterReleaseMinMs + ArpSID_kLimiterReleaseSpanMs);
    return (m - ArpSID_kLimiterReleaseMinMs) / ArpSID_kLimiterReleaseSpanMs;
}

static constexpr float ArpSID_kSeqTempoMinBpm = 20.0f;
static constexpr float ArpSID_kSeqTempoSpanBpm = 280.0f;

static inline float ArpSID_normToSeqTempoBpm(float norm) noexcept {
    return ArpSID_kSeqTempoMinBpm + ArpSID_sanitize01(norm) * ArpSID_kSeqTempoSpanBpm;
}

static inline float ArpSID_seqTempoBpmToNorm(float bpm) noexcept {
    const float b = std::clamp(std::isfinite(bpm) ? bpm : ArpSID_kSeqTempoMinBpm,
                               ArpSID_kSeqTempoMinBpm,
                               ArpSID_kSeqTempoMinBpm + ArpSID_kSeqTempoSpanBpm);
    return (b - ArpSID_kSeqTempoMinBpm) / ArpSID_kSeqTempoSpanBpm;
}

static constexpr int ArpSID_kSeqStepsMax = 32;

static inline int ArpSID_normToSeqSteps(float norm) noexcept {
    return std::clamp(static_cast<int>(std::lround(ArpSID_sanitize01(norm) *
                                                   static_cast<float>(ArpSID_kSeqStepsMax - 1))) + 1,
                      1, ArpSID_kSeqStepsMax);
}

static inline float ArpSID_seqStepsToNorm(int steps) noexcept {
    const int s = std::clamp(steps, 1, ArpSID_kSeqStepsMax);
    return static_cast<float>(s - 1) / static_cast<float>(ArpSID_kSeqStepsMax - 1);
}

static inline float ArpSID_detuneCentsToRatio(float cents) noexcept {
    const float clean = std::isfinite(cents) ? cents : 0.0f;
    return std::exp2f(clean * (1.0f / 1200.0f));
}

static inline float ArpSID_normToDetuneRatioOffset(float norm) noexcept {
    return ArpSID_detuneCentsToRatio(ArpSID_normToDetuneCents(norm)) - 1.0f;
}

// Canonical Hz -> SID frequency-register conversion. Rounding is important:
// truncation biases every generated note flat and is audible on low registers.
static inline std::uint16_t ArpSID_hzToSidFrequencyRegister(double hz,
                                                            double clockHz) noexcept {
    if (!std::isfinite(clockHz) || clockHz <= 0.0 ||
        !std::isfinite(hz) || hz <= 0.0) {
        return std::isinf(hz) && hz > 0.0 ? 65535u : 0u;
    }
    const double raw = hz * 16777216.0 / clockHz;
    if (!std::isfinite(raw)) return raw > 0.0 ? 65535u : 0u;
    return static_cast<std::uint16_t>(
        std::lround(std::clamp(raw, 0.0, 65535.0)));
}

} // namespace ArpSID
