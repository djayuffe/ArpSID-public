#pragma once

#include <algorithm>
#include <cmath>

namespace ArpSID {

// Canonical DrSID output bus law.
//
// DrSID renders through a real SIDChip core plus a small transient overlay. The
// raw SIDChip core is intentionally calibrated like a single SID, but drum kits
// are short, sparse envelopes and were previously attenuated twice by
// MasterVolume * DrSidVolume and then again by the DrSID render accent floor.
// Keeping the makeup here makes all AU/VST/projection paths use one law instead
// of each caller hand-multiplying levels differently.
inline constexpr float kDrSidBusMakeupGain = 1.08f;
inline constexpr float kDrSidMaxInternalBusGain = 1.00f;
inline constexpr float kDrSidPostSidHeadroom = 0.82f;
inline constexpr float kDrSidMaxAccentGain = 1.00f;

inline float sidSafeUnitParam(float v) noexcept {
    return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
}

inline float sidCanonicalDrSidBusGain(float masterVolumeNorm, float drSidVolumeNorm) noexcept {
    return std::clamp(sidSafeUnitParam(masterVolumeNorm) *
                      sidSafeUnitParam(drSidVolumeNorm) *
                      kDrSidBusMakeupGain,
                      0.0f,
                      kDrSidMaxInternalBusGain);
}


inline float sidDrSidAccentGain(float env, float modWheel, float pressure) noexcept {
    const float safeEnv = std::clamp(std::isfinite(env) ? env : 0.0f, 0.0f, 1.0f);
    const float mw = sidSafeUnitParam(modWheel);
    const float pr = sidSafeUnitParam(pressure);
    return std::clamp(0.18f + 0.66f * safeEnv + 0.08f * mw + 0.08f * pr,
                      0.0f,
                      kDrSidMaxAccentGain);
}

inline float sidDrSidOutputShape(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;

    // DrSID should not constantly color the entire drum bus. Keep normal SID
    // drum body samples linear and only bend peaks above the clean headroom
    // knee, so the shaper acts as a transient guard instead of a permanent
    // saturation stage.
    constexpr float kKnee = kDrSidPostSidHeadroom;
    constexpr float kCeiling = 0.98f;

    const float ax = std::fabs(x);
    if (ax <= kKnee) return x;

    const float range = kCeiling - kKnee;
    const float excess = ax - kKnee;
    const float shaped = kKnee + range * std::tanh(excess / range);
    return std::copysign(std::min(shaped, kCeiling), x);
}

} // namespace ArpSID
