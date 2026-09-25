#pragma once
#include <cstdint>
#include <cmath>
#include "sid_variant_profile.h"

namespace ArpSID {

enum class SIDModel : uint8_t;

inline constexpr bool sidVariantUsesNtscClock(const SidVariantProfile& profile) noexcept {
    return profile.video_standard == SidVideoStandard::NTSC;
}

inline float sidVariantClockHz(const SidVariantProfile& profile) noexcept {
    return std::isfinite(profile.nominal_sid_clock_hz) && profile.nominal_sid_clock_hz > 1.0f
        ? profile.nominal_sid_clock_hz
        : sidDefaultClockHz(profile.video_standard);
}

inline constexpr uint8_t sidSystemByteFromVariantProfile(const SidVariantProfile& profile, bool adsrBug6581) noexcept {
    uint8_t sysb = 0;
    if (profile.family == SidFamily::MOS8580) sysb |= 0x02u;
    if (sidVariantUsesNtscClock(profile))     sysb |= 0x01u;
    if (adsrBug6581)                         sysb |= 0x04u;
    return sysb;
}

} // namespace ArpSID
