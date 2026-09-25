#pragma once

#include <cmath>
#include <cstdint>

namespace ArpSID {

static constexpr double kCanonicalDefaultSampleRate = 44100.0;
static constexpr double kCanonicalMinSampleRate = 8000.0;
static constexpr double kCanonicalMaxSampleRate = 384000.0;

inline bool isCanonicalHostSampleRate(double sr) noexcept {
    return std::isfinite(sr) && sr >= kCanonicalMinSampleRate && sr <= kCanonicalMaxSampleRate;
}

inline double canonicalizeHostSampleRate(double sr, double fallback = kCanonicalDefaultSampleRate) noexcept {
    if (isCanonicalHostSampleRate(sr)) {
        static constexpr double kRates[] = {
            8000.0, 11025.0, 16000.0, 22050.0, 24000.0,
            32000.0, 44100.0, 48000.0,
            88200.0, 96000.0,
            176400.0, 192000.0,
            352800.0, 384000.0
        };
        for (double r : kRates) {
            if (std::fabs(sr - r) <= 0.5) return r;
        }
        return sr;
    }
    if (isCanonicalHostSampleRate(fallback)) {
        static constexpr double kRates[] = {
            8000.0, 11025.0, 16000.0, 22050.0, 24000.0,
            32000.0, 44100.0, 48000.0,
            88200.0, 96000.0,
            176400.0, 192000.0,
            352800.0, 384000.0
        };
        for (double r : kRates) {
            if (std::fabs(fallback - r) <= 0.5) return r;
        }
        return fallback;
    }
    return kCanonicalDefaultSampleRate;
}

inline bool hostSampleRateLooksBogusOrMismatched(double requested, double negotiated) noexcept {
    if (!isCanonicalHostSampleRate(requested)) return true;
    if (!isCanonicalHostSampleRate(negotiated)) return true;
    const double n = canonicalizeHostSampleRate(negotiated);
    const double r = canonicalizeHostSampleRate(requested);
    return std::fabs(r - n) > 0.5;
}

} // namespace ArpSID
