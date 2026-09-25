#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ArpSID {

enum class SidModSource : uint8_t {
    None=0,
    LFO1, LFO2, LFO3, LFO4,
    Velocity,
    NoteNumber,
    KeyFollow,
    ModWheel,
    PitchBend,
    AfterTouch,
    PolyPressure,
    Random,
    Macro1, Macro2, Macro3, Macro4,
    Macro5, Macro6, Macro7, Macro8,
    Env1,
    _Count
};

constexpr int kSidModSourceCount = static_cast<int>(SidModSource::_Count);

inline SidModSource decodeSidModSourceFromIndex(uint8_t idx) noexcept {
    const int safe = std::clamp<int>((int)idx, 0, kSidModSourceCount - 1);
    return static_cast<SidModSource>(safe);
}

// Normalized modulation-source decoding is intentionally UI/presentation-only.
// Runtime/DSP code must persist and route explicit enum ids instead of reconstructing
// source identity from normalized floats.
inline SidModSource decodeSidModSourceFromNormalizedUi(float norm) noexcept {
    if (!std::isfinite(norm)) return SidModSource::None;
    const int maxIdx = std::max(0, kSidModSourceCount - 1);
    const int idx = std::clamp((int)std::lround(std::clamp(norm, 0.0f, 1.0f) * (float)maxIdx), 0, maxIdx);
    return decodeSidModSourceFromIndex(static_cast<uint8_t>(idx));
}

inline uint8_t encodeSidModSourceToIndex(SidModSource src) noexcept {
    const int idx = std::clamp((int)src, 0, kSidModSourceCount - 1);
    return static_cast<uint8_t>(idx);
}

inline float encodeSidModSourceToNormalizedUi(SidModSource src) noexcept {
    if (kSidModSourceCount <= 1) return 0.0f;
    const int idx = std::clamp((int)encodeSidModSourceToIndex(src), 0, kSidModSourceCount - 1);
    return static_cast<float>(idx) / static_cast<float>(kSidModSourceCount - 1);
}

enum class SidModTarget : uint8_t {
    None=0,
    FilterCutoff,
    FilterResonance,
    VCO1Detune, VCO1PulseWidth,
    VCO2Detune, VCO2PulseWidth,
    VCO3Detune, VCO3PulseWidth,
    MasterVolume,
    LFO1Rate, LFO2Rate, LFO3Rate, LFO4Rate,
    _Count
};

constexpr int kSidModTargetCount = static_cast<int>(SidModTarget::_Count);

enum class SidModTransform : uint8_t {
    Linear=0,
    Squared,
    Abs,
    Invert,
};

struct SidModRoute {
    SidModSource source = SidModSource::None;
    SidModTarget target = SidModTarget::None;
    float depth = 0.0f;
    SidModTransform transform = SidModTransform::Linear;
    bool bipolar = true;
    bool enabled = true;

    void sanitize() noexcept {
        if (!std::isfinite(depth)) depth = 0.0f;
        depth = std::clamp(depth, -1.0f, 1.0f);
    }

    bool active() const noexcept {
        return enabled && source != SidModSource::None && target != SidModTarget::None && std::fabs(depth) > 1.0e-5f;
    }
};

} // namespace ArpSID
