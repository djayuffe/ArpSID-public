// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "parameter_ids.h"

namespace ArpSID {

inline float canonicalNormalizedProgramValue(uint8_t program) noexcept {
    return static_cast<float>(program & 0x7F) / 127.0f;
}


inline float canonicalClampedTempo(float bpm, float defaultTempoBpm = 120.0f) noexcept {
    return (std::isfinite(bpm) && bpm > 0.0f) ? bpm : defaultTempoBpm;
}

inline bool canonicalIsValidParamTarget(uint32_t target, uint32_t count) noexcept {
    return target < count;
}

inline float canonicalClampedNormalizedValue(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

} // namespace ArpSID
