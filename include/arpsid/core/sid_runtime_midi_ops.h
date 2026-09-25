#pragma once
#include "sid_event_queue.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

inline int canonicalMidi7FromUnit(float v) noexcept {
    if (!std::isfinite(v)) return 0;
    return std::clamp(static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 127.0f)), 0, 127);
}

inline uint8_t canonicalMidi7FromEventValue(const SidTimedEvent& ev) noexcept {
    return (uint8_t)canonicalMidi7FromUnit(ev.value);
}

inline int canonicalPitchBend14FromEvent(const SidTimedEvent& ev) noexcept {
    if (ev.type == SidTimedEventType::PitchBend)
        return std::clamp(static_cast<int>(ev.data14), 0, 16383);
    if ((ev.value_u32 & ~0x3FFFu) == 0u && ev.value_u32 != 0u)
        return std::clamp(static_cast<int>(ev.value_u32 & 0x3FFFu), 0, 16383);
    const float signedNorm = std::isfinite(ev.value_f32)
                                 ? ev.value_f32
                                 : (std::isfinite(ev.value) ? ev.value : 0.0f);
    const float clamped = std::clamp(signedNorm, -1.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround((clamped * 0.5f + 0.5f) * 16383.0f)), 0, 16383);
}

inline int16_t canonicalPitchBendSigned14FromEvent(const SidTimedEvent& ev) noexcept {
    return (int16_t)std::clamp(canonicalPitchBend14FromEvent(ev) - 8192, -8192, 8191);
}

inline bool canonicalMidiCCIsOn(const SidTimedEvent& ev) noexcept {
    return ev.value >= 0.5f || canonicalMidi7FromEventValue(ev) >= 64;
}

} // namespace ArpSID
