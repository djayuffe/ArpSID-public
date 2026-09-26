// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <cstdint>
#include "sid_portamento_law.h"

namespace ArpSID {

// SID hardware has exactly 3 physical voices. SynthMode token ↔ voice-index
// binding is only valid for indices 0..2. Any VoiceEvent with voiceIdx >= 3
// must be clamped or discarded before writing SID registers.
static constexpr int kSidSynthVoiceCount = 3;

inline int canonicalVoiceModeIndexFromNormalized(float norm) noexcept {
    const float clamped = (norm < 0.0f) ? 0.0f : ((norm > 1.0f) ? 1.0f : norm);
    return (int)((clamped * 3.0f) + 0.5f);
}

inline int canonicalUnisonCountFromNormalizedSpread(float spread) noexcept {
    const float clamped = (spread < 0.0f) ? 0.0f : ((spread > 1.0f) ? 1.0f : spread);
    return 1 + static_cast<int>(clamped * 7.0f);
}

inline int requestedUnisonCountFromNormalizedSpread(float spread) noexcept {
    return canonicalUnisonCountFromNormalizedSpread(spread);
}

inline int sidRegProjectedUnisonCount(int requestedCount) noexcept {
    return (requestedCount < 1) ? 1 : ((requestedCount > kSidSynthVoiceCount) ? kSidSynthVoiceCount : requestedCount);
}

inline int sidRegProjectedUnisonCountFromNormalizedSpread(float spread) noexcept {
    return sidRegProjectedUnisonCount(requestedUnisonCountFromNormalizedSpread(spread));
}

struct SynthModeVoiceState {
    int  midiNote = -1;
    int  channel  = -1;
    int  noteId   = -1;
    uint64_t voiceToken = 0;  // canonical token — primary identity truth
    bool active   = false;
    bool keyDown  = false;
    bool sustained = false;
    bool sostenutoLatched = false;
    float age     = 0.0f;
    float pressure = 0.0f;
    bool pressureExplicit = false;
    uint8_t lastProjectedPressureSr = 0;
    bool lastProjectedPressureSrValid = false;
    uint8_t attackNibble = 0;
    uint8_t decayNibble = 0;
    uint8_t sustainNibble = 0;
    uint8_t releaseNibble = 0;
    uint16_t currentSidFreqReg = 0;
    uint16_t targetSidFreqReg = 0;
    DiscreteRegisterGlideState glide{};

    void reset() noexcept {
        midiNote = -1; channel = -1; noteId = -1; voiceToken = 0;
        active = false; keyDown = false;
        sustained = false; sostenutoLatched = false; age = 0.0f;
        pressure = 0.0f; pressureExplicit = false;
        lastProjectedPressureSr = 0; lastProjectedPressureSrValid = false;
        attackNibble = decayNibble = sustainNibble = releaseNibble = 0;
        currentSidFreqReg = 0; targetSidFreqReg = 0;
        glide.reset();
    }
};

using SynthModeVoices3 = std::array<SynthModeVoiceState, kSidSynthVoiceCount>;


} // namespace ArpSID

