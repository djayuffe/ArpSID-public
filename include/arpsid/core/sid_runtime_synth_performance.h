// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_runtime_synth_state.h"
#include "sid_runtime_register_ops.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

template <class VoicesArray, class SidWriteQueueT>
inline void applySynthModePitchBendToVoices(VoicesArray& smVoices,
                                            SidWriteQueueT& sidWriteQueue,
                                            double clockHz,
                                            float bendSemis,
                                            int channel,
                                            uint16_t sampleOffset,
                                            uint16_t cycleOffset) noexcept {
    if (!std::isfinite(bendSemis) || !std::isfinite(clockHz) || clockHz <= 0.0) return;
    const int clampedChannel = std::clamp(channel, 0, 15);
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        auto& v = smVoices[(size_t)i];
        if (!v.active) continue;
        if (v.channel >= 0 && v.channel != clampedChannel) continue;
        // FIX: Compute bent frequency from the voice's BASE midi note, not from the
        // current (possibly mid-glide) frequency register. This layers pitch bend on
        // top of portamento rather than resetting the glide trajectory.
        // v.targetSidFreqReg is the glide destination; apply bend to that note.
        const double bentMidi = static_cast<double>(v.midiNote) + static_cast<double>(bendSemis);
        const uint16_t freqReg = canonicalSidFrequencyRegisterForMidiNote(bentMidi, clockHz);
        // Write to currentSidFreqReg only — do NOT overwrite targetSidFreqReg so the
        // portamento law continues to drive toward the correct destination after bend.
        v.currentSidFreqReg = freqReg;
        canonicalQueueSidVoiceFrequencyWrite(sidWriteQueue, i, freqReg, sampleOffset, cycleOffset);
    }
}

template <class VoicesArray, class SidWriteQueueT>
inline void applySynthModeChannelPressureToVoices(VoicesArray& smVoices,
                                                  SidWriteQueueT& sidWriteQueue,
                                                  int channel,
                                                  float pressure,
                                                  uint16_t sampleOffset,
                                                  uint16_t cycleOffset) noexcept {
    const int clampedChannel = std::clamp(channel, 0, 15);
    const float clampedPressure = std::clamp(pressure, 0.0f, 1.0f);
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        auto& v = smVoices[(size_t)i];
        if (!v.active) continue;
        if (v.channel >= 0 && v.channel != clampedChannel) continue;
        const uint8_t scaledSustain = static_cast<uint8_t>(std::clamp<int>(
            (int)std::lround((float)v.sustainNibble * (0.25f + 0.75f * clampedPressure)), 0, 15));
        const uint8_t sr = static_cast<uint8_t>((scaledSustain << 4) | (v.releaseNibble & 0x0Fu));
        const bool duplicatePressure = v.pressureExplicit &&
            std::fabs(v.pressure - clampedPressure) <= 1.0e-6f;
        const bool duplicateSr = v.lastProjectedPressureSrValid &&
            v.lastProjectedPressureSr == sr;
        v.pressure = clampedPressure;
        v.pressureExplicit = true;
        if (duplicatePressure && duplicateSr) continue;
        v.lastProjectedPressureSr = sr;
        v.lastProjectedPressureSrValid = true;
        const int base = i * 7;
        sidWriteQueue.push((uint8_t)(base + 6), sr, sampleOffset, cycleOffset);
    }
}

template <class VoicesArray, class SidWriteQueueT>
inline void applySynthModePolyPressureToVoices(VoicesArray& smVoices,
                                               SidWriteQueueT& sidWriteQueue,
                                               int channel,
                                               int pitch,
                                               int noteId,
                                               float pressure,
                                               uint16_t sampleOffset,
                                               uint16_t cycleOffset,
                                               uint64_t voiceToken = 0) noexcept {
    const float clampedPressure = std::clamp(pressure, 0.0f, 1.0f);
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        auto& v = smVoices[(size_t)i];
        if (!v.active) continue;
        // Token-first: if caller supplies a token, match by token only.
        if (voiceToken != 0) {
            if (v.voiceToken != voiceToken) continue;
        } else {
            continue;
        }
        const uint8_t scaledSustain = static_cast<uint8_t>(std::clamp<int>(
            (int)std::lround((float)v.sustainNibble * (0.25f + 0.75f * clampedPressure)), 0, 15));
        const uint8_t sr = static_cast<uint8_t>((scaledSustain << 4) | (v.releaseNibble & 0x0Fu));
        const bool duplicatePressure = v.pressureExplicit &&
            std::fabs(v.pressure - clampedPressure) <= 1.0e-6f;
        const bool duplicateSr = v.lastProjectedPressureSrValid &&
            v.lastProjectedPressureSr == sr;
        v.pressure = clampedPressure;
        v.pressureExplicit = true;
        if (duplicatePressure && duplicateSr) continue;
        v.lastProjectedPressureSr = sr;
        v.lastProjectedPressureSrValid = true;
        const int base = i * 7;
        sidWriteQueue.push((uint8_t)(base + 6), sr, sampleOffset, cycleOffset);
        // Do not break on token match: unison / multi-voice synth-mode can legally
        // mirror one canonical token across multiple physical SID voices.
    }
}


template <class VoicePolicyT, class VoicesArray, class HardVoiceOffFn>
inline void applySynthModeSustainPedal(VoicePolicyT& voicePolicy,
                                       VoicesArray& smVoices,
                                       int channel,
                                       bool on,
                                       uint16_t sampleOffset,
                                       uint16_t cycleOffset,
                                       HardVoiceOffFn&& hardVoiceOff) noexcept {
    const int clampedChannel = std::clamp(channel, 0, 15);
    voicePolicy.setSustainPedal(clampedChannel, on);
    if (on) {
        for (auto& v : smVoices) {
            if (!v.active) continue;
            if (v.channel >= 0 && v.channel != clampedChannel) continue;
            v.sustained = true;
        }
        return;
    }
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        auto& v = smVoices[(size_t)i];
        if (!v.active || !v.sustained || v.keyDown) continue;
        if (v.channel >= 0 && v.channel != clampedChannel) continue;
        v.sustained = false;
        hardVoiceOff(i, sampleOffset, cycleOffset, false);
    }
}

template <class VoicePolicyT, class VoicesArray, class HardVoiceOffFn>
inline void applySynthModeSostenutoPedal(VoicePolicyT& voicePolicy,
                                         VoicesArray& smVoices,
                                         int channel,
                                         bool on,
                                         uint16_t sampleOffset,
                                         uint16_t cycleOffset,
                                         HardVoiceOffFn&& hardVoiceOff) noexcept {
    const int clampedChannel = std::clamp(channel, 0, 15);
    voicePolicy.setSostenutoPedal(clampedChannel, on);
    if (on) {
        for (auto& v : smVoices) {
            if (!v.active || !v.keyDown) continue;
            if (v.channel >= 0 && v.channel != clampedChannel) continue;
            v.sostenutoLatched = true;
        }
        return;
    }
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        auto& v = smVoices[(size_t)i];
        if (!v.active || !v.sostenutoLatched || v.keyDown) continue;
        if (v.channel >= 0 && v.channel != clampedChannel) continue;
        v.sostenutoLatched = false;
        hardVoiceOff(i, sampleOffset, cycleOffset, false);
    }
}

} // namespace ArpSID
